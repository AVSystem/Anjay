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

/**
 * Implementation of asynchronous @ref avs_coap_exchange_t operations that are
 * supposed to work for both client-side and server-side code.
 */

#include <avs_coap_init.h>

#include <avsystem/commons/avs_errno.h>

#include <avsystem/coap/async_client.h>

#include "avs_coap_code_utils.h"
#include "avs_coap_exchange.h"
#include "options/avs_coap_option.h"

#define MODULE_NAME coap
#include <avs_coap_x_log_config.h>

#include "avs_coap_ctx.h"
#include "options/avs_coap_options.h"

VISIBILITY_SOURCE_BEGIN

static inline bool is_pointer_part_of_enqueued_exchange(avs_coap_ctx_t *ctx,
                                                        const void *buffer) {
    const uint8_t *u8_buf = (const uint8_t *) buffer;

    AVS_LIST(avs_coap_exchange_t) it;
    AVS_LIST_FOREACH(it, _avs_coap_get_base(ctx)->client_exchanges) {
        avs_coap_exchange_t *exchange = (avs_coap_exchange_t *) it;
        const uint8_t *exchange_begin = (const uint8_t *) exchange;
        const uint8_t *exchange_end = (exchange_begin + sizeof(*exchange)
                                       + exchange->options_buffer_size);

        if (exchange_begin <= u8_buf && u8_buf < exchange_end) {
            return true;
        }
    }

    return false;
}

static avs_error_t call_payload_writer(avs_coap_payload_writer_t *write_payload,
                                       void *write_payload_arg,
                                       size_t payload_offset,
                                       void *buffer,
                                       size_t bytes_to_read,
                                       size_t *out_bytes_read) {
    int result = write_payload(payload_offset, buffer, bytes_to_read,
                               out_bytes_read, write_payload_arg);
    LOG(TRACE,
        _("write_payload(offset = ") "%u" _(", size ") "%u" _(") = ") "%d" _(
                "; read ") "%u" _(" B"),
        (unsigned) payload_offset, (unsigned) bytes_to_read, result,
        (unsigned) *out_bytes_read);

    if (result) {
        LOG(DEBUG, _("unable to get request payload (result = ") "%d" _(")"),
            result);
        return _avs_coap_err(AVS_COAP_ERR_PAYLOAD_WRITER_FAILED);
    }

    AVS_ASSERT(*out_bytes_read <= bytes_to_read,
               "write_payload handler reported writing more bytes than "
               "requested - buffer overflow could have happened");
    return AVS_OK;
}

/*
 * Calls the user-defined payload provider handler to retrieve next block of
 * request payload.
 *
 * Only attempts to read at most a single block of data. Size of that block
 * is bounded by @p buffer_size , which MUST be at least 1 byte bigger than
 * desired block size to allow detecting EOF condition.
 *
 * NOTE: it ALWAYS reads less than buffer_size bytes!
 */
