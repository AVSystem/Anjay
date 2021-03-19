#!/usr/bin/env python3
# Copyright 2017-2021 AVSystem <avsystem@avsystem.com>
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

import sys

assert sys.version_info >= (3, 5), "Python < 3.5 is unsupported"

import argparse
import binascii
import collections
import glob
import os
import re
import socket
import enum
import select
import logging

from typing import List, Optional, Tuple, Mapping
from prompt_toolkit.history import FileHistory

sys.path = [os.path.join(os.path.dirname(os.path.abspath(__file__)), 'powercmd')] + sys.path

import powercmd

sys.path = [os.path.dirname(os.path.abspath(__file__))] + sys.path

from tlv_shell import TLVBuilderShell

from lwm2m import coap
from lwm2m.tlv import TLV
from lwm2m.messages import *
from lwm2m.server import Lwm2mServer

from lwm2m.coap.server import SecurityMode

REGISTER_PATH = '/rd/demo'
DEFAULT_COAP_PORT = 5683
DEFAULT_COAPS_PORT = 5684


def block_size(s):
    val = int(s)
    if not 2 ** 4 <= val <= 2 ** 10:
        raise ValueError('invalid block size, expected 16 <= %d <= 1024' % (val,))
    if val & val - 1:
        raise ValueError('invalid block size, expected power of 2, got %d' % (val,))
    return val


def chunks(seq, n):
    for i in range(0, len(seq), n):
        yield seq[i:i + n]


def make_response(req, code, content='', extra_opts=[], type=coap.Type.ACKNOWLEDGEMENT):
    return coap.Packet(type=type,
                       code=code,
                       msg_id=(req.msg_id if type in (coap.Type.ACKNOWLEDGEMENT, coap.Type.RESET) else ANY),
                       token=req.token,
                       options=([coap.ContentFormatOption.APPLICATION_OCTET_STREAM] if content else []) + extra_opts,
                       content=content)

def _create_coap_server(port: int,
                        psk_identity: str = None,
                        psk_key: str = None,
                        ca_path: str = None,
                        ca_file: str = None,
                        crt_file: str = None,
                        key_file: str = None,
                        ipv6: bool = False,
                        debug: bool = False,
                        reuse_port: bool = False,
                        connection_id: str = ''):
    """
    Sets up a CoAP(s) server bound to given PORT.

    If any of: PSK_IDENTITY, PSK_KEY, CA_PATH, CA_FILE, CRT_FILE, KEY_FILE
    are specified, sets up a DTLS server, otherwise - raw CoAP server.
    """
    use_psk = psk_identity is not None and psk_key is not None
    use_cert = any((ca_path, ca_file, crt_file, key_file))
    if use_psk and use_cert:
        raise RuntimeError("Cannot use PSK and Certificates at the same time")

    use_dtls = use_psk or use_cert
    if debug and not use_dtls:
        print('warning: debug mode not available on non-DTLS sockets')

    if port is None or port < 0:
        port = DEFAULT_COAPS_PORT if use_dtls else DEFAULT_COAP_PORT

    if use_dtls:
        if use_psk:
            return coap.DtlsServer(psk_key=psk_key, psk_identity=psk_identity,
                                   listen_port=port, debug=debug, use_ipv6=ipv6,
                                   reuse_port=reuse_port, connection_id=connection_id)
        else:
            return coap.DtlsServer(ca_path=ca_path, ca_file=ca_file, crt_file=crt_file,
                                   key_file=key_file, listen_port=port, debug=debug,
                                   use_ipv6=ipv6, reuse_port=reuse_port, connection_id=connection_id)
    else:
        if len(connection_id) > 0:
            raise RuntimeError('Cannot use connection_id in NoSec mode')

        transport = coap.transport.Transport.UDP
        return coap.Server(port, ipv6, reuse_port=reuse_port, transport=transport)


