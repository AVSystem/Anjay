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

import unittest

from framework.lwm2m_test import *

from .dm.utils import DataModel, ValueValidator


class Test201_QueryingBasicInformationInPlainTextFormat(DataModel.Test):
    def runTest(self):
        # 1. Successive READ (CoAP GET) operations are performed on
        #    the targeted Resources of the Device Object (ID:3) Instance,
        #    with the CoAP Accept option set to Plain Text data format (0)
        #
        # A. In test step 1, Server has received all the expected "Success" messages
        #    (2.05 Content) along with the requested information with the expected
        #    values and according to the Plain Text data format:
        #    - Manufacturer Name (ID:0)
        #    - Model number (ID:1)
        #    - Serial number (ID:2)
        self.test_read(ResPath.Device.Manufacturer, ValueValidator.ascii_string(), coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.Device.ModelNumber,  ValueValidator.ascii_string(), coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.Device.SerialNumber, ValueValidator.ascii_string(), coap.ContentFormat.TEXT_PLAIN)


class Test203_QueryingBasicInformationInTLVFormat(DataModel.Test):
    def runTest(self):
        # 1. READ (CoAP GET) operation on Device Object Instance
        #    (/3/0), with the CoAP Accept option set to TLV data format (11542)
        # 2. Bits 7-6=00= Object Instance in which case the Value contains
        #    one or more Resource TLVs
        # 3. Bits 7-6=11= Resource with Value
        #
        # A. In test step 1. Server has received the "Success" message (2.05
        #    Content), along with the requested information in the TLV data format
        #    (11542) and containing the expected values concerning:
        #    - Manufacturer Name (ID:0)
        #    - Model number (ID:1)
        #    - Serial number (ID:2)
        #    - Firmware Version (ID:3)
        #    - Error Code (ID:11)
        #    - Supported Binding and Modes (ID:16) ("U")
        string = ValueValidator.ascii_string()
        integer_array = ValueValidator.multiple_resource(ValueValidator.from_raw_int())

        self.test_read('/%d/0' % OID.Device,
                       ValueValidator.tlv_instance(
                           resource_validators={
                               RID.Device.Manufacturer:             string,
                               RID.Device.ModelNumber:              string,
                               RID.Device.SerialNumber:             string,
                               RID.Device.FirmwareVersion:          string,
                               RID.Device.ErrorCode:                integer_array,
                               RID.Device.SupportedBindingAndModes: string,
                           },
                           ignore_extra=True))


class Test204_QueryingBasicInformationInJSONFormat(DataModel.Test):
    def runTest(self):
        # 1. READ (CoAP GET) operation on the Device Object Instance
        #    (/3/0), with the CoAP Accept option set to JSON data format (11543)
        #
        # A. In test step 1. Server has received the success message (2.05
        #    Content) along with the requested information in JSON data format
        #    (11543) and containing the expected values concerning:
        #    - Manufacturer Name (ID:0)
        #    - Model number (ID:1)
        #    - Serial number (ID:2)
        #    - Firmware Version (ID:3)
        #    - Error Code (ID:11)
        #    - Supported Binding and Modes (ID:16) ("U")
        #
        # TODO: check JSON contents, not only structure
        self.test_read('/%d/0' % OID.Device,
                       ValueValidator.json(),
                       coap.ContentFormat.APPLICATION_LWM2M_JSON)


class Test205_SettingBasicInformationInPlainTextFormat(DataModel.Test):
    def runTest(self):
        # This test has to set the following resources with specific values:
        # - Default minimum period (ID:2) : 0101 sec
        # - Default maximum period (ID:3) : 1010 sec
        # - Disable timeout (ID:5) : 2000 sec
        #
        # 1. Successive WRITE (CoAP PUT) operations are performed on the
        #    Resources of the Instance 0 of the Server Object in using the
        #    predefined values above. The Plain Text data format (0) is used
        # 2. The Server verifies (READs/CoAP GET) the result of the WRITE
        #    operations by querying the Instance 0 of the Server Object in using
        #    the TLV data format.
        # 3. The steps 1., 2. of the test are re-played with the initial values
        #    of the Configuration 3
        #
        # A. In test step 1. the Server receives a "Sucesss" message (2.04
        #    Changed) for each WRITE operation
        self.test_write_validated(ResPath.Server[0].DefaultMinPeriod, '101')
        self.test_write_validated(ResPath.Server[0].DefaultMaxPeriod, '1010')
        self.test_write_validated(ResPath.Server[0].DisableTimeout, '2000')


