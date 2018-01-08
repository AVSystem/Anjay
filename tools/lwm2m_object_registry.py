#!/usr/bin/env python3
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


import urllib.request
import argparse
import collections
import sys
import os
from xml.etree import ElementTree


class Lwm2mObjectEntry:
    """
    LwM2M Object Registry entry.

    Available attributes are the same as tag names in the DDF XML structure.
    """

    def __init__(self, tree):
        self._tree = tree

    def __getattr__(self, name):
        node = self._tree.find(name)
        if node is not None:
            return node.text.strip()
        return self._tree.get(name)


def _read_url(url: str) -> bytes:
    # we need to change the User-Agent - default one causes the server
    # to respond with 403 Forbidden
    req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
    with urllib.request.urlopen(req) as f:
        return f.read()


class Lwm2mObjectRegistry:
    def __init__(self, ddf_url='http://www.openmobilealliance.org/wp/OMNA/LwM2M/DDF.xml'):
        root = ElementTree.fromstring(_read_url(ddf_url))
        entries = (Lwm2mObjectEntry(obj) for obj in root.findall('Item'))

        self.objects = collections.OrderedDict(sorted((int(entry.ObjectID), entry) for entry in entries))


def _print_object_list():
    for oid, obj in Lwm2mObjectRegistry().objects.items():
        print('%d\t%s' % (oid, obj.Name))


def _print_object_definition(urn_or_oid):
    urn = urn_or_oid.strip()
    if urn.startswith('urn:oma:lwm2m:'):
        oid = int(urn.split(':')[-1])
    else:
        oid = int(urn)

    try:
        object_ddf_url = Lwm2mObjectRegistry().objects[oid].DDF
        print(_read_url(object_ddf_url).decode('utf-8-sig'))
    except KeyError:
        raise ValueError('Object with ID = %d not found' % oid)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="Accesses LwM2M Object registry.")
    parser.add_argument("--get-xml", type=str, metavar='urn_or_oid', help="Get Object definition XML by URN or ID")
    parser.add_argument("--list", action='store_true', help="List all registered LwM2M Objects")

    args = parser.parse_args()

    if args.list and args.get_xml is not None:
        print('conflicting options: --list, --get-xml')
        sys.exit(1)

    if args.list:
        _print_object_list()
    elif args.get_xml is not None:
        _print_object_definition(args.get_xml)
    else:
        parser.print_usage()
        sys.exit(1)
