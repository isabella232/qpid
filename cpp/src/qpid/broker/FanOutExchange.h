/*
 *
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
 *
 */
#ifndef _FanOutExchange_
#define _FanOutExchange_

#include <map>
#include <vector>
#include "qpid/broker/BrokerImportExport.h"
#include "qpid/broker/Exchange.h"
#include "qpid/framing/FieldTable.h"
#include "qpid/sys/CopyOnWriteArray.h"
#include "qpid/broker/Queue.h"

namespace qpid {
namespace broker {

class FanOutExchange : public virtual Exchange {
    typedef qpid::sys::CopyOnWriteArray<Binding::shared_ptr> BindingsArray;
    BindingsArray bindings;
    FedBinding fedBinding;
  public:
    static const std::string typeName;
        
    QPID_BROKER_EXTERN FanOutExchange(const std::string& name,
                                      management::Manageable* parent = 0, Broker* broker = 0);
    QPID_BROKER_EXTERN FanOutExchange(const std::string& _name,
                                      bool _durable, 
                                      const qpid::framing::FieldTable& _args,
                                      management::Manageable* parent = 0, Broker* broker = 0);

    virtual std::string getType() const { return typeName; }            
        
    QPID_BROKER_EXTERN virtual bool bind(Queue::shared_ptr queue,
                                         const std::string& routingKey,
                                         const qpid::framing::FieldTable* args);

    virtual bool unbind(Queue::shared_ptr queue, const std::string& routingKey, const qpid::framing::FieldTable* args);

    QPID_BROKER_EXTERN virtual void route(Deliverable& msg);

    QPID_BROKER_EXTERN virtual bool isBound(Queue::shared_ptr queue,
                                            const std::string* const routingKey,
                                            const qpid::framing::FieldTable* const args);

    QPID_BROKER_EXTERN virtual ~FanOutExchange();
    virtual bool supportsDynamicBinding() { return true; }

    // DataSource interface - used to write persistence data to async store
    uint64_t getSize();
    void write(char* target);

};

}
}



#endif
