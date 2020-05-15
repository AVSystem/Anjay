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

import http
import os
import threading
import time
import unittest
import contextlib
import resource

from framework.lwm2m_test import *
from .utils import DataModel, ValueValidator as VV


class FirmwareUpdate:
    class Test(DataModel.Test):
        def collect_values(self, path: Lwm2mPath, final_value, max_iterations=100, step_time=0.1):
            observed_values = []
            for _ in range(max_iterations):
                state = self.test_read(path)
                observed_values.append(state)
                if state == final_value:
                    break
                time.sleep(step_time)
            return observed_values

        def setUp(self, extra_cmdline_args=[]):
            self.ANJAY_MARKER_FILE = generate_temp_filename(dir='/tmp', prefix='anjay-fw-updated-')
            super().setUp(fw_updated_marker_path=self.ANJAY_MARKER_FILE, extra_cmdline_args=extra_cmdline_args)

        def tearDown(self):
            # reset the state machine
            # Write /5/0/1 (Firmware URI)
            req = Lwm2mWrite(ResPath.FirmwareUpdate.PackageURI, '')
            self.serv.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())
            super().tearDown()

    class TestWithCoapServer(Test):
        def setUp(self, coap_server=None, extra_cmdline_args=[]):
            super().setUp(extra_cmdline_args=extra_cmdline_args)

            from framework.coap_file_server import CoapFileServerThread
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


class FirmwareUpdateWithHttpServer:
    class Test(FirmwareUpdate.Test):
        FIRMWARE_PATH = '/firmware'
        HTTP_SERVER_CLASS = http.server.HTTPServer

        def get_firmware_uri(self):
            return 'http://127.0.0.1:%d%s' % (self.http_server.server_address[1], self.FIRMWARE_PATH)

        def before_download(self):
            pass

        def during_download(self, request_handler):
            pass

        def setUp(self, firmware_package):
            super().setUp()

            test_case = self

            class FirmwareRequestHandler(http.server.BaseHTTPRequestHandler):
                def do_GET(self):
                    test_case.requests.append(self.path)
                    test_case.before_download()

                    self.send_response(http.HTTPStatus.OK)
                    self.send_header('Content-type', 'application/octet-stream')
                    self.send_header('Content-length', len(firmware_package))
                    self.end_headers()

                    # give the test some time to read "Downloading" state
                    time.sleep(1)

                    test_case.during_download(self)
                    try:
                        self.wfile.write(firmware_package)
                    except BrokenPipeError:
                        pass

                def log_request(code='-', size='-'):
                    # don't display logs on successful request
                    pass

            self.requests = []
            self.http_server = self.HTTP_SERVER_CLASS(('', 0), FirmwareRequestHandler)

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


class Test751_FirmwareUpdate_QueryingTheReadableResources(FirmwareUpdate.Test):
    def runTest(self):
        # 1. READ (CoAP GET) operation is performed on the Firmware Update
        #    Object Instance
        #
        # A. In test step 1, the Server receives the status code "2.05" for
        #    READ operation success
        # B. In test step 1, the returned values regarding State (ID:3) and
        #    Update Result (ID:5) prove the Client FW update Capability is in
        #    initial state (State=Idle & Update Result= Initial Value).
        # C. In test step 1, the returned values regarding Firmware Update
        #    Protocol Support (ID:8) & Firmware Update Delivery Method
        #    (ID:9) allow to determine the supported characteristics of the
        #    Client FW Update Capability.
        self.test_read('/%d/0' % OID.FirmwareUpdate,
                       VV.tlv_instance(
                           resource_validators={
                               RID.FirmwareUpdate.State:                         VV.from_raw_int(0),
                               RID.FirmwareUpdate.UpdateResult:                  VV.from_raw_int(0),
                               RID.FirmwareUpdate.FirmwareUpdateProtocolSupport: VV.multiple_resource(VV.from_values(b'\x00', b'\x01', b'\x02', b'\x03', b'\x04', b'\x05')),
                               RID.FirmwareUpdate.FirmwareUpdateDeliveryMethod:  VV.from_raw_int(2),
                           },
                           ignore_extra=True))


class Test755_FirmwareUpdate_SettingTheWritableResourcePackage(FirmwareUpdate.Test):
    def runTest(self):
        # 1. A WRITE (CoAP PUT) operation with a NULL value ('\0') is
        #    performed by the Server on the Package Resource (ID:0) of the
        #    FW Update Object Instance
        #
        # A. In test step 1, the Server receives the success message "2.04"
        #    associated with the WRITE operation
        self.test_write(ResPath.FirmwareUpdate.Package, b'\0',
                        format=coap.ContentFormat.APPLICATION_OCTET_STREAM)

        # 2. The Server READs (CoAP GET) the FW Object Instance to get
        #    the values of the State (ID:3) and Update Result (ID:5) Resources
        #
        # B. In test step 2, the Server receives the success message "2.05" along
        #    with the value of State and Update Result Resources values.
        # C. In test step 2, the queried State and Update Result Resources values
        #    are both 0 (Idle / Initial value): FW Update Object Instance is in the
        #    Initial state.
        self.test_read('/%d/0' % OID.FirmwareUpdate,
                       VV.tlv_instance(
                           resource_validators={
                               RID.FirmwareUpdate.State:        VV.from_raw_int(0),
                               RID.FirmwareUpdate.UpdateResult: VV.from_raw_int(0),
                           },
                           ignore_extra=True))

        # 3. A WRITE (CoAP PUT) operation with a valid image is
        #    performed by the Server on the Package Resource (ID:0) of the
        #    FW Update Object Instance
        #
        # D. In test step 3, the Server receives the success message "2.04"
        #    associated with the WRITE request for loading the firmware image.
        self.test_write(ResPath.FirmwareUpdate.Package,
                        make_firmware_package(b''),
                        format=coap.ContentFormat.APPLICATION_OCTET_STREAM)

        # 4. The Server READs (CoAP GET) the FW Object Instance to get
        #    the values of the State (ID:3) and Update Result (ID:5) Resources
        #
        # E. In test step 4, the Server receives the success message "2.05" along
        #    with the State and Update Result Resources values.
        # F. In test step 4, the queried value of State resource is 2 (Downloaded)
        #    and the value of Update Result value is still 0 (Initial Value)
        self.test_read('/%d/0' % OID.FirmwareUpdate,
                       VV.tlv_instance(
                           resource_validators={
                               RID.FirmwareUpdate.State:        VV.from_raw_int(2),
                               RID.FirmwareUpdate.UpdateResult: VV.from_raw_int(0),
                           },
                           ignore_extra=True))


