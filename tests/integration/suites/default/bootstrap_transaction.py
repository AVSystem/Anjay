# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

from framework.lwm2m_test import *
from suites.default.bootstrap_client import BootstrapTest


class BootstrapTransactionTest(test_suite.Lwm2mTest):
    def setUp(self):
        self.setup_demo_with_servers(servers=1,
                                     num_servers_passed=0,
                                     bootstrap_server=True,
                                     extra_cmdline_args=['--bootstrap-timeout', '-1'])

    def tearDown(self):
        self.teardown_demo_with_servers(auto_deregister=False)

    def runTest(self):
        self.assertDemoRequestsBootstrap()

        # Create Server object
        req = Lwm2mWrite('/%d/1' % (OID.Server,),
                         TLV.make_resource(RID.Server.Lifetime, 60).serialize()
                         + TLV.make_resource(RID.Server.ShortServerID, 1).serialize()
                         + TLV.make_resource(RID.Server.NotificationStoring, True).serialize()
                         + TLV.make_resource(RID.Server.Binding, "U").serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())

        # Create Security object
        regular_serv_uri = 'coap://127.0.0.1:%d' % self.servers[0].get_listen_port()

        req = Lwm2mWrite('/%d/2' % (OID.Security,),
                         TLV.make_resource(RID.Security.ServerURI, regular_serv_uri).serialize()
                         + TLV.make_resource(RID.Security.Bootstrap, 0).serialize()
                         + TLV.make_resource(RID.Security.Mode, 3).serialize()
                         + TLV.make_resource(RID.Security.ShortServerID, 1).serialize()
                         + TLV.make_resource(RID.Security.PKOrIdentity, "").serialize()
                         + TLV.make_resource(RID.Security.SecretKey, "").serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())

        # create incomplete Geo-Points object
        req = Lwm2mWrite('/%d/42' % (OID.GeoPoints,),
                         TLV.make_resource(RID.GeoPoints.Latitude, 42.0).serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())

        # send Bootstrap Finish
        req = Lwm2mBootstrapFinish()
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_NOT_ACCEPTABLE),
                            self.bootstrap_server.recv())

        # still bootstrapping, so nothing shall be sent to the regular server
        with self.assertRaises(socket.timeout):
            print(self.servers[0].recv(timeout_s=5))

        # check that still bootstrapping indeed
        req = Lwm2mWrite('/%d/42' % (OID.GeoPoints,),
                         TLV.make_resource(RID.GeoPoints.Longitude, 69.0).serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())


class BootstrapTransactionPersistenceTest(test_suite.Lwm2mTest, test_suite.Lwm2mDmOperations):
    def setUp(self):
        self._dm_persistence_file = tempfile.NamedTemporaryFile()
        self.setup_demo_with_servers(
            servers=0, bootstrap_server=True,
            extra_cmdline_args=['--bootstrap-timeout', '-1',
                                '--dm-persistence-file', self._dm_persistence_file.name])

    def tearDown(self):
        try:
            self.teardown_demo_with_servers(auto_deregister=False)
        finally:
            self._dm_persistence_file.close()

    def runTest(self):
        self.assertDemoRequestsBootstrap()

        # Create Server object without Binding
        req = Lwm2mWrite('/%d/1' % (OID.Server,),
                         TLV.make_resource(RID.Server.Lifetime, 60).serialize()
                         + TLV.make_resource(RID.Server.ShortServerID, 1).serialize()
                         + TLV.make_resource(RID.Server.NotificationStoring, True).serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())

        # Create Security object without URI
        req = Lwm2mWrite('/%d/2' % (OID.Security,),
                         TLV.make_resource(RID.Security.Bootstrap, 0).serialize()
                         + TLV.make_resource(RID.Security.Mode, 3).serialize()
                         + TLV.make_resource(RID.Security.ShortServerID, 1).serialize()
                         + TLV.make_resource(RID.Security.PKOrIdentity, "").serialize()
                         + TLV.make_resource(RID.Security.SecretKey, "").serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())

        # send Bootstrap Finish
        req = Lwm2mBootstrapFinish()
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_NOT_ACCEPTABLE),
                            self.bootstrap_server.recv())

        self.request_demo_shutdown()
        self._terminate_demo()

        self.bootstrap_server.reset()

        self._start_demo(['--dm-persistence-file', self._dm_persistence_file.name]
                         + self.make_demo_args(DEMO_ENDPOINT_NAME, [],
                                               '1.0', '1.0',
                                               None))

        # Demo shall launch, with the initial server configuration
        self.assertDemoRequestsBootstrap()

        # The previously created instances shall not be present
        discover_result = self.discover(self.bootstrap_server).content.decode()
        self.assertNotIn('</%d/1' % (OID.Server,), discover_result)
        self.assertNotIn('</%d/2' % (OID.Security,), discover_result)


