# -*- coding: utf-8 -*-
#
# Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
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
import re
import ssl
import threading
import time
import zlib

from framework.coap_file_server import CoapFileServerThread
from framework.lwm2m_test import *

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
FIRMWARE_SCRIPT_TEMPLATE = '#!/bin/sh\n%secho updated > "%s"\nrm "$0"\n'


class FirmwareUpdate:
    class Test(test_suite.Lwm2mSingleServerTest):
        def set_auto_deregister(self, auto_deregister):
            self.auto_deregister = auto_deregister

        def set_check_marker(self, check_marker):
            self.check_marker = check_marker

        def setUp(self, garbage=0, *args, **kwargs):
            garbage_lines = ''
            while garbage > 0:
                garbage_line = '#' * (min(garbage, 80) - 1) + '\n'
                garbage_lines += garbage_line
                garbage -= len(garbage_line)
            self.ANJAY_MARKER_FILE = generate_temp_filename(dir='/tmp', prefix='anjay-fw-updated-')
            self.FIRMWARE_SCRIPT_CONTENT = \
                (FIRMWARE_SCRIPT_TEMPLATE % (garbage_lines, self.ANJAY_MARKER_FILE)).encode('ascii')
            super().setUp(fw_updated_marker_path=self.ANJAY_MARKER_FILE, *args, **kwargs)

        def tearDown(self):
            auto_deregister = getattr(self, 'auto_deregister', True)
            check_marker = getattr(self, 'check_marker', False)
            try:
                if check_marker:
                    for _ in range(10):
                        time.sleep(0.5)

                        if os.path.isfile(self.ANJAY_MARKER_FILE):
                            break
                    else:
                        self.fail('firmware marker not created')
                    with open(self.ANJAY_MARKER_FILE, "rb") as f:
                        line = f.readline()[:-1]
                        self.assertEqual(line, b"updated")
                    os.unlink(self.ANJAY_MARKER_FILE)
            finally:
                if auto_deregister:
                    # reset the state machine
                    # Write /5/0/1 (Firmware URI)
                    req = Lwm2mWrite('/5/0/1', '')
                    self.serv.send(req)
                    self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())
                super().tearDown(auto_deregister=auto_deregister)

        def read_update_result(self):
            req = Lwm2mRead('/5/0/5')
            self.serv.send(req)
            res = self.serv.recv()
            self.assertMsgEqual(Lwm2mContent.matching(req)(), res)
            return int(res.content)

        def read_state(self):
            req = Lwm2mRead('/5/0/3')
            self.serv.send(req)
            res = self.serv.recv()
            self.assertMsgEqual(Lwm2mContent.matching(req)(), res)
            return int(res.content)

        def write_firmware_and_wait_for_download(self, firmware_uri: str,
                                                 download_timeout_s=20):
            # Write /5/0/1 (Firmware URI)
            req = Lwm2mWrite('/5/0/1', firmware_uri)
            self.serv.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                                self.serv.recv())

            # wait until client downloads the firmware
            deadline = time.time() + download_timeout_s
            while time.time() < deadline:
                time.sleep(0.5)

                if self.read_state() == UPDATE_STATE_DOWNLOADED:
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
                        test_case.requests.append(self.path)
                        test_case._response_content = None

                    self.wfile.write(response_content)

                def log_request(code='-', size='-'):
                    # don't display logs on successful request
                    pass

            return http.server.HTTPServer(('', 0), FirmwareRequestHandler)

        def write_firmware_and_wait_for_download(self, *args, **kwargs):
            requests = list(self.requests)
            super().write_firmware_and_wait_for_download(*args, **kwargs)
            self.assertEqual(requests + ['/firmware'], self.requests)

        def setUp(self, *args, **kwargs):
            self.requests = []
            self._response_content = None
            self._response_cv = threading.Condition()

            self.http_server = self._create_server()

            super().setUp(*args, **kwargs)

            self.server_thread = threading.Thread(target=lambda: self.http_server.serve_forever())
            self.server_thread.start()

        def tearDown(self):
            try:
                super().tearDown()
            finally:
                self.http_server.shutdown()
                self.server_thread.join()

    class TestWithHttpsServer(TestWithHttpServer):
        def get_firmware_uri(self):
            http_uri = super().get_firmware_uri()
            assert http_uri[:5] == 'http:'
            return 'https:' + http_uri[5:]

        def setUp(self, pass_cert_to_demo=True, cn=None, *args, **kwargs):
            cert_pem, key_pem = self._generate_pem_cert_and_key(cn=cn)
            with tempfile.NamedTemporaryFile(delete=False) as cert_file, \
                    tempfile.NamedTemporaryFile(delete=False) as key_file:
                cert_file.write(cert_pem)
                cert_file.flush()

                key_file.write(key_pem)
                key_file.flush()

                self._cert_file = cert_file.name
                self._key_file = key_file.name

            extra_cmdline_args = []
            if 'extra_cmdline_args' in kwargs:
                extra_cmdline_args += kwargs['extra_cmdline_args']
                del kwargs['extra_cmdline_args']
            if pass_cert_to_demo:
                extra_cmdline_args += ['--fw-cert-file', self._cert_file]
            super().setUp(extra_cmdline_args=extra_cmdline_args, *args, **kwargs)

        def tearDown(self):
            def unlink_without_err(fname):
                try:
                    os.unlink(fname)
                except:
                    print('unlink(%r) failed' % (fname,))
                    sys.excepthook(*sys.exc_info())

            try:
                super().tearDown()
            finally:
                unlink_without_err(self._cert_file)
                unlink_without_err(self._key_file)

        @staticmethod
        def _generate_pem_cert_and_key(cn=None):
            import datetime
            from cryptography import x509
            from cryptography.x509.oid import NameOID
            from cryptography.hazmat.backends import default_backend
            from cryptography.hazmat.primitives import hashes, serialization
            from cryptography.hazmat.primitives.asymmetric import rsa

            key = rsa.generate_private_key(public_exponent=65537, key_size=2048, backend=default_backend())
            name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, cn or '127.0.0.1')])
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
            return cert_pem, key_pem

        def _create_server(self):
            http_server = super()._create_server()
            http_server.socket = ssl.wrap_socket(http_server.socket, certfile=self._cert_file, keyfile=self._key_file,
                                                 server_side=True)
            return http_server

    class TestWithCoapServer(Test):
        def setUp(self, coap_server=None, *args, **kwargs):
            super().setUp(*args, **kwargs)

            self.server_thread = CoapFileServerThread(coap_server=coap_server)
            self.server_thread.start()

        @property
        def file_server(self):
            return self.server_thread.file_server

        def tearDown(self):
            try:
                super().tearDown()
            finally:
                self.server_thread.join()

    @staticmethod
    def TestWithPartialDownloadAndRestart(base):
        class TestWithPartialDownloadAndRestartCommon(base):
            GARBAGE_SIZE = 8000

            def _get_valgrind_args(self):
                # these tests call demo_process.kill(), so Valgrind is not really useful
                return []

            def _start_demo(self, cmdline_args, timeout_s=30):
                self.cmdline_args = cmdline_args
                return super()._start_demo(cmdline_args, timeout_s)

            def wait_for_half_download(self):
                deadline = time.time() + self.GARBAGE_SIZE / 1000  # roughly twice the time expected as per SlowServer
                fsize = 0
                while time.time() < deadline:
                    time.sleep(0.5)
                    fsize = os.stat(self.fw_file_name).st_size
                    if fsize * 2 > self.GARBAGE_SIZE:
                        break
                if fsize * 2 <= self.GARBAGE_SIZE:
                    self.fail('firmware image not downloaded fast enough')
                elif fsize > self.GARBAGE_SIZE:
                    self.fail('firmware image downloaded too quickly')

            def setUp(self, *args, **kwargs):
                super().setUp(garbage=self.GARBAGE_SIZE, *args, **kwargs)

                import tempfile

                with tempfile.NamedTemporaryFile(delete=False) as f:
                    self.fw_file_name = f.name
                self.communicate('set-fw-package-path %s' % (os.path.abspath(self.fw_file_name)))

            def tearDown(self):
                with open(self.fw_file_name, "rb") as f:
                    self.assertEqual(f.read(), self.FIRMWARE_SCRIPT_CONTENT)
                super().tearDown()

        return TestWithPartialDownloadAndRestartCommon

    class TestWithPartialCoapDownloadAndRestart(TestWithPartialDownloadAndRestart.__func__(TestWithCoapServer)):
        def setUp(self):
            class SlowServer(coap.Server):
                def send(self, *args, **kwargs):
                    time.sleep(0.5)
                    result = super().send(*args, **kwargs)
                    self.reset()  # allow requests from other ports
                    return result

            super().setUp(coap_server=SlowServer())

            with self.file_server as file_server:
                file_server.set_resource('/firmware', make_firmware_package(self.FIRMWARE_SCRIPT_CONTENT))
                self.fw_uri = file_server.get_resource_uri('/firmware')

    class TestWithPartialHttpDownloadAndRestart(TestWithPartialDownloadAndRestart.__func__(TestWithHttpServer)):
        def get_etag(self, response_content):
            return '"%d"' % zlib.crc32(response_content)

        def check_success(self, handler, response_content, response_etag):
            pass

        def send_headers(self, handler, response_content, response_etag):
            pass

        def _create_server(self):
            test_case = self

            class FirmwareRequestHandler(http.server.BaseHTTPRequestHandler):
                def send_response(self, *args, **kwargs):
                    self._response_sent = True
                    return super().send_response(*args, **kwargs)

                def do_GET(self):
                    self._response_sent = False
                    test_case.requests.append(self.path)

                    # This condition variable makes it possible to defer sending the response.
                    # FirmwareUpdateStateChangeTest uses it to ensure demo has enough time
                    # to send the interim "Downloading" state notification.
                    with test_case._response_cv:
                        while test_case._response_content is None:
                            test_case._response_cv.wait()
                        response_content = test_case._response_content
                        response_etag = test_case.get_etag(response_content)

                        test_case.check_success(self, response_content, response_etag)
                        if self._response_sent:
                            return

                        test_case._response_content = None

                    self.send_response(http.HTTPStatus.OK)
                    self.send_header('Content-type', 'text/plain')
                    if response_etag is not None:
                        self.send_header('ETag', response_etag)
                    offset = test_case.send_headers(self, response_content, response_etag)
                    if offset is None:
                        offset = 0

                    self.end_headers()

                    while offset < len(response_content):
                        chunk = response_content[offset:offset + 1024]
                        self.wfile.write(chunk)
                        offset += len(chunk)
                        time.sleep(0.5)

                def log_message(self, *args, **kwargs):
                    # don't display logs
                    pass

            class SilentServer(http.server.HTTPServer):
                def handle_error(self, *args, **kwargs):
                    # don't log BrokenPipeErrors
                    if not isinstance(sys.exc_info()[1], BrokenPipeError):
                        super().handle_error(*args, **kwargs)

            return SilentServer(('', 0), FirmwareRequestHandler)


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
                            self.serv.recv())

        # Execute /5/0/2 (Update)
        req = Lwm2mExecute('/5/0/2')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())


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
                            self.serv.recv())


