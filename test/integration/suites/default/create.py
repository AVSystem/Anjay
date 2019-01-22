# -*- coding: utf-8 -*-
#
# Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
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


class Create(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        req = Lwm2mCreate('/12360',  # geo-points object
                          b''.join(map(TLV.serialize,
                                       [TLV.make_resource(0, 42.0),  # latitude
                                        TLV.make_resource(1, 69.0),  # longitude
                                        TLV.make_resource(2, 5.0)])))  # radius
        self.serv.send(req)
        res = self.serv.recv()
        self.assertMsgEqual(Lwm2mCreated.matching(req)(), res)

        # attempt to create "the same" object again
        iid = int(res.get_options(coap.Option.LOCATION_PATH)[1].content)
        req = Lwm2mCreate('/12360',
                          TLV.make_instance(iid,
                                            [TLV.make_resource(0, 14.0),
                                             TLV.make_resource(1, 17.0),
                                             TLV.make_resource(2, 19.0)]).serialize())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(code=coap.Code.RES_BAD_REQUEST),
                            self.serv.recv())


class IncompleteCreate(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        req = Lwm2mCreate('/12360',  # geo-points object
                          b''.join(map(TLV.serialize,
                                       [TLV.make_resource(0, 42.0),  # latitude
                                        TLV.make_resource(1, 69.0)])))  # longitude
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(code=coap.Code.RES_BAD_REQUEST),
                            self.serv.recv())


class UnsupportedCreate(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        req = Lwm2mCreate('/12360',  # geo-points object
                          b''.join(map(TLV.serialize,
                                       [TLV.make_resource(0, 42.0),
                                        TLV.make_resource(1, 69.0),
                                        TLV.make_resource(2, 5.0),
                                        TLV.make_resource(42, b'hello')])))  # unrecognized
        self.serv.send(req)
        res = self.serv.recv()
        self.assertMsgEqual(Lwm2mCreated.matching(req)(), res)

        req = Lwm2mRead(res.get_location_path())
        self.serv.send(req)
        res = self.serv.recv()
        self.assertMsgEqual(Lwm2mContent.matching(req)(
            format=coap.ContentFormat.APPLICATION_LWM2M_TLV,
            content=b''.join(map(TLV.serialize,
                                 [TLV.make_resource(0, 42.0),
                                  TLV.make_resource(1, 69.0),
                                  TLV.make_resource(2, 5.0),
                                  TLV.make_resource(3, b''),
                                  TLV.make_resource(4, 0)]))), res)


class RootPathOnCreate(test_suite.Lwm2mSingleServerTest,
                       test_suite.Lwm2mDmOperations):
    def runTest(self):
        self.create(self.serv, path='/', expect_error_code=coap.Code.RES_BAD_REQUEST)
