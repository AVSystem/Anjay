# -*- coding: utf-8 -*-
#
# Copyright 2017-2021 AVSystem <avsystem@avsystem.com>
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

import binascii
import struct
import tempfile
import collections
import sys

from typing import Optional

from .lwm2m import coap
from .firmware_package import FirmwareUpdateForcedError, make_firmware_package

if sys.version_info[0] == 3 and sys.version_info[1] < 7:
    # based on https://stackoverflow.com/a/18348004/2339636
    def namedtuple(*args, defaults=None, **kwargs):
        cls = collections.namedtuple(*args, **kwargs)
        cls.__new__.__defaults__ = defaults
        return cls
else:
    namedtuple = collections.namedtuple


class SequentialMsgIdGenerator:
    def __init__(self, start_id):
        self.curr_id = start_id

    def __next__(self):
        return self.next()

    def next(self):
        self.curr_id = (self.curr_id + 1) % 2 ** 16
        return self.curr_id


def generate_temp_filename(suffix='', prefix='tmp', dir=None):
    with tempfile.NamedTemporaryFile(suffix=suffix, prefix=prefix, dir=dir) as f:
        return f.name


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
    CellularConnectivity = 10
    ApnConnectionProfile = 11
    Portfolio = 16
    BinaryAppDataContainer = 19
    EventLog = 20
    Oscore = 21
    Temperature = 3303
    Accelerometer = 3313
    PushButton = 3347
    Test = 33605
    ExtDevInfo = 33606
    IpPing = 33607
    GeoPoints = 33608
    DownloadDiagnostics = 33609


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

    class CellularConnectivity:
        SMSCAddress = 0
        DisableRadioPeriod = 1
        ModuleActivationCode = 2
        VendorSpecificExtensions = 3
        PSMTimer = 4
        ActiveTimer = 5
        ServingPLMNRateControl = 6
        eDRXParamtersForIuMode = 7
        eDRXParamtersForWBS1Mode = 8
        eDRXParametersForNBS1Mode = 9
        eDRXParametersForAGbMode = 10
        ActivatedProfileNames = 11
        CoverageEnhancementLevel = 12
        PowerSavingModes = 13
        ActivePowerSavingModes = 14

    class ApnConnectionProfile:
        ProfileName = 0
        APN = 1
        AutoSelectAPNByDevice = 2
        EnableStatus = 3
        AuthenticationType = 4
        UserName = 5
        Secret = 6
        ReconnectSchedule = 7
        Validity = 8
        ConnectionEstablishmentTime = 9
        ConnectionEstablishmentResult = 10
        ConnectionEstablishmentRejectCause = 11
        ConnectionEndTime = 12
        TotalBytesSent = 13
        TotalBytesReceived = 14
        IPAddress = 15
        PrefixLength = 16
        SubnetMask = 17
        Gateway = 18
        PrimaryDNSAddress = 19
        SecondaryDNSAddress = 20
        QCI = 21
        VendorSpecificExtensions = 22
        TotalPacketsSent = 23
        PDNType = 24
        APNRateControl = 25

    class GeoPoints:
        Latitude = 0
        Longitude = 1
        Radius = 2
        Description = 3
        Inside = 4

    class Temperature:
        MinMeasuredValue = 5601
        MaxMeasuredValue = 5602
        MinRangeValue = 5603
        MaxRangeValue = 5604
        ResetMinAndMaxMeasuredValues = 5605
        SensorValue = 5700
        SensorUnits = 5701

    class Accelerometer:
        MinRangeValue = 5603
        MaxRangeValue = 5604
        XValue = 5702
        YValue = 5703
        ZValue = 5704
        SensorUnits = 5701

    class PushButton:
        DigitalInputState = 5500
        DigitalInputCounter = 5501
        ApplicationType = 5750

    class Test:
        Timestamp = 0
        Counter = 1
        IncrementCounter = 2
        IntArray = 3
        LastExecArgsArray = 4
        ResBytes = 5
        ResBytesSize = 6
        ResBytesBurst = 7
        ResInitIntArray = 9
        ResRawBytes = 10
        ResOpaqueArray = 11
        ResInt = 12
        ResBool = 13
        ResFloat = 14
        ResString = 15
        ResObjlnk = 16
        ResBytesZeroBegin = 17
        ResDouble = 18

    class Portfolio:
        Identity = 0
        GetAuthData = 1
        AuthData = 2
        AuthStatus = 3

    class ExtDevInfo:
        ObuId = 0
        PlateNumber = 1
        Imei = 2
        Imsi = 3
        Iccid = 4
        GprsRssi = 5
        GprsPlmn = 6
        GprsUlModulation = 7
        GprsDlModulation = 8
        GprsUlFrequency = 9
        GprsDlFrequency = 10
        RxBytes = 11
        TxBytes = 12
        NumIncomingRetransmissions = 13
        NumOutgoingRetransmissions = 14
        Uptime = 15

    class IpPing:
        Hostname = 0
        Repetitions = 1
        TimeoutMs = 2
        BlockSize = 3
        Dscp = 4
        Run = 5
        State = 6
        SuccessCount = 7
        ErrorCount = 8
        AvgTimeMs = 9
        MinTimeMs = 10
        MaxTimeMs = 11
        TimeStdevUs = 12

    class DownloadDiagnostics:
        State = 0
        Url = 1
        RomTimeUs = 2
        BomTimeUs = 3
        EomTimeUs = 4
        TotalBytes = 5
        Run = 6

    class Oscore:
        MasterSecret = 0
        SenderId = 1
        RecipientId = 2
        AeadAlgorithm = 3
        HmacAlgorithm = 4
        MasterSalt = 5
        IdContext = 6

    class BinaryAppDataContainer:
        Data = 0
        DataPriority = 1
        DataCreationTime = 2
        DataDescription = 3
        DataFormat = 4
        AppId = 5

    class EventLog:
        LogClass = 4010
        LogStart = 4011
        LogStop = 4012
        LogStatus = 4013
        LogData = 4014
        LogDataFormat = 4015