class FirmwareUpdateStateChangeTest(FirmwareUpdate.TestWithHttpServer):
    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(False)

    def runTest(self):
        self.serv.set_timeout(timeout_s=1)

        # disable minimum notification period
        write_attrs_req = Lwm2mWriteAttributes('/5/0/3', query=['pmin=0'])
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
                            self.serv.recv())

        # notification should be sent before downloading
        self.assertMsgEqual(Lwm2mNotify(observe_req.token, b'1'),
                            self.serv.recv())

        self.provide_response()

        # ... and after it finishes
        self.assertMsgEqual(Lwm2mNotify(observe_req.token, b'2'),
                            self.serv.recv())

        # Execute /5/0/2 (Update)
        req = Lwm2mExecute('/5/0/2')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # ... and when update starts
        self.assertMsgEqual(Lwm2mNotify(observe_req.token, b'3'),
                            self.serv.recv())

        # there should be exactly one request
        self.assertEqual(['/firmware'], self.requests)


class FirmwareUpdateBadBase64(FirmwareUpdate.Test):
    def runTest(self):
        # Write /5/0/0 (Firmware): some random text to see how it makes the world burn
        # (as text context does not implement some_bytes handler).
        data = bytes(b'\x01' * 16)
        req = Lwm2mWrite('/5/0/0', data, format=coap.ContentFormat.TEXT_PLAIN)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_INTERNAL_SERVER_ERROR),
                            self.serv.recv())


