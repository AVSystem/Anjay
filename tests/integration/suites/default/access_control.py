# -*- coding: utf-8 -*-
#
# Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from framework.lwm2m.tlv import TLVType
from framework.lwm2m_test import *

from . import bootstrap_server

# In the following test suite we assume that self.servers[0] has SSID=1, and
# self.servers[1] has SSID=2. Current implementation of the demo guarantees
# that at least.
#
# Also SSID=2 is the master, and SSID=1 is his slave, it can do things allowed
# only by SSID=2, and this test set shall check that this is indeed the case.

g = SequentialMsgIdGenerator(1)

# We'd love to keep instance id in some reasonable bound before we dive into
# a solution that makes use of discover and so on.
IID_BOUND = 64


# This is all defined in standard
class AccessMask:
    NONE = 0
    READ = 1 << 0
    WRITE = 1 << 1
    EXECUTE = 1 << 2
    DELETE = 1 << 3
    CREATE = 1 << 4

    OWNER = READ | WRITE | EXECUTE | DELETE



def make_acl_entry(ssid, access):
    return (int(ssid), access.to_bytes(1, byteorder='big'))


class AccessControl:
    # not declaring the helper class in global scope to prevent it from being
    # considered a test case on its own
    class Test(test_suite.Lwm2mTest, test_suite.Lwm2mDmOperations):
        def validate_iid(self, iid):
            self.assertTrue(iid >= 0 and iid < IID_BOUND)

        def find_access_control_instance(self, server, oid, iid, expect_existence=True):
            self.validate_iid(iid)
            # It is very sad, that we have to iterate through
            # Access Control instances, but we really do.
            for instance in range(IID_BOUND):
                req = Lwm2mRead(ResPath.AccessControl[instance].TargetIID)
                server.send(req)
                res = server.recv()

                # TODO: assertMsgEqual(Lwm2mResponse.matching...)
                if res.code != coap.Code.RES_CONTENT:
                    continue

                res = self.read_resource(server, OID.AccessControl, instance,
                                         RID.AccessControl.TargetOID)
                ret_oid = int(res.content)

                res = self.read_resource(server, OID.AccessControl, instance,
                                         RID.AccessControl.TargetIID)
                ret_iid = int(res.content)

                if ret_oid == oid and ret_iid == iid:
                    return instance
            if expect_existence:
                assert False, "%d/%d/%d does not exist" % (OID.AccessControl, oid, iid)
            return None

        def update_access(self, server, oid, iid, acl, expected_acl=None, expect_error_code=None):
            self.validate_iid(iid)
            # Need to find Access Control instance for this iid.
            ac_iid = self.find_access_control_instance(server, oid, iid)
            self.assertTrue(ac_iid > 0)

            tlv = TLV.make_multires(RID.AccessControl.ACL, acl).serialize()
            if expected_acl:
                assert expect_error_code is None
                expected_tlv = TLV.make_multires(RID.AccessControl.ACL, expected_acl).serialize()
            else:
                expected_tlv = tlv
            self.write_instance(server, OID.AccessControl, ac_iid, tlv, partial=True,
                                expect_error_code=expect_error_code)
            if not expect_error_code:
                read_tlv = self.read_resource(server, OID.AccessControl, ac_iid,
                                              RID.AccessControl.ACL,
                                              accept=coap.ContentFormat.APPLICATION_LWM2M_TLV)
                self.assertEqual(expected_tlv, read_tlv.content)

        def setUp(self, servers=2, extra_cmdline_args=[], **kwargs):
            if isinstance(servers, int):
                servers_count = servers
            else:
                servers_count = len(servers)
            extra_args = sum((['--access-entry', '/%d/65535,%d,%d' % (OID.Test, ssid, AccessMask.CREATE)] for ssid in
                              range(2, servers_count + 1)),
                             extra_cmdline_args)
            self.setup_demo_with_servers(servers=servers,
                                         extra_cmdline_args=extra_args,
                                         **kwargs)

        def tearDown(self):
            self.teardown_demo_with_servers()


