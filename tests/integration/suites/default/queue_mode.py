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

import framework.test_suite
from framework.lwm2m_test import *
from . import access_control, retransmissions


class QueueModeBehaviour(retransmissions.RetransmissionTest.TestMixin,
                         access_control.AccessControl.Test):
    PSK_IDENTITY = b'test-identity'
    PSK_KEY = b'test-key'

    def setUp(self):
        super().setUp(
            servers=[
                Lwm2mServer(coap.DtlsServer(psk_key=self.PSK_KEY, psk_identity=self.PSK_IDENTITY)),
                Lwm2mServer(coap.DtlsServer(psk_key=self.PSK_KEY, psk_identity=self.PSK_IDENTITY))],
            extra_cmdline_args=['--identity',
                                str(binascii.hexlify(self.PSK_IDENTITY), 'ascii'),
                                '--key', str(binascii.hexlify(self.PSK_KEY), 'ascii')])

    def tearDown(self):
        self.teardown_demo_with_servers(deregister_servers=[self.servers[1]])

    def runTest(self):
        # create the test object and give read access to servers[0]
        self.create_instance(server=self.servers[1], oid=OID.Test)
        self.update_access(server=self.servers[1], oid=OID.Test, iid=0,
                           acl=[access_control.make_acl_entry(1, access_control.AccessMask.READ),
                                access_control.make_acl_entry(2, access_control.AccessMask.OWNER)])

        # first check if sockets stay online in non-queue mode
        time.sleep(self.max_transmit_wait() - 2)
        self.assertEqual(self.get_socket_count(), 2)
        time.sleep(4)
        self.assertEqual(self.get_socket_count(), 2)

        # put servers[0] into queue mode
        self.write_resource(self.servers[0], OID.Server, 1, RID.Server.Binding, b'UQ')
        self.assertDemoUpdatesRegistration(self.servers[0], binding='UQ', content=ANY)

        # Observe the Counter argument
        self.observe(self.servers[0], OID.Test, 0, RID.Test.Counter)

        time.sleep(self.max_transmit_wait() - 2)
        self.assertEqual(self.get_socket_count(), 2)
        time.sleep(4)
        self.assertEqual(self.get_socket_count(), 1)

        # Trigger Notification from the non-queue server
        self.execute_resource(self.servers[1], OID.Test, 0, RID.Test.IncrementCounter)

        self.assertDtlsReconnect(self.servers[0])
        pkt = self.servers[0].recv()
        self.assertIsInstance(pkt, Lwm2mNotify)
        self.assertEqual(self.get_socket_count(), 2)

        # "queued RPCs"
        self.read_resource(self.servers[0], OID.Test, 0, RID.Test.Timestamp)
        # cancel observation
        self.observe(self.servers[0], OID.Test, 0, RID.Test.Counter, observe=1)

        # assert queue mode operation again
        time.sleep(12)
        self.assertEqual(self.get_socket_count(), 2)
        time.sleep(4)
        self.assertEqual(self.get_socket_count(), 1)


