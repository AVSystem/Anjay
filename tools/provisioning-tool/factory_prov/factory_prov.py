#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

import json
import os.path

import requests
from factory_prov.cert_gen import *
from framework import serialize_senml_cbor as ssc
from framework.lwm2m.coap.server import SecurityMode
from framework.test_utils import *

SupportedSecMode = [
    SecurityMode.PreSharedKey,
    SecurityMode.Certificate,
    SecurityMode.NoSec
]


class CoioteRegistration:
    def __init__(self, srv_info):
        self.addr = srv_info.get('url', 'https://eu.iot.avsystem.cloud') + \
                    ':' + \
                    str(srv_info.get('port', 8087))

        if 'access_token' not in srv_info:
            raise ValueError(
                'Missing access token to connect to Coiote DM API')
        self.token = srv_info['access_token']

        if 'endpoint_name' not in srv_info:
            raise ValueError('Missing endpint name in configuration')
        self.endpoint_name = srv_info['endpoint_name']

        if 'domain' not in srv_info:
            raise ValueError('Missing domain in configuration')
        self.domain = srv_info['domain']

        self.sec_mode = srv_info.get('sec_mode', 'nosec')
        self.pk_id = srv_info.get('pk_identity', None)
        self.pkey = srv_info.get('pkey', None)

    def register(self):
        API = '/api/coiotedm/v3/devices'
        hex_psk = None
        if self.pkey is not None:
            hex_psk = {'HexadecimalPsk': self.pkey.hex()}

        headers = {
            'Authorization': 'Bearer ' + self.token,
            'Accept': 'application/json'
        }

        request = {
            'properties': {
                'endpointName': self.endpoint_name
            },
            'connectorType': 'management',
            'domain': self.domain,
            'securityMode': self.sec_mode,
            'dtlsIdentity': self.pk_id,
            'dtlsPsk': hex_psk
        }

        resp = requests.post(url=self.addr + API,
                             headers=headers, json=request)
        if resp.ok:
            print('Device "%s" successfully registered' % self.endpoint_name)
        elif resp.status_code in [400, 403, 404, 409, 429, 503]:
            raise ConnectionError(
                f'{resp.status_code} ' + resp.json().get('error', 'No error message'))
        elif 401 <= resp.status_code < 600:
            resp.raise_for_status()
        else:
            raise ConnectionError(f'response: {resp.status_code}')


