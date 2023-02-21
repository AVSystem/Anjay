# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

import asyncio
import http
import http.server
import os
import re
import ssl
import threading
import unittest
import zlib

from framework.coap_file_server import CoapFileServerThread, CoapFileServer
from framework.lwm2m_test import *
from .access_control import AccessMask
from .block_write import Block, equal_chunk_splitter


class UpdateState:
    IDLE = 0
    DOWNLOADING = 1
    DOWNLOADED = 2
    UPDATING = 3


class UpdateResult:
    INITIAL = 0
    SUCCESS = 1
    NOT_ENOUGH_SPACE = 2
    OUT_OF_MEMORY = 3
    CONNECTION_LOST = 4
    INTEGRITY_FAILURE = 5
    UNSUPPORTED_PACKAGE_TYPE = 6
    INVALID_URI = 7
    FAILED = 8
    UNSUPPORTED_PROTOCOL = 9
    CANCELLED = 10
    DEFERRED = 11


FIRMWARE_PATH = '/firmware'
FIRMWARE_SCRIPT_TEMPLATE = '#!/bin/sh\n%secho updated > "%s"\nrm "$0"\n'


class FirmwareUpdate:
    class Test(test_suite.Lwm2mSingleServerTest):
        def set_auto_deregister(self, auto_deregister):
            self.auto_deregister = auto_deregister

        def set_check_marker(self, check_marker):
            self.check_marker = check_marker

        def set_reset_machine(self, reset_machine):
            self.reset_machine = reset_machine

        def set_expect_send_after_state_machine_reset(
                self, expect_send_after_state_machine_reset):
            self.expect_send_after_state_machine_reset = expect_send_after_state_machine_reset

        def setUp(self, garbage=0, *args, **kwargs):
            garbage_lines = ''
            while garbage > 0:
                garbage_line = '#' * (min(garbage, 80) - 1) + '\n'
                garbage_lines += garbage_line
                garbage -= len(garbage_line)
            self.ANJAY_MARKER_FILE = generate_temp_filename(
                dir='/tmp', prefix='anjay-fw-updated-')
            self.FIRMWARE_SCRIPT_CONTENT = \
                (FIRMWARE_SCRIPT_TEMPLATE %
                 (garbage_lines, self.ANJAY_MARKER_FILE)).encode('ascii')
            super().setUp(fw_updated_marker_path=self.ANJAY_MARKER_FILE, *args, **kwargs)

        def tearDown(self):
            auto_deregister = getattr(self, 'auto_deregister', True)
            check_marker = getattr(self, 'check_marker', False)
            reset_machine = getattr(self, 'reset_machine', True)
            expect_send_after_state_machine_reset = getattr(self,
                                                            'expect_send_after_state_machine_reset',
                                                            False)
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
                if reset_machine:
                    # reset the state machine
                    # Write /5/0/1 (Firmware URI)
                    req = Lwm2mWrite(ResPath.FirmwareUpdate.PackageURI, '')
                    self.serv.send(req)
                    self.assertMsgEqual(
                        Lwm2mChanged.matching(req)(), self.serv.recv())
                    if expect_send_after_state_machine_reset:
                        pkt = self.serv.recv()
                        self.assertMsgEqual(Lwm2mSend(), pkt)
                        CBOR.parse(pkt.content).verify_values(test=self,
                                                              expected_value_map={
                                                                  ResPath.FirmwareUpdate.State: UpdateState.IDLE,
                                                                  ResPath.FirmwareUpdate.UpdateResult: UpdateResult.INITIAL
                                                              })
                        self.serv.send(Lwm2mChanged.matching(pkt)())
                super().tearDown(auto_deregister=auto_deregister)

        def read_update_result(self):
            req = Lwm2mRead(ResPath.FirmwareUpdate.UpdateResult)
            self.serv.send(req)
            res = self.serv.recv()
            self.assertMsgEqual(Lwm2mContent.matching(req)(), res)
            return int(res.content)

        def read_state(self):
            req = Lwm2mRead(ResPath.FirmwareUpdate.State)
            self.serv.send(req)
            res = self.serv.recv()
            self.assertMsgEqual(Lwm2mContent.matching(req)(), res)
            return int(res.content)

        def write_firmware_and_wait_for_download(self, firmware_uri: str,
                                                 download_timeout_s=20):
            # Write /5/0/1 (Firmware URI)
            req = Lwm2mWrite(ResPath.FirmwareUpdate.PackageURI, firmware_uri)
            self.serv.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                                self.serv.recv())

            # wait until client downloads the firmware
            deadline = time.time() + download_timeout_s
            while time.time() < deadline:
                time.sleep(0.5)

                if self.read_state() == UpdateState.DOWNLOADED:
                    return

            self.fail('firmware still not downloaded')

    class TestWithHttpServer(Test):
        RESPONSE_DELAY = 0
        CHUNK_SIZE = sys.maxsize
        ETAGS = False
        FW_PKG_OPTS = {}

        def get_firmware_uri(self):
            return 'http://127.0.0.1:%d%s' % (
                self.http_server.server_address[1], FIRMWARE_PATH)

        def provide_response(self, use_real_app=False):
            with self._response_cv:
                self.assertIsNone(self._response_content)
                if use_real_app:
                    with open(os.path.join(self.config.demo_path, self.config.demo_cmd), 'rb') as f:
                        firmware = f.read()
                        self._response_content = make_firmware_package(
                            firmware, **self.FW_PKG_OPTS)
                else:
                    self._response_content = make_firmware_package(
                        self.FIRMWARE_SCRIPT_CONTENT, **self.FW_PKG_OPTS)
                self._response_cv.notify()

        def _create_server(self):
            test_case = self

            class FirmwareRequestHandler(http.server.BaseHTTPRequestHandler):
                def do_GET(self):
                    self.send_response(http.HTTPStatus.OK)
                    self.send_header('Content-type', 'text/plain')
                    if test_case.ETAGS:
                        self.send_header('ETag', '"some_etag"')
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

                    def chunks(data):
                        for i in range(0, len(response_content),
                                       test_case.CHUNK_SIZE):
                            yield response_content[i:i + test_case.CHUNK_SIZE]

                    for chunk in chunks(response_content):
                        time.sleep(test_case.RESPONSE_DELAY)
                        self.wfile.write(chunk)
                        self.wfile.flush()

                def log_request(self, code='-', size='-'):
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

            self.server_thread = threading.Thread(
                target=lambda: self.http_server.serve_forever())
            self.server_thread.start()

        def tearDown(self):
            try:
                super().tearDown()
            finally:
                self.http_server.shutdown()
                self.server_thread.join()

    class TestWithTlsServer(Test):
        @staticmethod
        def _generate_key():
            from cryptography.hazmat.backends import default_backend
            from cryptography.hazmat.primitives.asymmetric import rsa
            return rsa.generate_private_key(public_exponent=65537, key_size=2048,
                                            backend=default_backend())

        @staticmethod
        def _generate_cert(private_key, public_key, issuer_cn, cn='127.0.0.1', alt_ip=None,
                           ca=False):
            import datetime
            import ipaddress
            from cryptography import x509
            from cryptography.x509.oid import NameOID
            from cryptography.hazmat.backends import default_backend
            from cryptography.hazmat.primitives import hashes

            name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, cn)])
            issuer_name = x509.Name(
                [x509.NameAttribute(NameOID.COMMON_NAME, issuer_cn)])
            now = datetime.datetime.utcnow()
            cert_builder = (x509.CertificateBuilder().
                            subject_name(name).
                            issuer_name(issuer_name).
                            public_key(public_key).
                            serial_number(1000).
                            not_valid_before(now).
                            not_valid_after(now + datetime.timedelta(days=1)))
            if alt_ip is not None:
                cert_builder = cert_builder.add_extension(x509.SubjectAlternativeName(
                    [x509.DNSName(cn), x509.IPAddress(ipaddress.IPv4Address(alt_ip))]),
                    critical=False)
            if ca:
                cert_builder = cert_builder.add_extension(
                    x509.BasicConstraints(ca=True, path_length=None), critical=False)
            return cert_builder.sign(
                private_key, hashes.SHA256(), default_backend())

        @staticmethod
        def _generate_cert_and_key(
                cn='127.0.0.1', alt_ip='127.0.0.1', ca=False):
            key = FirmwareUpdate.TestWithTlsServer._generate_key()
            cert = FirmwareUpdate.TestWithTlsServer._generate_cert(key, key.public_key(), cn, cn,
                                                                   alt_ip, ca)
            return cert, key

        @staticmethod
        def _generate_pem_cert_and_key(
                cn='127.0.0.1', alt_ip='127.0.0.1', ca=False):
            from cryptography.hazmat.primitives import serialization

            cert, key = FirmwareUpdate.TestWithTlsServer._generate_cert_and_key(
                cn, alt_ip, ca)
            cert_pem = cert.public_bytes(encoding=serialization.Encoding.PEM)
            key_pem = key.private_bytes(encoding=serialization.Encoding.PEM,
                                        format=serialization.PrivateFormat.TraditionalOpenSSL,
                                        encryption_algorithm=serialization.NoEncryption())
            return cert_pem, key_pem

        def setUp(self, pass_cert_to_demo=True, **kwargs):
            cert_kwargs = {}
            for key in ('cn', 'alt_ip'):
                if key in kwargs:
                    cert_kwargs[key] = kwargs[key]
                    del kwargs[key]
            cert_pem, key_pem = self._generate_pem_cert_and_key(**cert_kwargs)

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
            super().setUp(extra_cmdline_args=extra_cmdline_args, **kwargs)

        def tearDown(self):
            def unlink_without_err(fname):
                try:
                    os.unlink(fname)
                except BaseException:
                    print('unlink(%r) failed' % (fname,))
                    sys.excepthook(*sys.exc_info())

            try:
                super().tearDown()
            finally:
                unlink_without_err(self._cert_file)
                unlink_without_err(self._key_file)

    class TestWithHttpsServer(TestWithTlsServer, TestWithHttpServer):
        def get_firmware_uri(self):
            http_uri = super().get_firmware_uri()
            assert http_uri[:5] == 'http:'
            return 'https:' + http_uri[5:]

        def _create_server(self):
            http_server = super()._create_server()
            http_server.socket = ssl.wrap_socket(http_server.socket, certfile=self._cert_file,
                                                 keyfile=self._key_file,
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

    class DemoArgsExtractorMixin:
        def _get_valgrind_args(self):
            # these tests call demo_process.kill(), so Valgrind is not really
            # useful
            return []

        def _start_demo(self, cmdline_args, timeout_s=30):
            self.cmdline_args = cmdline_args
            return super()._start_demo(cmdline_args, timeout_s)

    class TestWithPartialDownload:
        GARBAGE_SIZE = 8000

        def wait_for_half_download(self):
            # roughly twice the time expected as per SlowServer
            deadline = time.time() + self.GARBAGE_SIZE / 500
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
            self.communicate('set-fw-package-path %s' %
                             (os.path.abspath(self.fw_file_name)))

    class TestWithPartialDownloadAndRestart(
        TestWithPartialDownload, DemoArgsExtractorMixin):
        def tearDown(self):
            with open(self.fw_file_name, "rb") as f:
                self.assertEqual(f.read(), self.FIRMWARE_SCRIPT_CONTENT)
            super().tearDown()

    class TestWithPartialCoapDownloadAndRestart(TestWithPartialDownloadAndRestart,
                                                TestWithCoapServer):
        def setUp(self):
            class SlowServer(coap.Server):
                def send(self, *args, **kwargs):
                    time.sleep(0.5)
                    result = super().send(*args, **kwargs)
                    self.reset()  # allow requests from other ports
                    return result

            super().setUp(coap_server=SlowServer())

            with self.file_server as file_server:
                file_server.set_resource('/firmware',
                                         make_firmware_package(self.FIRMWARE_SCRIPT_CONTENT))
                self.fw_uri = file_server.get_resource_uri('/firmware')

    class TestWithPartialHttpDownloadAndRestart(TestWithPartialDownloadAndRestart,
                                                TestWithHttpServer):
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

                        test_case.check_success(
                            self, response_content, response_etag)
                        if self._response_sent:
                            return

                        test_case._response_content = None

                    self.send_response(http.HTTPStatus.OK)
                    self.send_header('Content-type', 'text/plain')
                    if response_etag is not None:
                        self.send_header('ETag', response_etag)
                    offset = test_case.send_headers(
                        self, response_content, response_etag)
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
        self.set_reset_machine(False)

    def runTest(self):
        # Write /5/0/0 (Firmware): script content
        req = Lwm2mWrite(ResPath.FirmwareUpdate.Package,
                         make_firmware_package(self.FIRMWARE_SCRIPT_CONTENT),
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Execute /5/0/2 (Update)
        req = Lwm2mExecute(ResPath.FirmwareUpdate.Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())


class FirmwareUpdateUriTest(FirmwareUpdate.TestWithHttpServer):
    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(False)
        self.set_reset_machine(False)

    def runTest(self):
        self.provide_response()
        self.write_firmware_and_wait_for_download(self.get_firmware_uri())

        # Execute /5/0/2 (Update)
        req = Lwm2mExecute(ResPath.FirmwareUpdate.Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())


class FirmwareUpdateStateChangeTest(FirmwareUpdate.TestWithHttpServer):
    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(False)
        self.set_reset_machine(False)

    def runTest(self):
        self.serv.set_timeout(timeout_s=1)

        # disable minimum notification period
        write_attrs_req = Lwm2mWriteAttributes(
            ResPath.FirmwareUpdate.State, query=['pmin=0'])
        self.serv.send(write_attrs_req)
        self.assertMsgEqual(Lwm2mChanged.matching(
            write_attrs_req)(), self.serv.recv())

        # initial state should be 0
        observe_req = Lwm2mObserve(ResPath.FirmwareUpdate.State)
        self.serv.send(observe_req)
        self.assertMsgEqual(Lwm2mContent.matching(
            observe_req)(content=b'0'), self.serv.recv())

        # Write /5/0/1 (Firmware URI)
        req = Lwm2mWrite(ResPath.FirmwareUpdate.PackageURI,
                         self.get_firmware_uri())
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
        req = Lwm2mExecute(ResPath.FirmwareUpdate.Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # ... and when update starts
        self.assertMsgEqual(Lwm2mNotify(observe_req.token, b'3'),
                            self.serv.recv())

        # there should be exactly one request
        self.assertEqual(['/firmware'], self.requests)


class FirmwareUpdateSendStateChangeTest(FirmwareUpdate.TestWithHttpServer):
    def setUp(self):
        super().setUp(minimum_version='1.1', maximum_version='1.1',
                      extra_cmdline_args=['--fw-update-use-send'])
        self.set_expect_send_after_state_machine_reset(True)

    def runTest(self):
        self.serv.set_timeout(timeout_s=3)

        self.assertEqual(self.read_state(), UpdateState.IDLE)

        # Write /5/0/1 (Firmware URI)
        req = Lwm2mWrite(ResPath.FirmwareUpdate.PackageURI,
                         self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        pkt = self.serv.recv()
        self.assertMsgEqual(Lwm2mSend(), pkt)
        CBOR.parse(pkt.content).verify_values(test=self,
                                              expected_value_map={
                                                  ResPath.FirmwareUpdate.UpdateResult: UpdateResult.INITIAL,
                                                  ResPath.FirmwareUpdate.State: UpdateState.DOWNLOADING
                                              })
        self.serv.send(Lwm2mChanged.matching(pkt)())

        self.provide_response(use_real_app=True)

        pkt = self.serv.recv()
        self.assertMsgEqual(Lwm2mSend(), pkt)
        CBOR.parse(pkt.content).verify_values(test=self,
                                              expected_value_map={
                                                  ResPath.FirmwareUpdate.UpdateResult: UpdateResult.INITIAL,
                                                  ResPath.FirmwareUpdate.State: UpdateState.DOWNLOADED
                                              })
        self.serv.send(Lwm2mChanged.matching(pkt)())

        # Execute /5/0/2 (Update)
        req = Lwm2mExecute(ResPath.FirmwareUpdate.Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        pkt = self.serv.recv()
        self.assertMsgEqual(Lwm2mSend(), pkt)
        CBOR.parse(pkt.content).verify_values(test=self,
                                              expected_value_map={
                                                  ResPath.FirmwareUpdate.UpdateResult: UpdateResult.INITIAL,
                                                  ResPath.FirmwareUpdate.State: UpdateState.UPDATING
                                              })
        self.serv.send(Lwm2mChanged.matching(pkt)())

        # there should be exactly one request
        self.assertEqual(['/firmware'], self.requests)

        self.serv.reset()
        self.assertDemoRegisters(version='1.1')

        pkt = self.serv.recv()
        self.assertMsgEqual(Lwm2mSend(), pkt)
        parsed_cbor = CBOR.parse(pkt.content)
        parsed_cbor.verify_values(test=self,
                                  expected_value_map={
                                      ResPath.FirmwareUpdate.UpdateResult: UpdateResult.SUCCESS,
                                      ResPath.FirmwareUpdate.State: UpdateState.IDLE
                                  })
        # Check if Send contains firmware and software version
        self.assertEqual(parsed_cbor[2].get(SenmlLabel.NAME), '/3/0/3')
        self.assertEqual(parsed_cbor[3].get(SenmlLabel.NAME), '/3/0/19')
        self.serv.send(Lwm2mChanged.matching(pkt)())


class FirmwareUpdateBadBase64(FirmwareUpdate.Test):
    def runTest(self):
        # Write /5/0/0 (Firmware): some random text to see how it makes the world burn
        # (as text context does not implement some_bytes handler).
        data = bytes(b'\x01' * 16)
        req = Lwm2mWrite(ResPath.FirmwareUpdate.Package, data,
                         format=coap.ContentFormat.TEXT_PLAIN)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_BAD_REQUEST),
                            self.serv.recv())


class FirmwareUpdateGoodBase64(FirmwareUpdate.Test):
    def runTest(self):
        import base64
        data = base64.encodebytes(bytes(b'\x01' * 16)).replace(b'\n', b'')
        req = Lwm2mWrite(ResPath.FirmwareUpdate.Package, data,
                         format=coap.ContentFormat.TEXT_PLAIN)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())


class FirmwareUpdateNullPkg(FirmwareUpdate.TestWithHttpServer):
    def runTest(self):
        self.provide_response()
        self.write_firmware_and_wait_for_download(self.get_firmware_uri())

        req = Lwm2mWrite(ResPath.FirmwareUpdate.Package, b'\0',
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.assertEqual(UpdateState.IDLE, self.read_state())
        self.assertEqual(UpdateResult.INITIAL, self.read_update_result())


class FirmwareUpdateEmptyPkgUri(FirmwareUpdate.TestWithHttpServer):
    def runTest(self):
        self.provide_response()
        self.write_firmware_and_wait_for_download(self.get_firmware_uri())

        req = Lwm2mWrite(ResPath.FirmwareUpdate.PackageURI, '')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.assertEqual(UpdateState.IDLE, self.read_state())
        self.assertEqual(UpdateResult.INITIAL, self.read_update_result())


class FirmwareUpdateInvalidUri(FirmwareUpdate.Test):
    def runTest(self):
        # observe Result
        observe_req = Lwm2mObserve(ResPath.FirmwareUpdate.UpdateResult)
        self.serv.send(observe_req)
        self.assertMsgEqual(Lwm2mContent.matching(
            observe_req)(content=b'0'), self.serv.recv())

        req = Lwm2mWrite(ResPath.FirmwareUpdate.PackageURI,
                         b'http://invalidfirmware.exe')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        while True:
            notify = self.serv.recv()
            self.assertMsgEqual(Lwm2mNotify(observe_req.token), notify)
            if int(notify.content) != UpdateResult.INITIAL:
                break
        self.assertEqual(UpdateResult.INVALID_URI, int(notify.content))
        self.serv.send(Lwm2mReset(msg_id=notify.msg_id))
        self.assertEqual(UpdateState.IDLE, self.read_state())


class FirmwareUpdateUnsupportedUri(FirmwareUpdate.Test):
    def runTest(self):
        req = Lwm2mWrite(ResPath.FirmwareUpdate.PackageURI,
                         b'unsupported://uri.exe')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_BAD_REQUEST),
                            self.serv.recv())
        # This does not even change state or anything, because according to the LwM2M spec
        # Server can't feed us with unsupported URI type
        self.assertEqual(UpdateState.IDLE, self.read_state())
        self.assertEqual(UpdateResult.UNSUPPORTED_PROTOCOL,
                         self.read_update_result())


class FirmwareUpdateOfflineUriTest(FirmwareUpdate.TestWithHttpServer):
    def runTest(self):
        self.communicate('enter-offline tcp')

        self.assertEqual(UpdateState.IDLE, self.read_state())
        self.assertEqual(UpdateResult.INITIAL, self.read_update_result())

        req = Lwm2mWrite(ResPath.FirmwareUpdate.PackageURI,
                         self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.assertEqual(UpdateState.IDLE, self.read_state())
        self.assertEqual(UpdateResult.CONNECTION_LOST,
                         self.read_update_result())


class FirmwareUpdateReplacingPkgUri(FirmwareUpdate.TestWithHttpServer):
    def runTest(self):
        self.provide_response()
        self.write_firmware_and_wait_for_download(self.get_firmware_uri())

        # This isn't specified anywhere as a possible transition, therefore
        # it is most likely a bad request.
        req = Lwm2mWrite(ResPath.FirmwareUpdate.PackageURI, 'http://something')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_BAD_REQUEST),
                            self.serv.recv())

        self.assertEqual(UpdateState.DOWNLOADED, self.read_state())
        self.assertEqual(UpdateResult.INITIAL, self.read_update_result())


