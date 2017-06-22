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


class Test701_ConnectivityMonitoring_QueryingTheReadableResourcesOfObject(DataModel.Test):
    def runTest(self):
        # A READ operation from server on the resource has been received by the client.
        # This test has to be run on the following resources:
        # a) Network bearer
        # b) Available network bearer
        # c) Radio signal strength
        # d) Link quality
        # e) IP addresses
        # f) Router IP addresses
        # g) Link utilization
        # h) APN
        # i) Cell ID
        # j) SMNC
        # k) SMCC

        integer_array = ValueValidator.multiple_resource(ValueValidator.from_raw_int())
        string_array = ValueValidator.multiple_resource(ValueValidator.ascii_string())

        self.test_read(ResPath.ConnectivityMonitoring.NetworkBearer,          ValueValidator.integer(), coap.ContentFormat.TEXT_PLAIN)
        # TODO: this cannot be represented in text/plain format
        self.test_read(ResPath.ConnectivityMonitoring.AvailableNetworkBearer, integer_array,            coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.test_read(ResPath.ConnectivityMonitoring.RadioSignalStrength,    ValueValidator.integer(), coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.ConnectivityMonitoring.LinkQuality,            ValueValidator.integer(), coap.ContentFormat.TEXT_PLAIN)
        # TODO: this cannot be represented in text/plain format
        self.test_read(ResPath.ConnectivityMonitoring.IPAddresses,            string_array,             coap.ContentFormat.APPLICATION_LWM2M_TLV)
        # TODO: this cannot be represented in text/plain format
        self.test_read(ResPath.ConnectivityMonitoring.RouterIPAddresses,      string_array,             coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.test_read(ResPath.ConnectivityMonitoring.LinkUtilization,        ValueValidator.integer(), coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.ConnectivityMonitoring.APN,                    string_array,             coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.test_read(ResPath.ConnectivityMonitoring.CellID,                 ValueValidator.integer(), coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.ConnectivityMonitoring.SMNC,                   ValueValidator.integer(), coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.ConnectivityMonitoring.SMCC,                   ValueValidator.integer(), coap.ContentFormat.TEXT_PLAIN)


class Test710_ConnectivityMonitoring_ObservationAndNotificationOfObservableResources(DataModel.Test):
    def runTest(self):
        # The Server establishes an Observation relationship with the Client to acquire
        # condition notifications about observable resources. This test has to be run for
        # the following resources:
        # a) Network bearer
        # b) Available network bearer
        # c) Radio signal strength
        # d) Link quality
        # e) IP addresses
        # f) Router IP address
        # g) Link utilization
        # h) APN
        # i) Cell ID
        # j) SMNC
        # k) SMCC

        integer_array = ValueValidator.multiple_resource(ValueValidator.from_raw_int())
        string_array = ValueValidator.multiple_resource(ValueValidator.ascii_string())

        self.test_observe(ResPath.ConnectivityMonitoring.NetworkBearer,          ValueValidator.integer())
        self.test_observe(ResPath.ConnectivityMonitoring.AvailableNetworkBearer, integer_array)
        self.test_observe(ResPath.ConnectivityMonitoring.RadioSignalStrength,    ValueValidator.integer())
        self.test_observe(ResPath.ConnectivityMonitoring.LinkQuality,            ValueValidator.integer())
        self.test_observe(ResPath.ConnectivityMonitoring.IPAddresses,            string_array)
        self.test_observe(ResPath.ConnectivityMonitoring.RouterIPAddresses,      string_array)
        self.test_observe(ResPath.ConnectivityMonitoring.LinkUtilization,        ValueValidator.integer())
        self.test_observe(ResPath.ConnectivityMonitoring.APN,                    string_array)
        self.test_observe(ResPath.ConnectivityMonitoring.CellID,                 ValueValidator.integer())
        self.test_observe(ResPath.ConnectivityMonitoring.SMNC,                   ValueValidator.integer())
        self.test_observe(ResPath.ConnectivityMonitoring.SMCC,                   ValueValidator.integer())
