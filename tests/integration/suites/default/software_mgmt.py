# -*- coding: utf-8 -*-
#
# Copyright 2017-2024 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.
import asyncio
import os
import re
import threading
import unittest
import ssl

from framework.coap_file_server import CoapFileServer
from framework.lwm2m_test import *
from .firmware_update import FirmwareUpdate
from .block_write import Block, equal_chunk_splitter, msg_id_generator


@enum.unique
class SoftwareUpdateForcedError(enum.IntEnum):
    NoError = 0
    FailedInstall = 1
    DelayedSuccessInstall = 2
    DelayedFailedInstall = 3
    SuccessInPerformInstall = 4
    SuccessInPerformInstallActivate = 5
    FailureInPerformInstall = 6
    FailureInPerformUninstall = 7
    FailureInPerformActivate = 8
    FailureInPerformDeactivate = 9
    FailureInPerformPrepareForUpdate = 10
    DoNothing = 11


def make_software_package(binary: bytes,
                          magic: bytes = b'ANJAY_SW',
                          crc: Optional[int] = None,
                          force_error: SoftwareUpdateForcedError = SoftwareUpdateForcedError.NoError,
                          version: int = 1):
    assert len(magic) == 8

    if crc is None:
        crc = binascii.crc32(binary)

    return struct.pack('>8sHHI', magic, version, force_error, crc) + binary


class ActivationState:
    DEACTIVATED = 0
    ACTIVATED = 1


class UpdateState:
    INITIAL = 0
    DOWNLOAD_STARTED = 1
    DOWNLOADED = 2
    DELIVERED = 3
    INSTALLED = 4


class UpdateResult:
    INITIAL = 0
    DOWNLOADING = 1
    INSTALLED = 2
    DOWNLOADED_VERIFIED = 3
    NOT_ENOUGH_SPACE = 50
    OUT_OF_MEMORY = 51
    CONNECTION_LOST = 52
    INTEGRITY_FAILURE = 53
    UNSUPPORTED_PACKAGE_TYPE = 54
    INVALID_URI = 56
    UPDATE_ERROR = 57
    INSTALLATION_FAILURE = 58
    UNINSTALLATION_FAILURE = 59


SOFTWARE_PATH = '/software'
SOFTWARE_SCRIPT_TEMPLATE = '#!/bin/sh\n%secho installed > "%s"\n'

INSTANCE_COUNT = 2


def packets_from_chunks(chunks, process_options=None,
                        path=ResPath.SoftwareManagement[0].Package,
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


class SoftwareManagement:
    class Test(test_suite.Lwm2mSingleServerTest):
        SW_PKG_OPTS = {}

        def set_auto_deregister(self, auto_deregister):
            self.auto_deregister = auto_deregister

        def set_check_marker(self, check_marker):
            self.check_marker = check_marker

        def setUp(self, garbage=0, auto_remove=True, *args, **kwargs):
            garbage_lines = ''
            while garbage > 0:
                garbage_line = '#' * (min(garbage, 80) - 1) + '\n'
                garbage_lines += garbage_line
                garbage -= len(garbage_line)
            self.ANJAY_MARKER_FILE = generate_temp_filename(
                dir='/tmp', prefix='anjay-sw-updated-')
            self.ANJAY_PERSISTENCE_FILE = generate_temp_filename(
                dir='/tmp', prefix='anjay-sw-persistence-')
            self.SOFTWARE_SCRIPT_CONTENT = \
                (SOFTWARE_SCRIPT_TEMPLATE %
                 (garbage_lines, self.ANJAY_MARKER_FILE)).encode('ascii')
            if auto_remove:
                self.SOFTWARE_SCRIPT_CONTENT += 'rm "$0"\n'.encode('ascii')
            super().setUp(sw_mgmt_persistence_file=self.ANJAY_PERSISTENCE_FILE, *args, **kwargs)

        def tearDown(self):
            auto_deregister = getattr(self, 'auto_deregister', True)
            check_marker = getattr(self, 'check_marker', False)

            try:
                if not check_marker:
                    return
                for _ in range(10):
                    time.sleep(0.5)

                    if os.path.isfile(self.ANJAY_MARKER_FILE):
                        break
                else:
                    self.fail('software marker not created')
                with open(self.ANJAY_MARKER_FILE, "rb") as f:
                    line = f.readline()[:-1]
                    self.assertEqual(line, b"installed")
                os.unlink(self.ANJAY_MARKER_FILE)
            finally:
                super().tearDown(auto_deregister=auto_deregister)

        def read_update_result(self, inst: int = 0):
            req = Lwm2mRead(ResPath.SoftwareManagement[inst].UpdateResult)
            self.serv.send(req)
            res = self.serv.recv()
            self.assertMsgEqual(Lwm2mContent.matching(req)(), res)
            return int(res.content)

        def read_state(self, inst: int = 0):
            req = Lwm2mRead(ResPath.SoftwareManagement[inst].UpdateState)
            self.serv.send(req)
            res = self.serv.recv()
            self.assertMsgEqual(Lwm2mContent.matching(req)(), res)
            return int(res.content)

        def read_activation_state(self, inst: int = 0):
            req = Lwm2mRead(ResPath.SoftwareManagement[inst].ActivationState)
            self.serv.send(req)
            res = self.serv.recv()
            self.assertMsgEqual(Lwm2mContent.matching(req)(), res)
            return int(res.content)

        def wait_for_download(self, download_timeout_s=20, inst: int = 0):
            # wait until client downloads and verify the software
            deadline = time.time() + download_timeout_s
            while time.time() < deadline:
                time.sleep(0.5)

                if self.read_state(inst) == UpdateState.DELIVERED:
                    return

            self.fail('software still not downloaded')

        def write_software_and_wait_for_download(self, software_uri: str,
                                                 download_timeout_s=20, inst: int = 0):
            # Write /9/0/3 (Package URI)
            self.write_software_uri_expect_success(software_uri, inst)

            self.wait_for_download(download_timeout_s, inst)

        def write_software_uri_expect_success(self, software_uri, inst: int = 0):
            req = Lwm2mWrite(
                ResPath.SoftwareManagement[inst].PackageURI, software_uri)
            self.serv.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                                self.serv.recv())

        def perform_software_install_expect_success(self, inst: int = 0):
            req = Lwm2mExecute(ResPath.SoftwareManagement[inst].Install)
            self.serv.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                                self.serv.recv())

        def perform_software_uninstall_expect_success(self, inst: int = 0, content=b''):
            req = Lwm2mExecute(
                ResPath.SoftwareManagement[inst].Uninstall, content=content)
            self.serv.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                                self.serv.recv())

        def wait_until_state_is(self, state, timeout_s=10, inst: int = 0):
            deadline = time.time() + timeout_s
            while time.time() < deadline:
                time.sleep(0.1)
                if self.read_state(inst) == state:
                    return

            self.fail(f'state still is not {state}')

        def wait_until_result_is(self, result, timeout_s=10, inst: int = 0):
            deadline = time.time() + timeout_s
            while time.time() < deadline:
                time.sleep(0.1)
                if self.read_update_result(inst) == result:
                    return

            self.fail(f'result still is not {result}')

    class TestWithHttpServer(FirmwareUpdate.TestWithHttpServerMixin, Test):
        PATH = SOFTWARE_PATH

        def get_software_uri(self):
            return super().get_firmware_uri()

        def provide_response(self, other_content: bytes = None):
            with self._response_cv:
                self.assertIsNone(self._response_content)
                if other_content:
                    self._response_content = make_software_package(
                        other_content, **self.SW_PKG_OPTS)
                else:
                    self._response_content = make_software_package(
                        self.SOFTWARE_SCRIPT_CONTENT, **self.SW_PKG_OPTS)
                self._response_cv.notify()

    class TestWithTlsServer(FirmwareUpdate.TestWithTlsServerMixin, Test):
        def setUp(self, pass_cert_to_demo=True, cmd_arg='', **kwargs):
            super().setUp(pass_cert_to_demo, cmd_arg='--sw-mgmt-cert-file', **kwargs)

    class TestWithHttpsServer(TestWithTlsServer, FirmwareUpdate.TestWithHttpsServerMixin, TestWithHttpServer):
        def get_software_uri(self):
            return super().get_firmware_uri()

    class TestWithCoapServer(FirmwareUpdate.TestWithCoapServerMixin, Test):
        pass

    class TestWithCoapsServer(FirmwareUpdate.TestWithCoapServerMixin, Test):
        SW_PSK_IDENTITY = b'sw-mgmt-psk-identity'
        SW_PSK_KEY = b'sw-mgmt-psk-key'

        def setUp(self, coap_server_class=coap.DtlsServer, extra_cmdline_args=None, *args, **kwargs):
            extra_cmdline_args = (extra_cmdline_args or []) + ['--sw-mgmt-psk-identity',
                                                               str(binascii.hexlify(
                                                                   self.SW_PSK_IDENTITY), 'ascii'),
                                                               '--sw-mgmt-psk-key', str(binascii.hexlify(
                                                                   self.SW_PSK_KEY), 'ascii')]
            super().setUp(*args, coap_server=coap_server_class(psk_identity=self.SW_PSK_IDENTITY,
                                                               psk_key=self.SW_PSK_KEY),
                          extra_cmdline_args=extra_cmdline_args, **kwargs)

    class TestWithPartialDownload:
        GARBAGE_SIZE = 8000

        def wait_for_half_download(self):
            # roughly twice the time expected as per SlowServer
            deadline = time.time() + self.GARBAGE_SIZE / 500
            fsize = 0
            while time.time() < deadline:
                time.sleep(0.5)
                fsize = os.stat(self.sw_file_name).st_size
                if fsize * 2 > self.GARBAGE_SIZE:
                    break
            if fsize * 2 <= self.GARBAGE_SIZE:
                self.fail('software image not downloaded fast enough')
            elif fsize > self.GARBAGE_SIZE:
                self.fail('software image downloaded too quickly')

        def setUp(self, *args, **kwargs):
            super().setUp(garbage=self.GARBAGE_SIZE, *args, **kwargs)

            import tempfile

            with tempfile.NamedTemporaryFile(delete=False) as f:
                self.sw_file_name = f.name
            self.communicate('set-sw-mgmt-package-path 0 %s' %
                             (os.path.abspath(self.sw_file_name)))

    class TestWithPartialDownloadAndRestart(
            TestWithPartialDownload, FirmwareUpdate.DemoArgsExtractorMixin):
        def setUp(self, *args, **kwargs):
            if 'auto_remove' not in kwargs:
                kwargs = {**kwargs, 'auto_remove': False}
            super().setUp(*args, **kwargs)

        def tearDown(self):
            try:
                with open(self.sw_file_name, "rb") as f:
                    self.assertEqual(f.read(), self.SOFTWARE_SCRIPT_CONTENT)
                super().tearDown()
            finally:
                try:
                    os.unlink(self.sw_file_name)
                except FileNotFoundError:
                    pass

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
                file_server.set_resource(self.PATH,
                                         make_software_package(self.SOFTWARE_SCRIPT_CONTENT))
                self.sw_uri = file_server.get_resource_uri(self.PATH)

    class TestWithPartialCoapsDownloadAndRestart(TestWithPartialDownloadAndRestart,
                                                 TestWithCoapsServer):
        def setUp(self):
            class SlowServer(coap.DtlsServer):
                def send(self, *args, **kwargs):
                    time.sleep(0.5)
                    return super().send(*args, **kwargs)

            super().setUp(coap_server_class=SlowServer)

            with self.file_server as file_server:
                file_server.set_resource(self.PATH,
                                         make_software_package(self.SOFTWARE_SCRIPT_CONTENT))
                self.sw_uri = file_server.get_resource_uri(self.PATH)

    class TestWithPartialHttpDownloadAndRestart(FirmwareUpdate.TestWithPartialHttpDownloadAndRestartMixin,
                                                TestWithPartialDownloadAndRestart,
                                                TestWithHttpServer):
        pass

    class BlockTest(Test, Block.Test):
        def block_init_file(self):
            import tempfile

            with tempfile.NamedTemporaryFile(delete=False) as f:
                sw_file_name = f.name
            self.communicate(
                'set-sw-mgmt-package-path 0 %s' % (os.path.abspath(sw_file_name)))
            return sw_file_name

        def block_send(self, data, splitter, **make_software_package_args):
            sw_file_name = self.block_init_file()

            make_software_package_args.update(self.SW_PKG_OPTS)
            chunks = list(splitter(
                make_software_package(data, **make_software_package_args)))

            for request in packets_from_chunks(chunks):
                self.serv.send(request)
                response = self.serv.recv()
                self.assertIsSuccessResponse(response, request)

            self.wait_until_state_is(state=UpdateState.DELIVERED)

            with open(sw_file_name, 'rb') as sw_file:
                self.assertEqual(sw_file.read(), data)

            self.files_to_cleanup.append(sw_file_name)

        def tearDown(self):
            for file in self.files_to_cleanup:
                try:
                    os.unlink(file)
                except FileNotFoundError:
                    pass

            super(Block.Test, self).tearDown()


