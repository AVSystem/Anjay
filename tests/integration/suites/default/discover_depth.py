# -*- coding: utf-8 -*-
#
# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

from framework.lwm2m_test import *

from . import test_object


class DiscoverDepth:
    class Test(test_object.TestObject.TestCase, test_suite.Lwm2mDmOperations):
        def setUp(self):
            class EmptyClass:
                pass

            super().setUp(minimum_version='1.2', maximum_version='1.2')
            self.execute_resource(self.serv, oid=OID.Test, iid=0, rid=RID.Test.ResInitIntArray,
                                  content=b"2='137'")

            self.write_attributes(self.serv, oid=OID.Test, query=['pmin=1'])
            self.write_attributes(self.serv, oid=OID.Test, iid=0, query=['pmax=2'])

            for resource in (RID.Test.__dict__.keys() - EmptyClass.__dict__.keys()):
                rid = getattr(RID.Test, resource)
                self.write_attributes(self.serv, oid=OID.Test, iid=0, rid=rid,
                                      query=['gt=%d' % (rid,)])


class DiscoverObjectDepthTest(DiscoverDepth.Test):
    def runTest(self):
        result = self.discover(self.serv, oid=OID.Test, depth=0)
        self.assertEqual(result.content, b'</33605>;pmin=1')

        result = self.discover(self.serv, oid=OID.Test, depth=1)
        self.assertEqual(result.content, b'</33605>;pmin=1,</33605/0>;pmax=2')

        result = self.discover(self.serv, oid=OID.Test, depth=2)
        self.assertEqual(result.content,
                         b'</33605>;pmin=1,</33605/0>;pmax=2,</33605/0/0>;gt=0,</33605/0/1>;gt=1,'
                         + b'</33605/0/2>;gt=2,</33605/0/3>;dim=1;gt=3,</33605/0/4>;dim=0;gt=4,'
                         + b'</33605/0/5>;gt=5,</33605/0/6>;gt=6,</33605/0/7>;gt=7,'
                         + b'</33605/0/9>;gt=9,</33605/0/10>;gt=10,</33605/0/11>;dim=1;gt=11,'
                         + b'</33605/0/12>;gt=12,</33605/0/13>;gt=13,</33605/0/14>;gt=14,'
                         + b'</33605/0/15>;gt=15,</33605/0/16>;gt=16,</33605/0/17>;gt=17,'
                         + b'</33605/0/18>;gt=18,</33605/0/19>;gt=19,</33605/0/20>;gt=20,'
                         + b'</33605/0/21>;gt=21,</33605/0/22>;dim=0;gt=22,</33605/0/23>;gt=23')


        result = self.discover(self.serv, oid=OID.Test, depth=3)
        self.assertEqual(result.content,
                         b'</33605>;pmin=1,</33605/0>;pmax=2,</33605/0/0>;gt=0,</33605/0/1>;gt=1,'
                         + b'</33605/0/2>;gt=2,</33605/0/3>;dim=1;gt=3,</33605/0/3/2>,'
                         + b'</33605/0/4>;dim=0;gt=4,</33605/0/5>;gt=5,</33605/0/6>;gt=6,'
                         + b'</33605/0/7>;gt=7,</33605/0/9>;gt=9,</33605/0/10>;gt=10,'
                         + b'</33605/0/11>;dim=1;gt=11,</33605/0/11/2>,</33605/0/12>;gt=12,'
                         + b'</33605/0/13>;gt=13,</33605/0/14>;gt=14,</33605/0/15>;gt=15,'
                         + b'</33605/0/16>;gt=16,</33605/0/17>;gt=17,</33605/0/18>;gt=18,'
                         + b'</33605/0/19>;gt=19,</33605/0/20>;gt=20,</33605/0/21>;gt=21,'
                         + b'</33605/0/22>;dim=0;gt=22,</33605/0/23>;gt=23')


class DiscoverInstanceDepthTest(DiscoverDepth.Test):
    def runTest(self):
        result = self.discover(self.serv, oid=OID.Test, iid=0, depth=0)
        self.assertEqual(result.content, b'</33605/0>;pmin=1;pmax=2')

        result = self.discover(self.serv, oid=OID.Test, iid=0, depth=1)
        self.assertEqual(result.content,
                         b'</33605/0>;pmin=1;pmax=2,</33605/0/0>;gt=0,</33605/0/1>;gt=1,'
                         + b'</33605/0/2>;gt=2,</33605/0/3>;dim=1;gt=3,</33605/0/4>;dim=0;gt=4,'
                         + b'</33605/0/5>;gt=5,</33605/0/6>;gt=6,</33605/0/7>;gt=7,'
                         + b'</33605/0/9>;gt=9,</33605/0/10>;gt=10,</33605/0/11>;dim=1;gt=11,'
                         + b'</33605/0/12>;gt=12,</33605/0/13>;gt=13,</33605/0/14>;gt=14,'
                         + b'</33605/0/15>;gt=15,</33605/0/16>;gt=16,</33605/0/17>;gt=17,'
                         + b'</33605/0/18>;gt=18,</33605/0/19>;gt=19,</33605/0/20>;gt=20,'
                         + b'</33605/0/21>;gt=21,</33605/0/22>;dim=0;gt=22,</33605/0/23>;gt=23')

        result = self.discover(self.serv, oid=OID.Test, iid=0, depth=2)
        self.assertEqual(result.content,
                         b'</33605/0>;pmin=1;pmax=2,</33605/0/0>;gt=0,</33605/0/1>;gt=1,'
                         + b'</33605/0/2>;gt=2,</33605/0/3>;dim=1;gt=3,</33605/0/3/2>,'
                         + b'</33605/0/4>;dim=0;gt=4,</33605/0/5>;gt=5,</33605/0/6>;gt=6,'
                         + b'</33605/0/7>;gt=7,</33605/0/9>;gt=9,</33605/0/10>;gt=10,'
                         + b'</33605/0/11>;dim=1;gt=11,</33605/0/11/2>,</33605/0/12>;gt=12,'
                         + b'</33605/0/13>;gt=13,</33605/0/14>;gt=14,</33605/0/15>;gt=15,'
                         + b'</33605/0/16>;gt=16,</33605/0/17>;gt=17,</33605/0/18>;gt=18,'
                         + b'</33605/0/19>;gt=19,</33605/0/20>;gt=20,</33605/0/21>;gt=21,'
                         + b'</33605/0/22>;dim=0;gt=22,</33605/0/23>;gt=23')
        max_depth_content = result.content

        result = self.discover(self.serv, oid=OID.Test, iid=0, depth=3)
        self.assertEqual(result.content, max_depth_content)


class DiscoverResourceDepthTest(DiscoverDepth.Test):
    def runTest(self):
        result = self.discover(self.serv, oid=OID.Test, iid=0, rid=RID.Test.IntArray, depth=0)
        self.assertEqual(result.content, b'</33605/0/3>;dim=1;pmin=1;pmax=2;gt=3')

        result = self.discover(self.serv, oid=OID.Test, iid=0, rid=RID.Test.IntArray, depth=1)
        self.assertEqual(result.content, b'</33605/0/3>;dim=1;pmin=1;pmax=2;gt=3,</33605/0/3/2>')
        max_depth_content = result.content

        result = self.discover(self.serv, oid=OID.Test, iid=0, rid=RID.Test.IntArray, depth=2)
        self.assertEqual(result.content, max_depth_content)

        result = self.discover(self.serv, oid=OID.Test, iid=0, rid=RID.Test.IntArray, depth=3)
        self.assertEqual(result.content, max_depth_content)
