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

from .firmware_update import FirmwareUpdate, UpdateState
from .firmware_update import UpdateResult as FU_UpdateResult
from framework.coap_file_server import CoapFileServer
from framework.lwm2m_test import *
from .access_control import AccessMask
from .block_write import Block, equal_chunk_splitter, msg_id_generator


class UpdateSeverity:
    CRITICAL = 0
    MANDATORY = 1
    OPTIONAL = 2


class UpdateResult(FU_UpdateResult):
    CONFLICTING_STATE = 12
    DEPENDENCY_ERROR = 13


class Instances:
    APP = 0
    TEE = 1
    BOOT = 2
    MODEM = 3


def packets_from_chunks(chunks, process_options=None,
                        path=ResPath.AdvancedFirmwareUpdate[
                            Instances.APP].Package,
                        format=coap.ContentFormat.APPLICATION_OCTET_STREAM,
                        code=coap.Code.REQ_PUT):
    for idx, chunk in enumerate(chunks):
        has_more = (idx != len(chunks) - 1)

        options = ((uri_path_to_options(path) if path is not None else [])
                   + [coap.Option.CONTENT_FORMAT(format),
                      coap.Option.BLOCK1(seq_num=chunk.idx, has_more=has_more,
                                         block_size=chunk.size)])

        if process_options is not None:
            options = process_options(options, idx)

        yield coap.Packet(type=coap.Type.CONFIRMABLE,
                          code=code,
                          token=random_stuff(size=5),
                          msg_id=next(msg_id_generator),
                          options=options,
                          content=chunk.content)


GARBAGE_FILE = b'GARBAGE'
DUMMY_FILE = os.urandom(1 * 1024)
DUMMY_LONG_FILE = os.urandom(128 * 1024)
FIRMWARE_PATH = '/firmware'
FIRMWARE_SCRIPT_TEMPLATE = '#!/bin/sh\n%secho updated > "%s"\nrm "$0"\n'


#
# Test cases below are derived from test cases used to test Firmware Update
#

class AdvancedFirmwareUpdate:
    class Test(test_suite.Lwm2mSingleServerTest):
        FW_PKG_OPTS = {'magic': b'AJAY_APP', 'version': 2, 'linked': []}

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
                dir='/tmp', prefix='anjay-afu-marked-')
            self.ORIGINAL_IMG_FILE = generate_temp_filename(
                dir='/tmp', prefix='anjay-afu-bootloader-')
            with open(self.ORIGINAL_IMG_FILE, 'wb') as f:
                f.write(GARBAGE_FILE)
            self.FIRMWARE_SCRIPT_CONTENT = \
                (FIRMWARE_SCRIPT_TEMPLATE %
                 (garbage_lines, self.ANJAY_MARKER_FILE)).encode('ascii')
            super().setUp(afu_marker_path=self.ANJAY_MARKER_FILE,
                          afu_original_img_file_path=self.ORIGINAL_IMG_FILE,
                          *args, **kwargs)

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
                os.unlink(self.ORIGINAL_IMG_FILE)
                if reset_machine:
                    # reset the state machine
                    # Write /33629/0/1 (Firmware URI)
                    req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[
                                         Instances.APP].PackageURI, '')
                    self.serv.send(req)
                    self.assertMsgEqual(
                        Lwm2mChanged.matching(req)(), self.serv.recv())
                    if expect_send_after_state_machine_reset:
                        pkt = self.serv.recv()
                        self.assertMsgEqual(Lwm2mSend(), pkt)
                        CBOR.parse(pkt.content).verify_values(test=self,
                                                              expected_value_map={
                                                                  ResPath.AdvancedFirmwareUpdate[
                                                                      Instances.APP].State: UpdateState.IDLE,
                                                                  ResPath.AdvancedFirmwareUpdate[
                                                                      Instances.APP].UpdateResult: UpdateResult.INITIAL
                                                              })
                        self.serv.send(Lwm2mChanged.matching(pkt)())
                super().tearDown(auto_deregister=auto_deregister)

        def read_update_result(self, inst: int):
            req = Lwm2mRead(ResPath.AdvancedFirmwareUpdate[inst].UpdateResult)
            self.serv.send(req)
            res = self.serv.recv()
            self.assertMsgEqual(Lwm2mContent.matching(req)(), res)
            return int(res.content)

        def read_state(self, inst: int):
            req = Lwm2mRead(ResPath.AdvancedFirmwareUpdate[inst].State)
            self.serv.send(req)
            res = self.serv.recv()
            self.assertMsgEqual(Lwm2mContent.matching(req)(), res)
            return int(res.content)

        def read_linked(self, inst: int):
            req = Lwm2mRead(
                ResPath.AdvancedFirmwareUpdate[inst].LinkedInstances)
            self.serv.send(req)
            res = self.serv.recv()
            self.assertMsgEqual(Lwm2mContent.matching(req)(), res)
            return res.content

        def read_linked_and_check(self, inst: int, expected_inst: list):
            """
            expected_inst   -- list of tuples, each of form (Resource Instance ID, Value)
            """
            received = self.read_linked(inst)
            expected = TLV.make_multires(
                RID.AdvancedFirmwareUpdate.LinkedInstances,
                expected_inst)
            self.assertEqual(expected.serialize(), received)

        def read_conflicting(self, inst: int):
            req = Lwm2mRead(
                ResPath.AdvancedFirmwareUpdate[inst].ConflictingInstances)
            self.serv.send(req)
            res = self.serv.recv()
            self.assertMsgEqual(Lwm2mContent.matching(req)(), res)
            return res.content

        def read_conflicting_and_check(self, inst: int,
                                       expected_inst: list):
            """
            expected_inst   -- list of tuples, each of form (Resource Instance ID, Value)
            """
            received = self.read_conflicting(inst)
            expected = TLV.make_multires(
                RID.AdvancedFirmwareUpdate.ConflictingInstances,
                expected_inst)
            self.assertEqual(expected.serialize(), received)

        def write_firmware_and_wait_for_download(self, inst: int,
                                                 firmware_uri: str,
                                                 download_timeout_s=20):
            # Write /33629/inst/1 (Firmware URI)
            req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[inst].PackageURI,
                             firmware_uri)
            self.serv.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                                self.serv.recv())

            # wait until client downloads the firmware
            deadline = time.time() + download_timeout_s
            while time.time() < deadline:
                time.sleep(0.5)

                if self.read_state(inst) == UpdateState.DOWNLOADED:
                    return

            self.fail('firmware still not downloaded')

        def wait_until_state_is(self, inst, state, timeout_s=10):
            deadline = time.time() + timeout_s
            while time.time() < deadline:
                time.sleep(0.1)
                if self.read_state(inst) == state:
                    return

            self.fail(f'state still is not {state}')

        def execute_update_and_check_success(self, inst):
            # Execute /33629/inst/2 (Update)
            req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[inst].Update)
            self.serv.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                                self.serv.recv())

            self.wait_until_state_is(inst, UpdateState.IDLE)

            # Check /33629/inst result
            self.assertEqual(UpdateResult.SUCCESS,
                             self.read_update_result(inst))

        def prepare_package(self, firmware: bytes):
            self.PACKAGE = make_firmware_package(
                firmware, **self.FW_PKG_OPTS)

        def prepare_package_app_img(self, use_real_app=False):
            if use_real_app:
                with open(os.path.join(self.config.demo_path,
                                       self.config.demo_cmd), 'rb') as f:
                    firmware = f.read()
            else:
                firmware = self.FIRMWARE_SCRIPT_CONTENT
            self.prepare_package(firmware)

        def prepare_package_additional_img(self, content: bytes):
            with open(self.ORIGINAL_IMG_FILE, 'wb') as f:
                f.write(content)
            self.prepare_package(content)

    class TestWithHttpServer(FirmwareUpdate.TestWithHttpServerMixin, Test):
        def provide_response_app_img(self, use_real_app=False):
            super().provide_response(use_real_app)

        def provide_response_additional_img(self, content: bytes, overwrite_original_img=True):
            if overwrite_original_img:
                with open(self.ORIGINAL_IMG_FILE, 'wb') as f:
                    f.write(content)
            super().provide_response(other_content=content)

    class TestWithTlsServer(FirmwareUpdate.TestWithTlsServerMixin, Test):
        def setUp(self, pass_cert_to_demo=True, cmd_arg='', **kwargs):
            super().setUp(pass_cert_to_demo, cmd_arg='--afu-cert-file',
                          **kwargs)

    class TestWithHttpsServer(TestWithTlsServer,
                              FirmwareUpdate.TestWithHttpsServerMixin,
                              TestWithHttpServer):
        pass

    class TestWithCoapServer(FirmwareUpdate.TestWithCoapServerMixin, Test):
        pass

    class TestWithCoapsServer(FirmwareUpdate.TestWithCoapsServerMixin, Test):
        def setUp(self, extra_cmdline_args=None, *args, **kwargs):
            extra_cmdline_args = (extra_cmdline_args or []) + ['--afu-psk-identity',
                                                               str(binascii.hexlify(
                                                                   self.FW_PSK_IDENTITY), 'ascii'),
                                                               '--afu-psk-key',
                                                               str(binascii.hexlify(
                                                                   self.FW_PSK_KEY), 'ascii')]
            super().setUp(*args, extra_cmdline_args=extra_cmdline_args, **kwargs)

    class TestWithPartialDownload(Test):
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
            self.communicate('set-afu-package-path %s' %
                             (os.path.abspath(self.fw_file_name)))

    class TestWithPartialDownloadAndRestart(
        FirmwareUpdate.DemoArgsExtractorMixin, TestWithPartialDownload):
        pass

    class TestWithPartialCoapDownloadAndRestart(
        TestWithPartialDownloadAndRestart,
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
                                         make_firmware_package(
                                             self.FIRMWARE_SCRIPT_CONTENT,
                                             **self.FW_PKG_OPTS))
                self.fw_uri = file_server.get_resource_uri('/firmware')

    class TestWithPartialCoapsDownloadAndRestart(
        TestWithPartialDownloadAndRestart,
        TestWithCoapsServer):
        def setUp(self):
            class SlowServer(coap.DtlsServer):
                def send(self, *args, **kwargs):
                    time.sleep(0.5)
                    return super().send(*args, **kwargs)

            super().setUp(coap_server_class=SlowServer)

            with self.file_server as file_server:
                file_server.set_resource('/firmware',
                                         make_firmware_package(
                                             self.FIRMWARE_SCRIPT_CONTENT,
                                             **self.FW_PKG_OPTS))
                self.fw_uri = file_server.get_resource_uri('/firmware')

    class TestWithPartialHttpDownloadAndRestart(
        FirmwareUpdate.TestWithPartialHttpDownloadAndRestartMixin,
        TestWithPartialDownloadAndRestart,
        TestWithHttpServer):
        pass

    class BlockTest(Test, Block.Test):
        def block_init_file(self):
            import tempfile

            with tempfile.NamedTemporaryFile(delete=False) as f:
                fw_file_name = f.name
            self.communicate(
                'set-afu-package-path %s' % (os.path.abspath(fw_file_name)))
            return fw_file_name

        def block_send(self, data, splitter, **make_firmware_package_args):
            fw_file_name = self.block_init_file()

            make_firmware_package_args.update(self.FW_PKG_OPTS)
            chunks = list(splitter(
                make_firmware_package(data, **make_firmware_package_args)))

            for request in packets_from_chunks(chunks):
                self.serv.send(request)
                response = self.serv.recv()
                self.assertIsSuccessResponse(response, request)

            with open(fw_file_name, 'rb') as fw_file:
                self.assertEqual(fw_file.read(), data)

            self.files_to_cleanup.append(fw_file_name)

        def tearDown(self):
            for file in self.files_to_cleanup:
                try:
                    os.unlink(file)
                except FileNotFoundError:
                    pass

            # now reset the state machine
            self.write_resource(self.serv, OID.AdvancedFirmwareUpdate, 0,
                                RID.AdvancedFirmwareUpdate.Package, b'\0',
                                format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
            super(Block.Test, self).tearDown()


class AdvancedFirmwareUpdatePackageTest(AdvancedFirmwareUpdate.Test):
    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(False)
        self.set_reset_machine(False)

    def runTest(self):
        # Write /33629/0/0 (Firmware): script content
        req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.APP].Package,
                         make_firmware_package(self.FIRMWARE_SCRIPT_CONTENT,
                                               **self.FW_PKG_OPTS),
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Execute /33629/0/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Demo delays its reset while APP update. Wait for it and terminate if reset not occurred.
        self._terminate_demo()


class AdvancedFirmwareUpdateUriTest(AdvancedFirmwareUpdate.TestWithHttpServer):

    def setUp(self, *args, **kwargs):
        super().setUp(*args, **kwargs)
        self.set_check_marker(True)
        self.set_auto_deregister(False)
        self.set_reset_machine(False)

    def runTest(self):
        self.provide_response_app_img()
        self.write_firmware_and_wait_for_download(Instances.APP,
                                                  self.get_firmware_uri())

        # Execute /33629/0/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Demo delays its reset while APP update. Wait for it and terminate if reset not occurred.
        self._terminate_demo()


class AdvancedFirmwareUpdateUriAutoSuspend(AdvancedFirmwareUpdateUriTest):
    def setUp(self):
        super().setUp(extra_cmdline_args=['--fw-auto-suspend'])


class AdvancedFirmwareUpdateUriManualSuspend(AdvancedFirmwareUpdate.TestWithHttpServer):
    def runTest(self):
        self.provide_response()

        requests = list(self.requests)
        firmware_uri = self.get_firmware_uri()

        self.communicate('afu-suspend')

        # Write /5/0/1 (Firmware URI)
        req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI, firmware_uri)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # wait until client enters the DOWNLOADING state
        deadline = time.time() + 20
        while time.time() < deadline:
            time.sleep(0.5)
            if self.read_state(Instances.APP) == UpdateState.DOWNLOADING:
                break
        else:
            self.fail('firmware still not in DOWNLOADING state')

        time.sleep(5)
        self.assertEqual(requests, self.requests)

        # resume the download
        self.communicate('afu-reconnect')

        # wait until client downloads the firmware
        deadline = time.time() + 20
        while time.time() < deadline:
            time.sleep(0.5)
            if self.read_state(Instances.APP) == UpdateState.DOWNLOADED:
                break
        else:
            self.fail('firmware still not downloaded')

        self.assertEqual(requests + ['/firmware'], self.requests)