class Test756_FirmwareUpdate_SettingTheWritableResourcePackageURI(FirmwareUpdateWithHttpServer.Test):
    def setUp(self):
        super().setUp(make_firmware_package(b''))

    def runTest(self):
        # 1. A WRITE (CoAP PUT) operation with an empty string value is
        #    performed by the Server on the Package Resource (ID:0) of the FW
        #    Update Object Instance
        #
        # A. In test step 1, the Server receives the success message "2.04"
        #    associated with the WRITE operation
        self.test_write(ResPath.FirmwareUpdate.PackageURI, b'')

        # 2. The Server READs (CoAP GET) the FW Object Instance to get the
        #    values of the State (ID:3) and Update Result (ID:5) Resources
        #
        # B. In test step 2, the Server receives the success message "2.05" along
        #    with the value of State and Update Result Resources values.
        # C. In test step 2, the queried State and Update Result Resources values
        #    are both 0 (Idle / Initial value): FW Update Object Instance is in the
        #    Initial state.
        self.test_read('/%d/0' % OID.FirmwareUpdate,
                       VV.tlv_instance(
                           resource_validators={
                               RID.FirmwareUpdate.State:        VV.from_raw_int(0),
                               RID.FirmwareUpdate.UpdateResult: VV.from_raw_int(0),
                           },
                           ignore_extra=True))

        # 3. A WRITE (CoAP PUT) operation with a valid image is performed by
        #    the Server on the Package Resource (ID:0) of the FW Update Object
        #    Instance
        #
        # D. In test step 3, the Server receives the success message "2.04"
        #    associated with the WRITE request for the loadded image.
        self.test_write(ResPath.FirmwareUpdate.PackageURI,
                        self.get_firmware_uri())

        # give the client some time to download firmware
        time.sleep(3)

        # 4. The Server READs (CoAP GET) the FW Object Instance to get the
        #    values of the State (ID:3) and Update Result (ID:5) Resources
        #
        # E. In test step 4, the Server receives the success message "2.05" along
        #    with the State and Update Result Resources values.
        # F. In test step 4, the queried value of State resource is 2 (Downloaded)
        #    and the value of Update Result value is still 0 (Initial Value)
        self.test_read('/%d/0' % OID.FirmwareUpdate,
                       VV.tlv_instance(
                           resource_validators={
                               RID.FirmwareUpdate.State:        VV.from_raw_int(2),
                               RID.FirmwareUpdate.UpdateResult: VV.from_raw_int(0),
                           },
                           ignore_extra=True))


class Test760_FirmwareUpdate_BasicObservationAndNotificationOnFirmwareUpdateObjectResources(FirmwareUpdate.Test):
    def runTest(self):
        # 1. The Server communicates to the Client pmin=2 and pmax=10
        #    periods with a WRITE-ATTRIBUTE (CoAP PUT) operation at
        #    the FW Update Object Instance level.
        #
        # A. In test step 1, the Server receives the success message "2.04" associated
        #    with the WRITE-ATTRIBUTE operation.
        self.test_write_attributes('/%d/0' % OID.FirmwareUpdate,
                                   pmin=2, pmax=10)

        # 2. The Server Sends OBSERVE (CoAP Observe Option) message
        #    to activate reporting on the State Resource (/5/0/3) of the FW
        #    Update Object Instance.
        #
        # B. In test step 2, the Server receives the success message "2.05" associated
        #    with the OBSERVE operation, along with the value of State =Idle
        req = Lwm2mObserve(ResPath.FirmwareUpdate.State)
        self.serv.send(req)
        res = self.serv.recv()
        self.assertMsgEqual(Lwm2mContent.matching(req)(content=b'0'), res)

        # 3. The Server delivers the firmware to the Client through a WRITE
        #    (CoAP PUT) operation on the Package Resource (/5/0/0)
        #
        # C. In test step 3, the Server receives the success message "2.04" associated
        #    with the WRITE operation delivering the firmaware image.
        self.test_write(ResPath.FirmwareUpdate.Package,
                        make_firmware_package(b''),
                        format=coap.ContentFormat.APPLICATION_OCTET_STREAM)

        # 4. The Client reports requested information with a NOTIFY
        #    message (CoAP response)
        #
        # D. In test step 4, the State Resource value returned by the Client in NOTIFY
        #    message is set to "Downloaded"
        req = Lwm2mObserve(ResPath.FirmwareUpdate.State)
        self.serv.send(req)
        res = self.serv.recv(timeout_s=3)
        self.assertMsgEqual(Lwm2mContent.matching(req)(content=b'2'), res)


