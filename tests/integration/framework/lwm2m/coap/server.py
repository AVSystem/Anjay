# -*- coding: utf-8 -*-
#
# Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

import contextlib
import enum
import errno
import socket
import time
from typing import Tuple, Optional

from .packet import Packet
from .transport import Transport


class SecurityMode(enum.Enum):
    PreSharedKey = 0
    RawPublicKey = 1
    Certificate = 2
    NoSec = 3
    CertificateWithEst = 4

    def __str__(self):
        if self == SecurityMode.PreSharedKey:
            return 'psk'
        elif self == SecurityMode.RawPublicKey:
            return 'rpk'
        elif self == SecurityMode.Certificate:
            return 'cert'
        elif self == SecurityMode.CertificateWithEst:
            return 'est'
        else:
            return 'nosec'


def _calculate_deadline(timeout_s=-1, deadline=None):
    if deadline is None and timeout_s is not None and timeout_s >= 0:
        return time.time() + timeout_s
    else:
        return deadline


@contextlib.contextmanager
def _override_timeout(sock, *args, **kwargs):
    deadline = _calculate_deadline(*args, **kwargs)
    skip_override = sock is None or deadline is None

    if skip_override:
        yield
    else:
        orig_timeout_s = sock.gettimeout()
        try:
            sock.settimeout(max(deadline - time.time(), 0))
            yield
        finally:
            try:
                sock.settimeout(orig_timeout_s)
            except OSError as e:
                if e.errno == errno.EBADF:
                    # sock has been closed during yield, ignore
                    pass
                else:
                    raise


def _disconnect_socket(old_sock, family):
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
    new_sock = socket.socket(family, socket.SOCK_DGRAM, 0)

    orig_reuse_addr = old_sock.getsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR)
    orig_reuse_port = old_sock.getsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT)

    orig_ipv6only = None
    if family == socket.AF_INET6 and hasattr(socket, 'IPV6_V6ONLY'):
        orig_ipv6only = old_sock.getsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY)

    # temporarily set REUSEADDR and REUSEPORT to allow new socket to bind
    # to the same port
    def set_sock_reuse(sock, reuse_addr, reuse_port):
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, reuse_addr)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, reuse_port)

    set_sock_reuse(new_sock, 1, 1)
    set_sock_reuse(old_sock, 1, 1)

    if orig_ipv6only is not None:
        new_sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, orig_ipv6only)

    new_sock.bind(old_sock.getsockname())
    old_sock.close()

    # restore original state of REUSEADDR/REUSEPORT
    set_sock_reuse(new_sock, orig_reuse_addr, orig_reuse_port)
    return new_sock


class Server(object):
    def __init__(self, listen_port=0, use_ipv6=False, reuse_port=False, transport=Transport.UDP):
        self._prev_remote_endpoint = None
        self.socket_timeout = None
        self.socket = None
        self.server_socket = None
        self.family = socket.AF_INET6 if use_ipv6 else socket.AF_INET
        self.ipv6_only = (str(use_ipv6).lower() == 'only')
        self.transport = transport
        self.reuse_port = reuse_port
        self.accepted_connection = False
        self._filtered_messages = []

        self.reset(listen_port)

    def listen(self, *args, **kwargs):
        deadline = _calculate_deadline(*args, **kwargs)

        if self.transport == Transport.UDP:
            assert self.get_remote_addr() is None
            with _override_timeout(self.socket, deadline=deadline):
                raw_pkt, remote_addr_port = self.socket.recvfrom(65536)

            self.connect_to_client(remote_addr_port)
            self._filtered_messages.append(raw_pkt)
        elif self.transport == Transport.TCP:
            self.socket.listen()
            with _override_timeout(self.socket, deadline=deadline):
                client_socket, _ = self.socket.accept()
                if self.server_socket is not None:
                    self.server_socket.close()
                self.server_socket = self.socket
                self.socket = client_socket
        else:
            raise ValueError("Invalid transport: %r" % (self.transport,))

    def connect_to_client(self, remote_addr: Tuple[str, int]) -> None:
        """
        This may be used e.g. for 1.0-style Server-Initiated Bootstrap over NoSec, when there is
        absolutely no initial traffic from the client.
        """
        self.socket.connect(remote_addr)
        self.accepted_connection = True

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
            self._raw_udp_socket = _disconnect_socket(self._raw_udp_socket, self.family)

    def _flush_recv_queue(self) -> None:
        self._filtered_messages.clear()
        with _override_timeout(self._raw_udp_socket, 0):
            try:
                while True:
                    self._raw_udp_socket.recv(4096)
            except:
                pass

    @contextlib.contextmanager
    def fake_close(self):
        try:
            self._fake_close()
            # Sometimes there may be a packet that managed to arrive before socket
            # went into "fake offline" mode. We certainly don't expect it after
            # restoring the socket.
            self._flush_recv_queue()
            yield
        finally:
            self._fake_unclose()

    def close(self) -> None:
        if self.socket:
            self.socket.close()
            self.socket = None
            self._filtered_messages.clear()

    def reset(self, listen_port=None) -> None:
        if listen_port is None:
            listen_port = self.get_listen_port() if self.socket else 0

        self.close()
        if self.server_socket is not None:
            self.socket = self.server_socket
        else:
            self.socket = socket.socket(self.family,
                                        socket.SOCK_STREAM if self.transport == Transport.TCP else socket.SOCK_DGRAM)
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT,
                                   1 if self.reuse_port else 0)
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR,
                                   1 if self.reuse_port else 0)
            if self.family == socket.AF_INET6 and hasattr(socket, 'IPV6_V6ONLY'):
                self.socket.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY,
                                       1 if self.ipv6_only else 0)
            self.socket.bind(('', listen_port))
        self.accepted_connection = False

    def send(self, coap_packet: Packet) -> None:
        self.socket.send(coap_packet.serialize(transport=self.transport))

    def recv_raw(self, timeout_s=-1, deadline=None, peek=False, bufsize=65536):
        deadline = _calculate_deadline(timeout_s, deadline)

        # NOTE: get_remote_addr() can sometimes return None, if someone
        # decided to "unconnect" the socket from a certain client. It is
        # only done for testing connection_id.
        if not self.get_remote_addr() and not self.accepted_connection:
            self.listen(deadline=deadline)

        self.accepted_connection = True

        if len(self._filtered_messages) > 0:
            if peek:
                return self._filtered_messages[0]
            else:
                return self._filtered_messages.pop(0)

        with _override_timeout(self.socket, deadline=deadline):
            raw_pkt = self.socket.recv(bufsize)

        if peek:
            self._filtered_messages.append(raw_pkt)

        return raw_pkt

    def recv(self, timeout_s: float = -1, deadline=None, filter=None, peek=False) -> Packet:
        deadline = _calculate_deadline(timeout_s, deadline)

        if filter is None:
            filter = lambda _: True

        filtered_messages = []
        while True:
            try:
                if self.transport == Transport.TCP:
                    raw_pkt = b''

                    def pkt_iterator():
                        nonlocal raw_pkt
                        while True:
                            data = self.recv_raw(deadline=deadline, bufsize=1)
                            if len(data) == 0:
                                break
                            raw_pkt += data
                            for b in data:
                                yield b

                    pkt_source = pkt_iterator()
                else:
                    raw_pkt = self.recv_raw(deadline=deadline)
                    pkt_source = raw_pkt
                pkt = Packet.parse(pkt_source, transport=self.transport)
            except BlockingIOError:
                raise socket.timeout('timed out')
            filter_result = filter(pkt)
            if peek or not filter_result:
                filtered_messages.append(raw_pkt)
            if filter_result:
                break

        self._filtered_messages += filtered_messages
        return pkt

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

    def get_local_addr(self) -> Optional[Tuple[str, int]]:
        return self.socket.getsockname()

    def get_remote_addr(self) -> Optional[Tuple[str, int]]:
        if not self.socket:
            return None

        try:
            return self.socket.getpeername()
        except Exception:
            return None

    def security_mode(self):
        return 'nosec'