class Test215_SettingBasicInformationInTLVFormat(DataModel.Test):
    def runTest(self):
        # Precondition:
        # The current values of the Server Object (ID:1) Instance 0, are saved
        # on the Server
        rids_to_restore = (RID.Server.Lifetime,
                           RID.Server.DefaultMinPeriod,
                           RID.Server.DefaultMaxPeriod,
                           RID.Server.DisableTimeout,
                           RID.Server.NotificationStoring,
                           RID.Server.Binding,
                           )
        prev_values = self.test_read('/%d/0' % OID.Server)
        prev_values_tlv = TLV.parse(prev_values)
        values_to_restore = [x for x in prev_values_tlv if x.identifier in rids_to_restore]

        tlv = TLV.make_instance(0, [
            TLV.make_resource(RID.Server.DefaultMinPeriod, 101),
            TLV.make_resource(RID.Server.DefaultMaxPeriod, 1010),
            TLV.make_resource(RID.Server.DisableTimeout, 2000),
            TLV.make_resource(RID.Server.NotificationStoring, 1),
            TLV.make_resource(RID.Server.Binding, 'UQ')
            ])

        # 1. A 1st WRITE (CoAP POST) operation on the Server Object (ID:1)
        #    Instance 0 is performed in using the set of values contains in the
        #    215-SetOfValues sample below.The data format TLV is used. (11542)
        # 2. The Server READs the result of the WRITE operation by querying the
        #    Server Object (ID:1) Instance 0.
        self.test_write('/%d/0' % OID.Server, tlv.serialize(),
                        coap.ContentFormat.APPLICATION_LWM2M_TLV,
                        update=True)

        # NOTE: changing server parameters triggers Update
        self.assertDemoUpdatesRegistration(binding='UQ')

        self.test_read('/%d/0' % OID.Server,
                       ValueValidator.tlv_instance(
                           resource_validators={
                               RID.Server.DefaultMinPeriod:    ValueValidator.from_raw_int(101),
                               RID.Server.DefaultMaxPeriod:    ValueValidator.from_raw_int(1010),
                               RID.Server.DisableTimeout:      ValueValidator.from_raw_int(2000),
                               RID.Server.NotificationStoring: ValueValidator.from_raw_int(1),
                               RID.Server.Binding:             ValueValidator.ascii_string('UQ'),
                           },
                           ignore_extra=True))

        # 3. A 2nd WRITE (CoAP PUT) operation on the Server Object (ID:1)
        #    Instance 0 is performed in using the Initial values preserved on the
        #    Server in conformance to the test pre-conditions.
        # 4. The Server READs the result of the previous WRITE operation by
        #    querying the Server Object (ID:1) Instance 0.
        self.test_write('/%d/0' % OID.Server,
                        b''.join(tlv.serialize() for tlv in values_to_restore),
                        coap.ContentFormat.APPLICATION_LWM2M_TLV)

        # NOTE: changing server parameters triggers Update
        self.assertDemoUpdatesRegistration(binding='U')

        self.assertEqual(prev_values,
                         self.test_read('/%d/0' % OID.Server))


@unittest.skip("JSON parsing is not implemented")
class Test220_SettingBasicInformationInJSONFormat(DataModel.Test):
    def runTest(self):
        # 1. A 1st WRITE (CoAP POST) operation on the Server Object (ID:1)
        #    Instance 0 is performed in using the set of values contains in the 220-
        #    SetOfValues sample below.The data format JSON is used. (11543)
        # 2. The Server READs the result of the WRITE operation by querying the
        #    the previously targeted Resources of the Server Object (ID:1) Instance 0.
        # 3. A 2nd WRITE (CoAP PUT) operation on the Server Object (ID:1)
        #    Instance 0 is performed in using the Initial values which have been
        #    preserved (pre-conditions).
        # 4. The Server READs the result of the previous WRITE operation by
        #    querying the Server Object (ID:1) Instance 0.
        # A. The Server receives the correct status codes for the steps 1. (2.04),
        #    2.(2.05), 3. (2.04) and 4. (2.05) of the test.
        # B. In test step 2. , the received values are consistent with the 220-
        #    SetOfValues sample
        # C. In test step 4, the received values are consistent with the values
        #    present in the initial Configuration 3
        pass


