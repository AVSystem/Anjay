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

import binascii
import enum
import struct
from typing import Optional


@enum.unique
class FirmwareUpdateForcedError(enum.IntEnum):
    NoError = 0
    OutOfMemory = 1
    FailedUpdate = 2
    DelayedSuccess = 3
    DelayedFailedUpdate = 4
    SetSuccessInPerformUpgrade = 5
    SetFailureInPerformUpgrade = 6
    DoNothing = 7


def make_firmware_package(binary: bytes,
                          magic: bytes = b'ANJAY_FW',
                          crc: Optional[int] = None,
                          force_error: FirmwareUpdateForcedError = FirmwareUpdateForcedError.NoError,
                          version: int = 1):
    assert len(magic) == 8

    if crc is None:
        crc = binascii.crc32(binary)

    meta = struct.pack('>8sHHI', magic, version, force_error, crc)
    return meta + binary


if __name__ == '__main__':
    import argparse

    parser = argparse.ArgumentParser(description='Create firmware package from executable.')
    parser.add_argument('-i', '--in-file',
                        help='Path to the input executable. Default: stdin',
                        default='/dev/stdin')
    parser.add_argument('-o', '--out-file',
                        help='Path to save firmware package to. If exists, it will be overwritten. Default: stdout',
                        default='/dev/stdout')
    parser.add_argument('-m', '--magic',
                        type=str,
                        help='Set firmware magic (must be exactly 8 bytes in length). Default: "ANJAY_FW"',
                        default='ANJAY_FW')
    parser.add_argument('-c', '--crc',
                        type=int,
                        help='Override CRC32 checksum value in firmware metadata with given value.',
                        default=None)
    parser.add_argument('-e', '--force-error',
                        type=str,
                        help=('Create a firmware that causes update failure. Possible values: '
                              + ', '.join(e.name for e in FirmwareUpdateForcedError.__members__.values())),
                        default='NoError')
    parser.add_argument('-v', '--version',
                        type=int,
                        help='Set firmware package version.',
                        default=1)

    args = parser.parse_args()
    args.force_error = FirmwareUpdateForcedError.__members__[args.force_error]

    with open(args.in_file, 'rb') as in_file, open(args.out_file, 'wb') as out_file:
        out_file.write(make_firmware_package(in_file.read(),
                                             magic=args.magic.encode('ascii'),
                                             crc=args.crc,
                                             force_error=args.force_error,
                                             version=args.version))
