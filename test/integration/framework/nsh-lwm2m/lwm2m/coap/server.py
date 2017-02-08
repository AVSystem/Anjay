import socket
import errno
import contextlib

from typing import Tuple, Optional

from .packet import Packet


@contextlib.contextmanager
def _override_timeout(socket, timeout_s=-1):
    skip_override = socket is None or (timeout_s is not None and timeout_s < 0)

    if skip_override:
        yield
    else:
        orig_timeout_s = socket.gettimeout()
        socket.settimeout(timeout_s)
        yield
        socket.settimeout(orig_timeout_s)


@contextlib.contextmanager
def _enable_native_lib_import_from_script_dir():
    """
    Apparently Python does NOT look for native modules in the script directory,
    unless the directory is explicitly listed in sys.path.

    This function exists to enable importing locally-installed pymbedtls.so.
    """
    import sys
    import os
    path = os.path.dirname(os.path.realpath(__file__))
    try:
        sys.path.insert(0, path)
        yield
    finally:
        sys.path.remove(path)


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

    def recv(self, timeout_s: float=-1) -> Packet:
        with _override_timeout(self.socket, timeout_s):
            if not self.get_remote_addr():
                self.listen()

            packet = self.socket.recv(65536)

        return Packet.parse(packet)

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
        try:
            return self.socket.getpeername()
        except:
            return None


class DtlsServer(Server):
    def __init__(self, psk_identity, psk_key, listen_port=0):
        self.psk_identity = psk_identity
        self.psk_key = psk_key
        self.server_socket = None

        super().__init__(listen_port)

    def connect(self, remote_addr: Tuple[str, int]) -> None:
        with _enable_native_lib_import_from_script_dir():
            from pymbedtls import Socket

            self.close()

            try:
                self.server_socket = None
                self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                self.socket = Socket(self.socket, self.psk_identity, self.psk_key)
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
        try:
            with _enable_native_lib_import_from_script_dir():
                from pymbedtls import ServerSocket

                super().reset(listen_port)
                self.server_socket = ServerSocket(self.socket, self.psk_identity, self.psk_key)
                self.socket = None
        except ImportError:
            raise ImportError('could not import pymbedtls! run '
                              '`python3 setup.py install --user` in the '
                              'pymbedtls/ subdirectory of nsh-lwm2m submodule')

    def listen(self, timeout_s: float=-1) -> None:
        with _override_timeout(self.server_socket, timeout_s):
            self.socket = self.server_socket.accept()
            if self.socket_timeout is not None:
                self.socket.settimeout(self.socket_timeout)

    def get_listen_port(self) -> int:
        return self.server_socket.getsockname()[1]
