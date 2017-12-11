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
import errno
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


def _disconnect_socket(old_sock):
    """
    Attempts to "disconnect" an UDP socket, making it accept packets from
    all remote addresses again. POSIX says that:

        If address is a null address for the protocol, the socket's peer
        address shall be reset.

    So, in theory, connect(('', 0)) should be enough. On FreeBSD 11 though,
    the peer address is reset by passing *an invalid address* instead
    (see https://www.freebsd.org/cgi/man.cgi?connect), and apparently
    0.0.0.0:0 is a perfectly valid one that happens to fail with
    EADDRNOTAVAIL. And even though Linux supports resetting peer address
    by connect() to an address with AF_UNSPEC family, Python does not offer
    an API that allows it: address family always matches the one set on
    the socket.

    As a workaround, we create a new socket and bind it to the same port.
    Fun thing is, we can't really close the original socket and then create
    a new one: if the socket is bound to an ephemeral port, that would
    create a window of opportunity for the OS to assign the port to
    *another* socket. This was exactly the case we observed on OSX, which
    apparently prefers reusing recently released ephemeral ports.

    To avoid that, we try something more complex:
    1. Create a new socket,
    2. Enable SO_REUSEADDR/SO_REUSEPORT on both old and new sockets,
    3. Bind new socket to the same local address as the old one,
    4. Close the old socket,
    5. Set SO_REUSEADDR/SO_REUSEPORT values on the new socket to the same
       ones as previously used by the old socket.
    """
    new_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, 0)

    orig_reuse_addr = old_sock.getsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR)
    orig_reuse_port = old_sock.getsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT)

    # temporarily set REUSEADDR and REUSEPORT to allow new socket to bind
    # to the same port
    def set_sock_reuse(sock, reuse_addr, reuse_port):
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, reuse_addr)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, reuse_port)

    set_sock_reuse(new_sock, 1, 1)
    set_sock_reuse(old_sock, 1, 1)

    new_sock.bind(old_sock.getsockname())
    old_sock.close()

    # restore original state of REUSEADDR/REUSEPORT
    set_sock_reuse(new_sock, orig_reuse_addr, orig_reuse_port)
    return new_sock


class Server(object):
    def __init__(self, listen_port=0):
        self._prev_remote_endpoint = None
        self.socket_timeout = None
        self.socket = None

        self.reset(listen_port)

    def listen(self, timeout_s=-1):
        with _override_timeout(self.socket, timeout_s):
            _, remote_addr_port = self.socket.recvfrom(1, socket.MSG_PEEK)

        self.connect(remote_addr_port)

    def connect(self, remote_addr: Tuple[str, int]) -> None:
        self.socket.connect(remote_addr)

    @property
    def _raw_udp_socket(self) -> None:
        return self.socket

    @_raw_udp_socket.setter
    def _raw_udp_socket(self, value) -> None:
        self.socket = value

    def _fake_close(self) -> None:
        """
        Force the OS to send Port Unreachable ICMP responses whenever the client
        attempts to send a message, without actually closing the socket. This
        prevents other threads from reusing the same ephemeral port.

        We do this by connecting the socket to a port that is supposed to be
        unused: UDP/1, which is assigned to TCP Port Service Multiplexer by IANA
        (https://www.iana.org/assignments/service-names-port-numbers/service-names-port-numbers.txt)
        or UDP/2, which is supposedly used by some "compressnet Management
        Utility".
        """
        assert self._prev_remote_endpoint is None

        try:
            self._prev_remote_endpoint = self._raw_udp_socket.getpeername()
        except OSError:
            pass

        self._raw_udp_socket.connect(('', 1))

    def _fake_unclose(self) -> None:
        if self._prev_remote_endpoint:
            self._raw_udp_socket.connect(self._prev_remote_endpoint)
            self._prev_remote_endpoint = None
        else:
            self._raw_udp_socket = _disconnect_socket(self._raw_udp_socket)

    @contextlib.contextmanager
    def fake_close(self):
        try:
            self._fake_close()
            yield
        finally:
            self._fake_unclose()

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

    @property
    def _raw_udp_socket(self) -> None:
        return self.socket.py_socket

    @_raw_udp_socket.setter
    def _raw_udp_socket(self, value) -> None:
        self.socket.py_socket = value

    def _fake_close(self):
        super()._fake_close()
        # we cannot use the same fake remote endpoint as for the "client" socket
        # because BSD does not allow two sockets with the same local/remote
        # endpoints. For details, see comment in Server._fake_close.
        self.server_socket.connect(('', 2))

    def _fake_unclose(self):
        super()._fake_unclose()
        self.server_socket = _disconnect_socket(self.server_socket)

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
