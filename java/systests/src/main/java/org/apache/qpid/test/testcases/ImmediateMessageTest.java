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
package org.apache.qpid.test.testcases;

import org.apache.qpid.test.framework.AMQPPublisher;
import org.apache.qpid.test.framework.Circuit;
import org.apache.qpid.test.framework.FrameworkBaseCase;
import org.apache.qpid.test.framework.MessagingTestConfigProperties;
import static org.apache.qpid.test.framework.MessagingTestConfigProperties.*;
import org.apache.qpid.test.framework.sequencers.CircuitFactory;

import uk.co.thebadgerset.junit.extensions.util.ParsedProperties;
import uk.co.thebadgerset.junit.extensions.util.TestContextProperties;

/**
 * ImmediateMessageTest tests for the desired behaviour of immediate messages. Immediate messages are a non-JMS
 * feature. A message may be marked with an immediate delivery flag, which means that a consumer must be connected
 * to receive the message, through a valid route, when it is sent, or when its transaction is committed in the case
 * of transactional messaging. If this is not the case, the broker should return the message with a NO_CONSUMERS code.
 *
 * <p><table id="crc"><caption>CRC Card</caption>
 * <tr><th> Responsibilities <th> Collaborations
 * <tr><td> Check that an immediate message is sent succesfully not using transactions when a consumer is connected.
 * <tr><td> Check that an immediate message is committed succesfully in a transaction when a consumer is connected.
 * <tr><td> Check that an immediate message results in no consumers code, not using transactions, when a consumer is
 *          disconnected.
 * <tr><td> Check that an immediate message results in no consumers code, in a transaction, when a consumer is
 *          disconnected.
 * <tr><td> Check that an immediate message results in no route code, not using transactions, when no outgoing route is
 *          connected.
 * <tr><td> Check that an immediate message results in no route code, upon transaction commit, when no outgoing route is
 *          connected.
 * <tr><td> Check that an immediate message is sent succesfully not using transactions when a consumer is connected.
 * <tr><td> Check that an immediate message is committed succesfully in a transaction when a consumer is connected.
 * <tr><td> Check that an immediate message results in no consumers code, not using transactions, when a consumer is
 *          disconnected.
 * <tr><td> Check that an immediate message results in no consumers code, in a transaction, when a consumer is
 *          disconnected.
 * <tr><td> Check that an immediate message results in no route code, not using transactions, when no outgoing route is
 *          connected.
 * <tr><td> Check that an immediate message results in no route code, upon transaction commit, when no outgoing route is
 *          connected.
 * </table>
 *
 * @todo All of these test cases will be generated by a test generator that thoroughly tests all combinations of test
 *       circuits.
 */
public class ImmediateMessageTest extends FrameworkBaseCase
{
    /**
     * Creates a new test case with the specified name.
     *
     * @param name The test case name.
     */
    public ImmediateMessageTest(String name)
    {
        super(name);
    }

    /** Check that an immediate message is sent succesfully not using transactions when a consumer is connected. */
    public void test_QPID_517_ImmediateOkNoTxP2P()
    {
        // Ensure transactional sessions are off.
        testProps.setProperty(TRANSACTED_PUBLISHER_PROPNAME, false);
        testProps.setProperty(PUBSUB_PROPNAME, false);

        // Run the default test sequence over the test circuit checking for no errors.
        CircuitFactory circuitFactory = getCircuitFactory();
        Circuit testCircuit = circuitFactory.createCircuit(testProps);

        assertNoFailures(testCircuit.test(1, assertionList(testCircuit.getPublisher().noExceptionsAssertion(testProps))));
    }

    /** Check that an immediate message is committed succesfully in a transaction when a consumer is connected. */
    public void test_QPID_517_ImmediateOkTxP2P()
    {
        // Ensure transactional sessions are off.
        testProps.setProperty(TRANSACTED_PUBLISHER_PROPNAME, true);
        testProps.setProperty(PUBSUB_PROPNAME, false);

        // Send one message with no errors.
        CircuitFactory circuitFactory = getCircuitFactory();
        Circuit testCircuit = circuitFactory.createCircuit(testProps);

        assertNoFailures(testCircuit.test(1, assertionList(testCircuit.getPublisher().noExceptionsAssertion(testProps))));
    }

