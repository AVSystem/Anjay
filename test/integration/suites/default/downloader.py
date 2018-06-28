# -*- coding: utf-8 -*-
#
# Copyright 2017-2018 AVSystem <avsystem@avsystem.com>
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

import concurrent.futures
import contextlib
import os
import time

from framework.coap_file_server import CoapFileServerThread
from framework.lwm2m_test import *

DUMMY_PAYLOAD = os.urandom(16 * 1024)


class CoapDownload:
    class Test(test_suite.Lwm2mSingleServerTest):
        def setUp(self, coap_server: coap.Server = None):
            super().setUp()

            self.file_server_thread = CoapFileServerThread(coap_server or coap.Server())
            self.file_server_thread.start()

            self.tempfile = tempfile.NamedTemporaryFile()

        @property
        def file_server(self):
            return self.file_server_thread.file_server

        def register_resource(self, path, *args, **kwargs):
            with self.file_server as file_server:
                file_server.set_resource(path, *args, **kwargs)
                return file_server.get_resource_uri(path)

        def tearDown(self):
            try:
                super().tearDown()
            finally:
                self.tempfile.close()
                self.file_server_thread.join()

        def read(self, path: Lwm2mPath):
            req = Lwm2mRead(path)
            self.serv.send(req)
            res = self.serv.recv()
            self.assertMsgEqual(Lwm2mContent.matching(req)(), res)
            return res.content

        def wait_until_downloads_finished(self):
            while self.get_socket_count() > len(self.servers):
                time.sleep(0.1)

    class ReconnectTest(test_suite.Lwm2mSingleServerTest):
        def setUp(self, coap_server: coap.Server = None):
            super().setUp()
            self.communicate('trim-servers 0')
            self.assertDemoDeregisters()
            self.file_server = (coap_server or coap.Server())
            self.file_server.set_timeout(5)
            self.tempfile = tempfile.NamedTemporaryFile()

        def tearDown(self):
            self.file_server.close()
            self.tempfile.close()
            super().tearDown(auto_deregister=False)

        @contextlib.contextmanager
        def downloadContext(self, path, psk_identity='', psk_key='', payload=DUMMY_PAYLOAD):
            # "download" command blocks until a connection is made, so we need to listen() in a separate thread
            with concurrent.futures.ThreadPoolExecutor() as executor:
                listen_future = executor.submit(self.file_server.listen)
                self.communicate(
                    'download %s://127.0.0.1:%s%s %s %s %s' % (
                        'coaps' if isinstance(self.file_server, coap.DtlsServer) else 'coap',
                        self.file_server.get_listen_port(), path, self.tempfile.name, psk_identity, psk_key))
                listen_future.result()
            self.assertEqual(1, self.get_socket_count())

            test_case = self

            class DownloadContext:
                def __init__(self):
                    self.seq_num = 0
                    self.block_size = 1024

                @property
                def read_offset(self):
                    return self.seq_num * self.block_size

                def transferData(self, bytes_limit=len(payload)):
                    while self.read_offset < bytes_limit:
                        req = test_case.file_server.recv()
                        test_case.assertMsgEqual(CoapGet(path, options=[
                            coap.Option.BLOCK2(seq_num=self.seq_num, block_size=self.block_size, has_more=0)]), req)
                        block2opt = coap.Option.BLOCK2(
                            seq_num=self.seq_num,
                            has_more=(len(DUMMY_PAYLOAD) > self.read_offset + self.block_size),
                            block_size=self.block_size)
                        test_case.file_server.send(Lwm2mContent.matching(req)(
                            content=DUMMY_PAYLOAD[self.read_offset:self.read_offset + self.block_size],
                            options=[block2opt]))
                        self.seq_num += 1

            yield DownloadContext()

            deadline = time.time() + 10
            while self.get_socket_count() > 0:
                if time.time() > deadline:
                    self.fail('Download not finished in time')
                time.sleep(0.1)

            with open(self.tempfile.name, 'rb') as f:
                self.assertEqual(f.read(), DUMMY_PAYLOAD)