class AdvancedFirmwareUpdateStateChangeTest(
    AdvancedFirmwareUpdate.TestWithHttpServer):
    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(False)
        self.set_reset_machine(False)

    def runTest(self):
        # disable minimum notification period
        write_attrs_req = Lwm2mWriteAttributes(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].State,
            query=['pmin=0'])
        self.serv.send(write_attrs_req)
        self.assertMsgEqual(Lwm2mChanged.matching(
            write_attrs_req)(), self.serv.recv())

        # initial state should be 0
        observe_req = Lwm2mObserve(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].State)
        self.serv.send(observe_req)
        self.assertMsgEqual(Lwm2mContent.matching(
            observe_req)(content=b'0'), self.serv.recv())

        # Write /33629/0/1 (Firmware URI)
        req = Lwm2mWrite(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI,
            self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # notification should be sent before downloading
        self.assertMsgEqual(Lwm2mNotify(observe_req.token, b'1'),
                            self.serv.recv())

        self.provide_response_app_img()

        # ... and after it finishes
        self.assertMsgEqual(Lwm2mNotify(observe_req.token, b'2'),
                            self.serv.recv())

        # Execute /33629/0/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # ... and when update starts
        self.assertMsgEqual(Lwm2mNotify(observe_req.token, b'3'),
                            self.serv.recv())

        # there should be exactly one request
        self.assertEqual(['/firmware'], self.requests)

        # Demo delays its reset while APP update. Wait for it and terminate if reset not occurred.
        self._terminate_demo()


class AdvancedFirmwareUpdateSendStateChangeTest(
    AdvancedFirmwareUpdate.TestWithHttpServer):
    def setUp(self):
        super().setUp(minimum_version='1.1', maximum_version='1.1',
                      extra_cmdline_args=['--afu-use-send'])
        self.set_reset_machine(False)
        self.set_expect_send_after_state_machine_reset(True)

    def runTest(self):
        self.assertEqual(self.read_state(Instances.APP), UpdateState.IDLE)

        # Write /33629/0/1 (Firmware URI)
        req = Lwm2mWrite(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI,
            self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        pkt = self.serv.recv()
        self.assertMsgEqual(Lwm2mSend(), pkt)
        CBOR.parse(pkt.content).verify_values(test=self,
                                              expected_value_map={
                                                  ResPath.AdvancedFirmwareUpdate[
                                                      Instances.APP].UpdateResult: UpdateResult.INITIAL,
                                                  ResPath.AdvancedFirmwareUpdate[
                                                      Instances.APP].State: UpdateState.DOWNLOADING
                                              })
        self.serv.send(Lwm2mChanged.matching(pkt)())

        self.provide_response_app_img(use_real_app=True)

        pkt = self.serv.recv()
        self.assertMsgEqual(Lwm2mSend(), pkt)
        CBOR.parse(pkt.content).verify_values(test=self,
                                              expected_value_map={
                                                  ResPath.AdvancedFirmwareUpdate[
                                                      Instances.APP].UpdateResult: UpdateResult.INITIAL,
                                                  ResPath.AdvancedFirmwareUpdate[
                                                      Instances.APP].State: UpdateState.DOWNLOADED
                                              })
        self.serv.send(Lwm2mChanged.matching(pkt)())

        # Execute /33629/0/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        pkt = self.serv.recv()
        self.assertMsgEqual(Lwm2mSend(), pkt)
        CBOR.parse(pkt.content).verify_values(test=self,
                                              expected_value_map={
                                                  ResPath.AdvancedFirmwareUpdate[
                                                      Instances.APP].UpdateResult: UpdateResult.INITIAL,
                                                  ResPath.AdvancedFirmwareUpdate[
                                                      Instances.APP].State: UpdateState.UPDATING
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
                                      ResPath.AdvancedFirmwareUpdate[
                                          Instances.APP].UpdateResult: UpdateResult.SUCCESS,
                                      ResPath.AdvancedFirmwareUpdate[
                                          Instances.APP].State: UpdateState.IDLE
                                  })
        # Check if Send contains firmware and software version
        self.assertEqual(parsed_cbor[3].get(SenmlLabel.NAME), '/3/0/3')
        self.assertEqual(parsed_cbor[4].get(SenmlLabel.NAME), '/3/0/19')
        self.serv.send(Lwm2mChanged.matching(pkt)())


class AdvancedFirmwareUpdateBadBase64(AdvancedFirmwareUpdate.Test):
    def runTest(self):
        # Write /33629/0/0 (Firmware): some random text to see how it makes the world burn
        # (as text context does not implement some_bytes handler).
        data = bytes(b'\x01' * 16)
        req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.APP].Package,
                         data,
                         format=coap.ContentFormat.TEXT_PLAIN)
        self.serv.send(req)
        self.assertMsgEqual(
            Lwm2mErrorResponse.matching(req)(coap.Code.RES_BAD_REQUEST),
            self.serv.recv())


class AdvancedFirmwareUpdateGoodBase64(AdvancedFirmwareUpdate.Test):
    def runTest(self):
        import base64
        data = base64.encodebytes(bytes(b'\x01' * 16)).replace(b'\n', b'')
        req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.APP].Package,
                         data,
                         format=coap.ContentFormat.TEXT_PLAIN)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())


class AdvancedFirmwareUpdateNullPkg(AdvancedFirmwareUpdate.TestWithHttpServer):
    def runTest(self):
        self.provide_response_app_img()
        self.write_firmware_and_wait_for_download(Instances.APP,
                                                  self.get_firmware_uri())

        req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.APP].Package,
                         b'\0',
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.assertEqual(UpdateState.IDLE, self.read_state(Instances.APP))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.APP))


class AdvancedFirmwareUpdateEmptyPkgUri(
    AdvancedFirmwareUpdate.TestWithHttpServer):
    def runTest(self):
        self.provide_response_app_img()
        self.write_firmware_and_wait_for_download(Instances.APP,
                                                  self.get_firmware_uri())

        req = Lwm2mWrite(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI, '')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.assertEqual(UpdateState.IDLE, self.read_state(Instances.APP))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.APP))


class AdvancedFirmwareUpdateInvalidUri(AdvancedFirmwareUpdate.Test):
    def runTest(self):
        # observe Result
        observe_req = Lwm2mObserve(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].UpdateResult)
        self.serv.send(observe_req)
        self.assertMsgEqual(Lwm2mContent.matching(
            observe_req)(content=b'0'), self.serv.recv())

        req = Lwm2mWrite(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI,
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
        self.assertEqual(UpdateState.IDLE, self.read_state(Instances.APP))


class AdvancedFirmwareUpdateUnsupportedUri(AdvancedFirmwareUpdate.Test):
    def runTest(self):
        req = Lwm2mWrite(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI,
            b'unsupported://uri.exe')
        self.serv.send(req)
        self.assertMsgEqual(
            Lwm2mErrorResponse.matching(req)(coap.Code.RES_BAD_REQUEST),
            self.serv.recv())
        # This does not even change state or anything, because according to the LwM2M spec
        # Server can't feed us with unsupported URI type
        self.assertEqual(UpdateState.IDLE, self.read_state(Instances.APP))
        self.assertEqual(UpdateResult.UNSUPPORTED_PROTOCOL,
                         self.read_update_result(Instances.APP))


class AdvancedFirmwareUpdateOfflineUriTest(
    AdvancedFirmwareUpdate.TestWithHttpServer):
    def runTest(self):
        self.communicate('enter-offline tcp')

        self.assertEqual(UpdateState.IDLE, self.read_state(Instances.APP))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.APP))

        req = Lwm2mWrite(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI,
            self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.assertEqual(UpdateState.IDLE, self.read_state(Instances.APP))
        self.assertEqual(UpdateResult.CONNECTION_LOST,
                         self.read_update_result(Instances.APP))


class AdvancedFirmwareUpdateReplacingPkgUri(
    AdvancedFirmwareUpdate.TestWithHttpServer):
    def runTest(self):
        self.provide_response_app_img()
        self.write_firmware_and_wait_for_download(Instances.APP,
                                                  self.get_firmware_uri())

        # This isn't specified anywhere as a possible transition, therefore
        # it is most likely a bad request.
        req = Lwm2mWrite(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI,
            'http://something')
        self.serv.send(req)
        self.assertMsgEqual(
            Lwm2mErrorResponse.matching(req)(coap.Code.RES_BAD_REQUEST),
            self.serv.recv())

        self.assertEqual(UpdateState.DOWNLOADED, self.read_state(Instances.APP))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.APP))


class AdvancedFirmwareUpdateReplacingPkg(
    AdvancedFirmwareUpdate.TestWithHttpServer):
    def runTest(self):
        self.provide_response_app_img()
        self.write_firmware_and_wait_for_download(Instances.APP,
                                                  self.get_firmware_uri())

        # This isn't specified anywhere as a possible transition, therefore
        # it is most likely a bad request.
        req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.APP].Package,
                         b'trololo',
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(
            Lwm2mErrorResponse.matching(req)(coap.Code.RES_BAD_REQUEST),
            self.serv.recv())

        self.assertEqual(UpdateState.DOWNLOADED, self.read_state(Instances.APP))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.APP))




class AdvancedFirmwareUpdateHttpsReconnectTest(
    AdvancedFirmwareUpdate.TestWithPartialDownloadAndRestart,
    AdvancedFirmwareUpdate.TestWithHttpsServer):
    RESPONSE_DELAY = 0.5
    CHUNK_SIZE = 1000
    ETAGS = True

    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(False)
        self.set_reset_machine(False)

    def runTest(self):
        self.provide_response_app_img()

        # Write /33629/0/1 (Firmware URI)
        req = Lwm2mWrite(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI,
            self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_for_half_download()

        self.assertEqual(len(self.requests), 1)

        self.communicate('afu-reconnect')
        self.provide_response_app_img()

        # wait until client downloads the firmware
        deadline = time.time() + 20
        while time.time() < deadline:
            time.sleep(0.5)

            if self.read_state(Instances.APP) == UpdateState.DOWNLOADED:
                break
        else:
            raise

        # Execute /33629/0/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Demo delays its reset while APP update. Wait for it and terminate if reset not occurred.
        self._terminate_demo()


class AdvancedFirmwareUpdateHttpsCancelPackageTest(
    AdvancedFirmwareUpdate.TestWithPartialDownload,
    AdvancedFirmwareUpdate.TestWithHttpServer):
    RESPONSE_DELAY = 0.5
    CHUNK_SIZE = 1000
    ETAGS = True

    def runTest(self):
        self.provide_response_app_img()

        # Write /33629/0/1 (Firmware URI)
        req = Lwm2mWrite(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI,
            self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_for_half_download()

        self.assertEqual(self.get_socket_count(), 2)

        req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.APP].Package,
                         b'\0',
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_until_socket_count(expected=1, timeout_s=5)

        self.assertEqual(UpdateState.IDLE, self.read_state(Instances.APP))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.APP))


class AdvancedFirmwareUpdateHttpsCancelPackageUriTest(
    AdvancedFirmwareUpdate.TestWithPartialDownload,
    AdvancedFirmwareUpdate.TestWithHttpServer):
    RESPONSE_DELAY = 0.5
    CHUNK_SIZE = 1000
    ETAGS = True

    def runTest(self):
        self.provide_response_app_img()

        # Write /33629/0/1 (Firmware URI)
        req = Lwm2mWrite(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI,
            self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_for_half_download()

        self.assertEqual(self.get_socket_count(), 2)

        req = Lwm2mWrite(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI, b'')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_until_socket_count(expected=1, timeout_s=5)

        self.assertEqual(UpdateState.IDLE, self.read_state(Instances.APP))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.APP))


class AdvancedFirmwareUpdateCoapCancelPackageUriTest(
    AdvancedFirmwareUpdate.TestWithPartialDownload,
    AdvancedFirmwareUpdate.TestWithCoapServer):
    def runTest(self):
        with self.file_server as file_server:
            file_server.set_resource('/firmware',
                                     make_firmware_package(
                                         self.FIRMWARE_SCRIPT_CONTENT))
            fw_uri = file_server.get_resource_uri('/firmware')

            # Write /33629/0/1 (Firmware URI)
            req = Lwm2mWrite(
                ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI,
                fw_uri)
            self.serv.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                                self.serv.recv())

            # Handle one GET
            file_server.handle_request()

        self.assertEqual(self.get_socket_count(), 2)

        # Cancel download
        req = Lwm2mWrite(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI, b'')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_until_socket_count(expected=1, timeout_s=5)

        self.assertEqual(UpdateState.IDLE, self.read_state(Instances.APP))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.APP))


class AdvancedFirmwareUpdateHttpsOfflineTest(
    AdvancedFirmwareUpdate.TestWithPartialDownloadAndRestart,
    AdvancedFirmwareUpdate.TestWithHttpServer):
    RESPONSE_DELAY = 0.5
    CHUNK_SIZE = 1000
    ETAGS = True

    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(False)
        self.set_reset_machine(False)

    def runTest(self):
        self.provide_response_app_img()

        # Write /33629/0/1 (Firmware URI)
        req = Lwm2mWrite(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI,
            self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_for_half_download()

        self.assertEqual(self.get_socket_count(), 2)
        self.communicate('enter-offline tcp')
        self.wait_until_socket_count(expected=1, timeout_s=5)
        self.provide_response_app_img()
        self.communicate('exit-offline tcp')

        deadline = time.time() + 20
        while time.time() < deadline:
            time.sleep(0.5)

            if self.read_state(Instances.APP) == UpdateState.DOWNLOADED:
                break
        else:
            raise

        # Execute /33629/0/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Demo delays its reset while APP update. Wait for it and terminate if reset not occurred.
        self._terminate_demo()


class AdvancedFirmwareUpdateHttpsTest(
    AdvancedFirmwareUpdate.TestWithHttpsServer):
    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(False)
        self.set_reset_machine(False)

    def runTest(self):
        self.provide_response_app_img()
        self.write_firmware_and_wait_for_download(Instances.APP,
                                                  self.get_firmware_uri(),
                                                  download_timeout_s=20)

        # Execute /33629/0/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Demo delays its reset while APP update. Wait for it and terminate if reset not occurred.
        self._terminate_demo()


class AdvancedFirmwareUpdateUnconfiguredHttpsTest(
    AdvancedFirmwareUpdate.TestWithHttpsServer):
    def setUp(self):
        super().setUp(pass_cert_to_demo=False)

    def runTest(self):
        # disable minimum notification period
        write_attrs_req = Lwm2mWriteAttributes(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].UpdateResult,
            query=['pmin=0'])
        self.serv.send(write_attrs_req)
        self.assertMsgEqual(Lwm2mChanged.matching(
            write_attrs_req)(), self.serv.recv())

        # initial result should be 0
        observe_req = Lwm2mObserve(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].UpdateResult)
        self.serv.send(observe_req)
        self.assertMsgEqual(Lwm2mContent.matching(
            observe_req)(content=b'0'), self.serv.recv())

        # Write /33629/0/1 (Firmware URI)
        req = Lwm2mWrite(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI,
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
        self.assertEqual(0, self.read_state(Instances.APP))


class AdvancedFirmwareUpdateUnconfiguredHttpsWithFallbackAttemptTest(
    AdvancedFirmwareUpdate.TestWithHttpsServer):
    def setUp(self):
        super().setUp(pass_cert_to_demo=False,
                      psk_identity=b'test-identity', psk_key=b'test-key')

    def runTest(self):
        # disable minimum notification period
        write_attrs_req = Lwm2mWriteAttributes(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].UpdateResult,
            query=['pmin=0'])
        self.serv.send(write_attrs_req)
        self.assertMsgEqual(Lwm2mChanged.matching(
            write_attrs_req)(), self.serv.recv())

        # initial result should be 0
        observe_req = Lwm2mObserve(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].UpdateResult)
        self.serv.send(observe_req)
        self.assertMsgEqual(Lwm2mContent.matching(
            observe_req)(content=b'0'), self.serv.recv())

        # Write /33629/0/1 (Firmware URI)
        req = Lwm2mWrite(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI,
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
        self.assertEqual(0, self.read_state(Instances.APP))


class AdvancedFirmwareUpdateInvalidHttpsTest(
    AdvancedFirmwareUpdate.TestWithHttpsServer):
    def setUp(self):
        super().setUp(cn='invalid_cn', alt_ip=None)

    def runTest(self):
        # disable minimum notification period
        write_attrs_req = Lwm2mWriteAttributes(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].UpdateResult,
            query=['pmin=0'])
        self.serv.send(write_attrs_req)
        self.assertMsgEqual(Lwm2mChanged.matching(
            write_attrs_req)(), self.serv.recv())

        # initial result should be 0
        observe_req = Lwm2mObserve(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].UpdateResult)
        self.serv.send(observe_req)
        self.assertMsgEqual(Lwm2mContent.matching(
            observe_req)(content=b'0'), self.serv.recv())

        # Write /33629/0/1 (Firmware URI)
        req = Lwm2mWrite(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI,
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
        self.assertEqual(0, self.read_state(Instances.APP))


class AdvancedFirmwareUpdateResetInIdleState(AdvancedFirmwareUpdate.Test):
    def runTest(self):
        self.assertEqual(UpdateState.IDLE, self.read_state(Instances.APP))

        req = Lwm2mWrite(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI, b'')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())
        self.assertEqual(UpdateState.IDLE, self.read_state(Instances.APP))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.APP))

        req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.APP].Package,
                         b'\0',
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())
        self.assertEqual(UpdateState.IDLE, self.read_state(Instances.APP))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.APP))


class AdvancedFirmwareUpdateCoapUri(AdvancedFirmwareUpdate.TestWithCoapServer):
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
                                     make_firmware_package(
                                         self.FIRMWARE_SCRIPT_CONTENT,
                                         **self.FW_PKG_OPTS))
            fw_uri = file_server.get_resource_uri('/firmware')
        self.write_firmware_and_wait_for_download(Instances.APP, fw_uri)


