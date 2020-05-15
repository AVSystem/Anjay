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
from suites.default.bootstrap_client import BootstrapTest
import socket
import unittest


class Tests:

    ROOT_PATH =     '/'
    OBJECT_PATH =   '/1'
    INSTANCE_PATH = '/1/2'
    RESOURCE_PATH = '/1/2/3'
    RESOURCE_INSTANCE_PATH = '/1/2/3/4'
    EXTENDED_PATH = '/1/2/3/4/5'

    def action(test, server, path, code, expect_error_code):
        path = CoapPath(path)
        req = coap.Packet(type=coap.Type.CONFIRMABLE,
                          code=code,
                          msg_id=ANY,
                          token=ANY,
                          options=path.to_uri_options())
        res = Lwm2mErrorResponse.matching(req)(code=expect_error_code)
        test._perform_action(server, req, res)

    class CoapTest(test_suite.Lwm2mSingleServerTest,
                   test_suite.Lwm2mDmOperations):
        def test(self, path, code, expect_error_code):
            Tests.action(self, self.serv, path, code, expect_error_code)

    class BootstrapTest(BootstrapTest.Test):
        def setUp(self):
            super().setUp(servers=0)

        def test(self, path, code, expect_error_code):
            self.assertDemoRequestsBootstrap()
            Tests.action(self, self.bootstrap_server, path, code, expect_error_code)


'''
Request to operation mapping:

Device Management & Service Enablement Interface
┌────────┬──────┬──────────────────┬──────────────────────────┬──────────────────────────┬──────────────────────────┬───────────────┐
│        │ Root │      Object      │         Instance         │         Resource         │    Resource Instance     │ Extended path │
├────────┼──────┼──────────────────┼──────────────────────────┼──────────────────────────┼──────────────────────────┼───────────────┤
│ GET    │ ---  │ Read / Discover  │ Read / Discover          │ Read / Discover          │ Read                     │ ---           │
│ PUT    │ ---  │ Write-Attributes │ Write / Write-Attributes │ Write / Write-Attributes │ Write / Write-Attributes │ ---           │
│ POST   │ ---  │ Create           │ Write                    │ Execute                  │ Write                    │ ---           │
│ DELETE │ ---  │ ---              │ Delete                   │ ---                      │ ---                      │ ---           │
└────────┴──────┴──────────────────┴──────────────────────────┴──────────────────────────┴──────────────────────────┴───────────────┘

Bootstrap Interface
┌────────┬──────────┬─────────────────┬──────────┬──────────┬───────────────────┬───────────────┐
│        │   Root   │     Object      │ Instance │ Resource │ Resource Instance │ Extended path │
├────────┼──────────┼─────────────────┼──────────┼──────────┼───────────────────┼───────────────┤
│ GET    │ Discover │ Read / Discover │ Read     │ ---      │ ---               │ ---           │
│ PUT    │ ---      │ Write           │ Write    │ Write    │ ---               │ ---           │
│ POST   │ ---      │ ---             │ ---      │ ---      │ ---               │ ---           │
│ DELETE │ Delete   │ Delete          │ Delete   │ ---      │ ---               │ ---           │
└────────┴──────────┴─────────────────┴──────────┴──────────┴───────────────────┴───────────────┘

* Discover requires Accept: application/link-format
** Write-Attributes has not Content-Format specified
'''


class GetOnRootPath(Tests.CoapTest):
    def runTest(self):
        self.test(path=Tests.ROOT_PATH,
                  code=coap.Code.REQ_GET,
                  expect_error_code=coap.Code.RES_BAD_REQUEST)


class PutOnRootPath(Tests.CoapTest):
    def runTest(self):
        self.test(path=Tests.ROOT_PATH,
                  code=coap.Code.REQ_PUT,
                  expect_error_code=coap.Code.RES_BAD_REQUEST)


class PostOnRootPath(Tests.CoapTest):
    def runTest(self):
        self.test(path=Tests.ROOT_PATH,
                  code=coap.Code.REQ_POST,
                  expect_error_code=coap.Code.RES_BAD_REQUEST)


class DeleteOnRootPath(Tests.CoapTest):
    def runTest(self):
        self.test(path=Tests.ROOT_PATH,
                  code=coap.Code.REQ_DELETE,
                  expect_error_code=coap.Code.RES_BAD_REQUEST)


class DeleteOnObjectPath(Tests.CoapTest):
    def runTest(self):
        self.test(path=Tests.OBJECT_PATH,
                  code=coap.Code.REQ_DELETE,
                  expect_error_code=coap.Code.RES_METHOD_NOT_ALLOWED)


class DeleteOnResourcePath(Tests.CoapTest):
    def runTest(self):
        self.test(path=Tests.RESOURCE_PATH,
                  code=coap.Code.REQ_DELETE,
                  expect_error_code=coap.Code.RES_METHOD_NOT_ALLOWED)


class DeleteOnResourceInstancePath(Tests.CoapTest):
    def runTest(self):
        self.test(path=Tests.RESOURCE_INSTANCE_PATH,
                  code=coap.Code.REQ_DELETE,
                  expect_error_code=coap.Code.RES_METHOD_NOT_ALLOWED)


class GetOnExtendedPath(Tests.CoapTest):
    def runTest(self):
        self.test(path=Tests.EXTENDED_PATH,
                  code=coap.Code.REQ_GET,
                  expect_error_code=coap.Code.RES_BAD_OPTION)