class Test770_FirmwareUpdate_SuccessfulFirmwareUpdateViaCoAP(FirmwareUpdate.Test):
    def runTest(self):
        demo_executable = os.path.join(self.config.demo_path, self.config.demo_cmd)
        with open(demo_executable, 'rb') as f:
            payload = f.read()

        prev_version = self.test_read(ResPath.Device.FirmwareVersion)

        # 1. Step 1 – Package Delivery
        #    a. The Server places the Client in the initial state of the FW Update
        #       process : A WRITE (CoAP PUT) operation with a NULL value
        #       (‘\0’) is performed by the Server on the Package Resource
        #       (ID:0) of the FW Update Object Instance
        #
        # A. Step 1 – Package Delivery
        #    a. In the test step 1.a, the Server receives the status code "2.04" for
        #       the WRITE success setting the Client in the FW update initial
        #       state.
        #    d. Update Result is "0" (Initial Value) during the whole step
        self.test_write(ResPath.FirmwareUpdate.Package, b'\0',
                        coap.ContentFormat.APPLICATION_OCTET_STREAM)

        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.State))

        # 1. Step 1 – Package Delivery
        #    b. The Server delivers the firmware to the Client through a WRITE
        #       (CoAP PUT) operation on the Package Resource (/5/0/0)
        #
        # A. Step 1 – Package Delivery
        #    b. In the test step 1.b, The Server receives success message with
        #       either a "2.31" status code (Continue) or a final "2.04" status
        #       code.
        self.test_write_block(ResPath.FirmwareUpdate.Package,
                              make_firmware_package(payload),
                              coap.ContentFormat.APPLICATION_OCTET_STREAM)

        # 1. Step 1 – Package Delivery
        #    c. Polling (READ command) or Notification on Update Result
        #       and State Resources is performed, up to the time State Resource
        #       takes the ‘Downloaded’ value (2)
        #
        # A. Step 1 – Package Delivery
        #    c. In the test step 1.c State Resource can take the value "1"
        #       (Downloading) during this sub-step and will take the value "2" at
        #       the end (Downloaded)
        #    d. Update Result is "0" (Initial Value) during the whole step
        self.assertEqual(b'2', self.test_read(ResPath.FirmwareUpdate.State))
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.UpdateResult))

        # 2. Step 2 – Firmware Update
        #    a. When the download is completed (State Resource value is ‘2’
        #       Downloaded) , the Server initiates a firmware update by
        #       triggering EXECUTE command on Update Resource (CoAP
        #       POST /5/0/2)
        #
        # B. Step 2 – Firmware Update
        #    a. In test step 2.a, the Server receives a success message "2.04"
        #       (Changed) in response to the EXECUTE command
        self.test_execute(ResPath.FirmwareUpdate.Update)
        # not supported: Updating state only observable via Observe
        # self.assertEqual(b'3', self.test_read(ResPath.FirmwareUpdate.State))

        # 2. Step 2 – Firmware Update
        #    b. Polling (READ command) or Notification on Update Result and
        #       State Resources is performed, up to the time State Resource is
        #       turned back to Idle value (0) or Update Result Resource contains
        #       an other value than the Initial one (0)
        #
        # B. Step 2 – Firmware Update
        #    b. In test step 2.b, the Server receives success message(s) "2.05"
        #       Contents along with a State Resource value of "3" (Updating)
        #       or "0" (Idle) and an Update Ressource value of "0" (Initial
        #       Value) or "1" (Firmware updated successfully)
        self.serv.reset()
        self.assertDemoRegisters()

        # 3. Step 3 – Process verification
        #    a. The Server READs Update Result ("/5/0/5") and State ("/5/0/3")
        #       Resources to know the result of the firmware update procedure.
        #
        # C. Step 3 – Process verification
        #    a. In test step 3.a, the Server receives success message(s) "2.05"
        #       Content" along with a State Resource value of "0" (Idle) and an
        #       Update Ressource value of "1" (Firmware updated successfully)
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.State))
        self.assertEqual(b'1', self.test_read(ResPath.FirmwareUpdate.UpdateResult))

        # 3. Step 3 – Process verification
        #    b. The Server READs the Resource "Firmware Update" from the
        #       Object Device Instance ("/3/0/3")
        #
        # C. Step 3 – Process verification
        #    b. In test step 3.b, the Server receives success message "2.05"
        #       Content" along with the expected value of the Resource
        #       Firmware Version from the Object Device Instance
        #
        # TODO: we currently update firmware with an identical executable,
        # so the version does not change
        self.assertEqual(prev_version, self.test_read(ResPath.Device.FirmwareVersion))


class Test771_FirmwareUpdate_SuccessfulFirmwareUpdateViaAlternateMechanism(FirmwareUpdateWithHttpServer.Test):
    def setUp(self):
        demo_executable = os.path.join(self.config.demo_path, self.config.demo_cmd)
        with open(demo_executable, 'rb') as f:
            pkg = make_firmware_package(f.read())

        super().setUp(pkg)

    def runTest(self):
        # In this test the package version stays the same after update
        prev_version = self.test_read(ResPath.Device.FirmwareVersion)

        # 1. Step 1 – Package Delivery
        #    a. The Server places the Client in the initial state of the FW
        #       Update process : A WRITE (CoAP PUT) operation with an
        #       empty string value is performed by the Server on the Package
        #       URI Resource (ID:1) of the FW Update Object Instance
        #
        # A. Step 1 – Package Delivery
        #    a. In the test step 1.a, the Server receives the status code "2.04"
        #       for the WRITE success setting the Client in the FW update
        #       initial state.
        #    e. Update Result is "0" (Initial Value) during the whole test
        #       step 1
        self.test_write(ResPath.FirmwareUpdate.PackageURI, '')
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.UpdateResult))

        # 1. Step 1 – Package Delivery
        #    b. The Server delivers the Package URI to the Client through a
        #       WRITE (CoAP PUT) operation on the Package URI Resource
        #       (/5/0/1)
        #
        # A. Step 1 – Package Delivery
        #    b. In the test step 1.b, the Server receives the status code "2.04"
        #       for the WRITE success setting the Package URI Client in the
        #       FW update Object Instance
        self.test_write(ResPath.FirmwareUpdate.PackageURI,
                        self.get_firmware_uri())
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.UpdateResult))

        # 1. Step 1 – Package Delivery
        #    c. The Client downloads the firmware from the provided URI via
        #       an alternative mechanism (not CoAP)
        #    d. Polling ( successive READ commands) or Notification on
        #       Update Result and State Resources is performed, up to the time
        #       State Resource takes the ‘Downloaded’ value (2)
        #
        # A. Step 1 – Package Delivery
        #    c. In the test step 1.c, The Server receives success message
        #       with either a "2.31" status code (Continue) or a final "2.04"
        #       status code.
        #    d. In the test step 1.d State Resource can take the value "1"
        #       (Downloading) during this sub-step and will take the value
        #       "2" at the end (Downloaded)
        #    e. Update Result is "0" (Initial Value) during the whole test
        #       step 1
        observed_states = self.collect_values(ResPath.FirmwareUpdate.State, b'2')
        self.assertEqual(b'2', observed_states[-1])
        self.assertIn(set(observed_states), [{b'0', b'1', b'2'}, {b'1', b'2'}])

        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.UpdateResult))

        # 2. Step 2 – Firmware Update
        #    a. When the download is completed (State Resource value is ‘2’
        #       Downloaded) , the Server initiates a firmware update by
        #       triggering EXECUTE command on Update Resource (CoAP
        #       POST /5/0/2 )
        #
        # B. Step 2 – Firmware Update
        #    a. In test step 2.a, the Server receives a success message "2.04"
        #       (Changed) in response to the EXECUTE command
        self.test_execute(ResPath.FirmwareUpdate.Update)
        # not supported: Updating state only observable via Observe
        # self.assertEqual(b'3', self.test_read(ResPath.FirmwareUpdate.State))

        # 2. Step 2 – Firmware Update
        #    b. Polling (READ command) or Notification on Update Result and
        #       State Resources is performed, up to the time State Resource is
        #       turned back to Idle value (0) or Update Result Resource contains
        #       an other value than the Initial one (0)
        #
        # B. Step 2 – Firmware Update
        #    b. In test step 2.b, the Server receives success message(s) "2.05"
        #       Contents along with a State Resource value of "3" (Updating)
        #       or "0" (Idle) and an Update Ressource value of "0" (Initial
        #       Value) or "1" (Firmware updated successfully)
        self.serv.reset()
        self.assertDemoRegisters()

        # 3. Step 3 – Process verification
        #    a. The Server READs Update Result ("/5/0/5") and State ("/5/0/3")
        #       Resources to know the result of the firmware update procedure.
        #
        # C. Step 3 – Process verification
        #    a. In test step 3.a, the Server receives success message(s) "2.05"
        #       Content" along with a State Resource value of "0" (Idle) and an
        #       Update Ressource value of "1" (Firmware updated successfully)
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.State))
        self.assertEqual(b'1', self.test_read(ResPath.FirmwareUpdate.UpdateResult))

        # 3. Step 3 – Process verification
        #    b. The Server READs the Resource "Firmware Update" from the
        #       Object Device Instance ("/3/0/3")
        #
        # C. Step 3 – Process verification
        #    b. In test step 3.b, the Server receives success message "2.05"
        #       Content" along with the expected value of the Resource
        #       Firmware Version from the Object Device Instance
        #
        # TODO: we currently update firmware with an identical executable,
        # so the version does not change
        self.assertEqual(prev_version, self.test_read(ResPath.Device.FirmwareVersion))