class AdvancedFirmwareUpdateCoapsUri(AdvancedFirmwareUpdate.TestWithCoapsServer,
                                     AdvancedFirmwareUpdateCoapUri):
    pass


class AdvancedFirmwareUpdateCoapsUriAutoSuspend(AdvancedFirmwareUpdateCoapsUri):
    def setUp(self):
        super().setUp(extra_cmdline_args=['--afu-auto-suspend'])


class AdvancedFirmwareUpdateCoapsUriManualSuspend(AdvancedFirmwareUpdateCoapsUri):
    def runTest(self):
        with self.file_server as file_server:
            file_server.set_resource('/firmware',
                                     make_firmware_package(self.FIRMWARE_SCRIPT_CONTENT,
                                                           **self.FW_PKG_OPTS))
            fw_uri = file_server.get_resource_uri('/firmware')

        self.communicate('afu-suspend')

        # Write /33629/0/1 (Firmware URI)
        req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI, fw_uri)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        # wait until the state machine enters the DOWNLOADING state
        deadline = time.time() + 20
        while time.time() < deadline:
            time.sleep(0.5)
            if self.read_state(Instances.APP) == UpdateState.DOWNLOADING:
                break
        else:
            self.fail('firmware still not in DOWNLOADING state')

        time.sleep(5)
        with self.file_server as file_server:
            self.assertEqual(0, len(file_server.requests))

        # resume the download
        self.communicate('afu-reconnect')

        # wait until client downloads the firmware
        deadline = time.time() + 20
        while time.time() < deadline:
            time.sleep(0.5)
            if self.read_state(Instances.APP) == UpdateState.DOWNLOADED:
                break
        else:
            self.fail('firmware still not downloaded')


class AdvancedFirmwareUpdateCoapsReconnectTest(
    AdvancedFirmwareUpdate.TestWithPartialCoapsDownloadAndRestart):
    def runTest(self):
        # Write /33629/0/1 (Firmware URI)
        req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI, self.fw_uri)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_for_half_download()

        with self.file_server as file_server:
            file_server._server.reset()
            self.communicate('afu-reconnect')
            self.assertDtlsReconnect(file_server._server, timeout_s=10,
                                     expected_error=['0x7700', '0x7900'])

        deadline = time.time() + 20
        while time.time() < deadline:
            time.sleep(0.5)

            if self.read_state(Instances.APP) == UpdateState.DOWNLOADED:
                break
        else:
            raise


class AdvancedFirmwareUpdateCoapsSuspendDuringOfflineAndReconnectDuringOnlineTest(
    AdvancedFirmwareUpdate.TestWithPartialCoapsDownloadAndRestart):
    def runTest(self):
        # Write /33629/0/1 (Firmware URI)
        req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI, self.fw_uri)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_for_half_download()

        with self.file_server as file_server:
            # Flush any buffers
            while True:
                try:
                    file_server._server.recv(timeout_s=1)
                except socket.timeout:
                    break

            self.communicate('enter-offline')

            with self.assertRaises(socket.timeout):
                file_server._server.recv(timeout_s=5)

            self.communicate('afu-suspend')

            with self.assertRaises(socket.timeout):
                file_server._server.recv(timeout_s=5)

            self.communicate('exit-offline')

            self.assertDemoRegisters()

            with self.assertRaises(socket.timeout):
                file_server._server.recv(timeout_s=5)

            file_server._server.reset()
            self.communicate('afu-reconnect')
            self.assertPktIsDtlsClientHello(
                file_server._server._raw_udp_socket.recv(65536, socket.MSG_PEEK))

        deadline = time.time() + 20
        while time.time() < deadline:
            time.sleep(0.5)

            if self.read_state(Instances.APP) == UpdateState.DOWNLOADED:
                break
        else:
            raise


class AdvancedFirmwareUpdateCoapsSuspendDuringOfflineAndReconnectDuringOfflineTest(
    AdvancedFirmwareUpdate.TestWithPartialCoapsDownloadAndRestart):
    def runTest(self):
        # Write /33629/0/1 (Firmware URI)
        req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI, self.fw_uri)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_for_half_download()

        with self.file_server as file_server:
            # Flush any buffers
            while True:
                try:
                    file_server._server.recv(timeout_s=1)
                except socket.timeout:
                    break

            self.communicate('enter-offline')

            with self.assertRaises(socket.timeout):
                file_server._server.recv(timeout_s=5)

            self.communicate('afu-suspend')

            with self.assertRaises(socket.timeout):
                file_server._server.recv(timeout_s=5)

            self.communicate('afu-reconnect')

            with self.assertRaises(socket.timeout):
                file_server._server.recv(timeout_s=5)

            file_server._server.reset()
            self.communicate('exit-offline')
            self.assertPktIsDtlsClientHello(
                file_server._server._raw_udp_socket.recv(65536, socket.MSG_PEEK))

        self.assertDemoRegisters()

        deadline = time.time() + 20
        while time.time() < deadline:
            time.sleep(0.5)

            if self.read_state(Instances.APP) == UpdateState.DOWNLOADED:
                break
        else:
            raise


class AdvancedFirmwareUpdateCoapsOfflineDuringSuspendAndReconnectDuringOnlineTest(
    AdvancedFirmwareUpdate.TestWithPartialCoapsDownloadAndRestart):
    def runTest(self):
        # Write /33629/0/1 (Firmware URI)
        req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI, self.fw_uri)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_for_half_download()

        with self.file_server as file_server:
            # Flush any buffers
            while True:
                try:
                    file_server._server.recv(timeout_s=1)
                except socket.timeout:
                    break

            self.communicate('afu-suspend')

            with self.assertRaises(socket.timeout):
                file_server._server.recv(timeout_s=5)

            self.communicate('enter-offline')

            with self.assertRaises(socket.timeout):
                file_server._server.recv(timeout_s=5)

            self.communicate('exit-offline')

            self.assertDemoRegisters()

            with self.assertRaises(socket.timeout):
                file_server._server.recv(timeout_s=5)

            file_server._server.reset()
            self.communicate('afu-reconnect')
            self.assertPktIsDtlsClientHello(
                file_server._server._raw_udp_socket.recv(65536, socket.MSG_PEEK))

        deadline = time.time() + 20
        while time.time() < deadline:
            time.sleep(0.5)

            if self.read_state(Instances.APP) == UpdateState.DOWNLOADED:
                break
        else:
            raise


class AdvancedFirmwareUpdateCoapsOfflineDuringSuspendAndReconnectDuringOfflineTest(
    AdvancedFirmwareUpdate.TestWithPartialCoapsDownloadAndRestart):
    def runTest(self):
        # Write /33629/0/1 (Firmware URI)
        req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI, self.fw_uri)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_for_half_download()

        with self.file_server as file_server:
            # Flush any buffers
            while True:
                try:
                    file_server._server.recv(timeout_s=1)
                except socket.timeout:
                    break

            self.communicate('afu-suspend')

            with self.assertRaises(socket.timeout):
                file_server._server.recv(timeout_s=5)

            self.communicate('enter-offline')

            with self.assertRaises(socket.timeout):
                file_server._server.recv(timeout_s=5)

            self.communicate('afu-reconnect')

            with self.assertRaises(socket.timeout):
                file_server._server.recv(timeout_s=5)

            file_server._server.reset()
            self.communicate('exit-offline')
            self.assertPktIsDtlsClientHello(
                file_server._server._raw_udp_socket.recv(65536, socket.MSG_PEEK))

        self.assertDemoRegisters()

        deadline = time.time() + 20
        while time.time() < deadline:
            time.sleep(0.5)

            if self.read_state(Instances.APP) == UpdateState.DOWNLOADED:
                break
        else:
            raise


class AdvancedFirmwareUpdateHttpSuspendDuringOfflineAndReconnectDuringOnlineTest(
    AdvancedFirmwareUpdate.TestWithPartialHttpDownloadAndRestart):
    def runTest(self):
        self.provide_response()

        # Write /33629/0/1 (Firmware URI)
        req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI,
                         self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_for_half_download()

        self.communicate('enter-offline')

        time.sleep(5)
        fsize = os.stat(self.fw_file_name).st_size
        self.assertEqual(len(self.requests), 1)
        self.provide_response()

        self.communicate('afu-suspend')

        time.sleep(5)
        self.assertEqual(os.stat(self.fw_file_name).st_size, fsize)
        self.assertEqual(len(self.requests), 1)

        self.communicate('exit-offline')

        self.assertDemoRegisters()

        time.sleep(5)
        self.assertEqual(os.stat(self.fw_file_name).st_size, fsize)
        self.assertEqual(len(self.requests), 1)

        self.communicate('afu-reconnect')

        deadline = time.time() + 20
        while time.time() < deadline:
            time.sleep(0.5)

            if self.read_state(Instances.APP) == UpdateState.DOWNLOADED:
                break
        else:
            raise

        self.assertEqual(len(self.requests), 2)


class AdvancedFirmwareUpdateHttpSuspendDuringOfflineAndReconnectDuringOfflineTest(
    AdvancedFirmwareUpdate.TestWithPartialHttpDownloadAndRestart):
    def runTest(self):
        self.provide_response()

        # Write /33629/0/1 (Firmware URI)
        req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI,
                         self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_for_half_download()

        self.communicate('enter-offline')

        time.sleep(5)
        fsize = os.stat(self.fw_file_name).st_size
        self.assertEqual(len(self.requests), 1)
        self.provide_response()

        self.communicate('afu-suspend')

        time.sleep(5)
        self.assertEqual(os.stat(self.fw_file_name).st_size, fsize)
        self.assertEqual(len(self.requests), 1)

        self.communicate('afu-reconnect')

        time.sleep(5)
        self.assertEqual(os.stat(self.fw_file_name).st_size, fsize)
        self.assertEqual(len(self.requests), 1)

        self.communicate('exit-offline')

        self.assertDemoRegisters()

        deadline = time.time() + 20
        while time.time() < deadline:
            time.sleep(0.5)

            if self.read_state(Instances.APP) == UpdateState.DOWNLOADED:
                break
        else:
            raise

        self.assertEqual(len(self.requests), 2)


class AdvancedFirmwareUpdateHttpOfflineDuringSuspendAndReconnectDuringOnlineTest(
    AdvancedFirmwareUpdate.TestWithPartialHttpDownloadAndRestart):
    def runTest(self):
        self.provide_response()

        # Write /33629/0/1 (Firmware URI)
        req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI,
                         self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_for_half_download()

        self.communicate('afu-suspend')

        time.sleep(5)
        fsize = os.stat(self.fw_file_name).st_size
        self.assertEqual(len(self.requests), 1)
        self.provide_response()

        self.communicate('enter-offline')

        time.sleep(5)
        self.assertEqual(os.stat(self.fw_file_name).st_size, fsize)
        self.assertEqual(len(self.requests), 1)

        self.communicate('exit-offline')

        self.assertDemoRegisters()

        time.sleep(5)
        self.assertEqual(os.stat(self.fw_file_name).st_size, fsize)
        self.assertEqual(len(self.requests), 1)

        self.communicate('afu-reconnect')

        deadline = time.time() + 20
        while time.time() < deadline:
            time.sleep(0.5)

            if self.read_state(Instances.APP) == UpdateState.DOWNLOADED:
                break
        else:
            raise

        self.assertEqual(len(self.requests), 2)


class AdvancedFirmwareUpdateHttpOfflineDuringSuspendAndReconnectDuringOfflineTest(
    AdvancedFirmwareUpdate.TestWithPartialHttpDownloadAndRestart):
    def runTest(self):
        self.provide_response()

        # Write /33629/0/1 (Firmware URI)
        req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI,
                         self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_for_half_download()

        self.communicate('afu-suspend')

        time.sleep(5)
        fsize = os.stat(self.fw_file_name).st_size
        self.assertEqual(len(self.requests), 1)
        self.provide_response()

        self.communicate('enter-offline')

        time.sleep(5)
        self.assertEqual(os.stat(self.fw_file_name).st_size, fsize)
        self.assertEqual(len(self.requests), 1)

        self.communicate('afu-reconnect')

        time.sleep(5)
        self.assertEqual(os.stat(self.fw_file_name).st_size, fsize)
        self.assertEqual(len(self.requests), 1)

        self.communicate('exit-offline')

        self.assertDemoRegisters()

        deadline = time.time() + 20
        while time.time() < deadline:
            time.sleep(0.5)

            if self.read_state(Instances.APP) == UpdateState.DOWNLOADED:
                break
        else:
            raise

        self.assertEqual(len(self.requests), 2)


class AdvancedFirmwareUpdateRestartWithDownloaded(AdvancedFirmwareUpdate.Test):
    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(False)
        self.set_reset_machine(False)

    def runTest(self):
        # Write /33629/0/0 (Firmware): script content
        req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.APP].Package,
                         make_firmware_package(self.FIRMWARE_SCRIPT_CONTENT,
                                               **self.FW_PKG_OPTS),
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        self.assertEqual(UpdateState.DOWNLOADED, self.read_state(Instances.APP))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.APP))

        # restart the app
        self.teardown_demo_with_servers()
        self.setup_demo_with_servers(
            afu_marker_path=self.ANJAY_MARKER_FILE,
            afu_original_img_file_path=self.ORIGINAL_IMG_FILE)

        self.assertEqual(UpdateState.DOWNLOADED, self.read_state(Instances.APP))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.APP))

        # Execute /33629/0/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Demo delays its reset while APP update. Wait for it and terminate if reset not occurred.
        self._terminate_demo()




class AdvancedFirmwareUpdateResumeDownloadingOverHttpWithReconnect(
    AdvancedFirmwareUpdate.TestWithPartialHttpDownloadAndRestart):
    def _get_valgrind_args(self):
        # we don't kill the process here, so we want Valgrind
        return AdvancedFirmwareUpdate.TestWithHttpServer._get_valgrind_args(
            self)

    def send_headers(self, handler, response_content, response_etag):
        if 'Range' in handler.headers:
            self.assertEqual(handler.headers['If-Match'], response_etag)
            match = re.fullmatch(r'bytes=([0-9]+)-', handler.headers['Range'])
            self.assertIsNotNone(match)
            offset = int(match.group(1))
            handler.send_header('Content-range',
                                'bytes %d-%d/*' % (
                                    offset, len(response_content) - 1))
            return offset

    def runTest(self):
        self.provide_response_app_img()
        # Write /33629/0/1 (Firmware URI)
        req = Lwm2mWrite(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI,
            self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_for_half_download()

        # reconnect
        self.serv.reset()
        self.communicate('reconnect')
        self.provide_response_app_img()
        self.assertDemoRegisters(self.serv, timeout_s=5)

        # wait until client downloads the firmware
        deadline = time.time() + 20
        state = None
        while time.time() < deadline:
            fsize = os.stat(self.fw_file_name).st_size
            self.assertGreater(fsize * 2, self.GARBAGE_SIZE)
            state = self.read_state(Instances.APP)
            self.assertIn(
                state, {UpdateState.DOWNLOADING, UpdateState.DOWNLOADED})
            if state == UpdateState.DOWNLOADED:
                break
            # prevent test from reading Result hundreds of times per second
            time.sleep(0.5)

        self.assertEqual(state, UpdateState.DOWNLOADED)

        self.assertEqual(len(self.requests), 2)




class AdvancedFirmwareUpdateWithDelayedResultTest:
    class TestMixin:
        def runTest(self, forced_error, result):
            with open(os.path.join(self.config.demo_path, self.config.demo_cmd),
                      'rb') as f:
                firmware = f.read()

            # Write /33629/0/0 (Firmware)
            self.block_send(firmware,
                            equal_chunk_splitter(chunk_size=1024),
                            force_error=forced_error)

            # Execute /33629/0/2 (Update)
            req = Lwm2mExecute(
                ResPath.AdvancedFirmwareUpdate[Instances.APP].Update)
            self.serv.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                                self.serv.recv())

            self.serv.reset()
            self.assertDemoRegisters()
            self.assertEqual(
                self.read_path(self.serv, ResPath.AdvancedFirmwareUpdate[
                    Instances.APP].UpdateResult).content,
                str(result).encode())
            self.assertEqual(self.read_path(self.serv,
                                            ResPath.AdvancedFirmwareUpdate[
                                                Instances.APP].State).content,
                             str(UpdateState.IDLE).encode())


