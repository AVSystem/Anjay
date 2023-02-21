# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

from framework.lwm2m.senml_cbor import *
from framework.test_utils import *
from framework.lwm2m_test import *


class ConnectToServerTest(test_suite.Lwm2mSingleServerTest, test_suite.Lwm2mDmOperations):
    def setUp(self):
        server = Lwm2mServer()
        config = [
            {SenmlLabel.NAME: '/0/1/0',  SenmlLabel.STRING: 'coap://127.0.0.1:%d' % (server.get_listen_port())},
            {SenmlLabel.NAME: '/0/1/1',  SenmlLabel.BOOL: False},
            {SenmlLabel.NAME: '/0/1/2',  SenmlLabel.VALUE: 3},
            {SenmlLabel.NAME: '/0/1/10', SenmlLabel.VALUE: 1},
            {SenmlLabel.NAME: '/1/1/0',  SenmlLabel.VALUE: 1},
            {SenmlLabel.NAME: '/1/1/1',  SenmlLabel.VALUE: 86400},
            {SenmlLabel.NAME: '/1/1/6',  SenmlLabel.BOOL: False},
            {SenmlLabel.NAME: '/1/1/7',  SenmlLabel.STRING: 'U'},
            {SenmlLabel.NAME: ResPath.Test[21].ResInt, SenmlLabel.VALUE: 64}
        ]

        with tempfile.NamedTemporaryFile() as f:
            f.write(CBOR.serialize(config))
            f.flush()
            super().setUp(servers=[server], num_servers_passed=0,
                          extra_cmdline_args=['--factory-provisioning-file', f.name])
        self.assertDemoRegisters()

    def runTest(self):
        response = self.read_path(self.serv, ResPath.Test[21].ResInt)
        self.assertEqual(response.get_content_format(), coap.ContentFormat.TEXT_PLAIN)
        self.assertEqual(response.content, b'64')

class WriteMultiInstanceResourceTest(test_suite.Lwm2mSingleServerTest, test_suite.Lwm2mDmOperations):
    def setUp(self):
        server = Lwm2mServer()
        config = [
            {SenmlLabel.NAME: '/0/1/0',  SenmlLabel.STRING: 'coap://127.0.0.1:%d' % (server.get_listen_port())},
            {SenmlLabel.NAME: '/0/1/1',  SenmlLabel.BOOL: False},
            {SenmlLabel.NAME: '/0/1/2',  SenmlLabel.VALUE: 3},
            {SenmlLabel.NAME: '/0/1/10', SenmlLabel.VALUE: 1},
            {SenmlLabel.NAME: '/1/1/0',  SenmlLabel.VALUE: 1},
            {SenmlLabel.NAME: '/1/1/1',  SenmlLabel.VALUE: 86400},
            {SenmlLabel.NAME: '/1/1/6',  SenmlLabel.BOOL: False},
            {SenmlLabel.NAME: ResPath.Test[1].IntArray + '/0', SenmlLabel.VALUE: 50},
            {SenmlLabel.NAME: '/1/1/7',  SenmlLabel.STRING: 'U'},
            {SenmlLabel.NAME: ResPath.Test[1].IntArray + '/1', SenmlLabel.VALUE: 99}
        ]

        with tempfile.NamedTemporaryFile() as f:
            f.write(CBOR.serialize(config))
            f.flush()
            super().setUp(servers=[server], num_servers_passed=0,
                          extra_cmdline_args=['--factory-provisioning-file', f.name])
        self.assertDemoRegisters()

    def runTest(self):
        response = self.read_path(self.serv, ResPath.Test[1].IntArray + '/0')
        self.assertEqual(response.get_content_format(), coap.ContentFormat.TEXT_PLAIN)
        self.assertEqual(response.content, b'50')
