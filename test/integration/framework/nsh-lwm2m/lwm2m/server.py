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

from . import coap
from .messages import get_lwm2m_msg


class Lwm2mServer:
    def __init__(self, coap_server=None):
        super().__setattr__('_coap_server', coap_server or coap.Server())
        self.set_timeout(timeout_s=5)

    def send(self, pkt: coap.Packet):
        if not isinstance(pkt, coap.Packet):
            raise ValueError(('pkt is %r, expected coap.Packet; did you forget additional parentheses? ' +
                             'valid syntax: Lwm2mSomething.matching(pkt)()') % (type(pkt),))
        self._coap_server.send(pkt.fill_placeholders())

    def recv(self, timeout_s=-1):
        pkt = self._coap_server.recv(timeout_s=timeout_s)
        return get_lwm2m_msg(pkt)

    def __getattr__(self, name):
        return getattr(self._coap_server, name)

    def __setattr__(self, name, value):
        return setattr(self._coap_server, name, value)

    def __delattr__(self, name):
        return delattr(self._coap_server, name)