static avs_error_t
fetch_payload_with_cache(avs_coap_ctx_t *ctx,
                         avs_coap_payload_writer_t *write_payload,
                         void *write_payload_arg,
                         size_t payload_offset,
                         uint8_t *buffer,
                         size_t buffer_size,
                         size_t *out_bytes_read,
                         eof_cache_t *cache) {
    (void) ctx;
    AVS_ASSERT(!is_pointer_part_of_enqueued_exchange(ctx, cache),
               "Passed eof_cache may be destroyed if the user-defined handler "
               "calls avs_coap_exchange_cancel. Use stack-allocated copy "
               "instead.");
    AVS_ASSERT(!is_pointer_part_of_enqueued_exchange(ctx, buffer)
                       && !is_pointer_part_of_enqueued_exchange(
                                  ctx, buffer + buffer_size),
               "Passed buffer may be destroyed if the user-defined handler "
               "calls avs_coap_exchange_cancel. The buffer must not be a part "
               "of the exchange object.");

    *out_bytes_read = 0;
    if (!write_payload) {
        return AVS_OK;
    }

    size_t bytes_read;
    size_t bytes_read_with_cache;
    size_t bytes_to_read;

    if (cache->empty) {
        // The cache is only empty when we're reading the initial payload block.
        //
        // Attempt to read one byte more than the block size. If the buffer
        // gets fully filled, that means we need to trigger a BLOCK-wise
        // transfer and store the cached byte for later use.
        bytes_to_read = buffer_size;
        avs_error_t err = call_payload_writer(write_payload, write_payload_arg,
                                              payload_offset, buffer,
                                              bytes_to_read, &bytes_read);
        if (avs_is_err(err)) {
            return err;
        }

        bytes_read_with_cache = bytes_read;
    } else {
        // When reading following request blocks, put the cached byte at the
        // start of buffer, then read block_size more bytes, storing the last
        // one in cache again.
        ++payload_offset;
        bytes_to_read = buffer_size - 1;
        avs_error_t err = call_payload_writer(write_payload, write_payload_arg,
                                              payload_offset, &buffer[1],
                                              bytes_to_read, &bytes_read);
        if (avs_is_err(err)) {
            return err;
        }

        buffer[0] = cache->value;
        cache->empty = true;
        bytes_read_with_cache = bytes_read + 1;
    }

    if (bytes_to_read != bytes_read) {
        // EOF reached
        *out_bytes_read = bytes_read_with_cache;
    } else {
        // No EOF yet, there's at least 1 byte more than a full block.
        // Put that byte into cache.
        cache->value = buffer[bytes_read_with_cache - 1];
        cache->empty = false;
        *out_bytes_read = bytes_read_with_cache - 1;
    }

    return AVS_OK;
}

#ifdef WITH_AVS_COAP_BLOCK
static avs_error_t lower_block_size(avs_coap_exchange_t *exchange,
                                    size_t max_payload_size) {
    assert(exchange);

    avs_coap_option_block_t block;
    bool has_block;
    avs_error_t err = _avs_coap_options_get_block_by_code(
            &exchange->options, exchange->code, &block, &has_block);

    if (avs_is_err(err)) {
        return err;
    }
    assert(has_block);

    size_t new_block_size = avs_max_power_of_2_not_greater_than(
            AVS_MIN(max_payload_size, AVS_COAP_BLOCK_MAX_SIZE));
    if (!_avs_coap_is_valid_block_size((uint16_t) new_block_size)) {
        LOG(DEBUG,
            _("CoAP context unable to handle payload size declared "
              "in BLOCK option (max size = ") "%u" _("; required = ") "%u" _(")"),
            (unsigned) max_payload_size, (unsigned) block.size);
        return _avs_coap_err(AVS_COAP_ERR_MESSAGE_TOO_BIG);
    }

    LOG(DEBUG, _("lowering block size: ") "%u" _(" -> ") "%u",
        (unsigned) block.size, (unsigned) new_block_size);

    // Reducing block size may overflow seq_num
    size_t scale_factor = block.size / new_block_size;
    if (block.seq_num * scale_factor > AVS_COAP_BLOCK_MAX_SEQ_NUMBER) {
        LOG(DEBUG,
            _("lowering block size overflows seq_num (") "%lu" _(" > ") "%lu" _(
                    ")"),
            (unsigned long) block.seq_num * scale_factor,
            (unsigned long) AVS_COAP_BLOCK_MAX_SEQ_NUMBER);
        return _avs_coap_err(AVS_COAP_ERR_BLOCK_SEQ_NUM_OVERFLOW);
    }

    if (block.is_bert && block.size != AVS_COAP_BLOCK_MAX_SIZE) {
        AVS_UNREACHABLE("bug: BERT option with size less than 1024");
    }

    if (block.size == AVS_COAP_BLOCK_MAX_SIZE && block.is_bert) {
        block.is_bert = false;
    }
    block.seq_num = (uint32_t) (block.seq_num * scale_factor);
    block.size = (uint16_t) new_block_size;

    avs_coap_options_remove_by_number(&exchange->options,
                                      _avs_coap_option_num_from_block_type(
                                              block.type));
    if (avs_is_err(avs_coap_options_add_block(&exchange->options, &block))) {
        AVS_UNREACHABLE("cannot rewrite BLOCK option even though its size did "
                        "not change");
    }

    return AVS_OK;
}
#endif // WITH_AVS_COAP_BLOCK

