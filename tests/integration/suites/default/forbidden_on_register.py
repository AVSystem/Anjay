# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

from framework.lwm2m_test import *
from framework.test_utils import *
from framework.lwm2m.coap.server import SecurityMode

from . import bootstrap_client as bs

import socket

class Test:
    PSK_KEY=b'key'
    PSK_IDENTITY=b'identity'

    class ClientReceivesForbiddenOnRegisterMixin:
        def setUp(self):
            self.setup_demo_with_servers(servers=1, auto_register=False)

        def tearDown(self):
            self.teardown_demo_with_servers(auto_deregister=False)

        def runTest(self):
            register_pkt = self.assertDemoRegisters(self.serv, respond=False)
            self.serv.send(Lwm2mErrorResponse.matching(register_pkt)(code=coap.Code.RES_FORBIDDEN))
            # Client shouldn't even retry
            with self.assertRaises(socket.timeout):
                self.serv.recv()


    class ClientSessionRevokedMixin(test_suite.Lwm2mDmOperations):
        def tearDown(self):
            self.teardown_demo_with_servers(auto_deregister=False)

        def runTest(self):
            # Trigger update.
            self.communicate('send-update')
            update_pkt = self.assertDemoUpdatesRegistration(self.serv, respond=False)
            # Respond to it with 4.00 Bad Request to simulate some kind of client account expiration on server side.
            self.serv.send(Lwm2mErrorResponse.matching(update_pkt)(code=coap.Code.RES_BAD_REQUEST))
            # This should cause client attempt to re-register.
            register_pkt = self.assertDemoRegisters(self.serv, respond=False)
            # To which we respond with 4.03 Forbidden, finishing off the communication.
            self.serv.send(Lwm2mErrorResponse.matching(register_pkt)(code=coap.Code.RES_FORBIDDEN))
            # Client shouldn't even retry
            with self.assertRaises(socket.timeout):
                self.serv.recv()

    class ClientReceivesForbiddenOnRegisterAndFallbacksToBootstrapMixin(bs.BootstrapTest.Test):
        def perform_bootstrap(self):
            raise NotImplemented('To be implemented by deriving class')

        def runTest(self):
            self.perform_bootstrap()
            # Demo tries to register.
            register_pkt = self.assertDemoRegisters(self.serv, respond=False)
            # To which we respond with 4.03 Forbidden.
            self.serv.send(Lwm2mErrorResponse.matching(register_pkt)(code=coap.Code.RES_FORBIDDEN))
            # Client should fallback to client initiated bootstrap
            if self.bootstrap_server.security_mode() != 'nosec':
                self.assertDtlsReconnect(self.bootstrap_server)
            self.assertDemoRequestsBootstrap()
            # Client shouldn't even retry to re-register
            with self.assertRaises(socket.timeout):
                self.serv.recv()

        def tearDown(self):
            self.teardown_demo_with_servers(auto_deregister=False)


    class ClientSessionRevokedAndFallbackToBootstrapMixin(bs.BootstrapTest.Test):
        def perform_bootstrap(self):
            raise NotImplemented('To be implemented by deriving class')

        def runTest(self):
            self.perform_bootstrap()
            # Demo normally registers.
            self.assertDemoRegisters(self.serv)
            # Trigger update.
            self.communicate('send-update')
            update_pkt = self.assertDemoUpdatesRegistration(self.serv, respond=False)
            # Respond to it with 4.00 Bad Request to simulate some kind of client account expiration on server side.
            self.serv.send(Lwm2mErrorResponse.matching(update_pkt)(code=coap.Code.RES_BAD_REQUEST))
            # This should cause client attempt to re-register.
            register_pkt = self.assertDemoRegisters(self.serv, respond=False)
            # To which we respond with 4.03 Forbidden, finishing off the communication.
            self.serv.send(Lwm2mErrorResponse.matching(register_pkt)(code=coap.Code.RES_FORBIDDEN))
            # Client should fallback to client initiated bootstrap
            self.assertDemoRequestsBootstrap()
            # Client shouldn't even retry to re-register
            with self.assertRaises(socket.timeout):
                self.serv.recv()

        def tearDown(self):
            self.teardown_demo_with_servers(auto_deregister=False)


class ClientReceivesForbiddenOnRegister(Test.ClientReceivesForbiddenOnRegisterMixin,
                                        test_suite.Lwm2mSingleServerTest):
    pass


class DtlsClientReceivesForbiddenOnRegister(Test.ClientReceivesForbiddenOnRegisterMixin,
                                            test_suite.Lwm2mDtlsSingleServerTest):
    pass


class ClientSessionRevoked(Test.ClientSessionRevokedMixin,
                           test_suite.Lwm2mSingleServerTest):
    pass


class DtlsClientSessionRevoked(Test.ClientSessionRevokedMixin,
                               test_suite.Lwm2mDtlsSingleServerTest):
    pass


class ClientReceivesForbiddenOnRegisterAndFallbacksToBootstrap(Test.ClientReceivesForbiddenOnRegisterAndFallbacksToBootstrapMixin):
    def perform_bootstrap(self):
        self.perform_typical_bootstrap(server_iid=1,
                                       security_iid=2,
                                       server_uri='coap://127.0.0.1:%d' % self.serv.get_listen_port(),
                                       lifetime=86400)


class DtlsClientReceivesForbiddenOnRegisterAndFallbacksToBootstrap(Test.ClientReceivesForbiddenOnRegisterAndFallbacksToBootstrapMixin):
    def perform_bootstrap(self):
        self.perform_typical_bootstrap(server_iid=1,
                                       security_iid=2,
                                       server_uri='coaps://127.0.0.1:%d' % self.serv.get_listen_port(),
                                       secure_identity=Test.PSK_IDENTITY,
                                       secure_key=Test.PSK_KEY,
                                       security_mode=SecurityMode.PreSharedKey,
                                       lifetime=86400)

    def setUp(self):
        super().setUp(servers=[Lwm2mServer(coap.DtlsServer(psk_key=Test.PSK_KEY, psk_identity=Test.PSK_IDENTITY))],
                      num_servers_passed=0)


class ClientSessionRevokedAndFallbackToBootstrap(Test.ClientSessionRevokedAndFallbackToBootstrapMixin):
    def perform_bootstrap(self):
        self.perform_typical_bootstrap(server_iid=1,
                                       security_iid=2,
                                       server_uri='coap://127.0.0.1:%d' % self.serv.get_listen_port(),
                                       lifetime=86400)


class DtlsClientSessionRevokedAndFallbackToBootstrap(Test.ClientSessionRevokedAndFallbackToBootstrapMixin):
    def perform_bootstrap(self):
        self.perform_typical_bootstrap(server_iid=1,
                                       security_iid=2,
                                       server_uri='coaps://127.0.0.1:%d' % self.serv.get_listen_port(),
                                       secure_identity=Test.PSK_IDENTITY,
                                       secure_key=Test.PSK_KEY,
                                       security_mode=SecurityMode.PreSharedKey,
                                       lifetime=86400)
    def setUp(self):
        super().setUp(servers=[Lwm2mServer(coap.DtlsServer(psk_key=Test.PSK_KEY, psk_identity=Test.PSK_IDENTITY))],
                      num_servers_passed=0)
