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

import collections
from typing import Optional

from .lwm2m.messages import *
from .test_utils import DEMO_ENDPOINT_NAME
from framework.lwm2m.coap.transport import Transport


class Lwm2mAsserts:
    def assertLwm2mPathValid(self, path):
        """
        Convenience assert that checks if a byte-string PATH is in the form
        /0/1/2. The PATH may contain 1-3 16bit integer segments.
        """
        self.assertEqual('/', path[0],
                         ('LwM2M path %r does not start with /' % (path,)))

        segments = path[1:].split('/')
        if len(segments) > 3:
            self.fail('LwM2M path too long (expected at most 3 segments): %r' % (path,))

        for segment in segments:
            try:
                self.assertTrue(0 <= int(segment) <= 2 ** 16 - 1,
                                ('LwM2M path segment not in range [0, 65535] '
                                 'in path %r' % (path,)))
            except ValueError:
                self.fail('segment rs is not an integer in link: %r' % (segment, path))

    def assertLinkListValid(self, link_list):
        """
        Convenience assert that checks if a byte-string LINK_LIST is in a CoRE
        Link format https://tools.ietf.org/html/rfc6690 and all links are
        valid LwM2M paths.
        """
        if link_list == '':
            self.fail('empty link list')

        for obj in link_list.split(','):
            path, *query = obj.split(';')
            self.assertTrue((len(path) >= len('</0>')
                             and path[0] == '<'
                             and path[-1] == '>'),
                            'invalid link: %r in %r' % (obj, link_list))
            self.assertLwm2mPathValid(path[1:-1])
            # TODO: check query strings

    def assertMsgEqual(self, expected, actual, msg=None):
        """
        Convenience assert that checks if ACTUAL Lwm2mMsg object matches
        EXPECTED one.

        ACTUAL and EXPECTED may have their MSG_ID, TOKEN, OPTIONS or CONTENT
        fields set to lwm2m.messages.ANY, in which case the value will not
        be checked.
        """
        msg_prefix = msg + ': ' if msg else ''

        try:
            if actual.version is not None:
                self.assertEqual(expected.version, actual.version,
                                 msg_prefix + 'unexpected CoAP version')
            if actual.type is not None:
                self.assertEqual(expected.type, actual.type,
                                 msg_prefix + 'unexpected CoAP type')
            self.assertEqual(expected.code, actual.code,
                             msg_prefix + 'unexpected CoAP code')

            if expected.msg_id is not ANY and actual.msg_id is not ANY and actual.msg_id is not None:
                self.assertEqual(expected.msg_id, actual.msg_id,
                                 msg_prefix + 'unexpected CoAP message ID')
            if expected.token is not ANY and actual.token is not ANY:
                self.assertEqual(expected.token, actual.token,
                                 msg_prefix + 'unexpected CoAP token')
            if expected.options is not ANY and actual.options is not ANY:
                self.assertEqual(expected.options, actual.options,
                                 msg_prefix + 'unexpected CoAP option list')
            if expected.content is not ANY and actual.content is not ANY:
                self.assertEqual(expected.content, actual.content,
                                 msg_prefix + 'unexpected CoAP content')
        except AssertionError as e:
            e.args = (e.args[0] + ('\n\n*** Expected ***\n%s\n*** Actual ***\n%s\n'
                                   % (str(expected), str(actual))),) + e.args[1:]
            raise

    DEFAULT_REGISTER_ENDPOINT = '/rd/demo'

    @staticmethod
    def _expected_register_message(version, endpoint, lifetime, binding, lwm2m11_queue_mode):
        # Note: the specific order of Uri-Query options does not matter, but
        # our packet equality comparator does not distinguish betwen "ordered"
        # and "unordered" options, so we expect a specific order of these
        # query-strings. dict() does not guarantee the order of items until
        # 3.7, so because we want to still work on 3.5, an explicitly ordered
        # list is used instead.
        query = [
            'lwm2m=%s' % (version,),
            'ep=%s' % (endpoint,),
            'lt=%s' % (lifetime if lifetime is not None else 86400,)
        ]
        if binding is not None:
            query.append('b=%s' % (binding,))
        if lwm2m11_queue_mode:
            query.append('Q')
        return Lwm2mRegister('/rd?' + '&'.join(query))

    def assertDemoRegisters(self,
                            server=None,
                            version='1.0',
                            location=DEFAULT_REGISTER_ENDPOINT,
                            endpoint=DEMO_ENDPOINT_NAME,
                            lifetime=None,
                            timeout_s=2,
                            respond=True,
                            binding=None,
                            lwm2m11_queue_mode=False,
                            reject=False):
        # passing a float instead of an integer results in a disaster
        # (serializes as e.g. lt=4.0 instead of lt=4), which makes the
        # assertion fail
        if lifetime is not None:
            self.assertIsInstance(lifetime, int, msg="lifetime MUST be an integer")

        serv = server or self.serv

        pkt = serv.recv(timeout_s=timeout_s)
        self.assertMsgEqual(self._expected_register_message(version, endpoint, lifetime, binding, lwm2m11_queue_mode), pkt)
        self.assertIsNotNone(pkt.content)
        self.assertGreater(len(pkt.content), 0)
        if respond:
            if reject:
                serv.send(Lwm2mErrorResponse(code=coap.Code.RES_UNAUTHORIZED, msg_id=pkt.msg_id, token=pkt.token))
            else:
                serv.send(Lwm2mCreated(location=location, msg_id=pkt.msg_id, token=pkt.token))
        return pkt

    def assertDemoUpdatesRegistration(self,
                                      server=None,
                                      location=DEFAULT_REGISTER_ENDPOINT,
                                      lifetime: Optional[int] = None,
                                      binding: Optional[str] = None,
                                      sms_number: Optional[str] = None,
                                      content: bytes = b'',
                                      timeout_s: float = 1,
                                      respond: bool = True):
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
        if respond:
            serv.send(Lwm2mChanged.matching(pkt)())
        return pkt

    def assertDemoDeregisters(self, server=None, path=DEFAULT_REGISTER_ENDPOINT, timeout_s=2, reset=True):
        serv = server or self.serv

        pkt = serv.recv(timeout_s=timeout_s)
        self.assertMsgEqual(Lwm2mDeregister(path), pkt)

        serv.send(Lwm2mDeleted(msg_id=pkt.msg_id, token=pkt.token))
        if reset:
            serv.reset()

    def assertDemoRequestsBootstrap(self, uri_path='', uri_query=None, respond_with_error_code=None,
                                    endpoint=DEMO_ENDPOINT_NAME, timeout_s=-1, preferred_content_format=None):
        pkt = self.bootstrap_server.recv(timeout_s=timeout_s)
        self.assertMsgEqual(Lwm2mRequestBootstrap(endpoint_name=endpoint,
                                                  preferred_content_format=preferred_content_format,
                                                  uri_path=uri_path,
                                                  uri_query=uri_query), pkt)
        if respond_with_error_code is None:
            self.bootstrap_server.send(Lwm2mChanged.matching(pkt)())
        else:
            self.bootstrap_server.send(Lwm2mErrorResponse.matching(
                pkt)(code=respond_with_error_code))


    def assertDtlsReconnect(self, server=None, timeout_s=1):
        serv = server or self.serv

        with self.assertRaises(RuntimeError) as raised:
            serv.recv(timeout_s=timeout_s)
        self.assertIn('0x6780', raised.exception.args[0])  # -0x6780 == MBEDTLS_ERR_SSL_CLIENT_RECONNECT

    def assertPktIsDtlsClientHello(self, pkt, seq_number=ANY):
        if seq_number is not ANY and seq_number >= 2 ** 48:
            raise RuntimeError(
                "Sorry, encoding of sequence number greater than 2**48 - 1 is not supported")

        allowed_headers = set()
        for version in (b'\xfe\xfd', b'\xfe\xff'):  # DTLS v1.0 or DTLS v1.2
            header = b'\x16'  # Content Type: Handshake
            header += version
            header += b'\x00\x00'  # Epoch: 0
            if seq_number is not ANY:
                # Sequence number is 48bit in length.
                header += seq_number.to_bytes(48 // 8, byteorder='big')
            allowed_headers.add(header)

        self.assertIn(pkt[:len(next(iter(allowed_headers)))], allowed_headers)
