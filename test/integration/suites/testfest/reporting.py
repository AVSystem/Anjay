from framework.lwm2m_test import *

import unittest
import socket

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

        self.test_observe(ResPath.Device.PowerSourceVoltage,                  int_array)
        self.test_observe(ResPath.ConnectivityMonitoring.RadioSignalStrength, ValueValidator.float())

        # 1. The server stops receiving information on “Line Voltage” and
        #    “Signal Strength” and associated entries from the list of
        #    observers are removed.
