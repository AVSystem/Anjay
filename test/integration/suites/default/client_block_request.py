from framework.lwm2m_test import *
from framework.lwm2m.tlv import TLV
import math
import unittest
import time
import socket

from typing import Optional


class BasicClientBlockRequest:
    class Test(test_suite.Lwm2mSingleServerTest):
        def __init__(self, test_method_name):
            super().__init__(test_method_name)

            self.A_LOT = 256 # arbitratry, big enough to trigger block-wise Update
            self.block_size = 1024 # maximum possible block size
            self.expected_payload_size = None


        def setUp(self):
            super().setUp()

            self.ac_object_instances_str = b','.join(b'</2/%d>' % x for x in range(self.A_LOT))
            self.test_object_instances_str = b','.join(b'</1337/%d>' % x for x in range(1, self.A_LOT + 1))
            self.expected_payload_size = (len(self.ac_object_instances_str)
                                          + len(self.test_object_instances_str)
                                          + 250) # estimated size of other objects
            self.expected_num_blocks = int(math.ceil(self.expected_payload_size / self.block_size))


            for _ in range(self.A_LOT):
                req = Lwm2mCreate('/1337')
                self.serv.send(req)
                self.assertMsgEqual(Lwm2mCreated.matching(req)(),
                                    self.serv.recv())


        def recv(self):
            """
            Receives a single packet. May be overridden in subclasses to perform
            additional actions before/after actual recv.
            """
            return self.serv.recv()

        def block_recv_next(self,
                            expected_seq_num,
                            validate=True):
            req = self.recv()

            if validate:
                has_more = (expected_seq_num < self.expected_num_blocks - 1)
                block_opt = coap.Option.BLOCK1(seq_num=expected_seq_num,
                                               has_more=has_more,
                                               block_size=self.block_size)

                self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT,
                                                query=[],
                                                options=[block_opt]),
                                    req)

            return req

        def block_recv(self,
                       seq_num_begin=0,
                       seq_num_end=None,
                       validate=True,
                       send_ack=None):
            payload = b''
            expected_seq_num = seq_num_begin
            wait_for_more = True

            if send_ack is None:
                send_ack = self.serv.send

            while wait_for_more:
                req = self.block_recv_next(expected_seq_num, validate=validate)
                expected_seq_num += 1
                payload += req.content

                block_opt = req.get_options(coap.Option.BLOCK1)[0]
                if block_opt.has_more():
                    response = Lwm2mContinue.matching(req)(options=[block_opt])
                else:
                    response = Lwm2mChanged.matching(req)(options=[block_opt])

                send_ack(response)

                wait_for_more = (block_opt.has_more() if seq_num_end is None
                                                      else expected_seq_num < seq_num_end)

            return payload

class ClientBlockRequest:
    class Test(BasicClientBlockRequest.Test):
        def __init__(self, test_method_name):
            super().__init__(test_method_name)

            self.expected_payload_size = None
            self.expected_num_blocks = None


        def set_block_size(self, new_block_size):
            self.block_size = new_block_size
            self.expected_num_blocks = int(math.ceil(self.expected_payload_size / self.block_size))


        def setUp(self):
            super().setUp()

            self.communicate('send-update')
            complete_payload = self.block_recv(validate=False)

            # change something in the DM so that next Update includes the list
            # of all instances
            self.serv.send(Lwm2mCreate('/1337'))
            iid = self.serv.recv().get_options(coap.Option.LOCATION_PATH)[-1].content_to_str()

            self.expected_payload_size = len(complete_payload) + len(',</1337/%s>,</2/%s>' % (iid, iid))
            self.expected_num_blocks = int(math.ceil(self.expected_payload_size / self.block_size))


class HumongousUpdateTest(BasicClientBlockRequest.Test):
    def runTest(self):
        self.communicate('send-update')
        complete_payload = self.block_recv()

        self.assertLinkListValid(complete_payload)
        self.assertIn(self.ac_object_instances_str, complete_payload)
        self.assertIn(self.test_object_instances_str, complete_payload)

        with self.assertRaises(socket.timeout, msg="client did not accept Update response"):
            self.serv.recv(timeout_s=3)


class HumongousUpdateWithSeparateResponseTest(BasicClientBlockRequest.Test):
    def send_separate_ack(self, msg):
        self.assertEqual(coap.Type.ACKNOWLEDGEMENT, msg.type,
                         "incorrect usage of SeparateResponseTest.send, it's "
                         "supposed to only be used for ACKs to block-wise "
                         "Update")

        separate_ack = Lwm2mEmpty.matching(msg)()
        self.serv.send(separate_ack)

        msg.type = coap.Type.CONFIRMABLE
        msg.msg_id = ANY
        self.serv.send(msg)

        self.assertMsgEqual(Lwm2mEmpty.matching(msg)(),
                            self.recv())

    def runTest(self):
        self.communicate('send-update')
        complete_payload = self.block_recv(send_ack=self.send_separate_ack)

        self.assertLinkListValid(complete_payload)
        self.assertIn(self.ac_object_instances_str, complete_payload)
        self.assertIn(self.test_object_instances_str, complete_payload)

        with self.assertRaises(socket.timeout, msg="client did not accept Update response"):
            self.serv.recv(timeout_s=3)


