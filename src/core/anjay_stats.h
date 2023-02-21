/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
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

void _anjay_coap_ctx_cleanup(anjay_unlocked_t *anjay, avs_coap_ctx_t **ctx);

avs_error_t _anjay_socket_cleanup(anjay_unlocked_t *anjay,
                                  avs_net_socket_t **socket);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_STATS_H
