# -*- coding: utf-8 -*-
#
# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.
import os

from framework.lwm2m_test import *
from framework.create_package import PackageForcedError, make_firmware_package

from .block_write import Block, equal_chunk_splitter
from .firmware_update import FirmwareUpdate, UpdateState, UpdateResult

class FirmwareUpdateModuleWithLwM2MResourcesBase:
    def fwUpdateLwM2M11OptionalResDisabled(self):
        self.skipIfFeatureStatus('ANJAY_WITH_MODULE_FW_UPDATE_V11_RESOURCES = OFF', 'FW Object LwM2M 1.1 optional resources disabled')

    def setUp(self, extra_cmdline_args=[], *args, **kwargs):
        self.fwUpdateLwM2M11OptionalResDisabled()

        super().setUp(extra_cmdline_args=extra_cmdline_args, *args, **kwargs)


class UpdateSeverity:
    CRITICAL = 0
    MANDATORY = 1
    OPTIONAL = 2


class FirmwareUpdateCancelDuringIdleTest(FirmwareUpdate.Test):
    def runTest(self):
        # Execute /5/0/10 (Cancel)
        req = Lwm2mExecute(ResPath.FirmwareUpdate.Cancel)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(code=coap.Code.RES_METHOD_NOT_ALLOWED),
                            self.serv.recv())

        self.assertEqual(UpdateState.IDLE, self.read_state())
        self.assertEqual(UpdateResult.INITIAL, self.read_update_result())


class FirmwareUpdateCancelDuringDownloadingTest(FirmwareUpdate.TestWithPartialDownload,
                                                FirmwareUpdate.TestWithHttpServer):
    RESPONSE_DELAY = 0.5
    CHUNK_SIZE = 3000

    def runTest(self):
        self.provide_response()

        # Write /5/0/1 (Firmware URI)
        self.write_firmware_uri_expect_success(self.get_firmware_uri())

        self.wait_for_half_download()

        # Execute /5/0/10 (Cancel)
        req = Lwm2mExecute(ResPath.FirmwareUpdate.Cancel)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        self.assertEqual(UpdateState.IDLE, self.read_state())
        self.assertEqual(UpdateResult.CANCELLED, self.read_update_result())


class FirmwareUpdateCancelDuringDownloadedTest(FirmwareUpdate.TestWithHttpServer):
    def runTest(self):
        self.provide_response()
        self.write_firmware_and_wait_for_download(self.get_firmware_uri())

        # Execute /5/0/10 (Cancel)
        req = Lwm2mExecute(ResPath.FirmwareUpdate.Cancel)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        self.assertEqual(UpdateState.IDLE, self.read_state())
        self.assertEqual(UpdateResult.CANCELLED, self.read_update_result())


class FirmwareUpdateCancelDuringUpdatingTest(FirmwareUpdate.TestWithHttpServer):
    # Don't run the downloaded package to be able to process Cancel
    FW_PKG_OPTS = {
        "force_error": PackageForcedError.Firmware.DoNothing
    }

    def setUp(self):
        super().setUp()
        self.set_reset_machine(False)

    def runTest(self):
        self.provide_response()
        self.write_firmware_and_wait_for_download(self.get_firmware_uri())

        # Execute /5/0/2 (Update)
        self.perform_firmware_update_expect_success()

        # Execute /5/0/10 (Cancel)
        req = Lwm2mExecute(ResPath.FirmwareUpdate.Cancel)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(code=coap.Code.RES_METHOD_NOT_ALLOWED),
                            self.serv.recv())


