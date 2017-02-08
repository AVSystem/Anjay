from framework.lwm2m_test import *
import unittest
import os
import http
import threading
import time
import binascii

from .utils import DataModel, ValueValidator


class Test750_FirmwareUpdate_QueryingTheReadableResources(DataModel.Test):
    def runTest(self):
        # A READ operation from server on the resource has been received by the
        # client. This test has to be run on the following resources:
        # a) State
        # b) Update supported objects
        # c) Update result
        # d) Pkgname
        # e) Pkgversion

        self.test_read(ResPath.FirmwareUpdate.State,                  ValueValidator.integer(),      coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.FirmwareUpdate.UpdateSupportedObjects, ValueValidator.boolean(),      coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.FirmwareUpdate.UpdateResult,           ValueValidator.integer(),      coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.FirmwareUpdate.PackageName,            ValueValidator.ascii_string(), coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.FirmwareUpdate.PackageVersion,         ValueValidator.ascii_string(), coap.ContentFormat.TEXT_PLAIN)


class Test755_FirmwareUpdate_SettingTheWritableResources(DataModel.Test):
    def runTest(self):
        # A WRITE operation from server on the resource has been received by the
        # client. This test has to be run for the following resources:
        # a) Package
        # b) Package URI
        # c) Update supported objects

        # TODO: Package/Package URI are write-only. It should be impossible to
        # validate written value as the test requires.
        self.test_write(ResPath.FirmwareUpdate.Package, 'test', format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.test_write(ResPath.FirmwareUpdate.PackageURI, 'http://localhost/test')

        self.test_write_validated(ResPath.FirmwareUpdate.UpdateSupportedObjects, '1')


class Test760_FirmwareUpdate_ObservationAndNotificationOfObservableResources(DataModel.Test):
    def runTest(self):
        # A READ operation from server on the resource has been received by the
        # client. This test has to be run on the following resources:
        # a) State
        # b) Update supported objects
        # c) Update result
        # d) Pkgname
        # e) Pkgversion

        self.test_observe(ResPath.FirmwareUpdate.State,                  ValueValidator.integer())
        self.test_observe(ResPath.FirmwareUpdate.UpdateSupportedObjects, ValueValidator.boolean())
        self.test_observe(ResPath.FirmwareUpdate.UpdateResult,           ValueValidator.integer())
        self.test_observe(ResPath.FirmwareUpdate.PackageName,            ValueValidator.ascii_string())
        self.test_observe(ResPath.FirmwareUpdate.PackageVersion,         ValueValidator.ascii_string())


class Test770_FirmwareUpdate_SuccessfulFirmwareUpdateViaCoAP(DataModel.Test):
    def runTest(self):
        # 1. Step 1 – Write
        #    a. The server delivers the firmware to the device through a WRITE
        #       (CoAP PUT/POST) operation on /5/0/0 (Package)

        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.State))

        demo_executable = os.path.join(self.config.demo_path, self.config.demo_cmd)
        with open(demo_executable, 'rb') as f:
            payload = f.read()

        self.test_write_block(ResPath.FirmwareUpdate.Package,
                              make_firmware_package(payload),
                              coap.ContentFormat.APPLICATION_OCTET_STREAM)

        #    b. Update Result is set to “0” (Initial value)
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.UpdateResult))

        # TODO: not supported
        #    c. When the download starts, State is set to “1” (Downloading)
        #    d. A READ (CoAP GET) on /5/0/3 (State) provides the status of the
        #       firmware download to the server. The server may send repeated
        #       READs or OBSERVE the resource to determine when the
        #       download is completed.

        #    e. When the download is completed, State is set to “2” (Downloaded)
        self.assertEqual(b'2', self.test_read(ResPath.FirmwareUpdate.State))

        # 2. Step 2 – Execute
        #    a. When the download is completed, the server initiates a firmware
        #       update by EXECUTE (CoAP POST) /5/0/2 (Update)
        req = Lwm2mExecute(ResPath.FirmwareUpdate.Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        # TODO: not supported
        #    b. State is set to “3” (Updating)
        #       If firmware is successfully updated
        #       b.1 Update Result is set to “1” (Firmware updated successfully)
        #       b.2 The device is automatically rebooting.
        self.serv.reset()
        self.assertDemoRegisters()

        #    c. The server READs Update Result to know the result of the
        #       firmware update procedure.
        self.assertEqual(b'1', self.test_read(ResPath.FirmwareUpdate.UpdateResult))


class FirmwareUpdateWithHttpServer:
    class Test(DataModel.Test):
        FIRMWARE_PATH = '/firmware'

        def get_firmware_uri(self):
            return 'http://127.0.0.1:%d%s' % (self.http_server.server_address[1], self.FIRMWARE_PATH)

        def before_download(self):
            pass

        def setUp(self, firmware_package):
            super().setUp()

            test_case = self
            class FirmwareRequestHandler(http.server.BaseHTTPRequestHandler):
                def do_GET(self):
                    test_case.requests.append(self.path)
                    test_case.before_download()

                    # give the test some time to read "Downloading" state
                    time.sleep(1)

                    self.send_header('Content-type', 'application/octet-stream')
                    self.send_response(http.HTTPStatus.OK)

                    self.wfile.write(firmware_package)

                def log_request(code='-', size='-'):
                    # don't display logs on successful request
                    pass

            self.requests = []
            self.http_server = http.server.HTTPServer(('', 0), FirmwareRequestHandler)

            self.server_thread = threading.Thread(target=lambda: self.http_server.serve_forever())
            self.server_thread.start()


        def tearDown(self):
            try:
                super().tearDown()
            finally:
                self.http_server.shutdown()
                self.server_thread.join()

            # there should be exactly one request
            self.assertEqual([self.FIRMWARE_PATH], self.requests)


class Test771_FirmwareUpdate_SuccessfulFirmwareUpdateViaAlternateMechanism(FirmwareUpdateWithHttpServer.Test):
    def setUp(self):
        demo_executable = os.path.join(self.config.demo_path, self.config.demo_cmd)
        with open(demo_executable, 'rb') as f:
            pkg = make_firmware_package(f.read())

        super().setUp(pkg)

    def runTest(self):
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.State))

        # 1. Step 1 – Write
        #    a. The server delivers the firmware URI to the device through a
        #       WRITE (CoAP PUT/POST) operation on /5/0/1 (Package URI)
        self.test_write(ResPath.FirmwareUpdate.PackageURI, self.get_firmware_uri())

        #    b. Update Result is set to “0” (Initial value)
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.UpdateResult))

        #    c. The Device downloads the firmware from the URI via an
        #       alternative mechanism (not CoAP)
        #    d. When the download starts, State is set to “1” (Downloading)
        #    e. A READ (CoAP GET) on /5/0/3 (State) provides the status of the
        #       firmware download to the server. The server may send repeated
        #       READs or OBSERVE the resource to determine when the
        #       download is completed.
        #    f. When the download is completed, State is set to “2” (Downloaded)
        observed_states = set()
        for _ in range(10):
            state = self.test_read(ResPath.FirmwareUpdate.State)
            observed_states.add(state)

            self.assertIn(state, [b'0', b'1', b'2'])
            if state == b'2':
                break

            time.sleep(1)

        self.assertIn(b'1', observed_states)
        self.assertIn(b'2', observed_states)


