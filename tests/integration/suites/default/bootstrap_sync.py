# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.
import re

from framework.lwm2m_test import *


class ClientIgnoresNonBootstrapTrafficDuringBootstrap(test_suite.Lwm2mSingleServerTest):
    PSK_IDENTITY = b'test-identity'
    PSK_KEY = b'test-key'

    def setUp(self):
        self.setup_demo_with_servers(servers=[Lwm2mServer(coap.DtlsServer(psk_key=self.PSK_KEY, psk_identity=self.PSK_IDENTITY))],
                                     bootstrap_server=Lwm2mServer(coap.DtlsServer(psk_key=self.PSK_KEY, psk_identity=self.PSK_IDENTITY)),
                                     extra_cmdline_args=['--identity',
                                                         str(binascii.hexlify(self.PSK_IDENTITY), 'ascii'),
                                                         '--key', str(binascii.hexlify(self.PSK_KEY), 'ascii')],
                                     auto_register=False)

        self.serv.listen()
        self.bootstrap_server.listen()
        self.assertDemoRegisters(self.serv)

    def runTest(self):
        req = Lwm2mCreate('/%d' % (OID.Test,))
        self.serv.send(req)
        res = self.serv.recv()
        self.assertMsgEqual(Lwm2mCreated.matching(req)(), res)
        self.assertEqual('/%d/0' % (OID.Test,), res.get_location_path())

        # force an Update so that change to the data model does not get notified later
        self.communicate('send-update')
        self.assertDemoUpdatesRegistration(content=ANY)

        req = Lwm2mRead(ResPath.Test[0].Counter)
        self.serv.send(req)
        res = self.serv.recv()
        self.assertMsgEqual(Lwm2mContent.matching(req)(), res)
        self.assertEqual(b'0', res.content)

        # 1.0-style "spurious" Server Initiated Bootstrap
        req = Lwm2mWrite(ResPath.Test[0].Counter, b'42')
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.bootstrap_server.recv())

        # now regular server shall not be able to communicate with the client
        req = Lwm2mExecute(ResPath.Test[0].IncrementCounter)
        self.serv.send(req)
        with self.assertRaises(OSError):
            self.serv.recv(timeout_s=5)

        self.assertEqual(1, self.get_socket_count())

        # Bootstrap Finish
        self.bootstrap_server.send(Lwm2mBootstrapFinish())
        self.assertIsInstance(self.bootstrap_server.recv(), Lwm2mChanged)

        # client reconnects with DTLS session resumption
        self.assertDtlsReconnect()

        # now we shall be able to do that Execute
        req = Lwm2mExecute(ResPath.Test[0].IncrementCounter)
        self.serv.send(req)
        res = self.serv.recv()
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), res)

        # verify that Execute was performed just once
        req = Lwm2mRead(ResPath.Test[0].Counter)
        self.serv.send(req)
        res = self.serv.recv()
        self.assertMsgEqual(Lwm2mContent.matching(req)(), res)
        self.assertEqual(b'43', res.content)
