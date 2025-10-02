# -*- coding: utf-8 -*-
#
# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.
from framework_tools.utils.lwm2m_test import *
from . import register
from . import access_control
from .access_control import AccessMask
from .bootstrap_client import BootstrapTest
from .send import Send
from .notifications import ConfirmableNotificationStatus
from framework_tools.lwm2m.tlv import TLVType
from framework_tools.utils.test_utils import *
import json


class Gateway:
    class Base:
        def gatewayDisabled(self):
            self.skipIfFeatureStatus('ANJAY_WITH_LWM2M_GATEWAY = OFF', 'LwM2M Gateway disabled')

        def setUp(self, extra_cmdline_args=[], *args, **kwargs):
            self.gatewayDisabled()

            extra_cmdline_args += ["-g"]
            super().setUp(extra_cmdline_args=extra_cmdline_args, *args, **kwargs)

        def assertInstances(self, expected_instances):
            res = self.read_object(self.serv, OID.Lwm2mGateway)
            tlv = TLV.parse(res.content)
            self.assertEqual(len(tlv), len(expected_instances))
            for instance in tlv:
                self.assertEqual(instance.tlv_type, TLVType.INSTANCE)
                expected_instances.remove(instance.identifier)
            self.assertEqual(len(expected_instances), 0)

        def extractPrefixes(self, server):
            res = self.read_object(
                server, OID.Lwm2mGateway, accept=coap.ContentFormat.APPLICATION_LWM2M_TLV)
            self.assertEqual(coap.ContentFormat.APPLICATION_LWM2M_TLV,
                             res.get_content_format())
            tlv = TLV.parse(res.content)
            prefixes = []
            for instance in tlv:
                resources = instance.value
                for resource in resources:
                    if resource.identifier == 1:
                        prefixes.append(resources[1].value.decode())

            self.assertEqual(len(prefixes), 2)
            self.assertNotEqual(prefixes[0], prefixes[1])
            return prefixes

    class BaseWithRegister(Base, register.RegisterTest):
        def setUp(self, *args, **kwargs):
            super().setUp(*args, **kwargs)
            self.extra_objects.append(Lwm2mResourcePathHelper.from_rid_object(
                RID.Lwm2mGateway, oid=OID.Lwm2mGateway, multi_instance=True, version='2.0'))

    class ObservationStatus(BaseWithRegister):
        OBSER_EXISTS_WO_ATTR = (True, 0, -1)
        OBSER_NOT_EXISTS = (False, 0, -1)

        def setUp(self, *args, **kwargs):
            version = '1.0'
            if 'minimum_version' in kwargs and 'maximum_version' in kwargs:
                version = '1.2'
            super().setUp(*args, **kwargs)

            self.assertDemoRegisters(version=version)
            self.prefixes = self.extractPrefixes(self.serv)
            self.path_gw1_res1 = "/%s/%d/%d/%d" % (
                self.prefixes[0], OID.Temperature, 0, RID.Temperature.SensorValue)
            self.path_gw1_res2 = "/%s/%d/%d/%d" % (
                self.prefixes[0], OID.Temperature, 0, RID.Temperature.ApplicationType)
            self.path_gw1_res3 = "/%s/%d/%d/%d" % (
                self.prefixes[0], OID.Temperature, 1, RID.Temperature.SensorValue)
            self.path_gw1_res4 = "/%s/%d/%d/%d" % (
                self.prefixes[0], OID.PushButton, 0, RID.PushButton.DigitalInputState)
            self.path_gw1_inst1 = "/%s/%d/%d" % (
                self.prefixes[0], OID.Temperature, 0)
            self.path_gw1_obj1 = "/%s/%d" % (self.prefixes[0], OID.Temperature)
            self.path_gw2_res1 = "/%s/%d/%d/%d" % (
                self.prefixes[1], OID.Temperature, 0, RID.Temperature.SensorValue)
            self.path_gw2_res2 = "/%s/%d/%d/%d" % (
                self.prefixes[1], OID.Temperature, 0, RID.Temperature.MaxMeasuredValue)
            self.path_res1 = "/%d/%d/%d" % (OID.Temperature,
                                            0, RID.Temperature.SensorValue)
            self.path_res2 = "/%d/%d/%d" % (OID.Temperature,
                                            0, RID.Temperature.MaxMeasuredValue)

            # nothing is observed in the beginning
            self.assertTupleEqual(self.observation_status(self.path_res1),
                                  self.OBSER_NOT_EXISTS)
            self.assertTupleEqual(self.observation_status(self.path_res2),
                                  self.OBSER_NOT_EXISTS)
            self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res1),
                                  self.OBSER_NOT_EXISTS)
            self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res2),
                                  self.OBSER_NOT_EXISTS)
            self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res3),
                                  self.OBSER_NOT_EXISTS)
            self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res4),
                                  self.OBSER_NOT_EXISTS)
            self.assertTupleEqual(self.gw_observation_status(self.path_gw2_res1),
                                  self.OBSER_NOT_EXISTS)
            self.assertTupleEqual(self.gw_observation_status(self.path_gw2_res2),
                                  self.OBSER_NOT_EXISTS)

        def tearDown(self):
            # receive notifications that may have been generated in the meantime
            while True:
                try:
                    self.serv.recv(
                        timeout_s=0.5, filter=lambda pkt: isinstance(pkt, Lwm2mNotify))
                except socket.timeout:
                    break
            super().tearDown()

        def gw_observation_status(self, path):
            dev_id = 65535
            if path.startswith("/" + self.prefixes[0]):
                dev_id = 0
            elif path.startswith("/" + self.prefixes[1]):
                dev_id = 1
            pos = path.find('/', 1)
            path = path[pos:]
            (is_observed, min_period, max_eval_period) = self.communicate(
                'observation-status %d %s' % (dev_id, path),
                match_regex=('is_observed == (.*), '
                             'min_period == (.*), '
                             'max_eval_period == (.*)\n')).groups()
            self.assertIn(is_observed, ('true', 'false'))
            return (is_observed == 'true', int(min_period), int(max_eval_period))

        def observation_status(self, path):
            (is_observed, min_period, max_eval_period) = self.communicate(
                'observation-status %s' % path,
                match_regex=('is_observed == (.*), '
                             'min_period == (.*), '
                             'max_eval_period == (.*)\n')).groups()
            self.assertIn(is_observed, ('true', 'false'))
            return (is_observed == 'true', int(min_period), int(max_eval_period))

    class BaseWithBootstrap(Base, BootstrapTest.TestMixin):
        def bootstrapDisabled(self):
            self.skipIfFeatureStatus('ANJAY_WITH_BOOTSTRAP = OFF', 'Bootstrap disabled')

        def setUp(self, extra_cmdline_args=[], *args, **kwargs):
            self.bootstrapDisabled()

            # extra_cmdline_args += ["-b"]
            super().setUp(extra_cmdline_args, servers=1, num_servers_passed=0, bootstrap_server=True, *args, *kwargs)


