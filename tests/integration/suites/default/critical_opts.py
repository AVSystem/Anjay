# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

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
