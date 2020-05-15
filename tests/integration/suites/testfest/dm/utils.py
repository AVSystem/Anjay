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

import socket
import json
import enum
from typing import List, Optional, Mapping, Tuple

from framework.lwm2m.tlv import *
from framework.lwm2m_test import *


class ValueValidator:
    def validate(self, value):
        """
        Implementations of this method should raise a ValueError on validation
        failure. Any return value is considered correct, any exception other
        than ValueError is propagated up.
        """
        raise NotImplementedError

    @classmethod
    def value(cls, expected):
        class Validator(cls):
            def validate(self, value):
                if expected is not None and value != expected:
                    raise ValueError('invalid value: expected %r, got %r'
                                     % (expected, value))
        return Validator()

    @classmethod
    def from_constructor(cls, ctor, expected_value=None):
        class Validator(cls):
            def validate(self, value):
                cls.value(expected_value).validate(ctor(value))

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
    def integer(cls, value: int = None):
        return cls.from_constructor(int, value)

    @classmethod
    def float(cls, expected_value: float = None):
        class Validator(cls):
            def validate(self, value):
                try:
                    if len(value) == 4:
                        v = struct.unpack('>f', value)[0]
                    else:
                        v = struct.unpack('>d', value)[0]

                    cls.value(expected_value).validate(v)
                except struct.error:
                    raise ValueError('could not unpack raw float (hex: %s)' % binascii.hexlify(value))

        return Validator()

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
    def ascii_string(cls, expected_value: str = None):
        class Validator(cls):
            def validate(self, value):
                s = value.decode('ascii')

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
    def from_raw_int(cls, expected_value: int = None):
        class Validator(cls):
            def validate(self, value):
                if len(value) > 8:
                    raise ValueError('raw integer value too long: expected at most 8 bytes, got %d' % len(value))
                try:
                    v = struct.unpack('>Q', (bytes(8) + value)[-8:])[0]
                    if v is not None and v != expected_value:
                        cls.value(expected_value).validate(v)

                except struct.error:
                    raise ValueError('could not unpack raw integer (hex: %s)' % binascii.hexlify(value))

        return Validator()

    @classmethod
    def objlnk(cls, expected_value: Optional[Tuple[int, int]] = None):
        class Validator(cls):
            def validate(self, value):
                try:
                    oid, iid = struct.unpack('>HH', value)

                    if not (0 <= oid <= 65535):
                        raise ValueError('invalid ObjLnk object ID: %r' % segments[0])
                    if not (0 <= iid <= 65534):
                        raise ValueError('invalid ObjLnk instance ID: %r' % segments[1])

                    if expected_value is not None:
                        cls.value(expected_value).validate((oid, iid))
                except struct.error:
                    raise ValueError('could not unpack objlnk (hex: %s)' % binascii.hexlify(value))

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

    @classmethod
    def tlv_instance(cls,
                     resource_validators: Mapping[int, 'Validator'],
                     instance_id: Optional[int] = None,
                     ignore_duplicates: bool = False,
                     ignore_missing: bool = False,
                     ignore_extra: bool = False):
        class Validator(cls):
            def validate(self, value):
                tlv = TLV.parse(value)

                if instance_id is None:
                    resources_tlv = tlv
                else:
                    if len(tlv) != 1:
                        raise ValueError('expected TLV with 1 Object Instance, got '
                                         '%d' % len(tlv))

                    tlv = tlv[0]
                    if tlv.tlv_type != TLVType.INSTANCE:
                        raise ValueError('not an Object Instance TLV')

                    if tlv.identifier != instance_id:
                        raise ValueError('expected Instance ID = %d, got %d'
                                         % (instance_id, tlv.identifier))

                    resources_tlv = tlv.value

                found_rids = set()

                for sub_tlv in resources_tlv:
                    assert sub_tlv.tlv_type in (TLVType.RESOURCE,
                                                TLVType.MULTIPLE_RESOURCE)

                    if sub_tlv.identifier not in resource_validators:
                        if not ignore_extra:
                            raise ValueError('unexpected Resource ID = %d'
                                             % sub_tlv.identifier)
                    else:
                        if sub_tlv.identifier in found_rids:
                            if not ignore_duplicates:
                                raise ValueError('unexpected duplicate Resource'
                                                 'ID = %d' % sub_tlv.identifier)

                        found_rids.add(sub_tlv.identifier)
                        try:
                            if sub_tlv.tlv_type == TLVType.MULTIPLE_RESOURCE:
                                resource_validators[sub_tlv.identifier].validate(sub_tlv.serialize())
                            else:
                                resource_validators[sub_tlv.identifier].validate(sub_tlv.value)
                        except Exception as e:
                            raise ValueError('Validation of resource %d failed'
                                             % sub_tlv.identifier) from e

                not_found = set(resource_validators) - found_rids
                if not_found and not ignore_missing:
                    raise ValueError('Resource IDs not found in TLV: %s'
                                     % ','.join(not_found))

        return Validator()

    @classmethod
    def json(cls):
        class Validator(cls):
            def validate(self, value_bytes):
                obj = json.loads(value_bytes.decode('utf-8'))
                unexpected_keys = [k for k in obj if k not in ('bn', 'bt', 'e')]
                if unexpected_keys:
                    raise ValueError('unexpected JSON key(s): ' + ', '.join(map(repr, unexpected_keys)))

                base_name = ''
                if 'bn' in obj:
                    try:
                        base_name = str(Lwm2mPath(obj['bn']))
                    except ValueError as e:
                        raise ValueError('not a valid JSON base name path: %r' % (obj['bn'],)) from e

                if 'bt' in obj:
                    base_time = obj['bt']
                    if not isinstance(base_time, float) and not isinstance(base_time, int):
                        raise ValueError('not a valid JSON base time (float expected): %r' % (base_time,))

                resource_list = obj['e']
                try:
                    iter(resource_list)
                except TypeError:
                    raise ValueError('not a valid JSON: expected iterable, got %r' % (resource_list,))

                for resource in resource_list:
                    unexpected_keys = [k for k in resource if k not in ('n', 't', 'v', 'bv', 'ov', 'sv')]
                    if unexpected_keys:
                        raise ValueError('unexpected JSON key(s) in e. object: ' + ', '.join(map(repr, unexpected_keys)))

                    if 'n' in resource:
                        full_path = CoapPath(base_name + resource['n'])
                        if len(full_path.segments) > 4:
                            raise ValueError('not a valid JSON response: path too long (%s, base: %s)' % (full_path, base_name))

                        for segment in full_path.segments:
                            try:
                                n = int(segment)
                                if not 0 <= n <= 65535:
                                    raise ValueError('LwM2M path segment not in range [0; 65535]: %s' % (segment,))
                            except ValueError as e:
                                raise ValueError('not an integer: %s' % (segment,)) from e

                    if 't' in resource:
                        if not isinstance(resource['t'], float) and not isinstance(resource['t'], int):
                            raise ValueError('not a valid JSON time (float expected): %r' % (resource['t'],))

                    num_value_entries = sum(int(k in ('v', 'bv', 'ov', 'sv')) for k in resource)
                    if num_value_entries != 1:
                        raise ValueError('not a valid JSON: %d value entries in %s, expected one' % (num_value_entries, ', '.join(resource)))

                    if 'v' in resource:
                        if not isinstance(resource['v'], float) and not isinstance(resource['v'], int):
                            raise ValueError('not a valid JSON value (float expected): %r' % (resource['v'],))
                    if 'bv' in resource:
                        if not isinstance(resource['bv'], bool):
                            raise ValueError('not a valid JSON value (boolean expected): %r' % (resource['bv'],))
                    if 'ov' in resource:
                        try:
                            objlnk = [int(x) for x in resource['ov'].split(':')]
                            if len(objlnk) != 2 or not all(0 <= x <= 65535 for x in objlnk):
                                raise ValueError
                        except ValueError as e:
                            raise ValueError('not a valid JSON value (objlnk expected): %r' % (resource['ov'],)) from e
                    if 'sv' in resource:
                        if not isinstance(resource['sv'], str):
                            raise ValueError('not a valid JSON value (string expected): %r' % (resource['sv'],))

        return Validator()