class CreateTest(AccessControl.Test):
    def runTest(self):
        observe_req = Lwm2mObserve('/%d' % (OID.AccessControl,))
        self.servers[0].send(observe_req)
        self.assertMsgEqual(Lwm2mContent.matching(observe_req)(), self.servers[0].recv())

        # SSID 2 has Create flag on OID.Test object, let's check if it
        # can really make an instance
        self.create_instance(server=self.servers[1], oid=OID.Test)

        # notification should arrive
        self.assertMsgEqual(Lwm2mNotify(observe_req.token), self.servers[0].recv())

        # Now do the same with SSID 1, it should fail very very much
        self.create_instance(server=self.servers[0], oid=OID.Test,
                             expect_error_code=coap.Code.RES_UNAUTHORIZED)


class ReadTest(AccessControl.Test):
    def runTest(self):
        self.create_instance(server=self.servers[1], oid=OID.Test)

        # SSID 2 has rights obviously
        self.read_resource(server=self.servers[1], oid=OID.Test, iid=0, rid=RID.Test.Timestamp)

        # SSID 1 has no rights, it should not be able to read instance / resource
        self.read_resource(server=self.servers[0], oid=OID.Test, iid=0, rid=RID.Test.Timestamp,
                           expect_error_code=coap.Code.RES_UNAUTHORIZED)
        self.read_instance(server=self.servers[0], oid=OID.Test, iid=0,
                           expect_error_code=coap.Code.RES_UNAUTHORIZED)


class WriteAttributesTest(AccessControl.Test):
    def runTest(self):
        self.create_instance(server=self.servers[1], oid=OID.Test)

        # SSID 2 can perform Write-Attributes
        self.write_attributes(server=self.servers[1], oid=OID.Test, iid=0, query=['pmin=500'])

        # SSID 1 can't, as it cannot Read
        self.write_attributes(server=self.servers[0], oid=OID.Test, iid=0, query=['pmin=600'],
                              expect_error_code=coap.Code.RES_UNAUTHORIZED)


class ChangingReadFlagsMatterTest(AccessControl.Test):
    def runTest(self):
        self.create_instance(server=self.servers[1], oid=OID.Test)

        # Update SSID 1 access rights, so that he'll be able to read resource
        self.update_access(server=self.servers[1], oid=OID.Test, iid=0,
                           acl=[make_acl_entry(1, AccessMask.READ),
                                make_acl_entry(2, AccessMask.OWNER)])

        # SSID 1 shall be able to read the resource and the entire instance too
        self.read_resource(server=self.servers[0], oid=OID.Test, iid=0, rid=RID.Test.Timestamp)
        self.read_instance(server=self.servers[0], oid=OID.Test, iid=0)


class ChangingFlagsNotifiesTest(AccessControl.Test):
    def runTest(self):
        self.create_instance(server=self.servers[1], oid=OID.Test)

        observe_req = Lwm2mObserve('/%d' % (OID.AccessControl,))
        self.servers[0].send(observe_req)
        self.assertMsgEqual(Lwm2mContent.matching(observe_req)(), self.servers[0].recv())

        # Update SSID 1 access rights
        self.update_access(server=self.servers[1], oid=OID.Test, iid=0,
                           acl=[make_acl_entry(1, AccessMask.READ),
                                make_acl_entry(2, AccessMask.OWNER)])

        # notification should arrive
        self.assertMsgEqual(Lwm2mNotify(observe_req.token), self.servers[0].recv())


class ExecuteTest(AccessControl.Test):
    def runTest(self):
        self.create_instance(server=self.servers[1], oid=OID.Test)

        self.execute_resource(self.servers[1], OID.Test, 0, RID.Test.IncrementCounter)

        # No fun for you SSID 1!
        self.execute_resource(server=self.servers[0], oid=OID.Test, iid=0,
                              rid=RID.Test.IncrementCounter,
                              expect_error_code=coap.Code.RES_UNAUTHORIZED)


