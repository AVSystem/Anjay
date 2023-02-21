# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

import os
import re
import socket

from framework.lwm2m.coap.server import SecurityMode
from framework.lwm2m_test import *
from framework import test_suite

from suites.default.block_write import Block, equal_chunk_splitter
from suites.default.firmware_update import UpdateResult, UpdateState


class FirmwareUpdateWithoutReboot(Block.Test):
    def runTest(self):
        with open(os.path.join(self.config.demo_path, self.config.demo_cmd), 'rb') as f:
            firmware = f.read()

        # Write /5/0/0 (Firmware)
        self.block_send(firmware,
                        equal_chunk_splitter(chunk_size=1024),
                        force_error=FirmwareUpdateForcedError.DoNothing)

        # Execute /5/0/2 (Update)
        req = Lwm2mExecute(ResPath.FirmwareUpdate.Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Wait until internal state machine is updated
        # We cannot rely on FirmwareUpdate.State resource because it is updated first
        # and the user code is only notified later, via a scheduler job
        self.read_log_until_match(regex=re.escape(b'*** FIRMWARE UPDATE:'), timeout_s=5)

        self.assertEqual(self.read_path(self.serv, ResPath.FirmwareUpdate.State).content.decode(),
                         str(UpdateState.UPDATING))

        self.communicate('set-fw-update-result ' + str(UpdateResult.SUCCESS))

        self.assertEqual(self.read_path(self.serv, ResPath.FirmwareUpdate.State).content.decode(),
                         str(UpdateState.IDLE))
        self.assertEqual(self.read_path(self.serv, ResPath.FirmwareUpdate.UpdateResult).content,
                         str(UpdateResult.SUCCESS).encode())
