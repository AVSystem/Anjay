# -*- coding: utf-8 -*-
#
# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

import contextlib
import errno
import select
import threading
import socket

from .server import DtlsServer

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


class CoapServerWithProxy(DtlsServer):
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


def disconnect_socket(sock):
    timeout = sock.gettimeout()
    try:
        # get rid of any data that might be in the socket buffer
        try:
            sock.settimeout(0)
            sock.recv(65535)
        except BlockingIOError:
            pass
        sock.connect(('', 0))
    except OSError as e:
        # On macOS, the call above returns failure, but actually works anyway...
        if e.errno not in {errno.EAFNOSUPPORT, errno.EADDRNOTAVAIL}:
            raise
    finally:
        sock.settimeout(timeout)