class AdvancedFirmwareUpdateWithDelayedSuccessTest(
    AdvancedFirmwareUpdateWithDelayedResultTest.TestMixin,
    AdvancedFirmwareUpdate.BlockTest):
    def runTest(self):
        super().runTest(FirmwareUpdateForcedError.DelayedSuccess,
                        UpdateResult.SUCCESS)


class AdvancedFirmwareUpdateWithDelayedFailureTest(
    AdvancedFirmwareUpdateWithDelayedResultTest.TestMixin,
    AdvancedFirmwareUpdate.BlockTest):
    def runTest(self):
        super().runTest(FirmwareUpdateForcedError.DelayedFailedUpdate,
                        UpdateResult.FAILED)


class AdvancedFirmwareUpdateWithSetSuccessInPerformUpgrade(
    AdvancedFirmwareUpdate.BlockTest):
    def runTest(self):
        with open(os.path.join(self.config.demo_path, self.config.demo_cmd),
                  'rb') as f:
            firmware = f.read()

        # Write /33629/0/0 (Firmware)
        self.block_send(firmware,
                        equal_chunk_splitter(chunk_size=1024),
                        force_error=FirmwareUpdateForcedError.SetSuccessInPerformUpgrade)

        # Execute /33629/0/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Update)
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
                self.fail(
                    'Firmware Update did not finish on time, last state = %s' % (
                        observed_states[-1] if observed_states else 'NONE'))
            observed_states.append(
                self.read_path(self.serv, ResPath.AdvancedFirmwareUpdate[
                    Instances.APP].State).content.decode())
            time.sleep(0.5)

        self.assertNotEqual([], observed_states)
        self.assertEqual(observed_states[-1], str(UpdateState.IDLE))
        self.assertEqual(self.read_path(self.serv,
                                        ResPath.AdvancedFirmwareUpdate[
                                            Instances.APP].UpdateResult).content,
                         str(UpdateResult.SUCCESS).encode())


class AdvancedFirmwareUpdateWithSetFailureInPerformUpgrade(
    AdvancedFirmwareUpdate.BlockTest):
    def runTest(self):
        with open(os.path.join(self.config.demo_path, self.config.demo_cmd),
                  'rb') as f:
            firmware = f.read()

        # Write /33629/0/0 (Firmware)
        self.block_send(firmware,
                        equal_chunk_splitter(chunk_size=1024),
                        force_error=FirmwareUpdateForcedError.SetFailureInPerformUpgrade)

        # Execute /33629/0/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Update)
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
                self.fail(
                    'Firmware Update did not finish on time, last state = %s' % (
                        observed_states[-1] if observed_states else 'NONE'))
            observed_states.append(
                self.read_path(self.serv, ResPath.AdvancedFirmwareUpdate[
                    Instances.APP].State).content.decode())
            time.sleep(0.5)

        self.assertNotEqual([], observed_states)
        self.assertEqual(observed_states[-1], str(UpdateState.IDLE))
        self.assertEqual(self.read_path(self.serv,
                                        ResPath.AdvancedFirmwareUpdate[
                                            Instances.APP].UpdateResult).content,
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
class AdvancedFirmwareUpdateCoapTlsTest(
    AdvancedFirmwareUpdate.TestWithTlsServer, test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(garbage=8000)

        class FirmwareResource(aiocoap.resource.Resource):
            async def render_get(resource, request):
                return aiocoap.Message(payload=make_firmware_package(
                    self.FIRMWARE_SCRIPT_CONTENT, **self.FW_PKG_OPTS))

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
                    lambda tman: EphemeralTlsServer.create_server(
                        ('127.0.0.1', 0), tman, ctx.log,
                        self.loop, sslctx)))

                socket = \
                    ctx.request_interfaces[0].token_interface.server.sockets[0]
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
        self.write_firmware_and_wait_for_download(Instances.APP,
                                                  self.get_firmware_uri())

        # Execute /33629/0/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Demo delays its reset while APP update. Wait for it and terminate if reset not occurred.
        self._terminate_demo()


class AdvancedFirmwareUpdateWeakEtagTest(
    AdvancedFirmwareUpdate.TestWithHttpServer):
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
        self.provide_response_app_img()
        self.write_firmware_and_wait_for_download(Instances.APP,
                                                  self.get_firmware_uri())

        with open(self.ANJAY_MARKER_FILE, 'rb') as f:
            marker_data = f.read()

        self.assertNotIn(b'weaketag', marker_data)

        # Execute /33629/0/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Demo delays its reset while APP update. Wait for it and terminate if reset not occurred.
        self._terminate_demo()


class SameSocketDownload:
    class Test(test_suite.Lwm2mDtlsSingleServerTest,
               test_suite.Lwm2mDmOperations,
               AdvancedFirmwareUpdate.Test):
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
                                          data=make_firmware_package(
                                              b'a' * self.GARBAGE_SIZE
                                              , **self.FW_PKG_OPTS))

        def read_state(self, serv=None):
            return int(self.read_resource(serv or self.serv,
                                          oid=OID.AdvancedFirmwareUpdate,
                                          iid=0,
                                          rid=RID.AdvancedFirmwareUpdate.State).content)

        def read_result(self, serv=None):
            return int(self.read_resource(serv or self.serv,
                                          oid=OID.AdvancedFirmwareUpdate,
                                          iid=0,
                                          rid=RID.AdvancedFirmwareUpdate.UpdateResult).content)

        def start_download(self):
            self.write_resource(self.serv,
                                oid=OID.AdvancedFirmwareUpdate,
                                iid=0,
                                rid=RID.AdvancedFirmwareUpdate.PackageURI,
                                content=self.file_server.get_resource_uri(
                                    FIRMWARE_PATH))

        def handle_get(self, pkt=None):
            if pkt is None:
                pkt = self.serv.recv()
            block2 = pkt.get_options(coap.Option.BLOCK2)
            if block2:
                self.assertEqual(block2[0].block_size(), self.BLK_SZ)
            self.file_server.handle_recvd_request(pkt)

        def num_blocks(self):
            return (len(
                self.file_server._resources[
                    FIRMWARE_PATH].data) + self.BLK_SZ - 1) // self.BLK_SZ


class AdvancedFirmwareDownloadSameSocket(SameSocketDownload.Test):
    def runTest(self):
        self.start_download()

        for _ in range(self.num_blocks()):
            pkt = self.serv.recv()
            self.handle_get(pkt)

        self.assertEqual(self.read_state(), UpdateState.DOWNLOADED)


class AdvancedFirmwareDownloadSameSocketAndOngoingBlockwiseWrite(
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
                             options=[
                                 coap.Option.BLOCK1(seq_num, has_more, 16)])
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


class AdvancedFirmwareDownloadSameSocketAndOngoingBlockwiseRead(
    SameSocketDownload.Test):
    BYTES = 160

    def runTest(self):
        self.create_instance(self.serv, oid=OID.Test, iid=1)
        self.write_resource(self.serv, oid=OID.Test, iid=1,
                            rid=RID.Test.ResBytesSize,
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
                            options=[
                                coap.Option.BLOCK2(seq_num, 0, block_size)])
            self.serv.send(pkt)
            res = self.serv.recv()
            self.assertEqual(pkt.msg_id, res.msg_id)
            self.assertTrue(len(res.get_options(coap.Option.BLOCK2)) > 0)

            if seq_num < self.num_blocks():
                # Client waits for next chunk of Raw Bytes, but gets firmware
                # block instead
                self.handle_get(dl_req_get)

        self.assertEqual(self.read_state(), UpdateState.DOWNLOADED)


class AdvancedFirmwareDownloadSameSocketUpdateDuringDownloadNstart2(
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


class AdvancedFirmwareDownloadSameSocketUpdateDuringDownloadNstart1(
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


class AdvancedFirmwareDownloadSameSocketAndReconnectNstart1(
    SameSocketDownload.Test):
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


class AdvancedFirmwareDownloadSameSocketUpdateTimeoutNstart2(
    SameSocketDownload.Test):
    NSTART = 2
    LIFETIME = 5
    MAX_RETRANSMIT = 1
    ACK_TIMEOUT = 2

    def runTest(self):
        time.sleep(self.LIFETIME + 1)
        self.assertMsgEqual(
            Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT, content=b''),
            self.serv.recv())
        self.assertMsgEqual(
            Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT, content=b''),
            self.serv.recv())
        self.start_download()

        dl_get0_0 = self.serv.recv(filter=CoapGet._pkt_matches)
        dl_get0_1 = self.serv.recv(filter=CoapGet._pkt_matches)  # retry
        self.handle_get(dl_get0_0)
        self.assertEqual(dl_get0_0.msg_id, dl_get0_1.msg_id)
        dl_get1_0 = self.serv.recv(filter=CoapGet._pkt_matches)

        # lifetime expired, demo re-registers
        self.assertDemoRegisters(lifetime=self.LIFETIME)

        # this is a retransmission
        dl_get1_1 = self.serv.recv()
        self.assertEqual(dl_get1_0.msg_id, dl_get1_1.msg_id)
        self.handle_get(dl_get1_1)
        self.handle_get(self.serv.recv())

        self.assertEqual(self.read_state(), UpdateState.DOWNLOADED)


class AdvancedFirmwareDownloadSameSocketUpdateTimeoutNstart1(
    SameSocketDownload.Test):
    LIFETIME = 5
    MAX_RETRANSMIT = 1
    ACK_TIMEOUT = 2

    def runTest(self):
        time.sleep(self.LIFETIME + 1)
        self.assertMsgEqual(
            Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT, content=b''),
            self.serv.recv())
        self.assertMsgEqual(
            Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT, content=b''),
            self.serv.recv())
        self.start_download()

        # registration temporarily held due to ongoing download
        self.handle_get(self.serv.recv())
        # and only after handling the GET, it can be sent finally
        self.assertDemoRegisters(lifetime=self.LIFETIME)
        self.handle_get(self.serv.recv())
        self.handle_get(self.serv.recv())

        self.assertEqual(self.read_state(), UpdateState.DOWNLOADED)


class AdvancedFirmwareDownloadSameSocketDontCare(SameSocketDownload.Test):
    def runTest(self):
        self.start_download()
        self.serv.recv()  # recv GET and ignore it


class AdvancedFirmwareDownloadSameSocketSuspendDueToOffline(
    SameSocketDownload.Test):
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


class AdvancedFirmwareDownloadSameSocketSuspendDueToOfflineDuringUpdate(
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


class AdvancedFirmwareDownloadSameSocketSuspendDueToOfflineDuringUpdateNoMessagesCheck(
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


class AdvancedFirmwareDownloadSameSocketAndBootstrap(SameSocketDownload.Test):
    def setUp(self):
        super().setUp(bootstrap_server=True)

    def tearDown(self):
        super(test_suite.Lwm2mSingleServerTest, self).tearDown(
            deregister_servers=[self.new_server])

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
                            content=bytes(
                                'coap://127.0.0.1:%d' % self.new_server.get_listen_port(),
                                'ascii'))
        self.write_resource(self.bootstrap_server,
                            oid=OID.Security,
                            iid=2,
                            rid=RID.Security.Mode,
                            content=str(
                                coap.server.SecurityMode.NoSec.value).encode())
        req = Lwm2mBootstrapFinish()
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())

        self.assertDemoRegisters(self.new_server)
        self.assertEqual(self.read_state(self.new_server), UpdateState.IDLE)




class AdvancedFirmwareUpdateCancelDuringIdleTest(AdvancedFirmwareUpdate.Test):
    def runTest(self):
        # Execute /33629/0/10 (Cancel)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Cancel)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(
            code=coap.Code.RES_METHOD_NOT_ALLOWED),
            self.serv.recv())

        self.assertEqual(UpdateState.IDLE, self.read_state(Instances.APP))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.APP))


class AdvancedFirmwareUpdateCancelDuringDownloadingTest(
    AdvancedFirmwareUpdate.TestWithPartialDownload,
    AdvancedFirmwareUpdate.TestWithHttpServer):
    RESPONSE_DELAY = 0.5
    CHUNK_SIZE = 3000

    def runTest(self):
        self.provide_response_app_img()

        # Write /33629/0/1 (Firmware URI)
        req = Lwm2mWrite(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI,
            self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_for_half_download()

        # Execute /33629/0/10 (Cancel)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Cancel)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        self.assertEqual(UpdateState.IDLE, self.read_state(Instances.APP))
        self.assertEqual(UpdateResult.CANCELLED,
                         self.read_update_result(Instances.APP))


class AdvancedFirmwareUpdateCancelDuringDownloadedTest(
    AdvancedFirmwareUpdate.TestWithHttpServer):
    def runTest(self):
        self.provide_response_app_img()
        self.write_firmware_and_wait_for_download(Instances.APP,
                                                  self.get_firmware_uri())

        # Execute /33629/0/10 (Cancel)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Cancel)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        self.assertEqual(UpdateState.IDLE, self.read_state(Instances.APP))
        self.assertEqual(UpdateResult.CANCELLED,
                         self.read_update_result(Instances.APP))


class AdvancedFirmwareUpdateCancelDuringUpdatingTest(
    AdvancedFirmwareUpdate.TestWithHttpServer):
    def setUp(self):
        super().setUp()
        self.set_reset_machine(False)
        # Don't run the downloaded package to be able to process Cancel
        self.FW_PKG_OPTS = {
            "force_error": FirmwareUpdateForcedError.DoNothing,
            'magic': b'AJAY_APP', 'version': 2, 'linked': []
        }

    def runTest(self):
        self.provide_response_app_img()
        self.write_firmware_and_wait_for_download(Instances.APP,
                                                  self.get_firmware_uri())

        # Execute /33629/0/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Execute /33629/0/10 (Cancel)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Cancel)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(
            code=coap.Code.RES_METHOD_NOT_ALLOWED),
            self.serv.recv())




class AdvancedFirmwareUpdateMaxDeferPeriodInvalidValueTest(
    AdvancedFirmwareUpdate.Test):
    def runTest(self):
        # Write /33629/0/13 (Maximum Defer Period)
        req = Lwm2mWrite(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].MaxDeferPeriod, b'-5')
        self.serv.send(req)
        self.assertMsgEqual(
            Lwm2mErrorResponse.matching(req)(coap.Code.RES_BAD_REQUEST),
            self.serv.recv())


class AdvancedFirmwareUpdateMaxDeferPeriodValidValueTest(
    AdvancedFirmwareUpdate.Test):
    def runTest(self):
        for max_defer_period_value in [b'0', b'30']:
            # Write /33629/0/13 (Maximum Defer Period)
            req = Lwm2mWrite(
                ResPath.AdvancedFirmwareUpdate[Instances.APP].MaxDeferPeriod,
                max_defer_period_value)
            self.serv.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())


class AdvancedFirmwareUpdateWithDefer(AdvancedFirmwareUpdate.BlockTest):
    def runTest(self):
        with open(os.path.join(self.config.demo_path, self.config.demo_cmd),
                  'rb') as f:
            firmware = f.read()

        # Write /33629/0/13 (Maximum Defer Period)
        req = Lwm2mWrite(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].MaxDeferPeriod, b'30')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Write /33629/0/0 (Firmware)
        self.block_send(firmware,
                        equal_chunk_splitter(chunk_size=1024),
                        force_error=FirmwareUpdateForcedError.Defer)

        # Execute /33629/0/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Update)
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
                self.fail(
                    'Firmware Update did not finish on time, last state = %s' % (
                        observed_states[-1] if observed_states else 'NONE'))
            observed_states.append(
                self.read_path(self.serv, ResPath.AdvancedFirmwareUpdate[
                    Instances.APP].State).content.decode())
            time.sleep(0.5)

        self.assertNotEqual([], observed_states)
        self.assertEqual(observed_states[-1], str(UpdateState.DOWNLOADED))
        self.assertEqual(self.read_path(self.serv,
                                        ResPath.AdvancedFirmwareUpdate[
                                            Instances.APP].UpdateResult).content,
                         str(UpdateResult.DEFERRED).encode())