class SoftwareManagementPackageTest(SoftwareManagement.Test):
    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(True)

    def runTest(self):
        # Write /9/0/2 (Software): script content
        req = Lwm2mWrite(ResPath.SoftwareManagement[0].Package,
                         make_software_package(
                             self.SOFTWARE_SCRIPT_CONTENT, magic=b'ANJAY_SW'),
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())
        self.wait_until_state_is(UpdateState.DELIVERED)
        # Execute /9/0/4 (Install)
        self.perform_software_install_expect_success()


class SoftwareManagementUriTest(SoftwareManagement.TestWithHttpServer):
    def setUp(self, *args, **kwargs):
        super().setUp(*args, **kwargs)
        self.set_check_marker(True)
        self.set_auto_deregister(True)

    def runTest(self):
        self.provide_response()
        self.write_software_and_wait_for_download(self.get_software_uri())

        # Execute /9/0/4 (Install)
        self.perform_software_install_expect_success()


class SoftwareManagementUriAutoSuspend(SoftwareManagementUriTest):
    def setUp(self):
        super().setUp(extra_cmdline_args=['--sw-mgmt-auto-suspend'])


class SoftwareManagementUriManualSuspend(SoftwareManagement.TestWithHttpServer):
    def runTest(self):
        self.provide_response()

        requests = list(self.requests)
        software_uri = self.get_software_uri()

        self.communicate('sw-mgmt-suspend')

        # Write /9/0/3 (Package URI)
        self.write_software_uri_expect_success(software_uri)

        # wait until client enters the DOWNLOAD STARTED state
        deadline = time.time() + 20
        while time.time() < deadline:
            time.sleep(0.5)
            if self.read_state() == UpdateState.DOWNLOAD_STARTED:
                break
        else:
            self.fail('software still not in DOWNLOAD STARTED state')

        time.sleep(5)
        self.assertEqual(requests, self.requests)

        # resume the download
        self.communicate('sw-mgmt-reconnect')

        self.wait_for_download()

        self.assertEqual(requests + [SOFTWARE_PATH], self.requests)


class SoftwareManagementTryActivateInWrongState(SoftwareManagement.TestWithHttpServer):
    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(True)

    def try_activate_deactivate(self):
        req = Lwm2mExecute(ResPath.SoftwareManagement[0].Activate)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_METHOD_NOT_ALLOWED),
                            self.serv.recv())

        req = Lwm2mExecute(ResPath.SoftwareManagement[0].Deactivate)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_METHOD_NOT_ALLOWED),
                            self.serv.recv())

    def runTest(self):
        self.try_activate_deactivate()

        # Write /9/0/3 (Package URI)
        self.write_software_uri_expect_success(self.get_software_uri())

        self.wait_until_state_is(UpdateState.DOWNLOAD_STARTED)
        self.try_activate_deactivate()

        self.provide_response()

        self.wait_until_state_is(UpdateState.DELIVERED)
        self.try_activate_deactivate()

        # Execute /9/0/4 (Install)
        self.perform_software_install_expect_success()

        req = Lwm2mExecute(ResPath.SoftwareManagement[0].Activate)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        req = Lwm2mExecute(ResPath.SoftwareManagement[0].Deactivate)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())


class SoftwareManagementStateAndResultChangeMixin:
    update_state_observe_token = 0
    update_result_observe_token = 0
    activation_state_observe_token = 0

    def check_notifications(self, update_state=None, update_result=None, activation_state=None):

        count_of_arg = sum(1 for arg in (
            update_state, update_result, activation_state) if arg is not None)

        for _ in range(count_of_arg):
            recv = self.serv.recv()
            if update_state is not None and recv.token == self.update_state_observe_token:
                self.assertMsgEqual(Lwm2mNotify(self.update_state_observe_token, str(update_state).encode()),
                                    recv)
            elif update_result is not None and recv.token == self.update_result_observe_token:
                self.assertMsgEqual(Lwm2mNotify(self.update_result_observe_token, str(update_result).encode()),
                                    recv)
            elif activation_state is not None and recv.token == self.activation_state_observe_token:
                self.assertMsgEqual(Lwm2mNotify(self.activation_state_observe_token, str(activation_state).encode()),
                                    recv)
            else:
                self.fail('unexpected token')

    def runTest(self):
        # disable minimum notification period
        write_attrs_req = Lwm2mWriteAttributes(
            ResPath.SoftwareManagement[0].UpdateState, query=['pmin=0'])
        self.serv.send(write_attrs_req)
        self.assertMsgEqual(Lwm2mChanged.matching(
            write_attrs_req)(), self.serv.recv())

        # initial state should be 0
        observe_req = Lwm2mObserve(ResPath.SoftwareManagement[0].UpdateState)
        self.serv.send(observe_req)
        self.assertMsgEqual(Lwm2mContent.matching(
            observe_req)(content=str(UpdateState.INITIAL).encode()), self.serv.recv())
        self.update_state_observe_token = observe_req.token

        observe_req = Lwm2mObserve(ResPath.SoftwareManagement[0].UpdateResult)
        self.serv.send(observe_req)
        self.assertMsgEqual(Lwm2mContent.matching(
            observe_req)(content=str(UpdateResult.INITIAL).encode()), self.serv.recv())
        self.update_result_observe_token = observe_req.token

        # we shouldn't get any notification from this resource in this test
        observe_req = Lwm2mObserve(
            ResPath.SoftwareManagement[0].ActivationState)
        self.serv.send(observe_req)
        self.assertMsgEqual(Lwm2mContent.matching(
            observe_req)(content=str(ActivationState.DEACTIVATED).encode()), self.serv.recv())
        self.activation_state_observe_token = observe_req.token

    def check_recv(self):
        try:
            self.serv.recv(timeout_s=0.5)
            self.fail('we should not receive anything at this point')
        except socket.timeout:
            pass