class FirmwareUpdateGoodBase64(FirmwareUpdate.Test):
    def runTest(self):
        import base64
        data = base64.encodebytes(bytes(b'\x01' * 16)).replace(b'\n', b'')
        req = Lwm2mWrite('/5/0/0', data, format=coap.ContentFormat.TEXT_PLAIN)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())


class FirmwareUpdateNullPkg(FirmwareUpdate.TestWithHttpServer):
    def runTest(self):
        self.provide_response()
        self.write_firmware_and_wait_for_download(self.get_firmware_uri())

        req = Lwm2mWrite('/5/0/0', b'\0', format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.assertEqual(UPDATE_STATE_IDLE, self.read_state())
        self.assertEqual(UPDATE_RESULT_INITIAL, self.read_update_result())


class FirmwareUpdateEmptyPkgUri(FirmwareUpdate.TestWithHttpServer):
    def runTest(self):
        self.provide_response()
        self.write_firmware_and_wait_for_download(self.get_firmware_uri())

        req = Lwm2mWrite('/5/0/1', '')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

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
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        while True:
            notify = self.serv.recv()
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
                            self.serv.recv())

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
                            self.serv.recv())

        self.assertEqual(UPDATE_STATE_DOWNLOADED, self.read_state())
        self.assertEqual(UPDATE_RESULT_INITIAL, self.read_update_result())


