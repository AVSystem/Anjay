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

#ifndef AVS_COAP_SRC_EXCHANGE_H
#define AVS_COAP_SRC_EXCHANGE_H

#include <avsystem/commons/avs_list.h>

#include <avsystem/coap/async.h>
#include <avsystem/coap/ctx.h>

#include "avs_coap_async_client.h"
#include "avs_coap_async_server.h"
#include "options/avs_coap_option.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct {
    bool empty;
    uint8_t value;
} eof_cache_t;

/**
 * An object representing a single request-response pair, regardless of payload
 * size of either. Abstracts away BLOCK-wise transfers.
 */
typedef struct avs_coap_exchange {
    /** Unique ID used to identify an exchange in user code. */
    avs_coap_exchange_id_t id;

    /** User-defined handler used to provide payload for sent message. */
    avs_coap_payload_writer_t *write_payload;
    void *write_payload_arg;

    /**
     * CoAP code of the message being sent, configured by the user (request
     * code for outgoing exchanges; final response code for incoming exchanges).
     */
    uint8_t code;
    /** CoAP token of the last message sent. */
    avs_coap_token_t token;
    /**
     * Set of options included in last sent message. Uses
     * @ref avs_coap_exchange_t#options_buffer as storage. Initialized with
     * user-provided CoAP options, changes during exchange lifetime as
     * necessary to handle BLOCK transfers.
     */
    avs_coap_options_t options;

    /**
     * A cache with optional<byte> semantics, used when reading user-provided
     * payload to detect EOF case.
     */
    eof_cache_t eof_cache;

    union {
        avs_coap_client_exchange_data_t client;
        avs_coap_server_exchange_data_t server;
    } by_type;

    /**
     * Number of bytes available in @ref avs_coap_exchange_t#options_buffer . It
     * may be different than @ref avs_coap_exchange_t#options.capacity .
     */
    size_t options_buffer_size;
    /**
     * Mostly @ref avs_coap_exchange_t#options storage, but it also may contain
     * some other (Client or Server specific) data.
     */
    char options_buffer[];
} avs_coap_exchange_t;

#define AVS_COAP_EXCHANGE_OUTGOING_CHUNK_PAYLOAD_MAX_SIZE \
    (AVS_COAP_BLOCK_MAX_SIZE + 1)

/**
 * Queries the expected size of the chunk that will be requested during the
 * next call to @ref avs_coap_payload_writer_t for a given exchange.
 *
 * NOTE: it is assumed that at the point of calling this function the first
 * exchange block was already sent, and accounting for EOF detection is not
 * necessary. For that particular case, use
 * @ref _avs_coap_get_first_outgoing_chunk_payload_size instead .
 *
 * @param ctx
 * CoAP context to operate on.
 *
 * @param id
 * ID of the exchange for which to perform the calculations. The function fails
 * if it does not refer to a valid exchange.
 *
 * @param out_payload_chunk_size
 * Pointer to a variable which will be filled with the calculated payload chunk
 * size. The size is guaranteed to be no larger than
 * @ref AVS_COAP_EXCHANGE_OUTGOING_CHUNK_PAYLOAD_MAX_SIZE . The actual size
 * passed to the next call to @ref avs_coap_payload_writer_t is guaranteed to be
 * no larger than the size returned from this function beforehand.
 *
 * @returns @ref AVS_OK for success, or an error condition for which the
 *          operation failed.
 */
avs_error_t _avs_coap_exchange_get_next_outgoing_chunk_payload_size(
        avs_coap_ctx_t *ctx,
        avs_coap_exchange_id_t id,
        size_t *out_payload_chunk_size);

/**
 * Fetches next chunk of payload associated with @p exchange to @p payload_buf .
 * Adds a BLOCK1/2 option to @p exchange CoAP options if necessary (not yet
 * present, but @p payload_buf too small to hold whole payload). Finally,
 * sends next chunk of the exchange, using @p send_result_handler as delivery
 * confirmation handler.
 *
 * @returns
 * @ref AVS_OK for success, or an error condition for which the operation
 * failed, which includes:
 * - @p ctx reporting not being able to handle packets with at least
 *   @ref AVS_COAP_BLOCK_MIN_SIZE bytes of payload, considering CoAP options
 *   currently held within @p exchange object,
 * - user-defined payload writer failure,
 * - @p exchange cancellation from within user-defined payload writer,
 * - insufficient space for inserting the BLOCK1/2 option.
 * - ctx->vtable->send_message failure.
 */
avs_error_t _avs_coap_exchange_send_next_chunk(
        avs_coap_ctx_t *ctx,
        avs_coap_exchange_t *exchange,
        avs_coap_send_result_handler_t *send_result_handler,
        void *send_result_handler_arg);

VISIBILITY_PRIVATE_HEADER_END

#endif // AVS_COAP_SRC_EXCHANGE_H
