import socket

from framework.lwm2m_test import *
from framework.lwm2m.tlv import TLV

class BootstrapServer:
    class Test(test_suite.Lwm2mTest):
        def setUp(self):
            extra_args = ['--bootstrap-holdoff', '3']
            self.setup_demo_with_servers(num_servers=0,
                                         bootstrap_server=True,
                                         extra_cmdline_args=extra_args)
        def tearDown(self):
            self.teardown_demo_with_servers()

        def get_demo_port(self):
            # wait for sockets initialization
            # scheduler-based socket initialization might delay socket setup a bit;
            # this loop is here to ensure `communicate()` call below works as
            # expected
            for _ in range(10):
                if int(self.communicate('socket-count', match_regex='SOCKET_COUNT==([0-9]+)\n').group(1)) > 0:
                    break
            else:
                self.fail("sockets not initialized in time");

            # send Bootstrap messages without request
            return int(self.communicate('get-port -1', match_regex='PORT==([0-9]+)\n').group(1))

class BootstrapServerTest(BootstrapServer.Test):
    def runTest(self):
        self.bootstrap_server.connect(('127.0.0.1', self.get_demo_port()))
        req = Lwm2mWrite('/1/42',
                         TLV.make_resource(RID.Server.Lifetime, 60).serialize()
                         + TLV.make_resource(RID.Server.Binding, "U").serialize()
                         + TLV.make_resource(RID.Server.ShortServerID, 42).serialize()
                         + TLV.make_resource(RID.Server.NotificationStoring, True).serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv(timeout_s=1))

        regular_serv = Lwm2mServer()
        regular_serv_uri = 'coap://127.0.0.1:%d' % regular_serv.get_listen_port()

        # Create Security object
        req = Lwm2mWrite('/0/42',
                         TLV.make_resource(RID.Security.ServerURI, regular_serv_uri).serialize()
                         + TLV.make_resource(RID.Security.Bootstrap, 0).serialize()
                         + TLV.make_resource(RID.Security.Mode, 3).serialize()
                         + TLV.make_resource(RID.Security.ShortServerID, 42).serialize()
                         + TLV.make_resource(RID.Security.PKOrIdentity, "").serialize()
                         + TLV.make_resource(RID.Security.SecretKey, "").serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.bootstrap_server.send(req)

        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv(timeout_s=1))

        # no Client Initiated bootstrap
        with self.assertRaises(socket.timeout):
            print(self.bootstrap_server.recv(timeout_s=4))

        # send Bootstrap Finish
        req = Lwm2mBootstrapFinish()
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv(timeout_s=1))

        self.assertDemoRegisters(server=regular_serv, lifetime=60)

        # Bootstrap Delete shall succeed
        for obj in (OID.Security, OID.Server, OID.AccessControl):
            req = Lwm2mDelete('/%d' % obj)
            self.bootstrap_server.send(req)
            self.assertMsgEqual(Lwm2mDeleted.matching(req)(),
                                self.bootstrap_server.recv(timeout_s=1))
        # now send Bootstrap Finish
        req = Lwm2mBootstrapFinish()
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv(timeout_s=1))
        self.assertDemoDeregisters(server=regular_serv)

        self.request_demo_shutdown()

        regular_serv.close()

class BootstrapEmptyResourcesDoesNotSegfault(BootstrapServer.Test):
    def runTest(self):
        self.bootstrap_server.connect(('127.0.0.1', self.get_demo_port()))

        req = Lwm2mWrite('/0/42',
                TLV.make_resource(RID.Security.ServerURI, 'coap://1.2.3.4:5678').serialize()
                         + TLV.make_resource(RID.Security.Bootstrap, 0).serialize()
                         + TLV.make_resource(RID.Security.Mode, 3).serialize()
                         + TLV.make_resource(RID.Security.ShortServerID, 42).serialize()
                         + TLV.make_resource(RID.Security.PKOrIdentity, b'').serialize()
                         + TLV.make_resource(RID.Security.SecretKey, b'').serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV)

        for _ in range(64):
            self.bootstrap_server.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                                self.bootstrap_server.recv(timeout_s=1))

        req = Lwm2mBootstrapFinish()
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv(timeout_s=1))
        self.request_demo_shutdown()


