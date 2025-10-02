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


class BootstrapHoldoff:
    class Test(test_suite.Lwm2mTest):
        def setUp(self, holdoff_s=None, **kwargs):
            extra_args = []
            if holdoff_s is not None:
                extra_args += ['--bootstrap-holdoff', str(holdoff_s)]
            self.setup_demo_with_servers(servers=0,
                                         bootstrap_server=True,
                                         extra_cmdline_args=extra_args,
                                         **kwargs)

        def get_demo_port(self, server_index=None):
            # wait for sockets initialization
            # scheduler-based socket initialization might delay socket setup a bit;
            # this loop is here to ensure `communicate()` call below works as
            # expected
            for _ in range(10):
                if self.get_socket_count() > 0:
                    break
            else:
                self.fail("sockets not initialized in time")

            return super().get_demo_port(server_index)


class BootstrapInitialHoldOffTimeToBig(BootstrapHoldoff.Test):
    def setUp(self):
        super().setUp(holdoff_s=30)

    def runTest(self):
        if self.read_log_until_match(b'Holdoff time is bigger then max allowed value, setting to 20.000000000 seconds', timeout_s=1.0) is None:
            raise self.failureException(
                'Holdoff time not limited to max value')

        # Write for just a Security object but without Server Object
        req = Lwm2mWrite('/%d/42' % (OID.Security,),
                         TLV.make_resource(
                             RID.Security.ServerURI, 'coap://1.2.3.4:5678').serialize()
                         + TLV.make_resource(RID.Security.Bootstrap, 0).serialize()
                         + TLV.make_resource(RID.Security.Mode, 3).serialize()
                         + TLV.make_resource(RID.Security.ShortServerID, 42).serialize()
                         + TLV.make_resource(RID.Security.PKOrIdentity, b'').serialize()
                         + TLV.make_resource(RID.Security.SecretKey, b'').serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV)

        port = self.get_demo_port()
        self.bootstrap_server.connect_to_client(('127.0.0.1', port))
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())

        req = Lwm2mBootstrapFinish()
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())

        self.request_demo_shutdown()


class BootstrapIncrementHoldOffTime(BootstrapHoldoff.Test):
    def setUp(self):
        super().setUp(holdoff_s=3)

    def runTest(self):
        # Write for just a Security object but without Server Object
        req = Lwm2mWrite('/%d/42' % (OID.Security,),
                         TLV.make_resource(
                             RID.Security.ServerURI, 'coap://1.2.3.4:5678').serialize()
                         + TLV.make_resource(RID.Security.Bootstrap, 0).serialize()
                         + TLV.make_resource(RID.Security.Mode, 3).serialize()
                         + TLV.make_resource(RID.Security.ShortServerID, 42).serialize()
                         + TLV.make_resource(RID.Security.PKOrIdentity, b'').serialize()
                         + TLV.make_resource(RID.Security.SecretKey, b'').serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV)

        holdoff = 3
        holdoff_cap = 20
        for _ in range(4):
            port = self.get_demo_port()
            self.bootstrap_server.connect_to_client(('127.0.0.1', port))
            self.bootstrap_server.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                                self.bootstrap_server.recv())

            req = Lwm2mBootstrapFinish()
            self.bootstrap_server.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                                self.bootstrap_server.recv())

            holdoff = holdoff * 2
            if (holdoff > holdoff_cap):
                holdoff = holdoff_cap

            if self.read_log_until_match((f'Scheduling bootstrap in {holdoff}.000000000').encode('utf-8'), timeout_s=1.0) is None:
                raise self.failureException(
                    f'Bootstrap not scheduled with holdoff {holdoff} sec')

        self.request_demo_shutdown()
