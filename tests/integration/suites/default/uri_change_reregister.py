# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

from framework.lwm2m_test import *


class UriChangeReregisterTest(test_suite.Lwm2mTest):
    def setUp(self):
        self.setup_demo_with_servers(servers=2,
                                     num_servers_passed=1,
                                     bootstrap_server=True,
                                     auto_register=False)

    def tearDown(self):
        self.teardown_demo_with_servers(deregister_servers=[self.servers[1]])

    def runTest(self):
        regular_serv1_uri = 'coap://127.0.0.1:%d' % self.servers[0].get_listen_port()
        regular_serv2_uri = 'coap://127.0.0.1:%d' % self.servers[1].get_listen_port()

        # Register to regular_serv1
        pkt = self.servers[0].recv()
        self.assertMsgEqual(
            Lwm2mRegister('/rd?lwm2m=1.0&ep=%s&lt=86400' % (DEMO_ENDPOINT_NAME,)),
            pkt)
        self.servers[0].send(Lwm2mCreated.matching(pkt)(location='/rd/demo'))

        self.assertEqual(2, self.get_socket_count())

        # modify the server URI
        demo_port = self.get_demo_port()
        self.bootstrap_server.connect_to_client(('127.0.0.1', demo_port))

        req = Lwm2mWrite(ResPath.Security[2].ServerURI, regular_serv2_uri)
        self.bootstrap_server.send(req)

        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())

        # send Bootstrap Finish - trigger notifications
        req = Lwm2mBootstrapFinish()
        self.bootstrap_server.send(req)

        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())

        # we should now get a Register on the new URL
        self.assertDemoRegisters(self.servers[1])
