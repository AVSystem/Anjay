from framework.lwm2m_test import *
from framework.lwm2m.tlv import *

import unittest
import socket
import math

from typing import List, Optional

class ValueValidator:
    def validate(self, value):
        """
        Implementations of this method should raise a ValueError on validation
        failure. Any return value is considered correct, any exception other
        than ValueError is propagated up.
        """
        raise NotImplementedError

    @classmethod
    def from_constructor(cls, ctor):
        class Validator(cls):
            def validate(self, value):
                ctor(value)
        return Validator()

    @classmethod
    def from_values(cls, *allowed_values):
        class Validator(cls):
            def validate(self, value):
                if value not in allowed_values:
                    raise ValueError('%s is not a valid value (expected one of: %s)'
                                     % (value, ' '.join(map(str, allowed_values))))
        return Validator()

    @classmethod
    def integer(cls):
        return cls.from_constructor(int)

    @classmethod
    def float(cls):
        return cls.from_constructor(float)

    @classmethod
    def float_as_string(cls):
        class Validator(cls):
            def validate(self, value):
                float(value.decode('ascii'))
        return Validator()

    @classmethod
    def boolean(cls):
        return cls.from_values(b'0', b'1')

    @classmethod
    def ascii_string(cls):
        class Validator(cls):
            def validate(self, value):
                value.decode('ascii')

        return Validator()

    @classmethod
    def multiple_resource(cls, internal_validator):
        class Validator(cls):
            def validate(self, value):
                tlv = TLV.parse(value)
                if len(tlv) != 1 or tlv[0].tlv_type != TLVType.MULTIPLE_RESOURCE:
                    raise ValueError('expected a single Multiple Resource')
                for res_instance in tlv[0].value:
                    if res_instance.tlv_type != TLVType.RESOURCE_INSTANCE:
                        raise ValueError('expected Resource Instance list')
                    internal_validator.validate(res_instance.value)

        return Validator()

    @classmethod
    def from_raw_int(cls):
        class Validator(cls):
            def validate(self, value):
                if len(value) > 8:
                    raise ValueError('raw integer value too long: expected at most 8 bytes, got %d' % len(value))
                try:
                    struct.unpack('>Q', (bytes(8) + value)[-8:])
                except struct.error:
                    raise ValueError('could not unpack raw integer (hex: %s)' % binascii.hexlify(value))

        return Validator()

    @classmethod
    def tlv_multiple_resource(cls, internal_validator):
        class Validator(cls):
            def validate(self, tlv_list):
                for tlv in tlv_list:
                    if tlv.tlv_type != TLVType.RESOURCE_INSTANCE:
                        raise ValueError('not a valid Multiple Resource')
                    internal_validator.validate(tlv.value)

        return Validator()

    @classmethod
    def tlv_resources(cls, *internal_validators):
        class Validator(cls):
            def validate(self, value):
                tlv_list = TLV.parse(value)
                for tlv, validator in zip(tlv_list, internal_validators):
                    validator.validate(tlv.value)

        return Validator()


class DataModel:
    class Test(test_suite.Lwm2mSingleServerTest):
        def test_read(self,
                      path: Lwm2mPath,
                      validator: Optional[ValueValidator]=None,
                      format: Optional[int]=None):
            req = Lwm2mRead(path, accept=format)
            self.serv.send(req)

            res = self.serv.recv()
            self.assertMsgEqual(Lwm2mContent.matching(req)(), res)

            if format is not None:
                if format == coap.ContentFormat.TEXT_PLAIN:
                    # plaintext format is implied, it may be omitted by the client
                    self.assertIn(res.get_content_format(), (None, format))
                else:
                    self.assertEqual(res.get_content_format(), format)

            if validator is not None:
                try:
                    validator.validate(res.content)
                except ValueError:
                    self.fail('invalid value in Read response for %s: %s' % (path, res.content))

            return res.content

        def test_write(self,
                       path: Lwm2mPath,
                       value: str,
                       format: coap.ContentFormat=coap.ContentFormat.TEXT_PLAIN):
            # WRITE (CoAP PUT/POST) on the resource with a value
            # admissible with regards to LWM2M technical specification
            req = Lwm2mWrite(path, value, format=format)
            self.serv.send(req)

            # Server receives success message (2.04 Changed)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                                self.serv.recv())

        def test_write_validated(self,
                                 path: Lwm2mPath,
                                 value: str,
                                 alternative_acceptable_values: List[str]=[]):
            self.test_write(path, value)

            acceptable_values = [v.encode('ascii') for v in [value] + alternative_acceptable_values]
            self.test_read(path, ValueValidator.from_values(*acceptable_values))

        def test_write_block(self,
                             path: Lwm2mPath,
                             payload: bytes,
                             format: coap.ContentFormat,
                             block_size: int=1024):
            offset = 0

            while offset < len(payload):
                new_offset = offset + block_size
                seq_num = offset // block_size

                block = payload[offset:new_offset]
                has_more = (new_offset < len(payload))
                block_opt = coap.Option.BLOCK1(seq_num=seq_num, has_more=has_more, block_size=block_size)

                msg = Lwm2mWrite(path, block, format, options=[block_opt])
                self.serv.send(msg)

                expected_response = Lwm2mContinue if has_more else Lwm2mChanged
                self.assertMsgEqual(expected_response.matching(msg)(), self.serv.recv())

                offset = new_offset

        def test_observe(self,
                         path: Lwm2mPath,
                         validator: ValueValidator,
                         **attributes):
            if not attributes:
                attributes['pmax'] = 1

            # The server communicates to the device min/max period,
            # threshold value and step with a WRITE ATTRIBUTE (CoAP
            # PUT) operation
            req = Lwm2mWriteAttributes(path, **attributes)
            self.serv.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

            req = Lwm2mObserve(path)
            self.serv.send(req)
            res = self.serv.recv()
            self.assertMsgEqual(Lwm2mContent.matching(req)(), res)
            try:
                validator.validate(res.content)
            except ValueError:
                self.fail('invalid value in Observe response for %s: %s' % (path, res.content))

            # Client reports requested information with a NOTIFY message
            # (COAP responses)
            notification = self.serv.recv(timeout_s=(attributes['pmax'] + 0.5))
            self.assertMsgEqual(Lwm2mNotify(token=req.token), notification)
            try:
                validator.validate(notification.content)
            except ValueError:
                self.fail('invalid value in Notify response for %s: %s' % (path, notification.content))

            # Server sends Cancel Observe (COAP RESET message) to
            # cancel the Observation relationship.
            self.serv.send(Lwm2mReset.matching(notification)())

            # Client stops reporting requested information and removes
            # associated entries from the list of observers
            with self.assertRaises(socket.timeout):
                self.serv.recv(timeout_s=2)
