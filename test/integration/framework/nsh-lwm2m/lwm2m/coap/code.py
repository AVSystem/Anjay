import struct

class Code(object):
    @staticmethod
    def powercmd_parse(text):
        from powercmd.utils import match_instance

        try:
            cls, detail = map(int, text.split('.'))
            return Code(cls, detail)
        except ValueError:
            return match_instance(Code, text)

    @staticmethod
    def powercmd_complete(text):
        from powercmd.utils import get_available_instance_names
        from powercmd.match_string import match_string

        possible = get_available_instance_names(Code)
        return match_string(text, possible)

    def __init__(self, cls, detail):
        if cls < 0 or cls > 7:
            raise ValueError('invalid code class')
        if detail < 0 or cls > 31:
            raise ValueError('invalid code detail')

        self.cls = cls
        self.detail = detail

    @staticmethod
    def parse(data):
        return Code.from_byte(struct.unpack('!B', data[:1]))

    @staticmethod
    def from_byte(val):
        return Code((val >> 5) & 0x7, val & 0x1F)

    def as_byte(self):
        return (self.cls << 5) | self.detail

    def get_name(self):
        for name, code in Code.__dict__.items():
            if (isinstance(code, Code)
                    and self.cls == code.cls
                    and self.detail == code.detail):
                return name
        return None

    def __str__(self):
        name = self.get_name()
        return '%d.%02d%s' % (self.cls, self.detail, ' (%s)' % name if name else '')

    def __repr__(self):
        name = self.get_name()
        if name:
            return 'coap.Code.%s' % (name,)
        else:
            return 'coap.Code("%d.%02d")' % (self.cls, self.detail)

    def __eq__(self, other):
        return (type(self) == type(other)
                and self.cls == other.cls
                and self.detail == other.detail)

Code.EMPTY = Code(0, 0)

Code.REQ_GET =    Code(0, 1)
Code.REQ_POST =   Code(0, 2)
Code.REQ_PUT =    Code(0, 3)
Code.REQ_DELETE = Code(0, 4)

Code.RES_CREATED                    = Code(2,  1)
Code.RES_DELETED                    = Code(2,  2)
Code.RES_VALID                      = Code(2,  3)
Code.RES_CHANGED                    = Code(2,  4)
Code.RES_CONTENT                    = Code(2,  5)
Code.RES_CONTINUE                   = Code(2,  31)
Code.RES_BAD_REQUEST                = Code(4,  0)
Code.RES_UNAUTHORIZED               = Code(4,  1)
Code.RES_BAD_OPTION                 = Code(4,  2)
Code.RES_FORBIDDEN                  = Code(4,  3)
Code.RES_NOT_FOUND                  = Code(4,  4)
Code.RES_METHOD_NOT_ALLOWED         = Code(4,  5)
Code.RES_NOT_ACCEPTABLE             = Code(4,  6)
Code.RES_REQUEST_ENTITY_INCOMPLETE  = Code(4,  8)
Code.RES_PRECONDITION_FAILED        = Code(4, 12)
Code.RES_REQUEST_ENTITY_TOO_LARGE   = Code(4, 13)
Code.RES_UNSUPPORTED_CONTENT_FORMAT = Code(4, 15)
Code.RES_INTERNAL_SERVER_ERROR      = Code(5,  0)
Code.RES_NOT_IMPLEMENTED            = Code(5,  1)
Code.RES_BAD_GATEWAY                = Code(5,  2)
Code.RES_SERVICE_UNAVAILABLE        = Code(5,  3)
Code.RES_GATEWAY_TIMEOUT            = Code(5,  4)
Code.RES_PROXYING_NOT_SUPPORTED     = Code(5,  5)
