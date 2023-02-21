# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.
import base64
import datetime
import os
import socket
import subprocess
import threading
import time

import cryptography
import cryptography.hazmat
import cryptography.hazmat.backends
import cryptography.x509

from framework.lwm2m.coap.server import SecurityMode
from framework.lwm2m.coap.transport import Transport
from framework.lwm2m_test import *
from framework.test_utils import DEMO_ENDPOINT_NAME, RID, OID
from suites.default import bootstrap_client
from suites.default import register
from suites.default import retransmissions


class SecurityObjectDmOperationsBySingleServer(test_suite.Lwm2mSingleServerTest,
                                               test_suite.Lwm2mDmOperations):
    def runTest(self):
        # Every security instance action on nonexistent instance shall return NOT_AUTHORIZED to not disclose
        # any information.
        for i in range(3):
            self.read_instance(server=self.serv, oid=OID.Security, iid=i,
                               expect_error_code=coap.Code.RES_UNAUTHORIZED)
            self.delete_instance(server=self.serv, oid=OID.Security, iid=i,
                                 expect_error_code=coap.Code.RES_UNAUTHORIZED)
            self.write_instance(server=self.serv, oid=OID.Security, iid=i,
                                expect_error_code=coap.Code.RES_UNAUTHORIZED)
            self.execute_resource(server=self.serv, oid=OID.Security, iid=i,
                                  rid=RID.Security.Bootstrap,
                                  expect_error_code=coap.Code.RES_UNAUTHORIZED)
            self.write_attributes(server=self.serv, oid=OID.Security, iid=i,
                                  rid=RID.Security.Bootstrap, query=['pmax=1'],
                                  expect_error_code=coap.Code.RES_UNAUTHORIZED)


