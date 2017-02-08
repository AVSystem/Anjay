from framework.lwm2m_test import *


class Create(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        req = Lwm2mCreate('/12360',  # geo-points object
                          b''.join(map(TLV.serialize,
                                       [TLV.make_resource(0, 42.0),  # latitude
                                        TLV.make_resource(1, 69.0),  # longitude
                                        TLV.make_resource(2, 5.0)])))  # radius
        self.serv.send(req)
        res = self.serv.recv()
        self.assertMsgEqual(Lwm2mCreated.matching(req)(), res)

        # attempt to create "the same" object again
        iid = int(res.get_options(coap.Option.LOCATION_PATH)[1].content)
        req = Lwm2mCreate('/12360',
                          TLV.make_instance(iid,
                                            [TLV.make_resource(0, 14.0),
                                             TLV.make_resource(1, 17.0),
                                             TLV.make_resource(2, 19.0)]).serialize())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(code=coap.Code.RES_BAD_REQUEST),
                            self.serv.recv())


class IncompleteCreate(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        req = Lwm2mCreate('/12360',  # geo-points object
                          b''.join(map(TLV.serialize,
                                       [TLV.make_resource(0, 42.0),  # latitude
                                        TLV.make_resource(1, 69.0)])))  # longitude
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(code=coap.Code.RES_BAD_REQUEST),
                            self.serv.recv())


class UnsupportedCreate(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        req = Lwm2mCreate('/12360',  # geo-points object
                          b''.join(map(TLV.serialize,
                                       [TLV.make_resource(0, 42.0),
                                        TLV.make_resource(1, 69.0),
                                        TLV.make_resource(2, 5.0),
                                        TLV.make_resource(42, b'hello')])))  # unrecognized
        self.serv.send(req)
        res = self.serv.recv()
        self.assertMsgEqual(Lwm2mCreated.matching(req)(), res)

        req = Lwm2mRead(res.get_location_path())
        self.serv.send(req)
        res = self.serv.recv()
        self.assertMsgEqual(Lwm2mContent.matching(req)(
            format=coap.ContentFormat.APPLICATION_LWM2M_TLV,
            content=b''.join(map(TLV.serialize,
                                 [TLV.make_resource(0, 42.0),
                                  TLV.make_resource(1, 69.0),
                                  TLV.make_resource(2, 5.0),
                                  TLV.make_resource(3, b''),
                                  TLV.make_resource(4, 0)]))), res)
