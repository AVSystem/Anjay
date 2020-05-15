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

#ifndef AVS_COAP_SRC_UDP_OPTIONS_OPTIONS_H
#define AVS_COAP_SRC_UDP_OPTIONS_OPTIONS_H

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include <avsystem/coap/option.h>

#include "options/avs_coap_option.h"

#include "../avs_coap_common_utils.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

static inline avs_error_t
_avs_coap_options_copy_into(avs_coap_options_t *out_dest,
                            const avs_coap_options_t *src) {
    if (out_dest->capacity < src->size) {
        return _avs_coap_err(AVS_COAP_ERR_MESSAGE_TOO_BIG);
    }
    if (src->size > 0) {
        memcpy(out_dest->begin, src->begin, src->size);
    }
    out_dest->size = src->size;
    return AVS_OK;
}

static inline avs_coap_options_t _avs_coap_options_copy(
        const avs_coap_options_t *opts, void *buffer, size_t capacity) {
    avs_coap_options_t copy = {
        .begin = buffer,
        .size = opts->size,
        .capacity = capacity
    };
    avs_error_t err = _avs_coap_options_copy_into(&copy, opts);
    assert(avs_is_ok(err));
    (void) err;
    return copy;
}

static inline avs_error_t
_avs_coap_options_copy_as_dynamic(avs_coap_options_t *out_dest,
                                  const avs_coap_options_t *src) {
    assert(!out_dest->allocated);
    avs_error_t err =
            avs_coap_options_dynamic_init_with_size(out_dest, src->size);
    if (avs_is_ok(err)) {
        err = _avs_coap_options_copy_into(out_dest, src);
        assert(avs_is_ok(err));
    }
    return err;
}

static inline void _avs_coap_options_shrink_to_fit(avs_coap_options_t *opts) {
    assert(opts->size <= opts->capacity);
    opts->capacity = opts->size;
}

/**
 * Checks syntactic validity of options in @p opts . Calculates actual options
 * size (either opts->size or offset at which @ref AVS_COAP_PAYLOAD_MARKER was
 * encounterd) and puts it into @p out_actual_size .
 *
 * If options are truncated, false is returned and @p out_truncated_options is
 * set to true (if not NULL).
 *
 * If payload marker was reached during parsing, @p out_payload_marker_reached
 * is set to true (if not NULL).
 */
bool _avs_coap_options_valid_until_payload_marker(
        const avs_coap_options_t *opts,
        size_t *out_actual_size,
        bool *out_truncated_options,
        bool *out_payload_marker_reached);

/**
 * Like @ref _avs_coap_options_valid_until_payload_marker, but also validates
 * that opts->size is correct.
 */
bool _avs_coap_options_valid(const avs_coap_options_t *opts);

bool _avs_coap_option_exists(const avs_coap_options_t *opts,
                             uint16_t opt_number);

static inline bool _avs_coap_option_is_critical(uint16_t opt_number) {
    /*
     * RFC 7252, 5.4.6 (https://tools.ietf.org/html/rfc7252#section-5.4.6):
     * > [...] odd numbers indicate a critical option, while even numbers
     * > indicate an elective option. Note that this is not just a convention,
     * > it is a feature of the protocol: Whether an option is elective or
     * > critical is entirely determined by whether its option number is even
     * > or odd.
     */
    return (opt_number % 2) == 1;
}

/**
 * Checks if options selected by the @p selector function are the same
 * in both option sets.
 *
 * @param first     First set of options to consider.
 * @param second    Second set of options to consider.
 * @param selector  A callback function that shall return true, if the certain
 *                  option is of interest, and false otherwise.
 *
 * @returns true if selected options are equal in both sets, false otherwise.
 */
bool _avs_coap_selected_options_equal(const avs_coap_options_t *first,
                                      const avs_coap_options_t *second,
                                      bool (*selector)(uint16_t));

#ifdef WITH_AVS_COAP_BLOCK
/**
 * Checks if a message with options @p curr can be considered a continuation
 * of a BLOCK-wise exchange whose previous request options were @p prev .
 *
 * @param prev_response                   Set of options used in a response sent
 *                                        after receiving last BLOCK request.
 * @param prev                            Set of options received in previous
 *                                        BLOCK request.
 * @param curr                            Set of options received in a current
 *                                        request.
 * @param expected_request_payload_offset Expected offset of BLOCK1 request
 *                                        payload.
 *
 * @returns true if @p curr are similar to @p prev enough to consider them
 *          consecutive parts of a single logical exchange.
 */
bool _avs_coap_options_is_sequential_block_request(
        const avs_coap_options_t *prev_response,
        const avs_coap_options_t *prev,
        const avs_coap_options_t *curr,
        size_t expected_request_payload_offset);

/**
 * Returns false if payload in message with BLOCK/BERT option with More Flag set
 * has invalid size. This happens in following cases:
 * - request with BLOCK1 and Block Size != @p payload_size
 * - response with BLOCK2 and Block Size != @p payload size
 * - request with BERT1 and @p payload_size % 1024 != 0 or @p payload_size == 0
 * - response with BERT2 and @p payload_size % 1024 != 0 or @p payload_size == 0
 *
 * Returns true if:
 * - @p payload_size is valid for appropriate BLOCK/BERT option
 * - @p coap_code isn't a request or response code
 * - appropriate BLOCK/BERT option isn't present
 */
bool _avs_coap_options_block_payload_valid(const avs_coap_options_t *opts,
                                           uint8_t coap_code,
                                           size_t payload_size);
/**
 * For a packet with given @p code and @p options, finds a BLOCK option
 * describing the packed payload (i.e. BLOCK1 for requests, BLOCK2 for
 * responses) if one exists.
 */
avs_error_t
_avs_coap_options_get_block_by_code(const avs_coap_options_t *options,
                                    uint8_t code,
                                    avs_coap_option_block_t *out_block,
                                    bool *out_has_block);

#endif // WITH_AVS_COAP_BLOCK

avs_error_t _avs_coap_options_parse(avs_coap_options_t *out_opts,
                                    bytes_dispenser_t *dispenser,
                                    bool *out_truncated_options,
                                    bool *out_payload_marker_reached);

/**
 * Returns the size, in bytes, required to store subset of CoAP options given
 * in @p that is used in @ref _avs_coap_options_is_sequential_block_request .
 */
size_t _avs_coap_options_request_key_size(const avs_coap_options_t *opts);

/**
 * Creates a new options list that uses @p buffer for storage and initializes
 * it with the subset of CoAP options from @p opts that is used in
 * @p ref _avs_coap_options_is_sequential_block_request .
 *
 * The behavior is undefined if @p buffer_size is smaller than required.
 * @ref _avs_coap_options_request_key_size should be used to derive number
 * of required bytes.
 */
avs_coap_options_t _avs_coap_options_copy_request_key(
        const avs_coap_options_t *opts, void *buffer, size_t buffer_size);

/**
 * Finds first option with given @p opt_number .
 *
 * Returns NULL if options is not found in @p opts .
 */
const avs_coap_option_t *
_avs_coap_options_find_first_opt(const avs_coap_options_t *opts,
                                 uint16_t opt_number);

VISIBILITY_PRIVATE_HEADER_END

#endif // AVS_COAP_SRC_UDP_OPTIONS_OPTIONS_H
