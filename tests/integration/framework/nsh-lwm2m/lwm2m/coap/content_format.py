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


class ContentFormat(object):
    TEXT_PLAIN = 0
    APPLICATION_LINK = 40
    APPLICATION_OCTET_STREAM = 42
    APPLICATION_CBOR = 60
    APPLICATION_LWM2M_SENML_JSON = 110
    APPLICATION_LWM2M_SENML_CBOR = 112
    APPLICATION_PKCS7_CERTS_ONLY = 281
    APPLICATION_PKCS10 = 286
    APPLICATION_PKIX_CERT = 287
    APPLICATION_LWM2M_TEXT_LEGACY = 1541
    APPLICATION_LWM2M_TLV_LEGACY = 1542
    APPLICATION_LWM2M_JSON_LEGACY = 1543
    APPLICATION_LWM2M_OPAQUE_LEGACY = 1544
    APPLICATION_LWM2M_TLV = 11542
    APPLICATION_LWM2M_JSON = 11543

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

    @staticmethod
    def _iter_formats(selector=lambda f: True):
        class EmptyClass:
            pass
        # Removing common fields, to avoid accidental selection of some internal fields.
        distinct_fields = ContentFormat.__dict__.keys() - EmptyClass.__dict__.keys()
        return (getattr(ContentFormat, field) for field in distinct_fields \
                if field.replace('_', '').isupper() and selector(field))

    @staticmethod
    def iter():
        return ContentFormat._iter_formats()
