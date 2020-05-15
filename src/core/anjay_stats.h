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

#ifndef ANJAY_STATS_H
#define ANJAY_STATS_H

#include <anjay_init.h>

#include <stdint.h>

#include <anjay/core.h>
#include <avsystem/coap/ctx.h>
#include <avsystem/commons/avs_socket.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

#ifdef ANJAY_WITH_NET_STATS

/**
 * Structure for aggregating statistics of all closed coap contexts and sockets.
 */
typedef struct {
    avs_coap_stats_t coap_stats;
    struct {
        uint64_t bytes_sent;
        uint64_t bytes_received;
    } socket_stats;
} closed_connections_stats_t;

#endif // ANJAY_WITH_NET_STATS

void _anjay_coap_ctx_cleanup(anjay_t *anjay, avs_coap_ctx_t **ctx);

avs_error_t _anjay_socket_cleanup(anjay_t *anjay, avs_net_socket_t **socket);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_STATS_H
