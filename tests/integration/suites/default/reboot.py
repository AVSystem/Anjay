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

from framework.lwm2m_test import *


class RebootSendsResponseTest(test_suite.Lwm2mSingleServerTest):
    def _get_valgrind_args(self):
        # Reboot cannot be performed when demo is run under valgrind
        return []

    def runTest(self):
        self.serv.set_timeout(timeout_s=1)

        # should send a response before rebooting
        req = Lwm2mExecute(ResPath.Device.Reboot)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        # should register after rebooting
        self.serv.reset()
        self.assertDemoRegisters(self.serv)
