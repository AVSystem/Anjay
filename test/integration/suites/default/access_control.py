# In the following test suite we assume that self.servers[0] has SSID=1, and
# self.servers[1] has SSID=2. Current implementation of the demo guarantees
# that at least.
#
# Also SSID=2 is the master, and SSID=1 is his slave, it can do things allowed
# only by SSID=2, and this test set shall check that this is indeed the case.

from framework.lwm2m.tlv import TLVType
from framework.lwm2m_test import *
from . import bootstrap_server

g = SequentialMsgIdGenerator(1)

# We'd love to keep instance id in some reasonable bound before we dive into
# a solution that makes use of discover and so on.
IID_BOUND = 64

# This is all defined in standard
ACCESS_MASK_READ = 1 << 0
ACCESS_MASK_WRITE = 1 << 1
ACCESS_MASK_EXECUTE = 1 << 2
ACCESS_MASK_DELETE = 1 << 3
ACCESS_MASK_CREATE = 1 << 4

ACCESS_MASK_OWNER = ACCESS_MASK_READ | ACCESS_MASK_WRITE | ACCESS_MASK_EXECUTE | ACCESS_MASK_DELETE

ACCESS_CONTROL_OID = 2
ACCESS_CONTROL_RID_OID = 0
ACCESS_CONTROL_RID_OIID = 1
ACCESS_CONTROL_RID_ACL = 2
ACCESS_CONTROL_RID_OWNER = 3

SERVER_OID = 1

# This however is not, but see demo/objects/test.c;
# INCREMENT_COUNTER is a very nice executable resource
TEST_OID = 1337
TEST_RID_INCREMENT_COUNTER = 2


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
                req = Lwm2mRead('/%d/%d/%d' % (ACCESS_CONTROL_OID, instance, ACCESS_CONTROL_RID_OIID))
                server.send(req)
                res = server.recv(timeout_s=2)

                # TODO: assertMsgEqual(Lwm2mResponse.matching...)
                if res.code != coap.Code.RES_CONTENT:
                    continue

                res = self.read_resource(server, ACCESS_CONTROL_OID, instance, ACCESS_CONTROL_RID_OID)
                ret_oid = int(res.content)

                res = self.read_resource(server, ACCESS_CONTROL_OID, instance, ACCESS_CONTROL_RID_OIID)
                ret_iid = int(res.content)

                if ret_oid == oid and ret_iid == iid:
                    return instance
            if expect_existence:
                assert False, "%d/%d/%d does not exist" % (ACCESS_CONTROL_OID, oid, iid)
            return None

        def update_access(self, server, oid, iid, acl, expected_acl=None):
            self.validate_iid(iid)
            # Need to find Access Control instance for this iid.
            ac_iid = self.find_access_control_instance(server, oid, iid)
            self.assertTrue(ac_iid > 0)

            tlv = TLV.make_multires(ACCESS_CONTROL_RID_ACL, acl).serialize()
            if expected_acl:
                expected_tlv = TLV.make_multires(ACCESS_CONTROL_RID_ACL, expected_acl).serialize()
            else:
                expected_tlv = tlv
            self.write_instance(server, ACCESS_CONTROL_OID, ac_iid, tlv, partial=True)
            read_tlv = self.read_resource(server, ACCESS_CONTROL_OID, ac_iid, ACCESS_CONTROL_RID_ACL,
                                          accept=coap.ContentFormat.APPLICATION_LWM2M_TLV)
            self.assertEqual(expected_tlv, read_tlv.content)

        def setUp(self, num_servers=2, **kwargs):
            extra_args = sum((['--access-entry', '1337', str(ssid)] for ssid in range(2, num_servers + 1)), [])
            self.setup_demo_with_servers(num_servers=num_servers,
                                         extra_cmdline_args=extra_args,
                                         **kwargs)

        def tearDown(self):
            self.teardown_demo_with_servers()


