import binascii
import struct
import enum

from typing import Optional


@enum.unique
class FirmwareUpdateForcedError(enum.IntEnum):
    NoError = 0
    OutOfMemory = 1
    FailedUpdate = 2

def make_firmware_package(binary: bytes,
                          magic: bytes=b'ANJAY_FW',
                          crc: Optional[int]=None,
                          force_error: FirmwareUpdateForcedError=FirmwareUpdateForcedError.NoError,
                          version: int=1):
    assert len(magic) == 8

    if crc is None:
        crc = binascii.crc32(binary)

    meta = struct.pack('>8sHHI', magic, version, force_error, crc)
    return meta + binary


if __name__ == '__main__':
    import sys
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