class FirmwareUpdateCancelAndUpdateAgainTest(FirmwareUpdate.TestWithPartialDownload,
                                             FirmwareUpdate.TestWithHttpServer):
    RESPONSE_DELAY = 0.5
    CHUNK_SIZE = 3000

    def setUp(self):
        super().setUp()
        self.set_check_marker(True)
        self.set_auto_deregister(False)
        self.set_reset_machine(False)

    def runTest(self):
        self.provide_response()

        # Write /5/0/1 (Firmware URI)
        self.write_firmware_uri_expect_success(self.get_firmware_uri())

        self.wait_for_half_download()

        # Execute /5/0/10 (Cancel)
        req = Lwm2mExecute(ResPath.FirmwareUpdate.Cancel)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        self.assertEqual(UpdateState.IDLE, self.read_state())
        self.assertEqual(UpdateResult.CANCELLED, self.read_update_result())

        self.provide_response()
        self.write_firmware_and_wait_for_download(self.get_firmware_uri())

        # Execute /5/0/2 (Update) again
        self.perform_firmware_update_expect_success()


class FirmwareUpdateMaxDeferPeriodInvalidValueTest(FirmwareUpdateModuleWithLwM2MResourcesBase,
                                                   FirmwareUpdate.Test):
    def runTest(self):
        # Write /5/0/13 (Maximum Defer Period)
        req = Lwm2mWrite(ResPath.FirmwareUpdate.MaxDeferPeriod, b'-5')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_BAD_REQUEST),
                            self.serv.recv())


class FirmwareUpdateMaxDeferPeriodValidValueTest(FirmwareUpdateModuleWithLwM2MResourcesBase,
                                                 FirmwareUpdate.Test):
    def runTest(self):
        for max_defer_period_value in [b'0', b'30']:
            # Write /5/0/13 (Maximum Defer Period)
            req = Lwm2mWrite(ResPath.FirmwareUpdate.MaxDeferPeriod, max_defer_period_value)
            self.serv.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())


class FirmwareUpdateWithDefer(FirmwareUpdateModuleWithLwM2MResourcesBase,
                              Block.Test):
    def runTest(self):
        with open(os.path.join(self.config.demo_path, self.config.demo_cmd), 'rb') as f:
            firmware = f.read()

        # Write /5/0/13 (Maximum Defer Period)
        req = Lwm2mWrite(ResPath.FirmwareUpdate.MaxDeferPeriod, b'30')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Write /5/0/0 (Firmware)
        self.block_send(firmware,
                        equal_chunk_splitter(chunk_size=1024),
                        force_error=PackageForcedError.Firmware.Defer)

        # Execute /5/0/2 (Update)
        self.perform_firmware_update_expect_success()

        # perform_upgrade handler is called via scheduler, so there is a small
        # window during which reading the Firmware Update State still returns
        # Updating. Wait for a while for State to actually change.
        observed_states = []
        deadline = time.time() + 5  # arbitrary limit
        while not observed_states or observed_states[-1] == str(UpdateState.UPDATING):
            if time.time() > deadline:
                self.fail('Firmware Update did not finish on time, last state = %s' % (
                    observed_states[-1] if observed_states else 'NONE'))
            observed_states.append(
                self.read_path(self.serv, ResPath.FirmwareUpdate.State).content.decode())
            time.sleep(0.5)

        self.assertNotEqual([], observed_states)
        self.assertEqual(observed_states[-1], str(UpdateState.DOWNLOADED))
        self.assertEqual(self.read_path(self.serv, ResPath.FirmwareUpdate.UpdateResult).content,
                         str(UpdateResult.DEFERRED).encode())


class FirmwareUpdateSeverityWriteInvalidValueTest(FirmwareUpdateModuleWithLwM2MResourcesBase, FirmwareUpdate.Test):
    def runTest(self):
        for invalid_severity in [b'-1', b'3']:
            # Write /5/0/11 (Severity)
            req = Lwm2mWrite(ResPath.FirmwareUpdate.Severity, invalid_severity)
            self.serv.send(req)
            self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_BAD_REQUEST),
                                self.serv.recv())


class FirmwareUpdateSeverityWriteValidValueTest(FirmwareUpdateModuleWithLwM2MResourcesBase, FirmwareUpdate.Test):
    def runTest(self):
        valid_severity_values = [
            UpdateSeverity.CRITICAL,
            UpdateSeverity.MANDATORY,
            UpdateSeverity.OPTIONAL
        ]
        for severity in [str(i).encode() for i in valid_severity_values]:
            # Write /5/0/11 (Severity)
            req = Lwm2mWrite(ResPath.FirmwareUpdate.Severity, severity)
            self.serv.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())


