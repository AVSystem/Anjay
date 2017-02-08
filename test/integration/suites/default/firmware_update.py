from framework.lwm2m_test import *

import os
import socket
import time
import threading
import http
import http.server
import tempfile
import binascii

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
            super().setUp()
            self.ANJAY_MARKER_FILE = tempfile.NamedTemporaryFile(dir='/tmp', prefix='anjay-fw-updated-')
            self.FIRMWARE_SCRIPT_CONTENT = (FIRMWARE_SCRIPT_TEMPLATE % self.ANJAY_MARKER_FILE.name).encode('ascii')
            self.communicate('set-fw-updated-marker %s' % self.ANJAY_MARKER_FILE.name)

        def tearDown(self):
            try:
                # no deregistration here, demo already terminated
                super().tearDown(auto_deregister=getattr(self, 'auto_deregister', True))
            finally:
                if getattr(self, 'check_marker', False):
                    line = self.ANJAY_MARKER_FILE.readline()[:-1]
                    self.assertEqual(line, b"updated")

                self.ANJAY_MARKER_FILE.close()

        def read_update_result(self):
            req = Lwm2mRead('/5/0/5')
            self.serv.send(req)
            res = self.serv.recv(timeout_s=1)
            self.assertMsgEqual(Lwm2mContent.matching(req)(), res)
            return int(res.content)

        def read_state(self):
            req = Lwm2mRead('/5/0/3')
            self.serv.send(req)
            res = self.serv.recv(timeout_s=1)
            self.assertMsgEqual(Lwm2mContent.matching(req)(), res)
            return int(res.content)


    class TestWithHttpServer(Test):
        def get_firmware_uri(self):
            return 'http://127.0.0.1:%d%s' % (self.http_server.server_address[1], FIRMWARE_PATH)

        def before_download(self):
            pass

        def write_firmware_and_wait_for_download(self):
            # Write /5/0/1 (Firmware URI)
            req = Lwm2mWrite('/5/0/1', self.get_firmware_uri())
            self.serv.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                                self.serv.recv(timeout_s=1))

            # wait until client downloads the firmware
            ready = False
            for _ in range(10):
                time.sleep(0.5)

                if self.read_state() == UPDATE_STATE_DOWNLOADED:
                    break
            else:
                self.fail('firmware still not downloaded')

        def setUp(self):
            super().setUp()

            response_content = make_firmware_package(self.FIRMWARE_SCRIPT_CONTENT)
            self.requests = []

            test_case = self
            class FirmwareRequestHandler(http.server.BaseHTTPRequestHandler):
                def do_GET(self):
                    test_case.requests.append(self.path)
                    test_case.before_download()

                    self.send_header('Content-type', 'text/plain')
                    self.send_response(http.HTTPStatus.OK)
                    self.wfile.write(response_content)

                def log_request(code='-', size='-'):
                    # don't display logs on successful request
                    pass

            self.http_server = http.server.HTTPServer(('', 0),
                                                      FirmwareRequestHandler)

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

        # wait for execv
        time.sleep(0.5)

class FirmwareUpdateUriTest(FirmwareUpdate.TestWithHttpServer):
    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(False)

    def runTest(self):
        self.write_firmware_and_wait_for_download()

        # Execute /5/0/2 (Update)
        req = Lwm2mExecute('/5/0/2')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv(timeout_s=1))

        # wait for execv
        time.sleep(0.5)

class FirmwareUpdateStateChangeTest(FirmwareUpdate.TestWithHttpServer):
    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(False)

    def before_download(self):
        # wait a bit so that demo has a chance to send DOWNLOADING
        # state update
        time.sleep(1)

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
        #self.assertMsgEqual(Lwm2mNotify(observe_req.token, b'3'),
        #                    self.serv.recv())

        # wait for execv
        time.sleep(2.5)

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
        self.write_firmware_and_wait_for_download()

        req = Lwm2mWrite('/5/0/0', b'', format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv(timeout_s=1))

        self.assertEqual(UPDATE_STATE_IDLE, self.read_state())
        self.assertEqual(UPDATE_RESULT_INITIAL, self.read_update_result())

class FirmwareUpdateEmptyPkgUri(FirmwareUpdate.TestWithHttpServer):
    def runTest(self):
        self.write_firmware_and_wait_for_download()

        req = Lwm2mWrite('/5/0/1', '')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv(timeout_s=1))

        self.assertEqual(UPDATE_STATE_IDLE, self.read_state())
        self.assertEqual(UPDATE_RESULT_INITIAL, self.read_update_result())

class FirmwareUpdateInvalidUri(FirmwareUpdate.Test):
    def runTest(self):
        req = Lwm2mWrite('/5/0/1', b'http://invalidfirmware.exe')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv(timeout_s=1))
        # Wait till the demo realizes that URI is broken
        time.sleep(2.0)
        self.assertEqual(UPDATE_STATE_IDLE, self.read_state())
        self.assertEqual(UPDATE_RESULT_INVALID_URI, self.read_update_result())

class FirmwareUpdateUnsupportedUri(FirmwareUpdate.Test):
    def runTest(self):
        req = Lwm2mWrite('/5/0/1', b'unsupported://uri.exe')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_BAD_REQUEST),
                            self.serv.recv())
        # This does not even change state or anything, because according to the LWM2M spec
        # Server can't feed us with unsupported URI type
        self.assertEqual(UPDATE_STATE_IDLE, self.read_state())
        self.assertEqual(UPDATE_RESULT_UNSUPPORTED_PROTOCOL, self.read_update_result())

class FirmwareUpdateReplacingPkgUri(FirmwareUpdate.TestWithHttpServer):
    def runTest(self):
        self.write_firmware_and_wait_for_download()

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
        self.write_firmware_and_wait_for_download()

        # This isn't specified anywhere as a possible transition, therefore
        # it is most likely a bad request.
        req = Lwm2mWrite('/5/0/0', b'trololo', format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_BAD_REQUEST),
                            self.serv.recv(timeout_s=1))

        self.assertEqual(UPDATE_STATE_DOWNLOADED, self.read_state())
        self.assertEqual(UPDATE_RESULT_INITIAL, self.read_update_result())

