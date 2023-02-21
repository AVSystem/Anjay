/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_TEST_UTILS_H
#define ANJAY_TEST_UTILS_H

#include <avsystem/commons/avs_unit_mocksock.h>
#include <math.h>

#define SCOPED_PTR(Type, Deleter) __attribute__((__cleanup__(Deleter))) Type *

static inline void _anjay_mocksock_expect_stats_zero(avs_net_socket_t *socket) {
    avs_unit_mocksock_expect_shutdown(socket);
    avs_unit_mocksock_expect_get_opt(socket, AVS_NET_SOCKET_OPT_BYTES_SENT,
                                     (avs_net_socket_opt_value_t) {
                                         .bytes_sent = 0
                                     });
    avs_unit_mocksock_expect_get_opt(socket,
                                     AVS_NET_SOCKET_OPT_BYTES_RECEIVED,
                                     (avs_net_socket_opt_value_t) {
                                         .bytes_received = 0
                                     });
}

#endif /* ANJAY_TEST_UTILS_H */
