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

import time

from framework.lwm2m_test import *


# Setting up the CoAP stream for communication with a specific server is done
# by calling _anjay_get_server_stream. The stream has to be released with
# _anjay_release_server_stream before acquiring it again, or the library will
# cause an assertion failure.
#
# There was an issue in the Update sending routine that prevented the release
# call from being made in the case of an socket error, such as receiving the
# "Port Unreachable" ICMP message. This caused the demo to terminate on the
# next attempt to use the stream.
#
# This test case ensures that the demo does not crash in such case.

class AccessViolationOn256ByteUri(test_suite.Lwm2mSingleServerTest):
    # overrides a method from Lwm2mTest, called from Lwm2mTest.setup_demo_with_servers()
    def make_demo_args(self, *args, **kwargs):
        result = super().make_demo_args(*args, **kwargs)
        url = result[-1]
        self.assertTrue(url.startswith('coap://'))  # assert it is really a URL
        url = url.replace('.0', '.' + '0' * (256 - len(url) + 1), 1)  # "coap://127.000(...)000.0.1:12345/"
        self.assertEqual(len(url), 256)
        result[-1] = url
        return result

    def setUp(self):
        super().setUp(auto_register=False)

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        pass


class CrashAfterRequestWithTokenFollowedByNoToken(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        self.serv.set_timeout(timeout_s=1)

        # failure to clear the token from last message remembered by output
        # buffer combined with not overriding cached token length in case of
        # a zero-length token caused anjay to incorrectly assume a non-empty
        # token was already written to the buffer
        req = Lwm2mRead(ResPath.Location.Latitude, token=b'foo')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mContent.matching(req)(),
                            self.serv.recv())

        # the bug causes assertion failure during handling of this message
        req = Lwm2mRead(ResPath.Location.Latitude, token=b'')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mContent.matching(req)(),
                            self.serv.recv())


class ObserveCancelCrash(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        req = Lwm2mWriteAttributes(ResPath.Device.PowerSourceVoltage, pmax=1,
                                   options=[coap.Option.URI_QUERY('con=1')])
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        req = Lwm2mObserve(ResPath.Device.PowerSourceVoltage)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mContent.matching(req)(), self.serv.recv())

        notif = self.serv.recv(timeout_s=1.5)
        self.assertIsInstance(notif, Lwm2mNotify)
        self.assertEqual(notif.type, coap.Type.CONFIRMABLE)

        # send Cancel Observe before the ACK
        req = Lwm2mObserve(ResPath.Device.PowerSourceVoltage, observe=1, token=notif.token)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mContent.matching(req)(), self.serv.recv())

        # send the ACK
        self.serv.send(Lwm2mEmpty.matching(notif)())