class Test772_FirmwareUpdate_ErrorCase_FirmwarePackageNotDownloaded(DataModel.Test):
    def runTest(self):
        # Try to perform a device firmware installation when there is no downloaded
        # firmware package
        # Preconditions:
        #   o Device is registered at the LWM2M server
        #   o Device is switched on and operational
        #   o Firmware Update is available on the Server
        #   o State (/5/0/3) is different from “2” (Downloaded)
        state = self.test_read(ResPath.FirmwareUpdate.State)
        self.assertNotEqual(b'2', state)

        update_result = self.test_read(ResPath.FirmwareUpdate.UpdateResult)

        # An EXECUTE operation from the server on /5/0/2 (Update) is received
        # by the client
        #
        # Normal flow:
        # 1. The client does nothing
        req = Lwm2mExecute(ResPath.FirmwareUpdate.Update)
        self.serv.send(req)
        res = self.serv.recv()

        # Pass Criteria:
        # 1. New firmware is not installed on the device
        # 2. The device is not rebooted
        # 3. State (/5/0/3) does not change
        self.assertEqual(state,
                         self.test_read(ResPath.FirmwareUpdate.State))

        # 4. Update Result (/5/0/5) is not changed
        self.assertEqual(update_result,
                         self.test_read(ResPath.FirmwareUpdate.UpdateResult))

        # 5. The server receives the status code “4.05” for method not allowed
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_METHOD_NOT_ALLOWED),
                            res)