class UnregisteringAndRegisteringObjectsDuringBootstrapTransaction(BootstrapTest.Test):
    def setUp(self):
        super().setUp(num_servers_passed=1)

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        self.execute_resource(self.serv, OID.Server, 2, RID.Server.RequestBootstrapTrigger)

        self.assertDemoRequestsBootstrap()
        self.write_instance(self.bootstrap_server, oid=OID.Test, iid=42, content=b'')
        self.communicate('unregister-object %d' % OID.Test, timeout=5)
        self.communicate('reregister-object %d' % OID.Test, timeout=5)


class NotificationDuringBootstrap(BootstrapTest.Test, test_suite.Lwm2mDtlsSingleServerTest):
    def setUp(self):
        super().setUp(num_servers_passed=1)

    def runTest(self):
        self.observe(self.serv, OID.Device, 0, RID.Device.CurrentTime)

        self.execute_resource(self.serv, OID.Server, 2, RID.Server.RequestBootstrapTrigger)
        self.assertDemoRequestsBootstrap()
        self.serv.reset()

        with self.assertRaises(socket.timeout):
            print(self.serv.recv(timeout_s=5))

        self.serv.reset()

        self.perform_bootstrap_finish()

        # demo will resume DTLS session without sending any LwM2M messages
        self.serv.listen()

        notifications = 0
        while True:
            try:
                self.assertIsInstance(self.serv.recv(timeout_s=0.8), Lwm2mNotify)
                notifications += 1
            except socket.timeout:
                break

        self.assertTrue(4 <= notifications <= 6)


class NotificationDuringBootstrapInQueueMode(BootstrapTest.Test,
                                             test_suite.Lwm2mDtlsSingleServerTest):
    def setUp(self):
        super().setUp(num_servers_passed=1,
                      extra_cmdline_args=['--binding=UQ'],
                      auto_register=False)
        # demo will perform all DTLS handshakes before sending Register
        self.serv.listen()
        self.bootstrap_server.listen()
        self.assertDemoRegisters(binding='UQ')

    def runTest(self):
        self.observe(self.serv, OID.Device, 0, RID.Device.CurrentTime)

        self.execute_resource(self.serv, OID.Server, 2, RID.Server.RequestBootstrapTrigger)
        self.assertDemoRequestsBootstrap()
        self.serv.reset()

        with self.assertRaises(socket.timeout):
            print(self.serv.recv(timeout_s=5))

        self.serv.reset()

        self.perform_bootstrap_finish()

        # demo will resume DTLS session without sending any LwM2M messages
        self.serv.listen()

        notifications = 0
        while True:
            try:
                self.assertIsInstance(self.serv.recv(timeout_s=0.8), Lwm2mNotify)
                notifications += 1
            except socket.timeout:
                break

        self.assertTrue(4 <= notifications <= 6)