class AdvancedFirmwareUpdateSeverityWriteInvalidValueTest(
    AdvancedFirmwareUpdate.Test):
    def runTest(self):
        for invalid_severity in [b'-1', b'3']:
            # Write /33629/0/11 (Severity)
            req = Lwm2mWrite(
                ResPath.AdvancedFirmwareUpdate[Instances.APP].Severity,
                invalid_severity)
            self.serv.send(req)
            self.assertMsgEqual(
                Lwm2mErrorResponse.matching(req)(coap.Code.RES_BAD_REQUEST),
                self.serv.recv())


class AdvancedFirmwareUpdateSeverityWriteValidValueTest(
    AdvancedFirmwareUpdate.Test):
    def runTest(self):
        valid_severity_values = [
            UpdateSeverity.CRITICAL,
            UpdateSeverity.MANDATORY,
            UpdateSeverity.OPTIONAL
        ]
        for severity in [str(i).encode() for i in valid_severity_values]:
            # Write /33629/0/11 (Severity)
            req = Lwm2mWrite(
                ResPath.AdvancedFirmwareUpdate[Instances.APP].Severity,
                severity)
            self.serv.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())


class AdvancedFirmwareUpdateSeverityReadTest(AdvancedFirmwareUpdate.Test):
    def runTest(self):
        # Read default /33629/0/11 (Severity)
        req = Lwm2mRead(ResPath.AdvancedFirmwareUpdate[Instances.APP].Severity)
        self.serv.send(req)
        self.assertMsgEqual(
            Lwm2mContent.matching(req)(
                content=str(UpdateSeverity.MANDATORY).encode()),
            self.serv.recv())


class AdvancedFirmwareUpdateLastStateChangeTime:
    class Test:
        def observe_state(self):
            # Observe /33629/0/3 (State)
            observe_req = Lwm2mObserve('/%d/0/%d' % (
                OID.AdvancedFirmwareUpdate, RID.AdvancedFirmwareUpdate.State))
            self.serv.send(observe_req)
            self.assertMsgEqual(Lwm2mContent.matching(observe_req)(),
                                self.serv.recv())
            return observe_req.token

        def cancel_observe_state(self, token):
            cancel_req = Lwm2mObserve('/%d/0/%d' % (
                OID.AdvancedFirmwareUpdate, RID.AdvancedFirmwareUpdate.State),
                                      observe=1, token=token)
            self.serv.send(cancel_req)
            self.assertMsgEqual(Lwm2mContent.matching(cancel_req)(),
                                self.serv.recv())

        def get_states_and_timestamp(self, token, deadline=None):
            # Receive a notification from /33629/0/3 and read /33629/0/12
            notification_responses = [self.serv.recv(deadline=deadline)]
            self.assertMsgEqual(Lwm2mNotify(token), notification_responses[0])

            read_response = self.read_path(self.serv,
                                           ResPath.AdvancedFirmwareUpdate[0].LastStateChangeTime,
                                           deadline=deadline)
            while True:
                try:
                    notification_responses.append(
                        self.serv.recv(timeout_s=0,
                                       filter=lambda pkt: isinstance(pkt,
                                                                     Lwm2mNotify)))
                except socket.timeout:
                    break
            return [r.content.decode() for r in
                    notification_responses], read_response.content.decode()


class AdvancedFirmwareUpdateLastStateChangeTimeWithDelayedSuccessTest(
    AdvancedFirmwareUpdate.BlockTest,
    AdvancedFirmwareUpdateLastStateChangeTime.Test):
    def runTest(self):
        with open(os.path.join(self.config.demo_path, self.config.demo_cmd),
                  'rb') as f:
            firmware = f.read()

        observe_token = self.observe_state()

        # Write /33629/0/0 (Firmware)
        self.block_send(firmware,
                        equal_chunk_splitter(chunk_size=1024),
                        force_error=FirmwareUpdateForcedError.DelayedSuccess)

        _, before_update_timestamp = self.get_states_and_timestamp(
            observe_token)

        time.sleep(1)
        # Execute /33629/0/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        state_notification = self.serv.recv()
        self.assertMsgEqual(Lwm2mNotify(observe_token), state_notification)

        self.serv.reset()
        self.assertDemoRegisters()

        req = Lwm2mRead(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].LastStateChangeTime)
        self.serv.send(req)
        after_update_response = self.serv.recv()
        self.assertMsgEqual(Lwm2mContent.matching(req)(), after_update_response)
        all_timestamps = [before_update_timestamp,
                          after_update_response.content.decode()]
        self.assertEqual(all_timestamps, sorted(all_timestamps))


class AdvancedFirmwareUpdateLastStateChangeTimeWithDeferTest(
    AdvancedFirmwareUpdate.BlockTest,
    AdvancedFirmwareUpdateLastStateChangeTime.Test):
    def observe_after_update(self, token):
        observed_states = []
        observed_timestamps = []
        deadline = time.time() + 5  # arbitrary limit
        while not observed_states or observed_states[-1] == str(
                UpdateState.UPDATING):
            states, timestamp = self.get_states_and_timestamp(token, deadline=deadline)
            observed_states += states
            observed_timestamps.append(timestamp)
        self.assertNotEqual([], observed_timestamps)
        return observed_timestamps

    def runTest(self):
        with open(os.path.join(self.config.demo_path, self.config.demo_cmd),
                  'rb') as f:
            firmware = f.read()

        # Write /33629/0/13 (Maximum Defer Period)
        req = Lwm2mWrite(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].MaxDeferPeriod, b'30')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        observe_token = self.observe_state()

        # Write /33629/0/0 (Firmware)
        self.block_send(firmware,
                        equal_chunk_splitter(chunk_size=1024),
                        force_error=FirmwareUpdateForcedError.Defer)

        _, after_write_timestamp = self.get_states_and_timestamp(observe_token)

        time.sleep(1)
        # Execute /33629/0/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        observed_timestamps = self.observe_after_update(observe_token)

        all_timestamps = [after_write_timestamp] + observed_timestamps
        self.assertEqual(all_timestamps, sorted(all_timestamps))

        self.cancel_observe_state(observe_token)


class AdvancedFirmwareUpdateSeverityPersistenceTest(
    AdvancedFirmwareUpdate.Test):
    def restart(self):
        self.teardown_demo_with_servers()
        self.setup_demo_with_servers(
            afu_marker_path=self.ANJAY_MARKER_FILE,
            afu_original_img_file_path=self.ORIGINAL_IMG_FILE)

    def runTest(self):
        severity_values = [
            UpdateSeverity.CRITICAL,
            UpdateSeverity.MANDATORY,
            UpdateSeverity.OPTIONAL
        ]
        for severity in [str(i).encode() for i in severity_values]:
            # Write /33629/0/11 (Severity)
            req = Lwm2mWrite(
                ResPath.AdvancedFirmwareUpdate[Instances.APP].Severity,
                severity)
            self.serv.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

            req = Lwm2mWrite(
                ResPath.AdvancedFirmwareUpdate[Instances.APP].Package,
                make_firmware_package(self.FIRMWARE_SCRIPT_CONTENT,
                                      **self.FW_PKG_OPTS),
                format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
            self.serv.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())
            self.restart()

            req = Lwm2mRead(
                ResPath.AdvancedFirmwareUpdate[Instances.APP].Severity)
            self.serv.send(req)
            res = self.serv.recv()
            self.assertMsgEqual(Lwm2mContent.matching(req)(), res)

            self.assertEqual(severity, res.content)
            self.restart()


class AdvancedFirmwareUpdateDeadlinePersistenceTest(
    FirmwareUpdate.DemoArgsExtractorMixin,
    AdvancedFirmwareUpdate.BlockTest):
    def get_deadline(self):
        return int(self.communicate('get-afu-deadline',
                                    match_regex='AFU_APP_UPDATE_DEADLINE==([0-9]+)\n').group(
            1))

    def runTest(self):
        with open(os.path.join(self.config.demo_path, self.config.demo_cmd),
                  'rb') as f:
            firmware = f.read()

        # Write /33629/0/13 (Maximum Defer Period)
        req = Lwm2mWrite(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].MaxDeferPeriod, b'30')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Write /33629/0/0 (Firmware)
        self.block_send(firmware,
                        equal_chunk_splitter(chunk_size=1024),
                        force_error=FirmwareUpdateForcedError.Defer)

        # Execute /33629/0/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Update)
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
                self.fail(
                    'Firmware Update did not finish on time, last state = %s' % (
                        observed_states[-1] if observed_states else 'NONE'))
            observed_states.append(
                self.read_path(self.serv, ResPath.AdvancedFirmwareUpdate[
                    Instances.APP].State).content.decode())
            time.sleep(0.5)

        self.assertNotEqual([], observed_states)
        self.assertEqual(observed_states[-1], str(UpdateState.DOWNLOADED))

        saved_deadline = self.get_deadline()

        self.demo_process.kill()
        self.serv.reset()
        self._start_demo(self.cmdline_args)
        self.assertDemoRegisters()

        restored_deadline = self.get_deadline()

        self.assertEqual(saved_deadline, restored_deadline)


#
# Below test cases are specific for Advanced Firmware Update
#

class AdvancedFirmwareUpdatePackageTestTwoNotLinkedImages(
    AdvancedFirmwareUpdate.Test):
    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(False)
        self.set_reset_machine(False)

    def runTest(self):
        # Prepare package for /33629/0
        self.FW_PKG_OPTS = {'magic': b'AJAY_APP', 'version': 2}
        self.prepare_package_app_img()

        # Write /33629/0/0 (Firmware)
        req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.APP].Package,
                         self.PACKAGE,
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Check /33629/0 state and result
        self.assertEqual(UpdateState.DOWNLOADED, self.read_state(Instances.APP))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.APP))

        # Prepare package for /33629/1
        self.FW_PKG_OPTS = {'magic': b'AJAY_TEE', 'version': 2}
        self.prepare_package_additional_img(content=DUMMY_FILE)

        # Write /33629/1/0 (Firmware)
        req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.TEE].Package,
                         self.PACKAGE,
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Check /33629/1 state and result
        self.assertEqual(UpdateState.DOWNLOADED, self.read_state(Instances.TEE))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.TEE))

        self.execute_update_and_check_success(Instances.TEE)

        # Execute /33629/0/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Demo delays its reset while APP update. Wait for it and terminate if reset not occurred.
        self._terminate_demo()


class AdvancedFirmwareUpdateUriTestTwoNotLinkedImages(
    AdvancedFirmwareUpdate.TestWithHttpServer):

    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(False)
        self.set_reset_machine(False)

    def runTest(self):
        # Prepare package for /33629/0
        self.FW_PKG_OPTS = {'magic': b'AJAY_APP', 'version': 2}
        self.provide_response_app_img()

        # Write /33629/0/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.APP,
                                                  self.get_firmware_uri())

        # Check /33629/0 result
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.APP))

        # Prepare package for /33629/1
        self.FW_PKG_OPTS = {'magic': b'AJAY_TEE', 'version': 2}
        self.provide_response_additional_img(content=DUMMY_FILE)

        # Write /33629/1/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.TEE,
                                                  self.get_firmware_uri())

        # Check /33629/1 result
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.TEE))

        self.execute_update_and_check_success(Instances.TEE)

        # Execute /33629/0/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Demo delays its reset while APP update. Wait for it and terminate if reset not occurred.
        self._terminate_demo()


class AdvancedFirmwareUpdatePackageTestFourNotLinkedImages(
    AdvancedFirmwareUpdate.Test):
    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(False)
        self.set_reset_machine(False)

    def runTest(self):
        # Prepare package for /33629/0
        self.FW_PKG_OPTS = {'magic': b'AJAY_APP', 'version': 2}
        self.prepare_package_app_img()

        # Write /33629/0/0 (Firmware)
        req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.APP].Package,
                         self.PACKAGE,
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Check /33629/0 state and result
        self.assertEqual(UpdateState.DOWNLOADED, self.read_state(Instances.APP))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.APP))

        # Prepare package for /33629/1
        self.FW_PKG_OPTS = {'magic': b'AJAY_TEE', 'version': 2}
        self.prepare_package_additional_img(content=DUMMY_FILE)

        # Write /33629/1/0 (Firmware)
        req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.TEE].Package,
                         self.PACKAGE,
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Check /33629/1 state and result
        self.assertEqual(UpdateState.DOWNLOADED, self.read_state(Instances.TEE))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.TEE))

        # Prepare package for /33629/2
        self.FW_PKG_OPTS = {'magic': b'AJAYBOOT', 'version': 2}
        self.prepare_package_additional_img(content=DUMMY_FILE)

        # Write /33629/2/0 (Firmware)
        req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.BOOT].Package,
                         self.PACKAGE,
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Check /33629/2 state and result
        self.assertEqual(UpdateState.DOWNLOADED,
                         self.read_state(Instances.BOOT))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.BOOT))

        # Prepare package for /33629/3
        self.FW_PKG_OPTS = {'magic': b'AJAYMODE', 'version': 2}
        self.prepare_package_additional_img(content=DUMMY_FILE)

        # Write /33629/3/0 (Firmware)
        req = Lwm2mWrite(
            ResPath.AdvancedFirmwareUpdate[Instances.MODEM].Package,
            self.PACKAGE,
            format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Check /33629/3 state and result
        self.assertEqual(UpdateState.DOWNLOADED,
                         self.read_state(Instances.MODEM))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.MODEM))

        self.execute_update_and_check_success(Instances.TEE)
        self.execute_update_and_check_success(Instances.BOOT)
        self.execute_update_and_check_success(Instances.MODEM)

        # Execute /33629/0/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Demo delays its reset while APP update. Wait for it and terminate if reset not occurred.
        self._terminate_demo()


class AdvancedFirmwareUpdateUriTestFourNotLinkedImages(
    AdvancedFirmwareUpdate.TestWithHttpServer):
    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(False)
        self.set_reset_machine(False)

    def runTest(self):
        # Prepare package for /33629/0
        self.FW_PKG_OPTS = {'magic': b'AJAY_APP', 'version': 2}
        self.provide_response_app_img()

        # Write /33629/0/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.APP,
                                                  self.get_firmware_uri())

        # Check /33629/0 result
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.APP))

        # Prepare package for /33629/1
        self.FW_PKG_OPTS = {'magic': b'AJAY_TEE', 'version': 2}
        self.provide_response_additional_img(content=DUMMY_FILE)

        # Write /33629/0/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.TEE,
                                                  self.get_firmware_uri())

        # Check /33629/1 result
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.TEE))

        # Prepare package for /33629/2
        self.FW_PKG_OPTS = {'magic': b'AJAYBOOT', 'version': 2}
        self.provide_response_additional_img(content=DUMMY_FILE)

        # Write /33629/0/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.BOOT,
                                                  self.get_firmware_uri())

        # Check /33629/2 result
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.BOOT))

        # Prepare package for /33629/3
        self.FW_PKG_OPTS = {'magic': b'AJAYMODE', 'version': 2}
        self.provide_response_additional_img(content=DUMMY_FILE)

        # Write /33629/0/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.MODEM,
                                                  self.get_firmware_uri())

        # Check /33629/3 result
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.MODEM))

        self.execute_update_and_check_success(Instances.TEE)
        self.execute_update_and_check_success(Instances.BOOT)
        self.execute_update_and_check_success(Instances.MODEM)

        # Execute /33629/0/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Demo delays its reset while APP update. Wait for it and terminate if reset not occurred.
        self._terminate_demo()


