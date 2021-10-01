# -*- coding: utf-8 -*-
#
# Copyright 2017-2021 AVSystem <avsystem@avsystem.com>
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


class IpsoBasicSensorObjectReadTest(test_suite.Lwm2mSingleServerTest,
                                    test_suite.Lwm2mDmOperations):
    def runTest(self):
        # Read and check the unit
        unit = (self.read_resource(self.serv, OID.Temperature,
                0, RID.Temperature.SensorUnits)).content
        self.assertEqual(unit, b'Cel')

        # Read min and max value which can be measured by the sensor
        lower_bound = float((self.read_resource(
            self.serv, OID.Temperature, 0, RID.Temperature.MinRangeValue)).content)
        upper_bound = float((self.read_resource(
            self.serv, OID.Temperature, 0, RID.Temperature.MaxRangeValue)).content)

        # Read the sensor value
        response_1 = float((self.read_resource(
            self.serv, OID.Temperature, 0, RID.Temperature.SensorValue)).content)

        # Check if the measured value fits in range
        self.assertGreaterEqual(response_1, lower_bound)
        self.assertLessEqual(response_1, upper_bound)

        # Read the sensor value
        response_2 = float((self.read_resource(
            self.serv, OID.Temperature, 0, RID.Temperature.SensorValue)).content)

        # Check if the measured value fits in range
        self.assertGreaterEqual(response_2, lower_bound)
        self.assertLessEqual(response_2, upper_bound)

        # The values of the two consequtive reads should be different
        # (not in general, ut in the case of the demo)
        self.assertNotEqual(response_1, response_2)


class Ipso3dSensorObjectReadTest(test_suite.Lwm2mSingleServerTest,
                                 test_suite.Lwm2mDmOperations):
    def runTest(self):
        # Read and check the unit
        unit = (self.read_resource(self.serv, OID.Accelerometer,
                0, RID.Accelerometer.SensorUnits)).content
        self.assertEqual(unit, b'm/s2')

        # Read min and max value which can be measured by the sensor
        lower_bound = float((self.read_resource(
            self.serv, OID.Accelerometer, 0, RID.Accelerometer.MinRangeValue)).content)
        upper_bound = float((self.read_resource(
            self.serv, OID.Accelerometer, 0, RID.Accelerometer.MaxRangeValue)).content)

        # We test all of the axes
        for rid in [RID.Accelerometer.XValue, RID.Accelerometer.YValue, RID.Accelerometer.ZValue]:

            # Read the sensor value
            response_1 = float(
                (self.read_resource(self.serv, OID.Accelerometer, 0, rid)).content)

            # Check if the measured value fits in range
            self.assertGreaterEqual(response_1, lower_bound)
            self.assertLessEqual(response_1, upper_bound)

            # Read the sensor value
            response_2 = float(
                (self.read_resource(self.serv, OID.Accelerometer, 0, rid)).content)

            # Check if the measured value fits in range
            self.assertGreaterEqual(response_2, lower_bound)
            self.assertLessEqual(response_2, upper_bound)

            # The values of the two consequtive reads should be different
            # (not in general, ut in the case of the demo)
            self.assertNotEqual(response_1, response_2)


class IpsoButtonObjectReadTest(test_suite.Lwm2mSingleServerTest,
                               test_suite.Lwm2mDmOperations):
    def runTest(self):
        # Read and check the unit
        at = (self.read_resource(self.serv, OID.PushButton,
              0, RID.PushButton.ApplicationType)).content
        self.assertEqual(at, b'Fake demo Button')

        # Write Application Type and check if it changes
        new_button_name = b'New button name'
        self.write_resource(self.serv, OID.PushButton, 0,
                            RID.PushButton.ApplicationType, content=new_button_name)
        at = (self.read_resource(self.serv, OID.PushButton,
              0, RID.PushButton.ApplicationType)).content
        self.assertEqual(at, new_button_name)

        # Press and release button several times
        button_clicks = 9
        for _ in range(button_clicks):
            self.communicate("push-button-press 0")
            self.communicate("push-button-release 0")

        # Check the number of times pressed and state
        times_pressed = int((self.read_resource(
            self.serv, OID.PushButton, 0, RID.PushButton.DigitalInputCounter)).content)
        self.assertEqual(times_pressed, button_clicks)
        state = int((self.read_resource(self.serv, OID.PushButton,
                    0, RID.PushButton.DigitalInputState)).content)
        self.assertEqual(state, 0)

        # Press it one more time
        self.communicate("push-button-press 0")

        # Check the number of times pressed and state
        times_pressed = int((self.read_resource(
            self.serv, OID.PushButton, 0, RID.PushButton.DigitalInputCounter)).content)
        self.assertEqual(times_pressed, button_clicks+1)
        state = int((self.read_resource(self.serv, OID.PushButton,
                    0, RID.PushButton.DigitalInputState)).content)
        self.assertEqual(state, 1)