class CoapFileServer:
    def __init__(self,
                 root_directory: str,
                 port: int,
                 psk_identity: str = None,
                 psk_key: str = None,
                 ca_path: str = None,
                 ca_file: str = None,
                 crt_file: str = None,
                 key_file: str = None,
                 ipv6: bool = False,
                 debug: bool = False):
        assert port != 0

        self.root_directory = os.path.abspath(root_directory)
        self.listen_port = port
        self.psk_identity = psk_identity
        self.psk_key = psk_key
        self.ca_path = ca_path
        self.ca_file = ca_file
        self.crt_file = crt_file
        self.key_file = key_file
        self.ipv6 = ipv6
        self.debug = debug

    def _path_to_filename(self, path: str):
        return os.path.join(self.root_directory, path.lstrip('/'))

    def _read_file_chunk(self,
                         path: str,
                         offset: int,
                         size: int) -> Tuple[bytes, bool]: # data, has_more
        file_path = self._path_to_filename(path)
        stat = os.stat(file_path)

        with open(file_path, 'rb') as f:
            f.seek(offset)
            data = f.read(size)

        return (data, offset + size < stat.st_size)

    @staticmethod
    def _handle_bad_request(req):
        return coap.Packet(type=coap.Type.ACKNOWLEDGEMENT,
                           code=coap.Code.RES_BAD_REQUEST,
                           msg_id=req.msg_id,
                           token=req.token)

    @staticmethod
    def _handle_ping(req):
        return coap.Packet(type=coap.Type.RESET,
                           code=coap.Code.EMPTY,
                           msg_id=req.msg_id,
                           token=b'')

    def _handle_get(self, req):
        path = req.get_uri_path()
        block = req.get_options(coap.Option.BLOCK2)

        try:
            with open(self._path_to_filename(path), 'rb') as f:
                crc32 = binascii.unhexlify(hex(binascii.crc32(f.read()))[len('0x'):])

            seq_num = 0
            block_size = 1024
            if block:
                seq_num = block[0].seq_num()
                block_size = block[0].block_size()

            data, has_more = self._read_file_chunk(path,
                                                   offset=seq_num * block_size,
                                                   size=block_size)

            extra_opts = [coap.Option.ETAG(crc32)]
            if block or has_more:
                extra_opts += [coap.Option.BLOCK2(seq_num=seq_num,
                                                  has_more=has_more,
                                                  block_size=block_size)]

                return make_response(req=req,
                                     code=coap.Code.RES_CONTENT,
                                     content=data,
                                     extra_opts=extra_opts)
        except FileNotFoundError:
            return make_response(req=req,
                                 code=coap.Code.RES_NOT_FOUND)

    @staticmethod
    def _handle_unsupported(req):
        return coap.Packet(type=coap.Type.ACKNOWLEDGEMENT,
                           code=coap.Code.RES_METHOD_NOT_ALLOWED,
                           msg_id=req.msg_id,
                           token=req.token)

    @staticmethod
    def _packet_summary(pkt):
        def block_summary(pkt):
            blk1 = pkt.get_options(coap.Option.BLOCK1)
            blk2 = pkt.get_options(coap.Option.BLOCK2)
            return ', '.join(['']
                             + ([repr(blk1[0])] if blk1 else [])
                             + ([repr(blk2[0])] if blk2 else []))

        return ('%s, %s, id=%s, token=%s%s'
                % (pkt.code,
                   pkt.type,
                   pkt.msg_id,
                   pkt.token,
                   block_summary(pkt)))

    def _handle_packet(self, serv, req):
        res = None

        try:
            print('<- %s' % self._packet_summary(req))
            if self.debug:
                print(req)

            if req.type not in (coap.Type.CONFIRMABLE, coap.Type.NON_CONFIRMABLE):
                print('-> ignored (neither CON or NON)')
                return

            if req.code == coap.Code.EMPTY:
                if len(req.token) == 0:
                    res = self._handle_ping(req)
                else:
                    res = self._handle_bad_request(req)

            if req.code == coap.Code.REQ_GET:
                res = self._handle_get(req)

            if res is None:
                res = self._handle_unsupported(req)

            print('-> %s' % self._packet_summary(res))
            if self.debug:
                print(res)
        except KeyboardInterrupt:
            raise
        except:
            import traceback
            traceback.print_exc()
            res = make_response(req=req,
                                code=coap.Code.RES_INTERNAL_SERVER_ERROR)

        serv.send(res)


    def _create_server(self):
        serv = _create_coap_server(port=self.listen_port,
                                   psk_identity=self.psk_identity,
                                   psk_key=self.psk_key,
                                   ca_path=self.ca_path,
                                   ca_file=self.ca_file,
                                   crt_file=self.crt_file,
                                   key_file=self.key_file,
                                   ipv6=self.ipv6,
                                   debug=self.debug,
                                   reuse_port=True)
        return (serv.socket.fileno(), serv)

    @staticmethod
    def _poll_event_to_string(evt):
        return {
            select.POLLIN: 'POLLIN',
            select.POLLOUT: 'POLLOUT',
            select.POLLERR: 'POLLERR',
            select.POLLPRI: 'POLLPRI',
            select.POLLHUP: 'POLLHUP',
            select.POLLNVAL: 'POLLNVAL',
        }.get(evt, 'unknown poll event: %d' % evt)

    def serve_forever(self):
        servers = {}
        poll = select.poll()

        def ensure_listening_server_exists():
            if not any(s.get_remote_addr() is None for s in servers.values()):
                serv_fd, coap_serv = self._create_server()
                servers[serv_fd] = coap_serv
                poll.register(serv_fd, select.POLLIN | select.POLLERR | select.POLLHUP)

        try:
            print('Serving directory %s on port %s...' % (self.root_directory, self.listen_port))
            print('Press CTRL-C to stop')

            while True:
                ensure_listening_server_exists()
                for fd, event in poll.poll():
                    if event in (select.POLLERR, select.POLLHUP):
                        print('fd %d: %s' % (fd, self._poll_event_to_string(event)))
                        del servers[fd]
                    elif event == select.POLLIN:
                        try:
                            serv = servers[fd]
                            pkt = serv.recv()
                            # recv() connects the server to a remote endpoint; make sure
                            # there is always an "unconnected" one
                            ensure_listening_server_exists()
                            self._handle_packet(serv, pkt)
                        except ValueError as e:
                            print(e)
                    else:
                        print('fd %d: %s' % (fd, self._poll_event_to_string(event)))
        finally:
            for serv in servers.values():
                try:
                    serv.close()
                except:
                    pass


