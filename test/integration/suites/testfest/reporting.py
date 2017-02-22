# -*- coding: utf-8 -*-
#
# Copyright 2017 AVSystem <avsystem@avsystem.com>
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

from .dm.utils import *


class Test301_ObservationAndNotificationOfParameterValues(DataModel.Test):
    def runTest(self):
        VV = ValueValidator
        int_array = VV.tlv_resources(VV.tlv_multiple_resource(VV.from_raw_int()))

        self.test_observe(ResPath.Device.PowerSourceVoltage,
                          int_array,
                          pmin=1, pmax=2, gt=0, lt=100000, st=1)
        self.test_observe(ResPath.ConnectivityMonitoring.RadioSignalStrength,
                          ValueValidator.float(),
                          pmin=1, pmax=2, gt=0, lt=100000, st=1)

        # 1. The server has received the requested information and display of
        #    “Line Voltage” and “Signal Strength” to the user is possible


class Test302_CancelObservationsUsingResetOperation(DataModel.Test):
    def runTest(self):
        VV = ValueValidator
        int_array = VV.tlv_resources(VV.tlv_multiple_resource(VV.from_raw_int()))

        self.test_observe(ResPath.Device.PowerSourceVoltage, int_array)
        self.test_observe(ResPath.ConnectivityMonitoring.RadioSignalStrength, ValueValidator.float())

        # 1. The server stops receiving information on “Line Voltage” and
        #    “Signal Strength” and associated entries from the list of
        #    observers are removed.
