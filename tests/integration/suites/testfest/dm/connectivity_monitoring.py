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

from framework.lwm2m_test import *

from .utils import DataModel, ValueValidator as VV


class Test701_ConnectivityMonitoring_QueryingTheReadableResourcesOfObject(DataModel.Test):
    def runTest(self):
        integer_array = VV.multiple_resource(VV.from_raw_int())
        string_array = VV.multiple_resource(VV.ascii_string())

        # 1. READ (CoAP GET) operation is performed by the Server on the
        #    Connectivity Monitoring Object Instance of the Client
        #
        # A. In test step 1., the Client received a READ (Coap GET) command
        #    from the Server on the Connectivity Monitoring Object Instance
        # B. In test step 1., the Server receives the status code "2.05" for READ
        #    message success
        # C. In test step 1., along with the success message, the mandatory
        #    Resources (ID:0, 1,2, 4) and the optional ones, are received by the
        #    Server with expected values in compliance with LwM2M technical
        #    specification TS 1.0.
        self.test_read('/%d/0' % OID.ConnectivityMonitoring,
                       VV.tlv_instance(
                           resource_validators={
                               RID.ConnectivityMonitoring.NetworkBearer:          VV.from_raw_int(),
                               RID.ConnectivityMonitoring.AvailableNetworkBearer: integer_array,
                               RID.ConnectivityMonitoring.RadioSignalStrength:    VV.from_raw_int(),
                               RID.ConnectivityMonitoring.IPAddresses:            string_array,
                           },
                           ignore_extra=True))


class Test710_ConnectivityMonitoring_ObservationAndNotificationOfObservableResources(DataModel.Test):
    def runTest(self):
        # 1. The Server communicates to the Client the pmin=2 and pmax=10
        #    periods, threshold values with a WRITE ATTRIBUTE (CoAP
        #    PUT) operation at the Connectivity Monitoring Object Instance
        #    level
        # 2. The Server sends OBSERVE (CoAP Observe Option) message
        #    to activate reporting on the Monitoring Object Instance
        # 3. The Client reports requested information with a NOTIFY
        #    message (CoAP response)
        #
        # A. In test step 1., the Client has received a WRITE ATTRIBUTE
        #    Command (CoAP PUT) targeting the Connectivity Monitoring
        #    Object Instance with the proper pmin=2, and pmax=10
        #    parameters
        # B. In test step 1, the Server received the success message (2.04
        #    Changed) in response to the WRITE ATTRIBUTE command
        # C. In test step 2. the Client received the OBSERVE operation
        #    targeting the Connectivity Monitoring Object Instance
        # D. In test step 2., in response to its OBSERVE request, the Server
        #    receives the success message (Content 2.05) along with the
        #    Connectivity Object Instance initial values
        # E. In test step 3., based on pmin/pmax periods parameters received
        #    in test step 1., the Client reports Information to the Server with a
        #    NOTIFY message containing the Connectivity Object Instance
        #    updated values.
        # F. In test step 3., the values receives by the Server along with the
        #    success message (Content 2.05) must be as expected
        self.test_observe('/%d/0' % OID.ConnectivityMonitoring,
                          VV.tlv_instance(resource_validators={},
                                          ignore_extra=True),
                          pmin=2, pmax=10)
