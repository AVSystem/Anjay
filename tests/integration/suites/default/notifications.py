# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.
from framework.lwm2m_test import *


class CancellingConfirmableNotifications(test_suite.Lwm2mSingleServerTest,
                                         test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(extra_cmdline_args=['--confirmable-notifications'])

    def runTest(self):
        self.create_instance(self.serv, oid=OID.Test, iid=0)

        observe1 = self.observe(self.serv, oid=OID.Test, iid=0, rid=RID.Test.Counter)
        observe2 = self.observe(self.serv, oid=OID.Test, iid=0, rid=RID.Test.Counter)

        self.execute_resource(self.serv, oid=OID.Test, iid=0, rid=RID.Test.IncrementCounter)
        notify1 = self.serv.recv()
        if bytes(notify1.token) != bytes(observe1.token):
            # which observation is handled first isn't exactly deterministic
            tmp = observe1
            observe1 = observe2
            observe2 = tmp

        self.assertIsInstance(notify1, Lwm2mNotify)
        self.assertEqual(bytes(notify1.token), bytes(observe1.token))
        self.serv.send(Lwm2mEmpty.matching(notify1)())

        notify2 = self.serv.recv()
        self.assertIsInstance(notify2, Lwm2mNotify)
        self.assertEqual(bytes(notify2.token), bytes(observe2.token))
        self.serv.send(Lwm2mEmpty.matching(notify2)())

        self.assertEqual(notify1.content, notify2.content)

        # Cancel the next notification
        self.execute_resource(self.serv, oid=OID.Test, iid=0, rid=RID.Test.IncrementCounter)
        notify1 = self.serv.recv()
        self.assertIsInstance(notify1, Lwm2mNotify)
        self.assertEqual(bytes(notify1.token), bytes(observe1.token))
        self.assertNotEqual(notify1.content, notify2.content)
        self.serv.send(Lwm2mReset.matching(notify1)())

        # the second observation should still produce notifications
        notify2 = self.serv.recv()
        self.assertIsInstance(notify2, Lwm2mNotify)
        self.assertEqual(bytes(notify2.token), bytes(observe2.token))
        self.assertEqual(notify1.content, notify2.content)
        self.serv.send(Lwm2mEmpty.matching(notify2)())

        self.execute_resource(self.serv, oid=OID.Test, iid=0, rid=RID.Test.IncrementCounter)
        notify2 = self.serv.recv()
        self.assertIsInstance(notify2, Lwm2mNotify)
        self.assertEqual(bytes(notify2.token), bytes(observe2.token))
        self.assertNotEqual(notify1.content, notify2.content)
        self.serv.send(Lwm2mReset.matching(notify2)())