class CoapDownloadSockets(CoapDownload.Test):
    def runTest(self):
        self.communicate('download %s %s' % (self.register_resource('/', DUMMY_PAYLOAD), self.tempfile.name))

        self.assertEqual(2, self.get_socket_count())
        self.assertEqual(1, self.get_non_lwm2m_socket_count())
        self.assertEqual('UDP', self.get_transport(socket_index=-1))

        # make sure the download is actually done
        self.wait_until_downloads_finished()
        with open(self.tempfile.name, 'rb') as f:
            self.assertEqual(f.read(), DUMMY_PAYLOAD)


class CoapDownloadDoesNotBlockLwm2mTraffic(CoapDownload.Test):
    def runTest(self):
        self.communicate('download %s %s' % (self.register_resource('/', DUMMY_PAYLOAD), self.tempfile.name))

        for _ in range(10):
            self.read(ResPath.Device.SerialNumber)

        # make sure the download is actually done
        self.wait_until_downloads_finished()
        with open(self.tempfile.name, 'rb') as f:
            self.assertEqual(f.read(), DUMMY_PAYLOAD)


class CoapDownloadIgnoresUnrelatedRequests(CoapDownload.Test):
    def runTest(self):
        self.communicate('download %s %s' % (self.register_resource('/', DUMMY_PAYLOAD), self.tempfile.name))
        self.assertEqual(2, self.get_socket_count())

        # wait for first request
        while True:
            with self.file_server as file_server:
                if len(file_server.requests) != 0:
                    break
            time.sleep(0.001)

        for _ in range(10):
            with self.file_server as file_server:
                file_server._server.send(Lwm2mRead(ResPath.Device.SerialNumber).fill_placeholders())

        # make sure the download is actually done
        self.wait_until_downloads_finished()
        with open(self.tempfile.name, 'rb') as f:
            self.assertEqual(f.read(), DUMMY_PAYLOAD)


class CoapDownloadOverDtls(CoapDownload.Test):
    PSK_IDENTITY = b'Help'
    PSK_KEY = b'ImTrappedInAUniverseFactory'

    def setUp(self):
        super().setUp(coap_server=coap.DtlsServer(psk_key=self.PSK_KEY, psk_identity=self.PSK_IDENTITY))

    def runTest(self):
        self.communicate('download %s %s %s %s' % (self.register_resource('/', DUMMY_PAYLOAD),
                                                   self.tempfile.name,
                                                   self.PSK_IDENTITY.decode('ascii'),
                                                   self.PSK_KEY.decode('ascii')))

        # make sure the download is actually done
        self.wait_until_downloads_finished()
        with open(self.tempfile.name, 'rb') as f:
            self.assertEqual(f.read(), DUMMY_PAYLOAD)


class CoapDownloadRetransmissions(CoapDownload.Test):
    def runTest(self):
        def should_ignore_request(req):
            try:
                seq_num = req.get_options(coap.Option.BLOCK2)[0].seq_num()
                required_retransmissions = seq_num % 4
                with self.file_server as file_server:
                    num_retransmissions = len([x for x in file_server.requests if x == req])
                return num_retransmissions < required_retransmissions
            except:
                return False

        with self.file_server as file_server:
            file_server.set_resource('/', DUMMY_PAYLOAD)
            file_server.should_ignore_request = should_ignore_request
            uri = file_server.get_resource_uri('/')

        self.communicate('download %s %s' % (uri, self.tempfile.name))

        # make sure download succeeded
        self.wait_until_downloads_finished()
        with open(self.tempfile.name, 'rb') as f:
            self.assertEqual(f.read(), DUMMY_PAYLOAD)


