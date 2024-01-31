/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef _FLUF_COAP_OPTION_H
#define _FLUF_COAP_OPTION_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <fluf/fluf_defs.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * CoAP option numbers, as defined in RFC7252/RFC7641/RFC7959.
 * @{
 */
#define _FLUF_COAP_OPTION_IF_MATCH 1
#define _FLUF_COAP_OPTION_URI_HOST 3
#define _FLUF_COAP_OPTION_ETAG 4
#define _FLUF_COAP_OPTION_IF_NONE_MATCH 5
#define _FLUF_COAP_OPTION_OBSERVE 6
#define _FLUF_COAP_OPTION_URI_PORT 7
#define _FLUF_COAP_OPTION_LOCATION_PATH 8
#define _FLUF_COAP_OPTION_OSCORE 9
#define _FLUF_COAP_OPTION_URI_PATH 11
#define _FLUF_COAP_OPTION_CONTENT_FORMAT 12
#define _FLUF_COAP_OPTION_MAX_AGE 14
#define _FLUF_COAP_OPTION_URI_QUERY 15
#define _FLUF_COAP_OPTION_ACCEPT 17
#define _FLUF_COAP_OPTION_LOCATION_QUERY 20
#define _FLUF_COAP_OPTION_BLOCK2 23
#define _FLUF_COAP_OPTION_BLOCK1 27
#define _FLUF_COAP_OPTION_PROXY_URI 35
#define _FLUF_COAP_OPTION_PROXY_SCHEME 39
#define _FLUF_COAP_OPTION_SIZE1 60

/**
 * Constant returned from some of option-retrieving functions, indicating
 * the absence of requested option.
 */
#define _FLUF_COAP_OPTION_MISSING 1

#define _FLUF_COAP_OPTIONS_INIT_EMPTY(Name, OptionsSize) \
    fluf_coap_option_t _Opt##Name[OptionsSize];          \
    fluf_coap_options_t Name = {                         \
        .options_size = OptionsSize,                     \
        .options_number = 0,                             \
        .options = _Opt##Name,                           \
        .buff_size = 0,                                  \
        .buff_begin = NULL                               \
    }

typedef struct fluf_coap_option {
    const uint8_t *payload;
    size_t payload_len;
    uint16_t option_number;
} fluf_coap_option_t;

/**size_t *iterator
 * Note: this struct MUST be initialized with
 * @ref _FLUF_COAP_OPTIONS_INIT_EMPTY or _FLUF_COAP_OPTIONS_INIT_EMPTY_WITH_BUFF
 * before it is used.
 */
typedef struct fluf_coap_options {
    fluf_coap_option_t *options;
    size_t options_size;
    size_t options_number;

    uint8_t *buff_begin;
    size_t buff_size;
} fluf_coap_options_t;

int _fluf_coap_options_decode(fluf_coap_options_t *opts,
                              const uint8_t *msg,
                              size_t msg_size,
                              size_t *bytes_read);

/*
 * - 0 on success,
 * - _FLUF_COAP_OPTION_MISSING when there are no more options with
 *              given @p option_number to retrieve
 */
int _fluf_coap_options_get_data_iterate(const fluf_coap_options_t *opts,
                                        uint16_t option_number,
                                        size_t *iterator,
                                        size_t *out_option_size,
                                        void *out_buffer,
                                        size_t out_buffer_size);

int _fluf_coap_options_get_string_iterate(const fluf_coap_options_t *opts,
                                          uint16_t option_number,
                                          size_t *iterator,
                                          size_t *out_option_size,
                                          char *out_buffer,
                                          size_t out_buffer_size);

int _fluf_coap_options_get_u16_iterate(const fluf_coap_options_t *opts,
                                       uint16_t option_number,
                                       size_t *iterator,
                                       uint16_t *out_value);

int _fluf_coap_options_get_u32_iterate(const fluf_coap_options_t *opts,
                                       uint16_t option_number,
                                       size_t *iterator,
                                       uint32_t *out_value);

int _fluf_coap_options_add_data(fluf_coap_options_t *opts,
                                uint16_t opt_number,
                                const void *data,
                                size_t data_size);

static inline int _fluf_coap_options_add_string(fluf_coap_options_t *opts,
                                                uint16_t opt_number,
                                                const char *data) {
    return _fluf_coap_options_add_data(opts, opt_number, data, strlen(data));
}

int _fluf_coap_options_add_u16(fluf_coap_options_t *opts,
                               uint16_t opt_number,
                               uint16_t value);

int _fluf_coap_options_add_u32(fluf_coap_options_t *opts,
                               uint16_t opt_number,
                               uint32_t value);

int _fluf_coap_options_add_u64(fluf_coap_options_t *opts,
                               uint16_t opt_number,
                               uint64_t value);

#ifdef __cplusplus
}
#endif

#endif // _FLUF_COAP_OPTION_H
