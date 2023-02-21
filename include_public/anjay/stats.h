/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */
#ifndef ANJAY_INCLUDE_ANJAY_STATS_H
#define ANJAY_INCLUDE_ANJAY_STATS_H

#include <anjay/core.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @returns the total amount of bytes transmitted by the client.
 *
 * NOTE: When ANJAY_WITH_NET_STATS is disabled this function always returns 0.
 */
uint64_t anjay_get_tx_bytes(anjay_t *anjay);

/**
 * @returns the amount of bytes received by the client.
 *
 * NOTE: When ANJAY_WITH_NET_STATS is disabled this function always returns 0.
 */
uint64_t anjay_get_rx_bytes(anjay_t *anjay);

/**
 * @returns the number of packets received by the client to which cached
 *          responses were found.
 *
 * NOTE: When ANJAY_WITH_NET_STATS is disabled this function always returns 0.
 */
uint64_t anjay_get_num_incoming_retransmissions(anjay_t *anjay);

/**
 * @returns the number of packets sent by the client that were already
 *          cached as well as requests which the client did not get any
 *          response to.
 *
 * NOTE: When ANJAY_WITH_NET_STATS is disabled this function always returns 0.
 */
uint64_t anjay_get_num_outgoing_retransmissions(anjay_t *anjay);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ANJAY_INCLUDE_ANJAY_STATS_H */
