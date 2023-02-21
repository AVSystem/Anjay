# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

import os
import socket

from framework.lwm2m.coap.server import SecurityMode
from framework.lwm2m_test import *
from framework import test_suite


class ReconnectBootstrapTest(test_suite.Lwm2mSingleServerTest):
    def setUp(self):
        self.setup_demo_with_servers(servers=0, bootstrap_server=True)

    def runTest(self):
        self.bootstrap_server.set_timeout(timeout_s=1)
        pkt = self.bootstrap_server.recv()
        self.assertMsgEqual(Lwm2mRequestBootstrap(endpoint_name=DEMO_ENDPOINT_NAME),
                            pkt)
        self.bootstrap_server.send(Lwm2mChanged.matching(pkt)())

        original_remote_addr = self.bootstrap_server.get_remote_addr()

        # reconnect
        self.communicate('reconnect')
        self.bootstrap_server.reset()
        pkt = self.bootstrap_server.recv()

        # should retain remote port after reconnecting
        self.assertEqual(original_remote_addr,
                         self.bootstrap_server.get_remote_addr())
        self.assertMsgEqual(Lwm2mRequestBootstrap(endpoint_name=DEMO_ENDPOINT_NAME),
                            pkt)

        self.bootstrap_server.send(Lwm2mChanged.matching(pkt)())

        demo_port = self.get_demo_port()
        self.assertEqual(self.bootstrap_server.get_remote_addr()[1], demo_port)

        # send Bootstrap Finish
        req = Lwm2mBootstrapFinish()
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())

        # reconnect once again
        self.communicate('reconnect')

        # now there should be no Bootstrap Request
        with self.assertRaises(socket.timeout):
            print(self.bootstrap_server.recv(timeout_s=3))

        # should retain remote port after reconnecting
        new_demo_port = self.get_demo_port()
        self.assertEqual(demo_port, new_demo_port)

        self.bootstrap_server.connect_to_client(('127.0.0.1', new_demo_port))

        # DELETE /33605, essentially a no-op to check connectivity
        req = Lwm2mDelete(Lwm2mPath('/%d' % (OID.Test,)))
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mDeleted.matching(req)(),
                            self.bootstrap_server.recv())
