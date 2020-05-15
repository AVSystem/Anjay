/*
 * Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
