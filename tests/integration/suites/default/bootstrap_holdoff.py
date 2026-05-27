# -*- coding: utf-8 -*-
#
# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

from framework_tools.lwm2m.tlv import TLV
from framework.lwm2m_test import *


class BootstrapHoldoff:
    class Test(test_suite.Lwm2mTest):
        # Write for just a Security object but without Server Object
        BS_WRITE = Lwm2mWrite('/%d/42' % (OID.Security,),
                              TLV.make_resource(
            RID.Security.ServerURI, 'coap://1.2.3.4:5678').serialize()
            + TLV.make_resource(RID.Security.Bootstrap, 0).serialize()
            + TLV.make_resource(RID.Security.Mode, 3).serialize()
            + TLV.make_resource(RID.Security.ShortServerID, 42).serialize()
            + TLV.make_resource(RID.Security.PKOrIdentity, b'').serialize()
            + TLV.make_resource(RID.Security.SecretKey, b'').serialize(),
            format=coap.ContentFormat.APPLICATION_LWM2M_TLV)

        BS_SERVER_WRITE = Lwm2mWrite('/%d/42' % (OID.Server),
                    TLV.make_resource(
            RID.Server.Lifetime, 60).serialize()
            + TLV.make_resource(RID.Server.ShortServerID,
                                42).serialize()
            + TLV.make_resource(RID.Server.NotificationStoring,
                                True).serialize()
            + TLV.make_resource(RID.Server.Binding, 'U').serialize(),
            format=coap.ContentFormat.APPLICATION_LWM2M_TLV)

        def setUp(self, holdoff_s=None, **kwargs):
            extra_args = []
            if holdoff_s is not None:
                extra_args += ['--bootstrap-holdoff', str(holdoff_s)]
            self._last_bs_req_time = None
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

        def receive_bs_req(self, timeout):
            port = self.get_demo_port()
            self.bootstrap_server.connect_to_client(('127.0.0.1', port))

            elapsed_since_last_bs_req = 0
            if self._last_bs_req_time is not None:
                elapsed_since_last_bs_req = (time.monotonic()
                                             - self._last_bs_req_time)
            remaining_holdoff = max(0, timeout - elapsed_since_last_bs_req)

            # wait for holdoff to pass before client sends BS request
            quiet_period = max(0, remaining_holdoff - 2)
            if quiet_period > 0:
                with self.assertRaises(socket.timeout):
                    self.bootstrap_server.recv(timeout_s=quiet_period)

            # now it's time to receive BS request. There is +-2s of leeway
            bs_req = self.bootstrap_server.recv(timeout_s=4)

            self.assertMsgEqual(Lwm2mRequestBootstrap(endpoint_name=DEMO_ENDPOINT_NAME),
                                bs_req)
            self._last_bs_req_time = time.monotonic()
            self.bootstrap_server.send(Lwm2mChanged.matching(bs_req)())

        def send_bs_finish(self, err_code=None):
            req = Lwm2mBootstrapFinish()
            self.bootstrap_server.send(req)
            if not err_code:
                self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                                self.bootstrap_server.recv())
            else:
                self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(code=err_code),
                                self.bootstrap_server.recv())
        
        def verify_bootstrap_schedule_time(self, holdoff, jitter_random_factor):
            match = self.read_log_until_match(rb'Scheduling bootstrap in (\d+\.\d+)', timeout_s=1.0)
            if match is None:
                raise self.failureException(
                    f'Bootstrap not scheduled with holdoff {holdoff}-{holdoff * jitter_random_factor} sec')
            scheduled_time = float(match.group(1))

            self.assertGreaterEqual(scheduled_time, holdoff)
            self.assertLessEqual(scheduled_time, holdoff * jitter_random_factor)

            self.receive_bs_req(scheduled_time)


class BootstrapInitialHoldOffTimeToBig(BootstrapHoldoff.Test):
    def setUp(self):
        super().setUp(holdoff_s=30)

    def runTest(self):
        if self.read_log_until_match(b'Holdoff time is bigger then max allowed value, setting to 20.000000000 seconds', timeout_s=1.0) is None:
            raise self.failureException(
                'Holdoff time not limited to max value')

        self.receive_bs_req(20)

        self.bootstrap_server.send(self.BS_WRITE)
        self.assertMsgEqual(Lwm2mChanged.matching(self.BS_WRITE)(),
                            self.bootstrap_server.recv())

        self.send_bs_finish()

        self.request_demo_shutdown()


class BootstrapIncrementHoldOffTime(BootstrapHoldoff.Test):
    def setUp(self):
        super().setUp(holdoff_s=3)

    def runTest(self):
        holdoff = 3
        holdoff_cap = 20
        jitter_random_factor = 1.5
        for _ in range(4):
            self.verify_bootstrap_schedule_time(holdoff, jitter_random_factor)

            # send BS data and expect positive response
            self.bootstrap_server.send(self.BS_WRITE)
            self.assertMsgEqual(Lwm2mChanged.matching(self.BS_WRITE)(),
                                self.bootstrap_server.recv())

            self.send_bs_finish()

            holdoff = holdoff * 2
            if (holdoff > holdoff_cap):
                holdoff = holdoff_cap

        self.request_demo_shutdown()