class Test772_FirmwareUpdate_ErrorCase_FirmwarePackageNotDownloaded(FirmwareUpdate.Test):
    def runTest(self):
        # 1. The Server send a READ operation (CoAP GET /5/0) to the Client on the
        #    FW Update Object Instance to obtain the values of the State and Update
        #    Resources.
        #
        # A. In test step 1, the Server receives a success message ("2.05" Content)
        #    associated to its READ command along with a State Resource value
        #    which is not "2" (Downloaded) and a valid (0..9) Update Resource
        #    value
        state = self.test_read(ResPath.FirmwareUpdate.State)
        self.assertNotEqual(b'2', state)

        update_result = self.test_read(ResPath.FirmwareUpdate.UpdateResult)
        self.assertIn(int(update_result.decode('ascii')), range(10))

        # 2. the Client receives an EXECUTE operation on the Update Resource
        #    (CoAP POST /5/0/2 ) of the FW Update Object Instance
        #
        # B. In test step 2, the Server receives the status code "4.05" for method
        #    not allowed associated to its EXECUTE command
        req = Lwm2mExecute(ResPath.FirmwareUpdate.Update)
        self.serv.send(req)
        res = self.serv.recv()
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_METHOD_NOT_ALLOWED),
                            res)

        # 3. The Server send a READ operation again (CoAP GET /5/0/3) to the Client
        #    on the FW Update Object Instance to obtain the State and the Update
        #    Resource values
        #
        # C. In test step 3, the Server receives a success message ("2.05" Content)
        #    associated to its READ command along with a State Resource value
        #    and an Update Result Resource value, identical to the ones retrieved
        #    in Pass-Criteria A. The firmware has not bee installed.
        self.assertEqual(state, self.test_read(ResPath.FirmwareUpdate.State))
        self.assertEqual(update_result, self.test_read(ResPath.FirmwareUpdate.UpdateResult))


class Test773_FirmwareUpdate_ErrorCase_NotEnoughStorage_FirmwareURI(FirmwareUpdateWithHttpServer.Test):
    def setUp(self):
        @contextlib.contextmanager
        def temporary_soft_fsize_limit(limit_bytes):
            prev_limit = resource.getrlimit(resource.RLIMIT_FSIZE)

            try:
                resource.setrlimit(resource.RLIMIT_FSIZE, (limit_bytes, prev_limit[1]))
                yield
            finally:
                resource.setrlimit(resource.RLIMIT_FSIZE, prev_limit)

        demo_executable = os.path.join(self.config.demo_path, self.config.demo_cmd)
        with open(demo_executable, 'rb') as f:
            payload = f.read()

        # Limit file size for demo so that full firmware is too much.
        # After demo starts, we can safely restore original limit, as
        # the client already inherited smaller one.
        with temporary_soft_fsize_limit(len(payload) // 2):
            super().setUp(payload)

    def runTest(self):
        # 1. The Server verifies through a READ (CoAP GET) command on
        #    /5/0/3 (State) the FW Update Object Instance of the Client is in
        #    Idle State
        #
        # A. In test step 1, the Server receives the status code "2.05 " (Content) for
        #    the READ success command, along with the State Resource value of
        #    "0" (Idle)
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.State))

        # 2. The Server delivers the firmware package to the Client either
        #    through a WRITE (CoAP PUT) operation in the Package
        #    Resource (/5/0/0) or through a WRITE operation of an URI in the
        #    Package URI Resource.
        #
        # B. In test step 2., the Server receives the status code "2.04" (Changed)
        #    for the WRITE command setting either the Package URI Resource or
        #    setting the Package Resource, according to the chosen firmware
        #    delivery method.
        self.test_write(ResPath.FirmwareUpdate.PackageURI,
                        self.get_firmware_uri())

        # 3. The firmware downloading process is runing The Server sends
        #    repeated READs or OBSERVE on State and Update Result
        #    Resources (CoAP GET /5/0) of the FW Update Object Instance
        #    to determine when the download is completed or if an error
        #    occured.Before the end of download, the device runs out of
        #    storage and cannot finish the download
        # 4. When the Package delivery is stopped the server READs Update
        #    Result to know the result of the firmware update procedure.
        #
        # C. In test step 3., the State Resource retrieved with a value of "1" from
        #    successive Server READs or Client NOTIFY messages, indicates the
        #    download stage of the Package Delivery is engaged
        # D. In test step 3., the Update Result Resource (/5/0/5) retrieved from
        #    successive Server READs or Client NOTIFY messages will take the
        #    value "2" indicating an error occurred during the downloading
        #    process related to shortage of storage memory The State Resource
        #    value never reaches the Downloaded value ("2")
        # E. In test step 4., the success READ message(s) (status code "2.05"
        #    Content) on State Resource with value "0" (Idle) and Update Result
        #    Resource with value "2" indicates the firmware Package Delivery
        observed_values = self.collect_values(ResPath.FirmwareUpdate.State, b'0')
        self.assertEqual(b'0', observed_values[-1])
        self.assertEqual({b'0', b'1'}, set(observed_values))
        self.assertEqual(b'2', self.test_read(ResPath.FirmwareUpdate.UpdateResult))


