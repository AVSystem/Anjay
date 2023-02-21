/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <avs_coap_init.h>

#ifdef AVS_UNIT_TESTING

#    include "./socket.h"

#    define MODULE_NAME test
#    include <avs_coap_x_log_config.h>

void _avs_mocksock_create(avs_net_socket_t **mocksock, int inner_mtu, int mtu) {
    avs_unit_mocksock_create(mocksock);
    if (inner_mtu >= 0) {
        avs_unit_mocksock_enable_inner_mtu_getopt(*mocksock, inner_mtu);
    }
    if (mtu >= 0) {
        avs_unit_mocksock_enable_mtu_getopt(*mocksock, mtu);
    }
}

#endif // AVS_UNIT_TESTING
