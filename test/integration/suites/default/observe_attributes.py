from framework.lwm2m_test import *
from framework.lwm2m.tlv import TLV

import socket
from . import access_control as ac

class ObserveAttributesTest(test_suite.Lwm2mSingleServerTest,
                            test_suite.Lwm2mDmOperations):
    def runTest(self):
        # Create object
        self.create_instance(self.serv, oid=1337, iid=0)
        # Observe: Counter
        counter_pkt = self.observe(self.serv, oid=1337, iid=0, rid=1)

        # no message should arrive here
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=5)

        # Write Attributes
        self.write_attributes(self.serv, oid=1337, iid=0, rid=1, query=['pmax=2'])

        # now we should get notifications, even though nothing changed
        pkt = self.serv.recv(timeout_s=3)
        self.assertEqual(pkt.code, coap.Code.RES_CONTENT)
        self.assertEqual(pkt.content, counter_pkt.content)

        # and another one
        pkt = self.serv.recv(timeout_s=3)
        self.assertEqual(pkt.code, coap.Code.RES_CONTENT)
        self.assertEqual(pkt.content, counter_pkt.content)

class ObserveResourceWithEmptyHandler(test_suite.Lwm2mSingleServerTest,
                                      test_suite.Lwm2mDmOperations):
    def runTest(self):
        # See T832. resource_read handler implemented as 'return 0;'
        # used to cause segfault when observed.

        # Create object
        self.create_instance(self.serv, oid=1337, iid=0)

        # Observe: Empty
        self.observe(self.serv, oid=1337, iid=0, rid=5,
                     expect_error_code=coap.Code.RES_INTERNAL_SERVER_ERROR)
        # hopefully that does not segfault

class ObserveWithMultipleServers(ac.AccessControl.Test):
    def runTest(self):
        self.create_instance(server=self.servers[1], oid=1337, iid=0)
        self.update_access(server=self.servers[1], oid=1337, iid=0,
                           acl=[ac.make_acl_entry(1, ac.ACCESS_MASK_READ | ac.ACCESS_MASK_EXECUTE),
                                ac.make_acl_entry(2, ac.ACCESS_MASK_OWNER)])
        # Observe: Counter
        self.observe(self.servers[1], oid=1337, iid=0, rid=1)
        # Expecting silence
        with self.assertRaises(socket.timeout):
            self.servers[1].recv(timeout_s=2)

        self.write_attributes(self.servers[1], oid=1337, iid=0, rid=1, query=['gt=1'])
        with self.assertRaises(socket.timeout):
            self.servers[1].recv(timeout_s=2)

        self.execute_resource(self.servers[0], oid=1337, iid=0, rid=2) # ++counter
        self.execute_resource(self.servers[0], oid=1337, iid=0, rid=2) # ++counter
        pkt = self.servers[1].recv(timeout_s=2)
        self.assertEqual(pkt.code, coap.Code.RES_CONTENT)
        self.assertEqual(pkt.content, b'2')

class ObserveAttributesInheritanceFull(ac.AccessControl.Test):
    def runTest(self):
        self.create_instance(server=self.servers[1], oid=1337, iid=0)
        self.update_access(server=self.servers[1], oid=1337, iid=0,
                           acl=[ac.make_acl_entry(1, ac.ACCESS_MASK_READ | ac.ACCESS_MASK_EXECUTE),
                                ac.make_acl_entry(2, ac.ACCESS_MASK_OWNER)])

        self.write_attributes(self.servers[1], oid=1337, query=['gt=1'])
        self.write_attributes(self.servers[1], oid=1337, iid=0, query=['gt=2'])
        self.write_attributes(self.servers[1], oid=1337, iid=0, rid=1, query=['gt=3'])

        # Observe: Counter
        counter_pkt = self.observe(self.servers[1], oid=1337, iid=0, rid=1)

        self.execute_resource(self.servers[0], oid=1337, iid=0, rid=2) # ++counter
        with self.assertRaises(socket.timeout):
            self.servers[1].recv(timeout_s=1)

        self.execute_resource(self.servers[0], oid=1337, iid=0, rid=2) # ++counter
        with self.assertRaises(socket.timeout):
            self.servers[1].recv(timeout_s=1)

        self.execute_resource(self.servers[0], oid=1337, iid=0, rid=2) # ++counter
        with self.assertRaises(socket.timeout):
            self.servers[1].recv(timeout_s=1)

        self.execute_resource(self.servers[0], oid=1337, iid=0, rid=2) # ++counter

        self.assertEqual(self.servers[1].recv().content, b'4')

class ObserveAttributesInheritanceInstanceLevel(ac.AccessControl.Test):
    def runTest(self):
        self.create_instance(server=self.servers[1], oid=1337, iid=0)
        self.update_access(server=self.servers[1], oid=1337, iid=0,
                           acl=[ac.make_acl_entry(1, ac.ACCESS_MASK_READ | ac.ACCESS_MASK_EXECUTE),
                                ac.make_acl_entry(2, ac.ACCESS_MASK_OWNER)])

        self.write_attributes(self.servers[1], oid=1337, iid=0, query=['gt=2'])

        # Observe: Counter
        counter_pkt = self.observe(self.servers[1], oid=1337, iid=0, rid=1)

        self.execute_resource(self.servers[0], oid=1337, iid=0, rid=2) # ++counter
        with self.assertRaises(socket.timeout):
            self.servers[1].recv(timeout_s=1)

        self.execute_resource(self.servers[0], oid=1337, iid=0, rid=2) # ++counter
        with self.assertRaises(socket.timeout):
            self.servers[1].recv(timeout_s=1)

        self.execute_resource(self.servers[0], oid=1337, iid=0, rid=2) # ++counter
        self.assertEqual(self.servers[1].recv().content, b'3')
