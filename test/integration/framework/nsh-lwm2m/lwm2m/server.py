from . import coap
from .messages import get_lwm2m_msg, Lwm2mMsg

class Lwm2mServer(coap.Server):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.set_timeout(timeout_s=5)

    def send(self, pkt: coap.Packet):
        super().send(pkt.fill_placeholders())

    def recv(self, timeout_s=-1):
        pkt = super().recv(timeout_s=timeout_s)
        return get_lwm2m_msg(pkt)


class Lwm2mDtlsServer(coap.DtlsServer):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.set_timeout(timeout_s=5)

    def send(self, pkt: coap.Packet):
        super().send(pkt.fill_placeholders())

    def recv(self, timeout_s=-1):
        pkt = super().recv(timeout_s=timeout_s)
        return get_lwm2m_msg(pkt)
