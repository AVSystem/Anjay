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

import operator
import struct
import logging
import textwrap

from .code import Code
from .option import Option
from .type import Type
from .utils import hexlify, hexlify_nonprintable
from .transport import Transport

from collections import namedtuple

Header = namedtuple('Header', ('code', 'version', 'type', 'id', 'token_length'))


class Placeholder:
    def __init__(self, name):
        self.name = name

    def __str__(self):
        return self.name

    def __repr__(self):
        return self.name

    def __getattr__(self, _):
        return self

    def __call__(self, *_, **__):
        return self

    def __iter__(self):
        return iter([])


# A special value that may be used as msg_id, token, options or content to
# indicate that any value is acceptable when comparing it to another message.
ANY = Placeholder('ANY')


class SequentialMsgIdGenerator:
    def __init__(self, start_id):
        self.curr_id = start_id

    def __next__(self):
        return self.next()

    def next(self):
        self.curr_id = (self.curr_id + 1) % 2 ** 16
        return self.curr_id


_ID_GENERATOR = SequentialMsgIdGenerator(0x1337)


class RandomTokenGenerator:
    def __next__(self):
        return self.next()

    @staticmethod
    def next():
        with open('/dev/urandom', 'rb') as f:
            return f.read(8)


_TOKEN_GENERATOR = RandomTokenGenerator()


def _parse_udp_header(packet):
    if len(packet) < 4:
        raise ValueError("invalid CoAP message: %s" % hexlify(packet))
    version_type_token_length, code, msg_id = struct.unpack('!BBH', packet[:4])

    code = Code.from_byte(code)
    version = (version_type_token_length >> 6) & 0x03
    type = Type((version_type_token_length >> 4) & 0x03)
    token_length = version_type_token_length & 0x0F

    if version != 1:
        raise ValueError("invalid CoAP version: %d, expected 1" % version)

    at = 4

    return Header(code, version, type, msg_id, token_length), at

