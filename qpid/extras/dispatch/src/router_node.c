/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * 
 *   http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <stdio.h>
#include <string.h>
#include <qpid/dispatch.h>
#include "dispatch_private.h"

static char *module = "ROUTER";

//static char *local_prefix = "_local/";
//static char *topo_prefix  = "_topo/";

/**
 * Address Types and Processing:
 *
 *   Address                              Hash Key       onReceive         onEmit
 *   =============================================================================
 *   _local/<local>                       L<local>       handler           forward
 *   _topo/<area>/<router>/<local>        A<area>        forward           forward
 *   _topo/<my-area>/<router>/<local>     R<router>      forward           forward
 *   _topo/<my-area>/<my-router>/<local>  L<local>       forward+handler   forward
 *   _topo/<area>/all/<local>             A<area>        forward           forward
 *   _topo/<my-area>/all/<local>          L<local>       forward+handler   forward
 *   _topo/all/all/<local>                L<local>       forward+handler   forward
 *   <mobile>                             M<mobile>      forward+handler   forward
 */

struct dx_router_t {
    dx_dispatch_t      *dx;
    const char         *router_area;
    const char         *router_id;
    dx_node_t          *node;
    dx_link_list_t      in_links;
    dx_link_list_t      out_links;
    dx_message_list_t   in_fifo;
    sys_mutex_t        *lock;
    dx_timer_t         *timer;
    hash_t             *out_hash;
    uint64_t            dtag;
};


typedef struct {
    dx_link_t         *link;
    dx_message_list_t  out_fifo;
} dx_router_link_t;

ALLOC_DECLARE(dx_router_link_t);
ALLOC_DEFINE(dx_router_link_t);


typedef struct {
    const char       *id;
    dx_router_link_t *next_hop;
    // list of valid origins (pointers to router_node) - (bit masks?)
} dx_router_node_t;

ALLOC_DECLARE(dx_router_node_t);
ALLOC_DEFINE(dx_router_node_t);


struct dx_address_t {
    int                   is_local;
    dx_router_message_cb  handler;           // In-Process Consumer
    void                 *handler_context;
    dx_router_link_t     *rlink;             // Locally-Connected Consumer  - TODO: Make this a list
    dx_router_node_t     *rnode;             // Remotely-Connected Consumer - TODO: Make this a list
};

ALLOC_DECLARE(dx_address_t);
ALLOC_DEFINE(dx_address_t);


/**
 * Outbound Delivery Handler
 */
static void router_tx_handler(void* context, dx_link_t *link, pn_delivery_t *delivery)
{
    dx_router_t      *router  = (dx_router_t*) context;
    pn_link_t        *pn_link = pn_delivery_link(delivery);
    dx_router_link_t *rlink   = (dx_router_link_t*) dx_link_get_context(link);
    dx_message_t     *msg;
    size_t            size;

    sys_mutex_lock(router->lock);
    msg = DEQ_HEAD(rlink->out_fifo);
    if (!msg) {
        // TODO - Recind the delivery
        sys_mutex_unlock(router->lock);
        return;
    }

    DEQ_REMOVE_HEAD(rlink->out_fifo);
    size = (DEQ_SIZE(rlink->out_fifo));
    sys_mutex_unlock(router->lock);

    dx_message_send(msg, pn_link);

    //
    // If there is no incoming delivery, it was pre-settled.  In this case,
    // we must pre-settle the outgoing delivery as well.
    //
    if (dx_message_in_delivery(msg)) {
        pn_delivery_set_context(delivery, (void*) msg);
        dx_message_set_out_delivery(msg, delivery);
    } else {
        pn_delivery_settle(delivery);
        dx_free_message(msg);
    }

    pn_link_advance(pn_link);
    pn_link_offered(pn_link, size);
}


/**
 * Inbound Delivery Handler
 */
