# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

import re

from framework.lwm2m_test import *


class TimeAPIChecks:
    class Test(test_suite.Lwm2mTest):
        def setUp(self):
            self.setup_demo_with_servers(servers=1)

        def validTime(self, time):
            r = re.match('([0-9]+)\.([0-9]+)', time)
            return r is not None

        # Expects the log to be in format
        # [NAME_OF_TIMESTAMP]=[point in time according to the real-time clock]
        def readTimeFromLog(self, cmd, strTimestampName):
            reg_exp = strTimestampName + '=([0-9]+\.[0-9]+)'
            log_line = self.communicate(cmd, 5, reg_exp)
            self.assertIsNotNone(log_line)
            self.assertTrue(self.validTime(log_line.group(1)))
            return float(log_line.group(1))

        def readAllTimestampsFromLog(self, ssid=None):
            cmd_suffix = ''
            if ssid is not None:
                cmd_suffix = ' ' + str(ssid)

            reg_time = self.readTimeFromLog(
                'last-registration-time' + cmd_suffix, 'LAST_REGISTRATION_TIME')
            next_update_time = self.readTimeFromLog(
                'next-update-time' + cmd_suffix, 'NEXT_UPDATE_TIME')
            last_comm_time = self.readTimeFromLog(
                'last-communication-time' + cmd_suffix, 'LAST_COMMUNICATION_TIME')

            return reg_time, next_update_time, last_comm_time


class RegistrationTimeNotChanged(TimeAPIChecks.Test):
    def runTest(self):
        reg_time_1, next_update_time_1, last_comm_time_1 = self.readAllTimestampsFromLog()

        # this won't cause an update to be sent
        self.communicate('advance-time 3600')
        time.sleep(5)

        # the returned times should stay the same
        reg_time_2, next_update_time_2, last_comm_time_2 = self.readAllTimestampsFromLog()

        self.assertEqual(reg_time_1, reg_time_2)
        self.assertAlmostEqual(next_update_time_1, next_update_time_2, delta=0.005)
        self.assertEqual(last_comm_time_1, last_comm_time_2)


class RegistrationTimeAfterUpdate(TimeAPIChecks.Test):
    def runTest(self):
        self.servers[0].set_timeout(timeout_s=1)

        reg_time_1, next_update_time_1, last_comm_time_1 = self.readAllTimestampsFromLog()

        self.communicate('advance-time 3600')
        self.communicate('send-update')  # force update
        pkt = self.servers[0].recv()
        self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT,
                                        query=[],
                                        content=b''),
                            pkt)

        self.servers[0].send(Lwm2mChanged.matching(pkt)())

        reg_time_2, next_update_time_2, last_comm_time_2 = self.readAllTimestampsFromLog()

        self.assertLess(reg_time_1, reg_time_2)
        self.assertLess(next_update_time_1, next_update_time_2)
        self.assertLess(last_comm_time_1, last_comm_time_2)


class RegistrationTimeMultipleServers(TimeAPIChecks.Test):
    def setUp(self):
        self.setup_demo_with_servers(servers=2)

    def runTest(self):
        reg_time, next_update_time, last_comm_time = self.readAllTimestampsFromLog()
        srv_1_reg_time, srv_1_next_update_time, srv_1_last_comm_time = self.readAllTimestampsFromLog(
            1)
        srv_2_reg_time, srv_2_next_update_time, srv_2_last_comm_time = self.readAllTimestampsFromLog(
            2)

        self.assertEqual(reg_time, srv_2_reg_time)
        self.assertAlmostEqual(next_update_time, srv_1_next_update_time, delta=0.005)
        self.assertEqual(last_comm_time, srv_2_last_comm_time)

        # check if srv_1 and srv_2 times are different and that we are not returing
        # the same value
        self.assertNotEqual(srv_1_reg_time, srv_2_reg_time)
        self.assertNotEqual(srv_1_next_update_time, srv_2_next_update_time)
        self.assertNotEqual(srv_1_last_comm_time, srv_2_last_comm_time)


class RegistrationTimeAfterUpdateMultipleServers(TimeAPIChecks.Test):
    def setUp(self):
        self.setup_demo_with_servers(servers=2)

    def runTest(self):
        self.servers[0].set_timeout(timeout_s=1)

        srv_1_reg_time_old, srv_1_next_update_time_old, srv_1_last_comm_time_old = self.readAllTimestampsFromLog(
            1)
        srv_2_reg_time_old, srv_2_next_update_time_old, srv_2_last_comm_time_old = self.readAllTimestampsFromLog(
            2)

        self.communicate('advance-time 3600')
        self.communicate('send-update 1')  # update only server 1
        pkt = self.servers[0].recv()
        self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT,
                                        query=[],
                                        content=b''),
                            pkt)

        self.servers[0].send(Lwm2mChanged.matching(pkt)())

        srv_1_reg_time_new, srv_1_next_update_time_new, srv_1_last_comm_time_new = self.readAllTimestampsFromLog(
            1)
        srv_2_reg_time_new, srv_2_next_update_time_new, srv_2_last_comm_time_new = self.readAllTimestampsFromLog(
            2)

        # times for server 1 should be updated for server 2 should stay the same
        self.assertLess(srv_1_reg_time_old, srv_1_reg_time_new)
        self.assertLess(srv_1_next_update_time_old, srv_1_next_update_time_new)
        self.assertLess(srv_1_last_comm_time_old, srv_1_last_comm_time_new)

        self.assertEqual(srv_2_reg_time_old, srv_2_reg_time_new)
        self.assertAlmostEqual(srv_2_next_update_time_old, srv_2_next_update_time_new, delta=0.005)
        self.assertEqual(srv_2_last_comm_time_old, srv_2_last_comm_time_new)
