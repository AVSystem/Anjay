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

#ifndef AVS_COAP_MSG_CACHE_H
#define AVS_COAP_MSG_CACHE_H

#include <stddef.h>
#include <stdint.h>

#include <avsystem/coap/udp.h>

#include "udp/avs_coap_udp_msg.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

#define AVS_COAP_MSG_CACHE_DUPLICATE (-2)

typedef struct {
    avs_coap_udp_msg_t msg;
    const void *packet;
    size_t packet_size;
} avs_coap_udp_cached_response_t;

/**
 * Adds a message to cache. Drops oldest cache entries if needed to fit
 * @p msg, even if they did not expire yet.
 *
 * Cached message expires after EXCHANGE_LIFETIME from being added to the cache.
 *
 * @param cache       Cache object to put message in.
 * @param remote_addr Message recipient address. Messages sent to one recipient
 *                    will not be considered valid for another one.
 * @param remote_port Message recipient port.
 * @param msg         Message to cache.
 * @param tx_params   Transmission params. Determine cached message lifetime.
 *
 * @return 0 on success, a negative value if:
 *         @li @p cache is NULL,
 *         @li there was not enough memory to allocate endpoint data,
 *         @li @p cache is too small to fit @p msg,
 *         @li @p cache already contains a message with the same remote endpoint
 *             and message ID (@ref AVS_COAP_MSG_CACHE_DUPLICATE).
 *
 * NOTE: this function intentionally fails if a message with the same remote
 * endpoint and message ID is already present. If there is a valid one in the
 * cache, we should have used it instead of preparing a new response, so that
 * indicates a bug hiding somewhere.
 */
int _avs_coap_udp_response_cache_add(avs_coap_udp_response_cache_t *cache,
                                     const char *remote_addr,
                                     const char *remote_port,
                                     const avs_coap_udp_msg_t *msg,
                                     const avs_coap_udp_tx_params_t *tx_params);

/**
 * Looks up @p cache for a message with given @p msg_id and returns it if found.
 *
 * @param      cache       Cache object to look into, or NULL.
 * @param      remote_addr Message recipient address. Messages sent to one
 * recipient will not be considered valid for another one.
 * @param      remote_port Message recipient port.
 * @param      msg_id      CoAP message ID to look for.
 * @param[out] out_msg     Message object to fill on success.
 *
 * @return AVS_OK if a message matching @p msg_id was found in the cache and
 *         returned via @p out_msg, or an error condition for which the
 *         operation failed.
 */
avs_error_t
_avs_coap_udp_response_cache_get(avs_coap_udp_response_cache_t *cache,
                                 const char *remote_addr,
                                 const char *remote_port,
                                 uint16_t msg_id,
                                 avs_coap_udp_cached_response_t *out_response);

/**
 * @return Extra overhead, in bytes, required to put @p msg in cache. Total
 *         number of bytes used by a message is:
 *         <c>_avs_coap_udp_response_cache_overhead(msg)
 *         + _avs_coap_udp_msg_size(msg)</c>
 */
size_t _avs_coap_udp_response_cache_overhead(const avs_coap_udp_msg_t *msg);

VISIBILITY_PRIVATE_HEADER_END

#endif // AVS_COAP_MSG_CACHE_H