class CreateTest(AccessControl.Test):
    def runTest(self):
        observe_req = Lwm2mObserve('/2')
        self.servers[0].send(observe_req)
        self.assertMsgEqual(Lwm2mContent.matching(observe_req)(), self.servers[0].recv())

        # SSID 2 has Create flag on TEST_OID object, let's check if it
        # can really make an instance
        self.create_instance(server=self.servers[1], oid=TEST_OID)

        # notification should arrive
        self.assertMsgEqual(Lwm2mNotify(observe_req.token), self.servers[0].recv())

        # Now do the same with SSID 1, it should fail very very much
        self.create_instance(server=self.servers[0], oid=TEST_OID,
                             expect_error_code=coap.Code.RES_UNAUTHORIZED)


class ReadTest(AccessControl.Test):
    def runTest(self):
        self.create_instance(server=self.servers[1], oid=TEST_OID)

        # SSID 2 has rights obviously
        self.read_resource(server=self.servers[1], oid=TEST_OID, iid=1, rid=0)

        # SSID 1 has no rights, it should not be able to read instance / resource
        self.read_resource(server=self.servers[0], oid=TEST_OID, iid=1, rid=0,
                           expect_error_code=coap.Code.RES_UNAUTHORIZED)
        self.read_instance(server=self.servers[0], oid=TEST_OID, iid=1,
                           expect_error_code=coap.Code.RES_UNAUTHORIZED)


class ChangingReadFlagsMatterTest(AccessControl.Test):
    def runTest(self):
        self.create_instance(server=self.servers[1], oid=TEST_OID)

        # Update SSID 1 access rights, so that he'll be able to read resource
        self.update_access(server=self.servers[1], oid=TEST_OID, iid=1,
                           acl=[make_acl_entry(1, ACCESS_MASK_READ),
                                make_acl_entry(2, ACCESS_MASK_OWNER)])

        # SSID 1 shall be able to read the resource and the entire instance too
        self.read_resource(server=self.servers[0], oid=TEST_OID, iid=1, rid=0)
        self.read_instance(server=self.servers[0], oid=TEST_OID, iid=1)


class ExecuteTest(AccessControl.Test):
    def runTest(self):
        self.create_instance(server=self.servers[1], oid=TEST_OID)

        self.execute_resource(self.servers[1], TEST_OID, 1, TEST_RID_INCREMENT_COUNTER)

        # No fun for you SSID 1!
        self.execute_resource(server=self.servers[0], oid=TEST_OID, iid=1, rid=TEST_RID_INCREMENT_COUNTER,
                              expect_error_code=coap.Code.RES_UNAUTHORIZED)


class ChangingExecuteFlagsMatter(AccessControl.Test):
    def runTest(self):
        self.create_instance(server=self.servers[1], oid=TEST_OID)

        self.update_access(server=self.servers[1], oid=TEST_OID, iid=1,
                           acl=[make_acl_entry(1, ACCESS_MASK_EXECUTE),
                                make_acl_entry(2, ACCESS_MASK_OWNER)])

        self.execute_resource(self.servers[0], TEST_OID, 1, TEST_RID_INCREMENT_COUNTER)
        self.execute_resource(self.servers[1], TEST_OID, 1, TEST_RID_INCREMENT_COUNTER)


class DeleteTest(AccessControl.Test):
    def runTest(self):
        self.create_instance(server=self.servers[1], oid=TEST_OID)

        ac_iid = self.find_access_control_instance(server=self.servers[1], oid=TEST_OID, iid=1)

        self.delete_instance(server=self.servers[0], oid=TEST_OID, iid=1,
                             expect_error_code=coap.Code.RES_UNAUTHORIZED)
        self.delete_instance(server=self.servers[1], oid=TEST_OID, iid=1)
        # Instance is removed, so its corresponding Access Control Instance
        # should be removed automatically too. The answer shall be NOT_FOUND
        # according to the spec.
        self.read_instance(server=self.servers[1], oid=ACCESS_CONTROL_OID, iid=ac_iid,
                           expect_error_code=coap.Code.RES_NOT_FOUND)


