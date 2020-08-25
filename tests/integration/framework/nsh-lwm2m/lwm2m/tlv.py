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

import struct
import typing
from textwrap import indent


class TLVType:
    def __init__(self, value):
        self.value = value

    def __eq__(self, other):
        if isinstance(other, TLVType):
            return self.value == other.value
        else:
            return self.value == other

    def __str__(self):
        matching = [name for name, value in TLVType.__dict__.items()
                    if isinstance(value, TLVType) and value.value == self.value]

        assert len(matching) == 1
        return matching[0]

    def __hash__(self):
        return hash(str(self))


TLVType.INSTANCE = TLVType(0)
TLVType.RESOURCE_INSTANCE = TLVType(1)
TLVType.MULTIPLE_RESOURCE = TLVType(2)
TLVType.RESOURCE = TLVType(3)


class TLVList(list):
    def __str__(self):
        """
        A list of TLVs used as a result of TLV.parse, with a custom __str__ function
        for convenience.
        """
        return 'TLV (%d elements):\n\n' % len(self) + indent('\n'.join(x.full_description() for x in self), '  ')


class TLV:
    class BytesDispenser:
        def __init__(self, data):
            self.data = data
            self.at = 0

        def take(self, n):
            if self.at + n > len(self.data):
                raise IndexError('attempted to take %d bytes, but only %d available'
                                 % (n, len(self.data) - self.at))

            self.at += n
            return self.data[self.at - n:self.at]

        def bytes_remaining(self):
            return len(self.data) - self.at

    @staticmethod
    def encode_int(data):
        value = int(data)
        for n, modifier in zip([8, 16, 32, 64], ['b', 'h', 'i', 'q']):
            if -2 ** (n - 1) <= value < 2 ** (n - 1):
                return struct.pack('>%s' % modifier, value)

        raise NotImplementedError("integer out of supported range")

    @staticmethod
    def encode_double(data):
        return struct.pack('>d', float(data))

    @staticmethod
    def encode_float(data):
        return struct.pack('>f', float(data))

    @staticmethod
    def make_instance(instance_id: int,
                      content: typing.Iterable['TLV'] = None):
        """
        Creates an Object Instance TLV.
        instance_id -- ID of the Object Instance
        resources   -- serialized list of TLV resources
        """
        return TLV(TLVType.INSTANCE, instance_id, content or [])

    @staticmethod
    def _encode_resource_value(content: int or float or str or bytes):
        if isinstance(content, int):
            content = TLV.encode_int(content)
        elif isinstance(content, float):
            as_float = TLV.encode_float(content)
            if struct.unpack('>f', as_float)[0] == content:
                content = as_float  # single precision is enough
            else:
                content = TLV.encode_double(content)
        elif isinstance(content, str):
            content = content.encode('ascii')

        if not isinstance(content, bytes):
            raise ValueError('Unsupported resource value type: ' + type(content).__name__)

        return content

    @staticmethod
    def make_resource(resource_id: int,
                      content: int or float or str or bytes):
        """
        Creates a Resource TLV.
        resource_id -- ID of the Resource
        content     -- Resource content. If an integer is passed, its U2-encoded
                       form is used. Strings are ASCII-encoded.
        """
        return TLV(TLVType.RESOURCE, resource_id, TLV._encode_resource_value(content))

    @staticmethod
    def make_multires(resource_id, instances):
        """
        Encodes Multiple Resource Instances and their values in TLV

        resource_id -- ID of Resource to be encoded
        instances   -- list of tuples, each of form (Resource Instance ID, Value)
        """
        children = []
        for riid, value in instances:
            children.append(TLV(TLVType.RESOURCE_INSTANCE, int(riid),
                                TLV._encode_resource_value(value)))
        return TLV(TLVType.MULTIPLE_RESOURCE, int(resource_id), children)

    @staticmethod
    def _parse_internal(data):
        type_byte, = struct.unpack('!B', data.take(1))

        tlv_type = TLVType((type_byte >> 6) & 0b11)
        id_field_size = (type_byte >> 5) & 0b1
        length_field_size = (type_byte >> 3) & 0b11

        identifier_bits = b'\x00' + data.take(1 + id_field_size)
        identifier, = struct.unpack('!H', identifier_bits[-2:])

        if length_field_size == 0:
            length = type_byte & 0b111
        else:
            length_bits = b'\x00' * 3 + data.take(length_field_size)
            length, = struct.unpack('!I', length_bits[-4:])

        if tlv_type == TLVType.RESOURCE:
            return TLV(tlv_type, identifier, data.take(length))
        elif tlv_type == TLVType.RESOURCE_INSTANCE:
            return TLV(tlv_type, identifier, data.take(length))
        elif tlv_type == TLVType.MULTIPLE_RESOURCE:
            res_instances = []
            data = TLV.BytesDispenser(data.take(length))
            while data.bytes_remaining() > 0:
                res_instances.append(TLV._parse_internal(data))

            if not all(x.tlv_type == TLVType.RESOURCE_INSTANCE for x in res_instances):
                raise ValueError('not all parsed objects are Resource Instances')

            return TLV(tlv_type, identifier, res_instances)
        elif tlv_type == TLVType.INSTANCE:
            resources = []
            data = TLV.BytesDispenser(data.take(length))
            while data.bytes_remaining() > 0:
                resources.append(TLV._parse_internal(data))

            if data.bytes_remaining() > 0:
                raise ValueError('stray bytes at end of data')

            if not all(x.tlv_type in (TLVType.RESOURCE, TLVType.MULTIPLE_RESOURCE) for x in resources):
                raise ValueError('not all parsed objects are Resources')

            return TLV(tlv_type, identifier, resources)

    @staticmethod
    def parse(data) -> TLVList:
        data = TLV.BytesDispenser(data)

        result = TLVList()
        while data.bytes_remaining() > 0:
            result.append(TLV._parse_internal(data))
        return result

    def __init__(self, tlv_type, identifier, value):
        self.tlv_type = tlv_type
        self.identifier = identifier
        self.value = value

    def serialize(self):
        if self.tlv_type in (TLVType.RESOURCE, TLVType.RESOURCE_INSTANCE):
            data = self.value
        else:
            data = b''.join(x.serialize() for x in self.value)

        type_field = (self.tlv_type.value << 6)

        id_bytes = b''
        if self.identifier < 2 ** 8:
            id_bytes = struct.pack('!B', self.identifier)
        else:
            assert self.identifier < 2 ** 16
            type_field |= 0b100000
            id_bytes = struct.pack('!H', self.identifier)

        len_bytes = b''
        if len(data) < 8:
            type_field |= len(data)
        elif len(data) < 2 ** 8:
            type_field |= 0b01000
            len_bytes = struct.pack('!B', len(data))
        elif len(data) < 2 ** 16:
            type_field |= 0b10000
            len_bytes = struct.pack('!H', len(data))
        else:
            assert len(data) < 2 ** 24
            type_field |= 0b11000
            len_bytes = struct.pack('!I', len(data))[1:]

        return struct.pack('!B', type_field) + id_bytes + len_bytes + data

    def _get_resource_value(self):
        assert self.tlv_type in (TLVType.RESOURCE, TLVType.RESOURCE_INSTANCE)

        value = str(self.value)

        if len(self.value) <= 8:
            value += ' (int: %d' % struct.unpack('>Q', (bytes(8) + self.value)[-8:])[0]

            if len(self.value) == 4:
                value += ', float: %f' % struct.unpack('>f', self.value)[0]
            elif len(self.value) == 8:
                value += ', double: %f' % struct.unpack('>d', self.value)[0]

            value += ')'

        return value

    def __str__(self):
        if self.tlv_type == TLVType.INSTANCE:
            return 'instance %d (%d resources)' % (self.identifier, len(self.value))
        elif self.tlv_type == TLVType.MULTIPLE_RESOURCE:
            return 'multiple resource %d (%d instances)' % (self.identifier, len(self.value))
        elif self.tlv_type == TLVType.RESOURCE_INSTANCE:
            return 'resource instance %d = %s' % (self.identifier, self._get_resource_value())
        elif self.tlv_type == TLVType.RESOURCE:
            return 'resource %d = %s' % (self.identifier, self._get_resource_value())

    def to_string_without_id(self):
        if self.tlv_type == TLVType.INSTANCE:
            return 'instance (%d resources)' % len(self.value)
        elif self.tlv_type == TLVType.MULTIPLE_RESOURCE:
            return 'multiple resource (%d instances)' % len(self.value)
        elif self.tlv_type == TLVType.RESOURCE_INSTANCE:
            return 'resource instance = %s' % self._get_resource_value()
        elif self.tlv_type == TLVType.RESOURCE:
            return 'resource = %s' % self._get_resource_value()

    def __eq__(self, other):
        return (isinstance(other, TLV)
                and self.tlv_type == other.tlv_type
                and self.identifier == other.identifier
                and self.value == other.value)

    def full_description(self):
        if self.tlv_type == TLVType.INSTANCE:
            return ('instance %d (%d resources)\n%s'
                    % (self.identifier, len(self.value), indent('\n'.join(x.full_description() for x in self.value), '  ')))
        elif self.tlv_type == TLVType.MULTIPLE_RESOURCE:
            return ('multiple resource %d (%d instances)\n%s'
                    % (self.identifier, len(self.value), indent('\n'.join(x.full_description() for x in self.value), '  ')))
        else:
            return str(self)
