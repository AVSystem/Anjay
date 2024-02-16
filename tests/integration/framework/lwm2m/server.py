# -*- coding: utf-8 -*-
#
# Copyright 2017-2024 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

from . import coap
from .messages import get_lwm2m_msg


class Lwm2mServer:
    def __init__(self, coap_server=None):
        super().__setattr__('_coap_server', coap_server or coap.Server())
        self.set_timeout(timeout_s=15)

    def send(self, pkt: coap.Packet):
        if not isinstance(pkt, coap.Packet):
            raise ValueError(('pkt is %r, expected coap.Packet; did you forget additional parentheses? ' +
                             'valid syntax: Lwm2mSomething.matching(pkt)()') % (type(pkt),))
        self._coap_server.send(pkt.fill_placeholders())

    def recv(self, timeout_s: float = -1, deadline=None, filter=None, peek=False):
        lwm2m_filter = None
        if filter is not None:
            lwm2m_filter = lambda pkt: filter(get_lwm2m_msg(pkt))
        pkt = self._coap_server.recv(timeout_s=timeout_s, deadline=deadline, filter=lwm2m_filter,
                                     peek=peek)
        return get_lwm2m_msg(pkt)

    def __getattr__(self, name):
        return getattr(self._coap_server, name)

    def __setattr__(self, name, value):
        return setattr(self._coap_server, name, value)

    def __delattr__(self, name):
        return delattr(self._coap_server, name)
