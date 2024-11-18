# -*- coding: utf-8 -*-
#
# Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

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
        if not text.startswith('/'):
            raise ValueError('not a valid LwM2M path: %s' % (text,))

        self.numeric_segment_offset = 0
        if text.count('/') > 1:
            prefix, path = text[1:].split('/', maxsplit=1)

            # Try to detect if the first segement contains a non-numerical value
            if not prefix.isdigit() and prefix != "":
                self.numeric_segment_offset = 1

        super().__init__(text)

        if len(self.segments) > 4 + self.numeric_segment_offset:
            raise ValueError(
                'LwM2M path must not have more than 4 numeric segments and 1 non-numeric prefix')

        for segment in self.segments[self.numeric_segment_offset:]:
            try:
                int(segment)
            except ValueError as e:
                raise ValueError(
                    'LWM2M path segment is not an integer: %s' % (segment,), e)

    # The first segment can hold either a prefix or a object id, this function
    # helps extract the object, instance, resource and resource instance id from
    # the segment list. It is not intended to extract the prefix from the
    # segment list.
    def __get_segment_wrapper(self, idx):
        idx += self.numeric_segment_offset
        return int(self.segments[idx]) if len(self.segments) > idx else None

    @property
    def path_prefix(self):
        return self.segments[0] if self.numeric_segment_offset > 0 else None

    @property
    def object_id(self):
        return self.__get_segment_wrapper(0)

    @property
    def instance_id(self):
        return self.__get_segment_wrapper(1)

    @property
    def resource_id(self):
        return self.__get_segment_wrapper(2)

    @property
    def resource_instance_id(self):
        return self.__get_segment_wrapper(3)


class Lwm2mNonemptyPath(Lwm2mPath):
    def __init__(self, text):
        super().__init__(text)

        if len(self.segments) == 0 + self.numeric_segment_offset:
            raise ValueError('this LWM2M path requires at least Object ID')


class Lwm2mObjectPath(Lwm2mNonemptyPath):
    def __init__(self, text):
        super().__init__(text)

        if len(self.segments) != 1 + self.numeric_segment_offset:
            raise ValueError('not a LWM2M Object path: %s' % (text,))


class Lwm2mInstancePath(Lwm2mNonemptyPath):
    def __init__(self, text):
        super().__init__(text)

        if len(self.segments) != 2 + self.numeric_segment_offset:
            raise ValueError('not a LWM2M Instance path: %s' % (text,))


class Lwm2mResourcePath(Lwm2mNonemptyPath):
    def __init__(self, text):
        super().__init__(text)

        if len(self.segments) != 3 + self.numeric_segment_offset:
            raise ValueError('not a LWM2M Resource path: %s' % (text,))