class Packet(object):
    def __init__(self, type=None, code=1, msg_id=0, token=b'', options=None, content=b'', version=1):
        self.version = version
        self.type = type
        self.code = code
        self.msg_id = msg_id
        self.token = token
        self.options = sorted(options or [], key=operator.attrgetter('number')) if options is not ANY else ANY
        if content is ANY or content is None:
            self.content = content
        else:
            self.content = bytes(content)

    def __repr__(self):
        return ('coap.Packet(type=%s,\n'
                '            code=%s,\n'
                '            msg_id=%s,\n'
                '            token=%s,\n'
                '            options=%s,\n'
                '            content=%s,\n'
                '            version=%s)'
                % (repr(self.type),
                   repr(self.code),
                   repr(self.msg_id),
                   repr(self.token),
                   repr(self.options),
                   repr(self.content),
                   self.version))

    def _size_breakdown(self, header):
        serialized_opt_sizes = []
        prev_opt_number = 0
        for o in self.options:
            serialized_opt_sizes.append(len(o.serialize(prev_opt_number)))
            prev_opt_number = o.number

        sizes = {
            'header_size': 4,
            'token_size': len(self.token) if self.token else 0,
            'options_size': sum(serialized_opt_sizes),
            'marker_size': (1 if self.content else 0),
            'payload_size': len(self.content) if self.content else 0
        }

        options_breakdown = [
            '- %d for %s' % (size, opt) for size, opt in zip(serialized_opt_sizes, self.options)
        ]
        return textwrap.dedent('''\
            {header}
            - header:         {header_size:>5}
            - token:          {token_size:>5}
            - options:        {options_size:>5}
            {options_breakdown}
            - payload marker: {marker_size:>5}
            - payload:        {payload_size:>5}
                              -----
            TOTAL             {packet_size:>5}''').format(
                header=header,
                packet_size=sum(sizes.values()),
                options_breakdown=textwrap.indent('\n'.join(options_breakdown), prefix='  '),
                **sizes)

    @staticmethod
    def parse(self, transport=Transport.UDP):
        packet = memoryview(self)
        if transport == Transport.UDP:
            header, offset = _parse_udp_header(packet)
        else:
            raise ValueError("Invalid transport: %r" % (transport,))

        if header.token_length > 8:
            raise ValueError("invalid CoAP token length: %d, expected <= 8" % header.token_length)

        token = packet[offset:offset+header.token_length]
        offset += header.token_length

        options = []
        content = b''

        while offset < len(packet):
            if packet[offset] == 0xFF:
                content = packet[offset + 1:]
                if not content:
                    raise ValueError('payload marker at end of packet is invalid')
                offset = len(packet)
            else:
                opt, bytes_parsed = Option.parse(packet[offset:], options[-1].number if options else 0)
                options.append(opt)
                offset += bytes_parsed

        if offset != len(packet):
            raise ValueError("CoAP packet malformed starting at offset %d: %s" % (offset, hexlify(packet[offset:])))

        pkt = Packet(header.type, header.code, header.id, token, options, content, header.version)
        if transport == Transport.UDP:
            # TODO: add log for TCP
            logging.debug('%s', pkt._size_breakdown('received'))
        return pkt

    def fill_placeholders(self):
        if self.msg_id is ANY:
            self.msg_id = next(_ID_GENERATOR)
        if self.token is ANY:
            self.token = next(_TOKEN_GENERATOR)
        if self.options is ANY:
            self.options = []
        if self.content is ANY:
            self.content = b''

        return self


    def _serialize_udp_header(self):
        return struct.pack('!BBH',
                        (self.version << 6) | (self.type.value << 4) | (len(self.token) & 0xF),
                        self.code.as_byte(),
                        self.msg_id)

    def serialize(self, transport=Transport.UDP):
        if any(x is ANY for x in (self.msg_id, self.token, self.options, self.content)):
            raise ValueError('cannot serialize CoAP packet: placeholder values present')

        content = b'\xFF' + self.content if self.content else b''

        prev_opt_number = 0
        serialized_opts = []
        for o in self.options:
            serialized_opts.append(o.serialize(prev_opt_number))
            prev_opt_number = o.number

        if transport == Transport.UDP:
            logging.debug('%s', self._size_breakdown('sent'))
            data = self._serialize_udp_header()
        else:
            raise ValueError("Invalid transport: %r" % (transport,))

        return (data + self.token + b''.join(serialized_opts) + content)

    def get_options(self, type):
        return [o for o in self.options if o.number == type.number]

    def get_uri_path(self):
        path = []
        for opt in self.options:
            if opt.matches(Option.URI_PATH):
                path.append(opt.content.decode('ascii'))
        return '/' + '/'.join(path)

    def get_location_path(self):
        path = []
        for opt in self.options:
            if opt.matches(Option.LOCATION_PATH):
                path.append(opt.content.decode('ascii'))
        if path:
            return '/' + '/'.join(path)

    def get_content_format(self):
        opts = self.get_options(Option.CONTENT_FORMAT)
        if len(opts) == 0:
            return None
        elif len(opts) != 1:
            raise ValueError('%d Content-Format options found' % (len(opts),))
        return opts[0].content_to_int()

    def get_full_uri(self):
        path = []
        query = []

        for opt in self.options:
            if opt.matches(Option.URI_PATH):
                path.append(opt.content.decode('ascii'))
            elif opt.matches(Option.URI_QUERY):
                query.append(opt.content.decode('ascii'))

        return (('/' + '/'.join(path) if path else '')
                + (('?' + '&'.join(query)) if query else ''))

    def __str__(self):
        if self.token is ANY:
            token_str = str(self.token)
        else:
            token_str = hexlify_nonprintable(self.token) + ' (length: %d)' % (len(self.token),)

        if self.options is ANY:
            options_str = str(self.options)
        else:
            options_str = '\n    ' + '\n    '.join(str(o) for o in self.options)

        return (
            'version: %s\n'
            'type: %s\n'
            'code: %s\n'
            'msg_id: %s\n'
            'token: %s\n'
            'options:%s\n'
            'content: %s\n' % (
                self.version,
                self.type,
                self.code,
                str(self.msg_id),
                token_str,
                options_str,
                ('%d bytes' % len(self.content)) if self.content is not ANY else str(self.content)
            ))

    def __eq__(self, rhs: 'Packet'):
        return ((type(self), self.version, self.type, self.code, self.msg_id,
                 self.token, self.options, self.content)
                == (type(rhs), rhs.version, rhs.type, rhs.code, rhs.msg_id,
                    rhs.token, rhs.options, rhs.content))