class EmptyAclMeansFullAccessTest(AccessControl.Test):
    def runTest(self):
        self.create_instance(server=self.servers[1], oid=TEST_OID)
        self.update_access(server=self.servers[1], oid=TEST_OID, iid=1, acl=[])

        # Read
        self.read_resource(server=self.servers[1], oid=TEST_OID, iid=1, rid=0)
        self.read_resource(server=self.servers[0], oid=TEST_OID, iid=1, rid=0,
                           expect_error_code=coap.Code.RES_UNAUTHORIZED)
        self.read_instance(server=self.servers[0], oid=TEST_OID, iid=1,
                           expect_error_code=coap.Code.RES_UNAUTHORIZED)
        # Write
        self.write_instance(server=self.servers[1], oid=TEST_OID, iid=1, partial=True)
        self.write_instance(server=self.servers[0], oid=TEST_OID, iid=1, partial=True,
                            expect_error_code=coap.Code.RES_UNAUTHORIZED)
        # Execute
        self.execute_resource(server=self.servers[1], oid=TEST_OID, iid=1, rid=TEST_RID_INCREMENT_COUNTER)
        # Delete
        self.delete_instance(server=self.servers[1], oid=TEST_OID, iid=1)


class NoDuplicatedAclTest(AccessControl.Test):
    def runTest(self):
        self.create_instance(server=self.servers[1], oid=TEST_OID)
        self.update_access(server=self.servers[1], oid=TEST_OID, iid=1,
                           acl=[make_acl_entry(2, ACCESS_MASK_OWNER),
                                make_acl_entry(2, 0),
                                make_acl_entry(2, ACCESS_MASK_OWNER)],
                           expected_acl=[make_acl_entry(2, ACCESS_MASK_OWNER)])

        ac_iid = self.find_access_control_instance(server=self.servers[1], oid=TEST_OID, iid=1)

        res = self.read_resource(server=self.servers[1], oid=ACCESS_CONTROL_OID, iid=ac_iid, rid=ACCESS_CONTROL_RID_ACL)
        self.assertEqual(coap.ContentFormat.APPLICATION_LWM2M_TLV, res.get_content_format())

        tlv = TLV.parse(res.content)
        # One Multiple Resource
        self.assertEquals(len(tlv), 1)
        # with valid ID
        self.assertEquals(tlv[0].identifier, ACCESS_CONTROL_RID_ACL)
        # with exactly one Instance (2,ACCESS_MASK_OWNER)
        self.assertEquals(len(tlv[0].value), 1)
        self.assertEquals(tlv[0].value[0].value, ACCESS_MASK_OWNER.to_bytes(1, byteorder='big'))


class DefaultAclTest(AccessControl.Test):
    def test_default_acl():
        self.create_instance(server=self.servers[1], oid=TEST_OID)
        self.update_access(server=self.servers[1], oid=TEST_OID, iid=1,
                           acl=[make_acl_entry(0, ACCESS_MASK_EXECUTE),
                                make_acl_entry(2, ACCESS_MASK_OWNER)])

        # Now SSID 1 should be able to execute because he obtains access from the
        # default ACL (with ID=0)
        self.execute_resource(server=self.servers[0], oid=TEST_OID, iid=1,
                              rid=TEST_RID_INCREMENT_COUNTER)

        # Now we take away the rights and make sure SSID1 can not do execute
        self.update_access(server=self.servers[1], oid=TEST_OID, iid=1,
                           acl=[make_acl_entry(2, ACCESS_MASK_OWNER)])

        self.execute_resource(server=self.servers[0], oid=TEST_OID, iid=1, rid=0,
                              expect_error_code=coap.Code.RES_UNAUTHORIZED)