static void router_rx_handler(void* context, dx_link_t *link, pn_delivery_t *delivery)
{
    dx_router_t  *router  = (dx_router_t*) context;
    pn_link_t    *pn_link = pn_delivery_link(delivery);
    dx_message_t *msg;
    int           valid_message = 0;

    //
    // Receive the message into a local representation.  If the returned message
    // pointer is NULL, we have not yet received a complete message.
    //
    sys_mutex_lock(router->lock);
    msg = dx_message_receive(delivery);
    sys_mutex_unlock(router->lock);

    if (!msg)
        return;

    //
    // Validate the message through the Properties section
    //
    valid_message = dx_message_check(msg, DX_DEPTH_PROPERTIES);

    pn_link_advance(pn_link);
    pn_link_flow(pn_link, 1);

    if (valid_message) {
        dx_field_iterator_t *iter = dx_message_field_iterator(msg, DX_FIELD_TO);
        dx_address_t        *addr;
        if (iter) {
            dx_field_iterator_reset_view(iter, ITER_VIEW_ADDRESS_HASH);
            sys_mutex_lock(router->lock);
            hash_retrieve(router->out_hash, iter, (void*) &addr);
            dx_field_iterator_free(iter);

            if (addr) {
                //
                // To field is valid and contains a known destination.  Handle the various
                // cases for forwarding.
                //
                // Forward to the in-process handler for this message if there is one.
                // Note: If the handler is going to queue the message for deferred processing,
                //       it must copy the message.  This function assumes that the handler
                //       will process the message synchronously and be finished with it upon
                //       completion.
                //
                if (addr->handler)
                    addr->handler(addr->handler_context, msg);

                //
                // Forward to the local link for the locally-connected consumer, if present.
                // TODO - Don't forward if this is a "_local" address.
                //
                if (addr->rlink) {
                    pn_link_t    *pn_outlink = dx_link_pn(addr->rlink->link);
                    dx_message_t *copy       = dx_message_copy(msg);
                    DEQ_INSERT_TAIL(addr->rlink->out_fifo, copy);
                    pn_link_offered(pn_outlink, DEQ_SIZE(addr->rlink->out_fifo));
                    dx_link_activate(addr->rlink->link);
                }

                //
                // Forward to the next-hop for a remotely-connected consumer, if present.
                // Don't forward if this is a "_local" address.
                //
                if (addr->rnode) {
                    // TODO
                }

            } else {
                //
                // To field contains an unknown address.  Release the message.
                //
                pn_delivery_update(delivery, PN_RELEASED);
                pn_delivery_settle(delivery);
            }

            dx_free_message(msg);
            sys_mutex_unlock(router->lock);
        }
    } else {
        //
        // Message is invalid.  Reject the message.
        //
        pn_delivery_update(delivery, PN_REJECTED);
        pn_delivery_settle(delivery);
        pn_delivery_set_context(delivery, 0);
        dx_free_message(msg);
    }
}


/**
 * Delivery Disposition Handler
 */
static void router_disp_handler(void* context, dx_link_t *link, pn_delivery_t *delivery)
{
    pn_link_t *pn_link = pn_delivery_link(delivery);

    if (pn_link_is_sender(pn_link)) {
        pn_disposition_t  disp     = pn_delivery_remote_state(delivery);
        dx_message_t     *msg      = pn_delivery_get_context(delivery);
        pn_delivery_t    *activate = 0;

        if (msg) {
            assert(delivery == dx_message_out_delivery(msg));
            if (disp != 0) {
                activate = dx_message_in_delivery(msg);
                pn_delivery_update(activate, disp);
                // TODO - handling of the data accompanying RECEIVED/MODIFIED
            }

            if (pn_delivery_settled(delivery)) {
                //
                // Downstream delivery has been settled.  Propagate the settlement
                // upstream.
                //
                activate = dx_message_in_delivery(msg);
                pn_delivery_settle(activate);
                pn_delivery_settle(delivery);
                dx_free_message(msg);
            }

            if (activate) {
                //
                // Activate the upstream/incoming link so that the settlement will
                // get pushed out.
                //
                dx_link_t *act_link = (dx_link_t*) pn_link_get_context(pn_delivery_link(activate));
                dx_link_activate(act_link);
            }

            return;
        }
    } else {
        // TODO - Handle disposition updates from upstream
    }
}


/**
 * New Incoming Link Handler
 */
