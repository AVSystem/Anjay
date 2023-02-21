# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

import cbor2
import collections
import enum
import textwrap

@enum.unique
class SenmlLabel(enum.Enum):
    BASE_TIME = -3
    BASE_NAME = -2
    NAME = 0
    VALUE = 2
    STRING = 3
    BOOL = 4
    TIME = 6
    OPAQUE = 8
    OBJLNK = "vlo"


class CborResourceList(list):
    def __str__(self):
        """
        A list of CBOR map entries corresponding to resources from the payload.
        """
        return 'CBOR (%d elements):\n\n' % len(self) \
                + '\n'.join(textwrap.indent(str(e), '  ') for e in self)

    def verify_values(self, test, expected_value_map):
        """
        Verifies if the list contains all entries from EXPECTED_VALUE_MAP.
        Ignores timestamps.

        Requires passing TEST to make use of assertX().
        """
        path_value = {}
        basename = ''
        for entry in self:
            basename = entry.get(SenmlLabel.BASE_NAME, basename)
            name = entry.get(SenmlLabel.NAME, '')
            for value_type in (SenmlLabel.VALUE,
                               SenmlLabel.STRING,
                               SenmlLabel.OPAQUE,
                               SenmlLabel.BOOL,
                               SenmlLabel.OBJLNK):
                if value_type in entry:
                    path_value[basename + name] = entry[value_type]
                    break

        for path, value in expected_value_map.items():
            test.assertIn(path, path_value)
            test.assertEqual(path_value[path], value)


class CborResource(dict):
    def __init__(self, value):
        # Remap integers, or other raw SenML labels to SenmlLabel instances.
        for k, v in value.items():
            try:
                self[SenmlLabel(k)] = v
            except ValueError:
                self[k] = v


class CBOR:
    @staticmethod
    def parse(data) -> CborResourceList:
        return CborResourceList(CborResource(r) for r in cbor2.loads(data))

    @staticmethod
    def serialize(entries) -> bytes:
        entry_list = []
        for e in entries:
            entry = {}
            for k, v in e.items():
                if k in SenmlLabel:
                    entry[k.value] = v
                else:
                    entry[k] = v
            entry_list += [ entry ]

        return cbor2.dumps(entry_list)
