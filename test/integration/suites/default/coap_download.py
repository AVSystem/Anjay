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

from framework.lwm2m_test import *
from framework.coap_file_server import CoapFileServerThread

import sys
import os
import tempfile
import time


DUMMY_PAYLOAD = os.urandom(16 * 1024)


class CoapDownload:
    class Test(test_suite.Lwm2mSingleServerTest):
        def setUp(self, coap_server: coap.Server = None):
            super().setUp()

            self.file_server_thread = CoapFileServerThread(coap_server or coap.Server())
            self.file_server_thread.start()
            self.file_server = self.file_server_thread.file_server

            self.tempfile = tempfile.NamedTemporaryFile()


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


        def count_client_sockets(self):
            return int(self.communicate('socket-count', match_regex='SOCKET_COUNT==([0-9]+)\n').group(1))


        def wait_until_downloads_finished(self):
            while self.count_client_sockets() > len(self.servers):
                time.sleep(0.1)


class CoapDownloadDoesNotBlockLwm2mTraffic(CoapDownload.Test):
    def runTest(self):
        self.file_server.set_resource('/', DUMMY_PAYLOAD)

        self.communicate('download %s %s' % (self.file_server.get_resource_uri('/'),
                                             self.tempfile.name))

        for _ in range(10):
            self.read(ResPath.Device.SerialNumber)

        # make sure the download is actually done
        self.wait_until_downloads_finished()
        with open(self.tempfile.name, 'rb') as f:
            self.assertEqual(f.read(), DUMMY_PAYLOAD)


class CoapDownloadIgnoresUnrelatedRequests(CoapDownload.Test):
    def runTest(self):
        self.file_server.set_resource('/', DUMMY_PAYLOAD)

        self.communicate('download %s %s' % (self.file_server.get_resource_uri('/'),
                                             self.tempfile.name))
        self.assertEqual(2, self.count_client_sockets())

        # wait for first request
        while len(self.file_server.requests) == 0:
            time.sleep(0.001)

        for _ in range(10):
            self.file_server._server.send(Lwm2mRead(ResPath.Device.SerialNumber).fill_placeholders())

        # make sure the download is actually done
        self.wait_until_downloads_finished()
        with open(self.tempfile.name, 'rb') as f:
            self.assertEqual(f.read(), DUMMY_PAYLOAD)


class CoapDownloadOverDtls(CoapDownload.Test):
    PSK_IDENTITY = b'Help'
    PSK_KEY = b'ImTrappedInAUniverseFactory'

    def setUp(self):
        super().setUp(coap_server=coap.DtlsServer(psk_identity=self.PSK_IDENTITY,
                                                  psk_key=self.PSK_KEY))

    def runTest(self):
        self.file_server.set_resource('/', DUMMY_PAYLOAD)

        self.communicate('download %s %s %s %s' % (self.file_server.get_resource_uri('/'),
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
                num_retransmissions = len([x for x in self.file_server.requests if x == req])
                return num_retransmissions < required_retransmissions
            except:
                return False

        self.file_server.set_resource('/', DUMMY_PAYLOAD)
        self.file_server.should_ignore_request = should_ignore_request

        self.communicate('download %s %s' % (self.file_server.get_resource_uri('/'),
                                             self.tempfile.name))

        # make sure download succeeded
        self.wait_until_downloads_finished()
        with open(self.tempfile.name, 'rb') as f:
            self.assertEqual(f.read(), DUMMY_PAYLOAD)


class CoapDownloadAbortsOnErrorResponse(CoapDownload.Test):
    def runTest(self):
        self.file_server.set_resource('/', DUMMY_PAYLOAD)

        self.communicate('download %s %s' % (self.file_server.get_resource_uri('/'),
                                             self.tempfile.name))
        self.assertEqual(2, self.count_client_sockets())
        self.file_server.set_resource('/', None)

        # make sure download was aborted
        self.wait_until_downloads_finished()
        with open(self.tempfile.name, 'rb') as f:
            self.assertNotEqual(f.read(), DUMMY_PAYLOAD)


class CoapDownloadAbortsOnETagChange(CoapDownload.Test):
    def runTest(self):
        self.file_server.set_resource('/', DUMMY_PAYLOAD)

        self.communicate('download %s %s' % (self.file_server.get_resource_uri('/'),
                                             self.tempfile.name))
        self.assertEqual(2, self.count_client_sockets())

        while len(self.file_server.requests) == 0:
            time.sleep(0.001)

        old_etag = self.file_server._resources['/'].etag
        new_etag = bytes([(old_etag[0] + 1) % 256]) + old_etag[1:]
        self.assertNotEqual(old_etag, new_etag)
        self.file_server.set_resource('/', DUMMY_PAYLOAD, etag=new_etag)

        # make sure download was aborted
        self.wait_until_downloads_finished()
        with open(self.tempfile.name, 'rb') as f:
            self.assertNotEqual(f.read(), DUMMY_PAYLOAD)


class CoapDownloadAbortsOnUnexpectedResponse(CoapDownload.Test):
    def runTest(self):
        self.file_server.set_resource('/', DUMMY_PAYLOAD)
        self.file_server.should_ignore_request = lambda _: True

        self.communicate('download %s %s' % (self.file_server.get_resource_uri('/'),
                                             self.tempfile.name))
        self.assertEqual(2, self.count_client_sockets())

        while len(self.file_server.requests) == 0:
            time.sleep(0.001)

        self.file_server._server.send(Lwm2mCreated.matching(self.file_server.requests[-1])().fill_placeholders())

        # make sure download was aborted
        self.wait_until_downloads_finished()
        with open(self.tempfile.name, 'rb') as f:
            self.assertNotEqual(f.read(), DUMMY_PAYLOAD)
