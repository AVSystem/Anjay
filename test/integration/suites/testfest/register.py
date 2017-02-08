from framework.lwm2m_test import *

import unittest

class Test101_InitialRegistration(test_suite.Lwm2mSingleServerTest):
    def setUp(self):
        super().setUp(auto_register=False)

    def runTest(self):
        # Registration message (CoAP POST) is sent from client to server
        pkt = self.serv.recv(timeout_s=1)
        self.assertMsgEqual(
                Lwm2mRegister('/rd?lwm2m=%s&ep=%s&lt=86400' % (DEMO_LWM2M_VERSION, DEMO_ENDPOINT_NAME)),
                pkt)

        # Client receives Success message (2.01 Created) from the server
        self.serv.send(Lwm2mCreated.matching(pkt)(location=self.DEFAULT_REGISTER_ENDPOINT))

        # 1. Server has received REGISTER operation - assertMsgEqual above
        # 2. Server knows the following:
        #    - Endpoint Client Name - assertMsgEqual above
        #    - registration lifetime (optional) - assertMsgEqual above
        #    - LWM2M version (optional)
        #    - binding mode (optional)
        #    - SMS number (optional)

        #    - Objects and Object instances (mandatory and optional objects/object instances)
        self.assertLinkListValid(pkt.content)

        # 3. Client has received "Success" message from server

class Test102_RegistrationUpdate(test_suite.Lwm2mSingleServerTest):
    def setUp(self):
        super().setUp(lifetime=5)

    def runTest(self):
        # Re-Registration message (CoAP PUT) is sent from client to server
        pkt = self.serv.recv(timeout_s=2.5+1)
        self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT,
                                        query=[],
                                        content=b''),
                            pkt)

        # Client receives Success message (2.04 Changed) from the server
        self.serv.send(Lwm2mChanged.matching(pkt)())

        # 1. Server has received REGISTER operation -- ????? TODO: why does Update test expect Register?
        # 2. Server knows the following:
        #    - Endpoint Client Name -- ????? TODO: there's no Endpoint Name in Update
        #    - registration lifetime (optional)
        #    - LWM2M version (optional) -- ????? TODO: there's no LWM2M Version in Update
        #    - binding mode (optional)
        #    - SMS number (optional)
        #    - Objects and Object instances (optional)

        # 3. Client has received "Success" message from server

class Test103_Deregistration(test_suite.Lwm2mSingleServerTest):
    def tearDown(self):
        pass

    def runTest(self):
        self.request_demo_shutdown()

        # Deregistration message (CoAP DELETE) is sent from client to server
        pkt = self.serv.recv()
        self.assertMsgEqual(Lwm2mDeregister(self.DEFAULT_REGISTER_ENDPOINT), pkt)

        # Client receives Success message (2.02 Deleted) from the server
        self.serv.send(Lwm2mDeleted.matching(pkt)())

        # 1. Client is removed from the servers registration database

@unittest.skip("TODO: requires SMS support")
class Test104_RegistrationUpdateTrigger(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        # Binding is set by sending CoAP PUT /1/1/7 with string content 'U' from server to device
        req = Lwm2mWrite('/1/1/7', 'U')
        self.serv.send(req)

        # Server receives Success message (2.04 Changed) from the device
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        # Device registration expires on the server (for test purposes a short registration lifetime could be chosen)
        self.fail('TODO: wat?')

        # Registration Update Trigger message COAP POST 1/8 is sent from server to client via SMS
        self.fail('TODO: simulate Execute /1/1/8 via SMS message')

        # Re-Registration message (CoAP PUT) is sent from client to server via UDP
        pkt = self.serv.recv(timeout_s=2.5+1)
        self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT,
                                        query=[],
                                        content=b''),
                            pkt)

        # Client receives Success message (2.04 Changed) from the server
        self.serv.send(Lwm2mChanged.matching(pkt)())

        # 1. Server has received Register operation via UDP
        # 2. Server knows the following:
        #    - Endpoint Client Name
        query = dict(opt.content_to_str().split('=', maxsplit=2)
                     for opt in pkt.get_options(coap.Option.URI_QUERY))
        self.assertIn('ep', query)

        #    - registration lifetime (optional)
        #    - LWM2M version (optional)
        #    - binding mode (optional)
        #    - SMS number (optional)
        #    - Objects and Object instances (optional)

        # 3. Client has received "Success" message from server - Lwm2mChanged above

@unittest.skip("That test is invalid, check issue on github")
class Test105_InitialRegistrationToBootstrapServer(test_suite.Lwm2mSingleServerTest):
    pass
