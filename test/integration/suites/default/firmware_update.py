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

import http
import http.server
import os
import ssl
import threading
import time

from framework.lwm2m_test import *
from framework.coap_file_server import CoapFileServerThread

UPDATE_STATE_IDLE = 0
UPDATE_STATE_DOWNLOADING = 1
UPDATE_STATE_DOWNLOADED = 2
UPDATE_STATE_UPDATING = 3

UPDATE_RESULT_INITIAL = 0
UPDATE_RESULT_SUCCESS = 1
UPDATE_RESULT_NOT_ENOUGH_SPACE = 2
UPDATE_RESULT_OUT_OF_MEMORY = 3
UPDATE_RESULT_CONNECTION_LOST = 4
UPDATE_RESULT_INTEGRITY_FAILURE = 5
UPDATE_RESULT_UNSUPPORTED_PACKAGE_TYPE = 6
UPDATE_RESULT_INVALID_URI = 7
UPDATE_RESULT_FAILED = 8
UPDATE_RESULT_UNSUPPORTED_PROTOCOL = 9

FIRMWARE_PATH = '/firmware'
FIRMWARE_SCRIPT_TEMPLATE = '#!/bin/sh\necho updated > "%s"\nrm "$0"\n'


class FirmwareUpdate:
    class Test(test_suite.Lwm2mSingleServerTest):
        def set_auto_deregister(self, auto_deregister):
            self.auto_deregister = auto_deregister

        def set_check_marker(self, check_marker):
            self.check_marker = check_marker

        def setUp(self):
            self.ANJAY_MARKER_FILE = generate_temp_filename(dir='/tmp', prefix='anjay-fw-updated-')
            self.FIRMWARE_SCRIPT_CONTENT = (FIRMWARE_SCRIPT_TEMPLATE % self.ANJAY_MARKER_FILE).encode('ascii')
            super().setUp(extra_cmdline_args=['--fw-updated-marker-path', self.ANJAY_MARKER_FILE])

        def tearDown(self):
            check_marker = getattr(self, 'check_marker', False)
            try:
                if check_marker:
                    for _ in range(10):
                        time.sleep(0.5)

                        if os.path.isfile(self.ANJAY_MARKER_FILE):
                            break
                    else:
                        self.fail('firmware marker not created')
            finally:
                try:
                    # no deregistration here, demo already terminated
                    super().tearDown(auto_deregister=getattr(self, 'auto_deregister', True))
                finally:
                    if check_marker:
                        with open(self.ANJAY_MARKER_FILE, "rb") as f:
                            line = f.readline()[:-1]
                            self.assertEqual(line, b"updated")

        def read_update_result(self, timeout_s=1):
            req = Lwm2mRead('/5/0/5')
            self.serv.send(req)
            res = self.serv.recv(timeout_s=timeout_s)
            self.assertMsgEqual(Lwm2mContent.matching(req)(), res)
            return int(res.content)

        def read_state(self, timeout_s=1):
            req = Lwm2mRead('/5/0/3')
            self.serv.send(req)
            res = self.serv.recv(timeout_s=timeout_s)
            self.assertMsgEqual(Lwm2mContent.matching(req)(), res)
            return int(res.content)

        def write_firmware_and_wait_for_download(self, firmware_uri: str,
                                                 write_timeout_s=1, read_timeout_s=1, download_timeout_s=10):
            # Write /5/0/1 (Firmware URI)
            req = Lwm2mWrite('/5/0/1', firmware_uri)
            self.serv.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                                self.serv.recv(timeout_s=write_timeout_s))

            # wait until client downloads the firmware
            deadline = time.time() + download_timeout_s
            while time.time() < deadline:
                time.sleep(0.5)

                if self.read_state(timeout_s=read_timeout_s) == UPDATE_STATE_DOWNLOADED:
                    return

            self.fail('firmware still not downloaded')

    class TestWithHttpServer(Test):
        def get_firmware_uri(self):
            return 'http://127.0.0.1:%d%s' % (self.http_server.server_address[1], FIRMWARE_PATH)

        def provide_response(self):
            with self._response_cv:
                self.assertIsNone(self._response_content)
                self._response_content = make_firmware_package(self.FIRMWARE_SCRIPT_CONTENT)
                self._response_cv.notify()

        def _create_server(self):
            test_case = self

            class FirmwareRequestHandler(http.server.BaseHTTPRequestHandler):
                def do_GET(self):
                    test_case.requests.append(self.path)

                    self.send_response(http.HTTPStatus.OK)
                    self.send_header('Content-type', 'text/plain')
                    self.end_headers()

                    # This condition variable makes it possible to defer sending the response.
                    # FirmwareUpdateStateChangeTest uses it to ensure demo has enough time
                    # to send the interim "Downloading" state notification.
                    with test_case._response_cv:
                        while test_case._response_content is None:
                            test_case._response_cv.wait()
                        response_content = test_case._response_content
                        test_case._response_content = None

                    self.wfile.write(response_content)

                def log_request(code='-', size='-'):
                    # don't display logs on successful request
                    pass

            return http.server.HTTPServer(('', 0), FirmwareRequestHandler)

        def setUp(self):
            super().setUp()

            self.requests = []
            self._response_content = None
            self._response_cv = threading.Condition()

            self.http_server = self._create_server()

            self.server_thread = threading.Thread(target=lambda: self.http_server.serve_forever())
            self.server_thread.start()

        def tearDown(self):
            try:
                super().tearDown()
            finally:
                self.http_server.shutdown()
                self.server_thread.join()

            # there should be exactly one request
            self.assertEqual(['/firmware'], self.requests)

    class TestWithHttpsServer(TestWithHttpServer):
        def get_firmware_uri(self):
            http_uri = super().get_firmware_uri()
            assert http_uri[:5] == 'http:'
            return 'https:' + http_uri[5:]

        def _create_server(self):
            import datetime
            from cryptography import x509
            from cryptography.x509.oid import NameOID
            from cryptography.hazmat.backends import default_backend
            from cryptography.hazmat.primitives import hashes, serialization
            from cryptography.hazmat.primitives.asymmetric import rsa

            key = rsa.generate_private_key(public_exponent=65537, key_size=1024, backend=default_backend())
            name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'localhost')])
            now = datetime.datetime.utcnow()
            cert = (x509.CertificateBuilder().
                    subject_name(name).
                    issuer_name(name).
                    public_key(key.public_key()).
                    serial_number(1000).
                    not_valid_before(now).
                    not_valid_after(now + datetime.timedelta(days=1)).
                    sign(key, hashes.SHA256(), default_backend()))
            cert_pem = cert.public_bytes(encoding=serialization.Encoding.PEM)
            key_pem = key.private_bytes(encoding=serialization.Encoding.PEM,
                                        format=serialization.PrivateFormat.TraditionalOpenSSL,
                                        encryption_algorithm=serialization.NoEncryption())

            with tempfile.NamedTemporaryFile() as cert_file, tempfile.NamedTemporaryFile() as key_file:
                cert_file.write(cert_pem)
                cert_file.flush()

                key_file.write(key_pem)
                key_file.flush()

                http_server = super()._create_server()
                http_server.socket = ssl.wrap_socket(http_server.socket,
                                                     certfile=cert_file.name,
                                                     keyfile=key_file.name,
                                                     server_side=True)
                return http_server

    class TestWithCoapServer(Test):
        def setUp(self):
            super().setUp()

            self.server_thread = CoapFileServerThread()
            self.server_thread.start()
            self.file_server = self.server_thread.file_server

        def tearDown(self):
            try:
                super().tearDown()
            finally:
                self.server_thread.join()