class ChangingExecuteFlagsMatter(AccessControl.Test):
    def runTest(self):
        self.create_instance(server=self.servers[1], oid=OID.Test)

        self.update_access(server=self.servers[1], oid=OID.Test, iid=0,
                           acl=[make_acl_entry(1, AccessMask.EXECUTE),
                                make_acl_entry(2, AccessMask.OWNER)])

        self.execute_resource(self.servers[0], OID.Test, 0, RID.Test.IncrementCounter)
        self.execute_resource(self.servers[1], OID.Test, 0, RID.Test.IncrementCounter)


class DeleteTest(AccessControl.Test):
    def runTest(self):
        self.create_instance(server=self.servers[1], oid=OID.Test)

        observe_req = Lwm2mObserve('/%d' % (OID.AccessControl,))
        self.servers[0].send(observe_req)
        self.assertMsgEqual(Lwm2mContent.matching(observe_req)(), self.servers[0].recv())

        ac_iid = self.find_access_control_instance(server=self.servers[1], oid=OID.Test, iid=0)

        self.delete_instance(server=self.servers[0], oid=OID.Test, iid=0,
                             expect_error_code=coap.Code.RES_UNAUTHORIZED)
        self.delete_instance(server=self.servers[1], oid=OID.Test, iid=0)

        # notification should arrive
        self.assertMsgEqual(Lwm2mNotify(observe_req.token), self.servers[0].recv())

        # Instance is removed, so its corresponding Access Control Instance
        # should be removed automatically too. The answer shall be NOT_FOUND
        # according to the spec.
        self.read_instance(server=self.servers[1], oid=OID.AccessControl, iid=ac_iid,
                           expect_error_code=coap.Code.RES_NOT_FOUND)


class EmptyAclMeansFullAccessTest(AccessControl.Test):
    def runTest(self):
        self.create_instance(server=self.servers[1], oid=OID.Test)
        self.update_access(server=self.servers[1], oid=OID.Test, iid=0, acl=[],
                           expected_acl=[make_acl_entry(2, AccessMask.OWNER)])

        # Read
        self.read_resource(server=self.servers[1], oid=OID.Test, iid=0, rid=RID.Test.Timestamp)
        self.read_resource(server=self.servers[0], oid=OID.Test, iid=0, rid=RID.Test.Timestamp,
                           expect_error_code=coap.Code.RES_UNAUTHORIZED)
        self.read_instance(server=self.servers[0], oid=OID.Test, iid=0,
                           expect_error_code=coap.Code.RES_UNAUTHORIZED)
        # Write
        self.write_instance(server=self.servers[1], oid=OID.Test, iid=0, partial=True)
        self.write_instance(server=self.servers[0], oid=OID.Test, iid=0, partial=True,
                            expect_error_code=coap.Code.RES_UNAUTHORIZED)
        # Execute
        self.execute_resource(server=self.servers[1], oid=OID.Test, iid=0,
                              rid=RID.Test.IncrementCounter)
        # Delete
        self.delete_instance(server=self.servers[1], oid=OID.Test, iid=0)


class NoDuplicatedAclTest(AccessControl.Test):
    def runTest(self):
        self.create_instance(server=self.servers[1], oid=OID.Test)
        self.update_access(server=self.servers[1], oid=OID.Test, iid=0,
                           acl=[make_acl_entry(2, AccessMask.OWNER),
                                make_acl_entry(2, 0),
                                make_acl_entry(2, AccessMask.OWNER)],
                           expected_acl=[make_acl_entry(2, AccessMask.OWNER)])

        ac_iid = self.find_access_control_instance(server=self.servers[1], oid=OID.Test, iid=0)

        res = self.read_resource(server=self.servers[1], oid=OID.AccessControl, iid=ac_iid,
                                 rid=RID.AccessControl.ACL)
        self.assertEqual(coap.ContentFormat.APPLICATION_LWM2M_TLV, res.get_content_format())

        tlv = TLV.parse(res.content)
        # One Multiple Resource
        self.assertEquals(len(tlv), 1)
        # with valid ID
        self.assertEquals(tlv[0].identifier, RID.AccessControl.ACL)
        # with exactly one Instance (2,AccessMask.OWNER)
        self.assertEquals(len(tlv[0].value), 1)
        self.assertEquals(tlv[0].value[0].value, AccessMask.OWNER.to_bytes(1, byteorder='big'))