class AdvancedFirmwareUpdateUriTestFourNotLinkedImagesAPPFirst(
    AdvancedFirmwareUpdate.TestWithHttpServer):
    def setUp(self):
        super().setUp()
        self.set_check_marker(False)
        self.set_auto_deregister(True)
        self.set_reset_machine(False)

    def runTest(self):
        # Prepare package for /33629/0
        self.FW_PKG_OPTS = {'magic': b'AJAY_APP', 'version': 2}
        self.provide_response_app_img(use_real_app=True)

        # Write /33629/0/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.APP,
                                                  self.get_firmware_uri())

        # Check /33629/0 result
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.APP))

        # Prepare package for /33629/1
        self.FW_PKG_OPTS = {'magic': b'AJAY_TEE', 'version': 2}
        self.provide_response_additional_img(content=DUMMY_FILE)

        # Write /33629/0/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.TEE,
                                                  self.get_firmware_uri())

        # Check /33629/1 result
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.TEE))

        # Prepare package for /33629/2
        self.FW_PKG_OPTS = {'magic': b'AJAYBOOT', 'version': 2}
        self.provide_response_additional_img(content=DUMMY_FILE)

        # Write /33629/0/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.BOOT,
                                                  self.get_firmware_uri())

        # Check /33629/2 result
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.BOOT))

        # Prepare package for /33629/3
        self.FW_PKG_OPTS = {'magic': b'AJAYMODE', 'version': 2}
        self.provide_response_additional_img(content=DUMMY_FILE)

        # Write /33629/0/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.MODEM,
                                                  self.get_firmware_uri())

        # Check /33629/3 result
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.MODEM))

        # Execute /33629/0/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        self.serv.reset()
        self.assertDemoRegisters()
        self.execute_update_and_check_success(Instances.TEE)
        self.execute_update_and_check_success(Instances.BOOT)
        self.execute_update_and_check_success(Instances.MODEM)


class AdvancedFirmwareUpdateTestLinkedTeeToApp(
    AdvancedFirmwareUpdate.TestWithHttpServer):
    def setUp(self):
        super().setUp()
        self.set_check_marker(False)
        self.set_auto_deregister(True)
        self.set_reset_machine(False)

    def runTest(self):
        # Prepare package for /33629/0
        self.FW_PKG_OPTS = {'magic': b'AJAY_APP', 'version': 2,
                            'linked': [Instances.TEE]}
        self.provide_response_app_img()

        # Check /33629/0/16 (LinkedInstances), there should not be any linked instances
        self.read_linked_and_check(Instances.APP, [])

        # Write /33629/0/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.APP,
                                                  self.get_firmware_uri())

        # Check /33629/0 result and linked instances
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.APP))
        self.read_linked_and_check(Instances.APP, [(1, Objlink(33629, 1))])


class AdvancedFirmwareUpdateTestLinkedOthersToApp(
    AdvancedFirmwareUpdate.TestWithHttpServer):
    def setUp(self):
        super().setUp()
        self.set_check_marker(False)
        self.set_auto_deregister(True)
        self.set_reset_machine(False)

    def runTest(self):
        # Prepare package for /33629/0
        self.FW_PKG_OPTS = {'magic': b'AJAY_APP', 'version': 2,
                            'linked': [Instances.TEE,
                                       Instances.BOOT,
                                       Instances.MODEM]}
        self.provide_response_app_img()

        # Check /33629/0/16 (LinkedInstances), there should not be any linked instances
        self.read_linked_and_check(Instances.APP, [])

        # Write /33629/0/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.APP,
                                                  self.get_firmware_uri())

        # Check /33629/0 result linked and conflicting instances
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.APP))
        self.read_linked_and_check(Instances.APP, [(1, Objlink(33629, 1)),
                                                   (2, Objlink(33629, 2)),
                                                   (3, Objlink(33629, 3)), ])
        self.read_conflicting_and_check(Instances.APP, [(1, Objlink(33629, 1)),
                                                        (2, Objlink(33629, 2)),
                                                        (3, Objlink(33629, 3))])

        # Try to Update /33629/0
        # Execute /33629/0/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        self.wait_until_state_is(Instances.APP, UpdateState.DOWNLOADED)

        # Check /33629/0 result
        self.assertEqual(UpdateResult.DEPENDENCY_ERROR,
                         self.read_update_result(Instances.APP))


class AdvancedFirmwareUpdateTestConflictingAppAndTee(
    AdvancedFirmwareUpdate.TestWithHttpServer):
    def setUp(self):
        super().setUp()
        self.set_check_marker(False)
        self.set_auto_deregister(True)
        self.set_reset_machine(False)

    def runTest(self):
        # Prepare package for /33629/0
        self.FW_PKG_OPTS = {'magic': b'AJAY_APP', 'version': 2,
                            'linked': [Instances.TEE]}
        self.provide_response_app_img()

        # Check /33629/0/17, there should not be any conflicting instances
        self.read_conflicting_and_check(Instances.APP, [])

        # Write /33629/0/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.APP,
                                                  self.get_firmware_uri())

        # Check /33629/0 result and conflicting instances
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.APP))
        self.read_conflicting_and_check(Instances.APP, [(1, Objlink(33629, 1))])


class AdvancedFirmwareUpdateTestResolveConflictingAppAndTee(
    AdvancedFirmwareUpdate.TestWithHttpServer):
    def setUp(self):
        super().setUp()
        self.set_check_marker(False)
        self.set_auto_deregister(True)
        self.set_reset_machine(False)

    def runTest(self):
        # Prepare package for /33629/0
        self.FW_PKG_OPTS = {'magic': b'AJAY_APP', 'version': 2,
                            'linked': [Instances.TEE]}
        self.provide_response_app_img()

        # Check /33629/0/17, there should not be any conflicting instances
        self.read_conflicting_and_check(Instances.APP, [])

        # Write /33629/0/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.APP,
                                                  self.get_firmware_uri())

        # Check /33629/0 result and conflicting instances
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.APP))
        self.read_conflicting_and_check(Instances.APP, [(1, Objlink(33629, 1))])

        # Prepare package for /33629/1
        self.FW_PKG_OPTS = {'magic': b'AJAY_TEE', 'version': 2}
        self.provide_response_additional_img(content=DUMMY_FILE)

        # Write /33629/1/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.TEE,
                                                  self.get_firmware_uri())

        # Check /33629/1 result and linked
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.TEE))
        self.read_linked_and_check(Instances.TEE, [])

        # Check again /33629/0/17, conflicting instances should disappear
        self.read_conflicting_and_check(Instances.APP, [])


class AdvancedFirmwareUpdateTestResolveConflictingAndUpdateTeeAndBoot(
    AdvancedFirmwareUpdate.TestWithHttpServer):
    def setUp(self):
        super().setUp()
        self.set_check_marker(False)
        self.set_auto_deregister(True)
        self.set_reset_machine(False)

    def runTest(self):
        # Prepare package for /33629/1
        self.FW_PKG_OPTS = {'magic': b'AJAY_TEE', 'version': 2,
                            'linked': [Instances.BOOT]}
        self.provide_response_additional_img(content=DUMMY_FILE)

        # Check /33629/1/17, there should not be any conflicting instances
        self.read_conflicting_and_check(Instances.TEE, [])

        # Write /33629/1/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.TEE,
                                                  self.get_firmware_uri())

        # Check /33629/1 result and conflicting instances
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.TEE))
        self.read_conflicting_and_check(Instances.TEE, [(2, Objlink(33629, 2))])

        # Prepare package for /33629/2
        self.FW_PKG_OPTS = {'magic': b'AJAYBOOT', 'version': 2,
                            'linked': [Instances.TEE]}
        self.provide_response_additional_img(content=DUMMY_FILE)

        # Write /33629/2/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.BOOT,
                                                  self.get_firmware_uri())

        # Check /33629/2 result and linked
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.BOOT))
        self.read_linked_and_check(Instances.BOOT, [(1, Objlink(33629, 1))])

        # Check again /33629/1/17, conflicting instances should disappear
        self.read_conflicting_and_check(Instances.TEE, [])

        # Update 33629/1 TEE
        self.execute_update_and_check_success(Instances.TEE)

        # Check /33629/2 BOOT state and result, which should also be updated already
        self.assertEqual(UpdateState.IDLE, self.read_state(Instances.BOOT))
        self.assertEqual(UpdateResult.SUCCESS,
                         self.read_update_result(Instances.BOOT))

        # Check linked and conflicting are cleared
        self.read_linked_and_check(Instances.TEE, [])
        self.read_conflicting_and_check(Instances.TEE, [])
        self.read_linked_and_check(Instances.BOOT, [])
        self.read_conflicting_and_check(Instances.BOOT, [])


class AdvancedFirmwareUpdateTestNoConflictWithDownloadedEarlier(
    AdvancedFirmwareUpdate.TestWithHttpServer):
    def setUp(self):
        super().setUp()
        self.set_check_marker(False)
        self.set_auto_deregister(True)
        self.set_reset_machine(False)

    def runTest(self):
        # Prepare package for /33629/1
        self.FW_PKG_OPTS = {'magic': b'AJAY_TEE', 'version': 2}
        self.provide_response_additional_img(content=DUMMY_FILE)

        # Write /33629/0/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.TEE,
                                                  self.get_firmware_uri())

        # Check /33629/1 result and linked
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.TEE))
        self.read_linked_and_check(Instances.TEE, [])

        # Prepare package for /33629/1
        self.FW_PKG_OPTS = {'magic': b'AJAY_APP', 'version': 2,
                            'linked': [Instances.TEE]}
        self.provide_response_app_img()

        # Check /33629/0/17, there should not be any conflicting instances
        self.read_conflicting_and_check(Instances.APP, [])

        # Write /33629/0/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.APP,
                                                  self.get_firmware_uri())

        # Check /33629/0 result
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.APP))

        # Check again /33629/0/17, TEE already downloaded so should not be any conflict
        self.read_conflicting_and_check(Instances.APP, [])


class AdvancedFirmwareUpdateTestFailedUpdate(
    AdvancedFirmwareUpdate.TestWithHttpServer):
    def setUp(self):
        super().setUp()
        self.set_check_marker(False)
        self.set_auto_deregister(True)
        self.set_reset_machine(False)

    def runTest(self):
        # Prepare package for /33629/1
        self.FW_PKG_OPTS = {'magic': b'AJAY_TEE', 'version': 2,
                            'linked': [Instances.BOOT]}
        self.provide_response_additional_img(content=DUMMY_FILE, overwrite_original_img=False)

        # Check /33629/1/17, there should not be any conflicting instances
        self.read_conflicting_and_check(Instances.TEE, [])

        # Write /33629/1/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.TEE,
                                                  self.get_firmware_uri())

        # Check /33629/1 result and conflicting instances
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.TEE))
        self.read_conflicting_and_check(Instances.TEE, [(2, Objlink(33629, 2))])

        # Prepare package for /33629/2
        self.FW_PKG_OPTS = {'magic': b'AJAYBOOT', 'version': 2,
                            'linked': [Instances.TEE]}
        self.provide_response_additional_img(content=DUMMY_FILE, overwrite_original_img=False)

        # Write /33629/2/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.BOOT,
                                                  self.get_firmware_uri())

        # Check /33629/2 result and linked
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.BOOT))
        self.read_linked_and_check(Instances.BOOT, [(1, Objlink(33629, 1))])

        # Check again /33629/1/17, conflicting instances should disappear
        self.read_conflicting_and_check(Instances.TEE, [])

        # Execute /33629/1/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.TEE].Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # This execute is not going to make successful update it is forced by
        # option 'prep_additional=False' while calling `provide_response`.
        # It means that this test just not going to prepare proper image to compare with by demo.
        self.wait_until_state_is(Instances.TEE, UpdateState.DOWNLOADED)

        # Check /33629/1 result
        self.assertEqual(UpdateResult.FAILED,
                         self.read_update_result(Instances.TEE))

        # Check /33629/2 state and result
        self.assertEqual(UpdateState.DOWNLOADED,
                         self.read_state(Instances.BOOT))
        self.assertEqual(UpdateResult.FAILED,
                         self.read_update_result(Instances.BOOT))


class AdvancedFirmwareUpdateTestUpdateBootWithLinkedTee(
    AdvancedFirmwareUpdate.TestWithHttpServer):
    def setUp(self):
        super().setUp()
        self.set_check_marker(False)
        self.set_auto_deregister(True)
        self.set_reset_machine(False)

    def runTest(self):
        # Prepare package for /33629/2
        self.FW_PKG_OPTS = {'magic': b'AJAYBOOT', 'version': 2,
                            'linked': [Instances.TEE]}
        self.provide_response_additional_img(content=DUMMY_FILE)

        # Check /33629/2/17, there should not be any conflicting instances
        self.read_conflicting_and_check(Instances.BOOT, [])

        # Write /33629/2/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.BOOT,
                                                  self.get_firmware_uri())

        # Check /33629/2 result and conflicting instances
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.BOOT))
        self.read_conflicting_and_check(Instances.BOOT,
                                        [(1, Objlink(33629, 1))])

        # Prepare package for /33629/1
        self.FW_PKG_OPTS = {'magic': b'AJAY_TEE', 'version': 2}
        self.provide_response_additional_img(content=DUMMY_FILE)

        # Write /33629/0/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.TEE,
                                                  self.get_firmware_uri())

        # Check /33629/1 result and linked
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.TEE))
        self.read_linked_and_check(Instances.TEE, [])

        # Check again /33629/2 state, result and conflicting instances
        self.assertEqual(UpdateState.DOWNLOADED,
                         self.read_state(Instances.BOOT))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.BOOT))
        self.read_conflicting_and_check(Instances.BOOT, [])

        # Execute /33629/2/2 (Update)
        req = Lwm2mExecute(
            ResPath.AdvancedFirmwareUpdate[Instances.BOOT].Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        self.wait_until_state_is(Instances.TEE, UpdateState.IDLE)

        # Check /33629/1 state, result, linked and conflicting
        self.assertEqual(UpdateState.IDLE, self.read_state(Instances.TEE))
        self.assertEqual(UpdateResult.SUCCESS,
                         self.read_update_result(Instances.TEE))
        self.read_linked_and_check(Instances.TEE, [])
        self.read_conflicting_and_check(Instances.TEE, [])

        # Check /33629/2 state, result, linked and conflicting
        self.assertEqual(UpdateState.IDLE, self.read_state(Instances.BOOT))
        self.assertEqual(UpdateResult.SUCCESS,
                         self.read_update_result(Instances.BOOT))
        self.read_linked_and_check(Instances.BOOT, [])
        self.read_conflicting_and_check(Instances.BOOT, [])


class AdvancedFirmwareUpdateTestSetConflictAfterCancelOfLinkedImage(
    AdvancedFirmwareUpdate.TestWithHttpServer):
    def setUp(self):
        super().setUp()
        self.set_check_marker(False)
        self.set_auto_deregister(True)
        self.set_reset_machine(False)

    def runTest(self):
        # Prepare package for /33629/0
        self.FW_PKG_OPTS = {'magic': b'AJAY_APP', 'version': 2,
                            'linked': [Instances.TEE]}
        self.provide_response_app_img()

        # Check /33629/0/17, there should not be any conflicting instances
        self.read_conflicting_and_check(Instances.APP, [])

        # Write /33629/0/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.APP,
                                                  self.get_firmware_uri())

        # Check /33629/0 result and conflicting instances
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.APP))
        self.read_conflicting_and_check(Instances.APP, [(1, Objlink(33629, 1))])

        # Prepare package for /33629/1
        self.FW_PKG_OPTS = {'magic': b'AJAY_TEE', 'version': 2}
        self.provide_response_additional_img(content=DUMMY_FILE)

        # Write /33629/0/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.TEE,
                                                  self.get_firmware_uri())

        # Check /33629/1 result and linked
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.TEE))
        self.read_linked_and_check(Instances.TEE, [])

        # Check again /33629/0/17, conflicting instances should disappear
        self.read_conflicting_and_check(Instances.APP, [])

        # Execute /33629/1/10 (Cancel)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.TEE].Cancel)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Check again /33629/0/17, conflicting instances should be there again
        self.read_conflicting_and_check(Instances.APP, [(1, Objlink(33629, 1))])


