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


class CoapPath(object):
    def __init__(self, text):
        if not text.startswith('/'):
            raise ValueError('not a valid CoAP path: %s' % (text,))

        if text == '/':
            self.segments = []
        else:
            self.segments = text[1:].split('/')

    def __str__(self):
        return '/' + '/'.join(self.segments)

    def __repr__(self):
        return '%s(\'%s\')' % (self.__class__.__name__, str(self))

    def to_uri_options(self, opt=coap.Option.URI_PATH):
        return [opt(str(segment)) for segment in self.segments]


class Lwm2mPath(CoapPath):
    def __init__(self, text):
        super().__init__(text)

        if len(self.segments) > 4:
            raise ValueError('LWM2M path must not have more than 4 segments')

        for segment in self.segments:
            try:
                int(segment)
            except ValueError as e:
                raise ValueError('LWM2M path segment is not an integer: %s' % (segment,), e)

    @property
    def object_id(self):
        return int(self.segments[0]) if len(self.segments) > 0 else None

    @property
    def instance_id(self):
        return int(self.segments[1]) if len(self.segments) > 1 else None

    @property
    def resource_id(self):
        return int(self.segments[2]) if len(self.segments) > 2 else None

    @property
    def resource_instance_id(self):
        return int(self.segments[3]) if len(self.segments) > 3 else None


class Lwm2mNonemptyPath(Lwm2mPath):
    def __init__(self, text):
        super().__init__(text)

        if len(self.segments) == 0:
            raise ValueError('this LWM2M path requires at least Object ID')


class Lwm2mObjectPath(Lwm2mNonemptyPath):
    def __init__(self, text):
        super().__init__(text)

        if len(self.segments) != 1:
            raise ValueError('not a LWM2M Object path: %s' % (text,))


class Lwm2mInstancePath(Lwm2mNonemptyPath):
    def __init__(self, text):
        super().__init__(text)

        if len(self.segments) != 2:
            raise ValueError('not a LWM2M Instance path: %s' % (text,))


class Lwm2mResourcePath(Lwm2mNonemptyPath):
    def __init__(self, text):
        super().__init__(text)

        if len(self.segments) != 3:
            raise ValueError('not a LWM2M Resource path: %s' % (text,))