class DefaultAclTest(AccessControl.Test):
    def runTest(self):
        self.read_instance(server=self.servers[0], oid=OID.Server, iid=1)
        self.read_instance(server=self.servers[0], oid=OID.Server, iid=2,
                           expect_error_code=coap.Code.RES_UNAUTHORIZED)
        self.read_instance(server=self.servers[1], oid=OID.Server, iid=1,
                           expect_error_code=coap.Code.RES_UNAUTHORIZED)
        self.read_instance(server=self.servers[1], oid=OID.Server, iid=2)

        self.read_path(self.servers[0], ResPath.Device.SerialNumber)

        self.create_instance(server=self.servers[1], oid=OID.Test)
        self.update_access(server=self.servers[1], oid=OID.Test, iid=0,
                           acl=[make_acl_entry(0, AccessMask.EXECUTE),
                                make_acl_entry(2, AccessMask.OWNER)])

        # Now SSID 1 should be able to execute because he obtains access from the
        # default ACL (with ID=0)
        self.execute_resource(server=self.servers[0], oid=OID.Test, iid=0,
                              rid=RID.Test.IncrementCounter)

        # Now we take away the rights and make sure SSID1 can not do execute
        self.update_access(server=self.servers[1], oid=OID.Test, iid=0,
                           acl=[make_acl_entry(0, AccessMask.NONE)],
                           expected_acl=[make_acl_entry(0, AccessMask.NONE),
                                         make_acl_entry(2, AccessMask.OWNER)])

        self.execute_resource(server=self.servers[0], oid=OID.Test, iid=0,
                              rid=RID.Test.IncrementCounter,
                              expect_error_code=coap.Code.RES_UNAUTHORIZED)


class ReadObjectWithPartialReadAccessTest(AccessControl.Test):
    def runTest(self):
        self.create_instance(server=self.servers[1], oid=OID.Test)  # IID=1
        self.create_instance(server=self.servers[1], oid=OID.Test)  # IID=2
        self.create_instance(server=self.servers[1], oid=OID.Test)  # IID=3

        # IID=0 will be readable by SSID=1 because of default ACL
        self.update_access(server=self.servers[1], oid=OID.Test, iid=0,
                           acl=[make_acl_entry(0, AccessMask.READ),
                                make_acl_entry(2, AccessMask.OWNER)])
        # IID=1 will not be readable by SSID=1
        self.update_access(server=self.servers[1], oid=OID.Test, iid=1,
                           acl=[make_acl_entry(0, AccessMask.EXECUTE),
                                make_acl_entry(2, AccessMask.OWNER)])
        # IID=2 will be readable by SSID=1 because of direct ACL entry
        self.update_access(server=self.servers[1], oid=OID.Test, iid=2,
                           acl=[make_acl_entry(1, AccessMask.READ),
                                make_acl_entry(2, AccessMask.OWNER)])

        # SSID=2 should see all three Instances
        res = self.read_object(server=self.servers[1], oid=OID.Test)
        self.assertEqual(coap.ContentFormat.APPLICATION_LWM2M_TLV, res.get_content_format())

        tlv = TLV.parse(res.content)
        expected_instances = set([0, 1, 2])
        self.assertEquals(len(tlv), len(expected_instances))
        for instance in tlv:
            self.assertEquals(instance.tlv_type, TLVType.INSTANCE)
            expected_instances.remove(instance.identifier)
        self.assertEquals(len(expected_instances), 0)

        # And SSID=1 should see only IID=1 and IID=3
        res = self.read_object(server=self.servers[0], oid=OID.Test)
        self.assertEqual(coap.ContentFormat.APPLICATION_LWM2M_TLV, res.get_content_format())

        tlv = TLV.parse(res.content)
        expected_instances = set([0, 2])
        self.assertEquals(len(tlv), len(expected_instances))
        for instance in tlv:
            self.assertEquals(instance.tlv_type, TLVType.INSTANCE)
            expected_instances.remove(instance.identifier)
        self.assertEquals(len(expected_instances), 0)