class RegisterWithGateway(Gateway.BaseWithRegister):
    pass


class AddAndRemoveEndDevices(Gateway.BaseWithRegister):
    def runTest(self):
        self.assertDemoRegisters(self.serv)

        # demo automatically starts with 2 End Devices
        self.assertInstances(set([0, 1]))

        # adding same device twice is forbidden
        self.communicate('gw_register 0')
        self.assertInstances(set([0, 1]))

        self.communicate('gw_deregister 0')
        self.assertInstances(set([1]))

        self.communicate('gw_deregister 1')
        self.assertInstances(set([]))

        self.communicate('gw_register 0')
        self.assertInstances(set([0]))

        self.communicate('gw_register 1')
        self.assertInstances(set([0, 1]))


class GatewayPerformBasicOperationsOnGatewayObject(Gateway.BaseWithRegister):
    def runTest(self):
        self.assertDemoRegisters(self.serv)

        res = self.read_object(self.serv, OID.Lwm2mGateway)
        self.assertEqual(coap.ContentFormat.APPLICATION_LWM2M_TLV,
                         res.get_content_format())

        res = self.read_instance(self.serv, OID.Lwm2mGateway, 0)
        self.assertEqual(coap.ContentFormat.APPLICATION_LWM2M_TLV,
                         res.get_content_format())

        res = self.read_resource(self.serv, OID.Lwm2mGateway, 0, 0)
        self.assertEqual(coap.ContentFormat.TEXT_PLAIN,
                         res.get_content_format())
        self.assertEqual(res.content.decode('utf-8'), "urn:dev:001234")
        res = self.read_resource(self.serv, OID.Lwm2mGateway, 1, 0)
        self.assertEqual(coap.ContentFormat.TEXT_PLAIN,
                         res.get_content_format())
        self.assertEqual(res.content.decode('utf-8'), "urn:dev:556789")
        res = self.read_resource(self.serv, OID.Lwm2mGateway, 0, 1)
        self.assertEqual(coap.ContentFormat.TEXT_PLAIN,
                         res.get_content_format())
        self.assertEqual(res.content.decode('utf-8'), "dev0")
        res = self.read_resource(self.serv, OID.Lwm2mGateway, 1, 1)
        self.assertEqual(coap.ContentFormat.TEXT_PLAIN,
                         res.get_content_format())
        self.assertEqual(res.content.decode('utf-8'), "dev1")
        res = self.read_resource(self.serv, OID.Lwm2mGateway, 0, 3)
        self.assertEqual(coap.ContentFormat.TEXT_PLAIN,
                         res.get_content_format())
        self.assertEqual(res.content.decode('utf-8'),
                         "</19/0>,</3303>;ver=1.1,</3303/0>,</3347/0>")
        res = self.read_resource(self.serv, OID.Lwm2mGateway, 1, 3)
        self.assertEqual(coap.ContentFormat.TEXT_PLAIN,
                         res.get_content_format())
        self.assertEqual(res.content.decode('utf-8'),
                         "</19/0>,</3303>;ver=1.1,</3303/0>,</3347/0>")

        res = self.create_instance(self.serv, OID.Lwm2mGateway,
                                   expect_error_code=coap.Code.RES_METHOD_NOT_ALLOWED)


class GatewayPerformBasicOperationsOnEndDevicesObject(Gateway.BaseWithRegister):
    def runTest(self):
        self.assertDemoRegisters(self.serv)

        prefixes = self.extractPrefixes(self.serv)

        # operate on buttons differently to verify if the Reads work properly

        self.communicate("gw_press_button 0")
        self.communicate("gw_release_button 0")
        self.communicate("gw_press_button 1")
        self.communicate("gw_release_button 1")
        self.communicate("gw_press_button 1")

        # build expected contents
        button1_res1_val = 0
        button1_res2_val = 1
        button1_res3_val = "Button 0"
        button2_res1_val = 1
        button2_res2_val = 2
        button2_res3_val = "Button 1"

        button1_res1 = TLV.make_resource(
            RID.PushButton.DigitalInputState, button1_res1_val)
        button1_res2 = TLV.make_resource(
            RID.PushButton.DigitalInputCounter, button1_res2_val)
        button1_res3 = TLV.make_resource(
            RID.PushButton.ApplicationType, button1_res3_val)
        button1_instance = button1_res1.serialize() \
            + button1_res2.serialize() \
            + button1_res3.serialize()
        button1_object = TLV.make_instance(0, [button1_res1,
                                               button1_res2,
                                               button1_res3]).serialize()

        button2_res1 = TLV.make_resource(
            RID.PushButton.DigitalInputState, button2_res1_val)
        button2_res2 = TLV.make_resource(
            RID.PushButton.DigitalInputCounter, button2_res2_val)
        button2_res3 = TLV.make_resource(
            RID.PushButton.ApplicationType, button2_res3_val)
        button2_instance = button2_res1.serialize() \
            + button2_res2.serialize() \
            + button2_res3.serialize()
        button2_object = TLV.make_instance(0, [button2_res1,
                                               button2_res2,
                                               button2_res3]).serialize()

        # Read on Object
        res = self.read_path(self.serv, '/%s/3347' % prefixes[0])
        self.assertEqual(coap.ContentFormat.APPLICATION_LWM2M_TLV,
                         res.get_content_format())
        self.assertEqual(res.content, button1_object)

        res = self.read_path(self.serv, '/%s/3347' % prefixes[1])
        self.assertEqual(coap.ContentFormat.APPLICATION_LWM2M_TLV,
                         res.get_content_format())
        self.assertEqual(res.content, button2_object)

        # Read on Instance
        res = self.read_path(self.serv, '/%s/3347/0' % prefixes[0])
        self.assertEqual(coap.ContentFormat.APPLICATION_LWM2M_TLV,
                         res.get_content_format())
        self.assertEqual(res.content, button1_instance)

        res = self.read_path(self.serv, '/%s/3347/0' % prefixes[1])
        self.assertEqual(coap.ContentFormat.APPLICATION_LWM2M_TLV,
                         res.get_content_format())
        self.assertEqual(res.content, button2_instance)

        # Read on Resource
        res = self.read_path(self.serv, '/%s/3347/0/5500' % prefixes[0])
        self.assertEqual(coap.ContentFormat.TEXT_PLAIN,
                         res.get_content_format())
        self.assertEqual(res.content.decode(), str(button1_res1_val))
        res = self.read_path(self.serv, '/%s/3347/0/5501' % prefixes[0])
        self.assertEqual(coap.ContentFormat.TEXT_PLAIN,
                         res.get_content_format())
        self.assertEqual(res.content.decode(), str(button1_res2_val))
        res = self.read_path(self.serv, '/%s/3347/0/5750' % prefixes[0])
        self.assertEqual(coap.ContentFormat.TEXT_PLAIN,
                         res.get_content_format())
        self.assertEqual(res.content.decode(), button1_res3_val)

        res = self.read_path(self.serv, '/%s/3347/0/5500' % prefixes[1])
        self.assertEqual(coap.ContentFormat.TEXT_PLAIN,
                         res.get_content_format())
        self.assertEqual(res.content.decode(), str(button2_res1_val))
        res = self.read_path(self.serv, '/%s/3347/0/5501' % prefixes[1])
        self.assertEqual(coap.ContentFormat.TEXT_PLAIN,
                         res.get_content_format())
        self.assertEqual(res.content.decode(), str(button2_res2_val))
        res = self.read_path(self.serv, '/%s/3347/0/5750' % prefixes[1])
        self.assertEqual(coap.ContentFormat.TEXT_PLAIN,
                         res.get_content_format())
        self.assertEqual(res.content.decode(), button2_res3_val)

        # Write on Resource
        req = Lwm2mWrite('/%s/3347/0/5750' % (prefixes[0]), "df",
                         format=coap.ContentFormat.TEXT_PLAIN)
        expected_res = self._make_expected_res(
            req, success_res_cls=Lwm2mChanged, expect_error_code=None)
        res = self._perform_action(
            self.serv, request=req, expected_response=expected_res)

        req = Lwm2mWrite('/%s/3347/0/5750' % (prefixes[1]), "gh",
                         format=coap.ContentFormat.TEXT_PLAIN)
        expected_res = self._make_expected_res(
            req, success_res_cls=Lwm2mChanged, expect_error_code=None)
        res = self._perform_action(
            self.serv, request=req, expected_response=expected_res)

        res = self.read_path(self.serv, '/%s/3347/0/5750' % prefixes[0])
        self.assertEqual(coap.ContentFormat.TEXT_PLAIN,
                         res.get_content_format())
        self.assertEqual(res.content.decode(), "df")

        res = self.read_path(self.serv, '/%s/3347/0/5750' % prefixes[1])
        self.assertEqual(coap.ContentFormat.TEXT_PLAIN,
                         res.get_content_format())
        self.assertEqual(res.content.decode(), "gh")

        # Execute on Resource
        req = Lwm2mExecute('/%s/3303/0/5605' % (prefixes[0]))
        expected_res = self._make_expected_res(
            req, success_res_cls=Lwm2mChanged, expect_error_code=None)
        res = self._perform_action(
            self.serv, request=req, expected_response=expected_res)


