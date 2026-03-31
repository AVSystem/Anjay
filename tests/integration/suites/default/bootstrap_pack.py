# -*- coding: utf-8 -*-
#
# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

import socket

from framework_tools.lwm2m.coap.server import SecurityMode
from framework.lwm2m_test import *
from . import bootstrap_client


class BootstrapPackTest:
    class Test(bootstrap_client.BootstrapTest.Test):
        def setUp(self, *args, **kwargs):
            super().setUp(minimum_version='1.2', maximum_version='1.2', *args, **kwargs)

        def build_add_server_bp(self,
                                server_iid,
                                security_iid,
                                server_uri,
                                lifetime=86400,
                                secure_identity=b'',
                                secure_key=b'',
                                security_mode: SecurityMode = SecurityMode.NoSec,
                                binding="U",
                                bootstrap_on_registration_failure=None):
            sec_pref = '0/' + str(security_iid) + '/'
            security_data = [
                {SenmlLabel.BASE_NAME: '/',
                 SenmlLabel.NAME: sec_pref + '0', SenmlLabel.STRING: server_uri},
                {SenmlLabel.NAME: sec_pref + '1', SenmlLabel.BOOL: False},
                {SenmlLabel.NAME: sec_pref + '2', SenmlLabel.VALUE: security_mode.value}
            ]
            if secure_identity:
                security_data += [
                    {SenmlLabel.NAME: sec_pref + '3', SenmlLabel.OPAQUE: secure_identity}]
            if secure_key:
                security_data += [{SenmlLabel.NAME: sec_pref + '5', SenmlLabel.OPAQUE: secure_key}]
            security_data += [{SenmlLabel.NAME: sec_pref + '10', SenmlLabel.VALUE: server_iid}]

            srv_pref = '1/' + str(server_iid) + '/'
            server_data = [
                {SenmlLabel.NAME: srv_pref + '0', SenmlLabel.VALUE: server_iid},
                {SenmlLabel.NAME: srv_pref + '1', SenmlLabel.VALUE: lifetime},
                {SenmlLabel.NAME: srv_pref + '6', SenmlLabel.BOOL: False},
                {SenmlLabel.NAME: srv_pref + '7', SenmlLabel.STRING: binding}
            ]
            if bootstrap_on_registration_failure is not None:
                server_data += [{
                    SenmlLabel.NAME: srv_pref + str(RID.Server.BootstrapOnRegistrationFailure),
                    SenmlLabel.VALUE: bootstrap_on_registration_failure
                }]

            return security_data + server_data

        def add_server_bp(self, pkt_to_match, *args, **kwargs):
            self.bootstrap_server.send(Lwm2mContent.matching(pkt_to_match)(
                content=CBOR.serialize(self.build_add_server_bp(*args, **kwargs)),
                format=coap.ContentFormat.APPLICATION_LWM2M_SENML_CBOR))

        def perform_bootstrap_pack(self, server_iid, security_iid, server_uri, lifetime=86400,
                                   secure_identity=b'', secure_key=b'',
                                   security_mode: SecurityMode = SecurityMode.NoSec,
                                   holdoff_s=None, binding="U",
                                   endpoint=DEMO_ENDPOINT_NAME,
                                   bootstrap_on_registration_failure=None,
                                   bootstrap_request_timeout_s=None):
            # For the first holdoff_s seconds, the client should wait for Bootstrap-Pack.
            # Note that we subtract 1 second to take into account code execution delays.
            if holdoff_s is None:
                holdoff_s = self.holdoff_s or 0
            no_message_s = max(0, holdoff_s - 1)
            if no_message_s > 0:
                with self.assertRaises(socket.timeout):
                    print(self.bootstrap_server.recv(timeout_s=no_message_s))

            # We should get BootstrapPack Request now
            pkt = None
            if bootstrap_request_timeout_s is None:
                pkt = self.assertDemoRequestsBootstrapPack(endpoint=endpoint)
            elif bootstrap_request_timeout_s >= 0:
                pkt = self.assertDemoRequestsBootstrapPack(
                    endpoint=endpoint, timeout_s=bootstrap_request_timeout_s)

            self.add_server_bp(pkt_to_match=pkt,
                               server_iid=server_iid,
                               security_iid=security_iid,
                               server_uri=server_uri,
                               lifetime=lifetime,
                               secure_identity=secure_identity,
                               secure_key=secure_key,
                               security_mode=security_mode,
                               binding=binding,
                               bootstrap_on_registration_failure=bootstrap_on_registration_failure)


