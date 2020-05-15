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

#ifndef AVS_COAP_SRC_UDP_OPTIONS_OPTION_H
#define AVS_COAP_SRC_UDP_OPTIONS_OPTION_H

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <avsystem/coap/option.h>

#include "avs_coap_parse_utils.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

/**
 * From RFC8323:
 * "BERT Option:
 *  A Block1 or Block2 option that includes an SZX (block size)
 *  value of 7."
 */
#define AVS_COAP_OPT_BERT_SZX 7

#define AVS_COAP_OPT_BLOCK_MAX_SZX 6

/**
 * Maximum size, in bytes, required for encoding a BLOCK1/BLOCK2 option.
 *
 * Technically, CoAP options may contain up to 2 bytes of extended option number
 * and up to 2 bytes of extended length. This should never be required for BLOCK
 * options. Why? 2-byte extended values are required for interpreting values
 * >= 269. BLOCK uses 23/27 option numbers and allows up to 3 content bytes.
 * Therefore correct BLOCK options will use at most 1 byte for extended number
 * (since wrapping is not allowed) and will never use extended length field.
 */
#define AVS_COAP_OPT_BLOCK_MAX_SIZE \
    (1    /* option header   */     \
     + 1  /* extended number */     \
     + 3) /* block option value */

/**
 * CoAP Observe option has number 6, so it never requires an extended nuber
 * field. Its content is up to 3 bytes, so extended length is not required
 * either.
 */
#define AVS_COAP_OPT_OBSERVE_MAX_SIZE \
    (1    /* option header */         \
     + 3) /* option value */

/**
 * @returns CoAP option number appropriate for BLOCK transfer of given @p type .
 */
static inline uint16_t
_avs_coap_option_num_from_block_type(avs_coap_option_block_type_t type) {
    return type == AVS_COAP_BLOCK1 ? AVS_COAP_OPTION_BLOCK1
                                   : AVS_COAP_OPTION_BLOCK2;
}

/**
 * @returns true if @p size is an acceptable CoAP BLOCK size.
 */
static inline bool _avs_coap_is_valid_block_size(uint16_t size) {
    return avs_is_power_of_2(size) && size <= AVS_COAP_BLOCK_MAX_SIZE
           && size >= AVS_COAP_BLOCK_MIN_SIZE;
}

/**
 * Magic value defined in RFC7252, used internally when constructing/parsing
 * CoAP packets.
 */
#define AVS_COAP_PAYLOAD_MARKER ((uint8_t) { 0xFF })

AVS_STATIC_ASSERT(sizeof(AVS_COAP_PAYLOAD_MARKER) == 1,
                  payload_marker_must_be_1_byte_long);

/** Serialized CoAP option. */
typedef struct avs_coap_option {
    /**
     * Note: when working with CoAP options do not access these fields directly,
     * since they may not represent the actual encoded values. Use
     * @ref _avs_coap_option_value, @ref _avs_coap_option_delta and
     * @ref _avs_coap_option_content_length instead.
     */
    uint8_t delta_length;
    uint8_t content[];
} avs_coap_option_t;

/**
 * @param opt Option to operate on.
 *
 * @returns Pointer to the start of the option content.
 */
const uint8_t *_avs_coap_option_value(const avs_coap_option_t *opt);

/**
 * Retrieves a 16-bit integer option value.
 *
 * @param[in]  opt            CoAP option to retrieve value from.
 * @param[out] out_value      Pointer to variable to store the option value in.
 *
 * @returns 0 on success, a negative value if @p out_value is too small
 *          to hold the integer value of @p opt .
 */
int _avs_coap_option_u16_value(const avs_coap_option_t *opt,
                               uint16_t *out_value);

/**
 * Retrieves a 32-bit integer option value.
 *
 * @param[in]  opt            CoAP option to retrieve value from.
 * @param[out] out_value      Pointer to variable to store the option value in.
 *
 * @returns 0 on success, a negative value if @p out_value is too small
 *          to hold the integer value of @p opt .
 */
int _avs_coap_option_u32_value(const avs_coap_option_t *opt,
                               uint32_t *out_value);

