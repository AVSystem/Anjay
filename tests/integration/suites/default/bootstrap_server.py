# -*- coding: utf-8 -*-
#
# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

import socket

from framework_tools.lwm2m.tlv import TLV
from framework_tools.utils.lwm2m_test import *
from . import bootstrap_holdoff as bsh


class BootstrapServer:
    class Test(bsh.BootstrapHoldoff.Test):
        def setUp(self, **kwargs):
            super().setUp(holdoff_s=3, **kwargs)


class BootstrapServerTest(BootstrapServer.Test):
    def runTest(self):
        self.bootstrap_server.connect_to_client(('127.0.0.1', self.get_demo_port()))
        req = Lwm2mWrite('/%d/42' % (OID.Server,),
                         TLV.make_resource(RID.Server.Lifetime, 60).serialize()
                         + TLV.make_resource(RID.Server.Binding, "U").serialize()
                         + TLV.make_resource(RID.Server.ShortServerID, 42).serialize()
                         + TLV.make_resource(RID.Server.NotificationStoring, True).serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())

        regular_serv = Lwm2mServer()
        regular_serv_uri = 'coap://127.0.0.1:%d' % regular_serv.get_listen_port()

        # Create Security object
        req = Lwm2mWrite('/%d/42' % (OID.Security,),
                         TLV.make_resource(RID.Security.ServerURI, regular_serv_uri).serialize()
                         + TLV.make_resource(RID.Security.Bootstrap, 0).serialize()
                         + TLV.make_resource(RID.Security.Mode, 3).serialize()
                         + TLV.make_resource(RID.Security.ShortServerID, 42).serialize()
                         + TLV.make_resource(RID.Security.PKOrIdentity, "").serialize()
                         + TLV.make_resource(RID.Security.SecretKey, "").serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.bootstrap_server.send(req)

        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())

        # no Client Initiated bootstrap
        with self.assertRaises(socket.timeout):
            print(self.bootstrap_server.recv(timeout_s=4))

        # send Bootstrap Finish
        req = Lwm2mBootstrapFinish()
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())

        self.assertDemoRegisters(server=regular_serv, lifetime=60)

        # Bootstrap Delete / shall succeed
        req = Lwm2mDelete('/')
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mDeleted.matching(req)(),
                            self.bootstrap_server.recv())
        # ...even twice
        req = Lwm2mDelete('/')
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mDeleted.matching(req)(),
                            self.bootstrap_server.recv())
        # now send Bootstrap Finish
        req = Lwm2mBootstrapFinish()
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())

        # the client will now start Client Initiated bootstrap, because it has no regular server connection
        # this might happen after a backoff, if the Bootstrap Delete was handled before the response to Register
        self.assertDemoRequestsBootstrap(timeout_s=20)

        self.request_demo_shutdown()

        regular_serv.close()


class BootstrapEmptyResourcesDoesNotSegfault(BootstrapServer.Test):
    def runTest(self):
        self.bootstrap_server.connect_to_client(('127.0.0.1', self.get_demo_port()))

        req = Lwm2mWrite('/%d/42' % (OID.Security,),
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
                                self.bootstrap_server.recv())

        req = Lwm2mBootstrapFinish()
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())
        self.request_demo_shutdown()