class ReadObjectWithPartialReadAccessTest(AccessControl.Test):
    def runTest(self):
        self.create_instance(server=self.servers[1], oid=TEST_OID)  # IID=1
        self.create_instance(server=self.servers[1], oid=TEST_OID)  # IID=2
        self.create_instance(server=self.servers[1], oid=TEST_OID)  # IID=3

        # IID=1 will be readable by SSID=1 because of default ACL
        self.update_access(server=self.servers[1], oid=TEST_OID, iid=1,
                           acl=[make_acl_entry(0, ACCESS_MASK_READ),
                                make_acl_entry(2, ACCESS_MASK_OWNER)])
        # IID=2 will not be readable by SSID=1
        self.update_access(server=self.servers[1], oid=TEST_OID, iid=2,
                           acl=[make_acl_entry(0, ACCESS_MASK_EXECUTE),
                                make_acl_entry(2, ACCESS_MASK_OWNER)])
        # IID=3 will be readable by SSID=1 because of direct ACL entry
        self.update_access(server=self.servers[1], oid=TEST_OID, iid=3,
                           acl=[make_acl_entry(1, ACCESS_MASK_READ),
                                make_acl_entry(2, ACCESS_MASK_OWNER)])

        # SSID=2 should see all three Instances
        res = self.read_object(server=self.servers[1], oid=TEST_OID)
        self.assertEqual(coap.ContentFormat.APPLICATION_LWM2M_TLV, res.get_content_format())

        tlv = TLV.parse(res.content)
        expected_instances = set([1, 2, 3])
        self.assertEquals(len(tlv), len(expected_instances))
        for instance in tlv:
            self.assertEquals(instance.tlv_type, TLVType.INSTANCE)
            expected_instances.remove(instance.identifier)
        self.assertEquals(len(expected_instances), 0)

        # And SSID=1 should see only IID=1 and IID=3
        res = self.read_object(server=self.servers[0], oid=TEST_OID)
        self.assertEqual(coap.ContentFormat.APPLICATION_LWM2M_TLV, res.get_content_format())

        tlv = TLV.parse(res.content)
        expected_instances = set([1, 3])
        self.assertEquals(len(tlv), len(expected_instances))
        for instance in tlv:
            self.assertEquals(instance.tlv_type, TLVType.INSTANCE)
            expected_instances.remove(instance.identifier)
        self.assertEquals(len(expected_instances), 0)


class ReadObjectWithNoInstancesTest(AccessControl.Test):
    def runTest(self):
        res = self.read_object(server=self.servers[1], oid=TEST_OID)
        self.assertEqual(coap.ContentFormat.APPLICATION_LWM2M_TLV, res.get_content_format())

        tlv = TLV.parse(res.content)
        self.assertEquals(len(tlv), 0)
        self.create_instance(server=self.servers[1], oid=TEST_OID)
        self.create_instance(server=self.servers[1], oid=TEST_OID)
        self.create_instance(server=self.servers[1], oid=TEST_OID)

        res = self.read_object(server=self.servers[0], oid=TEST_OID)
        self.assertEqual(coap.ContentFormat.APPLICATION_LWM2M_TLV, res.get_content_format())

        tlv = TLV.parse(res.content)
        self.assertEquals(len(tlv), 0)


class ActionOnNonexistentInstanceTest(AccessControl.Test):
    def runTest(self):
        # Every instance action on nonexistent instance shall return NOT_FOUND
        self.read_instance(server=self.servers[0], oid=TEST_OID, iid=2,
                           expect_error_code=coap.Code.RES_NOT_FOUND)
        self.delete_instance(server=self.servers[0], oid=TEST_OID, iid=2,
                             expect_error_code=coap.Code.RES_NOT_FOUND)
        self.write_instance(server=self.servers[0], oid=TEST_OID, iid=2,
                            expect_error_code=coap.Code.RES_NOT_FOUND)
        self.execute_resource(server=self.servers[0], oid=TEST_OID, iid=2, rid=1,
                              expect_error_code=coap.Code.RES_NOT_FOUND)


