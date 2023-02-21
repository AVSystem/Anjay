# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

import itertools

from framework.lwm2m_test import *
from framework.test_utils import *
from . import retransmissions


class Send:
    class Test(test_suite.Lwm2mSingleServerTest,
               test_suite.Lwm2mDmOperations):
        def setUp(self, *args, minimum_version='1.1', maximum_version='1.1', **kwargs):
            super().setUp(*args, minimum_version=minimum_version, maximum_version=maximum_version,
                          **kwargs)

        def block_recv(self):
            blocks = []
            for seq_num in itertools.count(start=0, step=1):
                pkt = self.serv.recv()

                block1_opts = pkt.get_options(coap.Option.BLOCK1)
                block1_opt = block1_opts[0]
                self.assertEqual(len(block1_opts), 1, msg='BLOCK1 option %s' % (
                    'duplicated' if block1_opts else 'not found',))
                self.assertMsgEqual(Lwm2mSend(options=[block1_opt]), pkt)

                blocks.append(pkt)

                if block1_opt.has_more():
                    self.serv.send(Lwm2mContinue.matching(pkt)(options=[block1_opt]))
                else:
                    break

            return blocks


class SendFromDataModelTest(Send.Test):
    def runTest(self):
        self.communicate('send 1 %s' % (ResPath.Device.ModelNumber,))
        pkt = self.serv.recv()

        self.assertMsgEqual(Lwm2mSend(), pkt)
        self.serv.send(Lwm2mChanged.matching(pkt)())

        cbor = CBOR.parse(pkt.content)
        cbor.verify_values(test=self,
                           expected_value_map={
                               ResPath.Device.ModelNumber: 'demo-client'
                           })


class TrySendSecurityObjectResource(SendFromDataModelTest):
    def runTest(self):
        self.communicate('send 1 %s' % (ResPath.Security[1].ServerURI))

        # The command above should fail and nothing should arrive, execute
        # valid send command to check if only one message arrives.
        super().runTest()


class SendFromDataModelTestTwoResources(Send.Test):
    def runTest(self):
        self.communicate(
            'send 1 %s %s' %
            (ResPath.Device.ModelNumber,
             ResPath.Server[1].Lifetime))
        pkt = self.serv.recv()

        self.assertMsgEqual(Lwm2mSend(), pkt)
        self.serv.send(Lwm2mChanged.matching(pkt)())

        cbor = CBOR.parse(pkt.content)

        self.assertIsNone(cbor[0].get(SenmlLabel.BASE_NAME))
        self.assertIsNotNone(cbor[0].get(SenmlLabel.BASE_TIME))
        self.assertIsNone(cbor[0].get(SenmlLabel.TIME))

        self.assertIsNone(cbor[1].get(SenmlLabel.BASE_NAME))
        self.assertIsNone(cbor[1].get(SenmlLabel.BASE_TIME))
        self.assertIsNone(cbor[1].get(SenmlLabel.TIME))

        cbor.verify_values(test=self,
                           expected_value_map={
                               ResPath.Device.ModelNumber: 'demo-client',
                               ResPath.Server[1].Lifetime: 86400
                           })


class SendFromDataModelTestTwoResourcesWithCommonPrefix(Send.Test):
    def runTest(self):
        self.communicate(
            'send 1 %s %s' %
            (ResPath.Device.ModelNumber,
             ResPath.Device.Manufacturer))
        pkt = self.serv.recv()

        self.assertMsgEqual(Lwm2mSend(), pkt)
        self.serv.send(Lwm2mChanged.matching(pkt)())

        cbor = CBOR.parse(pkt.content)

        self.assertEqual(cbor[0].get(SenmlLabel.BASE_NAME), '/3/0')
        self.assertIsNotNone(cbor[0].get(SenmlLabel.BASE_TIME))
        self.assertIsNone(cbor[0].get(SenmlLabel.TIME))

        self.assertIsNone(cbor[1].get(SenmlLabel.BASE_NAME))
        self.assertIsNone(cbor[1].get(SenmlLabel.BASE_TIME))
        self.assertIsNone(cbor[1].get(SenmlLabel.TIME))

        cbor.verify_values(test=self,
                           expected_value_map={
                               ResPath.Device.ModelNumber: 'demo-client',
                               ResPath.Device.Manufacturer: '0023C7'
                           })