class Test773_FirmwareUpdate_ErrorCase_NotEnoughStorage_FirmwareURI_CoAP(FirmwareUpdate.TestWithCoapServer):
    def setUp(self):
        # limit file size to 100K; enough for persistence file, not
        # enough for firmware
        import resource
        self.prev_limit = resource.getrlimit(resource.RLIMIT_FSIZE)
        new_limit_b = 100 * 1024
        resource.setrlimit(resource.RLIMIT_FSIZE, (new_limit_b, self.prev_limit[1]))

        demo_executable = os.path.join(self.config.demo_path, self.config.demo_cmd)
        with open(demo_executable, 'rb') as f:
            self._payload = f.read()

        super().setUp()

    def tearDown(self):
        import resource
        resource.setrlimit(resource.RLIMIT_FSIZE, self.prev_limit)

        super().tearDown()

    def runTest(self):
        with self.file_server as file_server:
            file_server.set_resource('/firmware', self._payload)
            uri = file_server.get_resource_uri('/firmware')

        # 1. The Server verifies through a READ (CoAP GET) command on
        #    /5/0/3 (State) the FW Update Object Instance of the Client is in
        #    Idle State
        #
        # A. In test step 1, the Server receives the status code "2.05 " (Content) for
        #    the READ success command, along with the State Resource value of
        #    "0" (Idle)
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.State))

        # 2. The Server delivers the firmware package to the Client either
        #    through a WRITE (CoAP PUT) operation in the Package
        #    Resource (/5/0/0) or through a WRITE operation of an URI in the
        #    Package URI Resource.
        #
        # B. In test step 2., the Server receives the status code "2.04" (Changed)
        #    for the WRITE command setting either the Package URI Resource or
        #    setting the Package Resource, according to the chosen firmware
        #    delivery method.
        self.test_write(ResPath.FirmwareUpdate.PackageURI, uri)

        # 3. The firmware downloading process is runing The Server sends
        #    repeated READs or OBSERVE on State and Update Result
        #    Resources (CoAP GET /5/0) of the FW Update Object Instance
        #    to determine when the download is completed or if an error
        #    occured.Before the end of download, the device runs out of
        #    storage and cannot finish the download
        # 4. When the Package delivery is stopped the server READs Update
        #    Result to know the result of the firmware update procedure.
        #
        # C. In test step 3., the State Resource retrieved with a value of "1" from
        #    successive Server READs or Client NOTIFY messages, indicates the
        #    download stage of the Package Delivery is engaged
        # D. In test step 3., the Update Result Resource (/5/0/5) retrieved from
        #    successive Server READs or Client NOTIFY messages will take the
        #    value "2" indicating an error occurred during the downloading
        #    process related to shortage of storage memory The State Resource
        #    value never reaches the Downloaded value ("2")
        # E. In test step 4., the success READ message(s) (status code "2.05"
        #    Content) on State Resource with value "0" (Idle) and Update Result
        #    Resource with value "2" indicates the firmware Package Delivery
        observed_values = self.collect_values(ResPath.FirmwareUpdate.State, b'0')
        self.assertEqual(b'0', observed_values[-1])
        self.assertEqual({b'0', b'1'}, set(observed_values))
        self.assertEqual(b'2', self.test_read(ResPath.FirmwareUpdate.UpdateResult))


class Test774_FirmwareUpdate_ErrorCase_OutOfMemory(FirmwareUpdate.Test):
    def runTest(self):
        demo_executable = os.path.join(self.config.demo_path, self.config.demo_cmd)
        with open(demo_executable, 'rb') as f:
            payload = f.read()

        # 1. The Server verifies through a READ (CoAP GET) command on
        #    /5/0/3 (State) the FW Update Object Instance of the Client is in Idle
        #    State
        #
        # A. In test step 1, the Server receives the status code "2.05" (Content) for
        #    the READ success command, along with the State Resource value of
        #    "0" (Idle)
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.State))

        # 2. The Server delivers the firmware package to the Client either through
        #    a WRITE (CoAP PUT) operation in the Package Resource (/5/0/0) or
        #    through a WRITE operation of an URI in the Package URI Resource.
        #
        # B. In test step 2., the Server receives the status code "2.04" (Changed)
        #    for the WRITE command setting either the Package URI Resource or
        #    setting the Package Resource, according to the chosen firmware
        #    delivery method.
        self.test_write_block(ResPath.FirmwareUpdate.Package,
                              make_firmware_package(payload, force_error=FirmwareUpdateForcedError.OutOfMemory),
                              coap.ContentFormat.APPLICATION_OCTET_STREAM)

        # 3. The firmware download process is runing The Server sends repeated
        #    READs or OBSERVE on State and Update Result Resources (CoAP
        #    GET /5/0) of the FW Update Object Instance to determine when the
        #    download is completed or if an error occured.Before the end of
        #    download, the Client runs out of RAM and cannot finish the
        #    download
        # 4. When the Package delivery is stopped the server READs Update
        #    Result to know the result of the firmware update procedure.
        #
        # C. In test step 3., the State Resource retrieved with a value of "1" from
        #    successive Server READs or Client NOTIFY messages, indicates the
        #    download stage of the Package Delivery is engaged
        # D. In test step 3., the Update Result Resource (/5/0/5) retrieved from
        #    successive Server READs or Client NOTIFY messages will take the
        #    value "2" indicating an error occurred during the download process
        #    related to shortage of RAM The State Resource value never reaches
        #    the Downloaded value ("3")
        # E. In test step 4., the success READ message(s) (status code "2.05"
        #    Content) on State Resource with value "0" (Idle) and Update Result
        #    Resource with value "3" indicates the firmware Package Delivery
        #    aborted due to shortage of RAM.
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.State))
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

        observed_values = self.collect_values(ResPath.FirmwareUpdate.State, b'0')
        self.assertEqual(b'0', observed_values[-1])
        self.assertEqual({b'0', b'1'}, set(observed_values))
        self.assertEqual(b'3', self.test_read(ResPath.FirmwareUpdate.UpdateResult))