class PutOnExtendedPath(Tests.CoapTest):
    def runTest(self):
        self.test(path=Tests.EXTENDED_PATH,
                  code=coap.Code.REQ_PUT,
                  expect_error_code=coap.Code.RES_BAD_OPTION)


class PostOnExtendedPath(Tests.CoapTest):
    def runTest(self):
        self.test(path=Tests.EXTENDED_PATH,
                  code=coap.Code.REQ_POST,
                  expect_error_code=coap.Code.RES_BAD_OPTION)


class DeleteOnExtendedPath(Tests.CoapTest):
    def runTest(self):
        self.test(path=Tests.EXTENDED_PATH,
                  code=coap.Code.REQ_DELETE,
                  expect_error_code=coap.Code.RES_BAD_OPTION)


class BootstrapGetOnRootPath(Tests.BootstrapTest):
    def runTest(self):
        self.test(path=Tests.ROOT_PATH,
                  code=coap.Code.REQ_GET,
                  expect_error_code=coap.Code.RES_METHOD_NOT_ALLOWED)


class BootstrapPutOnRootPath(Tests.BootstrapTest):
    def runTest(self):
        self.test(path=Tests.ROOT_PATH,
                  code=coap.Code.REQ_PUT,
                  expect_error_code=coap.Code.RES_METHOD_NOT_ALLOWED)


class BootstrapPostOnRootPath(Tests.BootstrapTest):
    def runTest(self):
        self.test(path=Tests.ROOT_PATH,
                  code=coap.Code.REQ_POST,
                  expect_error_code=coap.Code.RES_METHOD_NOT_ALLOWED)


class BootstrapPostOnObjectPath(Tests.BootstrapTest):
    def runTest(self):
        self.test(path=Tests.OBJECT_PATH,
                  code=coap.Code.REQ_POST,
                  expect_error_code=coap.Code.RES_METHOD_NOT_ALLOWED)


class BootstrapPostOnInstancePath(Tests.BootstrapTest):
    def runTest(self):
        self.test(path=Tests.INSTANCE_PATH,
                  code=coap.Code.REQ_POST,
                  expect_error_code=coap.Code.RES_METHOD_NOT_ALLOWED)

class BootstrapGetOnResourcePath(Tests.BootstrapTest):
    def runTest(self):
        self.test(path=Tests.RESOURCE_PATH,
                  code=coap.Code.REQ_GET,
                  expect_error_code=coap.Code.RES_METHOD_NOT_ALLOWED)


class BootstrapPostOnResourcePath(Tests.BootstrapTest):
    def runTest(self):
        self.test(path=Tests.RESOURCE_PATH,
                  code=coap.Code.REQ_POST,
                  expect_error_code=coap.Code.RES_METHOD_NOT_ALLOWED)


class BootstrapDeleteOnResourcePath(Tests.BootstrapTest):
    def runTest(self):
        self.test(path=Tests.RESOURCE_PATH,
                  code=coap.Code.REQ_DELETE,
                  expect_error_code=coap.Code.RES_BAD_REQUEST)


class BootstrapGetOnResourceInstancePath(Tests.BootstrapTest):
    def runTest(self):
        self.test(path=Tests.RESOURCE_INSTANCE_PATH,
                  code=coap.Code.REQ_GET,
                  expect_error_code=coap.Code.RES_METHOD_NOT_ALLOWED)


class BootstrapPutOnResourceInstancePath(Tests.BootstrapTest):
    def runTest(self):
        self.test(path=Tests.RESOURCE_INSTANCE_PATH,
                  code=coap.Code.REQ_PUT,
                  expect_error_code=coap.Code.RES_METHOD_NOT_ALLOWED)


class BootstrapPostOnResourceInstancePath(Tests.BootstrapTest):
    def runTest(self):
        self.test(path=Tests.RESOURCE_INSTANCE_PATH,
                  code=coap.Code.REQ_POST,
                  expect_error_code=coap.Code.RES_METHOD_NOT_ALLOWED)


class BootstrapDeleteOnResourceInstancePath(Tests.BootstrapTest):
    def runTest(self):
        self.test(path=Tests.RESOURCE_INSTANCE_PATH,
                  code=coap.Code.REQ_DELETE,
                  expect_error_code=coap.Code.RES_BAD_REQUEST)


class BootstrapGetOnExtendedPath(Tests.BootstrapTest):
    def runTest(self):
        self.test(path=Tests.EXTENDED_PATH,
                  code=coap.Code.REQ_GET,
                  expect_error_code=coap.Code.RES_BAD_OPTION)


class BootstrapPutOnExtendedPath(Tests.BootstrapTest):
    def runTest(self):
        self.test(path=Tests.EXTENDED_PATH,
                  code=coap.Code.REQ_PUT,
                  expect_error_code=coap.Code.RES_BAD_OPTION)


class BootstrapPostOnExtendedPath(Tests.BootstrapTest):
    def runTest(self):
        self.test(path=Tests.EXTENDED_PATH,
                  code=coap.Code.REQ_POST,
                  expect_error_code=coap.Code.RES_BAD_OPTION)


class BootstrapDeleteOnExtendedPath(Tests.BootstrapTest):
    def runTest(self):
        self.test(path=Tests.EXTENDED_PATH,
                  code=coap.Code.REQ_DELETE,
                  expect_error_code=coap.Code.RES_BAD_OPTION)