Send = collections.namedtuple('Send', ['msg'])
Recv = collections.namedtuple('Recv', ['msg'])

NSH_HISTORY_FILE = os.path.join(os.path.expanduser('~'), '.nsh_history')

class Lwm2mCmd(powercmd.Cmd):
    def set_prompt(self, extra_text=None):
        extra_text = ' %s' % (extra_text,) if extra_text else ''
        self.prompt = '[%s]%s $ ' % (self.__class__.__name__, extra_text)

    def __init__(self, cmdline_args):
        super().__init__(history=FileHistory(NSH_HISTORY_FILE))

        self.cmdline_args = cmdline_args
        self.serv = None
        self.payload_buffer = b''
        self.expected_message = ANY
        self.set_prompt()
        self.history = []

        self.auto_reregister = True
        self.auto_update = True
        self.auto_ack = True

        self.history = []

        if cmdline_args.listen is not None:
            port = self.cmdline_args.listen
            if port is None or port < 0:
                use_dtls = self.cmdline_args.psk_identity is not None and self.cmdline_args.psk_key is not None
                port = DEFAULT_COAPS_PORT if use_dtls else DEFAULT_COAP_PORT
            try:
                self.do_listen()
            except Exception as e:
                print('could not listen on port %d (%s)' % (port, e))
                print('use "listen" command to start the server')

    def _get_last_request(self):
        for entry in reversed(self.history):
            if isinstance(entry, Recv) and entry.msg.code.is_request():
                return entry.msg

        return None

    def do_reset_history(self):
        "Clears command history."
        self.history = []

    def do_details(self,
                    idx: int = 1):
        """
        Displays details of a recent message.

        Examples:
            /details     - display last message
            /details NUM - display NUM-th last message
        """

        for entry in reversed(self.history):
            if (isinstance(entry, Recv)
                or isinstance(entry, Send)):
                idx -= 1
                if idx <= 0:
                    print('\n*** %s ***' % (entry.__class__.__name__))
                    print(entry.msg.details())
                    return

        print('message not found')

    def do_connect(self,
                   host: str = None,
                   port: int = DEFAULT_COAP_PORT):
        """
        Connects the socket to given HOST:PORT. Future packets will be sent to
        this address.
        """
        host = host or ('::1' if self.cmdline_args.ipv6 else '127.0.0.1')
        self.serv = None
        self.serv = coap.Server(use_ipv6=self.cmdline_args.ipv6)
        self.serv.connect_to_client((host, port))
        print('new remote endpoint: %s:%d' % (host, port))

    def do_unconnect(self):
        """
        "Unconnects" the socket from an already accepted client. The idea is that
        then the server will be able to receive packets from different (host, port),
        which may be useful for testing purposes.
        """
        self.serv._raw_udp_socket.connect(('',0))

    def do_listen(self,
                  port: int = None,
                  psk_identity: str = None,
                  psk_key: str = None,
                  ca_path: str = None,
                  ca_file: str = None,
                  crt_file: str = None,
                  key_file: str = None,
                  ipv6: bool = None,
                  debug: bool = None,
                  connection_id: str = ''):
        """
        Starts listening on given PORT. If PSK_IDENTITY and PSK_KEY are
        specified, sets up a DTLS server, otherwise - raw CoAP server.
        """

        try:
            self.serv = None

            port = port or self.cmdline_args.listen
            psk_identity = psk_identity or self.cmdline_args.psk_identity
            psk_key = psk_key or self.cmdline_args.psk_key
            debug = debug or self.cmdline_args.debug
            ipv6 = ipv6 or self.cmdline_args.ipv6

            coap_serv = _create_coap_server(port=port,
                                            psk_identity=psk_identity,
                                            psk_key=psk_key,
                                            ca_path=ca_path,
                                            ca_file=ca_file,
                                            crt_file=crt_file,
                                            key_file=key_file,
                                            ipv6=ipv6,
                                            debug=debug,
                                            connection_id=connection_id)
            self.serv = Lwm2mServer(coap_serv)

            print('waiting for a client on port %d ...'
                  % (self.serv.get_listen_port(),))
            self.serv.listen(timeout_s=None)
            msg = self._recv()

            # When using IPv6, get_remote_addr() returns 4-tuple with some additional
            # information we don't need.
            self.set_prompt('port: %d, client: %s:%d'
                            % ((self.serv.get_listen_port(),) + self.serv.get_remote_addr()[:2]))

            if isinstance(msg, Lwm2mRegister):
                self._send(Lwm2mCreated.matching(msg)(location=REGISTER_PATH))
            elif isinstance(msg, Lwm2mRequestBootstrap):
                self._send(Lwm2mChanged.matching(msg)())

            self.payload_buffer = b''
        except KeyboardInterrupt:
            pass
        except Exception as e:
            print(e)
            raise e

    def do_payload_buffer_clear(self):
        self.payload_buffer = b''

    def do_payload_buffer_show(self):
        print(repr(self.payload_buffer))

    def do_payload_buffer_show_hex(self):
        print(coap.utils.hexlify(self.payload_buffer))

    def do_payload_buffer_show_tlv(self):
        print(TLV.parse(self.payload_buffer))

    def _msg_verbose_compare(self, expected, actual):
        log = ''

        if expected is None:
            log += 'no message expected'
        elif actual is None:
            log += 'no message received'
        else:
            if actual.version is not None:
                if expected.version != actual.version:
                    log += 'unexpected CoAP version\n'
            if actual.type is not None:
                if expected.type != actual.type:
                    log += 'unexpected CoAP type\n'
            if expected.code != actual.code:
                log += 'unexpected CoAP code\n'

            if expected.msg_id is not ANY:
                if expected.msg_id != actual.msg_id:
                    log += 'unexpected CoAP message ID\n'
            if expected.token is not ANY:
                if expected.token != actual.token:
                    log += 'unexpected CoAP token\n'
            if expected.options is not ANY:
                if expected.options != actual.options:
                    log += 'unexpected CoAP option list\n'
            if expected.content is not ANY:
                if expected.content != actual.content:
                    log += 'unexpected CoAP content\n'

        if log:
            print('*** unexpected message ***\n'
                  '%s\n'
                  '--- GOT ---\n'
                  '%s\n'
                  '--- EXPECTED ---\n'
                  '%s\n'
                  '------' % (log, actual, expected))

    def _recv(self, timeout_s=None):
        msg = None

        try:
            pkt = self.serv.recv(timeout_s)
            msg = get_lwm2m_msg(pkt)
            print('<- %s' % (msg.summary(),))
            self.history.append(Recv(msg))

            self.payload_buffer += pkt.content
        finally:
            if self.expected_message is not ANY:
                self._msg_verbose_compare(self.expected_message, msg)
                self.expected_message = ANY

        return msg

    def _send(self, msg: Lwm2mMsg, timeout_s: float = 3):
        if not self.serv:
            raise Exception('not connected to any remote host')

        msg.fill_placeholders()

        print('-> %s' % (msg.summary(),))
        self.history.append(Send(msg))
        self.serv.send(msg)

        if msg.type == coap.Type.CONFIRMABLE:
            try:
                return self._recv(timeout_s=timeout_s)
            except socket.timeout:
                print('response not received')

    def do_coap(self,
                type: coap.Type = coap.Type.CONFIRMABLE,
                code: coap.Code = coap.Code.REQ_GET,
                msg_id: int = ANY,
                token: EscapedBytes = b'',
                options: List[coap.Option] = [],
                content: EscapedBytes = b'',
                respond: bool = True):
        """
        Send a custom CoAP message.

        If message CODE indicates a response, attempts to match MSG_ID and
        TOKEN to a last received request unless RESPOND is set to False.
        """
        if msg_id != ANY or token != b'' or not code.is_response:
            respond = False

        if respond:
            last_req = self._get_last_request()
            if not last_req:
                raise ValueError('no request to respond to')
            else:
                print('responding to: %s' % (last_req.summary(),))

            self._send(Lwm2mMsg.from_packet(make_response(last_req, code, content, options)))
        else:
            self._send(Lwm2mMsg(type=type,
                                code=code,
                                msg_id=msg_id,
                                token=token,
                                options=options,
                                content=content))

    def do_lwm2m_decode(self,
                        data: EscapedBytes):
        """
        Decodes a CoAP message and displays it in a human-readable form.
        """
        print(get_lwm2m_msg(coap.Packet.parse(data)))

    def do_coap_decode(self,
                       data: EscapedBytes):
        """
        Decodes a CoAP message and displays it in a human-readable form.
        """
        print(coap.Packet.parse(data))

    def do_tlv(self):
        """
        Launch a TLV sub-shell that facilitates creating TLV payloads.
        """
        bracket_idx = self.prompt.index(']')
        prompt = self.prompt[:bracket_idx] + '/TLV' + self.prompt[bracket_idx:]

        tlv_shell = TLVBuilderShell(prompt)
        tlv_shell.cmdloop()
        tlv_shell.do_serialize()

    def do_set(self,
               auto_update: bool=None,
               auto_reregister: bool=None,
               auto_ack: bool=None):
        if auto_reregister is not None:
            self.auto_reregister = auto_reregister
            print('Auto register responses %s' % ('enabled' if self.auto_reregister else 'disabled',))
        if auto_update is not None:
            self.auto_update = auto_update
            print('Auto update responses %s' % ('enabled' if self.auto_update else 'disabled',))
        if auto_ack is not None:
            self.auto_ack = auto_ack
            print('Auto 0.00 ACK responses %s' % ('enabled' if self.auto_ack else 'disabled',))

    def try_read(self, timeout_s=0.01):
        try:
            msg = self._recv(timeout_s=timeout_s)

            if isinstance(msg, Lwm2mDeregister):
                self._send(Lwm2mDeleted.matching(msg)())
            elif self.auto_reregister and isinstance(msg, Lwm2mRegister):
                self._send(Lwm2mCreated.matching(msg)(location=REGISTER_PATH))
            elif (self.auto_update and isinstance(msg, Lwm2mUpdate)) or isinstance(msg, Lwm2mSend):
                self._send(Lwm2mChanged.matching(msg)())
            elif (self.auto_ack and msg.type == coap.Type.CONFIRMABLE):
                self._send(Lwm2mEmpty.matching(msg)())

            return msg
        except socket.timeout:
            pass

    def do_sleep(self, timeout_s: float):
        """
        Blocks for TIMEOUT_S seconds.
        """
        import time
        time.sleep(timeout_s)

    def do_recv(self, timeout_s: float=None):
        """
        Waits for a next incoming message. If TIMEOUT_S is specified, the
        command will not wait longer than TIMEOUT_S if no messages are received.
        """
        self.try_read(timeout_s)

    def do_expect(self,
                  msg_code: str):
        """
        Makes the shell compare next received packet against the one configured
        via this command and print a message if a mismatch is detected.

        MSG_CODE can be:
        - a string with Python code that evalutes to a correct message,
        - None, if no messages are expected,
        - ANY to disable checking (default).

        Note: after receiving each message the "expected" value is set to ANY.
        """
        self.expected_message = eval(msg_code)
        if isinstance(self.expected_message, Lwm2mMsg):
            print('Expecting: %s' % (self.expected_message.summary()))
        else:
            print('Expecting: %s' % (str(self.expected_message),))

    def do_bootstrap(self,
                     uri: str,
                     security_mode: SecurityMode = None,
                     psk_identity: EscapedBytes = None,
                     psk_key: EscapedBytes = None,
                     client_cert_path: str = None,
                     client_private_key_path: str = None,
                     server_cert_path: str = None,
                     ssid: int = 1,
                     is_bootstrap: bool = False,
                     lifetime: int = 86400,
                     notification_storing: bool = False,
                     binding: str = 'U',
                     iid: int = 1,
                     finish: bool = True,
                     tls_ciphersuites: List[int] = []):
        """
        Sets up a Security and Server instances for an LwM2M server.

        In case of PreSharedKey security mode, PSK_IDENTITY and PSK_KEY
        are literal sequences to be used as DTLS identity and secret key.

        In case of Certificate security mode, CLIENT_CERT_PATH and
        SERVER_CERT_PATH shall be paths to binary DER-encoded X.509
        certificates, and CLIENT_PRIVATE_KEY_PATH to binary DER-encoded
        PKCS#8 file, which MUST NOT be password-protected.

        If IS_BOOTSTRAP is True, only the Security object instance is
        configured. LIFETIME, NOTIFICATION_STORING and BINDING are ignored
        in such case. SSID is still set for the Security instance.

        Both Security and Server object instances are created with given IID.

        If FINISH is set to True, a Bootstap Finish message will be sent
        after setting up Security/Server instances.
        """
        if security_mode is None:
            security_mode = {
                'coap': SecurityMode.NoSec,
                'coaps': SecurityMode.PreSharedKey
            }[uri.split(':')[0]]

        if ((psk_identity or psk_key)
                and (client_cert_path or client_private_key_path or server_cert_path)):
            print('Cannot set both PSK and cert mode at the same time')
            return

        pubkey_or_identity = b''
        if psk_identity:
            pubkey_or_identity = psk_identity
        elif client_cert_path:
            with open(client_cert_path, 'rb') as f:
                pubkey_or_identity = f.read()

        privkey = b''
        if psk_key:
            privkey = psk_key
        elif client_private_key_path:
            with open(client_private_key_path, 'rb') as f:
                privkey = f.read()

        server_pubkey_or_identity = b''
        if server_cert_path:
            with open(server_cert_path, 'rb') as f:
                server_pubkey_or_identity = f.read()

        security = TLV.make_instance(iid,
                                     [TLV.make_resource(0, uri),
                                      TLV.make_resource(1, 1 if is_bootstrap else 0),
                                      TLV.make_resource(2, security_mode.value),
                                      TLV.make_resource(3, pubkey_or_identity),
                                      TLV.make_resource(4, server_pubkey_or_identity),
                                      TLV.make_resource(5, privkey),
                                      TLV.make_resource(10, ssid),
                                      TLV.make_multires(16, enumerate(tls_ciphersuites))])
        server = TLV.make_instance(iid,
                                   [TLV.make_resource(0, ssid),
                                    TLV.make_resource(1, lifetime),
                                    TLV.make_resource(6, 1 if notification_storing else 0),
                                    TLV.make_resource(7, binding)])

        self._send(Lwm2mWrite('/0', security.serialize(), format=coap.ContentFormat.APPLICATION_LWM2M_TLV))

        if not is_bootstrap:
            self._send(Lwm2mWrite('/1', server.serialize(), format=coap.ContentFormat.APPLICATION_LWM2M_TLV))

        if finish:
            self._send(Lwm2mBootstrapFinish())

    def do_write_file(self,
                      fname: str,
                      path: str or Lwm2mResourcePath,
                      format: coap.ContentFormatOption = coap.ContentFormatOption.APPLICATION_OCTET_STREAM,
                      chunksize: int = 1024,
                      timeout_s: float = 3):
        """
        Opens file fname and attempts to push it using BLOCK1 to the Client.
        """
        with open(fname, 'rb') as f:
            contents = f.read()
            maxindex = (len(contents) + chunksize - 1) // chunksize
            for index, chunk in enumerate([contents[i*chunksize:(i+1)*chunksize] for i in range(maxindex)]):
                response = self._send(
                    Lwm2mWrite(path=path,
                               content=chunk,
                               format=format,
                               options=[coap.Option.BLOCK1(index, index < maxindex - 1, chunksize)]),
                    timeout_s=timeout_s)
                if response.code != coap.Code.RES_CONTINUE:
                    break

    def do_udp(self,
               content : EscapedBytes):
        if hasattr(self.serv.socket, 'py_socket'):
            self.serv.socket.py_socket.sendall(content)
        else:
            self.serv.socket.sendall(content)


    def do_file_server(self,
                       root_directory: str = '.',
                       port: int = None,
                       psk_identity: str = None,
                       psk_key: str = None,
                       ca_path: str = None,
                       ca_file: str = None,
                       crt_file: str = None,
                       key_file: str = None,
                       ipv6: bool = False,
                       debug: bool = False):
        """
        Serves files from ROOT_DIRECTORY over CoAP(s).
        """

        dtls = (psk_identity and psk_key) or (ca_path or ca_file or crt_file or key_file)
        port = port or (5684 if dtls else 5683)

        try:
            CoapFileServer(root_directory=root_directory,
                           port=port,
                           psk_identity=psk_identity,
                           psk_key=psk_key,
                           ca_path=ca_path,
                           ca_file=ca_file,
                           crt_file=crt_file,
                           key_file=key_file,
                           ipv6=ipv6,
                           debug=debug).serve_forever()
        except KeyboardInterrupt:
            pass


    def emptyline(self):
        while self.try_read():
            pass