class Test775_FirmwareUpdate_ErrorCase_ConnectionLostDuringDownloadPackageURI(FirmwareUpdateWithHttpServer.Test):
    class NoShutdownHttpServer(http.server.HTTPServer):
        def shutdown_request(self, request):
            pass

        def close_request(self, request):
            pass

    HTTP_SERVER_CLASS = NoShutdownHttpServer

    def during_download(self, req_handler):
        self._dangling_http_socket = req_handler.request
        # HACK to ignore any calls on .wfile afterwards
        req_handler.wfile = ANY

    def setUp(self):
        self._dangling_http_socket = None
        demo_executable = os.path.join(self.config.demo_path, self.config.demo_cmd)
        with open(demo_executable, 'rb') as f:
            pkg = make_firmware_package(f.read(), force_error=FirmwareUpdateForcedError.OutOfMemory)

        super().setUp(pkg)

    def tearDown(self):
        try:
            if self._dangling_http_socket is not None:
                self._dangling_http_socket.close()
        finally:
            super().tearDown()

    def runTest(self):
        # 1. The Server verifies through a READ (CoAP GET) command on
        #    /5/0/3 (State) the FW Update Object Instance of the Client is in Idle
        #    State
        #
        # A. In test step 1., the Server receives the status code "2.05 " (Content)
        #    for the READ success command, along with the State Resource value
        #    of "0" (Idle)
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.State))

        # 2. The Server delivers the firmware package to the Client through a
        #    WRITE operation of an URI in the Package URI Resource.
        #
        # B. In test step 2., the Server receives the status code "2.04" (Changed)
        #    for the WRITE command setting the Package URI Resource
        #    according to the PULL firmware delivery method.
        self.test_write(ResPath.FirmwareUpdate.PackageURI, self.get_firmware_uri())

        # 3. The Server sends repeated READs or OBSERVE on State and Update
        #    Result Resources (CoAP GET /5/0) of the FW Update Object
        #    Instance to determine when the download is completed or if an error
        #    occured.Before the end of download, the connection is intentionnaly
        #    lost and the download cannot be finished.
        # 4. When the Package delivery is stopped the Server READs Update
        #    Result to know the result of the firmware update procedure.
        #
        # C. In test step 3., the State Resource value set to "1" retrieved from
        #    successive Server READs or Client NOTIFY messages, indicates the
        #    Package Delivery process is engaged in a Download stage
        # D. In test step 3., the Update Result Resource (/5/0/5) retrieved from
        #    successive Server READs or Client NOTIFY messages will take the
        #    value "4" indicating an error occurred during the downloading
        #    process related to connection lost
        # E. In test step 4., the success READ message(s) (status code "2.05"
        #    Content) on State Resource with value "0" (Idle) and Update Result
        #    Resource with value "4" indicates the firmware Package Delivery
        #    aborted due to connection lost dur the Package delivery.
        observed_values = self.collect_values(ResPath.FirmwareUpdate.State, b'0',
                                              max_iterations=600)  # wait up to 60 seconds
        self.assertEqual(b'0', observed_values[-1])
        self.assertEqual({b'0', b'1'}, set(observed_values))
        self.assertEqual(b'4', self.test_read(ResPath.FirmwareUpdate.UpdateResult))


class Test775_FirmwareUpdate_ErrorCase_ConnectionLostDuringDownloadPackageURI_CoAP(FirmwareUpdate.TestWithCoapServer):
    def setUp(self):
        demo_executable = os.path.join(self.config.demo_path, self.config.demo_cmd)
        with open(demo_executable, 'rb') as f:
            pkg = make_firmware_package(f.read(), force_error=FirmwareUpdateForcedError.OutOfMemory)

        class MuteServer(coap.Server):
            def send(self, *args, **kwargs):
                pass

        super().setUp(coap_server=MuteServer(), extra_cmdline_args=['--fwu-ack-timeout', '1'])

        with self.file_server as file_server:
            file_server.set_resource('/firmware', pkg)
            self._uri = file_server.get_resource_uri('/firmware')

    def runTest(self):
        # 1. The Server verifies through a READ (CoAP GET) command on
        #    /5/0/3 (State) the FW Update Object Instance of the Client is in Idle
        #    State
        #
        # A. In test step 1., the Server receives the status code "2.05 " (Content)
        #    for the READ success command, along with the State Resource value
        #    of "0" (Idle)
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.State))

        # 2. The Server delivers the firmware package to the Client through a
        #    WRITE operation of an URI in the Package URI Resource.
        #
        # B. In test step 2., the Server receives the status code "2.04" (Changed)
        #    for the WRITE command setting the Package URI Resource
        #    according to the PULL firmware delivery method.
        self.test_write(ResPath.FirmwareUpdate.PackageURI, self._uri)

        # 3. The Server sends repeated READs or OBSERVE on State and Update
        #    Result Resources (CoAP GET /5/0) of the FW Update Object
        #    Instance to determine when the download is completed or if an error
        #    occured.Before the end of download, the connection is intentionnaly
        #    lost and the download cannot be finished.
        # 4. When the Package delivery is stopped the Server READs Update
        #    Result to know the result of the firmware update procedure.
        #
        # C. In test step 3., the State Resource value set to "1" retrieved from
        #    successive Server READs or Client NOTIFY messages, indicates the
        #    Package Delivery process is engaged in a Download stage
        # D. In test step 3., the Update Result Resource (/5/0/5) retrieved from
        #    successive Server READs or Client NOTIFY messages will take the
        #    value "4" indicating an error occurred during the downloading
        #    process related to connection lost
        # E. In test step 4., the success READ message(s) (status code "2.05"
        #    Content) on State Resource with value "0" (Idle) and Update Result
        #    Resource with value "4" indicates the firmware Package Delivery
        #    aborted due to connection lost dur the Package delivery.
        observed_values = self.collect_values(ResPath.FirmwareUpdate.State, b'0',
                                              max_iterations=50, step_time=1)
        self.assertEqual(b'0', observed_values[-1])
        self.assertEqual({b'0', b'1'}, set(observed_values))
        self.assertEqual(b'4', self.test_read(ResPath.FirmwareUpdate.UpdateResult))


class Test776_FirmwareUpdate_ErrorCase_CRCCheckFail(FirmwareUpdate.Test):
    def runTest(self):
        demo_executable = os.path.join(self.config.demo_path, self.config.demo_cmd)
        with open(demo_executable, 'rb') as f:
            payload = f.read()

        # 1. The Server verifies through a READ (CoAP GET) command on
        #    /5/0/3 (State) the FW Update Object Instance of the Client is in Idle
        #    State
        #
        # A. In test step 1, the Server receives the status code "2.05 " (Content) for
        #    the READ success command, along with the State Resource value of
        #    "0" (Idle)
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.State))

        # 2. The Server delivers the firmware package to the Client either through
        #    a WRITE (CoAP PUT) operation in the Package Resource (/5/0/0) or
        #    through a WRITE operation of an URI in the Package URI Resource.
        #
        # B. In test step 2., the Server receives the status code "2.04" (Changed)
        #    for the WRITE command setting either the Package URI Resource or
        #    setting the Package Resource, according to the chosen firmware
        #    delivery method.
        self.test_write_block(ResPath.FirmwareUpdate.Package,
                              make_firmware_package(payload, crc=0),
                              coap.ContentFormat.APPLICATION_OCTET_STREAM)

        # 3. The Server sends repeated READs or OBSERVE on State and Update
        #    Result Resources (CoAP GET /5/0) of the FW Update Object
        #    Instance to determine when the download is completed or if an error
        #    occured. The firmware package Integry Check failure stopped the
        #    download process.
        # 4. When the Package delivery is stopped the server READs Update
        #    Result to know the result of the firmware update procedure.
        #
        # C. In test step 3., the State Resource value set to "1" retrieved from
        #    successive Server READs or Client NOTIFY messages, indicates the
        #    Package Delivery process is maintained in Downloading stage
        # D. In test step 3., the Update Result Resource (/5/0/5) retrieved from
        #    successive Server READs or Client NOTIFY messages will take the
        #    value "5" indicating an error occurred during the downloading
        #    process related to the failure of the firmware package integrity check
        # E. In test step 4., the success READ message(s) (status code "2.05"
        #    Content) on State Resource with value "0" (Idle) and Update Result
        #    Resource with value "5" indicates the firmware Package Delivery
        #    aborted due to a Firmware Package Integrity failure.
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.State))
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

        observed_values = self.collect_values(ResPath.FirmwareUpdate.State, b'0')
        self.assertEqual(b'0', observed_values[-1])
        self.assertEqual({b'0', b'1'}, set(observed_values))
        self.assertEqual(b'5', self.test_read(ResPath.FirmwareUpdate.UpdateResult))