class FirmwareUpdateReplacingPkg(FirmwareUpdate.TestWithHttpServer):
    def runTest(self):
        self.provide_response()
        self.write_firmware_and_wait_for_download(self.get_firmware_uri())

        # This isn't specified anywhere as a possible transition, therefore
        # it is most likely a bad request.
        req = Lwm2mWrite(ResPath.FirmwareUpdate.Package, b'trololo',
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_BAD_REQUEST),
                            self.serv.recv())

        self.assertEqual(UpdateState.DOWNLOADED, self.read_state())
        self.assertEqual(UpdateResult.INITIAL, self.read_update_result())


class FirmwareUpdateHttpsResumptionTest(FirmwareUpdate.TestWithPartialDownloadAndRestart,
                                        FirmwareUpdate.TestWithHttpsServer):
    RESPONSE_DELAY = 0.5
    CHUNK_SIZE = 1000
    ETAGS = True

    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(False)
        self.set_reset_machine(False)

    def runTest(self):
        self.provide_response()

        # Write /5/0/1 (Firmware URI)
        req = Lwm2mWrite(ResPath.FirmwareUpdate.PackageURI,
                         self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_for_half_download()
        self.demo_process.kill()

        # restart demo app
        self.serv.reset()

        self.provide_response()
        self._start_demo(self.cmdline_args)
        self.assertDemoRegisters(self.serv)

        deadline = time.time() + 20
        while time.time() < deadline:
            time.sleep(0.5)

            if self.read_state() == UpdateState.DOWNLOADED:
                break
        else:
            raise

        # Execute /5/0/2 (Update)
        req = Lwm2mExecute(ResPath.FirmwareUpdate.Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())


class FirmwareUpdateHttpsCancelPackageTest(FirmwareUpdate.TestWithPartialDownload,
                                           FirmwareUpdate.TestWithHttpServer):
    RESPONSE_DELAY = 0.5
    CHUNK_SIZE = 1000
    ETAGS = True

    def runTest(self):
        self.provide_response()

        # Write /5/0/1 (Firmware URI)
        req = Lwm2mWrite(ResPath.FirmwareUpdate.PackageURI,
                         self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_for_half_download()

        self.assertEqual(self.get_socket_count(), 2)

        req = Lwm2mWrite(ResPath.FirmwareUpdate.Package, b'\0',
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_until_socket_count(expected=1, timeout_s=5)

        self.assertEqual(UpdateState.IDLE, self.read_state())
        self.assertEqual(UpdateResult.INITIAL, self.read_update_result())


class FirmwareUpdateHttpsCancelPackageUriTest(FirmwareUpdate.TestWithPartialDownload,
                                              FirmwareUpdate.TestWithHttpServer):
    RESPONSE_DELAY = 0.5
    CHUNK_SIZE = 1000
    ETAGS = True

    def runTest(self):
        self.provide_response()

        # Write /5/0/1 (Firmware URI)
        req = Lwm2mWrite(ResPath.FirmwareUpdate.PackageURI,
                         self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_for_half_download()

        self.assertEqual(self.get_socket_count(), 2)

        req = Lwm2mWrite(ResPath.FirmwareUpdate.PackageURI, b'')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_until_socket_count(expected=1, timeout_s=5)

        self.assertEqual(UpdateState.IDLE, self.read_state())
        self.assertEqual(UpdateResult.INITIAL, self.read_update_result())


class FirmwareUpdateCoapCancelPackageUriTest(FirmwareUpdate.TestWithPartialDownload,
                                             FirmwareUpdate.TestWithCoapServer):
    def runTest(self):
        with self.file_server as file_server:
            file_server.set_resource('/firmware',
                                     make_firmware_package(self.FIRMWARE_SCRIPT_CONTENT))
            fw_uri = file_server.get_resource_uri('/firmware')

            # Write /5/0/1 (Firmware URI)
            req = Lwm2mWrite(ResPath.FirmwareUpdate.PackageURI, fw_uri)
            self.serv.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                                self.serv.recv())

            # Handle one GET
            file_server.handle_request()

        self.assertEqual(self.get_socket_count(), 2)

        # Cancel download
        req = Lwm2mWrite(ResPath.FirmwareUpdate.PackageURI, b'')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_until_socket_count(expected=1, timeout_s=5)

        self.assertEqual(UpdateState.IDLE, self.read_state())
        self.assertEqual(UpdateResult.INITIAL, self.read_update_result())


class FirmwareUpdateHttpsOfflineTest(FirmwareUpdate.TestWithPartialDownloadAndRestart,
                                     FirmwareUpdate.TestWithHttpServer):
    RESPONSE_DELAY = 0.5
    CHUNK_SIZE = 1000
    ETAGS = True

    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(False)
        self.set_reset_machine(False)

    def runTest(self):
        self.provide_response()

        # Write /5/0/1 (Firmware URI)
        req = Lwm2mWrite(ResPath.FirmwareUpdate.PackageURI,
                         self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_for_half_download()

        self.assertEqual(self.get_socket_count(), 2)
        self.communicate('enter-offline tcp')
        self.wait_until_socket_count(expected=1, timeout_s=5)
        self.provide_response()
        self.communicate('exit-offline tcp')

        deadline = time.time() + 20
        while time.time() < deadline:
            time.sleep(0.5)

            if self.read_state() == UpdateState.DOWNLOADED:
                break
        else:
            raise

        # Execute /5/0/2 (Update)
        req = Lwm2mExecute(ResPath.FirmwareUpdate.Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())


class FirmwareUpdateHttpsTest(FirmwareUpdate.TestWithHttpsServer):
    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(False)
        self.set_reset_machine(False)

    def runTest(self):
        self.provide_response()
        self.write_firmware_and_wait_for_download(
            self.get_firmware_uri(), download_timeout_s=20)

        # Execute /5/0/2 (Update)
        req = Lwm2mExecute(ResPath.FirmwareUpdate.Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())


class FirmwareUpdateUnconfiguredHttpsTest(FirmwareUpdate.TestWithHttpsServer):
    def setUp(self):
        super().setUp(pass_cert_to_demo=False)

    def runTest(self):
        # disable minimum notification period
        write_attrs_req = Lwm2mWriteAttributes(ResPath.FirmwareUpdate.UpdateResult,
                                               query=['pmin=0'])
        self.serv.send(write_attrs_req)
        self.assertMsgEqual(Lwm2mChanged.matching(
            write_attrs_req)(), self.serv.recv())

        # initial result should be 0
        observe_req = Lwm2mObserve(ResPath.FirmwareUpdate.UpdateResult)
        self.serv.send(observe_req)
        self.assertMsgEqual(Lwm2mContent.matching(
            observe_req)(content=b'0'), self.serv.recv())

        # Write /5/0/1 (Firmware URI)
        req = Lwm2mWrite(ResPath.FirmwareUpdate.PackageURI,
                         self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # even before reaching the server, we should get an error
        notify_msg = self.serv.recv()
        # no security information => "Unsupported protocol"
        self.assertMsgEqual(Lwm2mNotify(observe_req.token,
                                        str(UpdateResult.UNSUPPORTED_PROTOCOL).encode()),
                            notify_msg)
        self.serv.send(Lwm2mReset(msg_id=notify_msg.msg_id))
        self.assertEqual(0, self.read_state())


class FirmwareUpdateUnconfiguredHttpsWithFallbackAttemptTest(
    FirmwareUpdate.TestWithHttpsServer):
    def setUp(self):
        super().setUp(pass_cert_to_demo=False,
                      psk_identity=b'test-identity', psk_key=b'test-key')

    def runTest(self):
        # disable minimum notification period
        write_attrs_req = Lwm2mWriteAttributes(ResPath.FirmwareUpdate.UpdateResult,
                                               query=['pmin=0'])
        self.serv.send(write_attrs_req)
        self.assertMsgEqual(Lwm2mChanged.matching(
            write_attrs_req)(), self.serv.recv())

        # initial result should be 0
        observe_req = Lwm2mObserve(ResPath.FirmwareUpdate.UpdateResult)
        self.serv.send(observe_req)
        self.assertMsgEqual(Lwm2mContent.matching(
            observe_req)(content=b'0'), self.serv.recv())

        # Write /5/0/1 (Firmware URI)
        req = Lwm2mWrite(ResPath.FirmwareUpdate.PackageURI,
                         self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # even before reaching the server, we should get an error
        notify_msg = self.serv.recv()
        # no security information => client will attempt PSK from data model
        # and fail handshake => "Connection lost"
        self.assertMsgEqual(Lwm2mNotify(observe_req.token,
                                        str(UpdateResult.CONNECTION_LOST).encode()),
                            notify_msg)
        self.serv.send(Lwm2mReset(msg_id=notify_msg.msg_id))
        self.assertEqual(0, self.read_state())


class FirmwareUpdateInvalidHttpsTest(FirmwareUpdate.TestWithHttpsServer):
    def setUp(self):
        super().setUp(cn='invalid_cn', alt_ip=None)

    def runTest(self):
        # disable minimum notification period
        write_attrs_req = Lwm2mWriteAttributes(ResPath.FirmwareUpdate.UpdateResult,
                                               query=['pmin=0'])
        self.serv.send(write_attrs_req)
        self.assertMsgEqual(Lwm2mChanged.matching(
            write_attrs_req)(), self.serv.recv())

        # initial result should be 0
        observe_req = Lwm2mObserve(ResPath.FirmwareUpdate.UpdateResult)
        self.serv.send(observe_req)
        self.assertMsgEqual(Lwm2mContent.matching(
            observe_req)(content=b'0'), self.serv.recv())

        # Write /5/0/1 (Firmware URI)
        req = Lwm2mWrite(ResPath.FirmwareUpdate.PackageURI,
                         self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # even before reaching the server, we should get an error
        notify_msg = self.serv.recv()
        # handshake failure => "Connection lost"
        self.assertMsgEqual(Lwm2mNotify(observe_req.token,
                                        str(UpdateResult.CONNECTION_LOST).encode()),
                            notify_msg)
        self.serv.send(Lwm2mReset(msg_id=notify_msg.msg_id))
        self.assertEqual(0, self.read_state())


class FirmwareUpdateResetInIdleState(FirmwareUpdate.Test):
    def runTest(self):
        self.assertEqual(UpdateState.IDLE, self.read_state())

        req = Lwm2mWrite(ResPath.FirmwareUpdate.PackageURI, b'')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())
        self.assertEqual(UpdateState.IDLE, self.read_state())
        self.assertEqual(UpdateResult.INITIAL, self.read_update_result())

        req = Lwm2mWrite(ResPath.FirmwareUpdate.Package, b'\0',
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())
        self.assertEqual(UpdateState.IDLE, self.read_state())
        self.assertEqual(UpdateResult.INITIAL, self.read_update_result())


class FirmwareUpdateCoapUri(FirmwareUpdate.TestWithCoapServer):
    def tearDown(self):
        super().tearDown()

        # there should be exactly one request
        with self.file_server as file_server:
            self.assertEqual(1, len(file_server.requests))
            self.assertMsgEqual(CoapGet('/firmware'),
                                file_server.requests[0])

    def runTest(self):
        with self.file_server as file_server:
            file_server.set_resource('/firmware',
                                     make_firmware_package(self.FIRMWARE_SCRIPT_CONTENT))
            fw_uri = file_server.get_resource_uri('/firmware')
        self.write_firmware_and_wait_for_download(fw_uri)


class FirmwareUpdateRestartWithDownloaded(FirmwareUpdate.Test):
    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(False)
        self.set_reset_machine(False)

    def runTest(self):
        # Write /5/0/0 (Firmware): script content
        req = Lwm2mWrite(ResPath.FirmwareUpdate.Package,
                         make_firmware_package(self.FIRMWARE_SCRIPT_CONTENT),
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # restart the app
        self.teardown_demo_with_servers()
        self.setup_demo_with_servers(
            fw_updated_marker_path=self.ANJAY_MARKER_FILE)

        self.assertEqual(UpdateState.DOWNLOADED, self.read_state())
        self.assertEqual(UpdateResult.INITIAL, self.read_update_result())

        # Execute /5/0/2 (Update)
        req = Lwm2mExecute(ResPath.FirmwareUpdate.Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())


class FirmwareUpdateRestartWithDownloading(
    FirmwareUpdate.TestWithPartialCoapDownloadAndRestart):
    def runTest(self):
        # Write /5/0/1 (Firmware URI)
        req = Lwm2mWrite(ResPath.FirmwareUpdate.PackageURI, self.fw_uri)
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
            self.assertIn(
                state, {UpdateState.DOWNLOADING, UpdateState.DOWNLOADED})
            if state == UpdateState.DOWNLOADED:
                break
        self.assertEqual(state, UpdateState.DOWNLOADED)


class FirmwareUpdateRestartWithDownloadingETagChange(
    FirmwareUpdate.TestWithPartialCoapDownloadAndRestart):
    def runTest(self):
        # Write /5/0/1 (Firmware URI)
        req = Lwm2mWrite(ResPath.FirmwareUpdate.PackageURI, self.fw_uri)
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
            file_server.set_resource('/firmware',
                                     make_firmware_package(
                                         self.FIRMWARE_SCRIPT_CONTENT),
                                     etag=new_etag)
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
            self.assertIn(
                state, {UpdateState.DOWNLOADING, UpdateState.DOWNLOADED})
            if state == UpdateState.DOWNLOADED:
                break
            # prevent test from reading Result hundreds of times per second
            time.sleep(0.5)

        self.assertEqual(state, UpdateState.DOWNLOADED)
        self.assertTrue(file_truncated)


class FirmwareUpdateRestartWithDownloadingOverHttp(
    FirmwareUpdate.TestWithPartialHttpDownloadAndRestart):
    def get_etag(self, response_content):
        return None

    def runTest(self):
        self.provide_response()
        # Write /5/0/1 (Firmware URI)
        req = Lwm2mWrite(ResPath.FirmwareUpdate.PackageURI,
                         self.get_firmware_uri())
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
            self.assertIn(
                state, {UpdateState.DOWNLOADING, UpdateState.DOWNLOADED})
            if state == UpdateState.DOWNLOADED:
                break
            # prevent test from reading Result hundreds of times per second
            time.sleep(0.5)

        self.assertEqual(state, UpdateState.DOWNLOADED)
        self.assertTrue(file_truncated)

        self.assertEqual(len(self.requests), 2)


class FirmwareUpdateResumeDownloadingOverHttp(
    FirmwareUpdate.TestWithPartialHttpDownloadAndRestart):
    def send_headers(self, handler, response_content, response_etag):
        if 'Range' in handler.headers:
            self.assertEqual(handler.headers['If-Match'], response_etag)
            match = re.fullmatch(r'bytes=([0-9]+)-', handler.headers['Range'])
            self.assertIsNotNone(match)
            offset = int(match.group(1))
            handler.send_header('Content-range',
                                'bytes %d-%d/*' % (offset, len(response_content) - 1))
            return offset

    def runTest(self):
        self.provide_response()
        # Write /5/0/1 (Firmware URI)
        req = Lwm2mWrite(ResPath.FirmwareUpdate.PackageURI,
                         self.get_firmware_uri())
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
            self.assertIn(
                state, {UpdateState.DOWNLOADING, UpdateState.DOWNLOADED})
            if state == UpdateState.DOWNLOADED:
                break
            # prevent test from reading Result hundreds of times per second
            time.sleep(0.5)

        self.assertEqual(state, UpdateState.DOWNLOADED)

        self.assertEqual(len(self.requests), 2)


class FirmwareUpdateResumeDownloadingOverHttpWithReconnect(
    FirmwareUpdate.TestWithPartialHttpDownloadAndRestart):
    def _get_valgrind_args(self):
        # we don't kill the process here, so we want Valgrind
        return FirmwareUpdate.TestWithHttpServer._get_valgrind_args(self)

    def send_headers(self, handler, response_content, response_etag):
        if 'Range' in handler.headers:
            self.assertEqual(handler.headers['If-Match'], response_etag)
            match = re.fullmatch(r'bytes=([0-9]+)-', handler.headers['Range'])
            self.assertIsNotNone(match)
            offset = int(match.group(1))
            handler.send_header('Content-range',
                                'bytes %d-%d/*' % (offset, len(response_content) - 1))
            return offset

    def runTest(self):
        self.provide_response()
        # Write /5/0/1 (Firmware URI)
        req = Lwm2mWrite(ResPath.FirmwareUpdate.PackageURI,
                         self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_for_half_download()

        # reconnect
        self.serv.reset()
        self.communicate('reconnect')
        self.provide_response()
        self.assertDemoRegisters(self.serv)

        # wait until client downloads the firmware
        deadline = time.time() + 20
        state = None
        while time.time() < deadline:
            fsize = os.stat(self.fw_file_name).st_size
            self.assertGreater(fsize * 2, self.GARBAGE_SIZE)
            state = self.read_state()
            self.assertIn(
                state, {UpdateState.DOWNLOADING, UpdateState.DOWNLOADED})
            if state == UpdateState.DOWNLOADED:
                break
            # prevent test from reading Result hundreds of times per second
            time.sleep(0.5)

        self.assertEqual(state, UpdateState.DOWNLOADED)

        self.assertEqual(len(self.requests), 2)


class FirmwareUpdateResumeFromStartWithDownloadingOverHttp(
    FirmwareUpdate.TestWithPartialHttpDownloadAndRestart):
    def runTest(self):
        self.provide_response()
        # Write /5/0/1 (Firmware URI)
        req = Lwm2mWrite(ResPath.FirmwareUpdate.PackageURI,
                         self.get_firmware_uri())
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
            self.assertIn(
                state, {UpdateState.DOWNLOADING, UpdateState.DOWNLOADED})
            if state == UpdateState.DOWNLOADED:
                break
            # prevent test from reading Result hundreds of times per second
            time.sleep(0.5)

        self.assertEqual(state, UpdateState.DOWNLOADED)

        self.assertEqual(len(self.requests), 2)


class FirmwareUpdateRestartAfter412WithDownloadingOverHttp(
    FirmwareUpdate.TestWithPartialHttpDownloadAndRestart):
    def check_success(self, handler, response_content, response_etag):
        if 'If-Match' in handler.headers:
            self.assertEqual(handler.headers['If-Match'], response_etag)
            handler.send_error(http.HTTPStatus.PRECONDITION_FAILED)

    def runTest(self):
        self.provide_response()
        # Write /5/0/1 (Firmware URI)
        req = Lwm2mWrite(ResPath.FirmwareUpdate.PackageURI,
                         self.get_firmware_uri())
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
            self.assertIn(
                state, {UpdateState.DOWNLOADING, UpdateState.DOWNLOADED})
            if state == UpdateState.DOWNLOADED:
                break
            # prevent test from reading Result hundreds of times per second
            time.sleep(0.5)

        self.assertEqual(state, UpdateState.DOWNLOADED)
        self.assertTrue(file_truncated)

        self.assertEqual(len(self.requests), 3)


class FirmwareUpdateWithDelayedResultTest:
    class TestMixin:
        def runTest(self, forced_error, result):
            with open(os.path.join(self.config.demo_path, self.config.demo_cmd), 'rb') as f:
                firmware = f.read()

            # Write /5/0/0 (Firmware)
            self.block_send(firmware,
                            equal_chunk_splitter(chunk_size=1024),
                            force_error=forced_error)

            # Execute /5/0/2 (Update)
            req = Lwm2mExecute(ResPath.FirmwareUpdate.Update)
            self.serv.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                                self.serv.recv())

            self.serv.reset()
            self.assertDemoRegisters()
            self.assertEqual(self.read_path(self.serv, ResPath.FirmwareUpdate.UpdateResult).content,
                             str(result).encode())
            self.assertEqual(self.read_path(self.serv, ResPath.FirmwareUpdate.State).content,
                             str(UpdateState.IDLE).encode())


class FirmwareUpdateWithDelayedSuccessTest(
    FirmwareUpdateWithDelayedResultTest.TestMixin, Block.Test):
    def runTest(self):
        super().runTest(FirmwareUpdateForcedError.DelayedSuccess, UpdateResult.SUCCESS)


class FirmwareUpdateWithDelayedFailureTest(
    FirmwareUpdateWithDelayedResultTest.TestMixin, Block.Test):
    def runTest(self):
        super().runTest(FirmwareUpdateForcedError.DelayedFailedUpdate, UpdateResult.FAILED)


class FirmwareUpdateWithSetSuccessInPerformUpgrade(Block.Test):
    def runTest(self):
        with open(os.path.join(self.config.demo_path, self.config.demo_cmd), 'rb') as f:
            firmware = f.read()

        # Write /5/0/0 (Firmware)
        self.block_send(firmware,
                        equal_chunk_splitter(chunk_size=1024),
                        force_error=FirmwareUpdateForcedError.SetSuccessInPerformUpgrade)

        # Execute /5/0/2 (Update)
        req = Lwm2mExecute(ResPath.FirmwareUpdate.Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # perform_upgrade handler is called via scheduler, so there is a small
        # window during which reading the Firmware Update State still returns
        # Updating. Wait for a while for State to actually change.
        observed_states = []
        deadline = time.time() + 5  # arbitrary limit
        while not observed_states or observed_states[-1] == str(
                UpdateState.UPDATING):
            if time.time() > deadline:
                self.fail('Firmware Update did not finish on time, last state = %s' % (
                    observed_states[-1] if observed_states else 'NONE'))
            observed_states.append(
                self.read_path(self.serv, ResPath.FirmwareUpdate.State).content.decode())
            time.sleep(0.5)

        self.assertNotEqual([], observed_states)
        self.assertEqual(observed_states[-1], str(UpdateState.IDLE))
        self.assertEqual(self.read_path(self.serv, ResPath.FirmwareUpdate.UpdateResult).content,
                         str(UpdateResult.SUCCESS).encode())


class FirmwareUpdateWithSetFailureInPerformUpgrade(Block.Test):
    def runTest(self):
        with open(os.path.join(self.config.demo_path, self.config.demo_cmd), 'rb') as f:
            firmware = f.read()

        # Write /5/0/0 (Firmware)
        self.block_send(firmware,
                        equal_chunk_splitter(chunk_size=1024),
                        force_error=FirmwareUpdateForcedError.SetFailureInPerformUpgrade)

        # Execute /5/0/2 (Update)
        req = Lwm2mExecute(ResPath.FirmwareUpdate.Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # perform_upgrade handler is called via scheduler, so there is a small
        # window during which reading the Firmware Update State still returns
        # Updating. Wait for a while for State to actually change.
        observed_states = []
        deadline = time.time() + 5  # arbitrary limit
        while not observed_states or observed_states[-1] == str(
                UpdateState.UPDATING):
            if time.time() > deadline:
                self.fail('Firmware Update did not finish on time, last state = %s' % (
                    observed_states[-1] if observed_states else 'NONE'))
            observed_states.append(
                self.read_path(self.serv, ResPath.FirmwareUpdate.State).content.decode())
            time.sleep(0.5)

        self.assertNotEqual([], observed_states)
        self.assertEqual(observed_states[-1], str(UpdateState.IDLE))
        self.assertEqual(self.read_path(self.serv, ResPath.FirmwareUpdate.UpdateResult).content,
                         str(UpdateResult.FAILED).encode())


try:
    import aiocoap
    import aiocoap.resource
    import aiocoap.transports.tls
except ImportError:
    # FirmwareUpdateCoapTlsTest requires a bleeding-edge version of aiocoap, that at the time of
    # writing this code, is not available even in the prerelease channel.
    # So we're not enforcing this dependency for now.
    pass


@unittest.skipIf('aiocoap.transports.tls' not in sys.modules,
                 'aiocoap.transports.tls not available')
@unittest.skipIf(sys.version_info < (3, 5, 3),
                 'SSLContext signature changed in Python 3.5.3')
class FirmwareUpdateCoapTlsTest(
    FirmwareUpdate.TestWithTlsServer, test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(garbage=8000)

        class FirmwareResource(aiocoap.resource.Resource):
            async def render_get(resource, request):
                return aiocoap.Message(payload=make_firmware_package(
                    self.FIRMWARE_SCRIPT_CONTENT))

        serversite = aiocoap.resource.Site()
        serversite.add_resource(('firmware',), FirmwareResource())

        sslctx = ssl.SSLContext()
        sslctx.load_cert_chain(self._cert_file, self._key_file)

        class EphemeralTlsServer(aiocoap.transports.tls.TLSServer):
            _default_port = 0

        class CoapTcpFileServerThread(threading.Thread):
            def __init__(self):
                super().__init__()
                self.loop = asyncio.new_event_loop()
                ctx = aiocoap.Context(loop=self.loop, serversite=serversite)
                self.loop.run_until_complete(ctx._append_tokenmanaged_transport(
                    lambda tman: EphemeralTlsServer.create_server(('127.0.0.1', 0), tman, ctx.log,
                                                                  self.loop, sslctx)))

                socket = ctx.request_interfaces[0].token_interface.server.sockets[0]
                self.server_address = socket.getsockname()

            def run(self):
                asyncio.set_event_loop(self.loop)
                try:
                    self.loop.run_forever()
                finally:
                    self.loop.run_until_complete(
                        self.loop.shutdown_asyncgens())
                    self.loop.close()

        self.server_thread = CoapTcpFileServerThread()
        self.server_thread.start()

        self.set_check_marker(True)
        self.set_auto_deregister(False)
        self.set_reset_machine(False)

    def tearDown(self):
        try:
            super().tearDown()
        finally:
            self.server_thread.loop.call_soon_threadsafe(
                self.server_thread.loop.stop)
            self.server_thread.join()

    def get_firmware_uri(self):
        return 'coaps+tcp://127.0.0.1:%d/firmware' % (
            self.server_thread.server_address[1],)

    def runTest(self):
        self.write_firmware_and_wait_for_download(self.get_firmware_uri())

        # Execute /5/0/2 (Update)
        req = Lwm2mExecute(ResPath.FirmwareUpdate.Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())


class FirmwareUpdateWeakEtagTest(FirmwareUpdate.TestWithHttpServer):
    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(False)
        self.set_reset_machine(False)

        orig_end_headers = self.http_server.RequestHandlerClass.end_headers

        def updated_end_headers(request_handler):
            request_handler.send_header('ETag', 'W/"weaketag"')
            orig_end_headers(request_handler)

        self.http_server.RequestHandlerClass.end_headers = updated_end_headers

    def runTest(self):
        self.provide_response()
        self.write_firmware_and_wait_for_download(self.get_firmware_uri())

        with open(self.ANJAY_MARKER_FILE, 'rb') as f:
            marker_data = f.read()

        self.assertNotIn(b'weaketag', marker_data)

        # Execute /5/0/2 (Update)
        req = Lwm2mExecute(ResPath.FirmwareUpdate.Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())


class SameSocketDownload:
    class Test(test_suite.Lwm2mDtlsSingleServerTest, test_suite.Lwm2mDmOperations):
        GARBAGE_SIZE = 2048
        # Set to be able to comfortably test interleaved requests
        ACK_TIMEOUT = 10
        MAX_RETRANSMIT = 4
        # Sometimes we want to allow the client to send more than one request at a time
        # e.g. Update and GET.
        NSTART = 1
        LIFETIME = 86400
        BINDING = 'U'
        BLK_SZ = 1024

        def setUp(self, *args, **kwargs):
            if 'extra_cmdline_args' not in kwargs:
                kwargs['extra_cmdline_args'] = []

            kwargs['extra_cmdline_args'] += [
                '--prefer-same-socket-downloads',
                '--ack-timeout', str(self.ACK_TIMEOUT),
                '--max-retransmit', str(self.MAX_RETRANSMIT),
                '--ack-random-factor', str(1.0),
                '--nstart', str(self.NSTART)
            ]
            kwargs['lifetime'] = self.LIFETIME
            super().setUp(*args, **kwargs)
            self.file_server = CoapFileServer(
                self.serv._coap_server, binding=self.BINDING)
            self.file_server.set_resource(path=FIRMWARE_PATH,
                                          data=make_firmware_package(b'a' * self.GARBAGE_SIZE))

        def read_state(self, serv=None):
            return int(self.read_resource(serv or self.serv,
                                          oid=OID.FirmwareUpdate,
                                          iid=0,
                                          rid=RID.FirmwareUpdate.State).content)

        def read_result(self, serv=None):
            return int(self.read_resource(serv or self.serv,
                                          oid=OID.FirmwareUpdate,
                                          iid=0,
                                          rid=RID.FirmwareUpdate.UpdateResult).content)

        def start_download(self):
            self.write_resource(self.serv,
                                oid=OID.FirmwareUpdate,
                                iid=0,
                                rid=RID.FirmwareUpdate.PackageURI,
                                content=self.file_server.get_resource_uri(FIRMWARE_PATH))

        def handle_get(self, pkt=None):
            if pkt is None:
                pkt = self.serv.recv()
            block2 = pkt.get_options(coap.Option.BLOCK2)
            if block2:
                self.assertEqual(block2[0].block_size(), self.BLK_SZ)
            self.file_server.handle_recvd_request(pkt)

        def num_blocks(self):
            return (len(
                self.file_server._resources[FIRMWARE_PATH].data) + self.BLK_SZ - 1) // self.BLK_SZ


class FirmwareDownloadSameSocket(SameSocketDownload.Test):
    def runTest(self):
        self.start_download()

        for _ in range(self.num_blocks()):
            pkt = self.serv.recv()
            self.handle_get(pkt)

        self.assertEqual(self.read_state(), UpdateState.DOWNLOADED)


class FirmwareDownloadSameSocketAndOngoingBlockwiseWrite(
    SameSocketDownload.Test):
    def runTest(self):
        self.create_instance(self.serv, oid=OID.Test, iid=1)
        self.start_download()

        resource_num_blocks = 10
        self.assertGreaterEqual(resource_num_blocks, self.num_blocks())
        for seq_num in range(resource_num_blocks):
            if seq_num < self.num_blocks():
                dl_req_get = self.serv.recv()

            has_more = seq_num < resource_num_blocks - 1
            # Server does blockwise write on the unrelated resource
            pkt = Lwm2mWrite(ResPath.Test[1].ResRawBytes, b'x' * 16,
                             format=coap.ContentFormat.APPLICATION_OCTET_STREAM,
                             options=[coap.Option.BLOCK1(seq_num, has_more, 16)])
            self.serv.send(pkt)
            if has_more:
                self.assertMsgEqual(
                    Lwm2mContinue.matching(pkt)(), self.serv.recv())
            else:
                self.assertMsgEqual(
                    Lwm2mChanged.matching(pkt)(), self.serv.recv())

            if seq_num < self.num_blocks():
                # Client waits for next chunk of Raw Bytes, but gets firmware
                # block instead
                self.handle_get(dl_req_get)

        self.assertEqual(self.read_state(), UpdateState.DOWNLOADED)


class FirmwareDownloadSameSocketAndOngoingBlockwiseRead(
    SameSocketDownload.Test):
    BYTES = 160

    def runTest(self):
        self.create_instance(self.serv, oid=OID.Test, iid=1)
        self.write_resource(self.serv, oid=OID.Test, iid=1, rid=RID.Test.ResBytesSize,
                            content=bytes(str(self.BYTES), 'ascii'))
        self.start_download()

        block_size = 16
        self.assertGreaterEqual(self.BYTES // block_size, self.num_blocks())
        for seq_num in range(self.BYTES // block_size):
            if seq_num < self.num_blocks():
                dl_req_get = self.serv.recv()

            # Server does blockwise write on the unrelated resource
            pkt = Lwm2mRead(ResPath.Test[1].ResBytes,
                            accept=coap.ContentFormat.APPLICATION_OCTET_STREAM,
                            options=[coap.Option.BLOCK2(seq_num, 0, block_size)])
            self.serv.send(pkt)
            res = self.serv.recv()
            self.assertEqual(pkt.msg_id, res.msg_id)
            self.assertTrue(len(res.get_options(coap.Option.BLOCK2)) > 0)

            if seq_num < self.num_blocks():
                # Client waits for next chunk of Raw Bytes, but gets firmware
                # block instead
                self.handle_get(dl_req_get)

        self.assertEqual(self.read_state(), UpdateState.DOWNLOADED)


class FirmwareDownloadSameSocketUpdateDuringDownloadNstart2(
    SameSocketDownload.Test):
    NSTART = 2

    def runTest(self):
        self.start_download()

        for _ in range(self.num_blocks()):
            dl_req_get = self.serv.recv()
            # rather than responding to a request, force Update
            self.communicate('send-update')
            self.assertDemoUpdatesRegistration(timeout_s=5)
            # and only then respond with next block
            self.handle_get(dl_req_get)

        self.assertEqual(self.read_state(), UpdateState.DOWNLOADED)


class FirmwareDownloadSameSocketUpdateDuringDownloadNstart1(
    SameSocketDownload.Test):
    def runTest(self):
        self.start_download()

        for _ in range(self.num_blocks()):
            dl_req_get = self.serv.recv()
            # rather than responding to a request, force Update
            self.communicate('send-update')
            # the Update won't be received, because of NSTART=1
            with self.assertRaises(socket.timeout):
                self.serv.recv(timeout_s=3)
            # so we respond to a block
            self.handle_get(dl_req_get)
            # and only then the Update arrives
            self.assertDemoUpdatesRegistration(timeout_s=5)

        self.assertEqual(self.read_state(), UpdateState.DOWNLOADED)


class FirmwareDownloadSameSocketAndReconnectNstart1(SameSocketDownload.Test):
    def runTest(self):
        self.start_download()

        for _ in range(self.num_blocks()):
            # get dl request and ignore it
            self.serv.recv()
            # rather than responding to a request force reconnect
            self.communicate('reconnect')
            self.serv.reset()
            # demo will resume DTLS session without sending any LwM2M messages
            self.serv.listen()
            # download request is retried
            dl_req_get = self.serv.recv()
            # and finally we respond to a block
            self.handle_get(dl_req_get)

        self.assertEqual(self.read_state(), UpdateState.DOWNLOADED)


class FirmwareDownloadSameSocketUpdateTimeoutNstart2(SameSocketDownload.Test):
    NSTART = 2
    LIFETIME = 5
    MAX_RETRANSMIT = 1
    ACK_TIMEOUT = 2

    def runTest(self):
        time.sleep(self.LIFETIME + 1)
        self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT, content=b''),
                            self.serv.recv())
        self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT, content=b''),
                            self.serv.recv())
        self.start_download()

        dl_get0_0 = self.serv.recv()
        dl_get0_1 = self.serv.recv()  # retry
        self.handle_get(dl_get0_0)
        self.assertEqual(dl_get0_0.msg_id, dl_get0_1.msg_id)
        dl_get1_0 = self.serv.recv()

        # lifetime expired, demo re-registers
        self.assertDemoRegisters(lifetime=self.LIFETIME)

        # this is a retransmission
        dl_get1_1 = self.serv.recv()
        self.assertEqual(dl_get1_0.msg_id, dl_get1_1.msg_id)
        self.handle_get(dl_get1_1)
        self.handle_get(self.serv.recv())

        self.assertEqual(self.read_state(), UpdateState.DOWNLOADED)


class FirmwareDownloadSameSocketUpdateTimeoutNstart1(SameSocketDownload.Test):
    LIFETIME = 5
    MAX_RETRANSMIT = 1
    ACK_TIMEOUT = 2

    def runTest(self):
        time.sleep(self.LIFETIME + 1)
        self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT, content=b''),
                            self.serv.recv())
        self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT, content=b''),
                            self.serv.recv())
        self.start_download()

        # registration temporarily held due to ongoing download
        self.handle_get(self.serv.recv())
        # and only after handling the GET, it can be sent finally
        self.assertDemoRegisters(lifetime=self.LIFETIME)
        self.handle_get(self.serv.recv())
        self.handle_get(self.serv.recv())

        self.assertEqual(self.read_state(), UpdateState.DOWNLOADED)


class FirmwareDownloadSameSocketDontCare(SameSocketDownload.Test):
    def runTest(self):
        self.start_download()
        self.serv.recv()  # recv GET and ignore it


class FirmwareDownloadSameSocketSuspendDueToOffline(SameSocketDownload.Test):
    ACK_TIMEOUT = 1.5

    def runTest(self):
        self.start_download()
        self.serv.recv()  # ignore first GET
        self.communicate('enter-offline')
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=2 * self.ACK_TIMEOUT)
        self.serv.reset()
        self.communicate('exit-offline')

        # demo will resume DTLS session without sending any LwM2M messages
        self.serv.listen()

        for _ in range(self.num_blocks()):
            self.handle_get(self.serv.recv())

        self.assertEqual(self.read_state(), UpdateState.DOWNLOADED)


class FirmwareDownloadSameSocketSuspendDueToOfflineDuringUpdate(
    SameSocketDownload.Test):
    ACK_TIMEOUT = 1.5

    def runTest(self):
        self.start_download()
        self.serv.recv()  # ignore first GET
        self.communicate('send-update', match_regex=re.escape('Update sent'))
        # actual Update message will not arrive due to NSTART
        with self.serv.fake_close():
            self.communicate('enter-offline')
            self.wait_until_socket_count(expected=0, timeout_s=5)
        self.serv.reset()
        self.communicate('exit-offline')
        # demo will resume DTLS session before sending Register
        self.serv.listen()
        self.assertDemoUpdatesRegistration()

        for _ in range(self.num_blocks()):
            self.handle_get(self.serv.recv())

        self.assertEqual(self.read_state(), UpdateState.DOWNLOADED)


class FirmwareDownloadSameSocketSuspendDueToOfflineDuringUpdateNoMessagesCheck(
    SameSocketDownload.Test):
    ACK_TIMEOUT = 1.5

    def runTest(self):
        # Note: This test is almost identical to the one above, but does not close the socket
        # during the offline period. This is to check that the client does not attempt to send any
        # packets during that time. With the bug that triggered the addition of these test cases,
        # these were two distinct code flow paths.

        self.start_download()
        self.serv.recv()  # ignore first GET
        self.communicate('send-update', match_regex=re.escape('Update sent'))
        # actual Update message will not arrive due to NSTART
        self.communicate('enter-offline')
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=2 * self.ACK_TIMEOUT)
        self.serv.reset()
        self.communicate('exit-offline')
        # demo will resume DTLS session before sending Update
        self.serv.listen()
        self.assertDemoUpdatesRegistration()

        for _ in range(self.num_blocks()):
            self.handle_get(self.serv.recv())

        self.assertEqual(self.read_state(), UpdateState.DOWNLOADED)


class FirmwareDownloadSameSocketAndBootstrap(SameSocketDownload.Test):
    def setUp(self):
        super().setUp(bootstrap_server=True)

    def tearDown(self):
        super().tearDown(deregister_servers=[self.new_server])

    def runTest(self):
        self.start_download()
        self.serv.recv()  # recv GET and ignore it

        self.execute_resource(self.serv,
                              oid=OID.Server,
                              iid=2,
                              rid=RID.Server.RequestBootstrapTrigger)

        self.assertDemoRequestsBootstrap()

        self.new_server = Lwm2mServer()
        self.write_resource(self.bootstrap_server,
                            oid=OID.Security,
                            iid=2,
                            rid=RID.Security.ServerURI,
                            content=bytes('coap://127.0.0.1:%d' % self.new_server.get_listen_port(),
                                          'ascii'))
        self.write_resource(self.bootstrap_server,
                            oid=OID.Security,
                            iid=2,
                            rid=RID.Security.Mode,
                            content=str(coap.server.SecurityMode.NoSec.value).encode())
        req = Lwm2mBootstrapFinish()
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())

        self.assertDemoRegisters(self.new_server)
        self.assertEqual(self.read_state(self.new_server), UpdateState.IDLE)


class FirmwareDownloadSameSocketInterruptedByReboot(FirmwareUpdate.DemoArgsExtractorMixin,
                                                    SameSocketDownload.Test):
    def runTest(self):
        self.start_download()

        for i in range(self.num_blocks()):
            self.handle_get(self.serv.recv(timeout_s=5))
            if i == 0:
                dl_req = self.serv.recv(timeout_s=5)
                self.demo_process.kill()
                self.serv.reset()
                self._start_demo(self.cmdline_args)
                self.assertDemoRegisters()

        self.assertEqual(self.read_state(), UpdateState.DOWNLOADED)


class FirmwareDownloadSameSocketInterruptedByRebootTwoServers(FirmwareUpdate.DemoArgsExtractorMixin,
                                                              SameSocketDownload.Test):
    def setUp(self):
        super().setUp(servers=2, extra_cmdline_args=[
            '--access-entry', '/%d/0,1,%d' % (OID.FirmwareUpdate,
                                              AccessMask.OWNER),
            '--access-entry', '/%d/0,2,%d' % (OID.FirmwareUpdate, AccessMask.OWNER)])

    def runTest(self):
        assert self.serv == self.servers[0]

        self.start_download()
        self.handle_get(self.serv.recv(timeout_s=5))
        dl_req = self.serv.recv(timeout_s=5)

        self.demo_process.kill()
        self.servers[0].reset()
        self.servers[1].reset()
        self._start_demo(self.cmdline_args)

        # demo will resume all DTLS sessions before sending Register
        self.servers[0].listen()
        self.servers[1].listen()
        self.assertDemoRegisters(self.servers[1])
        # Still not registered with server=0, but the download is "already
        # started"
        self.assertEqual(self.read_state(
            self.servers[1]), UpdateState.DOWNLOADING)
        self.assertDemoRegisters(self.servers[0])

        for _ in range(self.num_blocks() - 1):
            self.handle_get(self.serv.recv(timeout_s=5))

        self.assertEqual(self.read_state(), UpdateState.DOWNLOADED)


class FirmwareDownloadSameSocketInterruptedByRebootTwoServersFail(
    FirmwareUpdate.DemoArgsExtractorMixin,
    SameSocketDownload.Test):
    def setUp(self):
        super().setUp(servers=2, extra_cmdline_args=[
            '--access-entry', '/%d/0,1,%d' % (OID.FirmwareUpdate,
                                              AccessMask.OWNER),
            '--access-entry', '/%d/0,2,%d' % (OID.FirmwareUpdate, AccessMask.OWNER)])

    def runTest(self):
        assert self.serv == self.servers[0]

        self.start_download()
        self.handle_get(self.serv.recv(timeout_s=5))
        dl_req = self.serv.recv(timeout_s=5)

        self.demo_process.kill()
        self.servers[0].reset()
        self.servers[1].reset()
        self._start_demo(self.cmdline_args)

        # demo will resume all DTLS sessions before sending Register
        self.servers[0].listen()
        self.servers[1].listen()
        self.assertDemoRegisters(self.servers[1])
        # Still not registered with server=0, but the download is "already
        # started"
        self.assertEqual(self.read_state(
            self.servers[1]), UpdateState.DOWNLOADING)

        # After 5 min, should stop trying.
        self.advance_demo_time(5 * 60 + 1)
        # Anjay checks server state every 1 second or so let's have some leeway
        time.sleep(3)

        self.assertEqual(self.read_result(
            self.servers[1]), UpdateResult.CONNECTION_LOST)
        self.assertEqual(self.read_state(self.servers[1]), UpdateState.IDLE)

    def tearDown(self):
        super().tearDown(deregister_servers=[self.servers[1]])


class FirmwareDownloadSameSocketInterruptedByRebootTwoServersCancel(
    FirmwareUpdate.DemoArgsExtractorMixin,
    SameSocketDownload.Test):
    def setUp(self):
        super().setUp(servers=2, extra_cmdline_args=[
            '--access-entry', '/%d/0,1,%d' % (OID.FirmwareUpdate,
                                              AccessMask.OWNER),
            '--access-entry', '/%d/0,2,%d' % (OID.FirmwareUpdate, AccessMask.OWNER)])

    def runTest(self):
        assert self.serv == self.servers[0]

        self.start_download()
        self.handle_get(self.serv.recv(timeout_s=5))
        dl_req = self.serv.recv(timeout_s=5)

        self.demo_process.kill()
        self.servers[0].reset()
        self.servers[1].reset()
        self._start_demo(self.cmdline_args)

        # demo will resume all DTLS sessions before sending Register
        self.servers[0].listen()
        self.servers[1].listen()
        self.assertDemoRegisters(self.servers[1])
        # Still not registered with server=0, but the download is "already
        # started"
        self.assertEqual(self.read_state(
            self.servers[1]), UpdateState.DOWNLOADING)

        self.write_resource(self.servers[1],
                            oid=OID.FirmwareUpdate,
                            iid=0,
                            rid=RID.FirmwareUpdate.Package,
                            content=b'\x00',
                            format=coap.ContentFormat.APPLICATION_OCTET_STREAM)

        self.assertEqual(self.read_result(
            self.servers[1]), UpdateResult.INITIAL)
        self.assertEqual(self.read_state(self.servers[1]), UpdateState.IDLE)
        self.assertDemoRegisters(self.servers[0])