class ReadObjectWithNoInstancesTest(AccessControl.Test):
    def runTest(self):
        res = self.read_object(server=self.servers[1], oid=OID.Test)
        self.assertEqual(coap.ContentFormat.APPLICATION_LWM2M_TLV, res.get_content_format())

        tlv = TLV.parse(res.content)
        self.assertEquals(len(tlv), 0)
        self.create_instance(server=self.servers[1], oid=OID.Test)
        self.create_instance(server=self.servers[1], oid=OID.Test)
        self.create_instance(server=self.servers[1], oid=OID.Test)

        res = self.read_object(server=self.servers[0], oid=OID.Test)
        self.assertEqual(coap.ContentFormat.APPLICATION_LWM2M_TLV, res.get_content_format())

        tlv = TLV.parse(res.content)
        self.assertEquals(len(tlv), 0)


class ActionOnNonexistentInstanceTest(AccessControl.Test):
    def runTest(self):
        # Every instance action on nonexistent instance shall return NOT_FOUND
        self.read_instance(server=self.servers[0], oid=OID.Test, iid=2,
                           expect_error_code=coap.Code.RES_NOT_FOUND)
        self.delete_instance(server=self.servers[0], oid=OID.Test, iid=2,
                             expect_error_code=coap.Code.RES_NOT_FOUND)
        self.write_instance(server=self.servers[0], oid=OID.Test, iid=2,
                            expect_error_code=coap.Code.RES_NOT_FOUND)
        self.execute_resource(server=self.servers[0], oid=OID.Test, iid=2,
                              rid=RID.Test.IncrementCounter,
                              expect_error_code=coap.Code.RES_NOT_FOUND)


class RemovingAcoInstanceFailsTest(AccessControl.Test):
    def runTest(self):
        self.create_instance(server=self.servers[1], oid=OID.Test)
        ac_iid = self.find_access_control_instance(self.servers[1], oid=OID.Test, iid=0)
        self.delete_instance(server=self.servers[1], oid=OID.AccessControl, iid=ac_iid,
                             expect_error_code=coap.Code.RES_UNAUTHORIZED)


class EveryoneHasReadAccessToAcoInstancesTest(AccessControl.Test):
    def runTest(self):
        self.read_instance(server=self.servers[0], oid=OID.AccessControl, iid=0)
        self.read_instance(server=self.servers[1], oid=OID.AccessControl, iid=0)


class UnbootstrappingOnlyOneOwnerTest(AccessControl.Test):
    def setUp(self):
        super().setUp(servers=3)

    def runTest(self):
        self.create_instance(server=self.servers[2], oid=OID.Test)
        ac_iid = self.find_access_control_instance(server=self.servers[2], oid=OID.Test, iid=0)
        assert ac_iid
        # Deleting server shall delete ACO Instance and /OID.Test/0 instance too.
        self.communicate('trim-servers 2')
        self.assertDemoDeregisters(self.servers[2])
        self.assertDemoUpdatesRegistration(self.servers[0], content=ANY)
        self.assertDemoUpdatesRegistration(self.servers[1], content=ANY)
        del (self.servers[2])

        assert not self.find_access_control_instance(server=self.servers[1], oid=OID.Test, iid=0,
                                                     expect_existence=False)
        self.read_instance(server=self.servers[1], oid=OID.Test, iid=1,
                           expect_error_code=coap.Code.RES_NOT_FOUND)


