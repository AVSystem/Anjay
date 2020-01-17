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

import re

from framework.lwm2m_test import *


def object_set_from_payload(payload):
    return set(int(re.match(b'^</(\d+)[/>]', elem).group(1)) for elem in payload.split(b','))

def unregister_test(oid):
    class UnregisterTest(test_suite.Lwm2mSingleServerTest):
        def setUp(self):
            super().setUp(auto_register=False)
            pkt = self.serv.recv()
            expected = Lwm2mRegister('/rd?lwm2m=1.0&ep=%s&lt=%d' % (DEMO_ENDPOINT_NAME, 86400))
            self.assertMsgEqual(expected, pkt)
            self.serv.send(
                Lwm2mCreated(location=self.DEFAULT_REGISTER_ENDPOINT, msg_id=pkt.msg_id, token=pkt.token))
            self.initial_objects = object_set_from_payload(pkt.content)

        def runTest(self):
            self.communicate('unregister-object %d' % oid)
            pkt = self.serv.recv()
            self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT), pkt)
            current_objects = object_set_from_payload(pkt.content)

            self.assertEqual(self.initial_objects - {oid}, current_objects)

            self.serv.send(Lwm2mChanged.matching(pkt)())

    return UnregisterTest


class UnregisterDevice(unregister_test(OID.Device)): pass


class UnregisterConnectivityMonitoring(unregister_test(OID.ConnectivityMonitoring)): pass


class UnregisterLocation(unregister_test(OID.Location)): pass


class UnregisterConnectivityStatistics(unregister_test(OID.ConnectivityStatistics)): pass


class UnregisterCellConnectivity(unregister_test(OID.CellularConnectivity)): pass


class UnregisterApnConnectionProfile(unregister_test(OID.ApnConnectionProfile)): pass


class UnregisterTest(unregister_test(OID.Test)): pass


class UnregisterExtDevInfo(unregister_test(OID.ExtDevInfo)): pass


class UnregisterIpPing(unregister_test(OID.IpPing)): pass


class UnregisterGeopoints(unregister_test(OID.GeoPoints)): pass


class UnregisterDownloadDiagnostics(unregister_test(OID.DownloadDiagnostics)): pass
