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


class Test1200_QueryingReadableResourcesOfCellularConnectivityObject(DataModel.Test):
    def runTest(self):
        # 1. READ (CoAP GET /10/0) operation on the Instance of the Object
        #    ID:10 is received by the Client and its answer is sent back to the
        #    Server
        #
        # A. In test step 1 :the Server receives the success message (2.05 Content)
        #    associated to its READ request
        # B. In step 1: the values returned by the Client in TLV format is consistent
        #    with the Configuration C 10. used for that test (Activated Profine Names,
        #    Serving PLMN rate Control...).
        self.test_read('/%d/0' % OID.CellularConnectivity,
                       VV.tlv_instance(
                           resource_validators={
                               RID.CellularConnectivity.ServingPLMNRateControl: VV.from_raw_int(),
                               RID.CellularConnectivity.ActivatedProfileNames:  VV.multiple_resource(VV.ascii_string()),
                               # all other Resources are optional
                           },
                           ignore_extra=True))


class Test1201_QueryingReadableResourcesOfCellularConnectivityObjectInVersion1_1(DataModel.Test):
    def runTest(self):
        # 1. DISCOVER (CoAP GET Accept:40) operation is performed by the
        #    Server on Object ID:10
        #
        # A. In test step 1, the Server – along with the success message 2.05 –
        #    received the information related to Object ID:10 including
        #    </10>;ver="1.1",</10/0/6>,</10/0/11>, </10/0/13>, </10/0/14>
        link_list = self.test_discover('/%d' % OID.CellularConnectivity).decode()
        links = link_list.split(',')
        self.assertIn('</%d>;ver="1.1"' % (OID.CellularConnectivity,), links)
        self.assertIn('<%s>' % (ResPath.CellularConnectivity.ServingPLMNRateControl,), links)
        self.assertIn('<%s>' % (ResPath.CellularConnectivity.ActivatedProfileNames,), links)
        self.assertIn('<%s>' % (ResPath.CellularConnectivity.PowerSavingModes,), links)
        self.assertIn('<%s>' % (ResPath.CellularConnectivity.ActivePowerSavingModes), links)

        # 2. READ (CoAP GET /10/0) operation on the Instance of the Object
        #    ID:10 is received by the Client and its answer is sent back to the
        #    Server
        #
        # B. In test step 2 :the Server receives the success message (2.05 Content)
        #    associated to its READ request on Object Instance /10/0
        # C. In test step 2: the values returned by the Client in TLV format is
        #    consistent with the Configuration C 10. used for that test (Activated
        #    Profine Names, Serving PLMN rate Control, Power Saving Modes,
        #    Active Power Sa&ving Modes...).
        self.test_read('/%d/0' % OID.CellularConnectivity,
                       VV.tlv_instance(
                           resource_validators={
                               RID.CellularConnectivity.ServingPLMNRateControl: VV.from_raw_int(),
                               RID.CellularConnectivity.ActivatedProfileNames:  VV.multiple_resource(VV.ascii_string()),
                               RID.CellularConnectivity.PowerSavingModes:       VV.from_raw_int(),
                               RID.CellularConnectivity.ActivePowerSavingModes: VV.from_raw_int(),
                               # all other Resources are optional
                           },
                           ignore_extra=True))

