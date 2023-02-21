# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

from .lwm2m.senml_cbor import *
from .test_utils import *


def make_path(oid, iid, rid, riid=None):
    if riid is None:
        return f'/{oid}/{iid}/{rid}'
    else:
        return f'/{oid}/{iid}/{rid}/{riid}'


def extract_multiresource(oid, iid, rid, multires):
    return [(make_path(oid, iid, rid, riid), value)
            for riid, value in multires.items()]


def extract_resource(oid, iid, rid, value):
    if isinstance(value, dict):
        return extract_multiresource(oid, iid, rid, value)
    else:
        return [(make_path(oid, iid, rid), value)]  # This need to be a list
        # because a multi resource
        # case is also a list


def extract_instance(oid, iid, instance):
    return [
        res_path_and_value
        for rid, value in instance.items()
        for res_path_and_value in extract_resource(oid, iid, rid, value)
    ]


def extract_objects(oid, object):
    return [
        res_path_and_value
        for iid, instance in object.items()
        for res_path_and_value in extract_instance(oid, iid, instance)
    ]


def extract_config(config):
    return [
        res_path_and_value
        for oid, object in config.items()
        for res_path_and_value in extract_objects(oid, object)
    ]


def serialize_config(config):
    serialized = extract_config(config_to_dict(config))
    out = []

    for path, value in serialized:
        if isinstance(value, bool):  # bool is treated as int, so check bool first
            out.append({SenmlLabel.NAME: path, SenmlLabel.BOOL: value})
        elif isinstance(value, int):
            out.append({SenmlLabel.NAME: path, SenmlLabel.VALUE: value})
        elif isinstance(value, str):
            out.append({SenmlLabel.NAME: path, SenmlLabel.STRING: value})
        elif isinstance(value, bytes):
            out.append({SenmlLabel.NAME: path, SenmlLabel.OPAQUE: value})
        elif isinstance(value, Objlink):
            out.append({SenmlLabel.NAME: path, SenmlLabel.OBJLNK: str(value)})
        else:
            raise TypeError('Unsupported data type')

    return CBOR.serialize(out)
