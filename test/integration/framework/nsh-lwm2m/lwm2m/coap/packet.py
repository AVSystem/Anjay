import operator
import struct

from .type import *
from .code import *
from .option import *
from .utils import *

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

"""
A special value that may be used as msg_id, token, options or content to
indicate that any value is acceptable when comparing it to another message.
"""
ANY = Placeholder('ANY')

class SequentialMsgIdGenerator:
    def __init__(self, start_id):
        self.curr_id = start_id

    def __next__(self):
        return self.next()

    def next(self):
        self.curr_id = (self.curr_id + 1) % 2**16
        return self.curr_id

_ID_GENERATOR = SequentialMsgIdGenerator(0x1337)

class RandomTokenGenerator:
    def __next__(self):
        return self.next()

    def next(self):
        with open('/dev/urandom', 'rb') as f:
            return f.read(8)

_TOKEN_GENERATOR = RandomTokenGenerator()

class Packet(object):
    def __init__(self, type, code, msg_id, token, options=[], content=b'', version=1):
        self.version = version
        self.type = type
        self.code = code
        self.msg_id = msg_id
        self.token = token
        self.options = sorted(options, key=operator.attrgetter('number')) if options is not ANY else ANY
        self.content = content

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

    @staticmethod
    def parse(packet):
        if len(packet) < 4:
            raise ValueError("invalid CoAP message: %s" % hexlify(packet))
        version_type_token_length, code, msg_id = struct.unpack('!BBH', packet[:4])

        code = Code.from_byte(code)
        version = (version_type_token_length >> 6) & 0x03
        type = Type((version_type_token_length >> 4) & 0x03)
        token_length = version_type_token_length & 0x0F

        if version != 1:
            raise ValueError("invalid CoAP version: %d, expected 1" % version)
        if token_length > 8:
            raise ValueError("invalid CoAP token length: %d, expected <= 8" % token_length)

        at = 4
        token = packet[at:at+token_length]
        at += token_length

        options = []
        content = b''

        while at < len(packet):
            if packet[at] == 0xFF:
                content = packet[at+1:]
                if not content:
                    raise ValueError('payload marker at end of packet is invalid')
                at = len(packet)
            else:
                opt, bytes_parsed = Option.parse(packet[at:], options[-1].number if options else 0)
                options.append(opt)
                at += bytes_parsed

        if at != len(packet):
            raise ValueError("CoAP packet malformed starting at offset %d: %s" % (at, hexlify(packet[at:])))

        return Packet(type, code, msg_id, token, options, content, version)

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

    def serialize(self):
        if any(x is ANY for x in (self.msg_id, self.token, self.options, self.content)):
            raise ValueError('cannot serialize CoAP packet: placeholder values present')

        content = b'\xFF' + self.content if self.content else b''

        prev_opt_number = 0
        serialized_opts = []
        for o in self.options:
            serialized_opts.append(o.serialize(prev_opt_number))
            prev_opt_number = o.number

        return (struct.pack('!BBH', (self.version << 6) | (self.type.value << 4) | (len(self.token) & 0xF), self.code.as_byte(), self.msg_id)
                + self.token
                + b''.join(serialized_opts)
                + content)

    def get_options(self, type):
        return [o for o in self.options if o.number == type.number]

    def get_uri_path(self):
        path = []
        for opt in self.options:
            if opt.matches(Option.URI_PATH):
                path.append(opt.content.decode('ascii'))
        if path:
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
            'version: %d\n'
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
