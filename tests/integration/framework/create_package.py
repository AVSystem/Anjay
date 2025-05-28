# -*- coding: utf-8 -*-
#
# Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

import binascii
import enum
import struct
from typing import List, Optional


class PackageForcedError():
    @enum.unique
    class Firmware(enum.IntEnum):
        NoError = 0
        OutOfMemory = 1
        FailedUpdate = 2
        DelayedSuccess = 3
        DelayedFailedUpdate = 4
        SetSuccessInPerformUpgrade = 5
        SetFailureInPerformUpgrade = 6
        DoNothing = 7
        Defer = 8

    @enum.unique
    class Software(enum.IntEnum):
        NoError = 0
        FailedInstall = 1
        DelayedSuccessInstall = 2
        DelayedFailedInstall = 3
        SuccessInPerformInstall = 4
        SuccessInPerformInstallActivate = 5
        FailureInPerformInstall = 6
        FailureInPerformUninstall = 7
        FailureInPerformActivate = 8
        FailureInPerformDeactivate = 9
        FailureInPerformPrepareForUpdate = 10
        DoNothing = 11


LINKED_SLOTS = 8
IMG_VER_STRLEN_MAX = 24  # 255.255.65535.4294967295

HEADER_VER_FW_SW = 2
HEADER_VER_AFU_SINGLE = 3
HEADER_VER_AFU_MULTI = 4


def _make_package(
        binary: bytes,
        magic: bytes,
        crc: Optional[int],
        force_error: PackageForcedError,
        pkg_version: bytes,
        linked: list):
    assert len(magic) == 8
    assert len(pkg_version) <= IMG_VER_STRLEN_MAX

    if crc is None:
        crc = binascii.crc32(binary)

    if magic == b'ANJAY_SW':
        assert not linked, 'Linked instances provided with wrong magic'
        assert force_error in PackageForcedError.Software, 'Wrong forced error for software package'
        return struct.pack(f'>8sHHIB{len(pkg_version)}s', magic, HEADER_VER_FW_SW, force_error,
                            crc, len(pkg_version), pkg_version) + binary
    elif magic == b'ANJAY_FW':
        assert not linked, 'Linked instances provided with wrong version of package format'
        assert force_error in PackageForcedError.Firmware, 'Wrong forced error for firmware package'
        return struct.pack(f'>8sHHIB{len(pkg_version)}s', magic, HEADER_VER_FW_SW, force_error, 
                           crc, len(pkg_version), pkg_version) + binary
    elif magic in [b'AJAY_APP', b'AJAY_TEE', b'AJAYBOOT', b'AJAYMODE']:
        assert len(linked) <= LINKED_SLOTS
        assert force_error in PackageForcedError.Firmware, 'Wrong forced error for firmware package'
        # Fill remaining linked slots with 0xFF or all slots if nothing provided.
        # Demo understands 0xFF as no linked instance.
        return struct.pack(f'>8sHHI{LINKED_SLOTS}sB{len(pkg_version)}s', magic, HEADER_VER_AFU_SINGLE, force_error,
            crc, bytes(linked + [0xFF] *(LINKED_SLOTS - len(linked))), len(pkg_version), pkg_version) + binary
    else:
        raise ValueError('Unknown magic')


def make_firmware_package(
        binary: bytes,
        magic: bytes = b'ANJAY_FW',
        crc: Optional[int] = None,
        force_error: PackageForcedError.Firmware = PackageForcedError.Firmware.NoError,
        pkg_version: bytes = b'1.0',
        linked: list = []):
    return _make_package(binary, magic, crc, force_error, pkg_version, linked)


def make_software_package(
        binary: bytes,
        crc: Optional[int] = None,
        force_error: PackageForcedError.Software = PackageForcedError.Software.NoError):
    return _make_package(binary, b'ANJAY_SW', crc, force_error, b'1.0', [])


def make_multiple_firmware_package(binary: List[bytes],
                                   magic: bytes = b'MULTIPKG'):
    meta = struct.pack(f'>8sHH{"I" * len(binary)}', magic, HEADER_VER_AFU_MULTI,
                       len(binary), *[len(pkg) for pkg in binary])
    package = meta
    for pkg in binary:
        package += pkg
    return package


if __name__ == '__main__':
    import argparse

    parser = argparse.ArgumentParser(
        description='Create package from executable.')
    parser.add_argument(
        '-i',
        '--in-file',
        help='Path to the input executable. Default: stdin',
        default='/dev/stdin')
    parser.add_argument(
        '-o',
        '--out-file',
        help='Path to save package to. If exists, it will be overwritten. Default: stdout',
        default='/dev/stdout')
    parser.add_argument(
        '-m',
        '--magic',
        type=str,
        help='Set package magic bytes to determine package type. '
        'To create package for Firmware Update set magic to "ANJAY_FW". '
        'To create package for Software Management set magic to "ANJAY_SW".'
        'To create package for Advanced Firmware Update, the following magics'
        'are possible: "AJAY_APP", "AJAY_TEE", "AJAYBOOT", "AJAYMODE", which'
        'corresponds respectively with instances 1, 2, 3, and 4 of object /33629.'
        'Any other magics will be rejected. Default: "ANJAY_FW".',
        default='ANJAY_FW')
    parser.add_argument(
        '-c',
        '--crc',
        type=int,
        help='Override CRC32 checksum value in package metadata with given value.',
        default=None)
    parser.add_argument(
        '-e',
        '--force-error',
        type=str,
        help=(
            'Create a package that causes update failure. Possible values for Firmware Update and Advanced Firmware Update: ' +
            ', '.join(
                e.name for e in PackageForcedError.Firmware.__members__.values()) +
            '. Possible values for Software Management: ' +
            ', '.join(
                e.name for e in PackageForcedError.Software.__members__.values())),
        default=None)
    parser.add_argument(
        '-l',
        '--linked-instances',
        type=int,
        nargs='+',
        help='Add instances that are linked to package. Possible usage: -l 1 2 3, means that instances '
            f'/33629/1, /33629/2 and /33629/3 are linked to package. Max amount of instances is {LINKED_SLOTS}.',
        default=0)
    parser.add_argument(
        '-v',
        '--version',
        type=str,
        help='Set package version. Version can be provided in SemVer format. Max version length is 24 chars.'
        'For the Firmware Update and Software Management objects, the package will only pass validation if'
        'its version is set to 1.0. Default: "1.0"',
        default="1.0")
    
    args = parser.parse_args()

    if args.force_error is None:
        args.force_error = PackageForcedError.Software.NoError if args.magic == 'ANJAY_SW' else PackageForcedError.Firmware.NoError
    else:
        try:
            if args.magic == 'ANJAY_SW':
                args.force_error = PackageForcedError.Software[args.force_error]
            else:
                args.force_error = PackageForcedError.Firmware[args.force_error]
        except KeyError:
            raise ValueError(f'Unsupported forced error: {args.force_error} for {args.magic}')
    
    linked = args.linked_instances if type(
        args.linked_instances) == list else []

    with open(args.in_file, 'rb') as in_file, open(args.out_file, 'wb') as out_file:
        out_file.write(_make_package(in_file.read(),
                                     magic=args.magic.encode('ascii'),
                                     crc=args.crc,
                                     force_error=args.force_error,
                                     pkg_version=args.version.encode('ascii'),
                                     linked=linked))