class SoftwareManagementStateAndResultChangeTest(SoftwareManagementStateAndResultChangeMixin, SoftwareManagement.TestWithHttpServer):
    install = True
    activate = False
    uninstall = True

    def setUp(self, *args, **kwargs):
        super().setUp(*args, **kwargs)
        if self.install:
            self.set_check_marker(True)
        self.set_auto_deregister(True)

    def runTest(self):
        super().runTest()

        # Write /9/0/3 (Package URI)
        self.write_software_uri_expect_success(self.get_software_uri())

        # notifications should be sent before downloading
        self.check_notifications(
            UpdateState.DOWNLOAD_STARTED, UpdateResult.DOWNLOADING)

        self.provide_response()

        # ... after it finishes
        self.check_notifications(UpdateState.DOWNLOADED, UpdateResult.INITIAL)

        # ... after package validation
        self.assertMsgEqual(Lwm2mNotify(self.update_state_observe_token, str(UpdateState.DELIVERED).encode()),
                            self.serv.recv())

        if self.install:
            # Execute /9/0/4 (Install)
            self.perform_software_install_expect_success()

            # ... after installation
            self.check_notifications(
                UpdateState.INSTALLED, UpdateResult.INSTALLED)

        if self.activate:
            req = Lwm2mExecute(ResPath.SoftwareManagement[0].Activate)
            self.serv.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                                self.serv.recv())

            # notification should be sent after activation
            self.assertMsgEqual(Lwm2mNotify(self.activation_state_observe_token, str(ActivationState.ACTIVATED).encode()),
                                self.serv.recv())

        if self.uninstall:
            # Execute /9/0/6 (Uninstall)
            self.perform_software_uninstall_expect_success()

            # ... and after uninstallation
            self.check_notifications(UpdateState.INITIAL, UpdateResult.INITIAL)

        self.check_recv()

        # there should be exactly one request
        self.assertEqual([SOFTWARE_PATH], self.requests)


class SoftwareManagementActivateChangeTest(SoftwareManagementStateAndResultChangeTest):
    install = True
    activate = True
    uninstall = False

    def runTest(self):
        super().runTest()

        req = Lwm2mExecute(ResPath.SoftwareManagement[0].Deactivate)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # ... and after deactivation
        self.assertMsgEqual(Lwm2mNotify(self.activation_state_observe_token, str(ActivationState.DEACTIVATED).encode()),
                            self.serv.recv())

        # Execute /9/0/6 (Uninstall)
        self.perform_software_uninstall_expect_success()

        # after deinstallation we should get UpdateState and UpdateResult notification
        self.check_notifications(UpdateState.INITIAL, UpdateResult.INITIAL)

        self.check_recv()


class SoftwareManagementUninstallFromDeliveredTest(SoftwareManagementStateAndResultChangeTest):
    install = False
    activate = False
    uninstall = False

    def runTest(self):
        super().runTest()

        result = self.read_update_result()
        # Execute /9/0/6 (Uninstall)
        self.perform_software_uninstall_expect_success()

        # after deinstallation we should get UpdateState notification
        self.check_notifications(UpdateState.INITIAL)

        self.assertEqual(result, self.read_update_result())

        self.check_recv()


class SoftwareManagementActivateUninstallFromInstalledTest(SoftwareManagementStateAndResultChangeTest):
    install = True
    activate = True
    uninstall = False

    def runTest(self):
        super().runTest()

        # Execute /9/0/6 (Uninstall)
        self.perform_software_uninstall_expect_success()

        # after deinstallation we should get UpdateState, UpdateResult and ActivationState notification
        # Note: We did not find find any mention in the documentation about changing the status of resource 12,
        # but it seems to us right to expect it to change to DEACTIVATED in such a case.
        self.check_notifications(
            UpdateState.INITIAL, UpdateResult.INITIAL, ActivationState.DEACTIVATED)

        self.check_recv()


class SoftwareManagementInstallSetFailedTest(SoftwareManagementStateAndResultChangeTest):
    install = False
    activate = False
    uninstall = False

    def setUp(self):
        super().setUp()
        self.SW_PKG_OPTS = {
            'force_error': SoftwareUpdateForcedError.FailureInPerformInstall}

    def runTest(self):
        super().runTest()

        # Execute /9/0/4 (Install)
        self.perform_software_install_expect_success()

        # ... after installation
        self.check_notifications(
            update_result=UpdateResult.INSTALLATION_FAILURE)

        self.check_recv()


class SoftwareManagementInstallFailedTest(SoftwareManagementStateAndResultChangeTest):
    install = False
    activate = False
    uninstall = False

    def setUp(self):
        super().setUp()
        self.SW_PKG_OPTS = {
            'force_error': SoftwareUpdateForcedError.FailedInstall}

    def runTest(self):
        super().runTest()

        # Execute /9/0/4 (Install)
        self.perform_software_install_expect_success()

        # ... after installation
        self.check_notifications(
            update_result=UpdateResult.INSTALLATION_FAILURE)

        self.check_recv()


class SoftwareManagementUninstallFromInstallStateFailedTest(SoftwareManagementStateAndResultChangeTest):
    install = True
    activate = False
    uninstall = False

    def setUp(self):
        super().setUp()
        self.SW_PKG_OPTS = {
            'force_error': SoftwareUpdateForcedError.FailureInPerformUninstall}

    def runTest(self):
        super().runTest()

        # Execute /9/0/6 (Uninstall)
        # Note: There is UNINSTALLATION_FAILURE (Uninstallation Failure during forUpdate(arg=0)) UpdateResult,
        # but the OMA documentation doesn't describe a failure uninstallation transition, so we stick to our implementation.
        req = Lwm2mExecute(ResPath.SoftwareManagement[0].Uninstall)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_INTERNAL_SERVER_ERROR),
                            self.serv.recv())

        self.check_recv()


class SoftwareManagementUninstallFromDeliveredWithArgTest(SoftwareManagementStateAndResultChangeTest):
    install = False
    activate = False
    uninstall = False

    def runTest(self):
        super().runTest()

        result = self.read_update_result()

        # Execute /9/0/6 (Uninstall)
        self.perform_software_uninstall_expect_success(content=b'0')

        # after deinstallation we should get UpdateState notification
        self.check_notifications(UpdateState.INITIAL)

        self.assertEqual(result, self.read_update_result())

        self.check_recv()


class SoftwareManagementUninstallFromInstalledWithArgTest(SoftwareManagementStateAndResultChangeTest):
    install = True
    activate = False
    uninstall = False

    def runTest(self):
        super().runTest()

        # Execute /9/0/6 (Uninstall)
        self.perform_software_uninstall_expect_success(content=b'0')

        # after deinstallation we should get UpdateState notification
        self.check_notifications(UpdateState.INITIAL, UpdateResult.INITIAL)

        self.check_recv()


class SoftwareManagementActivateUninstallFromInstalledWithArgTest(SoftwareManagementStateAndResultChangeTest):
    install = True
    activate = True
    uninstall = False

    def runTest(self):
        super().runTest()

        # Execute /9/0/6 (Uninstall)
        self.perform_software_uninstall_expect_success(content=b'0')

        # after deinstallation we should get UpdateState, UpdateResult and ActivationState notification
        # Note: We did not find find any mention in the documentation about changing the status of resource 12,
        # but it seems to us right to expect it to change to DEACTIVATED in such a case.
        self.check_notifications(
            UpdateState.INITIAL, UpdateResult.INITIAL, ActivationState.DEACTIVATED)

        self.check_recv()


class SoftwareManagementUninstallPrepareForUpdateFromDeliveredTest(SoftwareManagementStateAndResultChangeTest):
    install = False
    activate = False
    uninstall = False

    def runTest(self):
        super().runTest()

        # Execute /9/0/6 (Uninstall)
        req = Lwm2mExecute(
            ResPath.SoftwareManagement[0].Uninstall, content=b'1')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_METHOD_NOT_ALLOWED),
                            self.serv.recv())

        self.check_recv()


class SoftwareManagementUninstallPrepareForUpdateFromInstalledTest(SoftwareManagementStateAndResultChangeTest):
    install = True
    activate = False
    uninstall = False

    def runTest(self):
        super().runTest()

        # Execute /9/0/6 (Uninstall)
        self.perform_software_uninstall_expect_success(content=b'1')

        # after deinstallation we should get UpdateState, UpdateResult and ActivationState notification
        self.check_notifications(UpdateState.INITIAL, UpdateResult.INITIAL)

        self.check_recv()


class SoftwareManagementActivateUninstallPrepareForUpdateFromInstalledTest(SoftwareManagementStateAndResultChangeTest):
    install = True
    activate = True
    uninstall = False

    def runTest(self):
        super().runTest()

        # Execute /9/0/6 (Uninstall)
        self.perform_software_uninstall_expect_success(content=b'1')

        # after deinstallation we should get UpdateState, UpdateResult and ActivationState notification
        # Note: We did not find find any mention in the documentation about changing the status of resource 12,
        # but it seems to us right to expect it to change to DEACTIVATED in such a case.
        self.check_notifications(
            UpdateState.INITIAL, UpdateResult.INITIAL, ActivationState.DEACTIVATED)

        self.check_recv()


class SoftwareManagementUninstallPrepareForUpdateFailedTest(SoftwareManagementStateAndResultChangeTest):
    install = True
    activate = False
    uninstall = False

    def setUp(self):
        super().setUp()
        self.SW_PKG_OPTS = {
            'force_error': SoftwareUpdateForcedError.FailureInPerformPrepareForUpdate}

    def runTest(self):
        super().runTest()
        # Execute /9/0/6 (Uninstall)
        req = Lwm2mExecute(
            ResPath.SoftwareManagement[0].Uninstall, content=b'1')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_METHOD_NOT_ALLOWED),
                            self.serv.recv())

        self.check_recv()


class SoftwareManagementDoubleActivationAllowedTest(SoftwareManagementStateAndResultChangeTest):
    install = True
    activate = True
    uninstall = False

    def runTest(self):
        super().runTest()

        req = Lwm2mExecute(ResPath.SoftwareManagement[0].Activate)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        self.check_recv()


class SoftwareManagementDoubleActivationNotAllowedTest(SoftwareManagementStateAndResultChangeTest):
    install = True
    activate = True
    uninstall = False

    def setUp(self):
        super().setUp(extra_cmdline_args=[
            '--sw-mgmt-disable-repeated-activation-deactivation'])

    def runTest(self):
        super().runTest()

        req = Lwm2mExecute(ResPath.SoftwareManagement[0].Activate)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_METHOD_NOT_ALLOWED),
                            self.serv.recv())

        self.check_recv()