class FirmwareUpdatePackageTest(FirmwareUpdate.Test):
    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(False)

    def runTest(self):
        # Write /5/0/0 (Firmware): script content
        req = Lwm2mWrite('/5/0/0',
                         make_firmware_package(self.FIRMWARE_SCRIPT_CONTENT),
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv(timeout_s=1))

        # Execute /5/0/2 (Update)
        req = Lwm2mExecute('/5/0/2')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv(timeout_s=1))


class FirmwareUpdateUriTest(FirmwareUpdate.TestWithHttpServer):
    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(False)

    def runTest(self):
        self.provide_response()
        self.write_firmware_and_wait_for_download(self.get_firmware_uri())

        # Execute /5/0/2 (Update)
        req = Lwm2mExecute('/5/0/2')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv(timeout_s=1))


class FirmwareUpdateStateChangeTest(FirmwareUpdate.TestWithHttpServer):
    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(False)

    def runTest(self):
        self.serv.set_timeout(timeout_s=1)

        # disable minimum notification period
        write_attrs_req = Lwm2mWriteAttributes('/5/0/1', query=['pmin=0'])
        self.serv.send(write_attrs_req)
        self.assertMsgEqual(Lwm2mChanged.matching(write_attrs_req)(), self.serv.recv())

        # initial state should be 0
        observe_req = Lwm2mObserve('/5/0/3')
        self.serv.send(observe_req)
        self.assertMsgEqual(Lwm2mContent.matching(observe_req)(content=b'0'), self.serv.recv())

        # Write /5/0/1 (Firmware URI)
        req = Lwm2mWrite('/5/0/1', self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv(timeout_s=1))

        # notification should be sent before downloading
        self.assertMsgEqual(Lwm2mNotify(observe_req.token, b'1'),
                            self.serv.recv(timeout_s=3))

        self.provide_response()

        # ... and after it finishes
        self.assertMsgEqual(Lwm2mNotify(observe_req.token, b'2'),
                            self.serv.recv(timeout_s=3))

        # Execute /5/0/2 (Update)
        req = Lwm2mExecute('/5/0/2')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv(timeout_s=1))

        # ... and when update starts
        # TODO: this should be sent by an actual client.
        # demo does not implement a proper State/Update Result transitions.
        # Although State is always set as it should be, no notification is sent
        # before performing the firmware update (notifications are sent in a
        # scheduled job and the client code is not able to find out if state
        # change notify was sent or not).
        # Plus, there's no persistence that would allow setting Update Result
        # to 1 after successful update.
        # That's only demo client that is not really expected to update
        # properly, so it will stay as it is for now...
        # self.assertMsgEqual(Lwm2mNotify(observe_req.token, b'3'),
        #                     self.serv.recv())


