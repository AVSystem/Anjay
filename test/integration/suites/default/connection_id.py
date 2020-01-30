# -*- coding: utf-8 -*-
#
# Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import errno
import socket
import select
import unittest
import pymbedtls
import threading
import contextlib

from framework.lwm2m_test import *
from framework.lwm2m.coap.server import Server


class UdpProxy(threading.Thread):
    def __init__(self, client_socket, server_socket):
        super().__init__()
        self._mutex = threading.Lock()
        # This is the socket the demo sees and communicates with.
        self._client_socket = client_socket
        # This will be the socket connected to the LwM2M server. It MUST be connected to the target server.
        self._server_socket = server_socket

    def _handle_client_pkt(self, sock):
        pkt, remote_addr = sock.recvfrom(4096)
        self._client_socket.connect(remote_addr)

        self._server_socket.send(pkt)

    def _handle_server_pkt(self, sock):
        assert self._client_socket is not None
        pkt, _ = sock.recvfrom(4096)

        self._client_socket.send(pkt)

    def run(self):
        with self._mutex:
            self._operating = True

        poller = select.poll()
        poller.register(self._client_socket, select.POLLIN)
        poller.register(self._server_socket, select.POLLIN)
        while True:
            with self._mutex:
                if not self._operating:
                    break

            # Timeout after 60ms, to be able to interrupt the thread.
            for (fd, event) in poller.poll(60):
                if event & select.POLLIN:
                    if fd == self._client_socket.fileno():
                        self._handle_client_pkt(self._client_socket)
                    elif fd == self._server_socket.fileno():
                        self._handle_server_pkt(self._server_socket)
                if event & (select.POLLERR | select.POLLHUP):
                    raise RuntimeError('Socket error while trying to poll()')

    def stop(self):
        with self._mutex:
            self._operating = False


class CoapServerWithProxy(coap.DtlsServer):
    def __init__(self, *args, **kwargs):
        # This is the socket the demo sees and communicates with.
        self._client_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._client_socket.bind(('', 0))
        super().__init__(*args, **kwargs)
        # First self.reset() is called in parent class, and it setups some stuff
        # like an actual server socket. But then, another reset() would cause
        # an attempt to reuse existing UDP port (the one returend by overriden
        # get_listen_port()), therefore we disallow doing that.
        self.reset = self._not_implemented

    def _not_implemented(self, *args, **kwargs):
        raise NotImplementedError

    def get_listen_port(self):
        return self.get_local_addr()[1]

    def get_local_addr(self):
        return self._client_socket.getsockname()

    @contextlib.contextmanager
    def server_proxy(self):
        server_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        server_socket.connect(super().get_local_addr())
        proxy = UdpProxy(self._client_socket, server_socket)
        proxy.start()
        try:
            yield
        finally:
            proxy.stop()
            proxy.join()


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

    def setUp(self):
        server = Lwm2mServer(CoapServerWithProxy(psk_identity=self.PSK_IDENTITY,
                                                 psk_key=self.PSK_KEY,
                                                 connection_id=self.CONNECTION_ID_VALUE))
        super().setUp(servers=[server], auto_register=False, extra_cmdline_args=['--use-connection-id'])

    def runTest(self):
        with self.serv.server_proxy():
            self.assertDemoRegisters()
            self.read_resource(self.serv, oid=OID.Device, iid=0, rid=0)

        # Unconnect the socket at the pymbedtls site (this unconnects the link between "server proxy"
        # and "Lwm2m Server" in the diagram above), to allow accepting packets from unknown endpoints.
        self.serv.socket.py_socket.connect(('', 0))

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
        try:
            self.serv.socket.py_socket.connect(('', 0))
        except OSError as e:
            # On macOS, the call above returns failure, but actually works anyway...
            if e.errno not in {errno.EAFNOSUPPORT, errno.EADDRNOTAVAIL}:
                raise

        # Nonetheless, connection_id was not used, so we should expect that the server
        # ignores Update messages messages.
        with self.serv.server_proxy():
            self.communicate('send-update')
            with self.assertRaises(socket.timeout):
                self.assertDemoUpdatesRegistration()

    def tearDown(self):
        super().tearDown(force_kill=True)
