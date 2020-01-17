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


class Test651_DeviceObject_QueryingTheReadableResourcesOfObject(DataModel.Test):
    def runTest(self):
        # Precondition:
        # The Initial values of the Device Object (ID:3) Instance, are saved on
        # the Server
        rids_to_restore = (RID.Device.CurrentTime,
                           RID.Device.UTCOffset,
                           RID.Device.Timezone)
        prev_values = self.test_read('/%d/0' % OID.Device)
        prev_values_tlv = TLV.parse(prev_values)
        values_to_restore = [x for x in prev_values_tlv if x.identifier in rids_to_restore]

        tlv = TLV.make_instance(0, [
            TLV.make_resource(RID.Device.CurrentTime, 1367491215),
            TLV.make_resource(RID.Device.UTCOffset, '+02:00'),
            TLV.make_resource(RID.Device.Timezone, 'Europe/Paris')
            ])

        # 1. A 1st WRITE (CoAP POST) operation on the Device Object (ID:3)
        #    Instance is performed in using the set of values contains in the
        #    651-SetOfValues sample below.The data format TLV is used. (11542)
        self.test_write('/%d/0' % OID.Device, tlv.serialize(),
                        coap.ContentFormat.APPLICATION_LWM2M_TLV,
                        update=True)

        # 2. The Server READs the result of the WRITE operation by querying
        #    the Device Object (ID:3) Instance.
        self.test_read('/%d/0' % OID.Device,
                       VV.tlv_instance(
                           resource_validators={
                               RID.Device.CurrentTime: VV.from_raw_int(),
                               RID.Device.UTCOffset:   VV.ascii_string('+02:00'),
                               RID.Device.Timezone:    VV.ascii_string('Europe/Paris'),
                           },
                           ignore_extra=True))

        # 3. A 2nd WRITE (CoAP PUT) operation on the Device Object (ID:3)
        #    Instance is performed in using the Initial values which have been
        #    preserved (pre-conditions).
        self.test_write('/%d/0' % OID.Device,
                        b''.join(tlv.serialize() for tlv in values_to_restore),
                        coap.ContentFormat.APPLICATION_LWM2M_TLV)

        # 4. The Server READs the result of the previous WRITE operation by
        #    querying the Server Object (ID:3) Instance.

        def filter_out_dynamic_resources(tlv):
            return b''.join(x.serialize() for x in TLV.parse(tlv) \
                    if x.identifier not in (RID.Device.CurrentTime,
                                            RID.Device.PowerSourceVoltage,
                                            RID.Device.PowerSourceCurrent))

        self.assertEqual(filter_out_dynamic_resources(prev_values),
                         filter_out_dynamic_resources(self.test_read('/%d/0' % OID.Device)))

        # A. The Server receives the correct status codes for the steps 1.
        #    (2.04), 2. (2.05), 3. (2.04), & 4 (2.05) of the test.
        # B. In test step 3., the received values are as expected (readable
        #    resources) and consistent with the 651-SetOfValues sample.
        # C. In test step 7, the received values are consistent with the values
        #    present in the initial Configuration C.3


class Test652_DeviceObject_QueryingTheFirmwareVersionFromTheClient(DataModel.Test):
    def runTest(self):
        # 1. READ (CoAP GET) operation is performed on Device Object
        #    Resource "Firmware Version"
        #
        # A. In test step 2, the Server received the requested information
        #    (Firmware Version ) in the data format preferred by the Client
        # B. In test step 2 the Server receives the success message (2.05 Content)
        self.test_read(ResPath.Device.FirmwareVersion, VV.ascii_string(), coap.ContentFormat.TEXT_PLAIN)


class Test680_CreateObjectInstance(DataModel.Test):
    def runTest(self):
        # 1. CREATE (CoAP POST) operation is performed on Device Object
        #
        # A. In test step 1, the Server receives the failure message
        #    (4.05 Method Not Allowed)
        req = Lwm2mCreate('/%d' % OID.Device)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_METHOD_NOT_ALLOWED),
                            self.serv.recv())


class Test685_DeleteObjectInstance(DataModel.Test):
    def runTest(self):
        # 1. DELETE (CoAP DELETE) operation is performed on the
        #    Device Object Instance (/3/0)
        #
        # A. In test step 1, the Server received the requested information
        #    (Firmware Version ) in the data format preferred by the Client
        # B. In test step 1 the Server receives the failure message (4.05 Method
        #    Not Allowed)
        #
        # NOTE: A. is clearly invalid.
        req = Lwm2mDelete('/%d/0' % OID.Device)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_METHOD_NOT_ALLOWED),
                            self.serv.recv())