class CancelObserveMethod(enum.IntEnum):
    DontCancel = 0
    Reset = 1
    ObserveOption = 2


class DataModel:
    class Test(test_suite.Lwm2mSingleServerTest):
        def setUp(self, **kwargs):
            kwargs['extra_cmdline_args'] = (
                    kwargs.get('extra_cmdline_args', [])
                    + ['--security-iid', '0',
                       '--server-iid', '0'])

            super().setUp(**kwargs)

        def test_read(self,
                      path: Lwm2mPath,
                      validator: Optional[ValueValidator] = None,
                      format: Optional[int] = None,
                      server: Optional[Lwm2mServer] = None):
            server = server or self.serv

            req = Lwm2mRead(path, accept=format)
            server.send(req)

            res = server.recv()
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
                except ValueError as e:
                    raise ValueError('invalid value in Read response for %s: %s' % (path, res.content)) from e

            return res.content

        def test_write(self,
                       path: Lwm2mPath,
                       value: str,
                       format: coap.ContentFormat = coap.ContentFormat.TEXT_PLAIN,
                       server: Optional[Lwm2mServer] = None,
                       update: bool = False):
            server = server or self.serv

            # WRITE (CoAP PUT/POST) on the resource with a value
            # admissible with regards to LwM2M technical specification
            req = Lwm2mWrite(path, value, format=format, update=update)
            server.send(req)

            # Server receives success message (2.04 Changed)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                                server.recv())

        def test_write_validated(self,
                                 path: Lwm2mPath,
                                 value: str,
                                 alternative_acceptable_values: List[str] = [],
                                 server: Optional[Lwm2mServer] = None):
            self.test_write(path, value, server=server)

            acceptable_values = [v.encode('ascii') for v in [value] + alternative_acceptable_values]
            self.test_read(path, ValueValidator.from_values(*acceptable_values), server=server)

        def test_write_block(self,
                             path: Lwm2mPath,
                             payload: bytes,
                             format: coap.ContentFormat,
                             block_size: int = 1024,
                             return_on_fail: bool = False,
                             server: Optional[Lwm2mServer] = None):
            server = server or self.serv
            offset = 0

            while offset < len(payload):
                new_offset = offset + block_size
                seq_num = offset // block_size

                block = payload[offset:new_offset]
                has_more = (new_offset < len(payload))
                block_opt = coap.Option.BLOCK1(seq_num=seq_num, has_more=has_more, block_size=block_size)

                msg = Lwm2mWrite(path, block, format, options=[block_opt])
                server.send(msg)

                expected_response = Lwm2mContinue if has_more else Lwm2mChanged
                res = server.recv()
                if return_on_fail and not isinstance(res, expected_response):
                    return res

                self.assertMsgEqual(expected_response.matching(msg)(), res)

                offset = new_offset

        def test_expect_notify(self,
                               token: bytes,
                               validator: ValueValidator,
                               timeout_s: float,
                               server: Optional[Lwm2mServer] = None):
            server = server or self.serv

            notification = server.recv(timeout_s=timeout_s)
            self.assertMsgEqual(Lwm2mNotify(token=token), notification)
            try:
                validator.validate(notification.content)
                return notification
            except ValueError as e:
                raise ValueError('invalid value in Notify response %s' % (notification.content),) from e

        def test_cancel_observe(self,
                                path: Optional[Lwm2mPath] = None,
                                msg_id: Optional[int] = None,
                                token: bytes = None,
                                server: Optional[Lwm2mServer] = None):
            server = server or self.serv

            if path is None and msg_id is None:
                raise ValueError('Either path or msg_id must be specified')

            if path is not None:
                req = Lwm2mObserve(path, observe=1,
                                   **({'token': token} if token is not None else {}))
                server.send(req)
                self.assertMsgEqual(Lwm2mContent.matching(req)(), server.recv())
            elif msg_id is not None:
                server.send(Lwm2mReset(msg_id=msg_id))

        def test_observe(self,
                         path: Lwm2mPath,
                         validator: ValueValidator,
                         server: Optional[Lwm2mServer] = None,
                         cancel_observe: CancelObserveMethod = CancelObserveMethod.Reset,
                         **attributes):
            server = server or self.serv

            if not attributes:
                attributes['pmax'] = 1

            # The server communicates to the device min/max period,
            # threshold value and step with a WRITE ATTRIBUTE (CoAP
            # PUT) operation
            req = Lwm2mWriteAttributes(path, **attributes)
            server.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(), server.recv())

            req = Lwm2mObserve(path)
            server.send(req)
            res = server.recv()
            self.assertMsgEqual(Lwm2mContent.matching(req)(), res)
            try:
                validator.validate(res.content)
            except ValueError as e:
                raise ValueError('invalid value in Observe response for %s: %s' % (path, res.content)) from e

            # Client reports requested information with a NOTIFY message
            # (COAP responses)
            try:
                notification = self.test_expect_notify(token=req.token,
                                                       validator=validator,
                                                       timeout_s=(attributes['pmax'] + 0.5))
            except ValueError as e:
                raise ValueError('invalid value in Notify response for %s' % (path,)) from e

            if cancel_observe == CancelObserveMethod.Reset:
                # Server sends Cancel Observe (COAP RESET message) to
                # cancel the Observation relationship.
                self.test_cancel_observe(msg_id=notification.msg_id, server=server)
            elif cancel_observe == CancelObserveMethod.ObserveOption:
                # Server sends Cancel Observe (COAP GET with Observe=1 option)
                # to cancel the Observation relationship.
                self.test_cancel_observe(path, token=req.token, server=server)

            if cancel_observe != CancelObserveMethod.DontCancel:
                # Client stops reporting requested information and removes
                # associated entries from the list of observers
                with self.assertRaises(socket.timeout):
                    server.recv(timeout_s=2)

        def test_discover(self,
                          path: Lwm2mPath,
                          server: Optional[Lwm2mServer] = None):
            server = server or self.serv

            req = Lwm2mDiscover(path)
            server.send(req)

            res = server.recv()
            self.assertMsgEqual(Lwm2mContent.matching(req)(), res)

            return res.content

        def test_execute(self,
                         path: Lwm2mPath,
                         server: Optional[Lwm2mServer] = None):
            server = server or self.serv

            req = Lwm2mExecute(path)
            server.send(req)
            res = server.recv()
            self.assertMsgEqual(Lwm2mChanged.matching(req)(), res)

            return res.content

        def test_write_attributes(self,
                                  path: Lwm2mPath,
                                  server: Optional[Lwm2mServer] = None,
                                  **kwargs):
            server = server or self.serv

            req = Lwm2mWriteAttributes(path, **kwargs)
            server.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                                server.recv())


        def test_create(self,
                        path: Lwm2mPath,
                        content: bytes,
                        server: Optional[Lwm2mServer] = None):
            server = server or self.serv

            req = Lwm2mCreate(path, content)
            server.send(req)
            res = server.recv()
            self.assertMsgEqual(Lwm2mCreated.matching(req)(), res)

            location = res.get_location_path()
            self.assertTrue(location.startswith(path + '/'))

            return int(location[len(path) + 1:])