static int router_incoming_link_handler(void* context, dx_link_t *link)
{
    dx_router_t    *router  = (dx_router_t*) context;
    dx_link_item_t *item    = new_dx_link_item_t();
    pn_link_t      *pn_link = dx_link_pn(link);

    if (item) {
        DEQ_ITEM_INIT(item);
        item->link = link;

        sys_mutex_lock(router->lock);
        DEQ_INSERT_TAIL(router->in_links, item);
        sys_mutex_unlock(router->lock);

        pn_terminus_copy(pn_link_source(pn_link), pn_link_remote_source(pn_link));
        pn_terminus_copy(pn_link_target(pn_link), pn_link_remote_target(pn_link));
        pn_link_flow(pn_link, 32);
        pn_link_open(pn_link);
    } else {
        pn_link_close(pn_link);
    }
    return 0;
}


/**
 * New Outgoing Link Handler
 */
static int router_outgoing_link_handler(void* context, dx_link_t *link)
{
    dx_router_t *router  = (dx_router_t*) context;
    pn_link_t   *pn_link = dx_link_pn(link);
    const char  *r_tgt   = pn_terminus_get_address(pn_link_remote_target(pn_link));

    if (!r_tgt) {
        pn_link_close(pn_link);
        return 0;
    }

    dx_router_link_t *rlink = new_dx_router_link_t();
    rlink->link = link;
    DEQ_INIT(rlink->out_fifo);
    dx_link_set_context(link, rlink);

    dx_address_t *addr;

    dx_field_iterator_t *iter = dx_field_iterator_string(r_tgt, ITER_VIEW_ADDRESS_HASH);

    sys_mutex_lock(router->lock);
    hash_retrieve(router->out_hash, iter, (void**) &addr);
    if (!addr) {
        addr = new_dx_address_t();
        addr->is_local        = 0;
        addr->handler         = 0;
        addr->handler_context = 0;
        addr->rlink           = 0;
        addr->rnode           = 0;
        hash_insert(router->out_hash, iter, addr);
    }
    dx_field_iterator_free(iter);

    if (addr->rlink == 0) {
        addr->rlink = rlink;
        pn_terminus_copy(pn_link_source(pn_link), pn_link_remote_source(pn_link));
        pn_terminus_copy(pn_link_target(pn_link), pn_link_remote_target(pn_link));
        pn_link_open(pn_link);
        sys_mutex_unlock(router->lock);
        dx_log(module, LOG_TRACE, "Registered new local address: %s", r_tgt);
        return 0;
    }

    dx_log(module, LOG_TRACE, "Address '%s' not registered as it already exists", r_tgt);
    pn_link_close(pn_link);
    sys_mutex_unlock(router->lock);
    return 0;
}


/**
 * Outgoing Link Writable Handler
 */
static int router_writable_link_handler(void* context, dx_link_t *link)
{
    dx_router_t      *router = (dx_router_t*) context;
    int               grant_delivery = 0;
    pn_delivery_t    *delivery;
    dx_router_link_t *rlink = (dx_router_link_t*) dx_link_get_context(link);
    pn_link_t        *pn_link = dx_link_pn(link);
    uint64_t          tag;

    sys_mutex_lock(router->lock);
    if (DEQ_SIZE(rlink->out_fifo) > 0) {
        grant_delivery = 1;
        tag = router->dtag++;
    }
    sys_mutex_unlock(router->lock);

    if (grant_delivery) {
        pn_delivery(pn_link, pn_dtag((char*) &tag, 8));
        delivery = pn_link_current(pn_link);
        if (delivery) {
            router_tx_handler(context, link, delivery);
            return 1;
        }
    }

    return 0;
}


/**
 * Link Detached Handler
 */
static int router_link_detach_handler(void* context, dx_link_t *link, int closed)
{
    dx_router_t    *router  = (dx_router_t*) context;
    pn_link_t      *pn_link = dx_link_pn(link);
    const char     *r_tgt   = pn_terminus_get_address(pn_link_remote_target(pn_link));
    dx_link_item_t *item;

    if (!r_tgt)
        return 0;

    sys_mutex_lock(router->lock);
    if (pn_link_is_sender(pn_link)) {
        item = DEQ_HEAD(router->out_links);

        dx_field_iterator_t *iter = dx_field_iterator_string(r_tgt, ITER_VIEW_ADDRESS_HASH);
        dx_address_t        *addr;
        if (iter) {
            hash_retrieve(router->out_hash, iter, (void**) &addr);
            if (addr) {
                hash_remove(router->out_hash, iter);
                free_dx_router_link_t(addr->rlink);
                free_dx_address_t(addr);
                dx_log(module, LOG_TRACE, "Removed local address: %s", r_tgt);
            }
            dx_field_iterator_free(iter);
        }
    }
    else
        item = DEQ_HEAD(router->in_links);

    while (item) {
        if (item->link == link) {
            if (pn_link_is_sender(pn_link))
                DEQ_REMOVE(router->out_links, item);
            else
                DEQ_REMOVE(router->in_links, item);
            free_dx_link_item_t(item);
            break;
        }
        item = item->next;
    }

    sys_mutex_unlock(router->lock);
    return 0;
}


