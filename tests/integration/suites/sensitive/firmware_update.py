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

import os
import socket

from framework.lwm2m.coap.server import SecurityMode
from framework.lwm2m_test import *
from framework import test_suite

from suites.default.block_write import Block, equal_chunk_splitter

UPDATE_STATE_IDLE = 0
UPDATE_STATE_UPDATING = 3
UPDATE_RESULT_SUCCESS = 1


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

        self.assertEqual(self.read_path(self.serv, ResPath.FirmwareUpdate.State).content.decode(),
                         str(UPDATE_STATE_UPDATING))

        self.communicate('set-fw-update-result ' + str(UPDATE_RESULT_SUCCESS))

        self.assertEqual(self.read_path(self.serv, ResPath.FirmwareUpdate.State).content.decode(),
                         str(UPDATE_STATE_IDLE))
        self.assertEqual(self.read_path(self.serv, ResPath.FirmwareUpdate.UpdateResult).content,
                         str(UPDATE_RESULT_SUCCESS).encode())
