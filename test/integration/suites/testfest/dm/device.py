from framework.lwm2m_test import *

from .utils import DataModel, ValueValidator


class Test651_DeviceObject_QueryingTheReadableResourcesOfObject(DataModel.Test):
    def runTest(self):
        # A READ operation from server on the resource has been received by the
        # client. This test has to be run on the following resources:
        # a) Manufacturer
        # b) Device type
        # c) Model number
        # d) Serial number
        # e) Hardware version
        # f) Firmware version
        # g) Software version
        # h) Available power sources
        # i) Power source voltage
        # j) Power source current
        # k) Battery level
        # l) Battery status
        # m) Memory free
        # n) Memory total
        # o) Error code
        # p) Current time
        # q) UTC offset
        # r) Timezone
        # s) Supported binding and modes

        integer_array = ValueValidator.multiple_resource(ValueValidator.from_raw_int())

        self.test_read(ResPath.Device.Manufacturer,             ValueValidator.ascii_string(), coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.Device.ModelNumber,              ValueValidator.ascii_string(), coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.Device.SerialNumber,             ValueValidator.ascii_string(), coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.Device.FirmwareVersion,          ValueValidator.ascii_string(), coap.ContentFormat.TEXT_PLAIN)
        # TODO: this cannot be represented in text/plain format
        self.test_read(ResPath.Device.AvailablePowerSources,    integer_array,                 coap.ContentFormat.APPLICATION_LWM2M_TLV)
        # TODO: this cannot be represented in text/plain format
        self.test_read(ResPath.Device.PowerSourceVoltage,       integer_array,                 coap.ContentFormat.APPLICATION_LWM2M_TLV)
        # TODO: this cannot be represented in text/plain format
        self.test_read(ResPath.Device.PowerSourceCurrent,       integer_array,                 coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.test_read(ResPath.Device.BatteryLevel,             ValueValidator.integer(),      coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.Device.MemoryFree,               ValueValidator.integer(),      coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.Device.ErrorCode,                integer_array,                 coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.test_read(ResPath.Device.CurrentTime,              ValueValidator.integer(),      coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.Device.UTCOffset,                ValueValidator.ascii_string(), coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.Device.Timezone,                 ValueValidator.ascii_string(), coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.Device.SupportedBindingAndModes, ValueValidator.ascii_string(), coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.Device.DeviceType,               ValueValidator.ascii_string(), coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.Device.HardwareVersion,          ValueValidator.ascii_string(), coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.Device.SoftwareVersion,          ValueValidator.ascii_string(), coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.Device.BatteryStatus,            ValueValidator.ascii_string(), coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.Device.MemoryTotal,              ValueValidator.ascii_string(), coap.ContentFormat.TEXT_PLAIN)


class Test652_DeviceObject_QueryingTheFirmwareVersionFromTheClient(DataModel.Test):
    def runTest(self):
        # 1. Server has received the requested information and displays to the
        #    user the following information:
        #    - Firmware version
        self.test_read(ResPath.Device.FirmwareVersion, ValueValidator.ascii_string(), coap.ContentFormat.TEXT_PLAIN)


class Test655_DeviceObject_SettingTheWritableResources(DataModel.Test):
    def runTest(self):
        # A WRITE operation from server on the resource has been received by the
        # client. This test has to be run for the following resources:
        # a) Current time
        # b) UTC offset
        # c) Timezone

        self.test_write_validated(ResPath.Device.CurrentTime, '100',
                                  alternative_acceptable_values=['101', '102', '103'])
        self.test_write_validated(ResPath.Device.UTCOffset, 'UTC+12:45')
        self.test_write_validated(ResPath.Device.Timezone, 'Pacific/Chatham')


class Test660_DeviceObject_ObservationAndNotificationOfObservableResources(DataModel.Test):
    def runTest(self):
        # The Server establishes an Observation relationship with the Client to acquire
        # condition notifications about observable resources. This test has to be run for
        # the following resources:
        # a) Manufacturer
        # b) Device type
        # c) Model number
        # d) Serial number
        # e) Hardware version
        # f) Firmware version
        # g) Software version
        # h) Available power sources
        # i) Power source voltage
        # j) Power source current
        # k) Battery level
        # l) Battery status
        # m) Memory free
        # n) Memory total
        # o) Error code
        # p) Current time
        # q) UTC offset
        # r) Timezone
        # s) Supported binding and modes

        integer_array = ValueValidator.multiple_resource(ValueValidator.from_raw_int())

        self.test_observe(ResPath.Device.Manufacturer,             ValueValidator.ascii_string())
        self.test_observe(ResPath.Device.ModelNumber,              ValueValidator.ascii_string())
        self.test_observe(ResPath.Device.SerialNumber,             ValueValidator.ascii_string())
        self.test_observe(ResPath.Device.FirmwareVersion,          ValueValidator.ascii_string())
        self.test_observe(ResPath.Device.AvailablePowerSources,    integer_array)
        self.test_observe(ResPath.Device.PowerSourceVoltage,       integer_array)
        self.test_observe(ResPath.Device.PowerSourceCurrent,       integer_array)
        self.test_observe(ResPath.Device.BatteryLevel,             ValueValidator.integer())
        self.test_observe(ResPath.Device.MemoryFree,               ValueValidator.integer())
        self.test_observe(ResPath.Device.ErrorCode,                integer_array)
        self.test_observe(ResPath.Device.CurrentTime,              ValueValidator.integer())
        self.test_observe(ResPath.Device.UTCOffset,                ValueValidator.ascii_string())
        self.test_observe(ResPath.Device.Timezone,                 ValueValidator.ascii_string())
        self.test_observe(ResPath.Device.SupportedBindingAndModes, ValueValidator.ascii_string())
        self.test_observe(ResPath.Device.DeviceType,               ValueValidator.ascii_string())
        self.test_observe(ResPath.Device.HardwareVersion,          ValueValidator.ascii_string())
        self.test_observe(ResPath.Device.SoftwareVersion,          ValueValidator.ascii_string())
        self.test_observe(ResPath.Device.BatteryStatus,            ValueValidator.ascii_string())
        self.test_observe(ResPath.Device.MemoryTotal,              ValueValidator.ascii_string())