class FirmwareUpdateHttpsTest(FirmwareUpdate.TestWithHttpsServer):
    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(False)

    def runTest(self):
        self.provide_response()
        self.write_firmware_and_wait_for_download(self.get_firmware_uri(), download_timeout_s=20)

        # Execute /5/0/2 (Update)
        req = Lwm2mExecute('/5/0/2')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())


class FirmwareUpdateUnconfiguredHttpsTest(FirmwareUpdate.TestWithHttpsServer):
    def setUp(self):
        super().setUp(pass_cert_to_demo=False)

    def runTest(self):
        # disable minimum notification period
        write_attrs_req = Lwm2mWriteAttributes('/5/0/5', query=['pmin=0'])
        self.serv.send(write_attrs_req)
        self.assertMsgEqual(Lwm2mChanged.matching(write_attrs_req)(), self.serv.recv())

        # initial result should be 0
        observe_req = Lwm2mObserve('/5/0/5')
        self.serv.send(observe_req)
        self.assertMsgEqual(Lwm2mContent.matching(observe_req)(content=b'0'), self.serv.recv())

        # Write /5/0/1 (Firmware URI)
        req = Lwm2mWrite('/5/0/1', self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # even before reaching the server, we should get an error
        notify_msg = self.serv.recv()
        # no security information => "Unsupported protocol"
        self.assertMsgEqual(Lwm2mNotify(observe_req.token, str(UPDATE_RESULT_UNSUPPORTED_PROTOCOL).encode()),
                            notify_msg)
        self.serv.send(Lwm2mReset(msg_id=notify_msg.msg_id))
        self.assertEqual(0, self.read_state())


class FirmwareUpdateUnconfiguredHttpsWithFallbackAttemptTest(FirmwareUpdate.TestWithHttpsServer):
    def setUp(self):
        super().setUp(pass_cert_to_demo=False, psk_identity=b'test-identity', psk_key=b'test-key')

    def runTest(self):
        # disable minimum notification period
        write_attrs_req = Lwm2mWriteAttributes('/5/0/5', query=['pmin=0'])
        self.serv.send(write_attrs_req)
        self.assertMsgEqual(Lwm2mChanged.matching(write_attrs_req)(), self.serv.recv())

        # initial result should be 0
        observe_req = Lwm2mObserve('/5/0/5')
        self.serv.send(observe_req)
        self.assertMsgEqual(Lwm2mContent.matching(observe_req)(content=b'0'), self.serv.recv())

        # Write /5/0/1 (Firmware URI)
        req = Lwm2mWrite('/5/0/1', self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # even before reaching the server, we should get an error
        notify_msg = self.serv.recv()
        # no security information => client will attempt PSK from data model and fail handshake => "Connection lost"
        self.assertMsgEqual(Lwm2mNotify(observe_req.token, str(UPDATE_RESULT_CONNECTION_LOST).encode()), notify_msg)
        self.serv.send(Lwm2mReset(msg_id=notify_msg.msg_id))
        self.assertEqual(0, self.read_state())


class FirmwareUpdateInvalidHttpsTest(FirmwareUpdate.TestWithHttpsServer):
    def setUp(self):
        super().setUp(cn='invalid_cn')

    def runTest(self):
        # disable minimum notification period
        write_attrs_req = Lwm2mWriteAttributes('/5/0/5', query=['pmin=0'])
        self.serv.send(write_attrs_req)
        self.assertMsgEqual(Lwm2mChanged.matching(write_attrs_req)(), self.serv.recv())

        # initial result should be 0
        observe_req = Lwm2mObserve('/5/0/5')
        self.serv.send(observe_req)
        self.assertMsgEqual(Lwm2mContent.matching(observe_req)(content=b'0'), self.serv.recv())

        # Write /5/0/1 (Firmware URI)
        req = Lwm2mWrite('/5/0/1', self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # even before reaching the server, we should get an error
        notify_msg = self.serv.recv()
        # handshake failure => "Connection lost"
        self.assertMsgEqual(Lwm2mNotify(observe_req.token, str(UPDATE_RESULT_CONNECTION_LOST).encode()), notify_msg)
        self.serv.send(Lwm2mReset(msg_id=notify_msg.msg_id))
        self.assertEqual(0, self.read_state())


class FirmwareUpdateResetInIdleState(FirmwareUpdate.Test):
    def runTest(self):
        self.assertEqual(UPDATE_STATE_IDLE, self.read_state())

        req = Lwm2mWrite('/5/0/1', b'')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())
        self.assertEqual(UPDATE_STATE_IDLE, self.read_state())
        self.assertEqual(UPDATE_RESULT_INITIAL, self.read_update_result())

        req = Lwm2mWrite('/5/0/0', b'\0',
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())
        self.assertEqual(UPDATE_STATE_IDLE, self.read_state())
        self.assertEqual(UPDATE_RESULT_INITIAL, self.read_update_result())


class FirmwareUpdateCoapUri(FirmwareUpdate.TestWithCoapServer):
    def tearDown(self):
        super().tearDown()

        # there should be exactly one request
        with self.file_server as file_server:
            self.assertEqual(1, len(file_server.requests))
            self.assertMsgEqual(CoapGet('/firmware',
                                        options=[coap.Option.BLOCK2(seq_num=0,
                                                                    has_more=False,
                                                                    block_size=1024)]),
                                file_server.requests[0])

    def runTest(self):
        with self.file_server as file_server:
            file_server.set_resource('/firmware', make_firmware_package(self.FIRMWARE_SCRIPT_CONTENT))
            fw_uri = file_server.get_resource_uri('/firmware')
        self.write_firmware_and_wait_for_download(fw_uri)


class FirmwareUpdateRestartWithDownloaded(FirmwareUpdate.Test):
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
                            self.serv.recv())

        # restart the app
        self.teardown_demo_with_servers()
        self.setup_demo_with_servers(fw_updated_marker_path=self.ANJAY_MARKER_FILE)

        self.assertEqual(UPDATE_STATE_DOWNLOADED, self.read_state())
        self.assertEqual(UPDATE_RESULT_INITIAL, self.read_update_result())

        # Execute /5/0/2 (Update)
        req = Lwm2mExecute('/5/0/2')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())