class PerformReadOperationOnMultiInstanceResources(Gateway.BaseWithRegister):
    def runTest(self):
        self.assertDemoRegisters(self.serv)

        prefixes = self.extractPrefixes(self.serv)

        self.communicate(
            "gw-badc-write 0 0 0 device 0 value of multi instance resource")
        self.communicate(
            "gw-badc-write 1 0 0 device 1 value of multi instance resource")

        # Read resource
        res = self.read_path(self.serv, '/%s/19/0/0/0' % prefixes[0],
                             accept=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.assertEqual(
            coap.ContentFormat.APPLICATION_OCTET_STREAM, res.get_content_format())
        self.assertEqual(res.content.decode(),
                         'device 0 value of multi instance resource')

        res = self.read_path(self.serv, '/%s/19/0/0/0' % prefixes[1],
                             accept=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.assertEqual(
            coap.ContentFormat.APPLICATION_OCTET_STREAM, res.get_content_format())
        self.assertEqual(res.content.decode(),
                         'device 1 value of multi instance resource')


class PerformWriteOperationOnMultiInstanceResources(Gateway.BaseWithRegister):
    def runTest(self):
        self.assertDemoRegisters(self.serv)

        prefixes = self.extractPrefixes(self.serv)

        # Write and Read on Resource Instance
        dev_0_request = [
            {
                SenmlLabel.BASE_NAME: f'/{prefixes[0]}/19/0/0/0',
                SenmlLabel.OPAQUE: b'test value 1'
            }
        ]

        dev_1_request = [
            {
                SenmlLabel.BASE_NAME: f'/{prefixes[1]}/19/0/0/0',
                SenmlLabel.OPAQUE: b'different test value'
            }
        ]

        req = Lwm2mWrite('/%s/19/0' % (prefixes[0]), CBOR.serialize(dev_0_request),
                         format=coap.ContentFormat.APPLICATION_LWM2M_SENML_CBOR)
        expected_res = self._make_expected_res(
            req, success_res_cls=Lwm2mChanged, expect_error_code=None)
        res = self._perform_action(
            self.serv, request=req, expected_response=expected_res)

        req = Lwm2mWrite('/%s/19/0' % (prefixes[1]), CBOR.serialize(dev_1_request),
                         format=coap.ContentFormat.APPLICATION_LWM2M_SENML_CBOR)
        expected_res = self._make_expected_res(
            req, success_res_cls=Lwm2mChanged, expect_error_code=None)
        res = self._perform_action(
            self.serv, request=req, expected_response=expected_res)

        # Check the values
        res = self.read_path(self.serv, '/%s/19/0/0/0' % prefixes[0],
                             accept=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.assertEqual(
            coap.ContentFormat.APPLICATION_OCTET_STREAM, res.get_content_format())
        self.assertEqual(res.content.decode(), 'test value 1')

        res = self.read_path(self.serv, '/%s/19/0/0/0' % prefixes[1],
                             accept=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.assertEqual(
            coap.ContentFormat.APPLICATION_OCTET_STREAM, res.get_content_format())
        self.assertEqual(res.content.decode(), 'different test value')


class WriteMultipleResourcesOnEndDevice(Gateway.BaseWithRegister):
    def runTest(self):
        self.assertDemoRegisters(self.serv)

        prefixes = self.extractPrefixes(self.serv)

        # Write new resource instance
        dev_0_request = [
            {
                SenmlLabel.BASE_NAME: f'/{prefixes[0]}/19/0/',
                SenmlLabel.NAME: '0/0',
                SenmlLabel.OPAQUE: b'resource_instance_1_value'
            },
            {
                SenmlLabel.NAME: '0/1',
                SenmlLabel.OPAQUE: b'resource_instance_2_value'
            }
        ]

        self.write_path(self.serv, path="/%s/19/0" % prefixes[0], content=CBOR.serialize(dev_0_request),
                        format=coap.ContentFormat.APPLICATION_LWM2M_SENML_CBOR)

        # Check resource instance dim - should be equal to 2
        ret = self.discover_path(
            self.serv, path="/%s/19/0" % prefixes[0], depth=1)
        self.assertEqual(ret.content, b'</19/0>,</19/0/0>;dim=2')

        # Check the values
        res = self.read_path(self.serv, '/%s/19/0/0/0' % prefixes[0],
                             accept=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.assertEqual(
            coap.ContentFormat.APPLICATION_OCTET_STREAM, res.get_content_format())
        self.assertEqual(res.content.decode(), 'resource_instance_1_value')

        res = self.read_path(self.serv, '/%s/19/0/0/1' % prefixes[0],
                             accept=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.assertEqual(
            coap.ContentFormat.APPLICATION_OCTET_STREAM, res.get_content_format())
        self.assertEqual(res.content.decode(), 'resource_instance_2_value')


class AccessControlCreateTest(Gateway.Base, access_control.AccessControl.Test):
    def setUp(self):
        super().setUp(servers=2, oid=OID.BinaryAppDataContainer,
                      extra_cmdline_args=['--access-entry', '/%s/0,0,%s' % (OID.Lwm2mGateway, AccessMask.READ),
                                          '--access-entry', '/%s/1,0,%s' % (OID.Lwm2mGateway, AccessMask.READ)])

    def runTest(self):
        prefixes = self.extractPrefixes(self.servers[0])

        # SSID 2 has Create flag on OID.BinaryAppDataContainer object
        self.create(server=self.servers[1], path="/19")
        # Now do the same with SSID 1, it should fail
        self.create(server=self.servers[0], path="/19",
                    expect_error_code=coap.Code.RES_UNAUTHORIZED)
        # Create instance for endpoint objects, Access Control has no impact on them
        self.create(server=self.servers[0], path="/%s/19" % prefixes[0])
        self.create(server=self.servers[1], path="/%s/19" % prefixes[0])


class AccessControlDeleteTest(Gateway.Base, access_control.AccessControl.Test):
    def setUp(self):
        super().setUp(servers=2, oid=OID.BinaryAppDataContainer,
                      extra_cmdline_args=['--access-entry', '/%s/0,0,%s' % (OID.Lwm2mGateway, AccessMask.READ),
                                          '--access-entry', '/%s/1,0,%s' % (OID.Lwm2mGateway, AccessMask.READ)])

    def runTest(self):
        prefixes = self.extractPrefixes(server=self.servers[0])

        self.create(server=self.servers[1], path="/19")

        ac_iid = self.find_access_control_instance(
            server=self.servers[1], oid=OID.BinaryAppDataContainer, iid=0)

        # Delete instances of end device objects
        self.delete(server=self.servers[0], path="/%s/19/0" % prefixes[0])
        self.create(server=self.servers[0], path="/%s/19" % prefixes[0])
        self.delete(server=self.servers[1], path="/%s/19/0" % prefixes[0])
        # Deleting instance of BinaryAppDataContainer object for end device should not impact Access Control object
        self.read_instance(
            server=self.servers[1], oid=OID.AccessControl, iid=ac_iid)

        self.delete_instance(server=self.servers[0], oid=OID.BinaryAppDataContainer, iid=0,
                             expect_error_code=coap.Code.RES_UNAUTHORIZED)
        self.delete_instance(
            server=self.servers[1], oid=OID.BinaryAppDataContainer, iid=0)

        # Instance is removed, so its corresponding Access Control Instance
        # should be removed automatically too. The answer shall be NOT_FOUND
        # according to the spec.
        self.read_instance(server=self.servers[1], oid=OID.AccessControl, iid=ac_iid,
                           expect_error_code=coap.Code.RES_NOT_FOUND)


class GatewayWriteAttributes(Gateway.BaseWithRegister):
    def setUp(self):
        # LwM2M 1.2 is set to see all the attributes in observe on Object Level
        super().setUp(minimum_version='1.2', maximum_version='1.2')

    def checkDiscoverValues(self):
        prefixes = self.extractPrefixes(self.serv)
        # Discover RIID level is forbidden, rest should work
        self.discover_path(self.serv, path="/%s/19/0/0/0" %
                           prefixes[0], expect_error_code=coap.Code.RES_METHOD_NOT_ALLOWED)
        res = self.discover_path(self.serv, path="/%s/19/0/0" % prefixes[0])
        self.assertEqual(res.content.decode(),
                         "</19/0/0>;dim=1;pmin=1;pmax=4;epmin=5,</19/0/0/0>;epmin=6")
        res = self.discover_path(self.serv, path="/%s/19/0" % prefixes[0])
        self.assertEqual(res.content.decode(),
                         "</19/0>;pmin=3;pmax=4,</19/0/0>;dim=1;pmin=1;epmin=5")
        res = self.discover_path(self.serv, path="/%s/19" % prefixes[0])
        self.assertEqual(res.content.decode(),
                         "</19>;pmin=4;pmax=5,</19/0>;pmin=3;pmax=4,"
                         "</19/0/0>;dim=1;pmin=1;epmin=5")

    def runTest(self):
        self.assertDemoRegisters(self.serv, version='1.2')

        prefixes = self.extractPrefixes(self.serv)

        # no attributes at the beginning
        res = self.discover_path(self.serv, path="/%s/19" % prefixes[0])
        self.assertEqual(res.content.decode(),
                         "</19>,</19/0>,</19/0/0>;dim=1")

        self.write_attributes_path(
            self.serv, path="/%s/19/0/0/0" % prefixes[0], query=['epmin=6'])
        self.write_attributes_path(
            self.serv, path="/%s/19/0/0" % prefixes[0], query=['epmin=5', 'pmin=1'])
        self.write_attributes_path(
            self.serv, path="/%s/19/0" % prefixes[0], query=['pmin=3', 'pmax=4'])
        self.write_attributes_path(
            self.serv, path="/%s/19" % prefixes[0], query=['pmin=4', 'pmax=5'])

        # check if attributes are aplied and discover properly
        self.checkDiscoverValues()

        # check if invalid values are not applied and don't mess with current state
        self.write_attributes_path(self.serv, path="/%s/19/0/0" % prefixes[0], query=['epmin=-10'],
                                   expect_error_code=coap.Code.RES_BAD_OPTION)
        self.write_attributes_path(self.serv, path="/%s/19/0" % prefixes[0], query=['pmin=-20'],
                                   expect_error_code=coap.Code.RES_BAD_OPTION)
        self.write_attributes_path(self.serv, path="/%s/19" % prefixes[0], query=['pmax=-1'],
                                   expect_error_code=coap.Code.RES_BAD_OPTION)
        self.checkDiscoverValues()

        # check for crosstalk - write different attributes to same Object in Anjay DM and End Device DM
        self.write_attributes_path(
            self.serv, path="/%s/3303/0/5700" % prefixes[0], query=['epmin=6'])
        self.write_attributes_path(
            self.serv, path="/%s/3303/0/5700" % prefixes[0], query=['pmax=5'])
        self.write_attributes_path(
            self.serv, path="/3303/0/5700", query=['gt=3'])
        self.write_attributes_path(
            self.serv, path="/3303/0/5700", query=['lt=1'])

        res = self.discover_path(
            self.serv, path="/%s/3303/0/5700" % prefixes[0])
        self.assertEqual(res.content.decode(),
                         "</3303/0/5700>;pmax=5;epmin=6")
        res = self.discover_path(self.serv, path="/3303/0/5700")
        self.assertEqual(res.content.decode(),
                         "</3303/0/5700>;gt=3;lt=1")


class GatewayWriteAttributesMultipleServers(Gateway.Base, test_suite.Lwm2mTest, test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(servers=2,
                      extra_cmdline_args=['--access-entry', '/%s/0,0,%s' % (OID.Lwm2mGateway, AccessMask.READ),
                                          '--access-entry', '/%s/1,0,%s' % (OID.Lwm2mGateway, AccessMask.READ)],
                      minimum_version='1.2', maximum_version='1.2')

    def runTest(self):
        self.coap_ping(self.servers[0])
        self.coap_ping(self.servers[1])
        prefixes = self.extractPrefixes(self.servers[0])

        res = self.discover_path(self.servers[0], path="/%s/19" % prefixes[0])
        self.assertEqual(res.content.decode(),
                         "</19>,</19/0>,</19/0/0>;dim=1")

        res = self.discover_path(self.servers[1], path="/%s/19" % prefixes[0])
        self.assertEqual(res.content.decode(),
                         "</19>,</19/0>,</19/0/0>;dim=1")

        # server 0 sets and properly reads its attributes
        self.write_attributes_path(
            self.servers[0], path="/%s/19/0/0" % prefixes[0], query=['pmin=1', 'pmax=5'])
        res = self.discover_path(
            self.servers[0], path="/%s/19/0/0" % prefixes[0], depth=0)
        self.assertEqual(res.content.decode(), "</19/0/0>;dim=1;pmin=1;pmax=5")
        self.write_attributes_path(
            self.servers[0], path="/%s/19/0" % prefixes[0], query=['pmin=3', 'pmax=4'])
        res = self.discover_path(
            self.servers[0], path="/%s/19/0" % prefixes[0], depth=0)
        self.assertEqual(res.content.decode(), "</19/0>;pmin=3;pmax=4")
        self.write_attributes_path(
            self.servers[0], path="/%s/19" % prefixes[0], query=['pmin=4', 'pmax=5'])
        res = self.discover_path(
            self.servers[0], path="/%s/19" % prefixes[0], depth=0)
        self.assertEqual(res.content.decode(), "</19>;pmin=4;pmax=5")

        # server 1 does not see attributes set by server 0
        res = self.discover_path(self.servers[1], path="/%s/19" % prefixes[0])
        self.assertEqual(res.content.decode(), "</19>,</19/0>,</19/0/0>;dim=1")

        # server 1 sets attributes and they don't overwrite attributes set by server 0
        self.write_attributes_path(
            self.servers[1], path="/%s/19/0/0" % prefixes[0], query=['epmin=4', 'pmin=2'])
        res = self.discover_path(
            self.servers[0], path="/%s/19/0/0" % prefixes[0], depth=0)
        self.assertEqual(res.content.decode(), "</19/0/0>;dim=1;pmin=1;pmax=5")
        self.write_attributes_path(
            self.servers[1], path="/%s/19/0" % prefixes[0], query=['pmin=1', 'pmax=6'])
        res = self.discover_path(
            self.servers[0], path="/%s/19/0" % prefixes[0], depth=0)
        self.assertEqual(res.content.decode(), "</19/0>;pmin=3;pmax=4")
        self.write_attributes_path(
            self.servers[1], path="/%s/19" % prefixes[0], query=['pmin=8', 'pmax=9'])
        res = self.discover_path(
            self.servers[0], path="/%s/19" % prefixes[0], depth=0)
        self.assertEqual(res.content.decode(), "</19>;pmin=4;pmax=5")


class SendFromDataModelTest(Gateway.Base, Send.Test):
    def setApplicationTypeRes(self, prefixes):
        self.write_path(self.serv, path='/%s/3303/0/5750' % (prefixes[0]),
                        content="Test1", format=coap.ContentFormat.TEXT_PLAIN)
        self.write_path(self.serv, path='/%s/3303/0/5750' % (prefixes[1]),
                        content="Test2", format=coap.ContentFormat.TEXT_PLAIN)
        self.write_path(self.serv, path='/%s/3347/0/5750' % (prefixes[0]),
                        content="Test3", format=coap.ContentFormat.TEXT_PLAIN)
        self.write_path(self.serv, path='/%s/3347/0/5750' % (prefixes[1]),
                        content="Test4", format=coap.ContentFormat.TEXT_PLAIN)

    def processSend(self):
        pkt = self.serv.recv()
        self.assertMsgEqual(Lwm2mSend(), pkt)
        self.serv.send(Lwm2mChanged.matching(pkt)())
        return CBOR.parse(pkt.content)

    def runTest(self):
        prefixes = self.extractPrefixes(self.serv)
        self.setApplicationTypeRes(prefixes)

        # SEND with gateway resources
        self.communicate(
            'send 1 %s %s' %
            (ResPath.Device.ModelNumber,
             ResPath.Device.Manufacturer))

        cbor = self.processSend()
        cbor.verify_values(test=self,
                           expected_value_map={
                               ResPath.Device.ModelNumber: 'demo-client',
                               ResPath.Device.Manufacturer: '0023C7'
                           })
        self.assertEqual(cbor[0].get(SenmlLabel.BASE_NAME), '/3/0')
        self.assertIsNotNone(cbor[0].get(SenmlLabel.BASE_TIME))
        self.assertIsNone(cbor[0].get(SenmlLabel.TIME))

        self.assertIsNone(cbor[1].get(SenmlLabel.BASE_NAME))
        self.assertIsNone(cbor[1].get(SenmlLabel.BASE_TIME))
        self.assertIsNone(cbor[1].get(SenmlLabel.TIME))

        # SEND with end device resources
        self.communicate(
            'send 1 0 %s 0 %s' %
            (ResPath.PushButton.ApplicationType,
             ResPath.PushButton.DigitalInputCounter))

        cbor = self.processSend()
        cbor.verify_values(test=self,
                           expected_value_map={
                               '/%s/3347/0/5750' % (prefixes[0]): 'Test3',
                               '/%s/3347/0/5501' % (prefixes[0]): 0
                           })
        self.assertEqual(cbor[0].get(SenmlLabel.BASE_NAME),
                         '/%s/3347/0' % (prefixes[0]))
        self.assertIsNotNone(cbor[0].get(SenmlLabel.BASE_TIME))
        self.assertIsNone(cbor[0].get(SenmlLabel.TIME))

        self.assertIsNone(cbor[1].get(SenmlLabel.BASE_NAME))
        self.assertIsNone(cbor[1].get(SenmlLabel.BASE_TIME))
        self.assertIsNone(cbor[1].get(SenmlLabel.TIME))

        # SEND with same resources and two end devices
        self.communicate(
            'send 1 0 %s 1 %s' %
            (ResPath.Temperature.ApplicationType,
             ResPath.Temperature.ApplicationType))

        cbor = self.processSend()
        cbor.verify_values(test=self,
                           expected_value_map={
                               '/%s/3303/0/5750' % (prefixes[0]): 'Test1',
                               '/%s/3303/0/5750' % (prefixes[1]): 'Test2'
                           })
        self.assertIsNone(cbor[0].get(SenmlLabel.BASE_NAME))
        self.assertIsNotNone(cbor[0].get(SenmlLabel.BASE_TIME))
        self.assertIsNone(cbor[0].get(SenmlLabel.TIME))

        self.assertIsNone(cbor[1].get(SenmlLabel.BASE_NAME))
        self.assertIsNotNone(cbor[1].get(SenmlLabel.BASE_TIME))
        self.assertIsNone(cbor[1].get(SenmlLabel.TIME))

        # SEND with different resources and two end devices
        self.communicate(
            'send 1 0 %s 1 %s' %
            (ResPath.PushButton.DigitalInputCounter,
             ResPath.PushButton.ApplicationType))

        cbor = self.processSend()
        cbor.verify_values(test=self,
                           expected_value_map={
                               '/%s/3347/0/5501' % (prefixes[0]): 0,
                               '/%s/3347/0/5750' % (prefixes[1]): 'Test4'
                           })
        self.assertIsNone(cbor[0].get(SenmlLabel.BASE_NAME))
        self.assertIsNotNone(cbor[0].get(SenmlLabel.BASE_TIME))
        self.assertIsNone(cbor[0].get(SenmlLabel.TIME))

        self.assertIsNone(cbor[1].get(SenmlLabel.BASE_NAME))
        self.assertIsNotNone(cbor[1].get(SenmlLabel.BASE_TIME))
        self.assertIsNone(cbor[1].get(SenmlLabel.TIME))

        # SEND with gateway and two end devices resources
        self.communicate(
            'send 1 0 %s %s 1 %s' %
            (ResPath.Temperature.ApplicationType,
             ResPath.Device.ModelNumber,
             ResPath.PushButton.ApplicationType))

        cbor = self.processSend()
        cbor.verify_values(test=self,
                           expected_value_map={
                               '/%s/3303/0/5750' % (prefixes[0]): 'Test1',
                               ResPath.Device.ModelNumber: 'demo-client',
                               '/%s/3347/0/5750' % (prefixes[1]): 'Test4'
                           })
        self.assertIsNone(cbor[0].get(SenmlLabel.BASE_NAME))
        self.assertIsNotNone(cbor[0].get(SenmlLabel.BASE_TIME))
        self.assertIsNone(cbor[0].get(SenmlLabel.TIME))
        self.assertIsNone(cbor[1].get(SenmlLabel.BASE_NAME))
        self.assertIsNotNone(cbor[1].get(SenmlLabel.BASE_TIME))
        self.assertIsNone(cbor[1].get(SenmlLabel.TIME))
        self.assertIsNone(cbor[2].get(SenmlLabel.BASE_NAME))
        self.assertIsNotNone(cbor[2].get(SenmlLabel.BASE_TIME))
        self.assertIsNone(cbor[2].get(SenmlLabel.TIME))


class WriteComposite(Gateway.BaseWithRegister):
    def runTest(self):
        request = [{
            'n': '/dev0/3303/0/5750',
            'vs': "aa"
        }
        ]

        self.assertDemoRegisters(self.serv)

        self.write_composite(self.serv, content=json.dumps(request),
                             format=coap.ContentFormat.APPLICATION_LWM2M_SENML_JSON,
                             expect_error_code=coap.Code.RES_METHOD_NOT_ALLOWED)


class ReadComposite(Gateway.BaseWithRegister):
    def runTest(self):
        self.assertDemoRegisters(self.serv)

        self.read_composite(self.serv, paths=['/dev0/3303/0/5750'],
                            expect_error_code=coap.Code.RES_METHOD_NOT_ALLOWED,
                            accept=coap.ContentFormat.APPLICATION_LWM2M_SENML_JSON)


class ObserveComposite(Gateway.BaseWithRegister):
    def runTest(self):
        self.assertDemoRegisters(self.serv)

        self.observe_composite(self.serv, paths=['/dev0/3303/0/5750'],
                               expect_error_code=coap.Code.RES_METHOD_NOT_ALLOWED)


class ObservationStatusTest(Gateway.ObservationStatus):
    def runTest(self):
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res1),
                              (False, 0, -1))
        notif1 = self.observe_path(self.serv, path=self.path_gw1_res1)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res1),
                              # We expect pmin == 0 because  ANJAY_DM_DEFAULT_PMIN_VALUE == 0
                              (True, 0, -1))
        self.write_attributes_path(
            self.serv, path=self.path_gw1_res1, query=['pmin=42'])
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res1),
                              (True, 42, -1))
        notif2 = self.observe_path(
            self.serv, path=self.path_gw1_obj1)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res1),
                              # Now 2 paths are observed:
                              # 1. /dev0/3303/0/5700, pmin = 42
                              # 2. /dev0/3303,        pmin = 0 (ANJAY_DM_DEFAULT_PMIN_VALUE)
                              # So we expect effective pmin = min{42, 0} = 0
                              (True, 0, -1))
        self.write_attributes_path(
            self.serv, path=self.path_gw1_obj1, query=['pmin=41'])
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res1),
                              # Attributes for the paths have changed:
                              # 1. /dev0/3303/0/5700, pmin = 42
                              # 2. /dev0/3303,        pmin = 41
                              # Now 41 is effective pmin
                              (True, 41, -1))
        self.write_attributes_path(
            self.serv, path=self.path_gw1_obj1, query=['pmin=43'])
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res1),
                              # Attributes for the paths have changed:
                              # 1. /dev0/3303/0/5700, pmin = 42
                              # 2. /dev0/3303,        pmin = 43
                              # Now 42 is effective pmin
                              (True, 42, -1))
        self.write_attributes_path(
            self.serv, path=self.path_gw1_obj1, query=['epmax=5'])
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res1),
                              (True, 42, 5))

        # cancel observations
        self.observe_path(self.serv, path=self.path_gw1_obj1,
                          token=notif2.token, observe=1)
        self.observe_path(self.serv, path=self.path_gw1_res1,
                          token=notif1.token, observe=1)
        self.assertTupleEqual(self.gw_observation_status(
            # NOTE: attribute values don't correspond to the actual values stored in the attribute storage
            self.path_gw1_res1), (False, 0, -1))