class SoftwareManagementDoubleDeactivationAllowedTest(SoftwareManagementStateAndResultChangeTest):
    install = True
    activate = True
    uninstall = False

    def runTest(self):
        super().runTest()

        req = Lwm2mExecute(ResPath.SoftwareManagement[0].Deactivate)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        self.check_notifications(activation_state=ActivationState.DEACTIVATED)

        req = Lwm2mExecute(ResPath.SoftwareManagement[0].Deactivate)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        self.check_recv()


class SoftwareManagementDoubleDeactivationNotAllowedTest(SoftwareManagementStateAndResultChangeTest):
    install = True
    activate = True
    uninstall = False

    def setUp(self):
        super().setUp(extra_cmdline_args=[
            '--sw-mgmt-disable-repeated-activation-deactivation'])

    def runTest(self):
        super().runTest()

        req = Lwm2mExecute(ResPath.SoftwareManagement[0].Deactivate)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        self.check_notifications(activation_state=ActivationState.DEACTIVATED)

        req = Lwm2mExecute(ResPath.SoftwareManagement[0].Deactivate)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_METHOD_NOT_ALLOWED),
                            self.serv.recv())

        self.check_recv()


class SoftwareManagementPersistenceTest(FirmwareUpdate.DemoArgsExtractorMixin, SoftwareManagement.TestWithHttpServer):
    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(True)

    def restart(self):
        self.teardown_demo_with_servers()
        self.setup_demo_with_servers(
            sw_mgmt_persistence_file=self.ANJAY_PERSISTENCE_FILE)

    def runTest(self):
        self.assertEqual(UpdateState.INITIAL, self.read_state())
        self.assertEqual(UpdateResult.INITIAL, self.read_update_result())
        self.assertEqual(ActivationState.DEACTIVATED,
                         self.read_activation_state())

        self.provide_response()
        self.write_software_and_wait_for_download(self.get_software_uri())

        # Note: Restart with crash before integrity check is tested in SoftwareManagementRestartBeforeIntegrity
        self.wait_until_state_is(UpdateState.DELIVERED)

        self.restart()

        self.assertEqual(UpdateState.DELIVERED, self.read_state())
        self.assertEqual(UpdateResult.INITIAL, self.read_update_result())
        self.assertEqual(ActivationState.DEACTIVATED,
                         self.read_activation_state())

        self.perform_software_install_expect_success()

        self.wait_until_state_is(UpdateState.INSTALLED)

        self.restart()

        self.assertEqual(UpdateState.INSTALLED, self.read_state())
        self.assertEqual(UpdateResult.INSTALLED, self.read_update_result())
        self.assertEqual(ActivationState.DEACTIVATED,
                         self.read_activation_state())

        req = Lwm2mExecute(ResPath.SoftwareManagement[0].Activate)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        self.restart()

        self.assertEqual(UpdateState.INSTALLED, self.read_state())
        self.assertEqual(UpdateResult.INSTALLED, self.read_update_result())
        self.assertEqual(ActivationState.ACTIVATED,
                         self.read_activation_state())

        req = Lwm2mExecute(ResPath.SoftwareManagement[0].Deactivate)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        self.restart()

        self.assertEqual(UpdateState.INSTALLED, self.read_state())
        self.assertEqual(UpdateResult.INSTALLED, self.read_update_result())
        self.assertEqual(ActivationState.DEACTIVATED,
                         self.read_activation_state())

        self.perform_software_uninstall_expect_success()

        self.restart()

        self.assertEqual(UpdateState.INITIAL, self.read_state())
        self.assertEqual(UpdateResult.INITIAL, self.read_update_result())
        self.assertEqual(ActivationState.DEACTIVATED,
                         self.read_activation_state())


class SoftwareManagementAddNewInstanceTest(SoftwareManagement.TestWithHttpServer, test_suite.Lwm2mDmOperations):
    def setUp(self, *args, **kwargs):
        super().setUp(*args, **kwargs)
        self.set_check_marker(True)
        self.set_auto_deregister(True)

    def runTest(self):
        self.create_instance(self.serv, oid=OID.SoftwareManagement, iid=2)
        self.create_instance(self.serv, oid=OID.SoftwareManagement,
                             iid=3, expect_error_code=coap.Code.RES_METHOD_NOT_ALLOWED)

        self.provide_response()
        self.write_software_and_wait_for_download(
            self.get_software_uri(), inst=2)

        # Execute /9/2/4 (Install)
        self.perform_software_install_expect_success(inst=2)


class SoftwareManagementRemoveInstanceTest(SoftwareManagement.TestWithHttpServer, test_suite.Lwm2mDmOperations):
    def setUp(self, *args, **kwargs):
        super().setUp(*args, **kwargs)
        self.set_check_marker(True)
        self.set_auto_deregister(True)

    def runTest(self):
        self.delete_instance(self.serv, oid=OID.SoftwareManagement, iid=0)

        self.provide_response()
        self.write_software_and_wait_for_download(
            self.get_software_uri(), inst=1)

        # Execute /9/1/4 (Install)
        self.perform_software_install_expect_success(inst=1)


class SoftwareManagementBadBase64(SoftwareManagement.Test):
    def runTest(self):
        # Write /9/0/2 (Software): some random text to see how it makes the world burn
        # (as text context does not implement some_bytes handler).
        data = bytes(b'\x01' * 16)
        req = Lwm2mWrite(
            ResPath.SoftwareManagement[0].Package, data, format=coap.ContentFormat.TEXT_PLAIN)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_BAD_REQUEST),
                            self.serv.recv())


class SoftwareManagementGoodBase64(SoftwareManagement.Test):
    def runTest(self):
        import base64
        data = base64.encodebytes(bytes(b'\x01' * 16)).replace(b'\n', b'')
        req = Lwm2mWrite(ResPath.SoftwareManagement[0].Package, data,
                         format=coap.ContentFormat.TEXT_PLAIN)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())


class SoftwareManagementEmptyPkg(SoftwareManagement.TestWithHttpServer):
    def runTest(self):
        self.provide_response()
        self.write_software_and_wait_for_download(self.get_software_uri())

        req = Lwm2mWrite(ResPath.SoftwareManagement[0].Package, b'',
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_METHOD_NOT_ALLOWED),
                            self.serv.recv())

        self.assertEqual(UpdateState.DELIVERED, self.read_state())
        self.assertEqual(UpdateResult.INITIAL, self.read_update_result())


class SoftwareManagementEmptyPkgUri(SoftwareManagement.TestWithHttpServer):
    def runTest(self):
        self.provide_response()
        self.write_software_and_wait_for_download(self.get_software_uri())

        req = Lwm2mWrite(ResPath.SoftwareManagement[0].PackageURI, b'')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_METHOD_NOT_ALLOWED),
                            self.serv.recv())

        self.assertEqual(UpdateState.DELIVERED, self.read_state())
        self.assertEqual(UpdateResult.INITIAL, self.read_update_result())


class SoftwareManagementInvalidUri(SoftwareManagement.Test):
    def runTest(self):
        # observe Result
        observe_req = Lwm2mObserve(ResPath.SoftwareManagement[0].UpdateResult)
        self.serv.send(observe_req)
        self.assertMsgEqual(Lwm2mContent.matching(
            observe_req)(content=b'0'), self.serv.recv())

        # Write /9/0/3 (Package URI)
        self.write_software_uri_expect_success(b'http://invalidsoftware.exe')

        while True:
            notify = self.serv.recv()
            self.assertMsgEqual(Lwm2mNotify(observe_req.token), notify)
            if int(notify.content) != UpdateResult.INITIAL:
                break
        self.serv.send(Lwm2mReset(msg_id=notify.msg_id))
        self.assertEqual(UpdateResult.INVALID_URI, int(notify.content))
        self.assertEqual(UpdateState.INITIAL, self.read_state())


class SoftwareManagementUnsupportedUri(SoftwareManagement.Test):
    def runTest(self):
        req = Lwm2mWrite(ResPath.SoftwareManagement[0].PackageURI,
                         b'unsupported://uri.exe')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_BAD_REQUEST),
                            self.serv.recv())

        # This does not even change state or anything, because according to the LwM2M spec
        # Server can't feed us with unsupported URI type
        self.assertEqual(UpdateState.INITIAL, self.read_state())
        self.assertEqual(UpdateResult.INVALID_URI,
                         self.read_update_result())


class SoftwareManagementOfflineUriTest(SoftwareManagement.TestWithHttpServer):
    def runTest(self):
        self.communicate('enter-offline tcp')

        self.assertEqual(UpdateState.INITIAL, self.read_state())
        self.assertEqual(UpdateResult.INITIAL, self.read_update_result())

        # Write /9/0/3 (Package URI)
        self.write_software_uri_expect_success(self.get_software_uri())

        self.assertEqual(UpdateState.INITIAL, self.read_state())
        self.assertEqual(UpdateResult.CONNECTION_LOST,
                         self.read_update_result())


class SoftwareManagementReplacingPkgUri(SoftwareManagement.TestWithHttpServer):
    def runTest(self):
        self.provide_response()
        self.write_software_and_wait_for_download(self.get_software_uri())

        # This isn't specified anywhere as a possible transition, therefore
        # it is most likely a bad request.
        req = Lwm2mWrite(
            ResPath.SoftwareManagement[0].PackageURI, 'http://something')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_METHOD_NOT_ALLOWED),
                            self.serv.recv())

        self.assertEqual(UpdateState.DELIVERED, self.read_state())
        self.assertEqual(UpdateResult.INITIAL, self.read_update_result())


class SoftwareManagementReplacingPkg(SoftwareManagement.TestWithHttpServer):
    def runTest(self):
        self.provide_response()
        self.write_software_and_wait_for_download(self.get_software_uri())

        # This isn't specified anywhere as a possible transition, therefore
        # it is most likely a bad request.
        req = Lwm2mWrite(ResPath.SoftwareManagement[0].Package, b'trololo',
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_METHOD_NOT_ALLOWED),
                            self.serv.recv())

        self.assertEqual(UpdateState.DELIVERED, self.read_state())
        self.assertEqual(UpdateResult.INITIAL, self.read_update_result())


