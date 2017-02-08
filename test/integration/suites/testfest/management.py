from framework.lwm2m_test import *

import unittest

from .dm.utils import DataModel, ValueValidator

class Test201_QueryingBasicInformationInPlainTextFormat(DataModel.Test):
    def runTest(self):
        self.test_read(ResPath.Device.Manufacturer, ValueValidator.ascii_string(), coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.Device.ModelNumber,  ValueValidator.ascii_string(), coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.Device.SerialNumber, ValueValidator.ascii_string(), coap.ContentFormat.TEXT_PLAIN)

        # 1. Server has received the requested information and display of the
        # following data to the user is possible:
        #   - Manufacturer
        #   - Model number
        #   - Serial number

class Test203_QueryingBasicInformationInTLVFormat(DataModel.Test):
    def runTest(self):
        tlv_with_string = ValueValidator.tlv_resources(ValueValidator.ascii_string())

        self.test_read(ResPath.Device.Manufacturer, tlv_with_string, coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.test_read(ResPath.Device.ModelNumber,  tlv_with_string, coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.test_read(ResPath.Device.SerialNumber, tlv_with_string, coap.ContentFormat.APPLICATION_LWM2M_TLV)

        # 1. Server has received the requested information and display of the
        # following data to the user is possible:
        #   - Manufacturer
        #   - Model number
        #   - Serial number

@unittest.skip("JSON format not implemented")
class Test204_QueryingBasicInformationInJSONFormat(DataModel.Test):
    def runTest(self):
        self.test_read(ResPath.Device.Manufacturer, ValueValidator.ascii_string(), coap.ContentFormat.APPLICATION_LWM2M_JSON)
        self.test_read(ResPath.Device.ModelNumber,  ValueValidator.ascii_string(), coap.ContentFormat.APPLICATION_LWM2M_JSON)
        self.test_read(ResPath.Device.SerialNumber, ValueValidator.ascii_string(), coap.ContentFormat.APPLICATION_LWM2M_JSON)

        # 1. Server has received the requested information and display of the
        # following data to the user is possible:
        #   - Manufacturer
        #   - Model number
        #   - Serial number

class Test205_SettingBasicInformationInPlainTextFormat(DataModel.Test):
    def runTest(self):
        # This test has to be run for the following resources:
        #   a) Lifetime
        #   b) Default minimum period
        #   c) Default maximum period
        #   d) Disable timeout
        #   e) Notification storing when disabled or offline
        #   f) Binding

        # 1. The server receives the status code “2.04”
        # 2. The resource value has changed according to WRITE request
        self.test_write_validated(ResPath.Server[1].Lifetime, '123')
        self.test_write_validated(ResPath.Server[1].DefaultMinPeriod, '234')
        self.test_write_validated(ResPath.Server[1].DefaultMaxPeriod, '345')
        self.test_write_validated(ResPath.Server[1].DisableTimeout, '456')
        self.test_write_validated(ResPath.Server[1].NotificationStoring, '1')

        self.test_write(ResPath.Server[1].Binding, 'UQ')
        self.assertDemoUpdatesRegistration(lifetime=123, binding='UQ') # triggered by Binding change
        self.assertEqual(b'UQ', self.test_read(ResPath.Server[1].Binding))

class Test241_ExecutableResourceRebootingTheDevice(test_suite.Lwm2mSingleServerTest):
    def _get_valgrind_args(self):
        # Reboot cannot be performed when demo is run under valgrind
        return []

    def runTest(self):
        # Server performs Execute (COAP POST) on device/reboot resource
        req = Lwm2mExecute(ResPath.Device.Reboot)
        self.serv.send(req)
        # Server receives success message (2.04 Changed) from client
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        # 1. Device reboots successfully and re-registers at the server again
        self.serv.reset()
        self.assertDemoRegisters(timeout_s=3)