class FirmwareUpdateBadBase64(FirmwareUpdate.Test):
    def runTest(self):
        # Write /5/0/0 (Firmware): some random text to see how it makes the world burn
        # (as text context does not implement some_bytes handler).
        data = bytes(b'\x01' * 16)
        req = Lwm2mWrite('/5/0/0', data, format=coap.ContentFormat.TEXT_PLAIN)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_INTERNAL_SERVER_ERROR),
                            self.serv.recv(timeout_s=1))


class FirmwareUpdateGoodBase64(FirmwareUpdate.Test):
    def runTest(self):
        import base64
        data = base64.encodebytes(bytes(b'\x01' * 16)).replace(b'\n', b'')
        req = Lwm2mWrite('/5/0/0', data, format=coap.ContentFormat.TEXT_PLAIN)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv(timeout_s=1))


class FirmwareUpdateEmptyPkg(FirmwareUpdate.TestWithHttpServer):
    def runTest(self):
        self.provide_response()
        self.write_firmware_and_wait_for_download(self.get_firmware_uri())

        req = Lwm2mWrite('/5/0/0', b'', format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv(timeout_s=1))

        self.assertEqual(UPDATE_STATE_IDLE, self.read_state())
        self.assertEqual(UPDATE_RESULT_INITIAL, self.read_update_result())


class FirmwareUpdateEmptyPkgUri(FirmwareUpdate.TestWithHttpServer):
    def runTest(self):
        self.provide_response()
        self.write_firmware_and_wait_for_download(self.get_firmware_uri())

        req = Lwm2mWrite('/5/0/1', '')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv(timeout_s=1))

        self.assertEqual(UPDATE_STATE_IDLE, self.read_state())
        self.assertEqual(UPDATE_RESULT_INITIAL, self.read_update_result())


