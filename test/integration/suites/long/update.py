# -*- coding: utf-8 -*-
#
# Copyright 2017-2018 AVSystem <avsystem@avsystem.com>
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


class UpdateBackoffAfterCoapServerErrorResponse(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        # make sure 5.xx triggers Update retransmission
        self.communicate('send-update')

        expected_backoffs = [
            None,  # ignore initial backoff factor
            (2, 0.2),  # 2s (exponential backoff factor, allowed error)
            (2, 0.2),  # 4s
            (2, 0.2),  # 8s
            (2, 0.2),  # 16s
            (2, 0.2),  # 32s
            (2, 0.2),  # 64s
        ]

        def next_timeout_s(timestamps):
            if len(timestamps) < 2:
                return 5;
            elif expected_backoffs[len(timestamps) - 2] is None:
                return 5;
            else:
                return 2.5 * (timestamps[-1] - timestamps[-2])

        # should apply backoff before retrying after failure responses
        timestamps = [time.time()]
        for _ in range(len(expected_backoffs)):
            self.serv.reset()
            pkt = self.serv.recv(timeout_s=next_timeout_s(timestamps))
            timestamps.append(time.time())

            self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT,
                                            content=b''),
                                pkt)
            self.serv.send(Lwm2mErrorResponse.matching(pkt)(coap.Code.RES_INTERNAL_SERVER_ERROR))

        # should retry again after backoff
        self.serv.reset()
        pkt = self.serv.recv(timeout_s=next_timeout_s(timestamps))
        timestamps.append(time.time())

        self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT,
                                        content=b''),
                            pkt)
        self.serv.send(Lwm2mChanged.matching(pkt)(location=self.DEFAULT_REGISTER_ENDPOINT))

        # check backoff values
        timeouts_s = [next - prev for prev, next in zip(timestamps[:-1], timestamps[1:])]
        backoffs = [next / prev for prev, next in zip(timeouts_s[:-1], timeouts_s[1:])]

        for timeout_s, backoff, actual in zip(timeouts_s, expected_backoffs, backoffs):
            # backoffs for short timeout values fluctuate quite a bit, ignore them
            if backoff is not None and timeout_s > 3:
                expected, delta = backoff
                self.assertAlmostEqual(expected, actual, delta=delta,
                                       msg='\ntimeouts_s: %s\nbackoffs: %s' % (timeouts_s, backoffs))