def _make_command_handler(cls):
    """
    Turn given CLS, representing a CoAP or LWM2M message, into a callable
    command.

    Since the powercmd engine uses method signature to perform tab-completion,
    we must ensure that constructor signature is retained in the wrapper.
    """
    import functools

    # this decorator makes send_msg signature be a clone of cls.__init__ one
    @functools.wraps(cls.__init__)
    def send_msg(self, *args, **kwargs):
        try:
            self._send(cls(*args, **kwargs))
        except Exception as e:
            raise e.__class__('could not send %s (%s)' % (cls.__name__, e))

    return send_msg


def _snake_case_from_camel_case(name):
    """
    Transforms camelCase/PascalCase string into snake_case.
    Source: http://stackoverflow.com/a/1176023/2339636
    """
    s1 = re.sub('(.)([A-Z][a-z]+)', r'\1_\2', name)
    return re.sub('([a-z0-9])([A-Z])', r'\1_\2', s1).lower()


def _is_command_class(cls):
    """
    Checks if CLS represents a CoAP or LWM2M message that can be turned into
    a shell command.

    Such class must be derived from Lwm2mMsg, and all its arguments (excluding
    `self`) must have a type annotation.
    """
    if type(cls) is not type:
        return False
    if not issubclass(cls, Lwm2mMsg):
        return False

    import inspect
    params = inspect.signature(cls.__init__).parameters

    if any(p.annotation is inspect.Parameter.empty
           for p in list(params.values())[1:]):  # ignore 'self'
        print('ignoring %s: not command-compatible' % (cls.__name__,))
        return False

    return True