static void router_inbound_open_handler(void *type_context, dx_connection_t *conn)
{
}


static void router_outbound_open_handler(void *type_context, dx_connection_t *conn)
{
}


static void dx_router_timer_handler(void *context)
{
    dx_router_t *router = (dx_router_t*) context;

    //
    // Periodic processing.
    //
    dx_timer_schedule(router->timer, 1000);
}


static dx_node_type_t router_node = {"router", 0, 0,
                                     router_rx_handler,
                                     router_tx_handler,
                                     router_disp_handler,
                                     router_incoming_link_handler,
                                     router_outgoing_link_handler,
                                     router_writable_link_handler,
                                     router_link_detach_handler,
                                     0,   // node_created_handler
                                     0,   // node_destroyed_handler
                                     router_inbound_open_handler,
                                     router_outbound_open_handler };
static int type_registered = 0;


dx_router_t *dx_router(dx_dispatch_t *dx, const char *area, const char *id)
{
    if (!type_registered) {
        type_registered = 1;
        dx_container_register_node_type(dx, &router_node);
    }

    dx_router_t *router = NEW(dx_router_t);
    dx_container_set_default_node_type(dx, &router_node, (void*) router, DX_DIST_BOTH);

    DEQ_INIT(router->in_links);
    DEQ_INIT(router->out_links);
    DEQ_INIT(router->in_fifo);

    router->dx          = dx;
    router->lock        = sys_mutex();
    router->router_area = area;
    router->router_id   = id;

    router->timer = dx_timer(dx, dx_router_timer_handler, (void*) router);
    dx_timer_schedule(router->timer, 0); // Immediate

    router->out_hash = hash(10, 32, 0);
    router->dtag = 1;

    //
    // Inform the field iterator module of this router's id and area.  The field iterator
    // uses this to offload some of the address-processing load from the router.
    //
    dx_field_iterator_set_address(area, id);

    return router;
}


void dx_router_setup_agent(dx_dispatch_t *dx)
{
    // TODO
}


void dx_router_free(dx_router_t *router)
{
    dx_container_set_default_node_type(router->dx, 0, 0, DX_DIST_BOTH);
    sys_mutex_free(router->lock);
    free(router);
}


dx_address_t *dx_router_register_address(dx_dispatch_t        *dx,
                                         bool                  is_local,
                                         const char           *address,
                                         dx_router_message_cb  handler,
                                         void                 *context)
{
    char                 addr[1000];
    dx_address_t        *ad = new_dx_address_t();
    dx_field_iterator_t *iter;
    int                  result;

    if (!ad)
        return 0;

    ad->is_local        = is_local;
    ad->handler         = handler;
    ad->handler_context = context;
    ad->rlink           = 0;

    if (ad->is_local)
        strcpy(addr, "L");  // Local Hash-Key Space
    else
        strcpy(addr, "M");  // Mobile Hash-Key Space

    strcat(addr, address);
    iter = dx_field_iterator_string(addr, ITER_VIEW_NO_HOST);
    result = hash_insert(dx->router->out_hash, iter, ad);
    dx_field_iterator_free(iter);
    if (result != 0) {
        free_dx_address_t(ad);
        return 0;
    }

    dx_log(module, LOG_TRACE, "In-Process Address Registered: %s", address);
    return ad;
}


void dx_router_unregister_address(dx_address_t *ad)
{
    free_dx_address_t(ad);
}


void dx_router_send(dx_dispatch_t *dx,
                    const char    *address,
                    dx_message_t  *msg)
{
}