    /** Check that an immediate message results in no consumers code, not using transactions, when a consumer is disconnected. */
    public void test_QPID_517_ImmediateFailsConsumerDisconnectedNoTxP2P()
    {
        // Ensure transactional sessions are off.
        testProps.setProperty(TRANSACTED_PUBLISHER_PROPNAME, false);
        testProps.setProperty(PUBSUB_PROPNAME, false);

        // Disconnect the consumer.
        testProps.setProperty(RECEIVER_CONSUMER_ACTIVE_PROPNAME, false);

        CircuitFactory circuitFactory = getCircuitFactory();
        Circuit testCircuit = circuitFactory.createCircuit(testProps);

        // Send one message and get a linked no consumers exception.
        assertNoFailures(testCircuit.test(1,
                assertionList(((AMQPPublisher) testCircuit.getPublisher()).noConsumersAssertion(testProps))));
    }

    /** Check that an immediate message results in no consumers code, in a transaction, when a consumer is disconnected. */
    public void test_QPID_517_ImmediateFailsConsumerDisconnectedTxP2P()
    {
        // Ensure transactional sessions are on.
        testProps.setProperty(TRANSACTED_PUBLISHER_PROPNAME, true);
        testProps.setProperty(PUBSUB_PROPNAME, false);

        // Disconnect the consumer.
        testProps.setProperty(RECEIVER_CONSUMER_ACTIVE_PROPNAME, false);

        CircuitFactory circuitFactory = getCircuitFactory();
        Circuit testCircuit = circuitFactory.createCircuit(testProps);

        // Send one message and get a linked no consumers exception.
        assertNoFailures(testCircuit.test(1,
                assertionList(((AMQPPublisher) testCircuit.getPublisher()).noConsumersAssertion(testProps))));
    }

    /** Check that an immediate message results in no route code, not using transactions, when no outgoing route is connected. */
    public void test_QPID_517_ImmediateFailsNoRouteNoTxP2P()
    {
        // Ensure transactional sessions are off.
        testProps.setProperty(TRANSACTED_PUBLISHER_PROPNAME, false);
        testProps.setProperty(PUBSUB_PROPNAME, false);

        // Set up the messaging topology so that only the publishers producer is bound (do not set up the receivers to
        // collect its messages).
        testProps.setProperty(RECEIVER_CONSUMER_BIND_PROPNAME, false);

        // Send one message and get a linked no route exception.
        CircuitFactory circuitFactory = getCircuitFactory();
        Circuit testCircuit = circuitFactory.createCircuit(testProps);

        assertNoFailures(testCircuit.test(1,
                assertionList(((AMQPPublisher) testCircuit.getPublisher()).noRouteAssertion(testProps))));
    }

    /** Check that an immediate message results in no route code, upon transaction commit, when no outgoing route is connected. */
    public void test_QPID_517_ImmediateFailsNoRouteTxP2P()
    {
        // Ensure transactional sessions are on.
        testProps.setProperty(TRANSACTED_PUBLISHER_PROPNAME, true);
        testProps.setProperty(PUBSUB_PROPNAME, false);

        // Set up the messaging topology so that only the publishers producer is bound (do not set up the receivers to
        // collect its messages).
        testProps.setProperty(RECEIVER_CONSUMER_BIND_PROPNAME, false);

        // Send one message and get a linked no route exception.
        CircuitFactory circuitFactory = getCircuitFactory();
        Circuit testCircuit = circuitFactory.createCircuit(testProps);

        assertNoFailures(testCircuit.test(1,
                assertionList(((AMQPPublisher) testCircuit.getPublisher()).noRouteAssertion(testProps))));
    }

    /** Check that an immediate message is sent succesfully not using transactions when a consumer is connected. */
    public void test_QPID_517_ImmediateOkNoTxPubSub()
    {
        // Ensure transactional sessions are off.
        testProps.setProperty(TRANSACTED_PUBLISHER_PROPNAME, false);
        testProps.setProperty(PUBSUB_PROPNAME, true);

        // Send one message with no errors.
        CircuitFactory circuitFactory = getCircuitFactory();
        Circuit testCircuit = circuitFactory.createCircuit(testProps);

        assertNoFailures(testCircuit.test(1,
                assertionList(((AMQPPublisher) testCircuit.getPublisher()).noExceptionsAssertion(testProps))));
    }

    /** Check that an immediate message is committed succesfully in a transaction when a consumer is connected. */
    public void test_QPID_517_ImmediateOkTxPubSub()
    {
        // Ensure transactional sessions are off.
        testProps.setProperty(TRANSACTED_PUBLISHER_PROPNAME, true);
        testProps.setProperty(PUBSUB_PROPNAME, true);

        // Send one message with no errors.
        CircuitFactory circuitFactory = getCircuitFactory();
        Circuit testCircuit = circuitFactory.createCircuit(testProps);

        assertNoFailures(testCircuit.test(1,
                assertionList(((AMQPPublisher) testCircuit.getPublisher()).noExceptionsAssertion(testProps))));
    }