class TlsServer(Server):
    def __init__(self, psk_identity=None, psk_key=None, ca_path=None, ca_file=None, crt_file=None,
                 key_file=None, listen_port=0, debug=False, use_ipv6=False, reuse_port=False,
                 connection_id='', ciphersuites=None, transport=Transport.TCP):
        use_psk = (psk_identity and psk_key)
        use_certs = any((ca_path, ca_file, crt_file, key_file))
        if use_psk and use_certs:
            raise ValueError("Cannot use PSK and Certificates at the same time")

        try:
            from pymbedtls import PskSecurity, CertSecurity, Context
        except ImportError:
            raise ImportError('could not import pymbedtls! run '
                              '`python3 setup.py install --user` in the '
                              'pymbedtls/ subdirectory of nsh-lwm2m submodule '
                              'or export PYTHONPATH properly')

        if use_psk:
            security = PskSecurity(psk_key, psk_identity)
        elif use_certs:
            security = CertSecurity(ca_path, ca_file, crt_file, key_file)
        else:
            raise ValueError("Neither PSK nor Certificates were configured for use with DTLS")

        if ciphersuites is not None:
            security.set_ciphersuites(ciphersuites)

        self._pymbedtls_context = Context(security, debug, connection_id)
        self._security_mode = security.name()

        super().__init__(listen_port, use_ipv6, reuse_port=reuse_port, transport=transport)

    def connect_to_client(self, remote_addr: Tuple[str, int]) -> None:
        raise NotImplementedError('connect_to_client() not supported for DTLS servers')

    @property
    def _raw_udp_socket(self) -> None:
        return self.socket.py_socket

    @_raw_udp_socket.setter
    def _raw_udp_socket(self, value) -> None:
        self.socket.py_socket = value

    def reset(self, listen_port=None) -> None:
        from pymbedtls import ServerSocket
        super().reset(listen_port)
        if not isinstance(self.socket, ServerSocket):
            self.socket = ServerSocket(self._pymbedtls_context, self.socket)

    def listen(self, *args, **kwargs) -> None:
        deadline = _calculate_deadline(*args, **kwargs)

        from pymbedtls import ServerSocket
        assert isinstance(self.socket, ServerSocket)

        if self.transport == Transport.TCP:
            self.socket.py_socket.listen()

        with _override_timeout(self.socket, deadline=deadline):
            client_socket = self.socket.accept()
            if self.socket_timeout is not None:
                client_socket.settimeout(self.socket_timeout)

        if self.transport == Transport.UDP:
            self.socket.close()
            self.socket = client_socket
        elif self.transport == Transport.TCP:
            if self.server_socket is not None:
                self.server_socket.close()
            self.server_socket = self.socket
            self.socket = client_socket
        else:
            raise ValueError("Invalid transport: %r" % (self.transport,))

    def security_mode(self):
        # Either 'psk' or 'cert'.
        return self._security_mode


class DtlsServer(TlsServer):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs, transport=Transport.UDP)
