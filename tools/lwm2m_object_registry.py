#!/usr/bin/env python3
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


import urllib.request
import argparse
import collections
import logging
import sys
import os
from xml.etree import ElementTree
from itertools import groupby
from operator import attrgetter

class Lwm2mObjectEntry:
    """
    LwM2M Object Registry entry.

    Available attributes are the same as tag names in the DDF XML structure.
    """

    def __init__(self, tree):
        self._tree = tree

    def __getattr__(self, name):
        node = self._tree.find(name)
        if node is not None and node.text is not None:
            return node.text.strip()
        return self._tree.get(name)

    def __lt__(self, other):
        return (self.ObjectID, self.Ver) < (other.ObjectID, other.Ver)


def _read_url(url: str) -> bytes:
    # we need to change the User-Agent - default one causes the server
    # to respond with 403 Forbidden
    req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
    with urllib.request.urlopen(req) as f:
        return f.read()


class Lwm2mObjectRegistry:
    def __init__(self, repo_url='https://raw.githubusercontent.com/OpenMobileAlliance/lwm2m-registry/test'):
        self.repo_url = repo_url
        ddf_url = repo_url + '/DDF.xml'
        root = ElementTree.fromstring(_read_url(ddf_url))
        entries = (Lwm2mObjectEntry(obj) for obj in root.findall('Item'))

        grouped = ((int(key), list(group)) for key, group in groupby(entries, attrgetter('ObjectID')))
        self.objects = collections.OrderedDict(grouped)


def _print_object_list():
    for oid, objs in Lwm2mObjectRegistry().objects.items():
        for obj in objs:
            print('%d\t%s\t%s' % (oid, obj.Ver, obj.Name))


def get_object_definition(urn_or_oid, version):
    urn = urn_or_oid.strip()
    if urn.startswith('urn:oma:lwm2m:'):
        oid = int(urn.split(':')[-1])
    else:
        oid = int(urn)

    try:
        registry = Lwm2mObjectRegistry() 
        objects = registry.objects[oid]
        available_versions_message = 'Available versions for object with ID %d: %s' % (
            oid, ', '.join(str(obj.Ver) for obj in objects))

        if version is None:
            if (len(objects) > 1):
                logging.info('%s; defaulting to maximum available version: %s' % (
                    available_versions_message, max(objects).Ver))
            object_ddf_url = max(objects).DDF
        else:
            object_ddf_url = next(obj for obj in objects if obj.Ver == version).DDF
        if not object_ddf_url:
            raise ValueError("Object with ID = %d doesn't have attached XML definition" % oid)
        if not object_ddf_url.startswith('http'):
            object_ddf_url = registry.repo_url + '/' + object_ddf_url
        return _read_url(object_ddf_url).decode('utf-8-sig')
    except KeyError:
        raise ValueError('Object with ID = %d not found' % oid)
    except StopIteration:
        raise ValueError(available_versions_message)


def _print_object_definition(urn_or_oid, version):
    print(get_object_definition(urn_or_oid, version))


if __name__ == '__main__':
    logging.getLogger().setLevel(logging.INFO)

    parser = argparse.ArgumentParser(description="Accesses LwM2M Object registry")
    parser.add_argument("-l", "--list", action='store_true', help="List all registered LwM2M Objects")
    parser.add_argument("-g", "--get-xml", type=str, metavar='urn_or_oid', help="Get Object definition XML by URN or ID")
    parser.add_argument("-v", "--object-version", metavar='ver', type=str, help=
        "Explicitly choose version of an object if there exists more than one with the same ObjectID. Applicable only "
        "with --get-xml argument. Without --object-version specified, most up to date version is chosen.")

    args = parser.parse_args()

    if args.list and args.get_xml is not None:
        print('conflicting options: --list, --get-xml', file=sys.stderr)
        sys.exit(1)

    if args.object_version is not None and args.get_xml is None:
        print('--object-version option is applicable only with --get-xml', file=sys.stderr)
        sys.exit(1)

    if args.list:
        _print_object_list()
    elif args.get_xml is not None:
        _print_object_definition(args.get_xml, args.object_version)
    else:
        parser.print_usage()
        sys.exit(1)
