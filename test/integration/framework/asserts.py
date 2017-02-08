from .lwm2m.coap.utils import hexlify_nonprintable
from .lwm2m.messages import *
from .test_utils import DEMO_ENDPOINT_NAME, DEMO_LWM2M_VERSION

from typing import Optional


class Lwm2mAsserts:
    def assertLwm2mPathValid(self, path):
        """
        Convenience assert that checks if a byte-string PATH is in the form
        /0/1/2. The PATH may contain 1-3 16bit integer segments.
        """
        self.assertEqual(b'/'[0], path[0],
                         ('LWM2M path %s does not start with /'
                          % (hexlify_nonprintable(path),)))

        segments = path[1:].split(b'/')
        if len(segments) > 3:
            self.fail(('LWM2M path too long (expected at most 3 segments): %s'
                       % (hexlify_nonprintable(path),)))

        for segment in segments:
            try:
                self.assertTrue(0 <= int(segment.decode('ascii')) <= 2 ** 16 - 1,
                                ('LWM2M path segment not in range [0, 65535] '
                                 'in path %s' % (hexlify_nonprintable(path),)))
            except (ValueError, UnicodeDecodeError):
                self.fail('segment %s is not an integer in link: %s'
                          % (hexlify_nonprintable(segment),
                             hexlify_nonprintable(path)))

    def assertLinkListValid(self, link_list):
        """
        Convenience assert that checks if a byte-string LINK_LIST is in a CoRE
        Link format https://tools.ietf.org/html/rfc6690 and all links are
        valid LWM2M paths.
        """
        if link_list == b'':
            self.fail('empty link list')

        for obj in link_list.split(b','):
            path, *query = obj.split(b';')
            self.assertTrue((len(path) >= len(b'</0>')
                             and path[0] == b'<'[0]
                             and path[-1] == b'>'[0]),
                            'invalid link: %s in %s' % (hexlify_nonprintable(obj),
                                                        hexlify_nonprintable(link_list)))
            self.assertLwm2mPathValid(path[1:-1])
            # TODO: check query strings

    def assertMsgEqual(self, expected, actual, msg=None):
        """
        Convenience assert that checks if ACTUAL Lwm2mMsg object matches
        EXPECTED one.

        The EXPECTED may have its MSG_ID, TOKEN, OPTIONS or CONTENT fields set
        to lwm2m.messages.ANY, in which case the value will not be checked.
        """
        msg_prefix = msg + ': ' if msg else ''

        try:
            self.assertEqual(expected.version, actual.version,
                             msg_prefix + 'unexpected CoAP version')
            self.assertEqual(expected.type, actual.type,
                             msg_prefix + 'unexpected CoAP type')
            self.assertEqual(expected.code, actual.code,
                             msg_prefix + 'unexpected CoAP code')

            if expected.msg_id is not ANY:
                self.assertEqual(expected.msg_id, actual.msg_id,
                                 msg_prefix + 'unexpected CoAP message ID')
            if expected.token is not ANY:
                self.assertEqual(expected.token, actual.token,
                                 msg_prefix + 'unexpected CoAP token')
            if expected.options is not ANY:
                self.assertEqual(expected.options, actual.options,
                                 msg_prefix + 'unexpected CoAP option list')
            if expected.content is not ANY:
                self.assertEqual(expected.content, actual.content,
                                 msg_prefix + 'unexpected CoAP content')
        except AssertionError as e:
            e.args = (e.args[0] + ('\n\n*** Expected ***\n%s\n*** Actual ***\n%s\n'
                                   % (str(expected), str(actual))),) + e.args[1:]
            raise

    DEFAULT_REGISTER_ENDPOINT = '/rd/demo'

    def assertDemoRegisters(self,
                            server=None,
                            version=DEMO_LWM2M_VERSION,
                            location=DEFAULT_REGISTER_ENDPOINT,
                            lifetime=None,
                            timeout_s=2):
        serv = server or self.serv

        pkt = serv.recv(timeout_s=timeout_s)
        expected = Lwm2mRegister('/rd?lwm2m=%s&ep=%s&lt=%d' % (version, DEMO_ENDPOINT_NAME,
                                                               lifetime if lifetime is not None else 86400))
        self.assertMsgEqual(expected, pkt)
        serv.send(Lwm2mCreated(location=location, msg_id=pkt.msg_id, token=pkt.token))

    def assertDemoUpdatesRegistration(self,
                                      server=None,
                                      location=DEFAULT_REGISTER_ENDPOINT,
                                      lifetime: Optional[int]=None,
                                      binding: Optional[str]=None,
                                      sms_number: Optional[str]=None,
                                      content: bytes=b'',
                                      timeout_s: float=1):
        serv = server or self.serv

        query_args = (([('lt', lifetime)] if lifetime is not None else [])
                      + ([('sms', sms_number)] if sms_number is not None else [])
                      + ([('b', binding)] if binding is not None else []))
        query_string = '&'.join('%s=%s' % tpl for tpl in query_args)

        path = location
        if query_string:
            path += '?' + query_string

        pkt = serv.recv(timeout_s=timeout_s)
        self.assertMsgEqual(Lwm2mUpdate(path, content=content), pkt)
        serv.send(Lwm2mChanged.matching(pkt)())

    def assertDemoDeregisters(self, server=None, path=DEFAULT_REGISTER_ENDPOINT, timeout_s=1):
        serv = server or self.serv

        pkt = serv.recv(timeout_s=timeout_s)
        self.assertMsgEqual(Lwm2mDeregister(path), pkt)

        serv.send(Lwm2mDeleted(msg_id=pkt.msg_id, token=pkt.token))
        serv.reset()