def _load_message_commands():
    """
    Load all Lwm2mMsg subclasses containing constructors with command-compatible
    signatures (i.e. having type annotations for all non-self arguments)
    """
    from lwm2m import messages

    cmds_loaded = 0

    for cls_name, cls in messages.__dict__.items():
        if _is_command_class(cls):
            if cls_name.lower().startswith('lwm2m'):
                name = cls_name[len('lwm2m'):]
            else:
                name = cls_name
            name = 'do_' + _snake_case_from_camel_case(name)

            if hasattr(Lwm2mCmd, name):
                raise ValueError('multiple definitions of command %s' % (name,))

            try:
                setattr(Lwm2mCmd, name, _make_command_handler(cls))
            except Exception as e:
                raise ValueError('could not load command: %s (%s)' % (name, e))

            cmds_loaded += 1

    return cmds_loaded


_cmds_loaded = _load_message_commands()
print('loaded %d message types' % (_cmds_loaded,))
print('')

if __name__ == '__main__':
    LOG_LEVEL = os.getenv('LOGLEVEL', 'info').upper()
    try:
        import coloredlogs
        coloredlogs.install(level=LOG_LEVEL)
    except ImportError:
        logging.basicConfig(level=LOG_LEVEL)

    parser = argparse.ArgumentParser()
    parser.add_argument('--ipv6', '-6', default=False, action='store_true',
                        help=('Use IPv6 by default.'))
    parser.add_argument('--listen', '-l',
                        type=int, const=-1, metavar='PORT', nargs='?',
                        help=('Immediately starts listening on specified CoAP port. '
                              'If PORT is not specified, default one is used (%s '
                              'for CoAP, %s for CoAP/(D)TLS)' % (DEFAULT_COAP_PORT,
                                                                 DEFAULT_COAPS_PORT)))
    parser.add_argument('--psk-identity', '-i',
                        type=str, metavar='IDENTITY',
                        help='PSK identity to use for DTLS connection (literal string).')
    parser.add_argument('--psk-key', '-k',
                        type=str, metavar='KEY',
                        help='PSK key to use for DTLS connection (literal string).')
    parser.add_argument('--debug', action='store_true',
                        help='Enable mbed TLS debug output')

    cmdline_args = parser.parse_args()

    Lwm2mCmd(cmdline_args).cmdloop()