class Test777_FirmwareUpdate_ErrorCase_UnsupportedPackageType(FirmwareUpdate.Test):
    def runTest(self):
        # 1. The Server verifies through a READ (CoAP GET) command on
        #    /5/0/3 (State) the FW Update Object Instance of the Client is in Idle
        #    State
        #
        # A. In test step 1, the Server receives the status code "2.05 " (Content) for
        #    the READ success command, along with the State Resource value of
        #    "0" (Idle)
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.State))

        # 2. The Server delivers the firmware package to the Client either through
        #    a WRITE (CoAP PUT) operation in the Package Resource (/5/0/0 ) or
        #    through a WRITE operation of an URI in the Package URI Resource.
        #
        # B. In test step 2., the Server receives the status code "2.04" (Changed)
        #    for the WRITE command setting either the Package URI Resource or
        #    setting the Package Resource, according to the chosen firmware
        #    delivery method.
        self.test_write(ResPath.FirmwareUpdate.Package, b'A' * 1024,
                        format=coap.ContentFormat.APPLICATION_OCTET_STREAM)

        # 3. The Server sends repeated READs or OBSERVE on State and Update
        #    Result Resources (CoAP GET /5/0) of the FW Update Object
        #    Instance to determine when the download is completed or if an error
        #    occured. The Download cannot be finished since the firmware
        #    package type is not supported by the Client
        # 4. When the Package delivery is stopped the server READs Update
        #    Result to know the result of the firmware update procedure.
        #
        # C. In test step 3., the State Resource value set to "1" retrieved from
        #    successive Server READs or Client NOTIFY messages, indicates the
        #    Package Delivery process is in Downloading stage
        # D. In test step 3., the Update Result Resource (/5/0/5) retrieved from
        #    successive Server READs or Client NOTIFY messages will take the
        #    value "6" indicating an error occurred during the downloading
        #    process related to the firmware package type not supported by the
        #    Client.
        # E. In test step 4., the success READ message(s) (status code "2.05"
        #    Content) on State Resource with value "1" (Downloading) and
        #    Update Result Resource with value "6 indicates the firmware Package
        #    Delivery aborted due to a firmware package type not supported by the
        #    Client.
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.State))
        self.assertEqual(b'6', self.test_read(ResPath.FirmwareUpdate.UpdateResult))


class FirmwareUpdate_ErrorCase_UnsupportedPackageType_PackageURI(FirmwareUpdateWithHttpServer.Test):
    def setUp(self):
        super().setUp(b'A' * 1024)

    def runTest(self):
        # Test 777, but with Package URI
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.UpdateResult))
        self.test_write(ResPath.FirmwareUpdate.PackageURI, self.get_firmware_uri())

        observed_values = self.collect_values(ResPath.FirmwareUpdate.State, b'0')
        self.assertEqual(b'0', observed_values[-1])
        self.assertEqual({b'0', b'1'}, set(observed_values))
        self.assertEqual(b'6', self.test_read(ResPath.FirmwareUpdate.UpdateResult))


class Test778_FirmwareUpdate_ErrorCase_InvalidURI(FirmwareUpdate.Test):
    def runTest(self):
        # 1. The Server verifies through a READ (CoAP GET) command on
        #    /5/0/3 (State) the FW Update Object Instance of the Client is in Idle
        #    State
        #
        # A. In test step 1, the Server receives the status code "2.05 " (Content) for
        #    the READ success command, along with the State Resource value of
        #    "0" (Idle)
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.State))

        # 2. The Server initiates a firmware package delivery to the Client through
        #    a WRITE operation of an invalid URI in the Package URI Resource.
        #
        # B. In test step 2., the Server receives the status code "2.04" (Changed)
        #    for the WRITE command setting the Package URI Resource
        self.test_write(ResPath.FirmwareUpdate.PackageURI,
                        'http://mylovelyfwserver/')

        # 3. The Server sends repeated READs or OBSERVE on State and Update
        #    Result Resources (CoAP GET /5/0) of the FW Update Object
        #    Instance to determine when the download is completed or if an error
        #    occured. The download process is stopped by the Client due to the
        #    usage of a bad URI.
        # 4. When the Package delivery is stopped the server READs Update
        #    Result to know the result of the firmware update procedure.
        # C. In test step 3., the State Resource value set to "1" retrieved from
        #    successive Server READs or Client NOTIFY messages, indicates the
        #    Package Delivery process is maintained in Downloading stage
        # D. In test step 3., the Update Result Resource (/5/0/5) retrieved from
        #    successive Server READs or Client NOTIFY messages will take the
        #    value "7" indicating an error occurred during the downloading
        #    process related to the usage of a bad URI
        # E. In test step 4., the success READ message(s) (status code "2.05"
        #    Content) on State Resource with value "0" (Idle) and Update Result
        #    Resource with value "7" indicates the firmware Package Delivery
        #    aborted due to the connection to an Invalid URI for the firmware
        #    package delivery.
        observed_values = self.collect_values(ResPath.FirmwareUpdate.State, b'0')
        self.assertEqual(b'0', observed_values[-1])
        # TODO? client does not report "Downloading" state
        # self.assertEqual({b'0', b'1'}, set(observed_values))
        self.assertEqual({b'0'}, set(observed_values))
        self.assertEqual(b'7', self.test_read(ResPath.FirmwareUpdate.UpdateResult))