class SoftwareManagementHttpsReconnectTest(SoftwareManagement.TestWithPartialDownloadAndRestart,
                                           SoftwareManagement.TestWithHttpsServer):
    RESPONSE_DELAY = 0.5
    CHUNK_SIZE = 1000

    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(True)

    def runTest(self):
        self.provide_response()

        # Write /9/0/3 (Package URI)
        self.write_software_uri_expect_success(self.get_software_uri())

        self.wait_for_half_download()

        self.assertEqual(len(self.requests), 1)

        self.communicate('sw-mgmt-reconnect')
        self.provide_response()

        self.wait_for_download()

        self.assertEqual(len(self.requests), 2)

        # Execute /9/0/4 (Install)
        self.perform_software_install_expect_success()


class SoftwareManagementHttpsWritePackageDuringDownloadingTest(SoftwareManagement.TestWithPartialDownload,
                                                               SoftwareManagement.TestWithHttpServer):
    RESPONSE_DELAY = 0.5
    CHUNK_SIZE = 1000

    def runTest(self):
        self.provide_response()

        # Write /9/0/3 (Package URI)
        self.write_software_uri_expect_success(self.get_software_uri())

        self.wait_for_half_download()

        self.assertEqual(self.get_socket_count(), 2)

        req = Lwm2mWrite(ResPath.SoftwareManagement[0].Package, b'',
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_METHOD_NOT_ALLOWED),
                            self.serv.recv())

        self.wait_until_socket_count(expected=2, timeout_s=5)

        self.assertEqual(UpdateState.DOWNLOAD_STARTED, self.read_state())
        self.assertEqual(UpdateResult.DOWNLOADING, self.read_update_result())


class SoftwareManagementHttpsWritePackageUriDuringDownloadingTest(SoftwareManagement.TestWithPartialDownload,
                                                                  SoftwareManagement.TestWithHttpServer):
    RESPONSE_DELAY = 0.5
    CHUNK_SIZE = 1000

    def runTest(self):
        self.provide_response()

        # Write /9/0/3 (Package URI)
        self.write_software_uri_expect_success(self.get_software_uri())

        self.wait_for_half_download()

        self.assertEqual(self.get_socket_count(), 2)

        req = Lwm2mWrite(ResPath.SoftwareManagement[0].PackageURI, b'')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_METHOD_NOT_ALLOWED),
                            self.serv.recv())

        self.wait_until_socket_count(expected=2, timeout_s=5)

        self.assertEqual(UpdateState.DOWNLOAD_STARTED, self.read_state())
        self.assertEqual(UpdateResult.DOWNLOADING, self.read_update_result())


class SoftwareManagementCoapWritePackageUriDuringDownloadingTest(SoftwareManagement.TestWithPartialDownload,
                                                                 SoftwareManagement.TestWithCoapServer):
    def runTest(self):
        with self.file_server as file_server:
            file_server.set_resource(self.PATH,
                                     make_software_package(self.SOFTWARE_SCRIPT_CONTENT))
            sw_uri = file_server.get_resource_uri(self.PATH)

            # Write /9/0/3 (Package URI)
            self.write_software_uri_expect_success(sw_uri)

            # Handle one GET
            file_server.handle_request()

        self.assertEqual(self.get_socket_count(), 2)

        req = Lwm2mWrite(ResPath.SoftwareManagement[0].PackageURI, b'')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_METHOD_NOT_ALLOWED),
                            self.serv.recv())

        self.wait_until_socket_count(expected=2, timeout_s=5)

        self.assertEqual(UpdateState.DOWNLOAD_STARTED, self.read_state())
        self.assertEqual(UpdateResult.DOWNLOADING, self.read_update_result())


class SoftwareManagementHttpsOfflineTest(SoftwareManagement.TestWithPartialDownloadAndRestart,
                                         SoftwareManagement.TestWithHttpServer):
    RESPONSE_DELAY = 0.5
    CHUNK_SIZE = 1000

    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(True)

    def runTest(self):
        self.provide_response()

        # Write /9/0/3 (Package URI)
        self.write_software_uri_expect_success(self.get_software_uri())

        self.wait_for_half_download()

        self.assertEqual(self.get_socket_count(), 2)
        self.communicate('enter-offline tcp')
        self.wait_until_socket_count(expected=1, timeout_s=5)
        self.provide_response()
        self.communicate('exit-offline tcp')

        self.wait_for_download()

        # Execute /9/0/4 (Install)
        self.perform_software_install_expect_success()


class SoftwareManagementHttpsTest(SoftwareManagement.TestWithHttpsServer):
    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(True)

    def runTest(self):
        self.provide_response()
        self.write_software_and_wait_for_download(
            self.get_software_uri(), download_timeout_s=20)

        # Execute /9/0/4 (Install)
        self.perform_software_install_expect_success()


class SoftwareManagementUnconfiguredHttpsTest(SoftwareManagement.TestWithHttpsServer):
    def setUp(self):
        super().setUp(pass_cert_to_demo=False)

    def runTest(self):
        # disable minimum notification period
        write_attrs_req = Lwm2mWriteAttributes(ResPath.SoftwareManagement[0].UpdateResult,
                                               query=['pmin=0'])
        self.serv.send(write_attrs_req)
        self.assertMsgEqual(Lwm2mChanged.matching(
            write_attrs_req)(), self.serv.recv())

        # initial result should be 0
        observe_req = Lwm2mObserve(ResPath.SoftwareManagement[0].UpdateResult)
        self.serv.send(observe_req)
        self.assertMsgEqual(Lwm2mContent.matching(
            observe_req)(content=b'0'), self.serv.recv())

        # Write /9/0/3 (Package URI)
        self.write_software_uri_expect_success(self.get_software_uri())

        # even before reaching the server, we should get an error
        notify_msg = self.serv.recv()
        # no security information => "invalid URI"
        self.assertMsgEqual(Lwm2mNotify(observe_req.token,
                                        str(UpdateResult.INVALID_URI).encode()),
                            notify_msg)
        self.serv.send(Lwm2mReset(msg_id=notify_msg.msg_id))
        self.assertEqual(UpdateState.INITIAL, self.read_state())


class SoftwareManagementUnconfiguredHttpsWithFallbackAttemptTest(
        SoftwareManagement.TestWithHttpsServer):
    def setUp(self):
        super().setUp(pass_cert_to_demo=False,
                      psk_identity=b'test-identity', psk_key=b'test-key')

    def runTest(self):
        # disable minimum notification period
        write_attrs_req = Lwm2mWriteAttributes(ResPath.SoftwareManagement[0].UpdateResult,
                                               query=['pmin=0'])
        self.serv.send(write_attrs_req)
        self.assertMsgEqual(Lwm2mChanged.matching(
            write_attrs_req)(), self.serv.recv())

        # initial result should be 0
        observe_req = Lwm2mObserve(ResPath.SoftwareManagement[0].UpdateResult)
        self.serv.send(observe_req)
        self.assertMsgEqual(Lwm2mContent.matching(
            observe_req)(content=b'0'), self.serv.recv())

        # Write /9/0/3 (Package URI)
        self.write_software_uri_expect_success(self.get_software_uri())

        # even before reaching the server, we should get an error
        notify_msg = self.serv.recv()
        # no security information => client will attempt PSK from data model
        # and fail handshake => "Connection lost"
        self.assertMsgEqual(Lwm2mNotify(observe_req.token,
                                        str(UpdateResult.CONNECTION_LOST).encode()),
                            notify_msg)
        self.serv.send(Lwm2mReset(msg_id=notify_msg.msg_id))
        self.assertEqual(UpdateState.INITIAL, self.read_state())


class SoftwareManagementInvalidHttpsTest(SoftwareManagement.TestWithHttpsServer):
    def setUp(self):
        super().setUp(cn='invalid_cn', alt_ip=None)

    def runTest(self):
        # disable minimum notification period
        write_attrs_req = Lwm2mWriteAttributes(ResPath.SoftwareManagement[0].UpdateResult,
                                               query=['pmin=0'])
        self.serv.send(write_attrs_req)
        self.assertMsgEqual(Lwm2mChanged.matching(
            write_attrs_req)(), self.serv.recv())

        # initial result should be 0
        observe_req = Lwm2mObserve(ResPath.SoftwareManagement[0].UpdateResult)
        self.serv.send(observe_req)
        self.assertMsgEqual(Lwm2mContent.matching(
            observe_req)(content=b'0'), self.serv.recv())

        # Write /9/0/3 (Package URI)
        self.write_software_uri_expect_success(self.get_software_uri())

        # even before reaching the server, we should get an error
        notify_msg = self.serv.recv()
        # handshake failure => "Connection lost"
        self.assertMsgEqual(Lwm2mNotify(observe_req.token,
                                        str(UpdateResult.CONNECTION_LOST).encode()),
                            notify_msg)
        self.serv.send(Lwm2mReset(msg_id=notify_msg.msg_id))
        self.assertEqual(0, self.read_state())


class SoftwareManagementWriteEmptyUriInIdleState(SoftwareManagement.Test):
    def runTest(self):
        self.assertEqual(UpdateState.INITIAL, self.read_state())

        req = Lwm2mWrite(ResPath.SoftwareManagement[0].PackageURI, b'')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_BAD_REQUEST),
                            self.serv.recv())

        self.assertEqual(UpdateState.INITIAL, self.read_state())
        self.assertEqual(UpdateResult.INVALID_URI, self.read_update_result())


class SoftwareManagementCoapUri(SoftwareManagement.TestWithCoapServer):
    def tearDown(self):
        super().tearDown()

        # there should be exactly one request
        with self.file_server as file_server:
            self.assertEqual(1, len(file_server.requests))
            self.assertMsgEqual(CoapGet(self.PATH),
                                file_server.requests[0])

    def runTest(self):
        with self.file_server as file_server:
            file_server.set_resource(self.PATH,
                                     make_software_package(self.SOFTWARE_SCRIPT_CONTENT))
            sw_uri = file_server.get_resource_uri(self.PATH)
        self.write_software_and_wait_for_download(sw_uri)