@unittest.skip("TODO")
class Test773_FirmwareUpdate_ErrorCase_NotEnoughStorage(DataModel.Test):
    def runTest(self):
        pass


class Test774_FirmwareUpdate_ErrorCase_OutOfMemory(DataModel.Test):
    def runTest(self):
        # A WRITE operation from the server on /5/0/0 (Package) is received by
        # the Client
        # Normal flow:
        # 1. The server delivers the firmware to the device through a WRITE
        #    (CoAP PUT/POST) operation on /5/0/0 (Package)
        # 2. Update Result is set to “0” (Initial)
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.UpdateResult))

        demo_executable = os.path.join(self.config.demo_path, self.config.demo_cmd)
        with open(demo_executable, 'rb') as f:
            payload = f.read()

        self.test_write_block(ResPath.FirmwareUpdate.Package,
                              make_firmware_package(payload, force_error=FirmwareUpdateForcedError.OutOfMemory),
                              coap.ContentFormat.APPLICATION_OCTET_STREAM)

        # TODO: not supported
        # 3. When the download starts, State is set to “1” (Downloading)
        # 4. A READ (CoAP GET) on /5/0/3 (State) provides the status of the
        #    firmware download to the server. The server may send repeated
        #    READs or OBSERVE the resource to determine when the
        #    download is completed.
        # 5. Before the end of download, the device runs out of memory and
        #    cannot finish the download (State still keeps the value “1”
        #    :Downloading)
        # 6. The client removes what was downloaded

        # 7. /5/0/3 (State) is set to “0” (Idle)
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.State))

        # 8. /5/0/5 (Update Result) is set to “3” (Out of memory during
        #    downloading process)
        # 9. The server READs Update Result to know the result of the
        #    firmware update procedure.
        self.assertEqual(b'3', self.test_read(ResPath.FirmwareUpdate.UpdateResult))


class FirmwareUpdate_ErrorCase_OutOfMemory_PackageURI(FirmwareUpdateWithHttpServer.Test):
    def setUp(self):
        demo_executable = os.path.join(self.config.demo_path, self.config.demo_cmd)
        with open(demo_executable, 'rb') as f:
            pkg = make_firmware_package(f.read(), force_error=FirmwareUpdateForcedError.OutOfMemory)

        super().setUp(pkg)

    def runTest(self):
        # Test 774, but with Package URI
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.UpdateResult))
        self.test_write(ResPath.FirmwareUpdate.PackageURI, self.get_firmware_uri())

        observed_values = set()
        for _ in range(100):
            curr_val = self.test_read(ResPath.FirmwareUpdate.State)
            observed_values.add(curr_val)
            if curr_val == b'0':
                break
            time.sleep(0.1)

        self.assertEqual(set([b'0', b'1']), observed_values)
        self.assertEqual(b'3', self.test_read(ResPath.FirmwareUpdate.UpdateResult))


@unittest.skip("TODO")
class Test775_FirmwareUpdate_ErrorCase_ConnectionLostDuringDownloadPackageURI(DataModel.Test):
    def runTest(self):
        pass


