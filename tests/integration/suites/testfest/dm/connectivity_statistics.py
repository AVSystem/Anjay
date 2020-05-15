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

import time

from framework.lwm2m_test import *

from .utils import DataModel, ValueValidator as VV


class Test901_ConnectivityStatistics_QueryingADataCollectionFromConnectivityObjectInstance(DataModel.Test):
    def runTest(self):
        # 1. EXECUTE operation (CoAP POST) is performed on the Start
        #    Resource of the Connectivity Statistics Object Instance
        #
        # A. In test step 1., the Client receives a Start command for the
        #    Connectivity Statistics Object Instance (CoAP POST)
        # B. In test step 1., the Server receives the success message ("2.4"
        #    Changed) in response of its EXECUTE command on the Client
        self.test_execute(ResPath.ConnectivityStatistics.Start)

        # 2. After few seconds, an EXECUTE operation (CoAP POST) is
        #    performed on the Stop Ressource of the Connectivity Statistics
        #    Object Instance
        #
        # C. In test step 2., the Client receives a Stop command for the
        #    Connectivity Statistics Object Instance (CoAP POST)
        # D. In test step 2., the Server receives the success message ("2.4"
        #    Changed) in response of its EXECUTE command on the Client
        time.sleep(5)
        self.test_execute(ResPath.ConnectivityStatistics.Stop)

        # 3. A READ (CoAP GET) operation is performed by the Server on
        #    the Connectivity Object Instance of the Client
        #
        # E. In test step 3., the Client receives a READ operation on the
        #    Connectivity Object Instance
        # F. In test step 3., the Server receives the status code "2.05" for the
        #    READ message success
        # G. In test step 3., along with the success message, the Server receives the
        #    Connectivity Statistics Object Instance value according to the Client
        #    preferred data format.
        # H. In test step 3, the received Connectivity Statistics Object Instance
        #    contains expected values in compliance with the LwM2M technical
        #    specification 1.0, for the mandatory Resources (Network Bearer,
        #    Available Network Bearer, Radio Signal Bearer, IP Address), and the
        #    optional ones
        #
        # NOTE: listed Resources are from Connectivity Monitoring, not Statistics
        self.test_read('/%d/0' % OID.ConnectivityStatistics,
                       VV.tlv_instance(resource_validators={
                           RID.ConnectivityStatistics.SMSTxCounter:       VV.from_raw_int(),
                           RID.ConnectivityStatistics.SMSRxCounter:       VV.from_raw_int(),
                           RID.ConnectivityStatistics.RxData:             VV.from_raw_int(),
                           RID.ConnectivityStatistics.TxData:             VV.from_raw_int(),
                           RID.ConnectivityStatistics.MaxMessageSize:     VV.from_raw_int(),
                           RID.ConnectivityStatistics.AverageMessageSize: VV.from_raw_int(),
                           RID.ConnectivityStatistics.CollectionPeriod:   VV.from_raw_int(),
                        }, ignore_missing=True)) # all readable Resources are optional


class Test905_ConnectivityStatistics_SettingTheWritableResources(DataModel.Test):
    def runTest(self):
        # 1. A READ (CoAP GET) operation is performed by the Server on the
        #    Connectivity Object Instance of the Client
        #
        # A. In test step 1., the Client receives a READ operation on the
        #    Connectivity Object Instance
        # B. In test step 1., the Server receives the status code "2.05" for the
        #    READ success message along with the Connectivity Statistics Object
        #    Instance value according to the Client preferred data format.
        prev_values = self.test_read('/%d/0' % OID.ConnectivityStatistics)
        prev_values_tlv = TLV.parse(prev_values)
        prev_collection_period_raw = [x for x in prev_values_tlv if x.identifier == RID.ConnectivityStatistics.CollectionPeriod][0].value
        prev_collection_period = struct.unpack('>Q', prev_collection_period_raw.rjust(8, b'\0'))[0]

        # 2. A WRITE (CoAP PUT) operation is performed on the Client
        #    targeting the Collection Period Resource (ID:8) of the Connectivity
        #    Object Instance with a value different of the one collected in step 2.
        # 3. A new READ (CoAP GET) operation is performed by the Server on
        #    the Connectivity Object Instance of the Client
        #
        # C. In step 2, the Client receives a WRITE command (CoAP PUT)
        #    targeting the Collection Period of the Connectivity Statistics Object
        #    Instance with a value different of the one collected in pass B.
        # D. In test step 2, the Server received receives a success message (2.04
        #    Changed) related to its WRITE operation
        # E. In test step 3., the Client receives a new READ operation on the
        #    Connectivity Object Instance
        # F. In step 3, the Server receives success message (2.05 Content) and the
        #    requested value in the LwM2M Client preferred data format
        # G. The value of the Collection Period Resource value collected in Pass F,
        #    correspond to the value which was set in step 2
        self.test_write_validated(ResPath.ConnectivityStatistics.CollectionPeriod,
                                  str(prev_collection_period + 1))