class BootstrapDNSFailIncrementsTimer(BootstrapHoldoff.Test):
    def setUp(self):
        super().setUp(holdoff_s=3)

    def runTest(self):
        holdoff = 3
        holdoff_cap = 20
        jitter_random_factor = 1.5
        for _ in range(2):
            self.verify_bootstrap_schedule_time(holdoff, jitter_random_factor)

            BS_WRITE_WRONG_ADDRESS = Lwm2mWrite('/%d/42' % (OID.Security,),
                                TLV.make_resource(
                RID.Security.ServerURI, 'coap://does not exist:5678').serialize()
                + TLV.make_resource(RID.Security.Bootstrap, 0).serialize()
                + TLV.make_resource(RID.Security.Mode, 3).serialize()
                + TLV.make_resource(RID.Security.ShortServerID, 42).serialize()
                + TLV.make_resource(RID.Security.PKOrIdentity, b'').serialize()
                + TLV.make_resource(RID.Security.SecretKey, b'').serialize(),
                format=coap.ContentFormat.APPLICATION_LWM2M_TLV)

            # send BS data and expect positive response
            self.bootstrap_server.send(BS_WRITE_WRONG_ADDRESS)
            self.assertMsgEqual(Lwm2mChanged.matching(BS_WRITE_WRONG_ADDRESS)(),
                                self.bootstrap_server.recv())
            self.bootstrap_server.send(self.BS_SERVER_WRITE)
            self.assertMsgEqual(Lwm2mChanged.matching(self.BS_SERVER_WRITE)(),
                                self.bootstrap_server.recv())

            self.send_bs_finish()

            holdoff = holdoff * 2
            if (holdoff > holdoff_cap):
                holdoff = holdoff_cap

            # DNS lookup fail sometimes takes a while, depending on the platform.
            # We wait until Anjay stops retrying to resolve it before we go to the next iteration
            if self.read_log_until_match(b'cannot establish connection to \\[does not exist\\]', timeout_s=10.0) is None:
                raise self.failureException(
                    f'Waiting for DNS lookup fail timed out')

        self.request_demo_shutdown()

class BootstrapSSIDMismatchIncrementsTimer(BootstrapHoldoff.Test):
    def setUp(self):
        super().setUp(holdoff_s=3)

    def runTest(self):
        holdoff = 3
        holdoff_cap = 20
        jitter_random_factor = 1.5
        for _ in range(2):
            self.verify_bootstrap_schedule_time(holdoff, jitter_random_factor)

            # send BS data and expect positive response
            self.bootstrap_server.send(self.BS_WRITE)
            self.assertMsgEqual(Lwm2mChanged.matching(self.BS_WRITE)(),
                                self.bootstrap_server.recv())
            
            BS_SERVER_WRITE_WRONGSSID = Lwm2mWrite('/%d/42' % (OID.Server),
                    TLV.make_resource(
                    RID.Server.Lifetime, 60).serialize()
                    + TLV.make_resource(RID.Server.ShortServerID,
                                        41).serialize()
                    + TLV.make_resource(RID.Server.NotificationStoring,
                                        True).serialize()
                    + TLV.make_resource(RID.Server.Binding, 'UQ').serialize(),
                    format=coap.ContentFormat.APPLICATION_LWM2M_TLV)

            self.bootstrap_server.send(BS_SERVER_WRITE_WRONGSSID)
            self.assertMsgEqual(Lwm2mChanged.matching(BS_SERVER_WRITE_WRONGSSID)(),
                                self.bootstrap_server.recv())

            self.send_bs_finish()

            holdoff = holdoff * 2
            if (holdoff > holdoff_cap):
                holdoff = holdoff_cap

        self.request_demo_shutdown()

class BootstrapWrongURISchemeIsRejected(BootstrapHoldoff.Test):
    def setUp(self):
        super().setUp(holdoff_s=2)

    def runTest(self):
        self.receive_bs_req(2)

        # send BS data
        BS_WRITE_WRONG_SCHEME = Lwm2mWrite('/%d/42' % (OID.Security,),
                            TLV.make_resource(
            RID.Security.ServerURI, 'coaps://does-not-exist:5684').serialize()
            + TLV.make_resource(RID.Security.Bootstrap, 0).serialize()
            + TLV.make_resource(RID.Security.Mode, 3).serialize()
            + TLV.make_resource(RID.Security.ShortServerID, 42).serialize()
            + TLV.make_resource(RID.Security.PKOrIdentity, b'').serialize()
            + TLV.make_resource(RID.Security.SecretKey, b'').serialize(),
            format=coap.ContentFormat.APPLICATION_LWM2M_TLV)

        self.bootstrap_server.send(BS_WRITE_WRONG_SCHEME)
        self.assertMsgEqual(Lwm2mChanged.matching(BS_WRITE_WRONG_SCHEME)(), self.bootstrap_server.recv())

        self.bootstrap_server.send(self.BS_SERVER_WRITE)
        self.assertMsgEqual(Lwm2mChanged.matching(self.BS_SERVER_WRITE)(),
                                self.bootstrap_server.recv())

        self.send_bs_finish(coap.Code.RES_NOT_ACCEPTABLE)

        self.request_demo_shutdown()
