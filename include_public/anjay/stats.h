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
#ifndef ANJAY_INCLUDE_ANJAY_STATS_H
#define ANJAY_INCLUDE_ANJAY_STATS_H

#include <anjay/core.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @returns the total amount of bytes transmitted by the client.
 *
 * NOTE: When ANJAY_WITH_NET_STATS is disabled this function always return 0.
 */
uint64_t anjay_get_tx_bytes(anjay_t *anjay);

/**
 * @returns the amount of bytes received by the client.
 *
 * NOTE: When ANJAY_WITH_NET_STATS is disabled this function always return 0.
 */
uint64_t anjay_get_rx_bytes(anjay_t *anjay);

/**
 * @returns the number of packets received by the client to which cached
 *          responses were found.
 *
 * NOTE: When ANJAY_WITH_NET_STATS is disabled this function always return 0.
 */
uint64_t anjay_get_num_incoming_retransmissions(anjay_t *anjay);

/**
 * @returns the number of packets sent by the client that were already
 *          cached as well as requests which the client did not get any
 *          response to.
 *
 * NOTE: When ANJAY_WITH_NET_STATS is disabled this function always return 0.
 */
uint64_t anjay_get_num_outgoing_retransmissions(anjay_t *anjay);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ANJAY_INCLUDE_ANJAY_STATS_H */