class Test779_FirmwareUpdate_ErrorCase_UnsuccessfulFirmwareUpdate(FirmwareUpdate.Test):
    def runTest(self):
        demo_executable = os.path.join(self.config.demo_path, self.config.demo_cmd)
        with open(demo_executable, 'rb') as f:
            payload = f.read()

        prev_version = self.test_read(ResPath.Device.FirmwareVersion)

        # 1. Step 1 – Package Delivery
        #    a. The Server verifies through a READ (CoAP GET) command on
        #       /5/0/3 (State) the FW Update Object Instance of the Client is in Idle
        #       State
        #
        # A. Package Delivery
        #    a. In test step 1.a, the Server receives the status code "2.05 " (Content)
        #       for the READ success command, along with the State Resource value
        #       of "0" (Idle)
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.State))

        # 1. Step 1 – Package Delivery
        #    b. The Server retrieves (CoAP GET) the initial value of the Firmware
        #       Version Resource from the Object Device Instance for verification in
        #       the Pass Criteria (C)
        #
        # A. Package Delivery
        #    b. In test step 1.b, the Server receives the status code "2.05 " (Content)
        #       for the READ success command, along with the initial value of the
        #       Firmware version Resource available from the Object Device
        #       Instance.
        prev_version = self.test_read(ResPath.Device.FirmwareVersion)

        # 1. Step 1 – Package Delivery
        #    c. The Server delivers the firmware package to the Client either
        #       through a WRITE (CoAP PUT) operation in the Package Resource
        #       (/5/0/0 ) or through a WRITE operation of an URI in the Package URI
        #       Resource.
        #
        # A. Package Delivery
        #    c. In test step 1.c, the Server receives the status code "2.04" (Changed)
        #       for the WRITE command setting either the Package URI Resource or
        #       setting the Package Resource, according to the chosen firmware
        #       delivery method.
        self.test_write_block(ResPath.FirmwareUpdate.Package,
                              make_firmware_package(payload, force_error=FirmwareUpdateForcedError.FailedUpdate),
                              coap.ContentFormat.APPLICATION_OCTET_STREAM)

        # 1. Step 1 – Package Delivery
        #    d. Polling ( successive READ commands) or Notification on Update
        #       Result and State Resources is performed, up to the time State
        #       Resource takes the ‘Downloaded’ value (2)
        #
        # A. Package Delivery
        #    d. In at this end of test step 1.d, the State Resource take the value "2"
        #       (Downloaded)
        self.assertEqual(b'2', self.test_read(ResPath.FirmwareUpdate.State))

        # 2. Step 2 – Installation Failure
        #    a. When the download is completed (State Resource value is ‘2’
        #       Downloaded) , the Server initiates a firmware update by triggering
        #       EXECUTE command on Update Resource (CoAP POST /5/0/2 )
        #
        # B. Installation failure
        #    a. In test step 2.a, the Server receives a success message "2.04"
        #       (Changed) in response to the EXECUTE command
        self.test_execute(ResPath.FirmwareUpdate.Update)

        # 2. Step 2 – Installation Failure
        #    b. Polling (READ command) or Notification on Update Result and
        #       State Resources is performed, up to the time State Resource is
        #       turned back to 2 (Downloaded) or the Update Result Resource
        #       contains the value "8" (Firmware update failed )
        #
        # B. Installation failure
        #    b. In test step 2.b, the Server receives success message(s) "2.05"
        #       Contents along with a State Resource value of "3" (Updating) or "2"
        #       (Downloaded) and an Update Ressource value of "0" (Initial Value)
        #       and "8" at the end (Firmware updated failure)
        observed_values = self.collect_values(ResPath.FirmwareUpdate.State, b'2')
        self.assertEqual(b'2', observed_values[-1])
        # state == 3 may or may not not be observed
        self.assertTrue(set(observed_values).issubset({b'2', b'3'}))
        self.assertEqual(b'8', self.test_read(ResPath.FirmwareUpdate.UpdateResult))

        # 3. Step 3 – Process Verification
        #    a. The server READs Update Result & State Resources to know the
        #       result of the firmware update procedure.
        #
        # C. Process Verification
        #    a. In test step 3.a, the Server receives success message(s) "2.05"
        #       Content" along with a State Resource value of "2" (Downloaded) and
        #       an Update Ressource value of "8" (Firmware updated failed)
        self.assertEqual(b'2', self.test_read(ResPath.FirmwareUpdate.State))
        self.assertEqual(b'8', self.test_read(ResPath.FirmwareUpdate.UpdateResult))

        # 3. Step 3 – Process Verification
        #    b. The Server READs the Firmware Version Resource from the
        #       Object Device Instance
        #
        # C. Process Verification
        #    b. In test step 3.b the Server receives success message(s) "2.05" Content"
        #       along with a Firmware Version Resource value form the Object
        #       Device Instance which has not changed compared to the one retrieved
        #       in Pass Criteria A.b
        self.assertEqual(prev_version, self.test_read(ResPath.Device.FirmwareVersion))


class FirmwareUpdate_ErrorCase_UnsuccessfulFirmwareUpdate_PackageURI(FirmwareUpdateWithHttpServer.Test):
    def setUp(self):
        demo_executable = os.path.join(self.config.demo_path, self.config.demo_cmd)
        with open(demo_executable, 'rb') as f:
            pkg = make_firmware_package(f.read(), force_error=FirmwareUpdateForcedError.FailedUpdate)

        super().setUp(pkg)

    def runTest(self):
        # Test 777, but with Package URI
        self.assertEqual(b'0', self.test_read(ResPath.FirmwareUpdate.UpdateResult))
        prev_version = self.test_read(ResPath.Device.FirmwareVersion)

        self.test_write(ResPath.FirmwareUpdate.PackageURI, self.get_firmware_uri())

        observed_values = self.collect_values(ResPath.FirmwareUpdate.State, b'2')
        self.assertEqual(b'2', observed_values[-1])
        self.assertEqual({b'1', b'2'}, set(observed_values))

        req = Lwm2mExecute(ResPath.FirmwareUpdate.Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        observed_values = self.collect_values(ResPath.FirmwareUpdate.State, b'2')
        self.assertEqual(b'2', observed_values[-1])
        # state == 3 may or may not not be observed
        self.assertTrue(set(observed_values).issubset({b'2', b'3'}))
        self.assertEqual(b'8', self.test_read(ResPath.FirmwareUpdate.UpdateResult))

        self.assertEqual(prev_version, self.test_read(ResPath.Device.FirmwareVersion))
