# -*- coding: utf-8 -*-
#
# Copyright 2017-2018 AVSystem <avsystem@avsystem.com>
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