class Test776_FirmwareUpdate_ErrorCase_CRCCheckFail(DataModel.Test):
    def runTest(self):
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.State))

        # 1. The server delivers the firmware to the device through a WRITE
        #    (CoAP PUT/POST) operation on /5/0/0 (Package)
        # 2. Update Result is set to “0” (Initial)

        demo_executable = os.path.join(self.config.demo_path, self.config.demo_cmd)
        with open(demo_executable, 'rb') as f:
            payload = f.read()

        self.test_write_block(ResPath.FirmwareUpdate.Package,
                              make_firmware_package(payload, crc=0),
                              coap.ContentFormat.APPLICATION_OCTET_STREAM)

        # TODO: not supported
        # 3. When the download starts, State is set to “1” (Downloading)
        # 4. A READ (CoAP GET) on /5/0/3 (State) provides the status of the
        #    firmware download to the server. The server may send repeated
        #    READs or OBSERVE the resource to determine when the
        #    download is completed.

        # 5. The package integrity check fails
        # 6. The client removes what was downloaded
        # 7. /5/0/3 (State) is set to “0” (Initial)
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.State))

        # 8. /5/0/5 (Update Result) is set to “5” (CRC check failure for new
        #    downloaded package)
        # 9. The server READs Update Result to know the result of the
        #    firmware update procedure.
        self.assertEqual(b'5', self.test_read(ResPath.FirmwareUpdate.UpdateResult))


class FirmwareUpdate_ErrorCase_CRCCheckFail_PackageURI(FirmwareUpdateWithHttpServer.Test):
    def setUp(self):
        demo_executable = os.path.join(self.config.demo_path, self.config.demo_cmd)
        with open(demo_executable, 'rb') as f:
            pkg = make_firmware_package(f.read(), crc=0)

        super().setUp(pkg)

    def runTest(self):
        # Test 776, but with Package URI
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.UpdateResult))
        self.test_write(ResPath.FirmwareUpdate.PackageURI, self.get_firmware_uri())

        observed_values = set()
        for _ in range(100):
            curr_val = self.test_read(ResPath.FirmwareUpdate.State)
            observed_values.add(curr_val)
            if curr_val == b'0':
                break
            time.sleep(0.1)

        self.assertEqual(set([b'0', b'1']), observed_values)
        self.assertEqual(b'5', self.test_read(ResPath.FirmwareUpdate.UpdateResult))


class Test777_FirmwareUpdate_ErrorCase_UnsupportedPackageType(DataModel.Test):
    def runTest(self):
        # A WRITE operation from the server on /5/0/0 (Package) with a package
        # with an unsupported type is received by the client
        # Normal flow:
        # 1. The server delivers the firmware package to the device through a
        #    WRITE (CoAP PUT/POST) operation on /5/0/0 (Package)
        # 2. Update Result is set to “0” (Initial)
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.UpdateResult))

        self.test_write(ResPath.FirmwareUpdate.Package, b'A' * 1024,
                        format=coap.ContentFormat.APPLICATION_OCTET_STREAM)

        # TODO: not supported
        # 3. When the download starts, State is set to “1” (Downloading)
        # 4. A READ (CoAP GET) on /5/0/3 (State) provides the status of the
        #    firmware download to the server. The server may send repeated
        #    READs or OBSERVE the resource to determine when the
        #    download is completed.
        # 5. The package type is not supported
        # 6. The client removes what was downloaded

        # 7. /5/0/3 (State) is set to “0” (Idle)
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.State))

        # 8. /5/0/5 (Update Result) is set to “6” (Unsupported package type)
        # 9. The server READs Update Result to know the result of the
        #    firmware update procedure.
        self.assertEqual(b'6', self.test_read(ResPath.FirmwareUpdate.UpdateResult))