class ChangeServersDuringBootstrap(BootstrapTest.Test):
    def setUp(self):
        super().setUp(servers=2, num_servers_passed=2)

    def tearDown(self):
        super().tearDown(deregister_servers=[self.servers[0]])

    def runTest(self):
        self.servers[0].reset()
        self.servers[1].reset()
        self.bootstrap_server.connect_to_client(('127.0.0.1', self.get_demo_port()))
        for i in (0, 1):
            iid = i + 2
            self.write_instance(self.bootstrap_server, OID.AccessControl, i + 1000,
                                TLV.make_resource(RID.AccessControl.TargetOID,
                                                  OID.Server).serialize() +
                                TLV.make_resource(RID.AccessControl.TargetIID, iid).serialize() +
                                TLV.make_resource(RID.AccessControl.Owner, iid).serialize())
        self.perform_bootstrap_finish()

        self.assertDemoRegisters(self.servers[0])
        self.assertDemoRegisters(self.servers[1])
        self.coap_ping(self.servers[0])
        self.coap_ping(self.servers[1])

        self.execute_resource(self.servers[0], OID.Server, 2, RID.Server.RequestBootstrapTrigger)
        self.assertDemoRequestsBootstrap()
        self.servers[0].reset()
        self.servers[1].reset()

        self.communicate('trim-servers 2', timeout=5)

        with self.assertRaises(socket.timeout):
            print(self.servers[0].recv(timeout_s=5))
        with self.assertRaises(socket.timeout):
            print(self.servers[1].recv(timeout_s=5))

        self.perform_bootstrap_finish()
        self.assertDemoRegisters(self.servers[0])


class DisableServerDuringBootstrap(BootstrapTest.Test):
    def setUp(self):
        super().setUp(num_servers_passed=1)

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        self.execute_resource(self.serv, OID.Server, 2, RID.Server.RequestBootstrapTrigger)
        self.serv.reset()
        self.assertDemoRequestsBootstrap()

        self.communicate('disable-server 2 3', timeout=5)

        with self.assertRaises(socket.timeout):
            print(self.serv.recv(timeout_s=5))


class EnableServerDuringBootstrap(BootstrapTest.Test):
    def setUp(self):
        super().setUp(num_servers_passed=1)

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        self.execute_resource(self.serv, OID.Server, 2, RID.Server.RequestBootstrapTrigger)
        self.serv.reset()
        self.assertDemoRequestsBootstrap()

        self.communicate('disable-server 2 -1', timeout=5)
        self.communicate('enable-server 2', timeout=5)

        with self.assertRaises(socket.timeout):
            print(self.serv.recv(timeout_s=5))


class ExitOfflineDuringBootstrap(BootstrapTest.Test):
    def setUp(self):
        super().setUp(num_servers_passed=1)

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        self.execute_resource(self.serv, OID.Server, 2, RID.Server.RequestBootstrapTrigger)
        self.serv.reset()
        self.assertDemoRequestsBootstrap()

        self.communicate('exit-offline', timeout=5)

        with self.assertRaises(socket.timeout):
            print(self.serv.recv(timeout_s=5))


class EnterAndExitOfflineDuringBootstrap(BootstrapTest.Test):
    def setUp(self):
        super().setUp(num_servers_passed=1)

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        self.execute_resource(self.serv, OID.Server, 2, RID.Server.RequestBootstrapTrigger)
        self.serv.reset()
        self.assertDemoRequestsBootstrap()

        self.communicate('enter-offline', timeout=5)
        with self.assertRaises(socket.timeout):
            print(self.serv.recv(timeout_s=2))
        self.communicate('exit-offline', timeout=5)

        self.assertDemoRequestsBootstrap()

        with self.assertRaises(socket.timeout):
            print(self.serv.recv(timeout_s=5))


class ReconnectDuringBootstrap(BootstrapTest.Test):
    def setUp(self):
        super().setUp(num_servers_passed=1)

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        self.execute_resource(self.serv, OID.Server, 2, RID.Server.RequestBootstrapTrigger)
        self.serv.reset()
        self.assertDemoRequestsBootstrap()

        self.communicate('reconnect', timeout=5)
        self.assertDemoRequestsBootstrap()

        with self.assertRaises(socket.timeout):
            print(self.serv.recv(timeout_s=5))
