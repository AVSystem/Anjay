# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

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