class AdvancedFirmwareUpdatePackageTestWithMultiPackage(
    AdvancedFirmwareUpdate.Test):
    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(False)
        self.set_reset_machine(False)

    def runTest(self):
        # Prepare package for /33629/0
        self.FW_PKG_OPTS = {'magic': b'AJAY_APP', 'version': 2}
        self.prepare_package_app_img()
        app_pkg = self.PACKAGE

        # Prepare package for /33629/1
        self.FW_PKG_OPTS = {'magic': b'AJAY_TEE', 'version': 2}
        self.prepare_package_additional_img(content=DUMMY_FILE)
        tee_pkg = self.PACKAGE

        # Prepare multiple package
        multi_pkg = make_multiple_firmware_package([app_pkg, tee_pkg])

        # Write multiple package to /33629/0/0 (Firmware)
        req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.APP].Package,
                         multi_pkg,
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Check /33629/0 state and result
        self.assertEqual(UpdateState.DOWNLOADED, self.read_state(Instances.APP))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.APP))

        # Check /33629/1 state and result
        self.assertEqual(UpdateState.DOWNLOADED, self.read_state(Instances.TEE))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.TEE))

        self.execute_update_and_check_success(Instances.TEE)

        # Execute /33629/0/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Demo delays its reset while APP update. Wait for it and terminate if reset not occurred.
        self._terminate_demo()


class AdvancedFirmwareUpdatePackageTestWithMultiPackageAllImages(
    AdvancedFirmwareUpdate.Test):
    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(False)
        self.set_reset_machine(False)

    def runTest(self):
        # Prepare package for /33629/0
        self.FW_PKG_OPTS = {'magic': b'AJAY_APP', 'version': 2}
        self.prepare_package_app_img()
        app_pkg = self.PACKAGE

        # Prepare package for /33629/1
        self.FW_PKG_OPTS = {'magic': b'AJAY_TEE', 'version': 2,
                            'linked': [Instances.BOOT, Instances.MODEM]}
        self.prepare_package_additional_img(content=DUMMY_FILE)
        tee_pkg = self.PACKAGE

        # Prepare package for /33629/2
        self.FW_PKG_OPTS = {'magic': b'AJAYBOOT', 'version': 2}
        self.prepare_package_additional_img(content=DUMMY_FILE)
        boot_pkg = self.PACKAGE

        # Prepare package for /33629/3
        self.FW_PKG_OPTS = {'magic': b'AJAYMODE', 'version': 2}
        self.prepare_package_additional_img(content=DUMMY_FILE)
        modem_pkg = self.PACKAGE

        # Prepare multiple package
        multi_pkg = make_multiple_firmware_package(
            [app_pkg, tee_pkg, boot_pkg, modem_pkg])

        # Write multiple package to /33629/3/0 (Firmware)
        req = Lwm2mWrite(
            ResPath.AdvancedFirmwareUpdate[Instances.MODEM].Package,
            multi_pkg,
            format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Check /33629/0 state and result
        self.assertEqual(UpdateState.DOWNLOADED, self.read_state(Instances.APP))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.APP))

        # Check /33629/1 state and result
        self.assertEqual(UpdateState.DOWNLOADED, self.read_state(Instances.TEE))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.TEE))

        # Check /33629/2 state and result
        self.assertEqual(UpdateState.DOWNLOADED,
                         self.read_state(Instances.BOOT))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.BOOT))

        # Check /33629/3 state and result
        self.assertEqual(UpdateState.DOWNLOADED,
                         self.read_state(Instances.MODEM))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.MODEM))

        # Execute /33629/1/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.TEE].Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        self.wait_until_state_is(Instances.TEE, UpdateState.IDLE)

        # Check /33629/1 result
        self.assertEqual(UpdateResult.SUCCESS,
                         self.read_update_result(Instances.TEE))

        # Check /33629/2 state and result
        self.assertEqual(UpdateState.IDLE, self.read_state(Instances.BOOT))
        self.assertEqual(UpdateResult.SUCCESS,
                         self.read_update_result(Instances.BOOT))

        # Check /33629/3 state and result
        self.assertEqual(UpdateState.IDLE, self.read_state(Instances.MODEM))
        self.assertEqual(UpdateResult.SUCCESS,
                         self.read_update_result(Instances.MODEM))

        # Execute /33629/0/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Demo delays its reset while APP update. Wait for it and terminate if reset not occurred.
        self._terminate_demo()


class AdvancedFirmwareUpdatePackageTestWithMultiPackageConflictingDownloads(
    AdvancedFirmwareUpdate.Test):
    def setUp(self):
        super().setUp()
        self.set_check_marker(False)
        self.set_auto_deregister(True)
        self.set_reset_machine(False)

    def runTest(self):
        # Prepare package for /33629/1
        self.FW_PKG_OPTS = {'magic': b'AJAY_TEE', 'version': 2}
        self.prepare_package_additional_img(content=DUMMY_FILE)

        # Write multiple package to /33629/1/0 (Firmware)
        req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.TEE].Package,
                         self.PACKAGE,
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Check /33629/1 state and result
        self.assertEqual(UpdateState.DOWNLOADED, self.read_state(Instances.TEE))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.TEE))

        # Prepare package for /33629/0
        self.FW_PKG_OPTS = {'magic': b'AJAY_APP', 'version': 2}
        self.prepare_package_app_img()
        app_pkg = self.PACKAGE

        # Prepare package for /33629/1
        self.FW_PKG_OPTS = {'magic': b'AJAY_TEE', 'version': 2}
        self.prepare_package_additional_img(content=DUMMY_FILE)
        tee_pkg = self.PACKAGE

        # Prepare multiple package
        multi_pkg = make_multiple_firmware_package([app_pkg, tee_pkg])

        # Write multiple package to /33629/3/0 (Firmware)
        req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.APP].Package,
                         multi_pkg,
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Check /33629/0 state and result
        self.assertEqual(UpdateState.IDLE, self.read_state(Instances.APP))
        self.assertEqual(UpdateResult.CONFLICTING_STATE,
                         self.read_update_result(Instances.APP))
        self.read_conflicting_and_check(Instances.APP, [(1, Objlink(33629, 1))])

        # Check /33629/1 state and result
        self.assertEqual(UpdateState.DOWNLOADED, self.read_state(Instances.TEE))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.TEE))


class AdvancedFirmwareUpdateUriTestExplicitLinkedUpdate(
    AdvancedFirmwareUpdate.TestWithHttpServer):

    def setUp(self):
        super().setUp()
        self.set_check_marker(False)
        self.set_auto_deregister(True)
        self.set_reset_machine(False)

    def runTest(self):
        # Prepare package for /33629/0
        self.FW_PKG_OPTS = {'magic': b'AJAY_APP', 'version': 2, 'linked': [1]}
        self.provide_response_app_img(use_real_app=True)

        # Write /33629/0/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.APP,
                                                  self.get_firmware_uri())

        # Check /33629/0 result and linked
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.APP))
        self.read_linked_and_check(Instances.APP, [(1, Objlink(33629, 1))])

        # Prepare package for /33629/1
        self.FW_PKG_OPTS = {'magic': b'AJAY_TEE', 'version': 2, 'linked': [0]}
        self.provide_response_additional_img(content=DUMMY_FILE)

        # Write /33629/1/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.TEE,
                                                  self.get_firmware_uri())

        # Check /33629/1 result and linked
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.TEE))
        self.read_linked_and_check(Instances.TEE, [(0, Objlink(33629, 0))])

        # Prepare package for /33629/2
        self.FW_PKG_OPTS = {'magic': b'AJAYBOOT', 'version': 2}
        self.prepare_package_additional_img(content=DUMMY_FILE)

        # Write /33629/2/0 (Firmware)
        req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.BOOT].Package,
                         self.PACKAGE,
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Check /33629/2 state and result
        self.assertEqual(UpdateState.DOWNLOADED,
                         self.read_state(Instances.BOOT))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.BOOT))

        # Prepare package for /33629/3
        self.FW_PKG_OPTS = {'magic': b'AJAYMODE', 'version': 2}
        self.prepare_package_additional_img(content=DUMMY_FILE)

        # Write /33629/3/0 (Firmware)
        req = Lwm2mWrite(
            ResPath.AdvancedFirmwareUpdate[Instances.MODEM].Package,
            self.PACKAGE,
            format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Check /33629/3 state and result
        self.assertEqual(UpdateState.DOWNLOADED,
                         self.read_state(Instances.MODEM))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.MODEM))

        # Execute /33629/0/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Update,
                           content=b'0=\'</33629/1>,</33629/2>,</33629/3>\'')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        self.serv.reset()
        self.assertDemoRegisters()

        # Check /33629/0 state and result
        self.assertEqual(UpdateState.IDLE,
                         self.read_state(Instances.APP))
        self.assertEqual(UpdateResult.SUCCESS,
                         self.read_update_result(Instances.APP))

        # Check /33629/1 state and result
        self.assertEqual(UpdateState.IDLE,
                         self.read_state(Instances.TEE))
        self.assertEqual(UpdateResult.SUCCESS,
                         self.read_update_result(Instances.TEE))

        # Check /33629/2 state and result
        self.assertEqual(UpdateState.IDLE,
                         self.read_state(Instances.BOOT))
        self.assertEqual(UpdateResult.SUCCESS,
                         self.read_update_result(Instances.BOOT))

        # Check /33629/3 state and result
        self.assertEqual(UpdateState.IDLE,
                         self.read_state(Instances.MODEM))
        self.assertEqual(UpdateResult.SUCCESS,
                         self.read_update_result(Instances.MODEM))


class AdvancedFirmwareUpdateUriTestExplicitSinglePartitionUpdate(
    AdvancedFirmwareUpdate.TestWithHttpServer):

    def setUp(self):
        super().setUp()
        self.set_check_marker(False)
        self.set_auto_deregister(True)
        self.set_reset_machine(False)

    def runTest(self):
        # Prepare package for /33629/0
        self.FW_PKG_OPTS = {'magic': b'AJAY_APP', 'version': 2, 'linked': [1]}
        self.provide_response_app_img(use_real_app=True)

        # Write /33629/0/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.APP,
                                                  self.get_firmware_uri())

        # Check /33629/0 result and linked
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.APP))
        self.read_linked_and_check(Instances.APP, [(1, Objlink(33629, 1))])

        # Prepare package for /33629/1
        self.FW_PKG_OPTS = {'magic': b'AJAY_TEE', 'version': 2, 'linked': [0]}
        self.provide_response_additional_img(content=DUMMY_FILE)

        # Write /33629/1/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.TEE,
                                                  self.get_firmware_uri())

        # Check /33629/1 result and linked
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.TEE))
        self.read_linked_and_check(Instances.TEE, [(0, Objlink(33629, 0))])

        # Prepare package for /33629/2
        self.FW_PKG_OPTS = {'magic': b'AJAYBOOT', 'version': 2}
        self.prepare_package_additional_img(content=DUMMY_FILE)

        # Write /33629/2/0 (Firmware)
        req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.BOOT].Package,
                         self.PACKAGE,
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Check /33629/2 state and result
        self.assertEqual(UpdateState.DOWNLOADED,
                         self.read_state(Instances.BOOT))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.BOOT))

        # Prepare package for /33629/3
        self.FW_PKG_OPTS = {'magic': b'AJAYMODE', 'version': 2}
        self.prepare_package_additional_img(content=DUMMY_FILE)

        # Write /33629/3/0 (Firmware)
        req = Lwm2mWrite(
            ResPath.AdvancedFirmwareUpdate[Instances.MODEM].Package,
            self.PACKAGE,
            format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Check /33629/3 state and result
        self.assertEqual(UpdateState.DOWNLOADED,
                         self.read_state(Instances.MODEM))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.MODEM))

        # Execute /33629/0/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Update,
                           content=b'0')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        self.serv.reset()
        self.assertDemoRegisters()

        # Check /33629/0 state and result
        self.assertEqual(UpdateState.IDLE,
                         self.read_state(Instances.APP))
        self.assertEqual(UpdateResult.SUCCESS,
                         self.read_update_result(Instances.APP))

        # Check /33629/1 state and result
        self.assertEqual(UpdateState.DOWNLOADED,
                         self.read_state(Instances.TEE))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.TEE))


class AdvancedFirmwareUpdateUriTestCheckPkgVersion(
    AdvancedFirmwareUpdate.TestWithHttpServer):

    def setUp(self):
        super().setUp()
        self.set_check_marker(False)
        self.set_auto_deregister(True)
        self.set_reset_machine(False)

    def runTest(self):
        # Prepare package for /33629/0
        self.FW_PKG_OPTS = {'magic': b'AJAY_APP', 'version': 2,
                            'pkg_version': b'2.0.1'}
        self.provide_response_app_img()

        # Write /33629/0/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.APP,
                                                  self.get_firmware_uri())

        # Check /33629/0 result
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.APP))

        # Check /33629/7 pkg_version
        req = Lwm2mRead(
            ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageVersion)
        self.serv.send(req)
        res = self.serv.recv()
        self.assertMsgEqual(Lwm2mContent.matching(req)(), res)
        self.assertEqual(res.content, b'2.0.1')


class AdvancedFirmwareUpdateVersionConflictTest(
    AdvancedFirmwareUpdate.TestWithHttpServer):

    def setUp(self):
        super().setUp()
        self.set_check_marker(False)
        self.set_auto_deregister(True)
        self.set_reset_machine(False)

    def runTest(self):
        # Prepare package for /33629/0
        self.FW_PKG_OPTS = {'magic': b'AJAY_APP', 'version': 2,
                            'pkg_version': b'2.0.1'}
        self.provide_response_app_img()

        # Write /33629/0/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.APP,
                                                  self.get_firmware_uri())

        # Check /33629/0 result
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.APP))

        # In demo there is requirement that major version of TEE has to be equal or above new version of APP
        # There should be conflicting instance
        self.read_conflicting_and_check(Instances.APP, [(1, Objlink(33629, 1))])

        # Execute /33629/0/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        self.wait_until_state_is(Instances.APP, UpdateState.DOWNLOADED)

        # Check /33629/0 result
        self.assertEqual(UpdateResult.DEPENDENCY_ERROR,
                         self.read_update_result(Instances.APP))

        # Prepare package for /33629/1
        self.FW_PKG_OPTS = {'magic': b'AJAY_TEE', 'version': 2,
                            'pkg_version': b'2.0.1'}
        self.provide_response_additional_img(content=DUMMY_FILE)

        # Write /33629/1/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.TEE,
                                                  self.get_firmware_uri())

        self.execute_update_and_check_success(Instances.TEE)

        # Check again conflicting, it should disappear
        self.read_conflicting_and_check(Instances.APP, [])


class AdvancedFirmwareUpdateQueueParallelPull(AdvancedFirmwareUpdate.TestWithCoapServer):
    def setUp(self, coap_server=None, *args, **kwargs):
        super().setUp(coap_server=[None, None], *args, **kwargs)

    def runTest(self):
        # Prepare package for /33629/0
        self.FW_PKG_OPTS = {'magic': b'AJAY_APP', 'version': 2}
        self.prepare_package_additional_img(content=DUMMY_LONG_FILE)
        with self.get_file_server(serv=0) as file_server:
            file_server.set_resource('/firmwareAPP',
                                     self.PACKAGE)
            fw_uri = file_server.get_resource_uri('/firmwareAPP')

        # Write /33629/inst/1 (Firmware URI)
        req1 = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI,
                          fw_uri)
        self.serv.send(req1)

        # Prepare package for /33629/1
        self.FW_PKG_OPTS = {'magic': b'AJAY_TEE', 'version': 2}
        self.prepare_package_additional_img(content=DUMMY_FILE)
        with self.get_file_server(serv=1) as file_server:
            file_server.set_resource('/firmwareTEE',
                                     self.PACKAGE)
            fw_uri = file_server.get_resource_uri('/firmwareTEE')

        # Write /33629/inst/1 (Firmware URI)
        req2 = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.TEE].PackageURI,
                          fw_uri)
        self.serv.send(req2)

        # This case finally tests if two strings occur one after another to ensure that
        # download schedule for TEE is done just after the APP finish downloading
        if self.read_log_until_match(regex=re.escape(b'instance /33629/0 downloaded successfully'),
                                     timeout_s=5) is None:
            raise self.failureException(
                'string not found')

        if self.read_log_until_match(regex=re.escape(b'download scheduled: ' + fw_uri.encode()),
                                     timeout_s=1) is None:
            raise self.failureException(
                'string not found')

        # There should be two request already received as we didn't check it before, to not wait for client
        self.assertMsgEqual(Lwm2mChanged.matching(req1)(),
                            self.serv.recv())
        self.assertMsgEqual(Lwm2mChanged.matching(req2)(),
                            self.serv.recv())

        # Make sure that queued download finished properly
        self.wait_until_state_is(Instances.TEE, UpdateState.DOWNLOADED)


