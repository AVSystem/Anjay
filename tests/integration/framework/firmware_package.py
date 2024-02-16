# -*- coding: utf-8 -*-
#
# Copyright 2017-2024 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

import binascii
import enum
import struct
from typing import List, Optional


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
    Defer = 8


LINKED_SLOTS = 8
IMG_VER_STRLEN_MAX = 24  # 255.255.65535.4294967295


def make_firmware_package(binary: bytes,
                          magic: bytes = b'ANJAY_FW',
                          crc: Optional[int] = None,
                          force_error: FirmwareUpdateForcedError = FirmwareUpdateForcedError.NoError,
                          version: int = 1,
                          linked: list = [],
                          pkg_version=b'1.0'):
    assert len(magic) == 8

    if crc is None:
        crc = binascii.crc32(binary)

    if version == 2:
        assert len(linked) <= LINKED_SLOTS
        assert len(pkg_version) <= IMG_VER_STRLEN_MAX
        # Fill remaining linked slots with 0xFF or all slots if nothing provided.
        # Demo understands 0xFF as no linked instance.
        meta = struct.pack(f'>8sHHI{LINKED_SLOTS}sB{len(pkg_version)}s', magic, version, force_error, crc,
                           bytes(linked + [0xFF] * (LINKED_SLOTS - len(linked))), len(pkg_version), pkg_version)
    else:
        assert not linked, 'Linked instances provided with wrong version of package format'
        meta = struct.pack('>8sHHI', magic, version, force_error, crc)

    return meta + binary


def make_multiple_firmware_package(binary: List[bytes],
                                   magic: bytes = b'MULTIPKG',
                                   version: int = 3):
    meta = struct.pack(f'>8sHH{"I" * len(binary)}', magic,
                       version, len(binary), *[len(pkg) for pkg in binary])
    package = meta
    for pkg in binary:
        package += pkg
    return package


if __name__ == '__main__':
    import argparse

    parser = argparse.ArgumentParser(description='Create firmware package from executable. Use -v option to switch '
                                                 'between Firmware Update and Advanced Firmware Update packages.')
    parser.add_argument('-i', '--in-file',
                        help='Path to the input executable. Default: stdin',
                        default='/dev/stdin')
    parser.add_argument('-o', '--out-file',
                        help='Path to save firmware package to. If exists, it will be overwritten. Default: stdout',
                        default='/dev/stdout')
    parser.add_argument('-m', '--magic',
                        type=str,
                        help='Set firmware magic (must be exactly 8 bytes in length). Default: "ANJAY_FW". If Anjay '
                             'demo is used with  Advanced Firmware Update, possible magics are: "AJAY_APP", "AJAY_TEE", '
                             '"AJAYBOOT", "AJAYMODE", which corresponds respectively with instances 1, 2, 3, and 4 of '
                             'object /33629. ',
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
    parser.add_argument('-l', '--linked-instances',
                        type=int,
                        nargs='+',
                        help='Add instances that are linked to package. Possible usage: -l 1 2 3, means that instances '
                             f'/33629/1, /33629/2 and /33629/3 are linked to package. Max amount of instances is {LINKED_SLOTS}.',
                        default=0)
    parser.add_argument('-v', '--version',
                        type=int,
                        help='Set version of firmware package format. Two formats are supported. -v 1 corresponds to standard '
                             'Firmware Update, -v 2 corresponds to Advanced Firmware Update. Setting this argument to any '
                             'other value leds to use version 1, which is default value.',
                        default=1)

    args = parser.parse_args()
    args.force_error = FirmwareUpdateForcedError.__members__[args.force_error]
    linked = args.linked_instances if type(
        args.linked_instances) == list else []
    with open(args.in_file, 'rb') as in_file, open(args.out_file, 'wb') as out_file:
        out_file.write(make_firmware_package(in_file.read(),
                                             magic=args.magic.encode('ascii'),
                                             crc=args.crc,
                                             force_error=args.force_error,
                                             version=args.version,
                                             linked=linked))
