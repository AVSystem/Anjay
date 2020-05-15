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


class Test801_Location_QueryingTheResourcesOfObject(DataModel.Test):
    def runTest(self):
        # 1. READ (CoAP GET) is performed by the Server on the Location
        #    Object Instance of the Client
        #
        # A. In test step 1., the Client receives a READ (CoAP GET) command
        #    from the Server on the Location Object Instance
        # B. In test step 1., the Server receives the status code "2.05" for READ
        #    message success
        # C. In test step 1., along with the success message, the mandatory
        #    Resources (Latitude, Longitude, Timestamp) and optional ones, are
        #    received by the Server with expected values in compliance with
        #    LwM2M technical specification 1.0
        self.test_read('/%d/0' % OID.Location,
                       VV.tlv_instance(resource_validators={
                            RID.Location.Latitude: VV.float(),
                            RID.Location.Longitude: VV.float(),
                            RID.Location.Timestamp: VV.from_raw_int(),
                           }, ignore_extra=True))


class Test810_Location_ObservationAndNotificationOfObservableResources(DataModel.Test):
    def runTest(self):
        # 1. The Server communicates to the Client pmin=2 pmax=10 period
        #    threshold values with a WRITE ATTRIBUTE (CoAP PUT)
        #    operation at the Location Object Instance level
        # 2. The Server sends OBSERVE (CoAP Observe Option) message
        #    to activate reporting on the Location Object Instance.
        # 3. The Client reports requested information with a NOTIFY
        #    message (CoAP response)
        #
        # A. In test step 1., the Server received a WRITE ATTRIBUTE command
        #    (CoAP PUT) targeting the Location Object Instance with the proper
        #    pmin=2 and pmin=10 parameters.
        # B. In test step 1., the Server received the success message (2.04
        #    Changed) in response to the WRITE ATTRIBUTE command
        # C. In test step 2., the Client received the OBSERVE operation targeting
        #    the Location Object Instance
        # D. In test step 2., in response to its OBSERVE operation, the Server
        #    receives the success message (Content 2.05) along with the initial
        #    values of the Location Object Instance
        # E. In test step 3., based on pmin/pmax periods parameters received in
        #    test step 1., the Client reports information to the Server with NOTIFY
        #    messages containing the Location Object Instance updated values.
        # F. In test step 3., the values received by the Server along with the
        #    success message (Content 2.05) must be as expected : at less the
        #    Mandatory Timestamp Resource must have admissible values
        #    according to the pmin and pmax parameters.
        self.test_observe('/%d/0' % OID.Location,
                          VV.tlv_instance(resource_validators={
                               RID.Location.Latitude: VV.float(),
                               RID.Location.Longitude: VV.float(),
                               RID.Location.Timestamp: VV.from_raw_int(),
                              }, ignore_extra=True),
                          pmin=2, pmax=10)