class SendBlockTest(Send.Test):
    def runTest(self):
        self.create_instance(self.serv, oid=OID.Test, iid=0)
        self.write_resource(self.serv, oid=OID.Test, iid=0, rid=RID.Test.ResBytesSize,
                            content=b'1024')
        self.communicate('send 1 %s' % (ResPath.Test[0].ResBytes,))

        blocks = self.block_recv()
        self.serv.send(Lwm2mChanged.matching(blocks[-1])())

        cbor = CBOR.parse(b''.join(p.content for p in blocks))
        cbor.verify_values(test=self,
                           expected_value_map={
                               ResPath.Test[0].ResBytes: bytes(b % 128 for b in range(1024))
                           })


class MuteSend(Send.Test):
    def runTest(self):
        self.communicate('send 1 %s' % ResPath.Device.ModelNumber)
        pkt = self.serv.recv()
        self.assertMsgEqual(Lwm2mSend(), pkt)
        self.serv.send(Lwm2mChanged.matching(pkt)())

        # Mute
        mute_send_path = Lwm2mResourcePath(ResPath.Server[1].MuteSend)
        oid = mute_send_path.object_id
        iid = mute_send_path.instance_id
        rid = mute_send_path.resource_id
        self.write_resource(self.serv, oid, iid, rid, b'1')
        self.assertTrue(self.communicate('send 1 %s' % ResPath.Device.ModelNumber,
                                         match_regex='cannot perform LwM2M Send, result: 2',
                                         timeout=1))

        # Unmute
        self.write_resource(self.serv, oid, iid, rid, b'0')
        self.communicate('send 1 %s' % ResPath.Device.ModelNumber)
        pkt = self.serv.recv()
        self.assertMsgEqual(Lwm2mSend(), pkt)
        self.serv.send(Lwm2mChanged.matching(pkt)())


class DeferrableSend(Send.Test):
    def runTest(self):
        self.write_resource(self.serv, OID.Server, 1, RID.Server.DisableTimeout, '5')
        self.execute_resource(self.serv, OID.Server, 1, RID.Server.Disable)
        self.assertDemoDeregisters()

        self.communicate('send_deferrable 1 %s' % ResPath.Device.ModelNumber)
        self.assertDemoRegisters(version='1.1', timeout_s=10)
        pkt = self.serv.recv(timeout_s=5)
        self.assertMsgEqual(Lwm2mSend(), pkt)
        self.serv.send(Lwm2mChanged.matching(pkt)())


class DeferrableSendDeferredMultipleTimes(Send.Test):
    def setUp(self):
        super().setUp(bootstrap_server=True)

    def runTest(self):
        self.execute_resource(self.serv, OID.Server, 2, RID.Server.RequestBootstrapTrigger)
        self.assertDemoRequestsBootstrap(
            preferred_content_format=coap.ContentFormat.APPLICATION_LWM2M_SENML_CBOR)

        self.serv.reset()

        self.communicate('send_deferrable 2 %s' % ResPath.Device.ModelNumber)

        self.write_resource(self.bootstrap_server, OID.Security, 2, RID.Security.ServerURI,
                            'coap://completely.invalid.server')
        req = Lwm2mBootstrapFinish()
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.bootstrap_server.recv())

        # URI is invalid, client will retry bootstrap - make it fail miserably
        self.assertDemoRequestsBootstrap(
            preferred_content_format=coap.ContentFormat.APPLICATION_LWM2M_SENML_CBOR,
            respond_with_error_code=coap.Code.RES_FORBIDDEN)

        self.communicate('reconnect')

        self.assertDemoRequestsBootstrap(
            preferred_content_format=coap.ContentFormat.APPLICATION_LWM2M_SENML_CBOR)
        self.write_resource(self.bootstrap_server, OID.Security, 2, RID.Security.ServerURI,
                            'coap://127.0.0.1:%d' % (self.serv.get_listen_port(),))
        req = Lwm2mBootstrapFinish()
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.bootstrap_server.recv())

        # demo can register again, and the Send operation is finally performed
        self.assertDemoRegisters(version='1.1')
        pkt = self.serv.recv(timeout_s=5)
        self.assertMsgEqual(Lwm2mSend(), pkt)
        self.serv.send(Lwm2mChanged.matching(pkt)())