class BootstrapPackBasicTest(BootstrapPackTest.Test):
    def setUp(self):
        super().setUp(holdoff_s=3, timeout_s=3)

    def runTest(self):
        self.perform_bootstrap_pack(server_iid=1,
                                    security_iid=2,
                                    server_uri='coap://127.0.0.1:%d' % self.serv.get_listen_port(),
                                    lifetime=60)
        self.assertDemoRegisters(self.serv, lifetime=60, version='1.2')


class BootstrapPackFallbackTest(BootstrapPackTest.Test):
    def runTest(self):
        # We should get BootstrapPackRequest in the beginning
        pkt = self.assertDemoRequestsBootstrapPack(endpoint=DEMO_ENDPOINT_NAME)

        # We respond with NOT_IMPLEMENTED
        self.bootstrap_server.send(Lwm2mErrorResponse.matching(
            pkt)(code=coap.Code.RES_NOT_IMPLEMENTED))

        # Fallback to casual bootstrap
        self.assertDemoRequestsBootstrap(endpoint=DEMO_ENDPOINT_NAME,
                                         preferred_content_format=coap.ContentFormat.APPLICATION_LWM2M_SENML_CBOR)

        # Now we can perform the casual bootstrap
        req = Lwm2mDelete('/')
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mDeleted.matching(req)(),
                            self.bootstrap_server.recv())
        self.add_server(server_iid=1, security_iid=2,
                        server_uri='coap://127.0.0.1:%d' % self.serv.get_listen_port(),
                        lifetime=60)
        self.perform_bootstrap_finish()

        # And it should register
        self.assertDemoRegisters(self.serv, lifetime=60, version='1.2')


class BootstrapPackBlockTest(BootstrapPackTest.Test):
    def runTest(self):
        # NOTE: The secure key field is unused,
        # but it makes the data large enough to require a block transfer
        pack = self.build_add_server_bp(
            server_iid=1,
            security_iid=2,
            server_uri='coap://127.0.0.1:%d' % self.serv.get_listen_port(),
            secure_key=random_stuff(102400))
        pack_data = CBOR.serialize(pack)
        pack_chunks = [pack_data[i:i + 1024] for i in range(0, len(pack_data), 1024)]

        req = self.assertDemoRequestsBootstrapPack()
        for seq_num in range(len(pack_chunks) - 1):
            self.bootstrap_server.send(Lwm2mContent.matching(req)(
                content=pack_chunks[seq_num],
                format=coap.ContentFormat.APPLICATION_LWM2M_SENML_CBOR,
                options=[coap.Option.BLOCK2(seq_num=seq_num, has_more=True, block_size=1024)]))

            req = self.assertDemoRequestsBootstrapPack()
            options = req.get_options(coap.Option.BLOCK2)
            self.assertEqual(len(options), 1)
            self.assertEqual(options[0].block_size(), 1024)
            self.assertEqual(options[0].seq_num(), seq_num + 1)

        self.bootstrap_server.send(Lwm2mContent.matching(req)(
            content=pack_chunks[len(pack_chunks) - 1],
            format=coap.ContentFormat.APPLICATION_LWM2M_SENML_CBOR,
            options=[coap.Option.BLOCK2(seq_num=len(pack_chunks) - 1, has_more=False,
                                        block_size=1024)]))

        self.assertDemoRegisters(self.serv, version='1.2')
