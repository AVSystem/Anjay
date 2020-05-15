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

import unittest
from framework.lwm2m_test import *

from .dm.utils import *


class Test301_ObservationAndNotificationOfParameterValues(DataModel.Test):
    def runTest(self):
        VV = ValueValidator
        int_array = VV.tlv_resources(VV.tlv_multiple_resource(VV.from_raw_int()))

        # 1. Server Determines which Power Sources are on the Device (READ
        #    access to Resource ID:6 of Device Object ID:3)
        self.test_read(ResPath.Device.AvailablePowerSources)

        # 2. Server communicates with the Device Object ID:3 via WRITE-ATTRIBUTE
        #    (CoAP PUT) operations, to set the pmin & pmax
        #    Attributes of each targeted Resources (e.g.
        #    /3/0/7?pmin=5&pmax=15 and /3/0/8?pmin10&pmax=20)
        # 3. Server sends OBSERVE (CoAP GET operation with Observe
        #    Option set to 0) messages for the targeted Resources (ID:7 & ID:8 )
        #    to activate reporting
        # 4. Client reports requested information with a NOTIFY message
        #    (CoAP responses)
        #
        # A. The Server regularly receives the requested information and
        #    displays "Power Source Voltage" and "Power Source Current"
        #    values to the user if possible.
        self.test_observe(ResPath.Device.PowerSourceVoltage,
                          int_array, pmin=1, pmax=2, st=1)
        self.test_observe(ResPath.Device.PowerSourceCurrent,
                          int_array, pmin=1, pmax=2, st=1)


@unittest.skip("Covered by test 301")
class Test302_CancelObservationsUsingResetOperation(DataModel.Test):
    def runTest(self):
        # 1. Client reports requested information with NOTIFY messages
        #    (CoAP responses) on Resource ID:7 and ID:8
        # 2. On receving a NOTIFY message from Resource ID:7, the
        #    Server sends a Cancel Observation (CoAP RESET message) to
        #    remove the Observation relationship with that Resource .
        # 3. On receving a NOTIFY message from Resource ID:8, the
        #    Server sends a Cancel Observation (CoAP RESET message) to
        #    remove the Observation relationship with that Resource .
        # 4. Client stops reporting requested information and removes
        #    associated entries from the list of observers
        #
        # A. The Server receives regular NOTIFICATION from Device
        #    Object ID:3 on Resources Power Source Voltage (ID:7) and
        #    Power Source Current (ID:8)
        # B. The Server stops receiving information first on "Power Source
        #    Voltage" then on "Power Source Current" after having sent the
        #    Cancel Observation (Reset Operation) on that Resources.
        #    Associated entries from the list of observers are removed.
        pass


class Test303_CancelObservationsUsingCancelParameter(DataModel.Test):
    def runTest(self):
        VV = ValueValidator
        int_array = VV.tlv_resources(VV.tlv_multiple_resource(VV.from_raw_int()))

        # 1. Client reports requested information with a NOTIFY message
        #    (CoAP responses)
        # 2. After receiving few notifications, Server sends OBSERVE
        #    operation with the CANCEL parameter (CoAP GET message
        #    with Observe option set to 1) to cancel the Observation
        #    relationship with the Instance of the Device Object (/3/0)
        # 3. Client stops reporting requested information on both Resources
        #    and removes associated entries from the list of observers.
        #
        # A. The Server receives regular NOTIFICATION from Device
        #    Object ID:3 on Resources Power Source Voltage (ID:7) and
        #    Power Source Current (ID:8)
        # B. The Server stops receiving information on "Power Source
        #    Voltage" and "Power Source Current" after having sent the
        #    Cancel Observation (with Cancel parameter) on the Device
        #    Object Instance (/3/0).Associated entries from the list of
        #    observers are removed.
        self.test_observe(ResPath.Device.PowerSourceVoltage,
                          int_array, cancel_observe=CancelObserveMethod.ObserveOption,
                          pmin=1, pmax=2, st=1)
        self.test_observe(ResPath.Device.PowerSourceCurrent,
                          int_array, cancel_observe=CancelObserveMethod.ObserveOption,
                          pmin=1, pmax=2, st=1)