class Test241_ExecutableResourceRebootingTheDevice(DataModel.Test):
    def _get_valgrind_args(self):
        # Reboot cannot be performed when demo is run under valgrind
        return []

    def runTest(self):
        # 1. Server performs Execute (CoAP POST) operation on the
        #    Reboot Resource of Device Object (ID:3) Instance
        # A. In test step 1, the Server receives the success message (2.04
        #    Changed) related to the EXECUTE operation on the Client
        req = Lwm2mExecute(ResPath.Device.Reboot)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        # 2. Client registers again with the Server as in the test
        #    LightweightM2M-1.0-int-101
        # B. In test step2., Device reboots and the Client registers
        #    successfully with the Server again (see LightweightM2M-1.0-
        #    int-101)
        self.serv.reset()
        self.assertDemoRegisters(timeout_s=3)


class Test260_DiscoverCommand(DataModel.Test):
    def runTest(self):
        # 1. The initial DISCOVER (CoAP GET Accept:40) operation is
        #    performed on Objet Device ID:3
        #
        # A. In test step 1, the Success Message ("2.05" Content) is received by
        # the Server along with the payload related to the DISCOVER request
        # and containing :
        # </3>,</3/0/1>,</3/0/2>,</3/0/3>,</3/0/4>,</3/0/6>;dim=2,
        # </3/0/7>;dim=2, </3/0/8>;dim=2, </3/0/9>,</3/0/11>,</3/0/16>
        #
        # NOTE: According to
        # https://github.com/OpenMobileAlliance/OMA_LwM2M_for_Developers/issues/135
        # Discover on Object level should not return Resource attributes.
        # Even if it did, /3/0/11 is a Multiple Resource, so it should have
        # ;dim= attribute as well.
        # TODO: this needs to be clarified.
        link_list = self.test_discover('/%d' % (OID.Device,)).decode()
        links = link_list.split(',')
        self.assertIn('</%d>' % (OID.Device,), links)
        self.assertIn('<%s>' % (ResPath.Device.ModelNumber,), links)
        self.assertIn('<%s>' % (ResPath.Device.SerialNumber,), links)
        self.assertIn('<%s>' % (ResPath.Device.FirmwareVersion,), links)
        self.assertIn('<%s>' % (ResPath.Device.Reboot,), links)
        self.assertIn('<%s>' % (ResPath.Device.AvailablePowerSources,), links)
        self.assertIn('<%s>' % (ResPath.Device.PowerSourceVoltage,), links)
        self.assertIn('<%s>' % (ResPath.Device.PowerSourceCurrent,), links)
        self.assertIn('<%s>' % (ResPath.Device.BatteryLevel,), links)
        self.assertIn('<%s>' % (ResPath.Device.ErrorCode,), links)
        self.assertIn('<%s>' % (ResPath.Device.SupportedBindingAndModes,), links)

        # 2. A WRITE-ATTRIBUTES operation set the pmin=10 & pmax=200
        #    <NOTIFICATION> Attributes at the Object Instance level
        #
        # B. In test step 2, a Success message is received by the Server ("2.04"
        #    Changed) related to the WRITE-ATTRIBUTES operation
        self.test_write_attributes('/%d/0' % (OID.Device,), pmin=10, pmax=200)

        # 3. Same DISCOVER command as in test step 1., is performed
        #
        # C. In test step 3, the Success message ("2.05" Content) is received by
        #    the Server along with the payload related to the DISCOVER request
        #    and containing :
        #    < /3>,</3/0>;pmin=10;pmax=200,</3/0/1>, </3/0/2>, </3/0/3>,
        #    </3/0/4>, </3/0/6>;dim=2, </3/0/7>;dim=2, </3/0/8>;dim=2,
        #    </3/0/9>,</3/0/11>,</3/0/16>
        #
        # TODO: WTF? this should not show attributes as well
        link_list = self.test_discover('/%d' % (OID.Device,)).decode()
        links = link_list.split(',')
        self.assertIn('</%d>' % (OID.Device,), links)
        self.assertIn('<%s>' % (ResPath.Device.ModelNumber,), links)
        self.assertIn('<%s>' % (ResPath.Device.SerialNumber,), links)
        self.assertIn('<%s>' % (ResPath.Device.FirmwareVersion,), links)
        self.assertIn('<%s>' % (ResPath.Device.Reboot,), links)
        self.assertIn('<%s>' % (ResPath.Device.AvailablePowerSources,), links)
        self.assertIn('<%s>' % (ResPath.Device.PowerSourceVoltage,), links)
        self.assertIn('<%s>' % (ResPath.Device.PowerSourceCurrent,), links)
        self.assertIn('<%s>' % (ResPath.Device.BatteryLevel,), links)
        self.assertIn('<%s>' % (ResPath.Device.ErrorCode,), links)
        self.assertIn('<%s>' % (ResPath.Device.SupportedBindingAndModes,), links)

        # 4. The WRITE-ATTRIBUTES operation set the lt=1, gt=6 et st=1
        #    <NOTIFICATION> Attributes of the Resource ID:7 (Power
        #    Source Voltage)
        #
        # D. In test step 4, a "Success" message is received by the Server
        # ("2.04" Changed) related to the WRITE-ATTRIBUTES operation
        self.test_write_attributes(ResPath.Device.PowerSourceVoltage, lt=1, gt=6, st=1)

        # 5. Same DISCOVER command as in test step 1. is performed
        #
        # E. In test step 5, the Success message ("2.05" Content) is received
        #    by the Server along with the same payload received in step 3,
        #    except for the Resource 7 :
        #    < /3>,</3/0>;pmin=10;pmax=200,</3/0/1>, </3/0/2>, </3/0/3>,
        #    </3/0/4>, </3/0/6>;dim=2, </3/0/7>;dim=2, ;lt=1;
        #
        # TODO: WTF? this should not show attributes as well
        link_list = self.test_discover('/%d' % (OID.Device,)).decode()
        links = link_list.split(',')
        self.assertIn('</%d>' % (OID.Device,), links)
        self.assertIn('<%s>' % (ResPath.Device.ModelNumber,), links)
        self.assertIn('<%s>' % (ResPath.Device.SerialNumber,), links)
        self.assertIn('<%s>' % (ResPath.Device.FirmwareVersion,), links)
        self.assertIn('<%s>' % (ResPath.Device.Reboot,), links)
        self.assertIn('<%s>' % (ResPath.Device.AvailablePowerSources,), links)
        self.assertIn('<%s>' % (ResPath.Device.PowerSourceVoltage,), links)
        self.assertIn('<%s>' % (ResPath.Device.PowerSourceCurrent,), links)
        self.assertIn('<%s>' % (ResPath.Device.BatteryLevel,), links)
        self.assertIn('<%s>' % (ResPath.Device.ErrorCode,), links)
        self.assertIn('<%s>' % (ResPath.Device.SupportedBindingAndModes,), links)

        # 6. DISCOVER operation is performed on Resource ID:7 of the
        #    Object Device Instance
        #
        # F. In test step 6, the Success message ("2.05" Content) is received by
        #    the Server along with the payload related to the DISCOVER request
        #    and just containing :
        #    </3/0/7>;pmin=10;pmax=200;dim=2 ;lt=1;gt=6 ;st=1
        link_list = self.test_discover(ResPath.Device.PowerSourceVoltage)
        self.assertNotIn(b',', link_list)
        attrs = link_list.split(b';')
        self.assertIn(b'pmin=10', attrs)
        self.assertIn(b'pmax=200', attrs)
        self.assertIn(b'lt=1', attrs)
        self.assertIn(b'gt=6', attrs)
        self.assertIn(b'st=1', attrs)
        self.assertTrue(any(attr.startswith(b'dim=') for attr in attrs))


