class ContentFormat(object):
    @staticmethod
    def to_str(fmt):
        for k, v in ContentFormat.__dict__.items():
            if v == fmt:
                return k
        return 'Unknown format (%s)' % (str(fmt),)

    @staticmethod
    def to_repr(fmt):
        for k, v in ContentFormat.__dict__.items():
            if v == fmt:
                return k
        return str(fmt)

ContentFormat.TEXT_PLAIN = 0
ContentFormat.APPLICATION_LINK = 40
ContentFormat.APPLICATION_OCTET_STREAM = 42
ContentFormat.APPLICATION_LWM2M_TEXT_LEGACY = 1541
ContentFormat.APPLICATION_LWM2M_TLV_LEGACY = 1542
ContentFormat.APPLICATION_LWM2M_JSON_LEGACY = 1543
ContentFormat.APPLICATION_LWM2M_OPAQUE_LEGACY = 1544
ContentFormat.APPLICATION_LWM2M_TLV = 11542
ContentFormat.APPLICATION_LWM2M_JSON = 11543