class FactoryProvisioning:
    def __init__(self,
                 endpoint_cfg,
                 endpoint_name,
                 server_info,
                 token,
                 cert_info):
        if server_info is not None:
            with open(server_info, 'r') as file:
                self.srv_info = json.load(file)
        else:
            self.srv_info = None

        self.endpoint_name = endpoint_name
        self.access_token = token

        # cfg_dict is used as a helper for easy extraction of resources
        self.cfg_dict = {}

        with open(endpoint_cfg, 'r') as file:
            self.endpoint_cfg = file.read()
            self.cfg_dict = config_to_dict(self.endpoint_cfg)

            self.sec_mode = self.__extract_sec_mode()
            if self.sec_mode == SecurityMode.PreSharedKey.value:
                self.pk_id = self.__extract_pk_id()
                self.pkey = self.__extract_pkey()

        if cert_info is not None:
            if os.path.isfile(cert_info):
                with open(cert_info, 'r') as file:
                    self.cert_info = json.load(file)
        else:
            self.cert_info = None

        # private/public certificates
        self.server_cert = None
        self.endpoint_cert = None
        self.endpoint_key = None

    def __extract_resource(self, oid, rid):
        if oid in self.cfg_dict:
            list_of_inst = list(self.cfg_dict[oid].values())
            if len(list_of_inst) != 1:
                raise ValueError(
                    'Invalid number of instances of object /%d. Only one instance supported.' % oid)
            instance = list_of_inst[0]
            if rid in instance:
                return instance[rid]
        raise ValueError(
            'Missing /%d/*/%d resource in endpoint configuration' % (oid, rid))

    def __extract_sec_mode(self):
        retval = self.__extract_resource(OID.Security, RID.Security.Mode)
        if retval in [v.value for v in SupportedSecMode]:
            return retval
        else:
            raise ValueError(
                'Unsupported Security Mode in endpoint configuration')

    def __extract_pkey(self):
        retval = self.__extract_resource(OID.Security, RID.Security.SecretKey)
        if len(retval) > 0:
            return retval
        else:
            raise ValueError('Security Object Secret Key resource is empty')

    def __extract_pk_id(self):
        retval = self.__extract_resource(
            OID.Security, RID.Security.PKOrIdentity)
        if len(retval) > 0:
            return retval
        else:
            raise ValueError(
                'Security Object Private Key ID resource is empty')

    def __serialize_config(self):
        return ssc.serialize_config(str(self.cfg_dict))

    def get_sec_mode(self):
        return str(SecurityMode(self.sec_mode))

    def set_server_cert(self, cert):
        if os.path.isfile(cert):
            self.server_cert = cert
        else:
            raise OSError('Missing server cert')

    def set_endpoint_cert_and_key(self, cert, key):
        if os.path.isfile(cert) and os.path.isfile(key):
            self.endpoint_cert = cert
            self.endpoint_key = key
        else:
            raise OSError('Missing endpoint cert/key')

    def generate_self_signed_cert(self):
        if self.cert_info is None:
            raise ValueError('Missing information for certificate generation')

        print('Generating device certificates...')
        try:
            os.mkdir('cert')
        except FileExistsError:
            pass
        cert_dir = os.path.realpath('cert')
        key_filename = os.path.join(cert_dir, 'client_key')
        cert_filename = os.path.join(cert_dir, 'client_cert')

        if 'commonName' in self.cert_info:
            cert_info = self.cert_info
        else:
            cert_info = {**self.cert_info, 'commonName': self.endpoint_name}

        gen_cert_and_key(cert_info, key_filename, cert_filename)
        try:
            self.set_endpoint_cert_and_key(cert_filename + '.der', key_filename + '.der')
            print('Certificates generated')
        except:
            raise RuntimeError('Failed to generate certificate')

    def provision_device(self):
        print(f'Security Mode set to "{SecurityMode(self.sec_mode)}"')

        if self.sec_mode == SecurityMode.Certificate.value:
            list_of_sec_inst = list(self.cfg_dict[0].values())
            if len(list_of_sec_inst) != 1:
                raise ValueError(
                    'Invalid number of Security instances. Requires exactly one instance.')

            sec_inst = list_of_sec_inst[0]

            if self.server_cert is not None:
                print(f'Load server cert: {self.server_cert}')
                with open(self.server_cert, 'rb') as cert:
                    sec_inst[RID.Security.ServerPKOrIdentity] = cert.read()
            else:
                raise ValueError('Missing server cert')

            if self.endpoint_cert is not None:
                print(f'Load endpoint cert: {self.endpoint_cert}')
                with open(self.endpoint_cert, 'rb') as cert:
                    sec_inst[RID.Security.PKOrIdentity] = cert.read()
            else:
                raise ValueError('Missing endpoint cert')

            if self.endpoint_key is not None:
                print(f'Load endpoint key: {self.endpoint_key}')
                with open(self.endpoint_key, 'rb') as cert:
                    sec_inst[RID.Security.SecretKey] = cert.read()
            else:
                raise ValueError('Missing endpoint key')

        print('Serializing endpoint configuration...')
        cbor_blob = self.__serialize_config()
        if len(cbor_blob) > 0:
            print('SenML CBOR blob created')
        else:
            raise RuntimeError('Failed to serialized endpoint configuration')

        # TODO: open device and send blob/certs to the device, for now dump blob to file
        with open('SenMLCBOR', 'wb') as file:
            file.write(cbor_blob)

        print('Load endpoint configuration: SenMLCBOR')

    def register(self):
        if self.srv_info is None:
            raise ValueError(
                'Missing Coiote server information for registration operation')

        if self.access_token is None:
            raise ValueError(
                'Missing access token for authorization to Coiote server')

        if self.endpoint_name is None:
            raise ValueError(
                'Missing endpoint name for registration to Coiote server')

        self.srv_info['sec_mode'] = str(SecurityMode(self.sec_mode))
        self.srv_info['access_token'] = self.access_token
        self.srv_info['endpoint_name'] = self.endpoint_name
        if self.sec_mode == SecurityMode.PreSharedKey.value:
            self.srv_info['pk_identity'] = self.pk_id.decode()
            self.srv_info['pkey'] = self.pkey
        print('Register endpoint to Coiote...')
        CoioteServer = CoioteRegistration(self.srv_info)
        CoioteServer.register()
