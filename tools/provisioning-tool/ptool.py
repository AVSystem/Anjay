#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

import argparse
import requests
import sys
from factory_prov import factory_prov as fp


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='Factory provisioning tool')
    parser.add_argument('-c', '--endpoint_cfg', type=str,
                        help='Configuration file containing device information to be loaded on the device',
                        required=True)
    parser.add_argument('-e', '--URN', type=str,
                        help='Endpoint name to use during registration',
                        required=False)
    parser.add_argument('-s', '--server', type=str,
                        help='JSON format file containing Coiote server information',
                        required=False)
    parser.add_argument('-t', '--token', type=str,
                        help='Access token for authorization to Coiote server',
                        required=False)
    parser.add_argument('-C', '--cert', type=str,
                        help='JSON format file containing information for the generation of a self signed certificate',
                        required=False)
    parser.add_argument('-k', '--pkey', type=str,
                        help='Endpoint private key in DER format, ignored if CERT parameter is set',
                        required=False)
    parser.add_argument('-r', '--pcert', type=str,
                        help='Endpoint public cert in DER format, ignored if CERT parameter is set',
                        required=False,)
    parser.add_argument('-p', '--scert', type=str,
                        help='Server public cert in DER format',
                        required=False)

    args = parser.parse_args()

    ret_val = 1

    try:
        fcty = fp.FactoryProvisioning(args.endpoint_cfg, args.URN, args.server,
                                      args.token, args.cert)
        if fcty.get_sec_mode() == 'cert':
            if args.scert is not None:
                fcty.set_server_cert(args.scert)

            if args.cert is not None:
                fcty.generate_self_signed_cert()
            elif args.pkey is not None and args.pcert is not None:
                fcty.set_endpoint_cert_and_key(args.pcert, args.pkey)

        fcty.provision_device()

        if args.server is not None and args.token is not None and args.URN is not None:
            fcty.register()

        ret_val = 0
    except ValueError as err:
        print('Incorrect configuration:', err)
    except ConnectionError as err:
        print('Coiote server error:', err)
    except requests.HTTPError as err:
        print(err)
    except OSError as err:
        print(err)
    except RuntimeError as err:
        print(err)
    except:
        print('Unexpected error, abort script execution')
    finally:
        sys.exit(ret_val)