class Test1210_SettingPowerSavingModeResourceOfCellularConnectivityObject(DataModel.Test):
    def runTest(self):
        # 1. DISCOVER (CoAP GET Accept:40) operation is performed by the
        #    Server on Object ID:10
        #
        # A. In test step 1, the Server – along with the success message 2.05 –
        #    received the information related to Object ID:10 including
        #    </10>;ver="1.1",</10/0/6>,</10/0/11>, </10/0/13>, </10/0/14>
        link_list = self.test_discover('/%d' % OID.CellularConnectivity).decode()
        links = link_list.split(',')
        self.assertIn('</%d>;ver="1.1"' % (OID.CellularConnectivity,), links)
        self.assertIn('<%s>' % (ResPath.CellularConnectivity.ServingPLMNRateControl,), links)
        self.assertIn('<%s>' % (ResPath.CellularConnectivity.ActivatedProfileNames,), links)
        self.assertIn('<%s>' % (ResPath.CellularConnectivity.PowerSavingModes,), links)
        self.assertIn('<%s>' % (ResPath.CellularConnectivity.ActivePowerSavingModes), links)

        # 2. A READ (CoAP GET /10/0/13) operation on the Instance 0 of the
        #    Object ID:10 is performed by the Server
        #
        # B. In test step 2 : the Server receives the success message (2.05 Content) and
        #    the requested value of the "Power Saving Modes" Resource (/10/0/13).
        #    This value must mach the value present in the C.11 configuration.
        self.assertEqual(b'3', self.test_read(ResPath.CellularConnectivity.PowerSavingModes))

        # 3. A WRITE operation is performed by the Server on the "Active Power
        #    Saving Modes" Resource (CoAP PUT/POST 10/0/14) of the Object
        #    ID:10 Instance 0, in using the set of values contains in the 1210-
        #    SetOfValues sample below.The data format TLV is used. (11542)
        #
        # C. In step 3, the Server receives the success message (2.04 Changed) related
        #    to the WRITE command
        self.test_write(ResPath.CellularConnectivity.ActivePowerSavingModes, '2')

        # 4. A READ (CoAP GET /10/0/14) operation is performed by the Server
        #    on the Instance 0 of the Object ID:10
        #
        # D. In test step 4 : the Server receives the success message (2.05 Content)
        #    associated to its READ request and the received value for the "Active
        #    Power Saving Modes" Resource" must match the value contains in the
        #    1210-SetOfValues sample.
        self.assertEqual(b'2', self.test_read(ResPath.CellularConnectivity.ActivePowerSavingModes))


class Test1230_ObservationAndNotificationOnCellularConnectivityObjectRelatedToPowerSavingModeResources(DataModel.Test):
    def runTest(self):
        # 1. DISCOVER (CoAP GET Accept:40) operation is performed by the
        #    Server on Object ID:10
        #
        # A. In test step 1, the Server – along with the success message 2.05 –
        #    received the information related to Object ID:10 including
        #    </10>;ver="1.1",</10/0/6>,</10/0/11>, </10/0/13>, </10/0/14>
        link_list = self.test_discover('/%d' % OID.CellularConnectivity).decode()
        links = link_list.split(',')
        self.assertIn('</%d>;ver="1.1"' % (OID.CellularConnectivity,), links)
        self.assertIn('<%s>' % (ResPath.CellularConnectivity.ServingPLMNRateControl,), links)
        self.assertIn('<%s>' % (ResPath.CellularConnectivity.ActivatedProfileNames,), links)
        self.assertIn('<%s>' % (ResPath.CellularConnectivity.PowerSavingModes,), links)
        self.assertIn('<%s>' % (ResPath.CellularConnectivity.ActivePowerSavingModes), links)

        # 2. Server communicates with the Object ID:10 in version 1.1 via
        #    WRITE-ATTRIBUTE (CoAP PUT) operations, to set the pmin &
        #    pmax Attributes of each targeted Resources (e.g.
        #    /10/0/13?pmin=5&pmax=15 and /10/0/14?pmin10&pmax=20)
        # 3. Server sends OBSERVE (CoAP GET operation with Observe Option
        #    set to 0) messages for the targeted Resources (ID:13 & ID:14 ) to
        #    activate reporting
        # 4. Client reports requested information with a NOTIFY message (CoAP
        #    responses)
        #
        # B. In test step 2, the Server receives success messages ("2.04" Changed)
        #    related to the WRITE-ATTRIBUTES operations
        # C. In test step 3, the Server receives success messages ("2.05" Content)
        #    along with the initial values of Resource ID:13 and ID:14
        # D. In test step 4, the Server regularly receives consistent information on the
        #    targeted Resources ID:13 & ID:14 of the Object ID:10 Instance 0.
        self.test_observe('/%d/0' % OID.CellularConnectivity,
                          VV.tlv_instance(
                              resource_validators={
                                  RID.CellularConnectivity.ServingPLMNRateControl: VV.from_raw_int(),
                                  RID.CellularConnectivity.ActivatedProfileNames:  VV.multiple_resource(VV.ascii_string()),
                                  RID.CellularConnectivity.PowerSavingModes:       VV.from_raw_int(),
                                  RID.CellularConnectivity.ActivePowerSavingModes: VV.from_raw_int(),
                                  # all other Resources are optional
                              },
                              ignore_extra=True),
                          pmin=1, pmax=3)