class FirmwareUpdateRestartWithDownloading(FirmwareUpdate.TestWithPartialCoapDownloadAndRestart):
    def runTest(self):
        # Write /5/0/1 (Firmware URI)
        req = Lwm2mWrite('/5/0/1', self.fw_uri)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_for_half_download()

        self.demo_process.kill()

        # restart demo app
        self.serv.reset()
        with self.file_server as file_server:
            file_server._server.reset()
        self._start_demo(self.cmdline_args)
        self.assertDemoRegisters(self.serv)

        # wait until client downloads the firmware
        deadline = time.time() + 20
        state = None
        while time.time() < deadline:
            fsize = os.stat(self.fw_file_name).st_size
            self.assertGreater(fsize * 2, self.GARBAGE_SIZE)
            state = self.read_state()
            self.assertIn(state, {UPDATE_STATE_DOWNLOADING, UPDATE_STATE_DOWNLOADED})
            if state == UPDATE_STATE_DOWNLOADED:
                break
        self.assertEqual(state, UPDATE_STATE_DOWNLOADED)


class FirmwareUpdateRestartWithDownloadingETagChange(FirmwareUpdate.TestWithPartialCoapDownloadAndRestart):
    def runTest(self):
        # Write /5/0/1 (Firmware URI)
        req = Lwm2mWrite('/5/0/1', self.fw_uri)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_for_half_download()

        self.demo_process.kill()

        # restart demo app
        self.serv.reset()
        with self.file_server as file_server:
            old_etag = file_server._resources['/firmware'].etag
            new_etag = bytes([(old_etag[0] + 1) % 256]) + old_etag[1:]
            self.assertNotEqual(old_etag, new_etag)
            file_server.set_resource('/firmware', make_firmware_package(self.FIRMWARE_SCRIPT_CONTENT), etag=new_etag)
        self._start_demo(self.cmdline_args)
        self.assertDemoRegisters(self.serv)

        # wait until client downloads the firmware
        deadline = time.time() + 20
        state = None
        file_truncated = False
        while time.time() < deadline:
            try:
                fsize = os.stat(self.fw_file_name).st_size
                if fsize * 2 <= self.GARBAGE_SIZE:
                    file_truncated = True
            except FileNotFoundError:
                file_truncated = True
            state = self.read_state()
            self.assertIn(state, {UPDATE_STATE_DOWNLOADING, UPDATE_STATE_DOWNLOADED})
            if state == UPDATE_STATE_DOWNLOADED:
                break
        self.assertEqual(state, UPDATE_STATE_DOWNLOADED)
        self.assertTrue(file_truncated)