class ObservationStatusObserveOperationTest(Gateway.ObservationStatus):
    def runTest(self):
        # set observe on Anjay
        notif1 = self.observe_path(self.serv, path=self.path_res1)
        self.assertTupleEqual(self.observation_status(self.path_res1),
                              self.OBSER_EXISTS_WO_ATTR)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res1),
                              self.OBSER_NOT_EXISTS)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw2_res1),
                              self.OBSER_NOT_EXISTS)

        # Cancel observe
        self.observe_path(self.serv, path=self.path_res1,
                          observe=1, token=notif1.token)

        # nothing is observed
        self.assertTupleEqual(self.observation_status(self.path_res1),
                              self.OBSER_NOT_EXISTS)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res1),
                              self.OBSER_NOT_EXISTS)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw2_res1),
                              self.OBSER_NOT_EXISTS)

        notif1 = self.observe_path(self.serv, path=self.path_gw1_res1)
        self.assertTupleEqual(self.observation_status(self.path_res1),
                              self.OBSER_NOT_EXISTS)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res1),
                              self.OBSER_EXISTS_WO_ATTR)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw2_res1),
                              self.OBSER_NOT_EXISTS)

        # Cancel observe
        self.observe_path(self.serv, path=self.path_gw1_res1,
                          observe=1, token=notif1.token)

        # nothing is observed
        self.assertTupleEqual(self.observation_status(self.path_res1),
                              self.OBSER_NOT_EXISTS)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res1),
                              self.OBSER_NOT_EXISTS)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw2_res1),
                              self.OBSER_NOT_EXISTS)