class Test910_ConnectivityStatistics_BasicObservationAndNotificationOnConnectivityStatisticsObjectInstance(DataModel.Test):
    def runTest(self):
        validator = VV.tlv_instance(resource_validators={
                                        RID.ConnectivityStatistics.SMSTxCounter:       VV.from_raw_int(),
                                        RID.ConnectivityStatistics.SMSRxCounter:       VV.from_raw_int(),
                                        RID.ConnectivityStatistics.RxData:             VV.from_raw_int(),
                                        RID.ConnectivityStatistics.TxData:             VV.from_raw_int(),
                                        RID.ConnectivityStatistics.MaxMessageSize:     VV.from_raw_int(),
                                        RID.ConnectivityStatistics.AverageMessageSize: VV.from_raw_int(),
                                        RID.ConnectivityStatistics.CollectionPeriod:   VV.from_raw_int(),
                                    }, ignore_missing=True) # all readable Resources are optional

        # 1. The Server communicates to the Client pmin=2 & pmax=10
        #    periods with a WRITE ATTRIBUTE (CoAP PUT) operation at
        #    the Connectivity Statistics Instance level.
        #
        # A. In test step 1., the Server received a WRITE ATTRIBUTE command
        #    (CoAP PUT) targeting the Connectivity Statistics Object Instance
        #    with the proper pmin=2 and pmin=10 parameters.
        # B. In test step 1., the Server received the success message (2.04
        #    Changed) in response to the WRITE ATTRIBUTE command
        self.test_write_attributes('/%d/0' % OID.ConnectivityStatistics,
                                   pmin=2, pmax=10)

        # 2. The Server set (CoAP PUT) the Collection Period Resource to 0
        #    in the Connectivity Statistics Object Instance.
        #
        # C. In test step 2 the Client received the OBSERVE operation targeting
        #    the Connectivity Statistics Object Instance
        self.test_write(ResPath.ConnectivityStatistics.CollectionPeriod, '0')

        # 3. The Server sends OBSERVE (CoAP Observe Option) message to
        #    activate reporting on the Connectivity Statistics Instance.
        #
        # D. In test step2, in response to its OBSERVE operation, the Server
        #    receives the success message (Content 2.05) along with the initial
        #    values of the Connectivity Statistics Object Instance
        req = Lwm2mObserve('/%d/0' % OID.ConnectivityStatistics)
        self.serv.send(req)
        res = self.serv.recv()
        self.assertMsgEqual(Lwm2mContent.matching(req)(), res)
        validator.validate(res.content)

        # 4. The Server starts the data collection on the Connectivity Staistics
        #    Object Instance by triggering the Start Resource of this Object
        #    Instance (CoAP POST)
        self.test_execute(ResPath.ConnectivityStatistics.Start)

        # 5. The Client reports requested information with NOTIFY messages
        #    (CoAP response)
        # 6. Several NOTIFY messages are received by the Server related to
        #    the Connectivity Statistics Object Instance
        #
        # E. In test step 3, based on pmin/pmax periods parameters received in test
        #    step 1., the Client reports information to the Server with NOTIFY
        #    messages containing the updated values of the Connectivity Statistics.
        #
        # F. In test step 3., the values returned by the Server along with the
        #    success message (Content 2.05) must be as expected.
        self.test_expect_notify(token=req.token, validator=validator, timeout_s=10.5)
        self.test_expect_notify(token=req.token, validator=validator, timeout_s=10.5)

        # 7. The Server stops the data collection on the Connectivity Staistics
        #    Object Instance by triggering the Stop Resource of this Object
        #    Instance (CoAP POST)
        self.test_execute(ResPath.ConnectivityStatistics.Stop)

        # 8. The Server stops the OBSERVATION process
        self.test_cancel_observe('/%d/0' % OID.ConnectivityStatistics)