class SoftwareManagementCoapsUri(SoftwareManagement.TestWithCoapsServer, SoftwareManagementCoapUri):
    pass


class SoftwareManagementCoapsUriAutoSuspend(SoftwareManagementCoapsUri):
    def setUp(self):
        super().setUp(extra_cmdline_args=['--sw-mgmt-auto-suspend'])


class SoftwareManagementCoapsUriManualSuspend(SoftwareManagementCoapsUri):
    def runTest(self):
        with self.file_server as file_server:
            file_server.set_resource(self.PATH,
                                     make_software_package(self.SOFTWARE_SCRIPT_CONTENT))
            sw_uri = file_server.get_resource_uri(self.PATH)

        self.communicate('sw-mgmt-suspend')

        # Write /9/0/3 (Package URI)
        self.write_software_uri_expect_success(sw_uri)

        # wait until the state machine enters the DOWNLOAD STARTED state
        deadline = time.time() + 20
        while time.time() < deadline:
            time.sleep(0.5)
            if self.read_state() == UpdateState.DOWNLOAD_STARTED:
                break
        else:
            self.fail('software still not in DOWNLOAD STARTED state')

        time.sleep(5)
        with self.file_server as file_server:
            self.assertEqual(0, len(file_server.requests))

        # resume the download
        self.communicate('sw-mgmt-reconnect')

        # wait until client downloads the software
        self.wait_for_download()


class SoftwareManagementCoapsReconnectTest(SoftwareManagement.TestWithPartialCoapsDownloadAndRestart):
    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(True)

    def runTest(self):
        # Write /9/0/3 (Package URI)
        self.write_software_uri_expect_success(self.sw_uri)

        self.wait_for_half_download()

        with self.file_server as file_server:
            file_server._server.reset()
            self.communicate('sw-mgmt-reconnect')
            self.assertDtlsReconnect(file_server._server, timeout_s=10,
                                     expected_error=['0x7700', '0x7900'])

        self.wait_for_download()

        # Execute /9/0/4 (Install)
        self.perform_software_install_expect_success()


class SoftwareManagementCoapsSuspendDuringOfflineAndReconnectDuringOnlineTest(SoftwareManagement.TestWithPartialCoapsDownloadAndRestart):
    def runTest(self):
        # Write /9/0/3 (Package URI)
        self.write_software_uri_expect_success(self.sw_uri)

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

            self.communicate('sw-mgmt-suspend')

            with self.assertRaises(socket.timeout):
                file_server._server.recv(timeout_s=5)

            self.communicate('exit-offline')

            self.assertDemoRegisters()

            with self.assertRaises(socket.timeout):
                file_server._server.recv(timeout_s=5)

            file_server._server.reset()
            self.communicate('sw-mgmt-reconnect')
            self.assertPktIsDtlsClientHello(
                file_server._server._raw_udp_socket.recv(65536, socket.MSG_PEEK))

        self.wait_for_download()


class SoftwareManagementCoapsSuspendDuringOfflineAndReconnectDuringOfflineTest(
        SoftwareManagement.TestWithPartialCoapsDownloadAndRestart):
    def runTest(self):
        # Write /9/0/3 (Package URI)
        self.write_software_uri_expect_success(self.sw_uri)

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

            self.communicate('sw-mgmt-suspend')

            with self.assertRaises(socket.timeout):
                file_server._server.recv(timeout_s=5)

            self.communicate('sw-mgmt-reconnect')

            with self.assertRaises(socket.timeout):
                file_server._server.recv(timeout_s=5)

            file_server._server.reset()
            self.communicate('exit-offline')
            self.assertPktIsDtlsClientHello(
                file_server._server._raw_udp_socket.recv(65536, socket.MSG_PEEK))

        self.assertDemoRegisters()

        self.wait_for_download()


class SoftwareManagementCoapsOfflineDuringSuspendAndReconnectDuringOnlineTest(
        SoftwareManagement.TestWithPartialCoapsDownloadAndRestart):
    def runTest(self):
        # Write /9/0/3 (Package URI)
        self.write_software_uri_expect_success(self.sw_uri)

        self.wait_for_half_download()

        with self.file_server as file_server:
            # Flush any buffers
            while True:
                try:
                    file_server._server.recv(timeout_s=1)
                except socket.timeout:
                    break

            self.communicate('sw-mgmt-suspend')

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
            self.communicate('sw-mgmt-reconnect')
            self.assertPktIsDtlsClientHello(
                file_server._server._raw_udp_socket.recv(65536, socket.MSG_PEEK))

        self.wait_for_download()


class SoftwareManagementCoapsOfflineDuringSuspendAndReconnectDuringOfflineTest(
        SoftwareManagement.TestWithPartialCoapsDownloadAndRestart):
    def runTest(self):
        # Write /9/0/3 (Package URI)
        self.write_software_uri_expect_success(self.sw_uri)

        self.wait_for_half_download()

        with self.file_server as file_server:
            # Flush any buffers
            while True:
                try:
                    file_server._server.recv(timeout_s=1)
                except socket.timeout:
                    break

            self.communicate('sw-mgmt-suspend')

            with self.assertRaises(socket.timeout):
                file_server._server.recv(timeout_s=5)

            self.communicate('enter-offline')

            with self.assertRaises(socket.timeout):
                file_server._server.recv(timeout_s=5)

            self.communicate('sw-mgmt-reconnect')

            with self.assertRaises(socket.timeout):
                file_server._server.recv(timeout_s=5)

            file_server._server.reset()
            self.communicate('exit-offline')
            self.assertPktIsDtlsClientHello(
                file_server._server._raw_udp_socket.recv(65536, socket.MSG_PEEK))

        self.assertDemoRegisters()

        self.wait_for_download()


class SoftwareManagementHttpSuspendDuringOfflineAndReconnectDuringOnlineTest(
        SoftwareManagement.TestWithPartialHttpDownloadAndRestart):
    def runTest(self):
        self.provide_response()

        # Write /9/0/3 (Package URI)
        self.write_software_uri_expect_success(self.get_software_uri())

        self.wait_for_half_download()

        self.communicate('enter-offline')

        time.sleep(5)
        fsize = os.stat(self.sw_file_name).st_size
        self.assertEqual(len(self.requests), 1)
        self.provide_response()

        self.communicate('sw-mgmt-suspend')

        time.sleep(5)
        self.assertEqual(os.stat(self.sw_file_name).st_size, fsize)
        self.assertEqual(len(self.requests), 1)

        self.communicate('exit-offline')

        self.assertDemoRegisters()

        time.sleep(5)
        self.assertEqual(os.stat(self.sw_file_name).st_size, fsize)
        self.assertEqual(len(self.requests), 1)

        self.communicate('sw-mgmt-reconnect')

        self.wait_for_download()

        self.assertEqual(len(self.requests), 2)


class SoftwareManagementHttpSuspendDuringOfflineAndReconnectDuringOfflineTest(
        SoftwareManagement.TestWithPartialHttpDownloadAndRestart):
    def runTest(self):
        self.provide_response()

        # Write /9/0/3 (Package URI)
        self.write_software_uri_expect_success(self.get_software_uri())

        self.wait_for_half_download()

        self.communicate('enter-offline')

        time.sleep(5)
        fsize = os.stat(self.sw_file_name).st_size
        self.assertEqual(len(self.requests), 1)
        self.provide_response()

        self.communicate('sw-mgmt-suspend')

        time.sleep(5)
        self.assertEqual(os.stat(self.sw_file_name).st_size, fsize)
        self.assertEqual(len(self.requests), 1)

        self.communicate('sw-mgmt-reconnect')

        time.sleep(5)
        self.assertEqual(os.stat(self.sw_file_name).st_size, fsize)
        self.assertEqual(len(self.requests), 1)

        self.communicate('exit-offline')

        self.assertDemoRegisters()

        self.wait_for_download()

        self.assertEqual(len(self.requests), 2)


class SoftwareManagementHttpOfflineDuringSuspendAndReconnectDuringOnlineTest(
        SoftwareManagement.TestWithPartialHttpDownloadAndRestart):
    def runTest(self):
        self.provide_response()

        # Write /9/0/3 (Package URI)
        self.write_software_uri_expect_success(self.get_software_uri())

        self.wait_for_half_download()

        self.communicate('sw-mgmt-suspend')

        time.sleep(5)
        fsize = os.stat(self.sw_file_name).st_size
        self.assertEqual(len(self.requests), 1)
        self.provide_response()

        self.communicate('enter-offline')

        time.sleep(5)
        self.assertEqual(os.stat(self.sw_file_name).st_size, fsize)
        self.assertEqual(len(self.requests), 1)

        self.communicate('exit-offline')

        self.assertDemoRegisters()

        time.sleep(5)
        self.assertEqual(os.stat(self.sw_file_name).st_size, fsize)
        self.assertEqual(len(self.requests), 1)

        self.communicate('sw-mgmt-reconnect')

        self.wait_for_download()

        self.assertEqual(len(self.requests), 2)


class SoftwareManagementHttpOfflineDuringSuspendAndReconnectDuringOfflineTest(
        SoftwareManagement.TestWithPartialHttpDownloadAndRestart):
    def runTest(self):
        self.provide_response()

        # Write /9/0/3 (Package URI)
        self.write_software_uri_expect_success(self.get_software_uri())

        self.wait_for_half_download()

        self.communicate('sw-mgmt-suspend')

        time.sleep(5)
        fsize = os.stat(self.sw_file_name).st_size
        self.assertEqual(len(self.requests), 1)
        self.provide_response()

        self.communicate('enter-offline')

        time.sleep(5)
        self.assertEqual(os.stat(self.sw_file_name).st_size, fsize)
        self.assertEqual(len(self.requests), 1)

        self.communicate('sw-mgmt-reconnect')

        time.sleep(5)
        self.assertEqual(os.stat(self.sw_file_name).st_size, fsize)
        self.assertEqual(len(self.requests), 1)

        self.communicate('exit-offline')

        self.assertDemoRegisters()

        self.wait_for_download()

        self.assertEqual(len(self.requests), 2)