class ObservationStatusCancelOperationTest(Gateway.ObservationStatus):
    def runTest(self):
        # set observe on Anjay
        observe_res_sv1 = self.observe_path(self.serv, path=self.path_res1)
        observe_res_sv2 = self.observe_path(self.serv, path=self.path_gw1_res1)
        observe_res_sv3 = self.observe_path(self.serv, path=self.path_gw2_res1)
        observe_res_mmv2 = self.observe_path(
            self.serv, path=self.path_gw1_res2)

        # check if observes are set
        self.assertTupleEqual(self.observation_status(self.path_res1),
                              self.OBSER_EXISTS_WO_ATTR)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res1),
                              self.OBSER_EXISTS_WO_ATTR)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw2_res1),
                              self.OBSER_EXISTS_WO_ATTR)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res2),
                              self.OBSER_EXISTS_WO_ATTR)

        # cancel one of them
        self.observe_path(self.serv, path=self.path_gw1_res1,
                          observe=1, token=observe_res_sv2.token)

        # check if rest are still set
        self.assertTupleEqual(self.observation_status(self.path_res1),
                              self.OBSER_EXISTS_WO_ATTR)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res1),
                              self.OBSER_NOT_EXISTS)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw2_res1),
                              self.OBSER_EXISTS_WO_ATTR)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res2),
                              self.OBSER_EXISTS_WO_ATTR)

        # cancel another one
        self.observe_path(self.serv, path=self.path_gw2_res1,
                          observe=1, token=observe_res_sv3.token)

        # check if rest are still set
        self.assertTupleEqual(self.observation_status(self.path_res1),
                              self.OBSER_EXISTS_WO_ATTR)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res1),
                              self.OBSER_NOT_EXISTS)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw2_res1),
                              self.OBSER_NOT_EXISTS)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res2),
                              self.OBSER_EXISTS_WO_ATTR)

        # cancel another one
        self.observe_path(self.serv, path=self.path_res1,
                          observe=1, token=observe_res_sv1.token)

        # check if rest are still set
        self.assertTupleEqual(self.observation_status(self.path_res1),
                              self.OBSER_NOT_EXISTS)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res1),
                              self.OBSER_NOT_EXISTS)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw2_res1),
                              self.OBSER_NOT_EXISTS)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res2),
                              self.OBSER_EXISTS_WO_ATTR)

        # cancel last one
        self.observe_path(self.serv, path=self.path_gw1_res2,
                          observe=1, token=observe_res_mmv2.token)

        # check there are no observes
        self.assertTupleEqual(self.observation_status(self.path_res1),
                              self.OBSER_NOT_EXISTS)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res1),
                              self.OBSER_NOT_EXISTS)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw2_res1),
                              self.OBSER_NOT_EXISTS)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res2),
                              self.OBSER_NOT_EXISTS)


