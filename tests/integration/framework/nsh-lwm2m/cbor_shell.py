# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

import os
import sys

sys.path = [os.path.dirname(os.path.dirname(os.path.abspath(__file__)))] + sys.path

import powercmd
import typing
from lwm2m.messages import EscapedBytes
from lwm2m.senml_cbor import *

def encode_int(data):
    return { SenmlLabel.VALUE: int(data) }

def encode_double(data):
    return { SenmlLabel.VALUE: float(data) }

def encode_string(data):
    return { SenmlLabel.STRING: str(data, 'ascii') }

def encode_opaque(data):
    return { SenmlLabel.OPAQUE: data }

def encode_objlnk(data):
    return { SenmlLabel.OBJLNK: str(data, 'ascii') }


class ResourceType:
    TYPES = {
        'int': encode_int,
        'double': encode_double,
        'string': encode_string,
        'opaque': encode_opaque,
        'objlnk': encode_objlnk,
    }
    DEFAULT = TYPES['string']

    @staticmethod
    def powercmd_parse(text):
        return ResourceType.TYPES.get(text, ResourceType.DEFAULT)

    @staticmethod
    def powercmd_complete(text):
        from powercmd.match_string import match_string
        return match_string(text, ResourceType.TYPES.keys())


class CBORBuilderShell(powercmd.Cmd):
    def __init__(self, prompt):
        super().__init__()
        self.prompt = prompt
        self.data = []

    def _name_to_idx(self, name):
        for idx, entry in enumerate(self.data):
            if entry[SenmlLabel.NAME] == name:
                return idx
        return -1

    def do_show(self):
        print(CBOR.parse(CBOR.serialize(self.data)), '\n')

    def do_serialize(self):
        print(''.join('\\x%02x' % (c,) for c in CBOR.serialize(self.data)))

    def do_remove(self,
                  name: str):
        idx = self._name_to_idx(name)
        if idx == -1:
            raise ValueError('Invalid name')
        del self.data[idx]

    def do_add_resource(self,
                        name: str,
                        type: ResourceType = ResourceType.DEFAULT,
                        value: EscapedBytes = None,
                        basename: str = None):
        if self._name_to_idx(name) != -1:
            return ValueError('Path with such name already exists')

        entry = {
            SenmlLabel.NAME: name
        }
        if basename is not None:
            entry[SenmlLabel.BASE_NAME] = basename
        if value is not None:
            entry.update(type(value))
        self.data += [ entry ]