class Test1250_ApnConfiguration(DataModel.Test):
    def runTest(self):
        # 1. CREATE (COAP POST) operation is performed by the Server
        #    targeting APN Connection Profile Object (ID:11) to create a 2nd
        #    instance of the APN connection profile Object with a new APN
        #    which is not active yet.
        #
        # A. In test step 1., The Server receives a Success message ("2.01"
        #    Created) associated to the Instantiation of the APN connection
        #    profile Object.
        new_iid = self.test_create('/%d' % OID.ApnConnectionProfile,
                                   TLV.make_resource(RID.ApnConnectionProfile.ProfileName, 'NewProfile').serialize()
                                   + TLV.make_resource(RID.ApnConnectionProfile.AuthenticationType, 3).serialize()) # 3 == None

        # 2. The Server triggers a Registration Update Trigger for forcing an
        #    UPDATE registration from the Client
        #
        # B. In test step 2., the Client receives a Registration Update Trigger
        #    request on Resource ID:7 of the Device Object Instance (ID:3)
        # C. In test step 2., the Server receives the Success message ("2.04"
        #    Changed) associated to the Update Registration Request
        self.test_execute(ResPath.Server[0].RegistrationUpdateTrigger)

        # 3. UPDATE (registration) message (COAP POST) is sent from
        #    Client to Server including information about the supported
        #    Objects and Object Instances and namely the new Instance of
        #    the APN Connection Profile Object
        #
        # D. In test step 3, the Server receives an UPDATE (registration)
        #    operation from the Client along with the updated list of
        #    Object/Object Instances in that Client. This list contains the new
        #    Instance of the APN connection profile Object (/11/1)
        pkt = self.serv.recv()
        self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT), pkt)
        self.serv.send(Lwm2mChanged.matching(pkt)())

        self.assertIn(b'</%d/%d>' % (OID.ApnConnectionProfile, new_iid), pkt.content)

        # 4. Server activates the new APN Connection Profile in changing
        #    Enable status to True by WRITE operation (COAP PUT /11/1/3)
        #
        # E. In test step 4, the Server receives the Success message
        #    associated with the Server WRITE operation for the new APN
        #    activation.
        self.test_write(ResPath.ApnConnectionProfile[new_iid].EnableStatus, '1')

        # 5. Server reads the list of active APN Connection Profiles by
        #    performing a TLV READ targeting Resource ID:11 of Object
        #    10 (/10/11)
        # F. In test step 5., the Server receives a Success message ("2.05"
        #    Content) along with the list of the active APN containing the
        #    new Created APN (11:1)
        # NOTE: In case the device only supports one active APN profile this test is
        # passed when the new APN profile is activated.
        self.test_read('/%d/0' % OID.CellularConnectivity,
                       VV.tlv_instance(resource_validators={
                               RID.CellularConnectivity.ActivatedProfileNames : VV.multiple_resource(VV.objlnk((OID.ApnConnectionProfile, new_iid))),
                           },
                           ignore_extra=True))