class SoftwareManagementRestartBeforeIntegrity(SoftwareManagement.Test):
    def setUp(self):
        super().setUp(extra_cmdline_args=[
            '--sw-mgmt-terminate-after-downloading'])
        self.set_check_marker(True)
        self.set_auto_deregister(True)

    def runTest(self):
        # Write /9/0/2 (Software)
        req = Lwm2mWrite(ResPath.SoftwareManagement[0].Package,
                         make_software_package(self.SOFTWARE_SCRIPT_CONTENT),
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # wait until demo stops responding
        deadline = time.time() + 20
        while time.time() < deadline:
            ret = self.communicate('send-update', timeout=2)
            if ret is None:
                break
            time.sleep(0.2)
        else:
            self.fail('demo still active')

        # restart the app
        self.assertDemoDeregisters()
        self.teardown_demo_with_servers(auto_deregister=False)
        self.setup_demo_with_servers(
            sw_mgmt_persistence_file=self.ANJAY_PERSISTENCE_FILE)

        self.assertEqual(UpdateState.DELIVERED, self.read_state())
        self.assertEqual(UpdateResult.INITIAL, self.read_update_result())

        # Execute /9/0/4 (Install)
        self.perform_software_install_expect_success()


class SoftwareManagementRestartAfterIntegrityFailed(SoftwareManagement.Test):
    def runTest(self):
        # Write /9/0/2 (Software)
        req = Lwm2mWrite(ResPath.SoftwareManagement[0].Package,
                         make_software_package(
                             self.SOFTWARE_SCRIPT_CONTENT, crc=2137),
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        self.wait_until_result_is(UpdateResult.INTEGRITY_FAILURE)
        self.assertEqual(UpdateResult.INTEGRITY_FAILURE,
                         self.read_update_result())
        self.assertEqual(UpdateState.INITIAL, self.read_state())

        # restart the app
        self.teardown_demo_with_servers()
        self.setup_demo_with_servers(
            sw_mgmt_persistence_file=self.ANJAY_PERSISTENCE_FILE)

        self.assertEqual(UpdateState.INITIAL, self.read_state())
        self.assertEqual(UpdateResult.INITIAL, self.read_update_result())


class SoftwareManagementRestartWithDelivered(SoftwareManagement.Test):
    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(True)

    def runTest(self):
        # Write /9/0/2 (Software)
        req = Lwm2mWrite(ResPath.SoftwareManagement[0].Package,
                         make_software_package(self.SOFTWARE_SCRIPT_CONTENT),
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        self.wait_until_state_is(UpdateState.DELIVERED)

        # restart the app
        self.teardown_demo_with_servers()
        self.setup_demo_with_servers(
            sw_mgmt_persistence_file=self.ANJAY_PERSISTENCE_FILE)

        self.assertEqual(UpdateState.DELIVERED, self.read_state())
        self.assertEqual(UpdateResult.INITIAL, self.read_update_result())

        # Execute /9/0/4 (Install)
        self.perform_software_install_expect_success()


class SoftwareManagementResumeDownloadingOverHttpWithReconnect(
        SoftwareManagement.TestWithPartialHttpDownloadAndRestart):
    def _get_valgrind_args(self):
        # we don't kill the process here, so we want Valgrind
        return SoftwareManagement.TestWithHttpServer._get_valgrind_args(self)

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
        # Write /9/0/3 (Package URI)
        self.write_software_uri_expect_success(self.get_software_uri())

        self.wait_for_half_download()

        # reconnect
        self.serv.reset()
        self.communicate('reconnect')
        self.provide_response()
        self.assertDemoRegisters(self.serv, timeout_s=5)

        # wait until client downloads the software
        deadline = time.time() + 20
        state = None
        while time.time() < deadline:
            fsize = os.stat(self.sw_file_name).st_size
            self.assertGreater(fsize * 2, self.GARBAGE_SIZE)
            state = self.read_state()
            self.assertIn(
                state, {UpdateState.DOWNLOAD_STARTED, UpdateState.DOWNLOADED, UpdateState.DELIVERED})
            if state == UpdateState.DELIVERED:
                break
            # prevent test from reading Result hundreds of times per second
            time.sleep(0.5)

        self.assertEqual(state, UpdateState.DELIVERED)

        self.assertEqual(len(self.requests), 2)


class SoftwareManagementWithDelayedResultTest:
    class TestMixin:
        def runTest(self, forced_error, state, result):
            with open(os.path.join(self.config.demo_path, self.config.demo_cmd), 'rb') as f:
                software = f.read()

            # Write /9/0/2 (Software)
            self.block_send(software,
                            equal_chunk_splitter(chunk_size=1024),
                            force_error=forced_error)

            # Execute /9/0/4 (Install)
            req = Lwm2mExecute(ResPath.SoftwareManagement[0].Install)
            self.serv.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                                self.serv.recv())

            self.serv.reset()
            self.assertDemoRegisters()
            self.assertEqual(self.read_path(self.serv, ResPath.SoftwareManagement[0].UpdateResult).content,
                             str(UpdateResult.INITIAL).encode())
            self.assertEqual(self.read_path(self.serv, ResPath.SoftwareManagement[0].UpdateState).content,
                             str(UpdateState.DELIVERED).encode())

            self.wait_until_result_is(result)

            self.assertEqual(self.read_path(self.serv, ResPath.SoftwareManagement[0].UpdateResult).content,
                             str(result).encode())
            self.assertEqual(self.read_path(self.serv, ResPath.SoftwareManagement[0].UpdateState).content,
                             str(state).encode())
            self.assertEqual(self.read_path(self.serv, ResPath.SoftwareManagement[0].ActivationState).content,
                             str(ActivationState.DEACTIVATED).encode())


class SoftwareManagementWithDelayedSuccessTest(
        SoftwareManagementWithDelayedResultTest.TestMixin, SoftwareManagement.BlockTest):
    def runTest(self):
        super().runTest(SoftwareUpdateForcedError.DelayedSuccessInstall,
                        UpdateState.INSTALLED, UpdateResult.INSTALLED)


class SoftwareManagementWithDelayedFailureTest(
        SoftwareManagementWithDelayedResultTest.TestMixin, SoftwareManagement.BlockTest):
    def runTest(self):
        super().runTest(SoftwareUpdateForcedError.DelayedFailedInstall,
                        UpdateState.DELIVERED, UpdateResult.INSTALLATION_FAILURE)


class SoftwareManagementWithSetSuccessInPerformInstall(SoftwareManagement.BlockTest):
    def runTest(self):
        with open(os.path.join(self.config.demo_path, self.config.demo_cmd), 'rb') as f:
            software = f.read()

        # Write /9/0/2 (Software)
        self.block_send(software,
                        equal_chunk_splitter(chunk_size=1024),
                        force_error=SoftwareUpdateForcedError.SuccessInPerformInstall)

        # Execute /9/0/4 (Install)
        self.perform_software_install_expect_success()

        # pkg_install_job is called via scheduler, so there is a small
        # window during which reading the Software Update Result still returns
        # INITIAL. Wait for a while for State to actually change.
        self.wait_until_result_is(UpdateResult.INSTALLED, timeout_s=5)

        self.assertEqual(self.read_state(), UpdateState.INSTALLED)
        self.assertEqual(self.read_update_result(), UpdateResult.INSTALLED)
        self.assertEqual(self.read_activation_state(),
                         ActivationState.DEACTIVATED)


class SoftwareManagementWithSetSuccessInPerformInstallActivate(SoftwareManagement.BlockTest):
    def runTest(self):
        with open(os.path.join(self.config.demo_path, self.config.demo_cmd), 'rb') as f:
            software = f.read()

        # Write /9/0/2 (Software)
        self.block_send(software,
                        equal_chunk_splitter(chunk_size=1024),
                        force_error=SoftwareUpdateForcedError.SuccessInPerformInstallActivate)

        # Execute /9/0/4 (Install)
        self.perform_software_install_expect_success()

        # pkg_install_job is called via scheduler, so there is a small
        # window during which reading the Software Update Result still returns
        # INITIAL. Wait for a while for State to actually change.
        self.wait_until_result_is(UpdateResult.INSTALLED, timeout_s=5)

        self.assertEqual(self.read_state(), UpdateState.INSTALLED)
        self.assertEqual(self.read_update_result(), UpdateResult.INSTALLED)
        self.assertEqual(self.read_activation_state(),
                         ActivationState.ACTIVATED)


class SoftwareManagementWithSetFailureInPerformInstall(SoftwareManagement.BlockTest):
    def runTest(self):
        with open(os.path.join(self.config.demo_path, self.config.demo_cmd), 'rb') as f:
            software = f.read()

        # Write /9/0/2 (Software)
        self.block_send(software,
                        equal_chunk_splitter(chunk_size=1024),
                        force_error=SoftwareUpdateForcedError.FailureInPerformInstall)

        self.wait_until_state_is(UpdateState.DELIVERED)

        # Execute /9/0/4 (Install)
        self.perform_software_install_expect_success()

        # pkg_install_job is called via scheduler, so there is a small
        # window during which reading the Software Update Result still returns
        # INITIAL. Wait for a while for State to actually change.
        self.wait_until_result_is(
            UpdateResult.INSTALLATION_FAILURE, timeout_s=5)

        self.assertEqual(self.read_state(), UpdateState.DELIVERED)
        self.assertEqual(self.read_update_result(),
                         UpdateResult.INSTALLATION_FAILURE)
        self.assertEqual(self.read_activation_state(),
                         ActivationState.DEACTIVATED)


class SoftwareManagementWithoutReboot(SoftwareManagement.BlockTest):
    def runTest(self):
        with open(os.path.join(self.config.demo_path, self.config.demo_cmd), 'rb') as f:
            software = f.read()

        # Write /9/0/2 (Software)
        self.block_send(software,
                        equal_chunk_splitter(chunk_size=1024),
                        force_error=SoftwareUpdateForcedError.DoNothing)

        self.wait_until_state_is(UpdateState.DELIVERED)

        # Execute /9/0/4 (Install)
        self.perform_software_install_expect_success()

        # Wait until internal state machine is updated
        # We cannot rely on SoftwareManagement.State resource because it is installed first
        # and the user code is only notified later, via a scheduler job
        self.read_log_until_match(regex=re.escape(
            b'*** SOFTWARE INSTALL:'), timeout_s=5)

        self.assertEqual(self.read_state(), UpdateState.DELIVERED)

        self.communicate('set-sw-mgmt-install-result 0 1')

        self.assertEqual(self.read_state(), UpdateState.INSTALLED)
        self.assertEqual(self.read_update_result(), UpdateResult.INSTALLED)
        self.assertEqual(self.read_activation_state(),
                         ActivationState.DEACTIVATED)


try:
    import aiocoap
    import aiocoap.resource
    import aiocoap.transports.tls
except ImportError:
    # SoftwareManagementCoapTlsTest requires a bleeding-edge version of aiocoap, that at the time of
    # writing this code, is not available even in the prerelease channel.
    # So we're not enforcing this dependency for now.
    pass


@unittest.skipIf('aiocoap.transports.tls' not in sys.modules,
                 'aiocoap.transports.tls not available')
@unittest.skipIf(sys.version_info < (3, 5, 3),
                 'SSLContext signature changed in Python 3.5.3')
class SoftwareManagementCoapTlsTest(
        SoftwareManagement.TestWithTlsServer, test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(garbage=8000)

        class SoftwareResource(aiocoap.resource.Resource):
            async def render_get(resource, request):
                return aiocoap.Message(payload=make_software_package(
                    self.SOFTWARE_SCRIPT_CONTENT))

        serversite = aiocoap.resource.Site()
        serversite.add_resource(('software',), SoftwareResource())

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
        self.set_auto_deregister(True)

    def tearDown(self):
        try:
            super().tearDown()
        finally:
            self.server_thread.loop.call_soon_threadsafe(
                self.server_thread.loop.stop)
            self.server_thread.join()

    def get_software_uri(self):
        return 'coaps+tcp://127.0.0.1:%d/software' % (
            self.server_thread.server_address[1],)

    def runTest(self):
        self.write_software_and_wait_for_download(self.get_software_uri())

        # Execute /9/0/4 (Install)
        self.perform_software_install_expect_success()


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

            self.ANJAY_PERSISTENCE_FILE = generate_temp_filename(
                dir='/tmp', prefix='anjay-sw-persistence-')

            kwargs['extra_cmdline_args'] += [
                '--prefer-same-socket-downloads',
                '--ack-timeout', str(self.ACK_TIMEOUT),
                '--max-retransmit', str(self.MAX_RETRANSMIT),
                '--ack-random-factor', str(1.0),
                '--nstart', str(self.NSTART),
                '--sw-mgmt-persistence-file', self.ANJAY_PERSISTENCE_FILE
            ]
            kwargs['lifetime'] = self.LIFETIME
            super().setUp(*args, **kwargs)
            self.file_server = CoapFileServer(
                self.serv._coap_server, binding=self.BINDING)
            self.file_server.set_resource(path=SOFTWARE_PATH,
                                          data=make_software_package(b'a' * self.GARBAGE_SIZE))

        def read_state(self, serv=None):
            return int(self.read_resource(serv or self.serv,
                                          oid=OID.SoftwareManagement,
                                          iid=0,
                                          rid=RID.SoftwareManagement.UpdateState).content)

        def read_result(self, serv=None):
            return int(self.read_resource(serv or self.serv,
                                          oid=OID.SoftwareManagement,
                                          iid=0,
                                          rid=RID.SoftwareManagement.UpdateResult).content)

        def start_download(self):
            self.write_resource(self.serv,
                                oid=OID.SoftwareManagement,
                                iid=0,
                                rid=RID.SoftwareManagement.PackageURI,
                                content=self.file_server.get_resource_uri(SOFTWARE_PATH))

        def handle_get(self, pkt=None):
            if pkt is None:
                pkt = self.serv.recv()
            block2 = pkt.get_options(coap.Option.BLOCK2)
            if block2:
                self.assertEqual(block2[0].block_size(), self.BLK_SZ)
            self.file_server.handle_recvd_request(pkt)

        def num_blocks(self):
            return (len(
                self.file_server._resources[SOFTWARE_PATH].data) + self.BLK_SZ - 1) // self.BLK_SZ

        def wait_for_delivered_state(self):
            deadline = time.time() + 5
            while time.time() < deadline:
                time.sleep(0.5)

                if self.read_state() == UpdateState.DELIVERED:
                    return

            self.fail('software still not downloaded')

        def tearDown(self, *args, **kwargs):
            super().tearDown(*args, **kwargs)


class SoftwareManagementDownloadSameSocket(SameSocketDownload.Test):
    def runTest(self):
        self.start_download()

        for _ in range(self.num_blocks()):
            pkt = self.serv.recv()
            self.handle_get(pkt)

        # wait until client downloads and verify the software
        self.wait_for_delivered_state()

        self.assertEqual(self.read_state(), UpdateState.DELIVERED)


class SoftwareManagementDownloadSameSocketAndOngoingBlockwiseWrite(
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
                # Client waits for next chunk of Raw Bytes, but gets software
                # block instead
                self.handle_get(dl_req_get)

        # wait until client downloads and verify the software
        self.wait_for_delivered_state()

        self.assertEqual(self.read_state(), UpdateState.DELIVERED)


class SoftwareManagementDownloadSameSocketAndOngoingBlockwiseRead(
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
                # Client waits for next chunk of Raw Bytes, but gets software
                # block instead
                self.handle_get(dl_req_get)

        # wait until client downloads and verify the software
        self.wait_for_delivered_state()

        self.assertEqual(self.read_state(), UpdateState.DELIVERED)


class SoftwareManagementDownloadSameSocketUpdateDuringDownloadNstart2(
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

        # wait until client downloads and verify the software
        self.wait_for_delivered_state()

        self.assertEqual(self.read_state(), UpdateState.DELIVERED)


class SoftwareManagementDownloadSameSocketUpdateDuringDownloadNstart1(
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

        # wait until client downloads and verify the software
        self.wait_for_delivered_state()

        self.assertEqual(self.read_state(), UpdateState.DELIVERED)


class SoftwareManagementSameSocketAndReconnectNstart1(SameSocketDownload.Test):
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

        # wait until client downloads and verify the software
        self.wait_for_delivered_state()

        self.assertEqual(self.read_state(), UpdateState.DELIVERED)


class SoftwareManagementDownloadSameSocketUpdateTimeoutNstart2(SameSocketDownload.Test):
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

        # wait until client downloads and verify the software
        self.wait_for_delivered_state()

        self.assertEqual(self.read_state(), UpdateState.DELIVERED)


class SoftwareManagementSameSocketUpdateTimeoutNstart1(SameSocketDownload.Test):
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

        # wait until client downloads and verify the software
        self.wait_for_delivered_state()

        self.assertEqual(self.read_state(), UpdateState.DELIVERED)


class SoftwareManagementSameSocketDontCare(SameSocketDownload.Test):
    def runTest(self):
        self.start_download()
        self.serv.recv()  # recv GET and ignore it


class SoftwareManagementSameSocketSuspendDueToOffline(SameSocketDownload.Test):
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

        # wait until client downloads and verify the software
        self.wait_for_delivered_state()

        self.assertEqual(self.read_state(), UpdateState.DELIVERED)


class SoftwareManagementSameSocketSuspendDueToOfflineDuringUpdate(
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

        # wait until client downloads and verify the software
        self.wait_for_delivered_state()

        self.assertEqual(self.read_state(), UpdateState.DELIVERED)


class SoftwareManagementSameSocketSuspendDueToOfflineDuringUpdateNoMessagesCheck(
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

        # wait until client downloads and verify the software
        self.wait_for_delivered_state()

        self.assertEqual(self.read_state(), UpdateState.DELIVERED)


class SoftwareManagementDownloadSameSocketAndBootstrap(SameSocketDownload.Test):
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
        self.assertEqual(self.read_state(self.new_server), UpdateState.INITIAL)


class SoftwareManagementHttpRequestTimeoutTest(SoftwareManagement.TestWithPartialDownload,
                                               SoftwareManagement.TestWithHttpServer):
    CHUNK_SIZE = 500
    RESPONSE_DELAY = 0.5
    TCP_REQUEST_TIMEOUT = 5

    def setUp(self):
        super().setUp(extra_cmdline_args=['--sw-mgmt-tcp-request-timeout',
                                          str(self.TCP_REQUEST_TIMEOUT)])

    def runTest(self):
        self.provide_response()

        # Write /9/0/3 (Package URI)
        self.write_software_uri_expect_success(self.get_software_uri())

        self.wait_for_half_download()
        # Change RESPONSE_DELAY so that the server stops responding
        self.RESPONSE_DELAY = self.TCP_REQUEST_TIMEOUT + 5

        half_download_time = time.time()
        self.wait_until_state_is(
            UpdateState.INITIAL, timeout_s=self.TCP_REQUEST_TIMEOUT + 5)
        fail_time = time.time()
        self.assertEqual(self.read_update_result(),
                         UpdateResult.CONNECTION_LOST)

        self.assertAlmostEqual(
            fail_time, half_download_time + self.TCP_REQUEST_TIMEOUT, delta=1.5)


class SoftwareManagementHttpRequestTimeoutTest20sec(SoftwareManagementHttpRequestTimeoutTest):
    TCP_REQUEST_TIMEOUT = 20
