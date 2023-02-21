/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#include "tests/utils/coap/socket.h"

void _anjay_mocksock_create(avs_net_socket_t **mocksock,
                            int inner_mtu,
                            int mtu) {
    avs_unit_mocksock_create_datagram(mocksock);
    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            *mocksock, avs_time_duration_from_scalar(30, AVS_TIME_S));
    if (inner_mtu >= 0) {
        avs_unit_mocksock_enable_inner_mtu_getopt(*mocksock, inner_mtu);
    }
    if (mtu >= 0) {
        avs_unit_mocksock_enable_mtu_getopt(*mocksock, mtu);
    }
}
