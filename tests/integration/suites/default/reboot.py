# -*- coding: utf-8 -*-
#
# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

from framework_tools.utils.lwm2m_test import *


class RebootSendsResponseTest(test_suite.Lwm2mSingleServerTest):
    def _get_valgrind_args(self):
        # Reboot cannot be performed when demo is run under valgrind
        return []

    def runTest(self):
        # should send a response before rebooting
        req = Lwm2mExecute(ResPath.Device.Reboot)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        # should register after rebooting
        self.serv.reset()
        self.assertDemoRegisters(self.serv)
