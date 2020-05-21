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

import base64
import itertools
import unittest

from framework.lwm2m_test import *
from framework.test_utils import *

from . import block_response as br


def test_object_bytes_generator(num_bytes):
    """ Generates exactly the same sequences of bytes as Test Object does. """
    return bytes(itertools.islice(itertools.cycle(range(128)), num_bytes))


class Base64Test:
    class Test(test_suite.Lwm2mSingleServerTest,
               test_suite.Lwm2mDmOperations):
        def setUp(self):
            super().setUp()
            self.create_instance(self.serv, oid=OID.Test, iid=1)

@unittest.skip("TODO: CoAP2 does not allow sending non-block messages larger than 1024")
class Base64DifferentLengths(Base64Test.Test):
    def runTest(self):
        for length in range(1, 1049):
            self.write_resource(self.serv, oid=OID.Test, iid=1,
                                rid=RID.Test.ResBytesSize, content=str(length))
            result = self.read_resource(self.serv, oid=OID.Test, iid=1, rid=RID.Test.ResBytes,
                                        accept=coap.ContentFormat.TEXT_PLAIN)
            decoded = base64.decodebytes(result.content)
            self.assertEquals(test_object_bytes_generator(length), decoded)


class Base64BlockTransfer(br.BlockResponseTest, test_suite.Lwm2mDmOperations):
    def runTest(self):
        LENGTH = 9001
        self.write_resource(self.serv, oid=OID.Test, iid=0,
                            rid=RID.Test.ResBytesSize, content=str(LENGTH))
        result = self.read_blocks(iid=0, accept=coap.ContentFormat.TEXT_PLAIN)
        decoded = base64.decodebytes(result)
        self.assertEquals(test_object_bytes_generator(LENGTH), decoded)


@unittest.skip("TODO: CoAP2 does not allow sending non-block messages larger than 1024")
class Base64ReadWrite(Base64Test.Test):
    def runTest(self):
        for length in range(1, 1049):
            raw_data = test_object_bytes_generator(length)
            b64_data = base64.encodebytes(raw_data).replace(b'\n', b'')

            self.write_resource(self.serv, oid=OID.Test, iid=1,
                                rid=RID.Test.ResRawBytes, content=b64_data)

            data = self.read_resource(self.serv, oid=OID.Test, iid=1,
                                      rid=RID.Test.ResRawBytes,
                                      accept=coap.ContentFormat.TEXT_PLAIN)
            self.assertEquals(raw_data, base64.decodebytes(data.content))


class Base64InvalidWrite(Base64Test.Test):
    def runTest(self):
        def write(value, expected_error_code=coap.Code.RES_BAD_REQUEST):
            self.write_resource(self.serv, oid=OID.Test, iid=1,
                                rid=RID.Test.ResRawBytes, content=value,
                                expect_error_code=expected_error_code)

        write(b'A=A')
        write(b'A=AAA')
        write(b'A AAA')
        write(b'A\nAAA')
        write(b'A\vAAA')
        write(b'A\tAAA')
        write(b'A==AAA=')
        write(b'A')
        write(b'AA')
        write(b'AAA')
        write(b'=')
        write(b'==')
        write(b'===')