class FirmwareUpdate_ErrorCase_UnsupportedPackageType_PackageURI(FirmwareUpdateWithHttpServer.Test):
    def setUp(self):
        super().setUp(b'A' * 1024)

    def runTest(self):
        # Test 777, but with Package URI
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.UpdateResult))
        self.test_write(ResPath.FirmwareUpdate.PackageURI, self.get_firmware_uri())

        observed_values = set()
        for _ in range(100):
            curr_val = self.test_read(ResPath.FirmwareUpdate.State)
            observed_values.add(curr_val)
            if curr_val == b'0':
                break
            time.sleep(0.1)

        self.assertEqual(set([b'0', b'1']), observed_values)
        self.assertEqual(b'6', self.test_read(ResPath.FirmwareUpdate.UpdateResult))


@unittest.skip("TODO")
class Test778_FirmwareUpdate_ErrorCase_InvalidURI(DataModel.Test):
    def runTest(self):
        pass


class Test779_FirmwareUpdate_ErrorCase_UnsuccessfulFirmwareUpdate(DataModel.Test):
    def runTest(self):
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.State))

        # 1. Step 1 – Write
        #    a. The server delivers the firmware to the device through a WRITE
        #       (CoAP PUT/POST) operation on /5/0/0 (Package)
        #    b. Update Result is set to “0” (Initial value)
        demo_executable = os.path.join(self.config.demo_path, self.config.demo_cmd)
        with open(demo_executable, 'rb') as f:
            payload = f.read()

        self.test_write_block(ResPath.FirmwareUpdate.Package,
                              make_firmware_package(payload, force_error=FirmwareUpdateForcedError.FailedUpdate),
                              coap.ContentFormat.APPLICATION_OCTET_STREAM)

        # TODO: not supported
        #    c. When the download starts, State is set to “1” (Downloading)
        #    d. A READ (CoAP GET) on /5/0/3 (State) provides the status of the
        #       firmware download to the server. The server may send repeated
        #       READs or OBSERVE the resource to determine when the
        #       download is completed.

        #    e. When the download is completed, State is set to “2”
        #       (Downloaded)
        self.assertEqual(b'2', self.test_read(ResPath.FirmwareUpdate.State))

        # 2. Step 2 – Execute
        #    a. When the download is completed, the server initiates a firmware
        #       update by EXECUTE (CoAP POST) on /5/0/2 (Update)
        #       Resource
        req = Lwm2mExecute(ResPath.FirmwareUpdate.Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # TODO: Updating -> Downloaded transition happens too fast
        #    b. State is set to “3” (Updating)
        # self.assertEqual(b'3', self.test_read(ResPath.FirmwareUpdate.State))

        #    c. Firmware updates fails
        #       c.1 Update Result is set to “8” (Firmware update failed )
        #       c.2 The device is not rebooted
        #       c.3 State is set back to “2” (Downloaded)
        #    d. The server READs Update Result to know the result of the
        #       firmware update procedure
        self.assertEqual(b'2', self.test_read(ResPath.FirmwareUpdate.State))
        self.assertEqual(b'8', self.test_read(ResPath.FirmwareUpdate.UpdateResult))


class FirmwareUpdate_ErrorCase_UnsuccessfulFirmwareUpdate_PackageURI(FirmwareUpdateWithHttpServer.Test):
    def setUp(self):
        demo_executable = os.path.join(self.config.demo_path, self.config.demo_cmd)
        with open(demo_executable, 'rb') as f:
            pkg = make_firmware_package(f.read(), force_error=FirmwareUpdateForcedError.FailedUpdate)

        super().setUp(pkg)

    def runTest(self):
        # Test 777, but with Package URI
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.UpdateResult))
        self.test_write(ResPath.FirmwareUpdate.PackageURI, self.get_firmware_uri())

        observed_values = set()
        for _ in range(100):
            curr_val = self.test_read(ResPath.FirmwareUpdate.State)
            observed_values.add(curr_val)
            if curr_val == b'2':
                break
            time.sleep(0.1)

        self.assertEqual(set([b'1', b'2']), observed_values)

        req = Lwm2mExecute(ResPath.FirmwareUpdate.Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        self.assertEqual(b'2', self.test_read(ResPath.FirmwareUpdate.State))
        self.assertEqual(b'8', self.test_read(ResPath.FirmwareUpdate.UpdateResult))