class DeferrableSendFail(Send.Test):
    def setUp(self):
        super().setUp(minimum_version='1.0')

    def runTest(self):
        self.write_resource(self.serv, OID.Server, 1, RID.Server.DisableTimeout, '5')
        self.execute_resource(self.serv, OID.Server, 1, RID.Server.Disable)
        self.assertDemoDeregisters()

        self.communicate('send_deferrable 1 %s' % ResPath.Device.ModelNumber)

        # demo attempts to register with lwm2m=1.1
        pkt = self.assertDemoRegisters(self.serv, version='1.1', timeout_s=10, respond=False)
        self.serv.send(Lwm2mErrorResponse.matching(pkt)(coap.Code.RES_PRECONDITION_FAILED))

        # demo retries with lwm2m=1.0
        self.assertDemoRegisters(self.serv, version='1.0')
        self.assertIsNotNone(self.read_log_until_match(b'SEND FINISHED HANDLER: -3', 1))


class CleanupWhileThereIsDeferredSend(Send.Test):
    def tearDown(self):
        try:
            self.request_demo_shutdown()
            self.assertIsNotNone(self.read_log_until_match(b'SEND FINISHED HANDLER: -2', 1))
        finally:
            super().tearDown(auto_deregister=False)

    def runTest(self):
        self.write_resource(self.serv, OID.Server, 1, RID.Server.DisableTimeout, '30')
        self.execute_resource(self.serv, OID.Server, 1, RID.Server.Disable)
        self.assertDemoDeregisters()

        self.communicate('send_deferrable 1 %s' % ResPath.Device.ModelNumber)


class ServerRemovedWhileThereIsDeferredSend(Send.Test):
    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        self.write_resource(self.serv, OID.Server, 1, RID.Server.DisableTimeout, '5')
        self.execute_resource(self.serv, OID.Server, 1, RID.Server.Disable)
        self.assertDemoDeregisters()

        self.communicate('send_deferrable 1 %s' % ResPath.Device.ModelNumber)
        self.communicate('trim-servers 0')
        self.assertIsNotNone(self.read_log_until_match(b'SEND FINISHED HANDLER: -3', 1))


class ServerRemovedWhileAwaitingResponseToDeferredSend(Send.Test):
    def setUp(self):
        super().setUp(extra_cmdline_args=['--nstart', '2'])

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        self.write_resource(self.serv, OID.Server, 1, RID.Server.DisableTimeout, '5')
        self.execute_resource(self.serv, OID.Server, 1, RID.Server.Disable)
        self.assertDemoDeregisters()

        self.communicate('send_deferrable 1 %s' % ResPath.Device.ModelNumber)
        self.assertDemoRegisters(version='1.1', timeout_s=10)
        pkt = self.serv.recv(timeout_s=5)
        self.assertMsgEqual(Lwm2mSend(), pkt)

        self.communicate('trim-servers 0')
        self.assertDemoDeregisters()
        self.assertIsNotNone(self.read_log_until_match(b'SEND FINISHED HANDLER: -2', 1))


class QueueModeSend(retransmissions.RetransmissionTest.TestMixin, Send.Test):
    PSK_IDENTITY = b'test-identity'
    PSK_KEY = b'test-key'

    def setUp(self):
        super().setUp(psk_identity=self.PSK_IDENTITY, psk_key=self.PSK_KEY,
                      extra_cmdline_args=['--binding=UQ'], auto_register=False)
        self.assertDemoRegisters(version='1.1', lwm2m11_queue_mode=True)

    def runTest(self):
        self.wait_until_socket_count(0, timeout_s=self.max_transmit_wait() + 2)

        self.communicate('send 1 %s' % (ResPath.Device.ModelNumber,))

        self.assertDtlsReconnect(timeout_s=5)
        pkt = self.serv.recv()
        self.assertMsgEqual(Lwm2mSend(), pkt)
        self.serv.send(Lwm2mChanged.matching(pkt)())

        cbor = CBOR.parse(pkt.content)
        cbor.verify_values(test=self,
                           expected_value_map={
                               ResPath.Device.ModelNumber: 'demo-client'
                           })