class ObservationStatusRemoveObjectInstance(Gateway.ObservationStatus):
    def setUp(self):
        super().setUp(minimum_version='1.2', maximum_version='1.2')

    def runTest(self):
        self.create(self.serv, path=self.path_gw1_obj1)

        # set observe on Anjay
        self.observe_path(self.serv, path=self.path_res1)
        notif = self.observe_path(self.serv, path=self.path_gw1_res1, pmax=1)
        self.observe_path(self.serv, path=self.path_gw1_res2)
        self.observe_path(self.serv, path=self.path_gw1_res3)
        self.observe_path(self.serv, path=self.path_gw1_res4)
        self.observe_path(self.serv, path=self.path_gw2_res1)

        # check if observes are set
        self.assertTupleEqual(self.observation_status(self.path_res1),
                              self.OBSER_EXISTS_WO_ATTR)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res1),
                              self.OBSER_EXISTS_WO_ATTR)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res2),
                              self.OBSER_EXISTS_WO_ATTR)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res3),
                              self.OBSER_EXISTS_WO_ATTR)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res4),
                              self.OBSER_EXISTS_WO_ATTR)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw2_res1),
                              self.OBSER_EXISTS_WO_ATTR)

        self.delete(self.serv, path=self.path_gw1_inst1)

        not_found = self.serv.recv(timeout_s=1, filter=lambda pkt: pkt.code ==
                                   coap.Code.RES_NOT_FOUND and pkt.token == notif.token)
        res = Lwm2mEmpty.matching(not_found)()
        self.serv.send(res)

        self.assertTupleEqual(self.observation_status(self.path_res1),
                              self.OBSER_EXISTS_WO_ATTR)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res1),
                              self.OBSER_NOT_EXISTS)
        # this one is still active, because there is no pmax attribute and anjay_notify_changed is not called, so Anjay doesn't try to read value
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res2),
                              self.OBSER_EXISTS_WO_ATTR)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res3),
                              self.OBSER_EXISTS_WO_ATTR)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res4),
                              self.OBSER_EXISTS_WO_ATTR)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw2_res1),
                              self.OBSER_EXISTS_WO_ATTR)