static avs_error_t
exchange_get_next_outgoing_chunk_payload_size(avs_coap_ctx_t *ctx,
                                              avs_coap_exchange_t *exchange,
                                              size_t *out_payload_chunk_size) {
    avs_error_t err = _avs_coap_get_max_block_size(
            ctx, exchange->code, &exchange->options, out_payload_chunk_size);
    /*
     * RFC 7959 allows us to lower block size for both requests and
     * responses:
     *
     * - In https://tools.ietf.org/html/rfc7959#section-2.3 :
     *   > [...] the client SHOULD use this block size or a smaller one in
     *   > all further requests in the transfer sequence, even if that
     *   > means changing the block size (and possibly scaling the block
     *   > number accordingly) from now on.
     *
     * - In https://tools.ietf.org/html/rfc7959#section-2.4 :
     *   > The server uses the block size indicated in the request option
     *   > or a smaller size [...]
     */
#ifdef WITH_AVS_COAP_BLOCK
    if (err.category == AVS_COAP_ERR_CATEGORY
            && err.code == AVS_COAP_ERR_MESSAGE_TOO_BIG) {
        if (*out_payload_chunk_size < AVS_COAP_BLOCK_MIN_SIZE) {
            LOG(WARNING,
                _("calculated payload size too small to handle even "
                  "smallest possible BLOCK (size ") "%u" _(" < ") "%u" _(")"),
                (unsigned) *out_payload_chunk_size,
                (unsigned) AVS_COAP_BLOCK_MIN_SIZE);
        } else {
            err = lower_block_size(exchange, *out_payload_chunk_size);
        }
    }
#endif // WITH_AVS_COAP_BLOCK

    return err;
}

avs_error_t _avs_coap_exchange_get_next_outgoing_chunk_payload_size(
        avs_coap_ctx_t *ctx,
        avs_coap_exchange_id_t id,
        size_t *out_payload_chunk_size) {
    AVS_ASSERT(avs_coap_exchange_id_valid(id),
               "Calculating payload size for an exchange that does not exist "
               "does not make sense. If you mean to calculate size for a "
               "message when no exchange object is available, use "
               "_avs_coap_get_next_outgoing_chunk_payload_size instead");

    AVS_LIST(avs_coap_exchange_t) exchange =
            _avs_coap_find_exchange_by_id(ctx, id);
    if (!exchange) {
        return avs_errno(AVS_EINVAL);
    }

    // poor man's method of detecting if we sent the first payload chunk
    AVS_ASSERT(!exchange->eof_cache.empty,
               "This function does not account for extra byte for EOF "
               "detection. When calculating the size for an initial payload "
               "block, use _avs_coap_get_first_outgoing_chunk_payload_size "
               "instead");

    return exchange_get_next_outgoing_chunk_payload_size(
            ctx, exchange, out_payload_chunk_size);
}

#ifdef WITH_AVS_COAP_BLOCK
static avs_error_t get_payload_offset(const avs_coap_exchange_t *exchange,
                                      size_t *out_offset) {
    avs_coap_option_block_t block;
    bool has_block;
    avs_error_t err = _avs_coap_options_get_block_by_code(
            &exchange->options, exchange->code, &block, &has_block);
    if (avs_is_err(err)) {
        return err;
    }

    if (has_block) {
        *out_offset = block.seq_num * block.size;
    } else {
        *out_offset = 0;
    }
    return AVS_OK;
}

static avs_error_t
exchange_add_initial_block_option(avs_coap_exchange_t *exchange,
                                  size_t payload_offset,
                                  size_t payload_size) {
    assert(!exchange->eof_cache.empty);
    assert(payload_size <= UINT16_MAX);
    assert(_avs_coap_is_valid_block_size((uint16_t) payload_size));
    assert(payload_offset % payload_size == 0);
    assert(payload_offset / payload_size <= UINT_MAX);

    avs_coap_option_block_t block = {
        .type = avs_coap_code_is_request(exchange->code) ? AVS_COAP_BLOCK1
                                                         : AVS_COAP_BLOCK2,
        .seq_num = (unsigned) (payload_offset / payload_size),
        .has_more = true,
        .size = (uint16_t) payload_size
    };

    if (avs_is_err(avs_coap_options_add_block(&exchange->options, &block))) {
        return _avs_coap_err(AVS_COAP_ERR_MESSAGE_TOO_BIG);
    }
    return AVS_OK;
}

