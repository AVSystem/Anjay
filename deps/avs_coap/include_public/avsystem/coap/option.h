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

#ifndef AVSYSTEM_COAP_OPTION_H
#define AVSYSTEM_COAP_OPTION_H

#include <avsystem/coap/avs_coap_config.h>

#include <avsystem/commons/avs_errno.h>
#include <avsystem/commons/avs_list.h>
#include <avsystem/commons/avs_utils.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * CoAP Content-Formats, as defined in "Constrained RESTful Environments (CoRE)
 * Parameters":
 * https://www.iana.org/assignments/core-parameters/core-parameters.xhtml
 * @{
 */
#define AVS_COAP_FORMAT_PLAINTEXT 0
#define AVS_COAP_FORMAT_COSE_ENCRYPT0 16
#define AVS_COAP_FORMAT_COSE_MAC0 17
#define AVS_COAP_FORMAT_COSE_SIGN1 18
#define AVS_COAP_FORMAT_LINK_FORMAT 40
#define AVS_COAP_FORMAT_XML 41
#define AVS_COAP_FORMAT_OCTET_STREAM 42
#define AVS_COAP_FORMAT_EXI 47
#define AVS_COAP_FORMAT_JSON 50
#define AVS_COAP_FORMAT_JSON_PATCH_JSON 51
#define AVS_COAP_FORMAT_MERGE_PATCH_JSON 52
#define AVS_COAP_FORMAT_CBOR 60
#define AVS_COAP_FORMAT_CWT 61
#define AVS_COAP_FORMAT_COSE_ENCRYPT 96
#define AVS_COAP_FORMAT_COSE_MAC 97
#define AVS_COAP_FORMAT_COSE_SIGN 98
#define AVS_COAP_FORMAT_COSE_KEY 101
#define AVS_COAP_FORMAT_COSE_KEY_SET 102
#define AVS_COAP_FORMAT_SENML_JSON 110
#define AVS_COAP_FORMAT_SENSML_JSON 111
#define AVS_COAP_FORMAT_SENML_CBOR 112
#define AVS_COAP_FORMAT_SENSML_CBOR 113
#define AVS_COAP_FORMAT_SENML_EXI 114
#define AVS_COAP_FORMAT_SENSML_EXI 115
#define AVS_COAP_FORMAT_COAP_GROUP_JSON 256
#define AVS_COAP_FORMAT_PKCS7_SERVER_GENERATED_KEY 280
#define AVS_COAP_FORMAT_PKCS7_CERTS_ONLY 281
#define AVS_COAP_FORMAT_PKCS8 284
#define AVS_COAP_FORMAT_CSR_ATTRS 285
#define AVS_COAP_FORMAT_PKCS10 286
#define AVS_COAP_FORMAT_PKIX_CERT 287
#define AVS_COAP_FORMAT_SENML_XML 310
#define AVS_COAP_FORMAT_SENSML_XML 311
#define AVS_COAP_FORMAT_OCF_CBOR 10000
#define AVS_COAP_FORMAT_OSCORE 10001
#define AVS_COAP_FORMAT_JSON_DEFLATE 11050
#define AVS_COAP_FORMAT_CBOR_DEFLATE 11060
#define AVS_COAP_FORMAT_OMA_LWM2M_TLV 11542
#define AVS_COAP_FORMAT_OMA_LWM2M_JSON 11543
/** @} */

/**
 * CoAP option numbers, as defined in RFC7252/RFC7641/RFC7959.
 * @{
 */
#define AVS_COAP_OPTION_IF_MATCH 1
#define AVS_COAP_OPTION_URI_HOST 3
#define AVS_COAP_OPTION_ETAG 4
#define AVS_COAP_OPTION_IF_NONE_MATCH 5
#define AVS_COAP_OPTION_OBSERVE 6
#define AVS_COAP_OPTION_URI_PORT 7
#define AVS_COAP_OPTION_LOCATION_PATH 8
#define AVS_COAP_OPTION_OSCORE 9
#define AVS_COAP_OPTION_URI_PATH 11
#define AVS_COAP_OPTION_CONTENT_FORMAT 12
#define AVS_COAP_OPTION_MAX_AGE 14
#define AVS_COAP_OPTION_URI_QUERY 15
#define AVS_COAP_OPTION_ACCEPT 17
#define AVS_COAP_OPTION_LOCATION_QUERY 20
#define AVS_COAP_OPTION_BLOCK2 23
#define AVS_COAP_OPTION_BLOCK1 27
#define AVS_COAP_OPTION_PROXY_URI 35
#define AVS_COAP_OPTION_PROXY_SCHEME 39
#define AVS_COAP_OPTION_SIZE1 60
/** @} */

/** Minimum size, in bytes, of a CoAP BLOCK message payload. */
#define AVS_COAP_BLOCK_MIN_SIZE (1 << 4)
/** Maximum size, in bytes, of a CoAP BLOCK message payload. */
#define AVS_COAP_BLOCK_MAX_SIZE (1 << 10)
/** Maximum value of a BLOCK sequence number (2^20-1) allowed by RFC7959. */
#define AVS_COAP_BLOCK_MAX_SEQ_NUMBER 0xFFFFF

/**
 * A magic value used to indicate the absence of the Content-Format option.
 * Mainly used during CoAP message parsing, passing it to the opts object does
 * nothing.
 * */
#define AVS_COAP_FORMAT_NONE UINT16_MAX

/** Maximum size of ETag option, as defined in RFC7252. */
#define AVS_COAP_MAX_ETAG_LENGTH 8

typedef struct {
    uint8_t size;
    char bytes[AVS_COAP_MAX_ETAG_LENGTH];
} avs_coap_etag_t;

/**
 * BLOCK1/BLOCK2 option helpers.
 * @{
 */

/**
 * Helper enum used to distinguish BLOCK1 and BLOCK2 transfers in BLOCK APIs.
 */
typedef enum {
    AVS_COAP_BLOCK1, //< Block-wise request
    AVS_COAP_BLOCK2  //< Block-wise response
} avs_coap_option_block_type_t;

/**
 * Parsed CoAP BLOCK option.
 *
 * If is_bert is true, size is set to 1024. It doesn't indicate actual payload
 * size, because BERT message may contain multiple blocks of 1024 bytes each.
 * See RFC8323 for more details.
 */
typedef struct avs_coap_option_block {
    avs_coap_option_block_type_t type;
    uint32_t seq_num;
    bool has_more;
    uint16_t size;
    bool is_bert;
} avs_coap_option_block_t;

/** @} */

/**
 * Note: this struct MUST be initialized with either
 * @ref avs_coap_options_create_empty or @ref avs_coap_options_dynamic_init
 * before it is used.
 */
typedef struct avs_coap_options {
    void *begin;
    size_t size;
    size_t capacity;

    /**
     * If true, @ref avs_coap_options#begin is a heap-allocated buffer owned
     * by the options object (allocated using @ref avs_malloc ). This means
     * avs_coap_options_add_* functions are free to reallocate it as necessary.
     * In that case, @ref avs_coap_options_cleanup MUST be used to free the
     * memory.
     */
    bool allocated;
} avs_coap_options_t;

/**
 * @returns true if @p etag1 and @p etag2 are equal, false otherwise.
 */
static inline bool avs_coap_etag_equal(const avs_coap_etag_t *etag1,
                                       const avs_coap_etag_t *etag2) {
    return etag1->size == etag2->size
           && !memcmp(etag1->bytes, etag2->bytes, etag1->size);
}

/**
 * A structure containing hex representation of a ETag that may be created by
 * @ref avs_coap_etag_hex().
 */
typedef struct {
    char buf[AVS_COAP_MAX_ETAG_LENGTH * 2 + 1];
} avs_coap_etag_hex_t;

static inline const char *avs_coap_etag_hex(avs_coap_etag_hex_t *out_value,
                                            const avs_coap_etag_t *etag) {
    assert(etag->size <= 8);
    if (avs_hexlify(out_value->buf, sizeof(out_value->buf), NULL, etag->bytes,
                    etag->size)) {
        AVS_UNREACHABLE("avs_hexlify() failed");
    }
    return out_value->buf;
}

#define AVS_COAP_ETAG_HEX(Etag) \
    (avs_coap_etag_hex(&(avs_coap_etag_hex_t) { { 0 } }, (Etag)))

/**
 * @param buffer   Buffer to use for option storage.
 *
 *                 Note: the buffer MUST live at least as long as returned
 *                 options object.
 *
 * @param capacity Number of bytes available in @p buffer.
 *
 * @return An empty options object. It may be filled with CoAP options with
 *         @ref avs_coap_options_add functions.
 */
static inline avs_coap_options_t
avs_coap_options_create_empty(void *buffer, size_t capacity) {
    avs_coap_options_t opts;
    opts.begin = buffer;
    opts.size = 0;
    opts.capacity = capacity;
    opts.allocated = false;
    return opts;
}

/**
 * Resets @p opts to an empty state, cleaning up memory owned by @p opts if
 * applicable.
 *
 * After this function returns, @p opts should be considered deleted and MUST
 * NOT be used in any avs_coap_options_* call other than
 * @ref avs_coap_options_cleanup .
 */
static inline void avs_coap_options_cleanup(avs_coap_options_t *opts) {
    if (opts->allocated) {
        avs_free(opts->begin);
    }

    opts->begin = NULL;
    opts->size = 0;
    opts->capacity = 0;
    opts->allocated = false;
}

/**
 * Initializes an @ref avs_coap_options_t object so that it is backed by a
 * buffer allocated with @ref avs_malloc , and can be resized as required when
 * adding new options.
 *
 * @param opts             Uninitialized options object. Note: this function
 *                         MUST NOT be called on an already initialized object.
 *                         Doing so MAY result in resource leaks.
 *
 * @param initial_capacity Desired initial capacity of the options object.
 *
 * @returns @ref AVS_OK for success, or <c>avs_errno(AVS_ENOMEM)</c> if there is
 *          not enough memory. After this function returns, it is safe to call
 *          @ref avs_coap_options_cleanup on @p opts , regardless of the
 *          initialization result.
 */
static inline avs_error_t
avs_coap_options_dynamic_init_with_size(avs_coap_options_t *opts,
                                        size_t initial_capacity) {
    opts->begin = initial_capacity ? avs_malloc(initial_capacity) : NULL;
    opts->size = 0;
    opts->capacity = initial_capacity;
    opts->allocated = true;

    if (initial_capacity && !opts->begin) {
        avs_coap_options_cleanup(opts);
        return avs_errno(AVS_ENOMEM);
    }

    return AVS_OK;
}

#define AVS_COAP_DYNAMIC_OPTIONS_DEFAULT_SIZE 128

/**
 * Initializes an @ref avs_coap_options_t object with default initial capacity.
 * It's literally an "overload" for @ref avs_coap_options_dynamic_init_with_size
 * using @ref AVS_COAP_DYNAMIC_OPTIONS_DEFAULT_SIZE .
 *
 * @param opts Uninitialized options object. Note: this function MUST NOT be
 *             called on an already initialized object. Doing so MAY result in
 *             resource leaks.
 *
 * @returns @ref AVS_OK for success, or <c>avs_errno(AVS_ENOMEM)</c> if there is
 *          not enough memory. After this function returns, it is safe to call
 *          @ref avs_coap_options_cleanup on @p opts , regardless of the
 *          initialization result.
 */
static inline avs_error_t
avs_coap_options_dynamic_init(avs_coap_options_t *opts) {
    return avs_coap_options_dynamic_init_with_size(
            opts, AVS_COAP_DYNAMIC_OPTIONS_DEFAULT_SIZE);
}

/**
 * Removes all options with given @p option_number added to @p opts.
 */
void avs_coap_options_remove_by_number(avs_coap_options_t *opts,
                                       uint16_t option_number);

/**
 * @name avs_coap_options_add
 * Functions that may be used to set up CoAP options.
 *
 * @{
 */

/**
 * Sets a Content-Format Option (@ref AVS_COAP_OPT_CONTENT_FORMAT = 12) in
 * the options list.
 *
 * @param opts    Options object to operate on.
 * @param format  Numeric value of the Content-Format option. May be one of the
 *                AVS_COAP_FORMAT_* contants. Calling this function with
 *                @ref AVS_COAP_FORMAT_NONE removes the Content-Format option.
 *
 * @returns @ref AVS_OK for success, or an error condition for which the
 *          operation failed.
 */
avs_error_t avs_coap_options_set_content_format(avs_coap_options_t *opts,
                                                uint16_t format);

#ifdef WITH_AVS_COAP_BLOCK
/**
 * Adds the Block1 or Block2 Option to the message being built.
 *
 * @param opts  Options object to operate on.
 * @param block BLOCK option content to set.
 *
 * @returns @ref AVS_OK for success, or an error condition for which the
 *          operation failed.
 */
avs_error_t avs_coap_options_add_block(avs_coap_options_t *opts,
                                       const avs_coap_option_block_t *block);
#endif // WITH_AVS_COAP_BLOCK

#ifdef WITH_AVS_COAP_OBSERVE
/**
 * Adds the Observe option to @p opts . Options value is encoded as 24 least
 * significant bits of @p value , as defined in RFC7641.
 *
 * @returns @ref AVS_OK for success, or an error condition for which the
 *          operation failed.
 */
avs_error_t avs_coap_options_add_observe(avs_coap_options_t *opts,
                                         uint32_t value);

/**
 * Gets the Observe option from @p opts .
 *
 * @returns 0 on success, @ref AVS_COAP_OPTION_MISSING if option isn't present,
 *          a negative value if option is malformed (eg. it's longer than 3
 *          bytes).
 */
int avs_coap_options_get_observe(const avs_coap_options_t *opts,
                                 uint32_t *out_value);
#endif // WITH_AVS_COAP_OBSERVE

/**
 * Adds an arbitrary CoAP option with custom value.
 *
 * Repeated calls to this function APPEND additional instances of a CoAP option.
 *
 * @param opts          Options object to operate on.
 * @param opt_number    CoAP Option Number to set.
 * @param opt_data      Option value.
 * @param opt_data_size Number of bytes in the @p opt_data buffer.
 *
 * @returns
 *  - <c>AVS_OK</c> for success
 *  - <c>avs_errno(AVS_ENOMEM)</c> for an out-of-memory condition
 *  - @ref AVS_COAP_ERR_MESSAGE_TOO_BIG if @p opts is not dynamically allocated
 *    and is too small to fit the new option
 */
avs_error_t avs_coap_options_add_opaque(avs_coap_options_t *opts,
                                        uint16_t opt_number,
                                        const void *opt_data,
                                        uint16_t opt_data_size);

/**
 * Equivalent to:
 *
 * @code
 * avs_coap_options_add_opaque(opts, opt_number,
 *                             opt_data, strlen(opt_data))
 * @endcode
 */
avs_error_t avs_coap_options_add_string(avs_coap_options_t *opts,
                                        uint16_t opt_number,
                                        const char *opt_data);

/**
 * Adds an option with a printf-style formatted string value.
 *
 * @param opts       Options object to operate on.
 * @param opt_number CoAP Option Number to set.
 * @param format     Format string to pass to snprintf().
 * @param ...        Format arguments.
 *
 * @returns @ref AVS_OK for success, or an error condition for which the
 *          operation failed.
 */
avs_error_t avs_coap_options_add_string_f(avs_coap_options_t *opts,
                                          uint16_t opt_number,
                                          const char *format,
                                          ...) AVS_F_PRINTF(3, 4);

/**
 * va_list variant of @ref avs_coap_options_add_string_f .
 */
avs_error_t avs_coap_options_add_string_fv(avs_coap_options_t *opts,
                                           uint16_t opt_number,
                                           const char *format,
                                           va_list list);

/**
 * Adds an arbitrary CoAP option with no value.
 * See @ref avs_coap_options_add_opaque for more opts.
 */
avs_error_t avs_coap_options_add_empty(avs_coap_options_t *opts,
                                       uint16_t opt_number);

/**
 * Adds an ETag option.
 *
 * @param opts Options object to operate on.
 * @param etag ETag option to add.
 *
 * @returns
 *  - <c>AVS_OK</c> for success
 *  - <c>avs_errno(AVS_EINVAL)</c> if @p etag is invalid
 *  - <c>avs_errno(AVS_ENOMEM)</c> for an out-of-memory condition
 *  - @ref AVS_COAP_ERR_MESSAGE_TOO_BIG if @p opts is not dynamically allocated
 *    and is too small to fit the new option
 */
avs_error_t avs_coap_options_add_etag(avs_coap_options_t *opts,
                                      const avs_coap_etag_t *etag);

/** @{
 * Functions below add an arbitrary CoAP option with an integer value. The value
 * is encoded in the most compact way available, so e.g. for @p value equal to 0
 * the option has no payload when added using any of them.
 *
 * See @ref avs_coap_options_add_opaque for more opts.
 */
avs_error_t avs_coap_options_add_uint(avs_coap_options_t *opts,
                                      uint16_t opt_number,
                                      const void *value,
                                      size_t value_size);

static inline avs_error_t avs_coap_options_add_u16(avs_coap_options_t *opts,
                                                   uint16_t opt_number,
                                                   uint16_t value) {
    return avs_coap_options_add_uint(opts, opt_number, &value, sizeof(value));
}

static inline avs_error_t avs_coap_options_add_u32(avs_coap_options_t *opts,
                                                   uint16_t opt_number,
                                                   uint32_t value) {
    return avs_coap_options_add_uint(opts, opt_number, &value, sizeof(value));
}

/** @} */

/**
 * Iterator object used to access CoAP message options. It is not supposed
 * to be modified by the user after initialization.
 */
typedef struct {
    avs_coap_options_t *opts;
    void *curr_opt;
    uint32_t prev_opt_number;
} avs_coap_option_iterator_t;

/** Empty iterator object initializer. */
static const avs_coap_option_iterator_t AVS_COAP_OPTION_ITERATOR_EMPTY = { NULL,
                                                                           NULL,
                                                                           0 };

/**
 * @name avs_coap_options_get
 * Functions that may be used to get CoAP options.
 *
 * @{
 */

/**
 * Constant returned from some of option-retrieving functions, indicating
 * the absence of requested option.
 */
#define AVS_COAP_OPTION_MISSING 1

/**
 * @param[in]  opts      CoAP options to retrieve Content-Format option from.
 * @param[out] out_value Retrieved value of the Content-Format CoAP option.
 *
 * NOTE: Content-Format Option is not critical, thus only the first one found
 * (if any) will be returned.
 *
 * @returns @li 0 if the Content-Format was successfully retrieved and written
 *              to <c>*out_value</c>, or the option was missing, in which case
 *              <c>*out_value</c> is set to @ref AVS_COAP_FORMAT_NONE ,
 *          @li A negative value if the option was malformed.
 */
int avs_coap_options_get_content_format(const avs_coap_options_t *opts,
                                        uint16_t *out_value);

#ifdef WITH_AVS_COAP_BLOCK
/**
 * Attempts to obtain block info of given block @p type.
 *
 * @param[in]  opts      CoAP options to operate on.
 * @param[in]  type      Type of the BLOCK option to retrieve
 *                       (@ref avs_coap_option_block_type_t)
 * @param[out] out_block @ref avs_coap_option_block_t struct to store parsed
 *                       BLOCK option in.
 *
 * @returns @li 0 if the BLOCK option was successfully retrieved,
 *          @li @ref AVS_COAP_OPTION_MISSING if requested BLOCK option is not
 *              present,
 *          @li -1 in case of error, including cases where the option is
 *              malformed or duplicated.
 */
int avs_coap_options_get_block(const avs_coap_options_t *opts,
                               avs_coap_option_block_type_t type,
                               avs_coap_option_block_t *out_block);
#endif // WITH_AVS_COAP_BLOCK

/**
 * Skips the option pointed by @p inout_it without reading it.
 *
 * @returns 0 if option was successfully skipped, negative value if there is
 *          nothing to skip. After successfull call, @p inout_it points to the
 *          next option.
 */
int avs_coap_options_skip_it(avs_coap_option_iterator_t *inout_it);

/**
 * Iterates over CoAP options from @p opts that match given @p option_number ,
 * yielding their values as opaque byte arrays.
 *
 * @param[in]    opts            CoAP options to operate on.
 * @param[in]    option_number   CoAP option number to look for.
 * @param[inout] it              Option iterator object that holds iteration
 *                               state. When starting the iteration, it MUST be
 *                               set with @ref AVS_COAP_OPTION_ITERATOR_EMPTY .
 *                               Points to the next CoAP option after successful
 *                               call.
 * @param[out]   out_option_size Size of the option value. After successful call
 *                               this is equal to number of bytes written to
 *                               @p buffer.
 * @param[out]   buffer          Buffer to put option value into.
 * @param[in]    buffer_size     Number of bytes available in @p buffer .
 *
 * NOTES:
 * - When iterating over options using this function, @p option_number MUST
 *   remain unchanged, otherwise the behavior is undefined.
 * - The iterator state MUST NOT be changed by user code during the iteration.
 *   Doing so causes the behavior of this function to be undefined.
 * - If call isn't successful, function may be called again using the same
 *   @p it and a new @p buffer of size @p out_option_size to read the option
 *   value again. Option may be also skipped by using
 *   @ref avs_coap_options_skip_it .
 *
 * @returns @li 0 on success,
 *          @li AVS_COAP_OPTION_MISSING when there are no more options with
 *              given @p option_number to retrieve,
 *          @li a negative value if @p buffer is not big enough to hold the
 *              option value or.
 */
int avs_coap_options_get_bytes_it(const avs_coap_options_t *opts,
                                  uint16_t option_number,
                                  avs_coap_option_iterator_t *it,
                                  size_t *out_option_size,
                                  void *buffer,
                                  size_t buffer_size);

/**
 * Getter for value of the first occurrence of option with number
 * @p option_number .
 *
 * Works like @ref avs_coap_options_get_bytes_it , but doesn't use iterators
 * to read repeated options, so it shouldn't be used if options are repeatable.
 */
static inline int avs_coap_options_get_bytes(const avs_coap_options_t *opts,
                                             uint16_t option_number,
                                             size_t *out_option_size,
                                             void *buffer,
                                             size_t buffer_size) {
    avs_coap_option_iterator_t it = AVS_COAP_OPTION_ITERATOR_EMPTY;
    return avs_coap_options_get_bytes_it(opts, option_number, &it,
                                         out_option_size, buffer, buffer_size);
}

/**
 * Iterates over CoAP options from @p opts that match given @p option_number ,
 * yielding their values as zero-terminated strings.
 *
 * @param[in]    opts            CoAP options to operate on.
 * @param[in]    option_number   CoAP option number to look for.
 * @param[inout] it              Option iterator object that holds iteration
 *                               state. When starting the iteration, it MUST be
 *                               set with @ref AVS_COAP_OPTION_ITERATOR_EMPTY .
 *                               Points to the next CoAP option after successful
 *                               call.
 * @param[out]   out_option_size Size of the option value including terminating
 *                               nullbyte. After successful call this is equal
 *                               to number of bytes written to @p buffer.
 * @param[out]   buffer          Buffer to put option value into.
 * @param[in]    buffer_size     Number of bytes available in @p buffer .
 *
 * NOTES:
 * - When iterating over options using this function, @p option_number MUST
 *   remain unchanged, otherwise the behavior is undefined.
 * - The iterator state MUST NOT be changed by user code during the iteration.
 *   Doing so causes the behavior if this function to be undefined.
 * - If call isn't successful, function may be called again using the same
 *   @p it and a new @p buffer of size @p out_option_size to read the option
 *   value again. Option may be also skipped by using
 *   @ref avs_coap_options_skip_it .
 *
 * @returns @li 0 on success,
 *          @li AVS_COAP_OPTION_MISSING when there are no more options with
 *              given @p option_number to retrieve,
 *          @li a negative value if @p buffer is not big enough to hold the
 *              option value or terminating nullbyte.
 */
int avs_coap_options_get_string_it(const avs_coap_options_t *opts,
                                   uint16_t option_number,
                                   avs_coap_option_iterator_t *it,
                                   size_t *out_option_size,
                                   char *buffer,
                                   size_t buffer_size);

/**
 * Getter for value of the first occurrence of option with number
 * @p option_number .
 *
 * Works like @ref avs_coap_options_get_string_it , but doesn't use iterators
 * to read repeated options, so it shouldn't be used if options are repeatable.
 */
static inline int avs_coap_options_get_string(const avs_coap_options_t *opts,
                                              uint16_t option_number,
                                              size_t *out_option_size,
                                              char *buffer,
                                              size_t buffer_size) {
    avs_coap_option_iterator_t it = AVS_COAP_OPTION_ITERATOR_EMPTY;
    return avs_coap_options_get_string_it(opts, option_number, &it,
                                          out_option_size, buffer, buffer_size);
}

/**
 * Finds a unique CoAP option with a 16-bit unsigned integer value.
 *
 * @param[in]  opts          CoAP options to operate on.
 * @param[in]  option_number CoAP option number to find.
 * @param[out] out_value     Pointer to variable to store the option value in.
 *
 * @returns @li 0 if exactly one option with given @p option_number was found,
 *              and its integer value was successfully put into @p out_value,
 *          @li AVS_COAP_OPTION_MISSING if @p opts does not contain any option
 *              with given @p option_number ,
 *          @li a negative value if multiple options with given @p option_number
 *              were found or the option value is too large to be stored in a
 *              16-bit variable
 */
int avs_coap_options_get_u16(const avs_coap_options_t *opts,
                             uint16_t option_number,
                             uint16_t *out_value);

/**
 * Finds a unique CoAP option with a 32-bit unsigned integer value.
 *
 * @param[in]  opts          CoAP options to operate on.
 * @param[in]  option_number CoAP option number to find.
 * @param[out] out_value     Pointer to variable to store the option value in.
 *
 * @returns @li 0 if exactly one option with given @p option_number was found,
 *              and its integer value was successfully put into @p out_value,
 *          @li AVS_COAP_OPTION_MISSING if @p opts does not contain any option
 *              with given @p option_number ,
 *          @li a negative value if multiple options with given @p option_number
 *              were found or the option value is too large to be stored in a
 *              32-bit variable
 */
int avs_coap_options_get_u32(const avs_coap_options_t *opts,
                             uint16_t option_number,
                             uint32_t *out_value);

/**
 * Iterates over ETag options from @p opts .
 *
 * @param[in]    opts     CoAP options to operate on.
 * @param[inout] it       Option iterator object that holds iteration state.
 *                        When starting the iteration, it MUST be
 *                        set with @ref AVS_COAP_OPTION_ITERATOR_EMPTY .
 * @param[out]   out_etag Pointer to store ETag to
 *
 * NOTES:
 * - The iterator state MUST NOT be changed by user code during the iteration.
 *   Doing so causes the behavior of this function to be undefined.
 *
 * @returns @li 0 on success,
 *          @li AVS_COAP_OPTION_MISSING when there are no more ETags,
 *          @li a negative value if ETag is longer than
 *              @ref AVS_COAP_MAX_ETAG_LENGTH .
 *
 * If ETag is missing or malformed, @p out_etag is filled with zeros.
 */
int avs_coap_options_get_etag_it(const avs_coap_options_t *opts,
                                 avs_coap_option_iterator_t *it,
                                 avs_coap_etag_t *out_etag);

/**
 * Getter for the first occurrence of ETag in @p opts .
 *
 * Works like @ref avs_coap_options_get_etag_it , but doesn't use iterators to
 * read repeated options. It shouldn't be used to retrieve ETags from requests,
 * because they might be repeated.
 */
static inline int avs_coap_options_get_etag(const avs_coap_options_t *opts,
                                            avs_coap_etag_t *out_etag) {
    avs_coap_option_iterator_t it = AVS_COAP_OPTION_ITERATOR_EMPTY;
    return avs_coap_options_get_etag_it(opts, &it, out_etag);
}

/** @} */

#ifdef __cplusplus
}
#endif

#endif // AVSYSTEM_COAP_OPTION_H