class ObservationStatusRemoveEndDevice(Gateway.ObservationStatus):
    def setUp(self):
        super().setUp(minimum_version='1.2', maximum_version='1.2')

    def runTest(self):
        # set observe on Anjay
        self.observe_path(self.serv, path=self.path_res1)
        notif1 = self.observe_path(self.serv, path=self.path_gw1_res1, pmax=1)
        notif2 = self.observe_path(self.serv, path=self.path_gw1_res2)
        notif3 = self.observe_path(self.serv, path=self.path_gw1_inst1)
        self.observe_path(self.serv, path=self.path_gw2_res1)

        # check if observes are set
        self.assertTupleEqual(self.observation_status(self.path_res1),
                              self.OBSER_EXISTS_WO_ATTR)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res1),
                              self.OBSER_EXISTS_WO_ATTR)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res2),
                              self.OBSER_EXISTS_WO_ATTR)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw2_res1),
                              self.OBSER_EXISTS_WO_ATTR)

        self.communicate('gw_deregister 0',
                         match_regex='command: gw_deregister 0')
        res = self.read_object(self.serv, OID.Lwm2mGateway)
        for entry in CBOR.parse(res.content):
            name = entry.get(SenmlLabel.NAME, '')
            self.assertTrue("/0/" not in name)

        for _ in range(3):
            not_found = self.serv.recv(timeout_s=1, filter=lambda pkt: pkt.code == coap.Code.RES_NOT_FOUND and pkt.token in (
                notif1.token, notif2.token, notif3.token))
            res = Lwm2mEmpty.matching(not_found)()
            self.serv.send(res)

        self.assertTupleEqual(self.observation_status(self.path_res1),
                              self.OBSER_EXISTS_WO_ATTR)
        # unfortunately we don't go through anjay_observe_path_entry_t, we simple check if gateway instance with a such prefix exists
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res1),
                              self.OBSER_NOT_EXISTS)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res2),
                              self.OBSER_NOT_EXISTS)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw2_res1),
                              self.OBSER_EXISTS_WO_ATTR)