class UnbootstrappingOwnerElection1(AccessControl.Test):
    def setUp(self):
        super().setUp(servers=3)

    def runTest(self):
        self.create_instance(server=self.servers[2], oid=OID.Test)
        self.update_access(server=self.servers[2], oid=OID.Test, iid=0,
                           acl=[make_acl_entry(1, AccessMask.WRITE | AccessMask.DELETE),
                                make_acl_entry(2, AccessMask.WRITE | AccessMask.EXECUTE),
                                make_acl_entry(3, AccessMask.OWNER)])
        self.communicate('trim-servers 2')
        self.assertDemoDeregisters(self.servers[2])
        self.assertDemoUpdatesRegistration(self.servers[0], content=ANY)
        self.assertDemoUpdatesRegistration(self.servers[1], content=ANY)
        del (self.servers[2])
        ac_iid = self.find_access_control_instance(server=self.servers[1], oid=OID.Test, iid=0)

        # SSID=1 shall win the election
        res = self.read_resource(self.servers[1],
                                 OID.AccessControl, ac_iid, RID.AccessControl.Owner)
        self.assertEqual(b'1', res.content)

        serv3 = Lwm2mServer()
        self.communicate('add-server coap://127.0.0.1:%d' % serv3.get_listen_port())
        self.assertDemoUpdatesRegistration(self.servers[0], content=ANY)
        self.assertDemoUpdatesRegistration(self.servers[1], content=ANY)
        self.assertDemoRegisters(serv3)

        # new SSID=3 is not the same server and does not have access any more
        self.create_instance(server=serv3, oid=OID.Test,
                             expect_error_code=coap.Code.RES_UNAUTHORIZED)


class UnbootstrappingOwnerElection2(AccessControl.Test):
    def setUp(self):
        super().setUp(servers=3)

    def runTest(self):
        self.create_instance(server=self.servers[2], oid=OID.Test)
        self.update_access(server=self.servers[2], oid=OID.Test, iid=0,
                           acl=[make_acl_entry(1, AccessMask.WRITE | AccessMask.EXECUTE),
                                make_acl_entry(2, AccessMask.WRITE | AccessMask.DELETE),
                                make_acl_entry(3, AccessMask.OWNER)])
        self.communicate('trim-servers 2')
        self.assertDemoDeregisters(self.servers[2])
        self.assertDemoUpdatesRegistration(self.servers[0], content=ANY)
        self.assertDemoUpdatesRegistration(self.servers[1], content=ANY)
        del (self.servers[2])
        ac_iid = self.find_access_control_instance(server=self.servers[1], oid=OID.Test, iid=0)

        # SSID=2 shall win the election now
        res = self.read_resource(self.servers[1],
                                 OID.AccessControl, ac_iid, RID.AccessControl.Owner)
        self.assertEqual(b'2', res.content)


class AclActiveDespiteOnlyOneServerSuccessfullyConnected(AccessControl.Test):
    def setUp(self):
        super().setUp(auto_register=False)

    def tearDown(self):
        self.teardown_demo_with_servers(deregister_servers=[self.servers[0]])

    def runTest(self):
        self.assertDemoRegisters(self.servers[0])

        # reject the Register on second server
        second_register = self.servers[1].recv()
        self.assertIsInstance(second_register, Lwm2mRegister)
        self.servers[1].send(Lwm2mReset.matching(second_register)())

        # SSID 1 has no rights to read information about SSID 2, even though it's the only properly connected server
        self.read_resource(server=self.servers[0], oid=OID.Server, iid=2,
                           rid=RID.Server.ShortServerID,
                           expect_error_code=coap.Code.RES_UNAUTHORIZED)