static avs_error_t exchange_update_block_option(avs_coap_exchange_t *exchange,
                                                size_t payload_offset,
                                                size_t payload_size) {
    avs_coap_option_block_t block;
    bool has_block;
    avs_error_t err = _avs_coap_options_get_block_by_code(
            &exchange->options, exchange->code, &block, &has_block);
    if (avs_is_err(err)) {
        return err;
    }

    if (!has_block) {
        if (!exchange->eof_cache.empty) {
            // cache not empty and no BLOCK option yet
            return exchange_add_initial_block_option(exchange, payload_offset,
                                                     payload_size);
        }
    } else if (block.has_more == exchange->eof_cache.empty) {
        uint16_t opt_num =
                (block.type == AVS_COAP_BLOCK1 ? AVS_COAP_OPTION_BLOCK1
                                               : AVS_COAP_OPTION_BLOCK2);
        avs_coap_options_remove_by_number(&exchange->options, opt_num);
        block.has_more = !exchange->eof_cache.empty;
        if (avs_is_err(
                    avs_coap_options_add_block(&exchange->options, &block))) {
            return _avs_coap_err(AVS_COAP_ERR_MESSAGE_TOO_BIG);
        }
    }
    return AVS_OK;
}
#endif // WITH_AVS_COAP_BLOCK

avs_error_t _avs_coap_exchange_send_next_chunk(
        avs_coap_ctx_t *ctx,
        avs_coap_exchange_t *exchange,
        avs_coap_send_result_handler_t *send_result_handler,
        void *send_result_handler_arg) {
    avs_coap_exchange_id_t id = exchange->id;

    // 1 byte extra to handle eof_cache
    uint8_t payload_buf[AVS_COAP_EXCHANGE_OUTGOING_CHUNK_PAYLOAD_MAX_SIZE];
    size_t bytes_to_read;
    avs_error_t err =
            exchange_get_next_outgoing_chunk_payload_size(ctx, exchange,
                                                          &bytes_to_read);
    if (avs_is_err(err)) {
        return err;
    }
    assert(bytes_to_read < sizeof(payload_buf));

    size_t payload_offset = 0;
#ifdef WITH_AVS_COAP_BLOCK
    err = get_payload_offset(exchange, &payload_offset);
    if (avs_is_err(err)) {
        return err;
    }
#endif // WITH_AVS_COAP_BLOCK

    size_t payload_size;
    eof_cache_t eof_cache = exchange->eof_cache;
    err = fetch_payload_with_cache(ctx, exchange->write_payload,
                                   exchange->write_payload_arg, payload_offset,
                                   payload_buf, bytes_to_read + 1,
                                   &payload_size, &eof_cache);

    if (!_avs_coap_find_exchange_by_id(ctx, id)) {
        // exchange canceled by user handler
        return _avs_coap_err(AVS_COAP_ERR_EXCHANGE_CANCELED);
    }

    if (avs_is_err(err)) {
        return err;
    }

    exchange->eof_cache = eof_cache;

#ifdef WITH_AVS_COAP_BLOCK
    err = exchange_update_block_option(exchange, payload_offset, payload_size);
#else  // WITH_AVS_COAP_BLOCK
    if (!exchange->eof_cache.empty) {
        err = _avs_coap_err(AVS_COAP_ERR_MESSAGE_TOO_BIG);
    }
#endif // WITH_AVS_COAP_BLOCK

    if (avs_is_err(err)) {
        return err;
    }

    avs_coap_borrowed_msg_t msg = {
        .code = exchange->code,
        .token = exchange->token,
        .options = exchange->options,
        .payload = payload_buf,
        .payload_size = payload_size,
        .total_payload_size = payload_size
    };

    return ctx->vtable->send_message(ctx, &msg, send_result_handler,
                                     send_result_handler_arg);
}
