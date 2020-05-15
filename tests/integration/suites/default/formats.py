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
from framework.test_utils import *


class WriteRequest:
    def __init__(self, value, instance=1, resource=None, error=coap.Code.RES_BAD_REQUEST):
        self.resource = resource
        self.instance = instance
        self.value = value
        self.error = error


class FormatTest:
    # From RFC7252, section 12.3 "CoAP Content-Format Registry":
    #
    # "The identifiers between 65000 and 65535 inclusive are reserved for
    #  experiments."
    UNSUPPORTED_FORMAT = 65000

    COMMON_BAD_WRITE_SCENARIOS = [
        WriteRequest(resource=RID.Test.ResInt, value=b'not-an-int'),
        WriteRequest(resource=RID.Test.ResBool, value=b'not-a-bool'),
        WriteRequest(resource=RID.Test.ResFloat, value=b'not-a-float'),
        WriteRequest(resource=RID.Test.ResObjlnk, value=b'not-an-objlnk'),
    ]

    class Test(test_suite.Lwm2mSingleServerTest,
               test_suite.Lwm2mDmOperations):
        def setUp(self):
            super().setUp()
            self.create_instance(self.serv, oid=OID.Test, iid=1)

        def run_write_scenario(self, format, extra_scenarios=[]):
            for scenario in FormatTest.COMMON_BAD_WRITE_SCENARIOS + extra_scenarios:
                if scenario.resource is None:
                    self.write_instance(self.serv, oid=OID.Test, iid=scenario.instance,
                                        format=format, content=scenario.value,
                                        expect_error_code=scenario.error)
                else:
                    self.write_resource(self.serv, oid=OID.Test, iid=1,
                                        rid=scenario.resource, format=format,
                                        content=scenario.value,
                                        expect_error_code=scenario.error)


class BadFormatOnRequestTest(FormatTest.Test):
    def runTest(self):
        self.write_resource(self.serv, oid=OID.FirmwareUpdate, iid=0, rid=RID.FirmwareUpdate.PackageURI,
                            format=FormatTest.UNSUPPORTED_FORMAT,
                            expect_error_code=coap.Code.RES_UNSUPPORTED_CONTENT_FORMAT)


class BadFormatOnResponseTest(FormatTest.Test):
    def runTest(self):
        self.read_resource(self.serv, oid=OID.Device, iid=0, rid=RID.Device.Manufacturer,
                           accept=FormatTest.UNSUPPORTED_FORMAT,
                           expect_error_code=coap.Code.RES_NOT_ACCEPTABLE)


class PlaintextWritesTest(FormatTest.Test):
    def runTest(self):
        self.run_write_scenario(format=coap.ContentFormat.TEXT_PLAIN,
                                extra_scenarios=[
                                    # Writing the string/bytes isn't really interesting. Let's at least try with an empty payload.
                                    WriteRequest(
                                        resource=RID.Test.ResString, value=b'', error=None),
                                    WriteRequest(
                                        resource=RID.Test.ResRawBytes, value=b'', error=None),
                                    # Internal buffer for storing objlnk is sizeof("65535:65535") bytes only, let's overflow it.
                                    WriteRequest(
                                        resource=RID.Test.ResObjlnk, value=b'x'*32),
                                    # Write on instance? This isn't the right format.
                                    WriteRequest(value=b'nothing really'),
                                    WriteRequest(value=b''),
                                ])


class OpaqueWritesTest(FormatTest.Test):
    def runTest(self):
        # Almost nothing is supported for Opaque content format.
        self.run_write_scenario(format=coap.ContentFormat.APPLICATION_OCTET_STREAM,
                                extra_scenarios=[
                                    WriteRequest(
                                        resource=RID.Test.ResString, value=b''),
                                    # Basically the only thing that'd work with opaque contexts.
                                    WriteRequest(
                                        resource=RID.Test.ResRawBytes, value=b'', error=None),
                                    # Write on instance? This isn't the right format.
                                    WriteRequest(value=b'nothing really'),
                                    WriteRequest(value=b''),
                                ])


class TlvWritesTest(FormatTest.Test):
    def runTest(self):
        self.run_write_scenario(format=coap.ContentFormat.APPLICATION_LWM2M_TLV,
                                extra_scenarios=[
                                    WriteRequest(
                                        resource=RID.Test.ResString, value=b''),
                                    WriteRequest(
                                        resource=RID.Test.ResRawBytes, value=b''),
                                    # Empty write on instance does nothing, but at least it succeeds for TLV contexts.
                                    WriteRequest(value=b'', error=None)
                                ])


class UnsupportedFormatWritesTest(FormatTest.Test):
    def runTest(self):
        for rid in (RID.Test.ResInt,
                    RID.Test.ResBool,
                    RID.Test.ResFloat,
                    RID.Test.ResObjlnk,
                    RID.Test.ResRawBytes):
            for content in (b'whatever', b''):
                self.write_resource(self.serv, oid=OID.Test, iid=1, rid=rid,
                                    format=FormatTest.UNSUPPORTED_FORMAT,
                                    content=content,
                                    expect_error_code=coap.Code.RES_UNSUPPORTED_CONTENT_FORMAT)




class TlvRIDIncompatibleWithPathRID(FormatTest.Test):
    def runTest(self):
        self.write_resource(self.serv, oid=OID.Test, iid=1, rid=RID.Test.Counter,
                            format=coap.ContentFormat.APPLICATION_LWM2M_TLV,
                            content=TLV.make_resource(
                                resource_id=RID.Test.ResBytesSize, content=123).serialize(),
                            expect_error_code=coap.Code.RES_BAD_REQUEST)


class PreferredHierarchicalContentFormat_1_0(test_suite.Lwm2mSingleServerTest,
                                             test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(extra_cmdline_args=['--prefer-hierarchical-formats'])

    def runTest(self):
        self.assertEqual(coap.ContentFormat.APPLICATION_LWM2M_TLV,
                         self.read_instance(self.serv, oid=OID.Device, iid=0).get_content_format())