class FirmwareUpdateRestartWithDownloadingOverHttp(FirmwareUpdate.TestWithPartialHttpDownloadAndRestart):
    def get_etag(self, response_content):
        return None

    def runTest(self):
        self.provide_response()
        # Write /5/0/1 (Firmware URI)
        req = Lwm2mWrite('/5/0/1', self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_for_half_download()

        self.assertEqual(2, self.get_socket_count())
        self.assertEqual(1, self.get_non_lwm2m_socket_count())
        self.assertEqual('TCP', self.get_transport(socket_index=-1))

        self.demo_process.kill()

        # restart demo app
        self.serv.reset()

        self.provide_response()
        self._start_demo(self.cmdline_args)
        self.assertDemoRegisters(self.serv)

        # wait until client downloads the firmware
        deadline = time.time() + 20
        state = None
        file_truncated = False
        while time.time() < deadline:
            try:
                fsize = os.stat(self.fw_file_name).st_size
                if fsize * 2 <= self.GARBAGE_SIZE:
                    file_truncated = True
            except FileNotFoundError:
                file_truncated = True
            state = self.read_state()
            self.assertIn(state, {UPDATE_STATE_DOWNLOADING, UPDATE_STATE_DOWNLOADED})
            if state == UPDATE_STATE_DOWNLOADED:
                break
        self.assertEqual(state, UPDATE_STATE_DOWNLOADED)
        self.assertTrue(file_truncated)

        self.assertEqual(len(self.requests), 2)


class FirmwareUpdateResumeDownloadingOverHttp(FirmwareUpdate.TestWithPartialHttpDownloadAndRestart):
    def send_headers(self, handler, response_content, response_etag):
        if 'Range' in handler.headers:
            self.assertEqual(handler.headers['If-Match'], response_etag)
            match = re.fullmatch(r'bytes=([0-9]+)-', handler.headers['Range'])
            self.assertIsNotNone(match)
            offset = int(match.group(1))
            handler.send_header('Content-range', 'bytes %d-%d/*' % (offset, len(response_content) - 1))
            return offset

    def runTest(self):
        self.provide_response()
        # Write /5/0/1 (Firmware URI)
        req = Lwm2mWrite('/5/0/1', self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_for_half_download()

        self.demo_process.kill()

        # restart demo app
        self.serv.reset()

        self.provide_response()
        self._start_demo(self.cmdline_args)
        self.assertDemoRegisters(self.serv)

        # wait until client downloads the firmware
        deadline = time.time() + 20
        state = None
        while time.time() < deadline:
            fsize = os.stat(self.fw_file_name).st_size
            self.assertGreater(fsize * 2, self.GARBAGE_SIZE)
            state = self.read_state()
            self.assertIn(state, {UPDATE_STATE_DOWNLOADING, UPDATE_STATE_DOWNLOADED})
            if state == UPDATE_STATE_DOWNLOADED:
                break
        self.assertEqual(state, UPDATE_STATE_DOWNLOADED)

        self.assertEqual(len(self.requests), 2)


class FirmwareUpdateResumeDownloadingOverHttpWithReconnect(FirmwareUpdate.TestWithPartialHttpDownloadAndRestart):
    def _get_valgrind_args(self):
        # we don't kill the process here, so we want Valgrind
        return FirmwareUpdate.TestWithHttpServer._get_valgrind_args(self)

    def send_headers(self, handler, response_content, response_etag):
        if 'Range' in handler.headers:
            self.assertEqual(handler.headers['If-Match'], response_etag)
            match = re.fullmatch(r'bytes=([0-9]+)-', handler.headers['Range'])
            self.assertIsNotNone(match)
            offset = int(match.group(1))
            handler.send_header('Content-range', 'bytes %d-%d/*' % (offset, len(response_content) - 1))
            return offset

    def runTest(self):
        self.provide_response()
        # Write /5/0/1 (Firmware URI)
        req = Lwm2mWrite('/5/0/1', self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_for_half_download()

        # reconnect
        self.serv.reset()
        self.communicate('reconnect')
        self.assertDemoRegisters(self.serv)
        self.provide_response()

        # wait until client downloads the firmware
        deadline = time.time() + 20
        state = None
        while time.time() < deadline:
            fsize = os.stat(self.fw_file_name).st_size
            self.assertGreater(fsize * 2, self.GARBAGE_SIZE)
            state = self.read_state()
            self.assertIn(state, {UPDATE_STATE_DOWNLOADING, UPDATE_STATE_DOWNLOADED})
            if state == UPDATE_STATE_DOWNLOADED:
                break
        self.assertEqual(state, UPDATE_STATE_DOWNLOADED)

        self.assertEqual(len(self.requests), 2)


class FirmwareUpdateResumeFromStartWithDownloadingOverHttp(FirmwareUpdate.TestWithPartialHttpDownloadAndRestart):
    def runTest(self):
        self.provide_response()
        # Write /5/0/1 (Firmware URI)
        req = Lwm2mWrite('/5/0/1', self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_for_half_download()

        self.demo_process.kill()

        # restart demo app
        self.serv.reset()

        self.provide_response()
        self._start_demo(self.cmdline_args)
        self.assertDemoRegisters(self.serv)

        # wait until client downloads the firmware
        deadline = time.time() + 20
        state = None
        while time.time() < deadline:
            fsize = os.stat(self.fw_file_name).st_size
            self.assertGreater(fsize * 2, self.GARBAGE_SIZE)
            state = self.read_state()
            self.assertIn(state, {UPDATE_STATE_DOWNLOADING, UPDATE_STATE_DOWNLOADED})
            if state == UPDATE_STATE_DOWNLOADED:
                break
        self.assertEqual(state, UPDATE_STATE_DOWNLOADED)

        self.assertEqual(len(self.requests), 2)


class FirmwareUpdateRestartAfter412WithDownloadingOverHttp(FirmwareUpdate.TestWithPartialHttpDownloadAndRestart):
    def check_success(self, handler, response_content, response_etag):
        if 'If-Match' in handler.headers:
            self.assertEqual(handler.headers['If-Match'], response_etag)
            handler.send_error(http.HTTPStatus.PRECONDITION_FAILED)

    def runTest(self):
        self.provide_response()
        # Write /5/0/1 (Firmware URI)
        req = Lwm2mWrite('/5/0/1', self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_for_half_download()

        self.demo_process.kill()

        # restart demo app
        self.serv.reset()

        self.provide_response()
        self._start_demo(self.cmdline_args)
        self.assertDemoRegisters(self.serv)

        # wait until client downloads the firmware
        deadline = time.time() + 20
        state = None
        file_truncated = False
        while time.time() < deadline:
            try:
                fsize = os.stat(self.fw_file_name).st_size
                if fsize * 2 <= self.GARBAGE_SIZE:
                    file_truncated = True
            except FileNotFoundError:
                file_truncated = True
            state = self.read_state()
            self.assertIn(state, {UPDATE_STATE_DOWNLOADING, UPDATE_STATE_DOWNLOADED})
            if state == UPDATE_STATE_DOWNLOADED:
                break
        self.assertEqual(state, UPDATE_STATE_DOWNLOADED)
        self.assertTrue(file_truncated)

        self.assertEqual(len(self.requests), 3)