class RemovingAcoInstanceFailsTest(AccessControl.Test):
    def runTest(self):
        self.create_instance(server=self.servers[1], oid=TEST_OID)
        ac_iid = self.find_access_control_instance(self.servers[1], oid=TEST_OID, iid=1)
        self.delete_instance(server=self.servers[1], oid=ACCESS_CONTROL_OID, iid=ac_iid,
                             expect_error_code=coap.Code.RES_UNAUTHORIZED)


class EveryoneHasReadAccessToAcoInstancesTest(AccessControl.Test):
    def runTest(self):
        self.read_instance(server=self.servers[0], oid=ACCESS_CONTROL_OID, iid=1)
        self.read_instance(server=self.servers[1], oid=ACCESS_CONTROL_OID, iid=1)


class UnbootstrappingOnlyOneOwnerTest(AccessControl.Test):
    def setUp(self):
        super().setUp(num_servers=3)

    def runTest(self):
        self.create_instance(server=self.servers[2], oid=TEST_OID)
        ac_iid = self.find_access_control_instance(server=self.servers[2], oid=TEST_OID, iid=1)
        assert ac_iid
        # Deleting server shall delete ACO Instance and /TEST_OID/1 instance too.
        self.communicate('trim-servers 2')
        self.assertDemoDeregisters(self.servers[2])
        del(self.servers[2])

        assert not self.find_access_control_instance(server=self.servers[1], oid=TEST_OID, iid=1,
                                                     expect_existence=False)
        self.read_instance(server=self.servers[1], oid=TEST_OID, iid=1,
                           expect_error_code=coap.Code.RES_NOT_FOUND)


class UnbootstrappingOwnerElection(AccessControl.Test):
    def setUp(self):
        super().setUp(num_servers=3)

    def runTest(self):
        self.create_instance(server=self.servers[2], oid=TEST_OID)
        self.update_access(server=self.servers[2], oid=TEST_OID, iid=1,
                           acl=[make_acl_entry(1, ACCESS_MASK_WRITE | ACCESS_MASK_DELETE),
                                make_acl_entry(2, ACCESS_MASK_WRITE | ACCESS_MASK_EXECUTE),
                                make_acl_entry(3, ACCESS_MASK_OWNER)])
        self.communicate('trim-servers 2')
        self.assertDemoDeregisters(self.servers[2])
        del(self.servers[2])
        ac_iid = self.find_access_control_instance(server=self.servers[1], oid=TEST_OID, iid=1)

        # SSID=1 shall win the election
        res = self.read_resource(self.servers[1],
                                 ACCESS_CONTROL_OID, ac_iid, ACCESS_CONTROL_RID_OWNER)
        self.assertEqual(b'1', res.content)

        serv3 = Lwm2mServer()
        self.communicate('add-server coap://127.0.0.1:%d' % serv3.get_listen_port())
        self.assertDemoRegisters(serv3)

        self.create_instance(server=serv3, oid=TEST_OID)
        self.update_access(server=serv3, oid=TEST_OID, iid=2,
                           acl=[make_acl_entry(1, ACCESS_MASK_WRITE | ACCESS_MASK_EXECUTE),
                                make_acl_entry(2, ACCESS_MASK_WRITE | ACCESS_MASK_DELETE),
                                make_acl_entry(3, ACCESS_MASK_OWNER)])
        self.communicate('trim-servers 2')
        self.assertDemoDeregisters(serv3)

        ac_iid = self.find_access_control_instance(server=self.servers[1], oid=TEST_OID, iid=2)
        # SSID=2 shall win the election now
        res = self.read_resource(self.servers[1],
                                 ACCESS_CONTROL_OID, ac_iid, ACCESS_CONTROL_RID_OWNER)
        self.assertEqual(b'2', res.content)


