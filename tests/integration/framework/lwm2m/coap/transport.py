# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

import enum

class Transport(enum.Enum):
    TCP = 0
    UDP = 1

    def __str__(self):
        if self == Transport.TCP:
            return 'tcp'
        else:
            return 'udp'
