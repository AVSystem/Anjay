# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

import string


def hexlify(s):
    return ''.join('\\x%02x' % c for c in s)


def hexlify_nonprintable(s):
    return ''.join(chr(c) if chr(c) in string.printable else ('\\x%02x' % c) for c in s)
