class Type(object):
    @staticmethod
    def powercmd_parse(text):
        from powercmd.utils import match_instance

        return match_instance(Type, text)

    @staticmethod
    def powercmd_complete(text):
        from powercmd.utils import get_available_instance_names
        from powercmd.match_string import match_string

        possible = get_available_instance_names(Type)
        return match_string(text, possible)

    def __init__(self, value):
        if value not in range(4):
            raise ValueError("invalid CoAP packet type: %d" % value)

        self.value = value

    def __str__(self):
        return {
            0: 'CONFIRMABLE',
            1: 'NON_CONFIRMABLE',
            2: 'ACKNOWLEDGEMENT',
            3: 'RESET'
        }[self.value]

    def __repr__(self):
        return 'coap.Type.%s' % str(self)

    def __eq__(self, other):
        return (type(self) is type(other)
                and self.value == other.value)

Type.CONFIRMABLE = Type(0)
Type.NON_CONFIRMABLE = Type(1)
Type.ACKNOWLEDGEMENT = Type(2)
Type.RESET = Type(3)
