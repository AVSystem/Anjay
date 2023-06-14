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

from framework.lwm2m_test import *

from suites.default.advanced_firmware_update import AdvancedFirmwareUpdate, equal_chunk_splitter
from suites.default.advanced_firmware_update import UpdateResult, UpdateState, Instances


class AdvancedFirmwareUpdateWithoutReboot(AdvancedFirmwareUpdate.BlockTest):
    def runTest(self):
        with open(os.path.join(self.config.demo_path, self.config.demo_cmd), 'rb') as f:
            firmware = f.read()

        # Write /33629/0/0 (Package)
        self.block_send(firmware,
                        equal_chunk_splitter(chunk_size=1024),
                        force_error=FirmwareUpdateForcedError.DoNothing)

        # Execute /33629/0/2 (Update)
        req = Lwm2mExecute(ResPath.AdvancedFirmwareUpdate[Instances.APP].Update)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Wait until internal state machine is updated
        # We cannot rely on FirmwareUpdate.State resource because it is updated first
        # and the user code is only notified later, via a scheduler job
        self.read_log_until_match(regex=re.escape(b'*** FIRMWARE UPDATE:'), timeout_s=5)

        self.assertEqual(
            self.read_path(self.serv, ResPath.AdvancedFirmwareUpdate[Instances.APP].State).content.decode(),
            str(UpdateState.UPDATING))

        self.communicate('set-afu-result ' + str(UpdateResult.SUCCESS))

        self.assertEqual(
            self.read_path(self.serv, ResPath.AdvancedFirmwareUpdate[Instances.APP].State).content.decode(),
            str(UpdateState.IDLE))
        self.assertEqual(self.read_path(self.serv, ResPath.AdvancedFirmwareUpdate[Instances.APP].UpdateResult).content,
                         str(UpdateResult.SUCCESS).encode())