    /** Check that an immediate message results in no consumers code, not using transactions, when a consumer is disconnected. */
    public void test_QPID_517_ImmediateFailsConsumerDisconnectedNoTxPubSub()
    {
        // Ensure transactional sessions are off.
        testProps.setProperty(TRANSACTED_PUBLISHER_PROPNAME, false);
        testProps.setProperty(PUBSUB_PROPNAME, true);

        // Use durable subscriptions, so that the route remains open with no subscribers.
        testProps.setProperty(DURABLE_SUBSCRIPTION_PROPNAME, true);

        // Disconnect the consumer.
        testProps.setProperty(RECEIVER_CONSUMER_ACTIVE_PROPNAME, false);

        CircuitFactory circuitFactory = getCircuitFactory();
        Circuit testCircuit = circuitFactory.createCircuit(testProps);

        // Send one message and get a linked no consumers exception.
        assertNoFailures(testCircuit.test(1,
                assertionList(((AMQPPublisher) testCircuit.getPublisher()).noConsumersAssertion(testProps))));
    }

    /** Check that an immediate message results in no consumers code, in a transaction, when a consumer is disconnected. */
    public void test_QPID_517_ImmediateFailsConsumerDisconnectedTxPubSub()
    {
        // Ensure transactional sessions are on.
        testProps.setProperty(TRANSACTED_PUBLISHER_PROPNAME, true);
        testProps.setProperty(PUBSUB_PROPNAME, true);

        // Use durable subscriptions, so that the route remains open with no subscribers.
        testProps.setProperty(DURABLE_SUBSCRIPTION_PROPNAME, true);

        // Disconnect the consumer.
        testProps.setProperty(RECEIVER_CONSUMER_ACTIVE_PROPNAME, false);

        CircuitFactory circuitFactory = getCircuitFactory();
        Circuit testCircuit = circuitFactory.createCircuit(testProps);

        // Send one message and get a linked no consumers exception.
        assertNoFailures(testCircuit.test(1,
                assertionList(((AMQPPublisher) testCircuit.getPublisher()).noConsumersAssertion(testProps))));
    }

    /** Check that an immediate message results in no route code, not using transactions, when no outgoing route is connected. */
    public void test_QPID_517_ImmediateFailsNoRouteNoTxPubSub()
    {
        // Ensure transactional sessions are off.
        testProps.setProperty(TRANSACTED_PUBLISHER_PROPNAME, false);
        testProps.setProperty(PUBSUB_PROPNAME, true);

        // Set up the messaging topology so that only the publishers producer is bound (do not set up the receivers to
        // collect its messages).
        testProps.setProperty(RECEIVER_CONSUMER_BIND_PROPNAME, false);

        // Send one message and get a linked no route exception.
        CircuitFactory circuitFactory = getCircuitFactory();
        Circuit testCircuit = circuitFactory.createCircuit(testProps);

        assertNoFailures(testCircuit.test(1,
                assertionList(((AMQPPublisher) testCircuit.getPublisher()).noRouteAssertion(testProps))));
    }

    /** Check that an immediate message results in no route code, upon transaction commit, when no outgoing route is connected. */
    public void test_QPID_517_ImmediateFailsNoRouteTxPubSub()
    {
        // Ensure transactional sessions are on.
        testProps.setProperty(TRANSACTED_PUBLISHER_PROPNAME, true);
        testProps.setProperty(PUBSUB_PROPNAME, true);

        // Set up the messaging topology so that only the publishers producer is bound (do not set up the receivers to
        // collect its messages).
        testProps.setProperty(RECEIVER_CONSUMER_BIND_PROPNAME, false);

        // Send one message and get a linked no route exception.
        CircuitFactory circuitFactory = getCircuitFactory();
        Circuit testCircuit = circuitFactory.createCircuit(testProps);

        assertNoFailures(testCircuit.test(1,
                assertionList(((AMQPPublisher) testCircuit.getPublisher()).noRouteAssertion(testProps))));
    }

    protected void setUp() throws Exception
    {
        super.setUp();

        testProps = TestContextProperties.getInstance(MessagingTestConfigProperties.defaults);

        /** All these tests should have the immediate flag on. */
        testProps.setProperty(IMMEDIATE_PROPNAME, true);
        testProps.setProperty(MANDATORY_PROPNAME, false);

        /** Bind the receivers consumer by default. */
        testProps.setProperty(RECEIVER_CONSUMER_BIND_PROPNAME, true);
        testProps.setProperty(RECEIVER_CONSUMER_ACTIVE_PROPNAME, true);
    }
}
