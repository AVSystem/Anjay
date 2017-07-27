/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
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

#ifndef ANJAY_COAP_OPT_H
#define ANJAY_COAP_OPT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

#define ANJAY_COAP_OPT_IF_MATCH 1
#define ANJAY_COAP_OPT_URI_HOST 3
#define ANJAY_COAP_OPT_ETAG 4
#define ANJAY_COAP_OPT_IF_NONE_MATCH 5
#define ANJAY_COAP_OPT_OBSERVE 6
#define ANJAY_COAP_OPT_URI_PORT 7
#define ANJAY_COAP_OPT_LOCATION_PATH 8
#define ANJAY_COAP_OPT_URI_PATH 11
#define ANJAY_COAP_OPT_CONTENT_FORMAT 12
#define ANJAY_COAP_OPT_MAX_AGE 14
#define ANJAY_COAP_OPT_URI_QUERY 15
#define ANJAY_COAP_OPT_ACCEPT 17
#define ANJAY_COAP_OPT_LOCATION_QUERY 20
#define ANJAY_COAP_OPT_BLOCK2 23
#define ANJAY_COAP_OPT_BLOCK1 27
#define ANJAY_COAP_OPT_PROXY_URI 35
#define ANJAY_COAP_OPT_PROXY_SCHEME 39
#define ANJAY_COAP_OPT_SIZE1 60

// Technically, CoAP options may contain up to 2 bytes of extended option number
// and up to 2 bytes of extended length. This should never be required for BLOCK
// options. Why? 2-byte extended values are required for interpreting values
// >= 269. BLOCK uses 23/27 option numbers and allows up to 3 content bytes.
// Therefore correct BLOCK options will use at most 1 byte for extended number
// (since wrapping is not allowed) and will never use extended length field.
#define ANJAY_COAP_OPT_BLOCK_MAX_SIZE (1    /* option header   */ \
                                       + 1  /* extended number */ \
                                       + 3) /* block option value */

#define ANJAY_COAP_OPT_INT_MAX_SIZE (1 /* option header */ \
                                     + 2 /* extended number */ \
                                     + 2 /* extended length */ \
                                     + sizeof(uint64_t))

// ETag option has number 4, which means it will never use "extended number"
// format. Since the maximum allowed option size is 8, it won't ever use the
// "extended length" either.
#define ANJAY_COAP_OPT_ETAG_MAX_SIZE (1 /* option header */ \
                                      + 8) /* max ETag length */

typedef struct anjay_coap_opt {
    /**
     * Note: when working with CoAP options do not access these fields directly,
     * since they may not represent the actual encoded values. Use
     * @ref _anjay_coap_opt_value, @ref _anjay_coap_opt_delta and
     * @ref _anjay_coap_opt_content_length instead.
     */
    uint8_t delta_length;
    uint8_t content[];
} anjay_coap_opt_t;

/**
 * @param opt Option to operate on.
 *
 * @returns Pointer to the start of the option content.
 */
const uint8_t *_anjay_coap_opt_value(const anjay_coap_opt_t *opt);

int _anjay_coap_opt_uint_value(const anjay_coap_opt_t *opt,
                               void *out_value,
                               size_t out_value_size);

static inline int _anjay_coap_opt_u32_value(const anjay_coap_opt_t *opt,
                                            uint32_t *out_value) {
    return _anjay_coap_opt_uint_value(opt, out_value, sizeof(*out_value));
}

int _anjay_coap_opt_string_value(const anjay_coap_opt_t *opt,
                                 size_t *out_bytes_read,
                                 char *buffer,
                                 size_t buffer_size);

int _anjay_coap_opt_block_seq_number(const anjay_coap_opt_t *opt,
                                     uint32_t *out_seq_num);
int _anjay_coap_opt_block_has_more(const anjay_coap_opt_t *opt,
                                   bool *out_has_more);
int _anjay_coap_opt_block_size(const anjay_coap_opt_t *opt,
                               uint16_t *out_size);

/**
 * @param opt Option to operate on.
 *
 * @returns Pointer to the start of the option content.
 */
uint32_t _anjay_coap_opt_delta(const anjay_coap_opt_t *opt);

/**
 * @param opt Option to operate on.
 *
 * @returns Pointer to the start of the option content.
 */
uint32_t _anjay_coap_opt_content_length(const anjay_coap_opt_t *opt);

/**
 * @param opt           Option to operate on.
 * @param max_opt_bytes Number of valid bytes available for the @p opt.
 *                      Used to prevent out-of-bounds buffer access.
 *
 * @returns True if the option has a valid format, false otherwise.
 */
bool _anjay_coap_opt_is_valid(const anjay_coap_opt_t *opt,
                              size_t max_opt_bytes);

/**
 * @param opt Option to operate on.
 *
 * @returns Total size of the option including content, in bytes.
 */
size_t _anjay_coap_opt_sizeof(const anjay_coap_opt_t *opt);

void _anjay_coap_opt_debug_print(const anjay_coap_opt_t *opt);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_COAP_OPT_H