class AclActiveDespiteOnlyOneServerSuccessfullyConnected(AccessControl.Test):
    def setUp(self):
        super().setUp(auto_register=False)

    def runTest(self):
        self.assertDemoRegisters(self.servers[0])

        # reject the Register on second server
        second_register = self.servers[1].recv()
        self.assertIsInstance(second_register, Lwm2mRegister)
        self.servers[1].send(Lwm2mReset.matching(second_register)())

        # SSID 1 has no rights, even though it's the only properly connected server
        self.read_resource(server=self.servers[0], oid=SERVER_OID, iid=1, rid=RID.Server.ShortServerID,
                           expect_error_code=coap.Code.RES_UNAUTHORIZED)

        # allow the second registration retry to make cleanup easier
        self.servers[1].reset()
        self.assertDemoRegisters(self.servers[1])


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
        self.bootstrap_server.connect(('127.0.0.1', self.get_demo_port()))

        for obj in (OID.Security, OID.Server, OID.AccessControl):
            req = Lwm2mDelete('/%d' % obj)
            self.bootstrap_server.send(req)
            self.assertMsgEqual(Lwm2mDeleted.matching(req)(),
                                self.bootstrap_server.recv())

        # create servers
        self.servers = [self.add_server(1), self.add_server(2)]

        # create test objects
        self.write_instance(self.bootstrap_server, TEST_OID, 42)
        self.write_instance(self.bootstrap_server, TEST_OID, 69)
        self.write_instance(self.bootstrap_server, TEST_OID, 514)

        # create ACLs
        self.write_instance(self.bootstrap_server, OID.AccessControl, 7,
                            TLV.make_resource(RID.AccessControl.TargetOID, TEST_OID).serialize()
                            + TLV.make_resource(RID.AccessControl.TargetIID, 514).serialize()
                            + TLV.make_multires(RID.AccessControl.ACL, {2: 7}.items()).serialize()
                            + TLV.make_resource(RID.AccessControl.Owner, 2).serialize())
        self.write_instance(self.bootstrap_server, OID.AccessControl, 9,
                            TLV.make_resource(RID.AccessControl.TargetOID, TEST_OID).serialize()
                            + TLV.make_resource(RID.AccessControl.TargetIID, 42).serialize()
                            + TLV.make_multires(RID.AccessControl.ACL, {1: 7}.items()).serialize()
                            + TLV.make_resource(RID.AccessControl.Owner, 1).serialize())

        # check that those are the only ACLs currently in data model
        self.assertIn(b'</2>,</2/7>,</2/9>,</3>', self.discover(self.bootstrap_server).content)

        # send Bootstrap Finish
        req = Lwm2mBootstrapFinish()
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv(timeout_s=1))

        self.assertDemoRegisters(self.servers[0])
        self.assertDemoRegisters(self.servers[1])

        # SSID 1 rights
        self.read_resource(server=self.servers[0], oid=TEST_OID, iid=42, rid=0)
        self.read_resource(server=self.servers[0], oid=TEST_OID, iid=69, rid=0,
                           expect_error_code=coap.Code.RES_UNAUTHORIZED)
        self.read_resource(server=self.servers[0], oid=TEST_OID, iid=514, rid=0,
                           expect_error_code=coap.Code.RES_UNAUTHORIZED)

        # SSID 2 rights
        self.read_resource(server=self.servers[1], oid=TEST_OID, iid=42, rid=0,
                           expect_error_code=coap.Code.RES_UNAUTHORIZED)
        self.read_resource(server=self.servers[1], oid=TEST_OID, iid=69, rid=0,
                           expect_error_code=coap.Code.RES_UNAUTHORIZED)
        self.read_resource(server=self.servers[1], oid=TEST_OID, iid=514, rid=0)

        # check that more ACLs were created
        self.assertIn(b'</2/0>', self.discover(self.servers[0], OID.AccessControl).content)
