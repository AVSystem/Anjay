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


class RequestTooLarge(test_suite.Lwm2mSingleServerTest):
    def setUp(self):
        super().setUp(extra_cmdline_args=['-I', '1000'])

    def runTest(self):
        req = Lwm2mWrite(ResPath.FirmwareUpdate.Package, random_stuff(1200))
        self.serv.send(req)
        res = self.serv.recv()
        self.assertMsgEqual(
            Lwm2mErrorResponse.matching(req)(coap.Code.RES_REQUEST_ENTITY_TOO_LARGE), res)