class FirmwareUpdateSeverityReadTest(FirmwareUpdateModuleWithLwM2MResourcesBase,
                                     FirmwareUpdate.Test):
    def runTest(self):
        # Read default /5/0/11 (Severity)
        req = Lwm2mRead(ResPath.FirmwareUpdate.Severity)
        self.serv.send(req)
        self.assertMsgEqual(
            Lwm2mContent.matching(req)(content=str(UpdateSeverity.MANDATORY).encode()),
            self.serv.recv())


class FirmwareUpdateLastStateChangeTime:
    class Test:
        def observe_state(self):
            # Observe /5/0/3 (State)
            observe_req = Lwm2mObserve('/%d/0/%d' % (OID.FirmwareUpdate, RID.FirmwareUpdate.State))
            self.serv.send(observe_req)
            self.assertMsgEqual(Lwm2mContent.matching(observe_req)(), self.serv.recv())
            return observe_req.token

        def cancel_observe_state(self, token):
            cancel_req = Lwm2mObserve('/%d/0/%d' % (OID.FirmwareUpdate, RID.FirmwareUpdate.State),
                                      observe=1, token=token)
            self.serv.send(cancel_req)
            self.assertMsgEqual(Lwm2mContent.matching(cancel_req)(), self.serv.recv())

        def get_states_and_timestamp(self, token, deadline=None):
            # Receive a notification from /5/0/3 and read /5/0/12
            notification_responses = [self.serv.recv(deadline=deadline)]
            self.assertMsgEqual(Lwm2mNotify(token), notification_responses[0])

            read_response = self.read_path(self.serv, ResPath.FirmwareUpdate.LastStateChangeTime,
                                           deadline=deadline)
            while True:
                try:
                    notification_responses.append(
                        self.serv.recv(timeout_s=0,
                                       filter=lambda pkt: isinstance(pkt, Lwm2mNotify)))
                except socket.timeout:
                    break
            return [r.content.decode() for r in
                    notification_responses], read_response.content.decode()


class FirmwareUpdateLastStateChangeTimeWithDelayedSuccessTest(FirmwareUpdateModuleWithLwM2MResourcesBase,
                                                              Block.Test,
                                                              FirmwareUpdateLastStateChangeTime.Test):
    def runTest(self):
        with open(os.path.join(self.config.demo_path, self.config.demo_cmd), 'rb') as f:
            firmware = f.read()

        observe_token = self.observe_state()

        # Write /5/0/0 (Firmware)
        self.block_send(firmware,
                        equal_chunk_splitter(chunk_size=1024),
                        force_error=PackageForcedError.Firmware.DelayedSuccess)

        _, before_update_timestamp = self.get_states_and_timestamp(observe_token)

        time.sleep(1)
        # Execute /5/0/2 (Update)
        self.perform_firmware_update_expect_success()

        state_notification = self.serv.recv()
        self.assertMsgEqual(Lwm2mNotify(observe_token), state_notification)

        self.serv.reset()
        self.assertDemoRegisters()

        req = Lwm2mRead(ResPath.FirmwareUpdate.LastStateChangeTime)
        self.serv.send(req)
        after_update_response = self.serv.recv()
        self.assertMsgEqual(Lwm2mContent.matching(req)(), after_update_response)
        all_timestamps = [before_update_timestamp, after_update_response.content.decode()]
        self.assertEqual(all_timestamps, sorted(all_timestamps))