class HumongousUpdateWithNonConfirmableSeparateResponseTest(BasicClientBlockRequest.Test):
    def send_separate_ack(self, msg):
        self.assertEqual(coap.Type.ACKNOWLEDGEMENT, msg.type,
                         "incorrect usage of SeparateResponseTest.send, it's "
                         "supposed to only be used for ACKs to block-wise "
                         "Update")

        separate_ack = Lwm2mEmpty.matching(msg)()
        self.serv.send(separate_ack)

        msg.type = coap.Type.NON_CONFIRMABLE
        msg.msg_id = ANY
        self.serv.send(msg)

    def runTest(self):
        self.communicate('send-update')
        complete_payload = self.block_recv(send_ack=self.send_separate_ack)

        self.assertLinkListValid(complete_payload)
        self.assertIn(self.ac_object_instances_str, complete_payload)
        self.assertIn(self.test_object_instances_str, complete_payload)

        with self.assertRaises(socket.timeout, msg="client did not accept Update response"):
            self.serv.recv(timeout_s=3)


class ResetResponseToFirstRequestBlock(ClientBlockRequest.Test):
    def runTest(self):
        self.communicate('send-update')

        req = self.block_recv_next(expected_seq_num=0)
        self.serv.send(Lwm2mReset.matching(req)())
        # client should abort


