# -*- coding: utf-8 -*-
#
# Copyright 2017 AVSystem <avsystem@avsystem.com>
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

import unittest

from framework.lwm2m_test import *

from .dm.utils import DataModel, ValueValidator


class Test201_QueryingBasicInformationInPlainTextFormat(DataModel.Test):
    def runTest(self):
        self.test_read(ResPath.Device.Manufacturer, ValueValidator.ascii_string(), coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.Device.ModelNumber, ValueValidator.ascii_string(), coap.ContentFormat.TEXT_PLAIN)
        self.test_read(ResPath.Device.SerialNumber, ValueValidator.ascii_string(), coap.ContentFormat.TEXT_PLAIN)

        # 1. Server has received the requested information and display of the
        # following data to the user is possible:
        #   - Manufacturer
        #   - Model number
        #   - Serial number


class Test203_QueryingBasicInformationInTLVFormat(DataModel.Test):
    def runTest(self):
        tlv_with_string = ValueValidator.tlv_resources(ValueValidator.ascii_string())

        self.test_read(ResPath.Device.Manufacturer, tlv_with_string, coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.test_read(ResPath.Device.ModelNumber, tlv_with_string, coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.test_read(ResPath.Device.SerialNumber, tlv_with_string, coap.ContentFormat.APPLICATION_LWM2M_TLV)

        # 1. Server has received the requested information and display of the
        # following data to the user is possible:
        #   - Manufacturer
        #   - Model number
        #   - Serial number


class Test204_QueryingBasicInformationInJSONFormat(DataModel.Test):
    def runTest(self):
        import json

        class JsonValidator(ValueValidator):
            def validate(self, value_bytes):
                obj = json.loads(value_bytes.decode('utf-8'))
                unexpected_keys = [k for k in obj if k not in ('bn', 'bt', 'e')]
                if unexpected_keys:
                    raise ValueError('unexpected JSON key(s): ' + ', '.join(map(repr, unexpected_keys)))

                base_name = ''
                if 'bn' in obj:
                    try:
                        base_name = str(Lwm2mPath(obj['bn']))
                    except ValueError as e:
                        raise ValueError('not a valid JSON base name path: %r (%s)' % (obj['bn'], e))

                if 'bt' in obj:
                    base_time = obj['bt']
                    if not isinstance(base_time, float) and not isinstance(base_time, int):
                        raise ValueError('not a valid JSON base time (float expected): %r' % (base_time,))

                resource_list = obj['e']
                try:
                    iter(resource_list)
                except TypeError:
                    raise ValueError('not a valid JSON: expected iterable, got %r' % (resource_list,))

                for resource in resource_list:
                    unexpected_keys = [k for k in resource if k not in ('n', 't', 'v', 'bv', 'ov', 'sv')]
                    if unexpected_keys:
                        raise ValueError('unexpected JSON key(s) in e. object: ' + ', '.join(map(repr, unexpected_keys)))

                    if 'n' in resource:
                        full_path = CoapPath(base_name + resource['n'])
                        if len(full_path.segments) > 4:
                            raise ValueError('not a valid JSON response: path too long (%s, base: %s)' % (full_path, base_name))

                        for segment in full_path.segments:
                            try:
                                n = int(segment)
                                if not 0 <= n <= 65535:
                                    raise ValueError('LwM2M path segment not in range [0; 65535]: %s' % (segment,))
                            except ValueError as e:
                                raise ValueError('not an integer: %s' % (segment,), e)

                    if 't' in resource:
                        if not isinstance(resource['t'], float) and not isinstance(resource['t'], int):
                            raise ValueError('not a valid JSON time (float expected): %r' % (resource['t'],))

                    num_value_entries = sum(int(k in ('v', 'bv', 'ov', 'sv')) for k in resource)
                    if num_value_entries != 1:
                        raise ValueError('not a valid JSON: %d value entries in %s, expected one' % (num_value_entries, ', '.join(resource)))

                    if 'v' in resource:
                        if not isinstance(resource['v'], float) and not isinstance(resource['v'], int):
                            raise ValueError('not a valid JSON value (float expected): %r' % (resource['v'],))
                    if 'bv' in resource:
                        if not isinstance(resource['bv'], bool):
                            raise ValueError('not a valid JSON value (boolean expected): %r' % (resource['bv'],))
                    if 'ov' in resource:
                        try:
                            objlnk = [int(x) for x in resource['ov'].split(':')]
                            if len(objlnk) != 2 or not all(0 <= x <= 65535 for x in objlnk):
                                raise ValueError
                        except ValueError:
                            raise ValueError('not a valid JSON value (objlnk expected): %r' % (resource['ov'],))
                    if 'sv' in resource:
                        if not isinstance(resource['sv'], str):
                            raise ValueError('not a valid JSON value (string expected): %r' % (resource['sv'],))

        self.test_read(ResPath.Device.Manufacturer, JsonValidator(),
                       coap.ContentFormat.APPLICATION_LWM2M_JSON)
        self.test_read(ResPath.Device.ModelNumber, JsonValidator(),
                       coap.ContentFormat.APPLICATION_LWM2M_JSON)
        self.test_read(ResPath.Device.SerialNumber, JsonValidator(),
                       coap.ContentFormat.APPLICATION_LWM2M_JSON)

        # 1. Server has received the requested information and display of the
        # following data to the user is possible:
        #   - Manufacturer
        #   - Model number
        #   - Serial number


class Test205_SettingBasicInformationInPlainTextFormat(DataModel.Test):
    def runTest(self):
        # This test has to be run for the following resources:
        #   a) Lifetime
        #   b) Default minimum period
        #   c) Default maximum period
        #   d) Disable timeout
        #   e) Notification storing when disabled or offline
        #   f) Binding

        # 1. The server receives the status code “2.04”
        # 2. The resource value has changed according to WRITE request
        self.test_write_validated(ResPath.Server[1].Lifetime, '123')
        self.test_write_validated(ResPath.Server[1].DefaultMinPeriod, '234')
        self.test_write_validated(ResPath.Server[1].DefaultMaxPeriod, '345')
        self.test_write_validated(ResPath.Server[1].DisableTimeout, '456')
        self.test_write_validated(ResPath.Server[1].NotificationStoring, '1')

        self.test_write(ResPath.Server[1].Binding, 'UQ')
        self.assertDemoUpdatesRegistration(lifetime=123, binding='UQ')  # triggered by Binding change
        self.assertEqual(b'UQ', self.test_read(ResPath.Server[1].Binding))


class Test241_ExecutableResourceRebootingTheDevice(test_suite.Lwm2mSingleServerTest):
    def _get_valgrind_args(self):
        # Reboot cannot be performed when demo is run under valgrind
        return []

    def runTest(self):
        # Server performs Execute (COAP POST) on device/reboot resource
        req = Lwm2mExecute(ResPath.Device.Reboot)
        self.serv.send(req)
        # Server receives success message (2.04 Changed) from client
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        # 1. Device reboots successfully and re-registers at the server again
        self.serv.reset()
        self.assertDemoRegisters(timeout_s=3)