/**
 * Retrieves an CoAP option value as a zero-terminated string.
 *
 * @param[in]  opt             Option to retrieve value from.
 * @param[out] out_option_size Size of the option value, including terminating
 *                             nullbyte. After successful call, it's equal to
 *                             the number of bytes written to @p buffer.
 * @param[out] buffer          Buffer to store the retrieved value in.
 * @param[in]  buffer_size     Number of bytes available in @p buffer .
 *
 * @returns @li 0 on success, in which case @p out_bytes_read contains
 *              the number of bytes successfully written to @p buffer .
 *              String written to @p buffer is guaranteed to be zero-terminated.
 *          @li A negative value if @p buffer is too small to hold the option
 *              value. In such case, @p buffer contents are not modified and
 *              @p out_bytes_read is not set.
 */
int _avs_coap_option_string_value(const avs_coap_option_t *opt,
                                  size_t *out_option_size,
                                  char *buffer,
                                  size_t buffer_size);

/**
 * Retrieves a BLOCK sequence number from a CoAP option.
 *
 * Note: the function does not check whether @p opt is indeed a BLOCK option.
 * Calling this function on non-BLOCK options causes undefined behavior.
 *
 * @param[in]  opt         CoAP option to read sequence number from.
 * @param[out] out_seq_num Read BLOCK sequence number.
 *
 * @returns @li 0 on success, in which case @p out_seq_num is set,
 *          @li -1 if the option value is too big to be a correct BLOCK option.
 */
int _avs_coap_option_block_seq_number(const avs_coap_option_t *opt,
                                      uint32_t *out_seq_num);

/**
 * Retrieves a "More" marker from a CoAP BLOCK option.
 *
 * Note: the function does not check whether @p opt is indeed a BLOCK option.
 * Calling this function on non-BLOCK options causes undefined behavior.
 *
 * @param[in]  opt          CoAP option to read sequence number from.
 * @param[out] out_has_more Value of the "More" flag of a BLOCK option.
 *
 * @returns @li 0 on success, in which case @p out_has_more is set,
 *          @li -1 if the option value is too big to be a correct BLOCK option.
 */
int _avs_coap_option_block_has_more(const avs_coap_option_t *opt,
                                    bool *out_has_more);

/**
 * Retrieves a block size from a CoAP BLOCK option.
 *
 * Note: the function does not check whether @p opt is indeed a BLOCK option.
 * Calling this function on non-BLOCK options causes undefined behavior.
 *
 * @param[in]  opt         CoAP option to read block size from.
 * @param[out] out_size    Block size, in bytes, encoded in the option. MUST NOT
 *                         be NULL.
 * @param[out] out_is_bert If true, then BLOCK option is a BERT (allowed only in
 *                         CoAP/TCP). MUST NOT be NULL.
 *
 * @returns @li 0 on success, in which case @p out_has_more is set,
 *          @li -1 if the option value is too big to be a correct BLOCK option
 *              or if the option is malformed.
 */
int _avs_coap_option_block_size(const avs_coap_option_t *opt,
                                uint16_t *out_size,
                                bool *out_is_bert);

/**
 * @param opt Option to operate on.
 *
 * @returns Option Delta (as per RFC7252 section 3.1).
 */
uint32_t _avs_coap_option_delta(const avs_coap_option_t *opt);

/**
 * @param opt Option to operate on.
 *
 * @returns Length of the option content, in bytes.
 */
uint32_t _avs_coap_option_content_length(const avs_coap_option_t *opt);

/**
 * @param opt           Option to operate on.
 * @param max_opt_bytes Number of valid bytes available for the @p opt.
 *                      Used to prevent out-of-bounds buffer access.
 *
 * @returns True if the option has a valid format, false otherwise.
 */
bool _avs_coap_option_is_valid(const avs_coap_option_t *opt,
                               size_t max_opt_bytes);

/**
 * @param opt Option to operate on.
 *
 * @returns Total size of the option including content, in bytes.
 */
size_t _avs_coap_option_sizeof(const avs_coap_option_t *opt);