class AclBootstrapping(bootstrap_server.BootstrapServer.Test, test_suite.Lwm2mDmOperations):
    def add_server(self, iid):
        server = Lwm2mServer()
        uri = 'coap://127.0.0.1:%d' % server.get_listen_port()
        self.write_instance(self.bootstrap_server, OID.Security, iid,
                            TLV.make_resource(RID.Security.ServerURI, uri).serialize()
                            + TLV.make_resource(RID.Security.Bootstrap, 0).serialize()
                            + TLV.make_resource(RID.Security.Mode, 3).serialize()
                            + TLV.make_resource(RID.Security.ShortServerID, iid).serialize()
                            + TLV.make_resource(RID.Security.PKOrIdentity, "").serialize()
                            + TLV.make_resource(RID.Security.SecretKey, "").serialize())
        self.write_instance(self.bootstrap_server, OID.Server, iid,
                            TLV.make_resource(RID.Server.Lifetime, 86400).serialize()
                            + TLV.make_resource(RID.Server.Binding, "U").serialize()
                            + TLV.make_resource(RID.Server.ShortServerID, iid).serialize()
                            + TLV.make_resource(RID.Server.NotificationStoring, True).serialize())
        return server

    def runTest(self):
        self.bootstrap_server.connect_to_client(('127.0.0.1', self.get_demo_port()))

        # Bootstrap Delete /
        req = Lwm2mDelete('/')
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mDeleted.matching(req)(),
                            self.bootstrap_server.recv())

        # create servers
        self.servers = [self.add_server(1), self.add_server(2)]

        # create test objects
        self.write_instance(self.bootstrap_server, OID.Test, 42)
        self.write_instance(self.bootstrap_server, OID.Test, 69)
        self.write_instance(self.bootstrap_server, OID.Test, 514)

        # create ACLs
        self.write_instance(self.bootstrap_server, OID.AccessControl, 7,
                            TLV.make_resource(RID.AccessControl.TargetOID, OID.Test).serialize()
                            + TLV.make_resource(RID.AccessControl.TargetIID, 514).serialize()
                            + TLV.make_multires(RID.AccessControl.ACL, {2: 7}.items()).serialize()
                            + TLV.make_resource(RID.AccessControl.Owner, 2).serialize())
        self.write_instance(self.bootstrap_server, OID.AccessControl, 9,
                            TLV.make_resource(RID.AccessControl.TargetOID, OID.Test).serialize()
                            + TLV.make_resource(RID.AccessControl.TargetIID, 42).serialize()
                            + TLV.make_multires(RID.AccessControl.ACL, {1: 7}.items()).serialize()
                            + TLV.make_resource(RID.AccessControl.Owner, 1).serialize())

        # check that those are the only ACLs currently in data model
        self.assertIn(
            b'</%d>,</%d/7>,</%d/9>,</%d>' % (
                OID.AccessControl, OID.AccessControl, OID.AccessControl, OID.Device),
            self.discover(self.bootstrap_server).content)

        # send Bootstrap Finish
        req = Lwm2mBootstrapFinish()
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())

        self.assertDemoRegisters(self.servers[0])
        self.assertDemoRegisters(self.servers[1])

        # SSID 1 rights
        self.read_resource(server=self.servers[0], oid=OID.Test, iid=42, rid=RID.Test.Timestamp)
        self.read_resource(server=self.servers[0], oid=OID.Test, iid=69, rid=RID.Test.Timestamp,
                           expect_error_code=coap.Code.RES_UNAUTHORIZED)
        self.read_resource(server=self.servers[0], oid=OID.Test, iid=514, rid=RID.Test.Timestamp,
                           expect_error_code=coap.Code.RES_UNAUTHORIZED)

        # SSID 2 rights
        self.read_resource(server=self.servers[1], oid=OID.Test, iid=42, rid=RID.Test.Timestamp,
                           expect_error_code=coap.Code.RES_UNAUTHORIZED)
        self.read_resource(server=self.servers[1], oid=OID.Test, iid=69, rid=RID.Test.Timestamp,
                           expect_error_code=coap.Code.RES_UNAUTHORIZED)
        self.read_resource(server=self.servers[1], oid=OID.Test, iid=514, rid=RID.Test.Timestamp)


class InvalidAcl(AccessControl.Test):
    def runTest(self):
        self.create_instance(server=self.servers[1], oid=OID.Test)

        # check that one cannot create an ACL with RIID==65535 (and that such attempt does not segfault)
        self.update_access(server=self.servers[1], oid=OID.Test, iid=0,
                           acl=[make_acl_entry(65535, AccessMask.READ)],
                           expect_error_code=coap.Code.RES_BAD_REQUEST)

        # check that valid ACL works after previous failed attempt
        self.update_access(server=self.servers[1], oid=OID.Test, iid=0,
                           acl=[make_acl_entry(2, AccessMask.READ)])