class ObservationStatusRemoveAfterGettingRST(Gateway.ObservationStatus):
    def setUp(self):
        super().setUp(minimum_version='1.2', maximum_version='1.2')

    def runTest(self):
        self.create(self.serv, path=self.path_gw1_obj1)

        self.write_attributes_path(
            self.serv, self.path_gw1_res1, query=['con=1'])
        # set observe on Anjay
        self.observe_path(self.serv, path=self.path_res1)
        notif1 = self.observe_path(self.serv, path=self.path_gw1_res1, pmax=1)
        self.observe_path(self.serv, path=self.path_gw1_res2)
        self.observe_path(self.serv, path=self.path_gw1_res3)
        self.observe_path(self.serv, path=self.path_gw1_res4)
        self.observe_path(self.serv, path=self.path_gw2_res1)

        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res1),
                              self.OBSER_EXISTS_WO_ATTR)

        notif2 = self.serv.recv(timeout_s=1, filter=lambda pkt: isinstance(
            pkt, Lwm2mNotify) and pkt.token == notif1.token)
        res = Lwm2mReset.matching(notif2)()
        self.serv.send(res)

        self.assertTupleEqual(self.observation_status(self.path_res1),
                              self.OBSER_EXISTS_WO_ATTR)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res1),
                              self.OBSER_NOT_EXISTS)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res2),
                              self.OBSER_EXISTS_WO_ATTR)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res3),
                              self.OBSER_EXISTS_WO_ATTR)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw1_res4),
                              self.OBSER_EXISTS_WO_ATTR)
        self.assertTupleEqual(self.gw_observation_status(self.path_gw2_res1),
                              self.OBSER_EXISTS_WO_ATTR)


class BootstrapEndDevices(Gateway.BaseWithBootstrap,
                          test_suite.Lwm2mDtlsSingleServerTest,
                          test_suite.Lwm2mDmOperations):
    def runTest(self):
        self.assertDemoRequestsBootstrap()

        # I need to hardcode the prefix and number of end devices here as the
        # Bootstrap Read can't target Objects other than /1 Server and
        # /2 Access Control; self.extractPrefixes() won't work

        # Try creating Gateway Object Instance
        self.write_instance(self.bootstrap_server,
                            oid=OID.Lwm2mGateway,
                            iid=3,
                            expect_error_code=coap.Code.RES_METHOD_NOT_ALLOWED,
                            partial=False)
        
        # Try deleting Gateway Object Instance
        self.delete_instance(self.bootstrap_server,
                             oid=OID.Lwm2mGateway,
                             iid=1,
                             expect_error_code=coap.Code.RES_METHOD_NOT_ALLOWED)
        
        # Try creating Instances of End Devices Objects
        self.write_path(server=self.bootstrap_server,
                        path='/dev0/%d/3' % OID.BinaryAppDataContainer,
                        expect_error_code=coap.Code.RES_METHOD_NOT_ALLOWED,
                        partial=False)
        
        # Try reading Resource on End Device 
        self.read_path(server=self.bootstrap_server,
                       path='/dev0/%d/0/0' % OID.BinaryAppDataContainer,
                       expect_error_code=coap.Code.RES_METHOD_NOT_ALLOWED)
        
        # Try writing a Resource on End Device
        self.write_path(server=self.bootstrap_server,
                        path='/dev0/%d/0/0/0' % OID.BinaryAppDataContainer,
                        content=b'00',
                        expect_error_code=coap.Code.RES_METHOD_NOT_ALLOWED,
                        partial=False)
        
        # Try discovering Object Instance on End Device
        self.discover_path(server=self.bootstrap_server,
                           path='/dev0/%d/0' % OID.BinaryAppDataContainer,
                           expect_error_code=coap.Code.RES_METHOD_NOT_ALLOWED)

        # Verify End Devices DM not reported in Discover '/'
        discover_root_res = self.discover_path(server=self.bootstrap_server,
                                               path='/')
        self.assertNotIn('dev', discover_root_res.content.decode())

        self.bootstrap_server.send(Lwm2mBootstrapFinish())

    def tearDown(self):
        super().tearDown(auto_deregister=False)


class ConfirmableNotificationStatusGateway(
        Gateway.ObservationStatus, ConfirmableNotificationStatus.Test):
    def setUp(self):
        super().setUp(extra_cmdline_args=['--confirmable-notifications'])

    def runTest(self):
        observe1_path = "/%s/%d/%d/%d" % (
                self.prefixes[0], OID.BinaryAppDataContainer, 0,
                                  RID.BinaryAppDataContainer.Data)
        observe2_path = "/%s/%d/%d/%d" % (
                self.prefixes[1], OID.BinaryAppDataContainer, 0,
                                  RID.BinaryAppDataContainer.Data)
        observe1 = self.observe_path(self.serv, observe1_path)
        observe2 = self.observe_path(self.serv, observe2_path)

        self.communicate("gw-badc-write 0 0 0 value1")
        self.communicate("gw-badc-write 1 0 0 value2")

        notify = self.serv.recv()

        self.assertIsInstance(notify, Lwm2mNotify)
        self.assertEqual(bytes(notify.token), bytes(observe1.token))
        self.serv.send(Lwm2mEmpty.matching(notify)())

        self.read_log_until_confirmable_notification_success(
            ssid=1, paths_count=1)
        self.read_log_until_path_occur(observe1_path)

        notify = self.serv.recv()

        self.assertIsInstance(notify, Lwm2mNotify)
        self.assertEqual(bytes(notify.token), bytes(observe2.token))
        self.serv.send(Lwm2mEmpty.matching(notify)())

        self.read_log_until_confirmable_notification_success(
            ssid=1, paths_count=1)
        self.read_log_until_path_occur(observe2_path)
