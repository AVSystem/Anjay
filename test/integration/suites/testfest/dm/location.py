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

from .utils import DataModel, ValueValidator


class VelocityValidator(ValueValidator):
    def validate(self, value):
        # TODO: implement some kind of actual validation - 3GPP velocity
        # description is complex as hell
        type = value[0]
        if (type & 0xF0) not in (0, 1, 2, 3):
            raise ValueError('unexpected velocity type')


class Test801_Location_QueryingTheResourcesOfObject(DataModel.Test):
    def runTest(self):
        # A READ operation from server on the resource has been received by the
        # client. This test has to be run on the following resources:
        # a) Latitude
        # b) Longitude
        # c) Altitude
        # d) Uncertainty
        # e) Velocity
        # f) Timestamp

        self.test_read(ResPath.Location.Latitude,    ValueValidator.float_as_string(), coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.Location.Longitude,   ValueValidator.float_as_string(), coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.Location.Altitude,    ValueValidator.float_as_string(), coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.Location.Uncertainty, ValueValidator.float_as_string(), coap.ContentFormat.TEXT_PLAIN)
        # TODO: cannot be represented in text/plain format, as test requires
        self.test_read(ResPath.Location.Velocity,    VelocityValidator(),              coap.ContentFormat.APPLICATION_OCTET_STREAM)
        # TODO: According to LwM2M spec, Timestamp values should be in range 0-6 (???)
        self.test_read(ResPath.Location.Timestamp,   ValueValidator.integer(),         coap.ContentFormat.TEXT_PLAIN)


class Test810_Location_ObservationAndNotificationOfObservableResources(DataModel.Test):
    def runTest(self):
        # The Server establishes an Observation relationship with the Client to acquire
        # condition notifications about observable resources. This test has to be run for
        # the following resources:
        # a) Latitude
        # b) Longitude
        # c) Altitude
        # d) Uncertainty
        # e) Velocity
        # f) Timestamp

        self.test_observe(ResPath.Location.Latitude,    ValueValidator.float_as_string())
        self.test_observe(ResPath.Location.Longitude,   ValueValidator.float_as_string())
        self.test_observe(ResPath.Location.Altitude,    ValueValidator.float_as_string())
        self.test_observe(ResPath.Location.Uncertainty, ValueValidator.float_as_string())
        self.test_observe(ResPath.Location.Velocity,    VelocityValidator())
        # TODO: According to LwM2M spec, Timestamp values should be in range 0-6 (???)
        self.test_observe(ResPath.Location.Timestamp,   ValueValidator.integer())