class FirmwareUpdateLastStateChangeTimeWithDeferTest(FirmwareUpdateModuleWithLwM2MResourcesBase,
                                                     Block.Test,
                                                     FirmwareUpdateLastStateChangeTime.Test):
    def observe_after_update(self, token):
        observed_states = []
        observed_timestamps = []
        deadline = time.time() + 5  # arbitrary limit
        while not observed_states or observed_states[-1] == str(UpdateState.UPDATING):
            states, timestamp = self.get_states_and_timestamp(token, deadline=deadline)
            observed_states += states
            observed_timestamps.append(timestamp)
        self.assertNotEqual([], observed_timestamps)
        return observed_timestamps

    def runTest(self):
        with open(os.path.join(self.config.demo_path, self.config.demo_cmd), 'rb') as f:
            firmware = f.read()

        # Write /5/0/13 (Maximum Defer Period)
        req = Lwm2mWrite(ResPath.FirmwareUpdate.MaxDeferPeriod, b'30')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        observe_token = self.observe_state()

        # Write /5/0/0 (Firmware)
        self.block_send(firmware,
                        equal_chunk_splitter(chunk_size=1024),
                        force_error=PackageForcedError.Firmware.Defer)

        _, after_write_timestamp = self.get_states_and_timestamp(observe_token)

        time.sleep(1)
        # Execute /5/0/2 (Update)
        self.perform_firmware_update_expect_success()

        observed_timestamps = self.observe_after_update(observe_token)

        all_timestamps = [after_write_timestamp] + observed_timestamps
        self.assertEqual(all_timestamps, sorted(all_timestamps))

        self.cancel_observe_state(observe_token)


class FirmwareUpdateSeverityPersistenceTest(FirmwareUpdateModuleWithLwM2MResourcesBase,
                                            FirmwareUpdate.Test):
    def restart(self):
        self.teardown_demo_with_servers()
        self.setup_demo_with_servers(
            fw_updated_marker_path=self.ANJAY_MARKER_FILE)

    def runTest(self):
        severity_values = [
            UpdateSeverity.CRITICAL,
            UpdateSeverity.MANDATORY,
            UpdateSeverity.OPTIONAL
        ]
        for severity in [str(i).encode() for i in severity_values]:
            # Write /5/0/11 (Severity)
            req = Lwm2mWrite(ResPath.FirmwareUpdate.Severity, severity)
            self.serv.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

            req = Lwm2mWrite(ResPath.FirmwareUpdate.Package,
                             make_firmware_package(self.FIRMWARE_SCRIPT_CONTENT),
                             format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
            self.serv.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())
            self.restart()

            req = Lwm2mRead(ResPath.FirmwareUpdate.Severity)
            self.serv.send(req)
            res = self.serv.recv()
            self.assertMsgEqual(Lwm2mContent.matching(req)(), res)

            self.assertEqual(severity, res.content)
            self.restart()


class FirmwareUpdateDeadlinePersistenceTest(FirmwareUpdateModuleWithLwM2MResourcesBase,
                                            FirmwareUpdate.DemoArgsExtractorMixin,
                                            Block.Test):
    def get_deadline(self):
        return int(self.communicate('get-fw-update-deadline',
                                    match_regex='FW_UPDATE_DEADLINE==([0-9]+)\n').group(1))

    def runTest(self):
        with open(os.path.join(self.config.demo_path, self.config.demo_cmd), 'rb') as f:
            firmware = f.read()

        # Write /5/0/13 (Maximum Defer Period)
        req = Lwm2mWrite(ResPath.FirmwareUpdate.MaxDeferPeriod, b'30')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Write /5/0/0 (Firmware)
        self.block_send(firmware,
                        equal_chunk_splitter(chunk_size=1024),
                        force_error=PackageForcedError.Firmware.Defer)

        # Execute /5/0/2 (Update)
        self.perform_firmware_update_expect_success()

        # perform_upgrade handler is called via scheduler, so there is a small
        # window during which reading the Firmware Update State still returns
        # Updating. Wait for a while for State to actually change.
        observed_states = []
        deadline = time.time() + 5  # arbitrary limit
        while not observed_states or observed_states[-1] == str(UpdateState.UPDATING):
            if time.time() > deadline:
                self.fail('Firmware Update did not finish on time, last state = %s' % (
                    observed_states[-1] if observed_states else 'NONE'))
            observed_states.append(
                self.read_path(self.serv, ResPath.FirmwareUpdate.State).content.decode())
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