/**
 * @param buffer           Buffer to serialize the option to.
 * @param buffer_size      Number of bytes available in @p buffer.
 * @param opt_number_delta Option number delta.
 * @param opt_data         Option content bytes.
 * @param opt_data_size    Number of content bytes to write.
 *
 * @returns Number of bytes written to @p buffer on success, 0 on error.
 *
 * NOTE: it is only safe to use this function to overwrite an option with
 * itself if the new @p opt_number_delta is no larger that previous one.
 */
size_t _avs_coap_option_serialize(uint8_t *buffer,
                                  size_t buffer_size,
                                  size_t opt_number_delta,
                                  const void *opt_data,
                                  size_t opt_data_size);

#define _AVS_COAP_EXT_U8 13
#define _AVS_COAP_EXT_U16 14
#define _AVS_COAP_EXT_RESERVED 15

#define _AVS_COAP_EXT_U8_BASE ((uint32_t) 13)
#define _AVS_COAP_EXT_U16_BASE ((uint32_t) 269)

#define _AVS_COAP_OPTION_DELTA_MASK 0xF0
#define _AVS_COAP_OPTION_DELTA_SHIFT 4
#define _AVS_COAP_OPTION_LENGTH_MASK 0x0F
#define _AVS_COAP_OPTION_LENGTH_SHIFT 0

static inline uint8_t
_avs_coap_option_get_short_delta(const avs_coap_option_t *opt) {
    return _AVS_FIELD_GET(opt->delta_length,
                          _AVS_COAP_OPTION_DELTA_MASK,
                          _AVS_COAP_OPTION_DELTA_SHIFT);
}

static inline void _avs_coap_option_set_short_delta(avs_coap_option_t *opt,
                                                    uint8_t delta) {
    assert(delta <= _AVS_COAP_EXT_RESERVED);
    _AVS_FIELD_SET(opt->delta_length, _AVS_COAP_OPTION_DELTA_MASK,
                   _AVS_COAP_OPTION_DELTA_SHIFT, delta);
}

static inline uint8_t
_avs_coap_option_get_short_length(const avs_coap_option_t *opt) {
    return _AVS_FIELD_GET(opt->delta_length,
                          _AVS_COAP_OPTION_LENGTH_MASK,
                          _AVS_COAP_OPTION_LENGTH_SHIFT);
}

static inline void _avs_coap_option_set_short_length(avs_coap_option_t *opt,
                                                     uint8_t length) {
    assert(length <= _AVS_COAP_EXT_RESERVED);
    _AVS_FIELD_SET(opt->delta_length, _AVS_COAP_OPTION_LENGTH_MASK,
                   _AVS_COAP_OPTION_LENGTH_SHIFT, length);
}

static inline size_t _avs_coap_get_opt_header_size(size_t opt_number_delta,
                                                   size_t opt_data_size) {
    assert(opt_number_delta <= UINT16_MAX);
    assert(opt_data_size <= UINT16_MAX);

    size_t header_size = 1;

    if (opt_number_delta >= _AVS_COAP_EXT_U16_BASE) {
        header_size += 2;
    } else if (opt_number_delta >= _AVS_COAP_EXT_U8_BASE) {
        header_size += 1;
    }

    if (opt_data_size >= _AVS_COAP_EXT_U16_BASE) {
        header_size += 2;
    } else if (opt_data_size >= _AVS_COAP_EXT_U8_BASE) {
        header_size += 1;
    }

    return header_size;
}

static inline const char *_avs_coap_option_block_string(
        char *buf, size_t size, const avs_coap_option_block_t *block) {
    assert(size >= sizeof("BLOCK1(seq_num 4294967295, size 65535, more 1)"));

    snprintf(buf, size,
             "BLOCK%d(seq_num %" PRIu32 ", size %" PRIu16 ", more %d)",
             block->type == AVS_COAP_BLOCK1 ? 1 : 2, block->seq_num,
             block->size, (int) block->has_more);
    return buf;
}

#define _AVS_COAP_OPTION_BLOCK_STRING(Opt) \
    _avs_coap_option_block_string(&(char[48]){ 0 }[0], 48, (Opt))

VISIBILITY_PRIVATE_HEADER_END

#endif // AVS_COAP_SRC_UDP_OPTIONS_OPTION_H
