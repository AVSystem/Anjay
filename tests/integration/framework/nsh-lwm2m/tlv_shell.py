# Copyright 2017-2021 AVSystem <avsystem@avsystem.com>
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

from typing import List, Tuple

import powercmd
from lwm2m.messages import EscapedBytes
from lwm2m.tlv import TLV, TLVType


class ResourceType:
    TYPES = {}
    DEFAULT = lambda x: x

    @staticmethod
    def powercmd_parse(text):
        return ResourceType.TYPES.get(text, ResourceType.DEFAULT)

    @staticmethod
    def powercmd_complete(text):
        from powercmd.match_string import match_string
        return match_string(text, ResourceType.TYPES.keys())


ResourceType.TYPES = {
    'int': TLV.encode_int,
    'double': TLV.encode_double
}


class ResourceOrInstancePath:
    """
    TLVBuilderShell helper type used for selecting a Resource, Resource Instance
    or Object Instance meant to be removed from the TLV being built.

    Example:
        '0' - the top-level element of the TLV with id=0.
        '0/1' - the child of the /0 element with id=1. It may be a Resource,
                Multiple Resource or Resource Instance.
        '0/1/2' - Resource Instance with id=2 of the /0/1 element. Such
                  3-segment path can only point to a Resource Instance.
    """

    @staticmethod
    def powercmd_parse(text):
        return ResourceOrInstancePath([int(x) for x in text.split('/')])

    def __init__(self,
                 segments: List[int]):
        if not (1 <= len(segments) <= 3):
            raise ValueError('Resource path must have 1-3 segments, got %s' % (str(segments),))
        self.segments = segments

    def __str__(self):
        return '/'.join(map(str, self.segments))


class ResourceParentPath(ResourceOrInstancePath):
    """
    TLVBuilderShell helper type used for selecting a parent Object Instance or
    Multiple Resource for an newly created elements.

    Note: segments of this path are element indexes, not IDs.

    Example:
        '0' - the first top-level element of the TLV. It may be either an
              Object Instance or a Multiple Resource.
        '0/1' - the second child of the /0 element. Such 2-segment path can
                only point to a Multiple Resource.
    """

    def __init__(self,
                 segments: List[int]):
        if not (1 <= len(segments) <= 2):
            raise ValueError('Resource parent path must have 1-2 segments, got %s' % (str(segments),))
        ResourceOrInstancePath.__init__(self, segments)

