from .lwm2m import coap
from .firmware_package import FirmwareUpdateForcedError, make_firmware_package

import binascii
import struct
from typing import Optional


class SequentialMsgIdGenerator:
    def __init__(self, start_id):
        self.curr_id = start_id

    def __next__(self):
        return self.next()

    def next(self):
        self.curr_id = (self.curr_id + 1) % 2**16
        return self.curr_id


def random_stuff(size):
    import os
    return os.urandom(size)


def uri_path_to_options(path):
    assert path.startswith('/')
    return [coap.Option.URI_PATH(x) for x in path.split('/')[1:]]


def get_another_token(token):
    other = token
    while other == token:
        other = random_stuff(8)
    return other


class OID:
    Security = 0
    Server = 1
    AccessControl = 2
    Device = 3
    ConnectivityMonitoring = 4
    FirmwareUpdate = 5
    Location = 6
    ConnectivityStatistics = 7
    Test = 1337

class RID:
    class Security:
        ServerURI = 0
        Bootstrap = 1
        Mode = 2
        PKOrIdentity = 3
        ServerPKOrIdentity = 4
        SecretKey = 5
        SMSSecurityMode = 6
        SMSBindingKeyParameters = 7
        SMSBindingSecretKeys = 8
        ServerSMSNumber = 9
        ShortServerID = 10
        ClientHoldOffTime = 11
        BootstrapTimeout = 12

    class Server:
        ShortServerID = 0
        Lifetime = 1
        DefaultMinPeriod = 2
        DefaultMaxPeriod = 3
        Disable = 4
        DisableTimeout = 5
        NotificationStoring = 6
        Binding = 7
        RegistrationUpdateTrigger = 8

    class AccessControl:
        TargetOID = 0
        TargetIID = 1
        ACL = 2
        Owner = 3

    class Device:
        Manufacturer = 0
        ModelNumber = 1
        SerialNumber = 2
        FirmwareVersion = 3
        Reboot = 4
        FactoryReset = 5
        AvailablePowerSources = 6
        PowerSourceVoltage = 7
        PowerSourceCurrent = 8
        BatteryLevel = 9
        MemoryFree = 10
        ErrorCode = 11
        ResetErrorCode = 12
        CurrentTime = 13
        UTCOffset = 14
        Timezone = 15
        SupportedBindingAndModes = 16
        DeviceType = 17
        HardwareVersion = 18
        SoftwareVersion = 19
        BatteryStatus = 20
        MemoryTotal = 21
        ExtDevInfo = 22

    class ConnectivityMonitoring:
        NetworkBearer = 0
        AvailableNetworkBearer = 1
        RadioSignalStrength = 2
        LinkQuality = 3
        IPAddresses = 4
        RouterIPAddresses = 5
        LinkUtilization = 6
        APN = 7
        CellID = 8
        SMNC = 9
        SMCC = 10

    class FirmwareUpdate:
        Package = 0
        PackageURI = 1
        Update = 2
        State = 3
        UpdateSupportedObjects = 4
        UpdateResult = 5
        PackageName = 6
        PackageVersion = 7
        FirmwareUpdateProtocolSupport = 8
        FirmwareUpdateDeliveryMethod = 9

    class Location:
        Latitude = 0
        Longitude = 1
        Altitude = 2
        Uncertainty = 3
        Velocity = 4
        Timestamp = 5

    class ConnectivityStatistics:
        SMSTxCounter = 0
        SMSRxCounter = 1
        TxData = 2
        RxData = 3
        MaxMessageSize = 4
        AverageMessageSize = 5
        Start = 6
        Stop = 7
        CollectionPeriod = 8
        CollectionDuration = 9

    class GeoPoints:
        Latitude = 0
        Longitude = 1
        Radius = 2
        Description = 3
        Inside = 4

    class Test:
        Timestamp = 0
        Counter = 1
        IncrementCounter = 2
        IntArray = 3
        LastExecArgsArray = 4
        ResBytes = 5
        ResBytesSize = 6
        ResBytesBurst = 7
        ResEmpty = 8
        ResInitIntArray = 9
        ResRawBytes = 10


class _Lwm2mResourcePathHelper:
    @classmethod
    def from_rid_object(cls, rid_obj, oid, multi_instance=False):
        return cls(resources={k: v for k, v in rid_obj.__dict__.items() if isinstance(v, int)},
                   oid=oid,
                   multi_instance=multi_instance)

    def __init__(self, resources, oid, iid=None, multi_instance=False):
        self._resources = resources
        self._oid = oid
        self._is_multi_instance = multi_instance

        if iid is not None:
            self._iid = iid
        else:
            self._iid = 0 if not self._is_multi_instance else None

    def __getitem__(self, iid):
        assert self._iid is None, "IID specified more than once"
        assert self._is_multi_instance or self._iid == 0, "IID must be 0 on single-instance objects"

        return type(self)(self._resources, oid=self._oid, iid=iid, multi_instance=True)

    def __getattr__(self, name):
        if name in self._resources:
            assert self._iid is not None, "IID not specified. Use ObjectName[IID].ResourceName"
            return '/%d/%d/%d' % (self._oid, self._iid, self._resources[name])
        raise AttributeError


class ResPath:
    Security = _Lwm2mResourcePathHelper.from_rid_object(RID.Security, oid=OID.Security, multi_instance=True)
    Server = _Lwm2mResourcePathHelper.from_rid_object(RID.Server, oid=OID.Server, multi_instance=True)
    Device = _Lwm2mResourcePathHelper.from_rid_object(RID.Device, oid=OID.Device)
    ConnectivityMonitoring = _Lwm2mResourcePathHelper.from_rid_object(RID.ConnectivityMonitoring, oid=OID.ConnectivityMonitoring)
    FirmwareUpdate = _Lwm2mResourcePathHelper.from_rid_object(RID.FirmwareUpdate, oid=OID.FirmwareUpdate)
    Location = _Lwm2mResourcePathHelper.from_rid_object(RID.Location, oid=OID.Location)
    ConnectivityStatistics = _Lwm2mResourcePathHelper.from_rid_object(RID.ConnectivityStatistics, oid=OID.ConnectivityStatistics)
    Test = _Lwm2mResourcePathHelper.from_rid_object(RID.Test, oid=OID.Test, multi_instance=True)


DEMO_ENDPOINT_NAME = 'urn:dev:os:0023C7-000001'
DEMO_LWM2M_VERSION = '1.0'
