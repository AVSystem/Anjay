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

import inspect
import math
import struct

from .content_format import ContentFormat
from .utils import hexlify_nonprintable


class OptionLike(object):
    def __init__(self, cls, number):
        self.cls = cls
        self.number = number


class Option(OptionLike):
    @staticmethod
    def powercmd_complete(text):
        from powercmd.utils import match_instance, get_available_instance_names
        from powercmd.match_string import match_string

        paren_idx = text.find('(')
        if paren_idx >= 0:
            # argument completion
            opt_name = text[:paren_idx]
            if opt_name == 'Option':
                ctor = Option
            else:
                ctor = match_instance(Option, text[:paren_idx],
                                      match_extra_cls=[OptionConstructor])
                if not ctor:
                    return []
                elif isinstance(ctor, OptionConstructor):
                    ctor = ctor.content_mapper

            sig = inspect.signature(ctor)

            # the '' is a hack to prevent Cmd from actually inserting the completion
            return ['', str(sig)]

        possible = get_available_instance_names(Option,
                                                match_extra_cls=[OptionConstructor],
                                                append_paren_to_callables=True)
        return match_string(text, possible)

    @staticmethod
    def powercmd_parse(text):
        from powercmd.utils import match_instance

        paren_idx = text.find('(')
        if paren_idx >= 0:
            ctor = match_instance(Option, text[:paren_idx],
                                  match_extra_cls=[OptionConstructor])
            arg = eval(text[paren_idx:])
            opt = ctor(*arg) if isinstance(arg, tuple) else ctor(arg)
        else:
            opt = match_instance(Option, text)

        return opt

    def __init__(self, number, content=b''):
        OptionLike.__init__(self, type(self), number)
        self.content = content

    @staticmethod
    def parse_ext_value(short_value, data):
        if short_value < 13:
            return short_value, 0
        elif short_value == 13:
            return 13 + struct.unpack('!B', data[:1])[0], 1
        elif short_value == 14:
            return 13 + 256 + struct.unpack('!H', data[:2])[0], 2
        elif short_value == 15:
            raise ValueError('reserved short value')

    @staticmethod
    def parse(data, prev_opt_number):
        short_delta_length, = struct.unpack('!B', data[:1])
        short_delta = (short_delta_length >> 4) & 0x0F
        short_length = short_delta_length & 0x0F

        at = 1
        number_delta, bytes_parsed = Option.parse_ext_value(short_delta, data[at:at + 2])
        number = prev_opt_number + number_delta
        at += bytes_parsed

        length, bytes_parsed = Option.parse_ext_value(short_length, data[at:at + 2])
        at += bytes_parsed

        if len(data) < at + length:
            raise ValueError('incomplete option')

        content = bytes(data[at:at + length])
        return Option.get_class_by_number(number)(number, content), at + length

    @staticmethod
    def serialize_ext_value(value):
        if value >= 13 + 256:
            return 14, struct.pack('!H', value - 13 - 256)
        elif value >= 13:
            return 13, struct.pack('!B', value - 13)
        else:
            return value, b''

    def serialize(self, prev_opt_number):
        short_delta, ext_delta = Option.serialize_ext_value(self.number - prev_opt_number)
        short_length, ext_length = Option.serialize_ext_value(len(self.content))

        return struct.pack('!B', (short_delta << 4) | short_length) + ext_delta + ext_length + self.content

    @classmethod
    def get_class_by_number(cls, number):
        for opt in cls.__dict__.values():
            if isinstance(opt, OptionLike) and opt.number == number:
                return opt.cls

        return Option

    @classmethod
    def get_name_by_number(cls, number):
        for name, opt in cls.__dict__.items():
            if isinstance(opt, OptionLike) and opt.number == number:
                return name

        return None

    @classmethod
    def get_number_of(cls, ctor):
        for opt in cls.__dict__.values():
            if isinstance(opt, OptionLike) and ctor is opt:
                return opt.number

        return None

    def content_to_str(self):
        return hexlify_nonprintable(self.content)

    def __str__(self):
        opt_name = Option.get_name_by_number(self.number)
        return 'option %d%s, content (%d bytes): %s' % (
            self.number,
            ' (%s)' % (opt_name,) if opt_name else '',
            len(self.content),
            self.content_to_str())

    def __repr__(self):
        opt_name = Option.get_name_by_number(self.number)
        if opt_name:
            return 'coap.Option.%s' % (opt_name,)
        else:
            return 'coap.Option(%d, %s)' % (self.number, repr(self.content))

    def __eq__(self, other):
        return (type(self) is type(other)
                and self.number == other.number
                and self.content == other.content)

    def matches(self, what):
        num = Option.get_number_of(what)
        assert num is not None
        return self.number == num


class IntOption(Option):
    @staticmethod
    def _pad_to_power_of_2_size(val):
        def _min_power_of_2_greater_or_equal(x):
            x -= 1
            x |= x >> 1
            x |= x >> 2
            x |= x >> 4
            x |= x >> 8
            x |= x >> 16
            return x + 1

        min_pot_ge = _min_power_of_2_greater_or_equal(len(val))
        return (b'\0' * (min_pot_ge - len(val))) + val

    def content_to_int(self):
        padded = IntOption._pad_to_power_of_2_size(self.content)

        return {
            0: lambda: 0,
            1: lambda: struct.unpack('!B', padded)[0],
            2: lambda: struct.unpack('!H', padded)[0],
            4: lambda: struct.unpack('!I', padded)[0],
            8: lambda: struct.unpack('!Q', padded)[0]
        }[len(padded)]()

    def content_to_str(self):
        return str(self.content_to_int())

    def __eq__(self, other):
        return (type(self) is type(other)
                and self.number == other.number
                and self.content_to_int() == other.content_to_int())

    def __repr__(self):
        opt_name = Option.get_name_by_number(self.number)
        return 'coap.Option.%s(%s)' % (opt_name, self.content_to_str())


