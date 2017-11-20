# -*- coding: utf-8 -*-
#
# Copyright 2017 AVSystem <avsystem@avsystem.com>
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

import contextlib
import socket
from typing import Tuple, Optional

from .packet import Packet


@contextlib.contextmanager
def _override_timeout(sock, timeout_s=-1):
    skip_override = sock is None or (timeout_s is not None and timeout_s < 0)

    if skip_override:
        yield
    else:
        orig_timeout_s = sock.gettimeout()
        sock.settimeout(timeout_s)
        yield
        sock.settimeout(orig_timeout_s)


class Server(object):
    def __init__(self, listen_port=0):
        self.socket_timeout = None
        self.socket = None

        self.reset(listen_port)

    def listen(self, timeout_s=-1):
        with _override_timeout(self.socket, timeout_s):
            _, remote_addr_port = self.socket.recvfrom(1, socket.MSG_PEEK)

        self.connect(remote_addr_port)

    def connect(self, remote_addr: Tuple[str, int]) -> None:
        self.socket.connect(remote_addr)

    def close(self) -> None:
        if self.socket:
            self.socket.close()
            self.socket = None

    def reset(self, listen_port=None) -> None:
        if listen_port is None:
            listen_port = self.get_listen_port() if self.socket else 0

        self.close()
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(('', listen_port))

    def send(self, coap_packet: Packet) -> None:
        self.socket.send(coap_packet.serialize())

    def recv_raw(self, timeout_s: float = -1):
        with _override_timeout(self.socket, timeout_s):
            if not self.get_remote_addr():
                self.listen()

            return self.socket.recv(65536)

    def recv(self, timeout_s: float = -1) -> Packet:
        return Packet.parse(self.recv_raw(timeout_s))

    def set_timeout(self, timeout_s: float) -> None:
        self.socket_timeout = timeout_s
        if self.socket:
            self.socket.settimeout(timeout_s)

    def get_timeout(self) -> Optional[float]:
        if self.socket:
            return self.socket.gettimeout()
        return self.socket_timeout

    def get_listen_port(self) -> int:
        return self.socket.getsockname()[1]

    def get_remote_addr(self) -> Optional[Tuple[str, int]]:
        if not self.socket:
            return None

        try:
            return self.socket.getpeername()
        except socket.error:
            return None

    def security_mode(self):
        return 'nosec'


class DtlsServer(Server):
    def __init__(self, psk_identity, psk_key, listen_port=0, debug=False):
        self.psk_identity = psk_identity
        self.psk_key = psk_key
        self.server_socket = None
        self.debug = debug

        super().__init__(listen_port)

    def connect(self, remote_addr: Tuple[str, int]) -> None:
        from pymbedtls import Socket

        self.close()

        try:
            self.server_socket = None
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.socket = Socket(self.socket, self.psk_identity, self.psk_key, self.debug)
            self.socket.connect(remote_addr)
        except:
            self.close()
            raise

    def close(self):
        super().close()
        if self.server_socket:
            self.server_socket.close()
            self.server_socket = None

    def reset(self, listen_port=None) -> None:
        if self.server_socket and listen_port is None:
            super().close()
        else:
            try:
                from pymbedtls import ServerSocket

                super().reset(listen_port)
                self.server_socket = ServerSocket(self.socket, self.psk_identity, self.psk_key, self.debug)
                self.socket = None
            except ImportError:
                raise ImportError('could not import pymbedtls! run '
                                  '`python3 setup.py install --user` in the '
                                  'pymbedtls/ subdirectory of nsh-lwm2m submodule '
                                  'or export PYTHONPATH properly')

    def listen(self, timeout_s: float = -1) -> None:
        with _override_timeout(self.server_socket, timeout_s):
            self.socket = self.server_socket.accept()
            if self.socket_timeout is not None:
                self.socket.settimeout(self.socket_timeout)

    def get_listen_port(self) -> int:
        return self.server_socket.getsockname()[1]

    def security_mode(self):
        return 'psk'
