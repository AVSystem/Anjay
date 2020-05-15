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