class _Lwm2mResourcePathHelper:
    @classmethod
    def from_rid_object(cls, rid_obj, oid, multi_instance=False, version=None):
        return cls(resources={k: v for k, v in rid_obj.__dict__.items() if isinstance(v, int)},
                   oid=oid,
                   multi_instance=multi_instance,
                   version=version)

    def __init__(self, resources, oid, iid=None, multi_instance=False, version=None):
        self.resources = resources
        self.oid = oid
        self.is_multi_instance = multi_instance
        self.version = version

        if iid is not None:
            self.iid = iid
        else:
            self.iid = 0 if not self.is_multi_instance else None

    def __getitem__(self, iid):
        assert self.iid is None, "IID specified more than once"
        assert self.is_multi_instance or self.iid == 0, "IID must be 0 on single-instance objects"

        return type(self)(self.resources, oid=self.oid, iid=iid,
                          multi_instance=True, version=self.version)

    def __getattr__(self, name):
        if name in self.resources:
            assert self.iid is not None, "IID not specified. Use ObjectName[IID].ResourceName"
            return '/%d/%d/%d' % (self.oid, self.iid, self.resources[name])
        raise AttributeError


class ResPath:
    Security = _Lwm2mResourcePathHelper.from_rid_object(
        RID.Security, oid=OID.Security, multi_instance=True)
    Server = _Lwm2mResourcePathHelper.from_rid_object(
        RID.Server, oid=OID.Server, multi_instance=True)
    AccessControl = _Lwm2mResourcePathHelper.from_rid_object(RID.AccessControl,
                                                             oid=OID.AccessControl,
                                                             multi_instance=True)
    Device = _Lwm2mResourcePathHelper.from_rid_object(
        RID.Device, oid=OID.Device)
    ConnectivityMonitoring = _Lwm2mResourcePathHelper.from_rid_object(RID.ConnectivityMonitoring,
                                                                      oid=OID.ConnectivityMonitoring)
    FirmwareUpdate = _Lwm2mResourcePathHelper.from_rid_object(
        RID.FirmwareUpdate, oid=OID.FirmwareUpdate)
    Location = _Lwm2mResourcePathHelper.from_rid_object(
        RID.Location, oid=OID.Location)
    ConnectivityStatistics = _Lwm2mResourcePathHelper.from_rid_object(RID.ConnectivityStatistics,
                                                                      oid=OID.ConnectivityStatistics)
    CellularConnectivity = _Lwm2mResourcePathHelper.from_rid_object(RID.CellularConnectivity,
                                                                    oid=OID.CellularConnectivity,
                                                                    version='1.1')
    ApnConnectionProfile = _Lwm2mResourcePathHelper.from_rid_object(RID.ApnConnectionProfile,
                                                                    oid=OID.ApnConnectionProfile,
                                                                    multi_instance=True)
    Temperature = _Lwm2mResourcePathHelper.from_rid_object(
        RID.Temperature, oid=OID.Temperature)
    Accelerometer = _Lwm2mResourcePathHelper.from_rid_object(
        RID.Accelerometer, oid=OID.Accelerometer)
    PushButton = _Lwm2mResourcePathHelper.from_rid_object(
        RID.PushButton, oid=OID.PushButton)
    Test = _Lwm2mResourcePathHelper.from_rid_object(
        RID.Test, oid=OID.Test, multi_instance=True)
    Portfolio = _Lwm2mResourcePathHelper.from_rid_object(
        RID.Portfolio, oid=OID.Portfolio, multi_instance=True)
    ExtDevInfo = _Lwm2mResourcePathHelper.from_rid_object(
        RID.ExtDevInfo, oid=OID.ExtDevInfo)
    IpPing = _Lwm2mResourcePathHelper.from_rid_object(
        RID.IpPing, oid=OID.IpPing)
    GeoPoints = _Lwm2mResourcePathHelper.from_rid_object(
        RID.GeoPoints, oid=OID.GeoPoints, multi_instance=True)
    DownloadDiagnostics = _Lwm2mResourcePathHelper.from_rid_object(RID.DownloadDiagnostics,
                                                                   oid=OID.DownloadDiagnostics)
    BinaryAppDataContainer = _Lwm2mResourcePathHelper.from_rid_object(
        RID.BinaryAppDataContainer, oid=OID.BinaryAppDataContainer, multi_instance=True)
    EventLog = _Lwm2mResourcePathHelper.from_rid_object(RID.EventLog, oid=OID.EventLog)

    @classmethod
    def objects(cls):
        results = []
        for _, field in cls.__dict__.items():
            if isinstance(field, _Lwm2mResourcePathHelper):
                results.append(field)
        return sorted(results, key=lambda field: field.oid)


class TxParams(namedtuple('TxParams',
                          ['ack_timeout',
                           'ack_random_factor',
                           'max_retransmit',
                           'max_latency'],
                          defaults=(2.0, 1.5, 4.0, 100.0))):
    def max_transmit_wait(self):
        return self.ack_timeout * self.ack_random_factor * (2**(self.max_retransmit + 1) - 1)

    def max_transmit_span(self):
        return self.ack_timeout * (2**self.max_retransmit - 1) * self.ack_random_factor

    def exchange_lifetime(self):
        """
        From RFC7252: "PROCESSING_DELAY is the time a node takes to turn
        around a Confirmable message into an acknowledgement. We assume
        the node will attempt to send an ACK before having the sender time
        out, so as a conservative assumption we set it equal to ACK_TIMEOUT"

        Thus we use self.ack_timeout as a PROCESSING_DELAY in the formula below.
        """
        return self.max_transmit_span() + 2 * self.max_latency + self.ack_timeout

    def first_retransmission_timeout(self):
        return self.ack_random_factor * self.ack_timeout

    def last_retransmission_timeout(self):
        return self.first_retransmission_timeout() * 2**self.max_retransmit


DEMO_ENDPOINT_NAME = 'urn:dev:os:0023C7-000001'
