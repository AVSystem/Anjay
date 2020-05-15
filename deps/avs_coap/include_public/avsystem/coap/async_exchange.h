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

#ifndef AVSYSTEM_COAP_ASYNC_EXCHANGE_H
#define AVSYSTEM_COAP_ASYNC_EXCHANGE_H

#include <avsystem/coap/avs_coap_config.h>

#include <stdint.h>

#include <avsystem/coap/ctx.h>
#include <avsystem/coap/writer.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * An ID used to uniquely identify an asynchronous request within CoAP context.
 */
typedef struct {
    uint64_t value;
} avs_coap_exchange_id_t;

static const avs_coap_exchange_id_t AVS_COAP_EXCHANGE_ID_INVALID = { 0 };

static inline bool avs_coap_exchange_id_equal(avs_coap_exchange_id_t a,
                                              avs_coap_exchange_id_t b) {
    return a.value == b.value;
}

static inline bool avs_coap_exchange_id_valid(avs_coap_exchange_id_t id) {
    return !avs_coap_exchange_id_equal(id, AVS_COAP_EXCHANGE_ID_INVALID);
}

/**
 * Releases all memory associated with not-yet-delivered request.
 * The <c>response_handler</c> is called with @ref AVS_COAP_EXCHANGE_CANCEL
 * if it was not NULL when creating the request.
 *
 * @param ctx         CoAP context to operate on.
 *
 * @param exchange_id ID of the undelivered request that should be canceled.
 *                    If the request was already delivered or represents a
 *                    request not known by the @p ctx, nothing happens.
 */
void avs_coap_exchange_cancel(avs_coap_ctx_t *ctx,
                              avs_coap_exchange_id_t exchange_id);

#ifdef __cplusplus
}
#endif

#endif // AVSYSTEM_COAP_ASYNC_EXCHANGE_H