class StringOption(Option):
    def __repr__(self):
        opt_name = Option.get_name_by_number(self.number)
        return 'coap.Option.%s(%s)' % (opt_name, repr(self.content_to_str()))


class ContentFormatOption(IntOption):
    @staticmethod
    def powercmd_complete(text):
        from powercmd.utils import get_available_instance_names
        from powercmd.match_string import match_string

        possible = get_available_instance_names(ContentFormatOption)
        return match_string(text, possible)

    @staticmethod
    def powercmd_parse(text):
        from powercmd.utils import match_instance

        try:
            return Option.CONTENT_FORMAT(int(text))
        except ValueError:
            return match_instance(ContentFormatOption, text)

    def content_to_str(self):
        number = self.content_to_int()
        for name, opt in ContentFormat.__dict__.items():
            if opt == number:
                return '%d (%s)' % (number, name)
        return str(number)

    def __repr__(self):
        opt_name = Option.get_name_by_number(self.number)
        return 'coap.Option.%s(%s)' % (
            opt_name, ContentFormat.to_repr(self.content_to_int()))


class AcceptOption(ContentFormatOption):
    @staticmethod
    def powercmd_parse(text):
        from powercmd.utils import match_instance

        try:
            return Option.ACCEPT(int(text))
        except ValueError:
            return match_instance(AcceptOption, text)


class OptionConstructor(OptionLike):
    def __init__(self, cls, number, content_mapper):
        OptionLike.__init__(self, cls, number)
        self.content_mapper = content_mapper

    def __call__(self, *args, **kwargs):
        return self.cls(self.number, self.content_mapper(*args, **kwargs))


class BlockOption(IntOption):
    def seq_num(self):
        content = self.content_to_int()
        return content >> 4

    def block_size(self):
        content = self.content_to_int()
        return 2 ** (4 + (content & 0x7))

    def has_more(self):
        content = self.content_to_int()
        return bool(content & 0x8)

    def content_to_str(self):
        return 'seq_num=%d, has_more=%d, block_size=%d' % (self.seq_num(),
                                                           self.has_more(),
                                                           self.block_size())


def is_power_of_2(num):
    return ((num & (num - 1)) == 0) and num > 0


def pack_block(seq_num: int,
               has_more: bool,
               block_size: int):
    if (seq_num >= 2 ** 20
            or not is_power_of_2(block_size)
            or not 16 <= block_size <= 2048):
        raise ValueError('invalid arguments')

    szx = round(math.log(block_size, 2)) - 4
    unpacked = (seq_num << 4) | (int(has_more) << 3) | (szx & 0x7)
    packed = struct.pack('!I', unpacked)
    return packed.lstrip(b'\0')


Option.IF_NONE_MATCH   = Option(5)

Option.IF_MATCH        = OptionConstructor(Option, 1, lambda x: x)
Option.ETAG            = OptionConstructor(Option, 4, lambda x: x)

Option.URI_HOST        = OptionConstructor(StringOption, 3,  lambda string: bytes(string, 'ascii'))
Option.LOCATION_PATH   = OptionConstructor(StringOption, 8,  lambda string: bytes(string, 'ascii'))
Option.URI_PATH        = OptionConstructor(StringOption, 11, lambda string: bytes(string, 'ascii'))
Option.URI_QUERY       = OptionConstructor(StringOption, 15, lambda string: bytes(string, 'ascii'))
Option.LOCATION_QUERY  = OptionConstructor(StringOption, 20, lambda string: bytes(string, 'ascii'))
Option.PROXY_URI       = OptionConstructor(StringOption, 35, lambda string: bytes(string, 'ascii'))
Option.PROXY_SCHEME    = OptionConstructor(StringOption, 39, lambda string: bytes(string, 'ascii'))

Option.OBSERVE         = OptionConstructor(IntOption, 6, lambda int8: struct.pack('!B', int8))
Option.URI_PORT        = OptionConstructor(IntOption, 7,  lambda int16: struct.pack('!H', int16))
Option.MAX_AGE         = OptionConstructor(IntOption, 14, lambda int32: struct.pack('!I', int32))
Option.SIZE1           = OptionConstructor(IntOption, 60, lambda int32: struct.pack('!I', int32))

Option.BLOCK1          = OptionConstructor(BlockOption, 27, pack_block)
Option.BLOCK2          = OptionConstructor(BlockOption, 23, pack_block)

Option.OSCORE          = OptionConstructor(StringOption, 9, lambda string: bytes(string, 'ascii'))


def pack_content_format(fmt: ContentFormat):
    return struct.pack('!H', fmt)


Option.CONTENT_FORMAT  = OptionConstructor(ContentFormatOption, 12, pack_content_format)
Option.ACCEPT          = OptionConstructor(AcceptOption,        17, pack_content_format)

for fmt_name, fmt_value in ((k, v) for k, v in ContentFormat.__dict__.items() if isinstance(v, int)):
    setattr(ContentFormatOption, fmt_name, Option.CONTENT_FORMAT(fmt_value))
    setattr(Option.CONTENT_FORMAT, fmt_name, Option.CONTENT_FORMAT(fmt_value))
    setattr(AcceptOption, fmt_name, Option.ACCEPT(fmt_value))
    setattr(Option.ACCEPT, fmt_name, Option.ACCEPT(fmt_value))