class ResetResponseToIntermediateRequestBlock(ClientBlockRequest.Test):
    def runTest(self):
        self.communicate('send-update')

        self.block_recv(seq_num_begin=0,
                        seq_num_end=(self.expected_num_blocks // 2))

        req = self.block_recv_next(expected_seq_num=(self.expected_num_blocks // 2))
        self.serv.send(Lwm2mReset.matching(req)())
        # client should abort


class ResetResponseToLastRequestBlock(ClientBlockRequest.Test):
    def runTest(self):
        self.communicate('send-update')

        self.block_recv(seq_num_begin=0,
                        seq_num_end=(self.expected_num_blocks - 1))

        req = self.block_recv_next(expected_seq_num=(self.expected_num_blocks - 1))
        self.serv.send(Lwm2mReset.matching(req)())
        # client should abort


class CoapErrorResponseToFirstRequestBlock(ClientBlockRequest.Test):
    def runTest(self):
        self.communicate('send-update')

        req = self.block_recv_next(expected_seq_num=0)
        self.serv.send(Lwm2mErrorResponse.matching(req)(coap.Code.RES_INTERNAL_SERVER_ERROR))
        # client should abort


class CoapErrorResponseToIntermediateRequestBlock(ClientBlockRequest.Test):
    def runTest(self):
        self.communicate('send-update')

        self.block_recv(seq_num_begin=0,
                        seq_num_end=(self.expected_num_blocks // 2))

        req = self.block_recv_next(expected_seq_num=(self.expected_num_blocks // 2))
        self.serv.send(Lwm2mErrorResponse.matching(req)(coap.Code.RES_INTERNAL_SERVER_ERROR))
        # client should abort


class CoapErrorResponseToLastRequestBlock(ClientBlockRequest.Test):
    def runTest(self):
        self.communicate('send-update')

        self.block_recv(seq_num_begin=0,
                        seq_num_end=(self.expected_num_blocks - 1))

        req = self.block_recv_next(expected_seq_num=(self.expected_num_blocks - 1))
        self.serv.send(Lwm2mErrorResponse.matching(req)(coap.Code.RES_INTERNAL_SERVER_ERROR))
        # client should abort


# IcmpErrorResponse* tests flow looks like this:
# 1. Force update.
# 2. Either receive part of the update or not.
# 3. Close Lwm2mServer socket.
# 4. Sleep for some time, hoping that the demo will realize server socket
#    is closed and a retry should be scheduled.
# 5. Restart server and receive update.
#
# Now, 4 seconds is enough to make demo realize that server really stopped
# running. Sleeping for shorter amounts of time might cause race condition,
# and in effect test failure:
# 1. as above
# 2. as above
# 3. During closing the socket another block message has been received,
#    there is no answer from the server though due to state it is currently in,
#    therefore recv() in demo reports timeout, which will cause retransmission
#    after at most 3 seconds.
# 4. Server restarts.
# 5. Retransmitted packet hits the server, and an error occurs because
#    we do not expect retransmitted block.
ICMP_ERROR_RESPONSE_SLEEP_SECODNS = 4

class IcmpErrorResponseToFirstRequestBlock(ClientBlockRequest.Test):
    def runTest(self):
        listen_port = self.serv.get_listen_port()

        self.communicate('send-update')
        self.serv.close()
        # client should abort and retry update in a while

        time.sleep(ICMP_ERROR_RESPONSE_SLEEP_SECODNS)
        self.serv = Lwm2mServer(listen_port=listen_port)
        self.block_recv()


class IcmpErrorResponseToIntermediateRequestBlock(ClientBlockRequest.Test):
    def runTest(self):
        listen_port = self.serv.get_listen_port()

        self.communicate('send-update')
        self.block_recv(seq_num_begin=0,
                        seq_num_end=(self.expected_num_blocks // 2))
        self.serv.close()
        # client should abort and retry update in a while

        time.sleep(ICMP_ERROR_RESPONSE_SLEEP_SECODNS)
        self.serv = Lwm2mServer(listen_port=listen_port)
        self.block_recv()


class IcmpErrorResponseToLastRequestBlock(ClientBlockRequest.Test):
    def runTest(self):
        listen_port = self.serv.get_listen_port()

        self.communicate('send-update')
        self.block_recv(seq_num_begin=0,
                        seq_num_end=(self.expected_num_blocks - 1))
        self.serv.close()
        # client should abort and retry update in a while

        time.sleep(ICMP_ERROR_RESPONSE_SLEEP_SECODNS)
        self.serv = Lwm2mServer(listen_port=listen_port)
        self.block_recv()


class NoResponseAfterFirstRequestBlock(ClientBlockRequest.Test):
    def runTest(self):
        self.communicate('send-update')

        self.block_recv_next(expected_seq_num=0)
        # ignore packet - client should retry

        self.serv.set_timeout(timeout_s=5)
        self.block_recv()


class NoResponseAfterIntermediateRequestBlock(ClientBlockRequest.Test):
    def runTest(self):
        self.communicate('send-update')

        self.block_recv(seq_num_begin=0,
                        seq_num_end=(self.expected_num_blocks // 2))

        self.block_recv_next(expected_seq_num=(self.expected_num_blocks // 2))
        # ignore packet - client should retry

        self.serv.set_timeout(timeout_s=5)
        self.block_recv(seq_num_begin=(self.expected_num_blocks // 2))


class NoResponseAfterLastRequestBlock(ClientBlockRequest.Test):
    def runTest(self):
        self.communicate('send-update')

        self.block_recv(seq_num_begin=0,
                        seq_num_end=(self.expected_num_blocks - 1))

        self.block_recv_next(expected_seq_num=(self.expected_num_blocks - 1))
        # ignore packet - client should retry

        self.serv.set_timeout(timeout_s=5)
        self.block_recv(seq_num_begin=(self.expected_num_blocks - 1))


class BlockSizeRenegotiation(ClientBlockRequest.Test):
    def runTest(self):
        # blocks 2+ should use reduced block size
        self.communicate('send-update')

        req = self.block_recv_next(expected_seq_num=0)
        block1 = req.get_options(coap.Option.BLOCK1)[0]

        new_block_size = 16
        self.assertNotEqual(new_block_size, block1.block_size)

        # request new block size in Continue message
        block_opt = coap.Option.BLOCK1(seq_num=0, has_more=1, block_size=new_block_size)
        self.serv.send(Lwm2mContinue.matching(req)(options=[block_opt]))

        # change block size for future packets
        block_size_ratio = self.block_size // new_block_size
        self.set_block_size(new_block_size)

        # receive remaining packets
        self.block_recv(seq_num_begin=block_size_ratio)


class BlockSizeRenegotiationInTheMiddleOfTransfer(ClientBlockRequest.Test):
    def runTest(self):
        # blocks 2+ should use reduced block size
        self.communicate('send-update')

        self.block_recv(seq_num_begin=0,
                        seq_num_end=(self.expected_num_blocks // 2))

        req = self.block_recv_next(expected_seq_num=(self.expected_num_blocks // 2))
        block1 = req.get_options(coap.Option.BLOCK1)[0]

        new_block_size = 16
        self.assertNotEqual(new_block_size, block1.block_size)

        # request new block size in Continue message
        block_opt = coap.Option.BLOCK1(seq_num=block1.seq_num(),
                                       has_more=1,
                                       block_size=new_block_size)
        self.serv.send(Lwm2mContinue.matching(req)(options=[block_opt]))

        # client should get confused and abort the transfer, retrying after a while
        time.sleep(1)
        self.block_recv()


class MismatchedResetWhileBlockRequestInProgress(ClientBlockRequest.Test):
    def runTest(self):
        self.communicate('send-update')

        self.block_recv(seq_num_begin=0,
                        seq_num_end=(self.expected_num_blocks // 2))

        req = self.block_recv_next(expected_seq_num=(self.expected_num_blocks // 2))

        # Reset with mismatched msg_id should be ignored
        self.serv.send(Lwm2mReset(msg_id=(req.msg_id+1)))

        # transfer should continue after receiving the correct response
        self.serv.send(Lwm2mContinue.matching(req)(options=req.get_options(coap.Option.BLOCK1)))
        self.block_recv(seq_num_begin=(self.expected_num_blocks // 2 + 1))


@unittest.skip("TODO: what should client do here?")
class UnexpectedServerRequestWhileBlockRequestInProgress(ClientBlockRequest.Test):
    def runTest(self):
        pass
