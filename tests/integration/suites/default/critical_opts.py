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

import unittest

class CriticalOptsTest(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        # This should result in 4.02 Bad Option response.
        pkt = Lwm2mRead(ResPath.Server[1].ShortServerID, options=[coap.Option.IF_NONE_MATCH])
        self.serv.send(pkt)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(pkt)(code=coap.Code.RES_BAD_OPTION),
                            self.serv.recv())

        # And this shuld work.
        pkt = Lwm2mRead(ResPath.Server[1].ShortServerID)
        self.serv.send(pkt)
        self.assertMsgEqual(Lwm2mContent.matching(pkt)(),
                            self.serv.recv())