class FirmwareUpdateInvalidUri(FirmwareUpdate.Test):
    def runTest(self):
        # observe Result
        observe_req = Lwm2mObserve('/5/0/5')
        self.serv.send(observe_req)
        self.assertMsgEqual(Lwm2mContent.matching(observe_req)(content=b'0'), self.serv.recv())

        req = Lwm2mWrite('/5/0/1', b'http://invalidfirmware.exe')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv(timeout_s=1))

        while True:
            notify = self.serv.recv(timeout_s=5)
            self.assertMsgEqual(Lwm2mNotify(observe_req.token), notify)
            if int(notify.content) != UPDATE_RESULT_INITIAL:
                break
        self.assertEqual(UPDATE_RESULT_INVALID_URI, int(notify.content))
        self.assertEqual(UPDATE_STATE_IDLE, self.read_state())


class FirmwareUpdateUnsupportedUri(FirmwareUpdate.Test):
    def runTest(self):
        req = Lwm2mWrite('/5/0/1', b'unsupported://uri.exe')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_BAD_REQUEST),
                            self.serv.recv())
        # This does not even change state or anything, because according to the LwM2M spec
        # Server can't feed us with unsupported URI type
        self.assertEqual(UPDATE_STATE_IDLE, self.read_state())
        self.assertEqual(UPDATE_RESULT_UNSUPPORTED_PROTOCOL, self.read_update_result())


class FirmwareUpdateReplacingPkgUri(FirmwareUpdate.TestWithHttpServer):
    def runTest(self):
        self.provide_response()
        self.write_firmware_and_wait_for_download(self.get_firmware_uri())

        # This isn't specified anywhere as a possible transition, therefore
        # it is most likely a bad request.
        req = Lwm2mWrite('/5/0/1', 'http://something')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_BAD_REQUEST),
                            self.serv.recv(timeout_s=1))

        self.assertEqual(UPDATE_STATE_DOWNLOADED, self.read_state())
        self.assertEqual(UPDATE_RESULT_INITIAL, self.read_update_result())


class FirmwareUpdateReplacingPkg(FirmwareUpdate.TestWithHttpServer):
    def runTest(self):
        self.provide_response()
        self.write_firmware_and_wait_for_download(self.get_firmware_uri())

        # This isn't specified anywhere as a possible transition, therefore
        # it is most likely a bad request.
        req = Lwm2mWrite('/5/0/0', b'trololo', format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_BAD_REQUEST),
                            self.serv.recv(timeout_s=1))

        self.assertEqual(UPDATE_STATE_DOWNLOADED, self.read_state())
        self.assertEqual(UPDATE_RESULT_INITIAL, self.read_update_result())


class FirmwareUpdateHttpsTest(FirmwareUpdate.TestWithHttpsServer):
    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(False)

    def runTest(self):
        self.provide_response()
        self.write_firmware_and_wait_for_download(self.get_firmware_uri(), read_timeout_s=10, download_timeout_s=20)

        # Execute /5/0/2 (Update)
        req = Lwm2mExecute('/5/0/2')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv(timeout_s=1))


class FirmwareUpdateResetInIdleState(FirmwareUpdate.Test):
    def runTest(self):
        self.assertEqual(UPDATE_STATE_IDLE, self.read_state())

        req = Lwm2mWrite('/5/0/1', b'')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())
        self.assertEqual(UPDATE_STATE_IDLE, self.read_state())
        self.assertEqual(UPDATE_RESULT_INITIAL, self.read_update_result())

        req = Lwm2mWrite('/5/0/0', b'')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())
        self.assertEqual(UPDATE_STATE_IDLE, self.read_state())
        self.assertEqual(UPDATE_RESULT_INITIAL, self.read_update_result())


class FirmwareUpdateCoapUri(FirmwareUpdate.TestWithCoapServer):
    def tearDown(self):
        super().tearDown()

        # there should be exactly one request
        self.assertEqual(1, len(self.file_server.requests))
        self.assertMsgEqual(CoapGet('/firmware',
                                    options=[coap.Option.BLOCK2(seq_num=0,
                                                                has_more=False,
                                                                block_size=1024)]),
                            self.file_server.requests[0])

    def runTest(self):
        self.file_server.set_resource('/firmware', make_firmware_package(self.FIRMWARE_SCRIPT_CONTENT))
        self.write_firmware_and_wait_for_download(self.file_server.get_resource_uri('/firmware'))