class TLVBuilderShell(powercmd.Cmd):
    def __init__(self, prompt):
        super(TLVBuilderShell, self).__init__()

        self.tlv = []
        self.current = None
        self.prompt = prompt
        self._top_level_type = None

        # Returns string corresponding to the level of the given type in TLVTree, which can be used
        # for the comparison of the levels of some TLVType variables and for logging the level.
        self._get_type_level = {
            TLVType.INSTANCE:'instance',
            TLVType.RESOURCE:'resource',
            TLVType.MULTIPLE_RESOURCE:'resource',
            TLVType.RESOURCE_INSTANCE:'resource instance'
        }
        self._complex_types = {
            TLVType.INSTANCE,
            TLVType.MULTIPLE_RESOURCE
        }

    def _display_tlv(self,
                     tlv,
                     path,
                     indent=''):
        select_mark = ' *'[int(tlv is self.current)]
        print('%s %-8s%s%s' % (select_mark, path, indent, tlv.to_string_without_id()))
        if tlv.tlv_type in (TLVType.MULTIPLE_RESOURCE, TLVType.INSTANCE):
            for child in tlv.value :
                self._display_tlv(child, path + '/' + str(child.identifier), indent + '  ')

    def _id_to_idx(self,
                   id,
                   tlv_list):
        for idx, element in enumerate(tlv_list):
            if element.identifier == id:
                return idx
        raise IndexError('Out of range')

    def _get_tlv_by_id(self,
                       id,
                       tlv_list):
        return tlv_list[self._id_to_idx(id, tlv_list)]

    def _contains_id(self,
                     id,
                     tlv_list):
        try:
            self._id_to_idx(id, tlv_list)
            return True
        except IndexError:
            return False

    def _tlv_from_path(self,
                       path):
        try :
            tlv_list = self.tlv
            for id in path.segments:
                result = self._get_tlv_by_id(id, tlv_list)
                tlv_list = result.value
        except Exception:
            raise ValueError('Incorrect path')
        return result

    def do_make_multires(self,
                         content: List[Tuple[int, EscapedBytes]]):
        """
        Builds Multiple Resource Instances from the list of tuples, each
        of form (Instance ID,Value).
        """
        for iid, value in content:
            self.do_add_resource_instance(iid, value)

    def do_show(self):
        """
        Displays current element structure in a human-readable form.
        """
        print('  %-8s%s' % ('path', 'value'))
        print('---------------')
        for tlv in self.tlv:
            self._display_tlv(tlv, str(tlv.identifier))

    def do_serialize(self):
        """
        Displays the prepared strucure as a TLV-encoded hex-escaped string.
        """

        def to_hex(tlv):
            return ''.join('\\x%02x' % (c,) for c in tlv.serialize())

        print(''.join(map(to_hex, self.tlv)))

    def do_deserialize(self,
                       data: EscapedBytes):
        """
        Loads a TLV-encoded element structure for further processing.
        """
        self.tlv = TLV.parse(data)
        self.do_show()

    def _add(self,
             identifier: int,
             tlv_type: TLVType,
             parent_tlv_type: TLVType,
             value: bytes):
        res_tlv = TLV(tlv_type, identifier, value)

        if self._top_level_type is None:
            self._top_level_type = self._get_type_level[tlv_type]
            print('Selected top-level: ' + self._get_type_level[tlv_type])

        if self._get_type_level[tlv_type] == self._top_level_type:
            if self._contains_id(identifier, self.tlv):
                raise ValueError('Element with such ID and parent already exists')
            self.tlv.append(res_tlv)
            if tlv_type in self._complex_types:
                self.current = self.tlv[-1]
        else:
            if self.current == None or self.current.tlv_type != parent_tlv_type:
                raise ValueError('cannot add a %s' % (str(tlv_type),))
            if self._contains_id(identifier, self.current.value):
                raise ValueError('Element with such ID and parent already exists')
            self.current.value.append(res_tlv)

        return res_tlv

    def do_add_resource(self,
                        identifier: int,
                        value: EscapedBytes,
                        type: ResourceType = ResourceType.DEFAULT):
        """
        Creates a Resource under the currently selected Object Instance. If
        there is none, it is created as a top-level element.
        """
        self._add(identifier, TLVType.RESOURCE, TLVType.INSTANCE, type(value))

    def do_add_multiple_resource(self,
                                 identifier: int):
        """
        Creates a Multiple Resource under the currently selected Object
        Instance. If there is none, it is created as a top-level element.
        """
        self.current = self._add(identifier, TLVType.MULTIPLE_RESOURCE, TLVType.INSTANCE, [])

    def do_add_resource_instance(self,
                                 identifier: int,
                                 value: EscapedBytes,
                                 type: ResourceType = ResourceType.DEFAULT):
        """
        Creates a Resource Instance of the currently selected Multiple Resource.
        """
        self._add(identifier, TLVType.RESOURCE_INSTANCE, TLVType.MULTIPLE_RESOURCE, type(value))

    def do_add_instance(self,
                        identifier: int):
        """
        Creates an Object Instance with given IDENTIFIER.

        Object Instances are always created as top-level elements.
        """
        self._add(identifier, TLVType.INSTANCE, None, [])

    def do_remove(self,
                  path: ResourceOrInstancePath):
        """
        Removes an element under the PATH.

        The PATH may consist of 1-3 integers separated by a '/' character.

        Example paths:
        - '5' - the top-level element with id=5,
        - '5/2' - the element with id=2 of the element under 5,
        - '3/1/2' - Resource Instance with id=2 of the Multiple Resource
                    to which ponts the path 5/. The second-level Resource must be a
                    Multiple Resource in this case.
        """
        try:
            if len(path.segments) == 1:
                del self.tlv[self._id_to_idx(path.segments[0], self.tlv)]
            else:
                tlv = self._get_tlv_by_id(path.segments[0], self.tlv)
                for id in path.segments[1:-1]:
                    tlv = tlv.value[self._id_to_idx(id, tlv.value)]
                del tlv.value[self._id_to_idx(path.segments[-1], tlv.value)]
        except IndexError:
            raise ValueError('instance or resource %s does not exist', (path,))

    def do_select(self,
                  path: ResourceParentPath):
        """
        Selects an Object Instance or Multiple Resource that further add_* calls
        will add elements into.

        The PATH consists of 1-2 integers separated by a '/' character.

        Example paths:
        - '4' - Object Instance or Multiple Resource (id=4),
        - '4/1' - Multiple Resource (id=1) inside the first Object Instance (id=4).
        """
        self.current = self._tlv_from_path(path)
        current_tlv_type = self.current.tlv_type
        if current_tlv_type not in (TLVType.MULTIPLE_RESOURCE, TLVType.INSTANCE):
            self.current = None
            raise ValueError('resource %s is %s, cannot be selected'
                                % (path, current_tlv_type))

        print('selected: %s %s' % (path, str(self.current)))