class CoapDownloadAbortsOnErrorResponse(CoapDownload.Test):
    def runTest(self):
        self.communicate('download %s %s' % (self.register_resource('/', DUMMY_PAYLOAD), self.tempfile.name))
        self.assertEqual(2, self.get_socket_count())
        with self.file_server as file_server:
            file_server.set_resource('/', None)

        # make sure download was aborted
        self.wait_until_downloads_finished()
        with open(self.tempfile.name, 'rb') as f:
            self.assertNotEqual(f.read(), DUMMY_PAYLOAD)


class CoapDownloadAbortsOnETagChange(CoapDownload.Test):
    def runTest(self):
        self.communicate('download %s %s' % (self.register_resource('/', DUMMY_PAYLOAD), self.tempfile.name))
        self.assertEqual(2, self.get_socket_count())

        while True:
            with self.file_server as file_server:
                if len(file_server.requests) != 0:
                    old_etag = file_server._resources['/'].etag
                    new_etag = bytes([(old_etag[0] + 1) % 256]) + old_etag[1:]
                    self.assertNotEqual(old_etag, new_etag)
                    file_server.set_resource('/', DUMMY_PAYLOAD, etag=new_etag)
                    break
            time.sleep(0.001)

        # make sure download was aborted
        self.wait_until_downloads_finished()
        with open(self.tempfile.name, 'rb') as f:
            self.assertNotEqual(f.read(), DUMMY_PAYLOAD)


class CoapDownloadAbortsOnUnexpectedResponse(CoapDownload.Test):
    def runTest(self):
        with self.file_server as file_server:
            file_server.set_resource('/', DUMMY_PAYLOAD)
            file_server.should_ignore_request = lambda _: True
            uri = file_server.get_resource_uri('/')

        self.communicate('download %s %s' % (uri, self.tempfile.name))
        self.assertEqual(2, self.get_socket_count())

        while True:
            with self.file_server as file_server:
                if len(file_server.requests) != 0:
                    break
            time.sleep(0.001)

        with self.file_server as file_server:
            file_server._server.send(Lwm2mCreated.matching(file_server.requests[-1])().fill_placeholders())

        # make sure download was aborted
        self.wait_until_downloads_finished()
        with open(self.tempfile.name, 'rb') as f:
            self.assertNotEqual(f.read(), DUMMY_PAYLOAD)


class CoapDownloadResumption(CoapDownload.ReconnectTest):
    def runTest(self):
        with self.downloadContext('/test') as ctx:
            ctx.transferData(len(DUMMY_PAYLOAD) / 2)

            req = self.file_server.recv()
            self.assertMsgEqual(CoapGet('/test', options=[
                coap.Option.BLOCK2(seq_num=ctx.seq_num, block_size=ctx.block_size, has_more=0)]), req)

            previous_port = self.file_server.get_remote_addr()[1]
            self.file_server.reset()
            self.communicate('reconnect')
            self.file_server.listen()
            self.assertNotEqual(self.file_server.get_remote_addr()[1], previous_port)

            ctx.transferData()


class CoapsDownloadResumption(CoapDownload.ReconnectTest):
    PSK_IDENTITY = '1d3nt17y'
    PSK_KEY = 's3cr3tk3y'

    def setUp(self):
        super().setUp(coap.DtlsServer(psk_identity=self.PSK_IDENTITY, psk_key=self.PSK_KEY))

    def runTest(self):
        with self.downloadContext('/test', self.PSK_IDENTITY, self.PSK_KEY) as ctx:
            ctx.transferData(len(DUMMY_PAYLOAD) / 2)

            req = self.file_server.recv()
            self.assertMsgEqual(CoapGet('/test', options=[
                coap.Option.BLOCK2(seq_num=ctx.seq_num, block_size=ctx.block_size, has_more=0)]), req)

            previous_port = self.file_server.get_remote_addr()[1]
            self.file_server.reset()
            self.communicate('reconnect')
            self.file_server.listen()
            self.assertNotEqual(self.file_server.get_remote_addr()[1], previous_port)

            ctx.transferData()