class AdvancedFirmwareUpdateRejectPushWhilePull(AdvancedFirmwareUpdate.TestWithCoapServer):
    def runTest(self):
        # Prepare package for /33629/0
        self.FW_PKG_OPTS = {'magic': b'AJAY_APP', 'version': 2}
        self.prepare_package_additional_img(content=DUMMY_LONG_FILE)
        with self.file_server as file_server:
            file_server.set_resource('/firmwareAPP',
                                     self.PACKAGE)
            fw_uri = file_server.get_resource_uri('/firmwareAPP')

        # Write /33629/inst/1 (Firmware URI)
        req1 = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI,
                          fw_uri)
        self.serv.send(req1)

        # Prepare package for /33629/1
        self.FW_PKG_OPTS = {'magic': b'AJAY_TEE', 'version': 2}
        self.prepare_package_additional_img(content=DUMMY_FILE)

        # Write /33629/3/0 (Firmware)
        req2 = Lwm2mWrite(
            ResPath.AdvancedFirmwareUpdate[Instances.MODEM].Package,
            self.PACKAGE,
            format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req2)

        # There should be request already received as we didn't check it before, to not wait for client
        self.assertMsgEqual(Lwm2mChanged.matching(req1)(),
                            self.serv.recv())

        # There is pull ongoing on another instance so push should return bad request
        expected_res = Lwm2mChanged.matching(req2)()
        expected_res.code = coap.Code.RES_METHOD_NOT_ALLOWED
        self.assertMsgEqual(expected_res, self.serv.recv())

        # Make sure that ongoing download is finished properly
        self.wait_until_state_is(Instances.APP, UpdateState.DOWNLOADED)


class AdvancedFirmwareUpdateHandleTooManyPulls(AdvancedFirmwareUpdate.TestWithCoapServer):
    def setUp(self, coap_server=None, *args, **kwargs):
        super().setUp(coap_server=[None, None], *args, **kwargs)

    def runTest(self):
        # Prepare package for /33629/0
        self.FW_PKG_OPTS = {'magic': b'AJAY_APP', 'version': 2}
        self.prepare_package_additional_img(content=DUMMY_LONG_FILE)
        with self.get_file_server(serv=0) as file_server:
            file_server.set_resource('/firmwareAPP',
                                     self.PACKAGE)
            fw_uri = file_server.get_resource_uri('/firmwareAPP')

        # Write /33629/inst/0 (Firmware URI)
        req1 = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI,
                          fw_uri)
        self.serv.send(req1)

        # Prepare package for /33629/1
        self.FW_PKG_OPTS = {'magic': b'AJAY_TEE', 'version': 2}
        self.prepare_package_additional_img(content=DUMMY_FILE)
        with self.get_file_server(serv=1) as file_server:
            file_server.set_resource('/firmwareTEE',
                                     self.PACKAGE)
            fw_uri = file_server.get_resource_uri('/firmwareTEE')

        # Write /33629/inst/1 (Firmware URI)
        req2 = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.TEE].PackageURI,
                          fw_uri)
        self.serv.send(req2)

        # Write /33629/inst/1 (Firmware URI)
        req3 = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.TEE].PackageURI,
                          fw_uri)
        self.serv.send(req3)

        # There should be three requests already received as we didn't check it before, to not wait for client
        self.assertMsgEqual(Lwm2mChanged.matching(req1)(),
                            self.serv.recv())
        self.assertMsgEqual(Lwm2mChanged.matching(req2)(),
                            self.serv.recv())

        # There is pull already ongoing so second one should return bad request
        expected_res = Lwm2mChanged.matching(req3)()
        expected_res.code = coap.Code.RES_BAD_REQUEST
        self.assertMsgEqual(expected_res, self.serv.recv())

        # Both instances still should end with DOWNLOADED state
        self.wait_until_state_is(Instances.APP, UpdateState.DOWNLOADED)
        self.wait_until_state_is(Instances.TEE, UpdateState.DOWNLOADED)


class AdvancedFirmwareUpdateHandleTooManyPullsWithSecureConnection(AdvancedFirmwareUpdate.TestWithCoapServer,
                                                                   test_suite.Lwm2mDtlsSingleServerTest):
    def setUp(self, coap_server=None, *args, **kwargs):
        dtlserv = [coap.DtlsServer(psk_key=self.PSK_KEY, psk_identity=self.PSK_IDENTITY),
                   coap.DtlsServer(psk_key=self.PSK_KEY, psk_identity=self.PSK_IDENTITY)]
        super().setUp(coap_server=dtlserv, *args, **kwargs)

    def runTest(self):
        # Prepare package for /33629/0
        self.FW_PKG_OPTS = {'magic': b'AJAY_APP', 'version': 2}
        self.prepare_package_additional_img(content=DUMMY_FILE)
        with self.get_file_server(serv=0) as file_server:
            file_server.set_resource('/firmwareAPP',
                                     self.PACKAGE)
            fw_uri = file_server.get_resource_uri('/firmwareAPP')

        # Write /33629/inst/0 (Firmware URI)
        req1 = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI,
                          fw_uri)
        self.serv.send(req1)

        # Prepare package for /33629/1
        self.FW_PKG_OPTS = {'magic': b'AJAY_TEE', 'version': 2}
        self.prepare_package_additional_img(content=DUMMY_FILE)
        with self.get_file_server(serv=1) as file_server:
            file_server.set_resource('/firmwareTEE',
                                     self.PACKAGE)
            fw_uri = file_server.get_resource_uri('/firmwareTEE')

        # Write /33629/inst/1 (Firmware URI)
        req2 = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.TEE].PackageURI,
                          fw_uri)
        self.serv.send(req2)

        # Write /33629/inst/1 (Firmware URI)
        req3 = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.TEE].PackageURI,
                          fw_uri)
        self.serv.send(req3)

        # There should be three requests already received as we didn't check it before, to not wait for client
        self.assertMsgEqual(Lwm2mChanged.matching(req1)(),
                            self.serv.recv())
        self.assertMsgEqual(Lwm2mChanged.matching(req2)(),
                            self.serv.recv())

        # There is pull already ongoing so second one should return bad request
        expected_res = Lwm2mChanged.matching(req3)()
        expected_res.code = coap.Code.RES_BAD_REQUEST
        self.assertMsgEqual(expected_res, self.serv.recv())

        # Both instances still should end with DOWNLOADED state
        self.wait_until_state_is(Instances.APP, UpdateState.DOWNLOADED)
        self.wait_until_state_is(Instances.TEE, UpdateState.DOWNLOADED)


class AdvancedFirmwareUpdateForceAppToUpdateFirstAndCheckProperStateOfAdditionalImgAfterReboot(
    AdvancedFirmwareUpdate.TestWithHttpServer):
    def setUp(self):
        super().setUp()
        self.set_check_marker(False)
        self.set_auto_deregister(True)
        self.set_reset_machine(False)

    def runTest(self):
        # Prepare package for /33629/0
        self.FW_PKG_OPTS = {'magic': b'AJAY_APP', 'version': 2}
        self.provide_response_app_img(use_real_app=True)

        # Write /33629/0/1 (Package URI)
        self.write_firmware_and_wait_for_download(Instances.APP,
                                                  self.get_firmware_uri())

        # Check /33629/0 state and result
        self.assertEqual(UpdateState.DOWNLOADED, self.read_state(Instances.APP))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.APP))

        # Prepare package for /33629/1
        self.FW_PKG_OPTS = {'magic': b'AJAY_TEE', 'version': 2}
        self.prepare_package_additional_img(content=DUMMY_FILE)

        # Write /33629/1/0 (Firmware)
        req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.TEE].Package,
                         self.PACKAGE,
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Check /33629/1 state and result
        self.assertEqual(UpdateState.DOWNLOADED, self.read_state(Instances.TEE))
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.TEE))

        # Execute /33629/1/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.TEE].Update,
                           content=b'0=\'</33629/0>\'')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Demo should reboot, it needs new server
        self.serv.reset()
        self.assertDemoRegisters()

        # After init state should be IDLE in both instances
        self.wait_until_state_is(Instances.APP, UpdateState.IDLE)
        self.wait_until_state_is(Instances.TEE, UpdateState.IDLE)

        # Check both results
        self.assertEqual(UpdateResult.SUCCESS,
                         self.read_update_result(Instances.APP))
        self.assertEqual(UpdateResult.SUCCESS,
                         self.read_update_result(Instances.TEE))


class AdvancedFirmwareUpdateCancelWhileDownloadQueued(AdvancedFirmwareUpdate.TestWithCoapServer):
    def setUp(self, coap_server=None, *args, **kwargs):
        super().setUp(coap_server=[None, None], *args, **kwargs)

    def runTest(self):
        # Prepare package for /33629/0
        self.FW_PKG_OPTS = {'magic': b'AJAY_APP', 'version': 2}
        self.prepare_package_app_img(use_real_app=True)
        with self.get_file_server(serv=0) as file_server:
            file_server.set_resource('/firmwareAPP',
                                     self.PACKAGE)
            fw_uri1 = file_server.get_resource_uri('/firmwareAPP')

        # Prepare package for /33629/1
        self.FW_PKG_OPTS = {'magic': b'AJAY_TEE', 'version': 2}
        self.prepare_package_additional_img(content=DUMMY_FILE)
        with self.get_file_server(serv=1) as file_server:
            file_server.set_resource('/firmwareTEE',
                                     self.PACKAGE)
            fw_uri2 = file_server.get_resource_uri('/firmwareTEE')

        # Write /33629/0/1 (Firmware URI)
        req1 = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI,
                          fw_uri1)
        self.serv.send(req1)

        # Write /33629/1/1 (Firmware URI)
        req2 = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.TEE].PackageURI,
                          fw_uri2)
        self.serv.send(req2)

        # There should be two request already received
        self.assertMsgEqual(Lwm2mChanged.matching(req1)(),
                            self.serv.recv())
        self.assertMsgEqual(Lwm2mChanged.matching(req2)(),
                            self.serv.recv())

        # Wait for download to start
        self.wait_until_state_is(Instances.APP, UpdateState.DOWNLOADING)
        # TEE URI is going to be queued but its state should be DOWNLOADING as well
        self.wait_until_state_is(Instances.TEE, UpdateState.DOWNLOADING)

        # Execute /33629/0/10 (Cancel)
        req1 = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Cancel)
        self.serv.send(req1)

        # Execute /33629/1/10 (Cancel)
        req2 = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.TEE].Cancel)
        self.serv.send(req2)

        # Check APP instance abort
        if self.read_log_until_match(regex=re.escape(b'Aborted ongoing download for instance 0'),
                                     timeout_s=5) is None:
            raise self.failureException(
                'string not found')

        # Download for instance TEE should start then
        if self.read_log_until_match(regex=re.escape(b'Scheduled download for instance 1'),
                                     timeout_s=5) is None:
            raise self.failureException(
                'string not found')

        # Check TEE instance abort
        if self.read_log_until_match(regex=re.escape(b'Aborted ongoing download for instance 1'),
                                     timeout_s=5) is None:
            raise self.failureException(
                'string not found')

        # There should be two request already received
        self.assertMsgEqual(Lwm2mChanged.matching(req1)(),
                            self.serv.recv())
        self.assertMsgEqual(Lwm2mChanged.matching(req2)(),
                            self.serv.recv())


        # Check states and results
        self.assertEqual(UpdateState.IDLE, self.read_state(Instances.APP))
        self.assertEqual(UpdateResult.CANCELLED,
                         self.read_update_result(Instances.APP))
        self.assertEqual(UpdateState.IDLE, self.read_state(Instances.TEE))
        self.assertEqual(UpdateResult.CANCELLED,
                         self.read_update_result(Instances.TEE))


class AdvancedFirmwareUpdateCancelCurrentDownloadAndLeaveSecondOne(AdvancedFirmwareUpdate.TestWithCoapServer):
    def setUp(self, coap_server=None, *args, **kwargs):
        super().setUp(coap_server=[None, None], *args, **kwargs)

    def runTest(self):
        # Prepare package for /33629/0
        self.FW_PKG_OPTS = {'magic': b'AJAY_APP', 'version': 2}
        self.prepare_package_app_img(use_real_app=True)
        with self.get_file_server(serv=0) as file_server:
            file_server.set_resource('/firmwareAPP',
                                     self.PACKAGE)
            fw_uri1 = file_server.get_resource_uri('/firmwareAPP')

        # Prepare package for /33629/1
        self.FW_PKG_OPTS = {'magic': b'AJAY_TEE', 'version': 2}
        self.prepare_package_additional_img(content=DUMMY_FILE)
        with self.get_file_server(serv=1) as file_server:
            file_server.set_resource('/firmwareTEE',
                                     self.PACKAGE)
            fw_uri2 = file_server.get_resource_uri('/firmwareTEE')

        # Write /33629/0/1 (Firmware URI)
        req1 = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI,
                          fw_uri1)
        self.serv.send(req1)

        # Write /33629/1/1 (Firmware URI)
        req2 = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.TEE].PackageURI,
                          fw_uri2)
        self.serv.send(req2)

        # There should be two request already received
        self.assertMsgEqual(Lwm2mChanged.matching(req1)(),
                            self.serv.recv())
        self.assertMsgEqual(Lwm2mChanged.matching(req2)(),
                            self.serv.recv())

        # Wait for download to start
        self.wait_until_state_is(Instances.APP, UpdateState.DOWNLOADING)
        # TEE URI is going to be queued but its state should be DOWNLOADING as well
        self.wait_until_state_is(Instances.TEE, UpdateState.DOWNLOADING)

        # Execute /33629/0/10 (Cancel)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Cancel)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Check states and results
        self.assertEqual(UpdateState.IDLE, self.read_state(Instances.APP))
        self.assertEqual(UpdateResult.CANCELLED,
                         self.read_update_result(Instances.APP))
        self.wait_until_state_is(Instances.TEE, UpdateState.DOWNLOADED)
        self.assertEqual(UpdateResult.INITIAL,
                         self.read_update_result(Instances.TEE))


class AdvancedFirmwareUpdateHttpRequestTimeoutTest(AdvancedFirmwareUpdate.TestWithPartialDownload,
                                                   AdvancedFirmwareUpdate.TestWithHttpServer):
    CHUNK_SIZE = 500
    RESPONSE_DELAY = 0.5
    TCP_REQUEST_TIMEOUT = 5

    def setUp(self):
        super().setUp(
            extra_cmdline_args=['--afu-tcp-request-timeout', str(self.TCP_REQUEST_TIMEOUT)])

    def runTest(self):
        self.provide_response()

        # Write /33629/0/1 (Firmware URI)
        req = Lwm2mWrite(ResPath.AdvancedFirmwareUpdate[Instances.APP].PackageURI,
                         self.get_firmware_uri())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.wait_for_half_download()
        # Change RESPONSE_DELAY so that the server stops responding
        self.RESPONSE_DELAY = self.TCP_REQUEST_TIMEOUT + 5

        half_download_time = time.time()
        self.wait_until_state_is(Instances.APP, UpdateState.IDLE,
                                 timeout_s=self.TCP_REQUEST_TIMEOUT + 5)
        fail_time = time.time()
        self.assertEqual(self.read_update_result(Instances.APP), UpdateResult.CONNECTION_LOST)

        self.assertAlmostEqual(fail_time, half_download_time + self.TCP_REQUEST_TIMEOUT, delta=1.5)


class AdvancedFirmwareUpdateHttpRequestTimeoutTest20sec(
    AdvancedFirmwareUpdateHttpRequestTimeoutTest):
    TCP_REQUEST_TIMEOUT = 20
