# -*- coding: utf-8 -*-
#
# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

import unittest

import pymbedtls

from framework.lwm2m_test import *
from framework_tools.lwm2m.coap.server_with_proxy import *
from suites.default.retransmissions import RetransmissionTest

# At the beginning of the test, things look like this:
#
# .------.      .-----------------------.      .-----------------------.      .--------------.
# | demo | <--> | client proxy (port A) |      |                       |      | Lwm2m Server |
# '------'      '-----------------------'      '-----------------------'      '--------------'
#
# That is, there's no path from demo to the actual Lwm2m Server.
#
# The path is created when one calls with self.serv.server_proxy(). The call creates
# a "server-side" proxy socket (filling the empty box), forwarding packets from client
# proxy to a backend Lwm2m Server:
#
# .------.      .-----------------------.      .-----------------------.      .--------------.
# | demo | <--> | client proxy (port A) | <--> | server proxy (port B) | <--> | Lwm2m Server |
# '------'      '-----------------------'      '-----------------------'      '--------------'
#
# From the Lwm2m Server perspective, the communication then looks like this (note that
# it sees the client using port B, due to packets being passed-through the "server proxy"):
#
# .--------------.                                       .--------------.
# | demo (port B)|                                       | Lwm2m Server |
# '--------------'                                       '--------------'
#       |    ----------- Client Hello connection_id() -------->   |
#       |                                                         |
#       |                             ...                         |
#       |                                                         |
#       |    <---- Server Hello + connection_id("something") --   |
#       |                                                         |
#       |                             ...                         |
#       |                                                         |
#       |    <--------------- regular LwM2M stuff ------------>   |
#
# After a while, the Client's port changes for some reason. This is represented as
# another call to with self.serv.server_proxy(), which basically creates a new
# "server proxy" socket:
#                                         note the port change -.
#                                                               v
# .------.      .-----------------------.      .-----------------------.      .--------------.
# | demo | <--> | client proxy (port A) | <--> | server proxy (port C) | <--> | Lwm2m Server |
# '------'      '-----------------------'      '-----------------------'      '--------------'
#
# In normal circumstances it'd confuse the Server, and a re-registration or at least
# re-handshake would have happened. However, with connection_id extension, the Server
# recognizes the client and no additional communication is performed.
#
# .--------------.                                       .--------------.
# | demo (port C)|                                       | Lwm2m Server |
# '--------------'                                       '--------------'
#       |  ---- Lwm2M Update + connection_id("something") ---->   |
#       |                                                         |
#       |    <---------------- 2.04 Changed -------------------   |
#
@unittest.skipIf(not pymbedtls.Context.supports_connection_id(),
                 "connection_id support is not enabled in pymbedtls")
class DtlsConnectionIdTest(test_suite.Lwm2mDtlsSingleServerTest,
                           test_suite.Lwm2mDmOperations):
    CONNECTION_ID_VALUE = 'something'

    def setUp(self, extra_cmdline_args=[], **kwargs):
        if '--use-connection-id' not in extra_cmdline_args:
            extra_cmdline_args.insert(0, '--use-connection-id')
        server = Lwm2mServer(CoapServerWithProxy(psk_identity=self.PSK_IDENTITY,
                                                 psk_key=self.PSK_KEY,
                                                 connection_id=self.CONNECTION_ID_VALUE))
        super().setUp(servers=[server], auto_register=False, extra_cmdline_args=extra_cmdline_args,
                      **kwargs)

    def runTest(self):
        with self.serv.server_proxy():
            self.assertDemoRegisters()
            self.read_resource(self.serv, oid=OID.Device, iid=0, rid=0)

        # Unconnect the socket at the pymbedtls site (this unconnects the link between "server proxy"
        # and "Lwm2m Server" in the diagram above), to allow accepting packets from unknown endpoints.
        disconnect_socket(self.serv.socket.py_socket)

        with self.serv.server_proxy():
            self.communicate('send-update')
            self.assertDemoUpdatesRegistration()
            super().request_demo_shutdown()
            self.assertDemoDeregisters(reset=False)

    def tearDown(self):
        super().tearDown(auto_deregister=False)


# The flow in this test is similar to DtlsConnectionIdTest, with the exception that
# this time connection_id extension is not used, and Server ignores messages from
# an endpoint it doesn't recognize via (host, port) tuple.
class DtlsWithoutConnectionIdTest(test_suite.Lwm2mDtlsSingleServerTest,
                                  test_suite.Lwm2mDmOperations):

    def setUp(self):
        server = Lwm2mServer(CoapServerWithProxy(psk_identity=self.PSK_IDENTITY,
                                                 psk_key=self.PSK_KEY))
        super().setUp(servers=[server], auto_register=False)

    def runTest(self):
        with self.serv.server_proxy():
            self.assertDemoRegisters()
            self.read_resource(self.serv, oid=OID.Device, iid=0, rid=0)

        # Unconnect the socket at the pymbedtls site (this unconnects the link between "server proxy"
        # and "Lwm2m Server" in the diagram above), to allow accepting packets from unknown endpoints.
        disconnect_socket(self.serv.socket.py_socket)

        # Nonetheless, connection_id was not used, so we should expect that the server
        # ignores Update messages messages.
        with self.serv.server_proxy():
            self.communicate('send-update')
            with self.assertRaises(socket.timeout):
                self.assertDemoUpdatesRegistration()

    def tearDown(self):
        super().tearDown(force_kill=True)


