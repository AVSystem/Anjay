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
import contextlib
from typing import NamedTuple, Optional
import socket
import struct
import threading
import zlib

import time

from .lwm2m import coap
from .lwm2m.path import CoapPath
from .lwm2m.messages import *


class CoapFileServer:
    Resource = NamedTuple('Resource', [('etag', bytes), ('data', bytes)])

    def __init__(self, coap_server: coap.Server, binding='U'):
        self._resources = {}
        self._server = coap_server
        self.requests = []
        self.should_ignore_request = lambda _: False
        self.binding = binding

    def set_resource(self,
                     path: str,
                     data: Optional[bytes],
                     etag: Optional[bytes] = None):
        if data is not None:
            if etag is None:
                etag = struct.pack('>I', zlib.crc32(data))
            self._resources[path] = self.Resource(etag=etag, data=data)
        else:
            del self._resources[path]

    def get_resource_uri(self, path: CoapPath):
        if path not in self._resources:
            raise ValueError('unknown resource: %s' % (path,))

        if isinstance(self._server, coap.DtlsServer):
            proto = 'coaps'
        elif isinstance(self._server, coap.Server):
            proto = 'coap'
        else:
            raise TypeError('unexpected server type')

        if self.binding == 'N':
            return '%s+nidd://%s' % (proto, path)
        else:
            return '%s://127.0.0.1:%d%s' % (proto, self._server.get_listen_port(), path)


    def _recv_request(self, timeout_s):
        if self._server.get_remote_addr() is None:
            try:
                self._server.listen(timeout_s=timeout_s)
            except socket.timeout:
                pass

        return self._server.recv(timeout_s=timeout_s)

    def handle_recvd_request(self, req):
        self.requests.append(req)

        if self.should_ignore_request(req):
            return

        if req.type != coap.Type.CONFIRMABLE:
            return

        if req.code.cls == 0:
            if req.code != coap.Code.REQ_GET:
                self._server.send(Lwm2mErrorResponse.matching(req)(
                    code=coap.Code.RES_METHOD_NOT_ALLOWED).fill_placeholders())
                return
        else:
            self._server.send(Lwm2mReset.matching(req).fill_placeholders())
            return

        # Confirmable GET request
        path = req.get_uri_path()
        if path not in self._resources:
            self._server.send(Lwm2mErrorResponse.matching(req)(
                code=coap.Code.RES_NOT_FOUND).fill_placeholders())
            return

        # CON GET to a known path
        block2 = req.get_options(coap.Option.BLOCK2)
        if block2:
            block2 = block2[0]
        else:
            block2 = coap.Option.BLOCK2(
                seq_num=0, has_more=False, block_size=1024)

        resource = self._resources[path]
        data_offset = block2.seq_num() * block2.block_size()
        res_block2 = coap.Option.BLOCK2(seq_num=block2.seq_num(),
                                        has_more=data_offset + block2.block_size() < len(resource.data),
                                        block_size=block2.block_size())
        content = resource.data[data_offset:data_offset + block2.block_size()]

        self._server.send(Lwm2mContent.matching(req)(content=content,
                                                     options=[res_block2, coap.Option.ETAG(resource.etag)]))


    def handle_request(self, timeout_s=5.0):
        self.handle_recvd_request(self._recv_request(timeout_s=timeout_s))


class CoapFileServerThread(threading.Thread):
    def __init__(self, coap_server: coap.Server = None):
        super().__init__()

        self._mutex = threading.RLock()
        self._file_server = CoapFileServer(coap_server or coap.Server())
        self._shutdown = False

    def run(self):
        while not self._shutdown:
            try:
                with self._mutex:
                    self._file_server.handle_request()
            except socket.timeout:
                pass
            time.sleep(0.01)  # yield to the scheduler

    def join(self):
        self._shutdown = True
        super().join()

    @property
    @contextlib.contextmanager
    def file_server(self):
        with self._mutex:
            yield self._file_server
