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

#include <avs_coap_init.h>

#include <string.h>

#include <avsystem/commons/avs_utils.h>

#include <avsystem/coap/code.h>
#include <avsystem/coap/option.h>

#include "options/avs_coap_iterator.h"
#include "options/avs_coap_option.h"

#define MODULE_NAME coap
#include <avs_coap_x_log_config.h>

#include "options/avs_coap_options.h"

#define MAX_OBSERVE_OPTION_VALUE (0xFFFFFF)

VISIBILITY_SOURCE_BEGIN

#ifdef WITH_AVS_COAP_BLOCK

static int fill_block_data(const avs_coap_option_t *block_opt,
                           uint32_t opt_number,
                           avs_coap_option_block_t *out_info) {
    assert(opt_number == AVS_COAP_OPTION_BLOCK1
           || opt_number == AVS_COAP_OPTION_BLOCK2);
    out_info->type = (opt_number == AVS_COAP_OPTION_BLOCK1) ? AVS_COAP_BLOCK1
                                                            : AVS_COAP_BLOCK2;

    // RFC 7959, Table 1 defines BLOCK1/2 option length as 0-3 bytes
    static const uint32_t MAX_BLOCK_DATA_SIZE = 3;

    if (_avs_coap_option_content_length(block_opt) > MAX_BLOCK_DATA_SIZE
            || _avs_coap_option_block_seq_number(block_opt, &out_info->seq_num)
            || _avs_coap_option_block_has_more(block_opt, &out_info->has_more)
            || _avs_coap_option_block_size(block_opt, &out_info->size,
                                           &out_info->is_bert)) {
        LOG(DEBUG, _("malformed BLOCK") "%d" _(" option"),
            opt_number == AVS_COAP_OPTION_BLOCK1 ? 1 : 2);
        return -1;
    }
    return 0;
}

static bool is_block_option_content_valid(const avs_coap_option_t *block_opt,
                                          uint32_t opt_number) {
    // Attempt to parse the BLOCK1/BLOCK2 option. This operation will fail in
    // case the option content is not well-formed.
    return (fill_block_data(block_opt, opt_number,
                            &(avs_coap_option_block_t) {
                                .type = AVS_COAP_BLOCK1
                            })
            == 0);
}

static avs_error_t
block_type_from_code(uint8_t code, avs_coap_option_block_type_t *out_type) {
    if (avs_coap_code_is_request(code)) {
        *out_type = AVS_COAP_BLOCK1;
        return AVS_OK;
    } else if (avs_coap_code_is_response(code)) {
        *out_type = AVS_COAP_BLOCK2;
        return AVS_OK;
    } else {
        LOG(DEBUG, "%s" _(" is neither a request nor response"),
            AVS_COAP_CODE_STRING(code));
        return avs_errno(AVS_EINVAL);
    }
}

avs_error_t
_avs_coap_options_get_block_by_code(const avs_coap_options_t *options,
                                    uint8_t code,
                                    avs_coap_option_block_t *out_block,
                                    bool *out_has_block) {
    avs_coap_option_block_type_t type;
    avs_error_t err = block_type_from_code(code, &type);
    if (avs_is_err(err)) {
        return err;
    }

    int opts_result = avs_coap_options_get_block(options, type, out_block);
    switch (opts_result) {
    case 0:
    case AVS_COAP_OPTION_MISSING:
        *out_has_block = (opts_result == 0);
        return AVS_OK;
    default:
        AVS_UNREACHABLE("malformed options got through packet validation");
        return _avs_coap_err(AVS_COAP_ERR_ASSERT_FAILED);
    }
}

#endif // WITH_AVS_COAP_BLOCK

bool _avs_coap_options_valid_until_payload_marker(
        const avs_coap_options_t *opts,
        size_t *out_actual_size,
        bool *out_truncated,
        bool *out_payload_marker_reached) {
    if (out_truncated) {
        *out_truncated = false;
    }
    if (out_payload_marker_reached) {
        *out_payload_marker_reached = false;
    }

    if (opts->size > opts->capacity) {
        LOG(DEBUG, _("unexpected size (") "%s" _(") > capacity (") "%s" _(")"),
            AVS_UINT64_AS_STRING(opts->size),
            AVS_UINT64_AS_STRING(opts->capacity));
        return false;
    }

    // non-repeatable critical options must not be present more than once
    struct {
        const uint16_t number;
        bool found;
    } non_repeatable_critical_options[] = {
        // clang-format off
        { AVS_COAP_OPTION_URI_HOST,       false },
        { AVS_COAP_OPTION_IF_NONE_MATCH,  false },
        { AVS_COAP_OPTION_URI_PORT,       false },
        { AVS_COAP_OPTION_OSCORE,         false },
        { AVS_COAP_OPTION_ACCEPT,         false },
        { AVS_COAP_OPTION_BLOCK2,         false },
        { AVS_COAP_OPTION_BLOCK1,         false },
        { AVS_COAP_OPTION_PROXY_URI,      false },
        { AVS_COAP_OPTION_PROXY_SCHEME,   false }
        // clang-format on
    };

    avs_coap_option_iterator_t it =
            _avs_coap_optit_begin((avs_coap_options_t *) (intptr_t) opts);

    for (; !_avs_coap_optit_end(&it); _avs_coap_optit_next(&it)) {
        assert(it.curr_opt >= opts->begin);

        const uint8_t *opts_begin = (const uint8_t *) opts->begin;
        size_t opt_offset =
                (size_t) ((const uint8_t *) it.curr_opt - opts_begin);

        assert(opts->size >= opt_offset);
        size_t bytes_available = opts->size - opt_offset;

        const avs_coap_option_t *opt = _avs_coap_optit_current(&it);
        if (!_avs_coap_option_is_valid(opt, bytes_available)) {
            LOG(DEBUG, _("malformed CoAP option at offset ") "%u",
                (unsigned) ((const uint8_t *) it.curr_opt - opts_begin));
            if (out_truncated) {
                *out_truncated = true;
            }
            return false;
        }

        uint32_t opt_number = _avs_coap_optit_number(&it);
        if (opt_number > UINT16_MAX) {
            LOG(DEBUG,
                _("invalid CoAP option number (") "%" PRIu32 _(" > 65535)"),
                opt_number);
            return false;
        }

        for (size_t i = 0; i < AVS_ARRAY_SIZE(non_repeatable_critical_options);
             ++i) {
            AVS_ASSERT(_avs_coap_option_is_critical(
                               non_repeatable_critical_options[i].number),
                       "every elective option can be repeated");
            if (non_repeatable_critical_options[i].number == opt_number) {
                if (non_repeatable_critical_options[i].found) {
                    LOG(DEBUG,
                        _("duplicated non-repeatable critical CoAP "
                          "option ") "%" PRIu32,
                        opt_number);
                    return false;
                }
                non_repeatable_critical_options[i].found = true;
            }
        }

        switch (opt_number) {
        case AVS_COAP_OPTION_BLOCK1:
        case AVS_COAP_OPTION_BLOCK2:
#ifdef WITH_AVS_COAP_BLOCK
            if (!is_block_option_content_valid(opt, opt_number)) {
                return false;
            }
            break;
#else  // WITH_AVS_COAP_BLOCK
            LOG(DEBUG, _("BLOCK option received, but BLOCKs are disabled"));
            return false;
#endif // WITH_AVS_COAP_BLOCK
        default:
            break;
        }
    }

    AVS_ASSERT(out_actual_size, "use _avs_coap_options_valid instead");
    *out_actual_size = (size_t) ((const uint8_t *) it.curr_opt
                                 - (const uint8_t *) it.opts->begin);
    // If options parser didn't reach the end of buffer and the next byte
    // is a payload marker, *out_payload_marker_reached is set. Otherwise,
    // it's possible that more options will arrive in the next packet (if TCP
    // is used).
    if (out_payload_marker_reached) {
        const bool all_bytes_parsed = (*out_actual_size == opts->capacity);
        if (!all_bytes_parsed
                && *(const uint8_t *) it.curr_opt == AVS_COAP_PAYLOAD_MARKER) {
            *out_payload_marker_reached = true;
        }
    }

    return true;
}

bool _avs_coap_options_valid(const avs_coap_options_t *opts) {
    size_t actual_size;

    if (!_avs_coap_options_valid_until_payload_marker(opts, &actual_size, NULL,
                                                      NULL)) {
        return false;
    }

    if (opts->size != actual_size) {
        LOG(DEBUG,
            _("size mismatch: declared ") "%" PRIu32 _(", actual ") "%" PRIu32,
            (uint32_t) opts->size, (uint32_t) actual_size);
        return false;
    }

    return true;
}

void avs_coap_options_remove_by_number(avs_coap_options_t *opts,
                                       uint16_t option_number) {
    avs_coap_option_iterator_t optit = _avs_coap_optit_begin(opts);

    while (!_avs_coap_optit_end(&optit)
           && _avs_coap_optit_number(&optit) < option_number) {
        _avs_coap_optit_next(&optit);
    }

    while (!_avs_coap_optit_end(&optit)
           && _avs_coap_optit_number(&optit) == option_number) {
        _avs_coap_optit_erase(&optit);
    }
}

avs_error_t avs_coap_options_set_content_format(avs_coap_options_t *opts,
                                                uint16_t format) {
    avs_coap_options_remove_by_number(opts, AVS_COAP_OPTION_CONTENT_FORMAT);

    if (format == AVS_COAP_FORMAT_NONE) {
        return AVS_OK;
    }

    return avs_coap_options_add_u16(opts, AVS_COAP_OPTION_CONTENT_FORMAT,
                                    format);
}

avs_error_t avs_coap_options_add_etag(avs_coap_options_t *opts,
                                      const avs_coap_etag_t *etag) {
    if (etag->size > AVS_COAP_MAX_ETAG_LENGTH) {
        LOG(ERROR, _("invalid ETag with length >") "%d" _(" bytes"),
            AVS_COAP_MAX_ETAG_LENGTH);
        return avs_errno(AVS_EINVAL);
    }
    return avs_coap_options_add_opaque(opts, AVS_COAP_OPTION_ETAG, etag->bytes,
                                       etag->size);
}

#ifdef WITH_AVS_COAP_BLOCK

static avs_error_t encode_block_size(uint16_t size,
                                     uint8_t *out_size_exponent) {
    switch (size) {
    case 16:
        *out_size_exponent = 0;
        break;
    case 32:
        *out_size_exponent = 1;
        break;
    case 64:
        *out_size_exponent = 2;
        break;
    case 128:
        *out_size_exponent = 3;
        break;
    case 256:
        *out_size_exponent = 4;
        break;
    case 512:
        *out_size_exponent = 5;
        break;
    case 1024:
        *out_size_exponent = 6;
        break;
    default:
        LOG(ERROR,
            _("invalid block size: ") "%d" _(", expected power of 2 between 16 "
                                             "and 1024 (inclusive)"),
            (int) size);
        return avs_errno(AVS_EINVAL);
    }

    return AVS_OK;
}

static avs_error_t add_block_opt(avs_coap_options_t *opts,
                                 uint16_t option_number,
                                 uint32_t seq_number,
                                 bool is_last_chunk,
                                 uint16_t size,
                                 bool is_bert) {
    uint8_t size_exponent;
    avs_error_t err = encode_block_size(size, &size_exponent);
    if (avs_is_err(err)) {
        return err;
    }

    // [TCP] If peer sent a BERT request for example, we have to respond with
    // message with BERT option, to avoid size renegotiation, which may be
    // confusing, because in CSM we said, that we support BERT.
    if (is_bert) {
        if (size_exponent == AVS_COAP_OPT_BLOCK_MAX_SZX) {
            size_exponent = AVS_COAP_OPT_BERT_SZX;
        } else {
            LOG(ERROR,
                _("unexpected size_exponent ") "%d" _(
                        " for option with BERT flag set, size should be 1024"),
                size_exponent);
            return avs_errno(AVS_EINVAL);
        }
    }

    AVS_STATIC_ASSERT(sizeof(int) >= sizeof(int32_t), int_type_too_small);
    if (seq_number >= (1 << 20)) {
        LOG(ERROR, _("block sequence number must be less than 2^20"));
        return avs_errno(AVS_ERANGE);
    }

    uint32_t value = ((seq_number & 0x000fffff) << 4)
                     | ((uint32_t) is_last_chunk << 3)
                     | (uint32_t) size_exponent;
    return avs_coap_options_add_u32(opts, option_number, value);
}

avs_error_t avs_coap_options_add_block(avs_coap_options_t *opts,
                                       const avs_coap_option_block_t *block) {
    return add_block_opt(opts,
                         _avs_coap_option_num_from_block_type(block->type),
                         block->seq_num, block->has_more, block->size,
                         block->is_bert);
}

#endif // WITH_AVS_COAP_BLOCK

#ifdef WITH_AVS_COAP_OBSERVE

avs_error_t avs_coap_options_add_observe(avs_coap_options_t *opts,
                                         uint32_t value) {
    value &= MAX_OBSERVE_OPTION_VALUE;
    return avs_coap_options_add_u32(opts, AVS_COAP_OPTION_OBSERVE, value);
}

int avs_coap_options_get_observe(const avs_coap_options_t *opts,
                                 uint32_t *value) {
    int result = avs_coap_options_get_u32(opts, AVS_COAP_OPTION_OBSERVE, value);
    if (!result && *value > MAX_OBSERVE_OPTION_VALUE) {
        result = -1;
    }
    return result;
}

#endif // WITH_AVS_COAP_OBSERVE

/*
 * Changing option delta field on a CoAP option may shorten its header
 * by a byte or two.
 */
static size_t bytes_gained_by_reducing_delta(const avs_coap_option_t *opt,
                                             uint16_t new_opt_delta) {
    size_t content_length = _avs_coap_option_content_length(opt);

    size_t old_opt_delta = _avs_coap_option_delta(opt);
    assert(new_opt_delta <= old_opt_delta);
    size_t old_hdr_size =
            _avs_coap_get_opt_header_size(old_opt_delta, content_length);

    size_t new_hdr_size =
            _avs_coap_get_opt_header_size(new_opt_delta, content_length);
    assert(old_hdr_size >= new_hdr_size);
    return old_hdr_size - new_hdr_size;
}

/**
 * Will a new option with @p new_opt_number, which is @p new_opt_sizeof bytes
 * in size (including header) fit into options object associated with
 * @p insert_it when it would be inserted in the place @p insert_it currently
 * points to?
 */
static bool new_option_fits(const avs_coap_option_iterator_t *insert_it,
                            uint16_t new_opt_number,
                            size_t new_opt_sizeof) {
    size_t bytes_available = insert_it->opts->capacity - insert_it->opts->size;

    if (!_avs_coap_optit_end(insert_it)) {
        uint32_t delta = _avs_coap_optit_number(insert_it) - new_opt_number;
        assert(delta <= UINT16_MAX);

        const avs_coap_option_t *opt = _avs_coap_optit_current(insert_it);
        bytes_available +=
                bytes_gained_by_reducing_delta(opt, (uint16_t) delta);
    }

    return bytes_available >= new_opt_sizeof;
}

/**
 * Rewrite option delta field of the CoAP option currently pointed to by @p it
 * with @p new_delta, which MUST be no larger than current one.
 *
 * Size of the <c>it->opts</c> object is adjusted accordingly. @p it is still
 * valid after the function returns, and points to the redelta'd option.
 */
static void update_option_delta_in_place(const avs_coap_option_iterator_t *it,
                                         uint16_t new_delta) {
    avs_coap_option_t *opt = _avs_coap_optit_current(it);

    /* Only reducing option delta is supported */
    assert(new_delta <= _avs_coap_option_delta(opt));

    size_t opt_sizeof = _avs_coap_option_sizeof(opt);
    const uint8_t *old_opt_end = (const uint8_t *) opt + opt_sizeof;

    /*
     * NOTE: this ends up overwriting the option with itself, but never
     * with a *longer* value.
     */
    size_t written =
            _avs_coap_option_serialize((uint8_t *) opt, opt_sizeof, new_delta,
                                       _avs_coap_option_value(opt),
                                       _avs_coap_option_content_length(opt));

    /*
     * If rewriting changed header size, previous steps leave a gap between
     * redelta'd option and all other ones. Shift all following options to
     * remove that gap.
     */
    uint8_t *new_opt_end = (uint8_t *) opt + written;
    assert(old_opt_end >= new_opt_end);

    const uint8_t *old_options_end =
            (const uint8_t *) it->opts->begin + it->opts->size;
    assert(old_opt_end <= old_options_end);
    memmove(new_opt_end, old_opt_end, (size_t) (old_options_end - old_opt_end));

    size_t gap_size = (size_t) (old_opt_end - new_opt_end);
    it->opts->size -= gap_size;
}

static avs_error_t grow_if_required(avs_coap_options_t *opts,
                                    uint16_t new_data_size) {
    if (!opts->allocated) {
        return AVS_OK;
    }

    // 1 header + 2 ext delta + 2 ext length
    static const size_t MAX_OPT_HEADER_SIZE = 5;
    size_t desired_capacity = opts->size + MAX_OPT_HEADER_SIZE + new_data_size;

    if (opts->capacity < desired_capacity) {
        void *new_buf = avs_realloc(opts->begin, desired_capacity);
        if (!new_buf) {
            return avs_errno(AVS_ENOMEM);
        }

        opts->begin = new_buf;
        opts->capacity = desired_capacity;
    }

    return AVS_OK;
}

avs_error_t avs_coap_options_add_opaque(avs_coap_options_t *opts,
                                        uint16_t opt_number,
                                        const void *opt_data,
                                        uint16_t opt_data_size) {
    avs_error_t err = grow_if_required(opts, opt_data_size);
    if (avs_is_err(err)) {
        return err;
    }

    avs_coap_option_iterator_t insert_it = _avs_coap_optit_begin(opts);
    while (!_avs_coap_optit_end(&insert_it)
           && _avs_coap_optit_number(&insert_it) <= opt_number) {
        _avs_coap_optit_next(&insert_it);
    }

    assert(opt_number >= insert_it.prev_opt_number);
    size_t opt_num_delta = (uint32_t) opt_number - insert_it.prev_opt_number;
    assert(opt_num_delta <= UINT16_MAX);

    size_t bytes_required =
            _avs_coap_get_opt_header_size(opt_num_delta, opt_data_size)
            + opt_data_size;

    if (!new_option_fits(&insert_it, opt_number, bytes_required)) {
        LOG(ERROR, _("options buffer too small to fit another option"));
        return _avs_coap_err(AVS_COAP_ERR_MESSAGE_TOO_BIG);
    }

    /*
     * Insert a new option into a buffer full of serialized options.
     *
     * insert_ptr -.                                 .- old_opts_end
     *             v                                 v
     *        -----+--------------+------------     -+
     *         ... |   next_opt   | other opts  ...  |
     *        -----+--------------+------------     -+
     *  [1]        |           .--'
     *             v           v
     *        -----+-----------+---------------
     *         ... | next_opt' | other opts...
     *        -----+-----------+---------------
     *  [2]        |           '- - - - - - - - - .
     *             '------------------.           |
     *                                v           v
     *        -----+------------------+-----------+---------------
     *         ... | [bytes_required] | next_opt' | other opts...
     *        -----+------------------+-----------+---------------
     *  [3]        |                  |
     *             v                  v
     *        -----+------------------+-----------+---------------
     *         ... |    new option    | next_opt' | other opts...
     *        -----+------------------+-----------+---------------
     *
     * TODO: remove an extra memmove by moving other opts right first
     * and then rewriting next_opt' in its final place
     */

    if (!_avs_coap_optit_end(&insert_it)) {
        /*
         * [1] next_opt option, if it exists, may require updating its option
         * number delta. This may even shorten its header by a byte or two.
         */
        uint32_t new_next_delta =
                _avs_coap_optit_number(&insert_it) - opt_number;
        assert(new_next_delta <= UINT16_MAX);
        update_option_delta_in_place(&insert_it, (uint16_t) new_next_delta);
    }

    /*
     * [2] Now move next_opt' and other opts forward to make bytes_required
     * free space for the new option.
     *
     * NOTE: _avs_coap_optit_current is not supposed to be used when the
     * iterator points to "end"; use curr_opt directly instead
     */
    uint8_t *insert_ptr = (uint8_t *) insert_it.curr_opt;
    assert(insert_ptr >= (uint8_t *) opts->begin);
    size_t insert_offset = (size_t) (insert_ptr - (uint8_t *) opts->begin);

    memmove(insert_ptr + bytes_required, insert_ptr,
            opts->size - insert_offset);

    opts->size += bytes_required;
    assert(opts->size <= opts->capacity);

    /*
     * [3] Finally, serialize the new option into freed space.
     */
    const size_t written =
            _avs_coap_option_serialize(insert_ptr, bytes_required,
                                       opt_num_delta, opt_data, opt_data_size);
    assert(written == bytes_required);
    (void) written;
    return AVS_OK;
}

avs_error_t avs_coap_options_add_string(avs_coap_options_t *opts,
                                        uint16_t opt_number,
                                        const char *opt_data) {
    size_t size = strlen(opt_data);
    if (size > UINT16_MAX) {
        return avs_errno(AVS_ERANGE);
    }

    return avs_coap_options_add_opaque(opts, opt_number, opt_data,
                                       (uint16_t) size);
}

avs_error_t avs_coap_options_add_string_fv(avs_coap_options_t *opts,
                                           uint16_t opt_number,
                                           const char *format,
                                           va_list list) {
    va_list list2;
    va_copy(list2, list);
    int result = vsnprintf(NULL, 0, format, list2);
    va_end(list2);

    if (result < 0 || result > UINT16_MAX) {
        LOG(DEBUG,
            _("invalid formatted option size: ") "%d" _(", expected integer in "
                                                        "range [0; 65535]"),
            result);
        return avs_errno(AVS_ERANGE);
    }

    uint16_t size = (uint16_t) result;
    char *buf = (char *) avs_malloc(size + 1u); // +1 for nullbyte
    if (!buf) {
        LOG(ERROR, _("out of memory"));
        return avs_errno(AVS_ENOMEM);
    }

    int written = vsnprintf(buf, size + 1u, format, list);
    assert(written == (int) size);
    (void) written;

    avs_error_t err = avs_coap_options_add_opaque(opts, opt_number, buf, size);
    avs_free(buf);
    return err;
}

avs_error_t avs_coap_options_add_string_f(avs_coap_options_t *opts,
                                          uint16_t opt_number,
                                          const char *format,
                                          ...) {
    va_list list;
    va_start(list, format);
    avs_error_t err =
            avs_coap_options_add_string_fv(opts, opt_number, format, list);
    va_end(list);
    return err;
}

avs_error_t avs_coap_options_add_empty(avs_coap_options_t *opts,
                                       uint16_t opt_number) {
    return avs_coap_options_add_opaque(opts, opt_number, "", 0);
}

avs_error_t avs_coap_options_add_uint(avs_coap_options_t *opts,
                                      uint16_t opt_number,
                                      const void *value,
                                      size_t value_size) {
#ifdef AVS_COMMONS_BIG_ENDIAN
    const uint8_t *converted = (const uint8_t *) value;
#else
    AVS_ASSERT(value_size <= 8,
               "uint options larger than 64 bits are not supported");
    uint8_t converted[8];
    for (size_t i = 0; i < value_size; ++i) {
        converted[value_size - 1 - i] = ((const uint8_t *) value)[i];
    }
#endif
    size_t start = 0;
    while (start < value_size && !converted[start]) {
        ++start;
    }
    return avs_coap_options_add_opaque(opts, opt_number, &converted[start],
                                       (uint16_t) (value_size - start));
}

const avs_coap_option_t *
_avs_coap_options_find_first_opt(const avs_coap_options_t *opts,
                                 uint16_t opt_number) {
    // TODO: const_cast; maybe const_iterator could be nice?
    for (avs_coap_option_iterator_t it =
                 _avs_coap_optit_begin((avs_coap_options_t *) (intptr_t) opts);
         !_avs_coap_optit_end(&it);
         _avs_coap_optit_next(&it)) {
        uint32_t curr_opt_number = _avs_coap_optit_number(&it);

        if (curr_opt_number == opt_number) {
            return _avs_coap_optit_current(&it);
        } else if (curr_opt_number > opt_number) {
            return NULL;
        }
    }

    return NULL;
}

int avs_coap_options_get_content_format(const avs_coap_options_t *opts,
                                        uint16_t *out_value) {
    assert(opts);
    assert(out_value);
    const avs_coap_option_t *opt =
            _avs_coap_options_find_first_opt(opts,
                                             AVS_COAP_OPTION_CONTENT_FORMAT);
    if (!opt) {
        *out_value = AVS_COAP_FORMAT_NONE;
        return 0;
    }
    return _avs_coap_option_u16_value(opt, out_value);
}

#ifdef WITH_AVS_COAP_BLOCK
int avs_coap_options_get_block(const avs_coap_options_t *opts,
                               avs_coap_option_block_type_t type,
                               avs_coap_option_block_t *out_info) {
    assert(opts);
    assert(out_info);
    uint16_t opt_number = type == AVS_COAP_BLOCK1 ? AVS_COAP_OPTION_BLOCK1
                                                  : AVS_COAP_OPTION_BLOCK2;
    memset(out_info, 0, sizeof(*out_info));
    const avs_coap_option_t *opt =
            _avs_coap_options_find_first_opt(opts, opt_number);
    if (!opt) {
        return AVS_COAP_OPTION_MISSING;
    }
    return fill_block_data(opt, opt_number, out_info);
}
#endif // WITH_AVS_COAP_BLOCK

int avs_coap_options_get_u16(const avs_coap_options_t *opts,
                             uint16_t option_number,
                             uint16_t *out_value) {
    const avs_coap_option_t *opt =
            _avs_coap_options_find_first_opt(opts, option_number);
    if (!opt) {
        LOG(TRACE, _("option ") "%d" _(" not found"), option_number);
        return AVS_COAP_OPTION_MISSING;
    }

    return _avs_coap_option_u16_value(opt, out_value);
}

int avs_coap_options_get_u32(const avs_coap_options_t *opts,
                             uint16_t option_number,
                             uint32_t *out_value) {
    const avs_coap_option_t *opt =
            _avs_coap_options_find_first_opt(opts, option_number);
    if (!opt) {
        LOG(TRACE, _("option ") "%d" _(" not found"), option_number);
        return AVS_COAP_OPTION_MISSING;
    }

    return _avs_coap_option_u32_value(opt, out_value);
}

static int get_option_it(const avs_coap_options_t *opts,
                         uint16_t option_number,
                         avs_coap_option_iterator_t *it,
                         size_t *out_opt_size,
                         void *buffer,
                         size_t buffer_size,
                         int (*fetch_value)(const avs_coap_option_t *opt,
                                            size_t *out_opt_size,
                                            void *buffer,
                                            size_t buffer_size)) {
    if (!it->opts) {
        // TODO: const_cast; maybe const_iterator could be nice?
        *it = _avs_coap_optit_begin((avs_coap_options_t *) (intptr_t) opts);
    } else {
        assert(it->opts == opts);
    }

    int retval = AVS_COAP_OPTION_MISSING;
    for (; !_avs_coap_optit_end(it); _avs_coap_optit_next(it)) {
        if (_avs_coap_optit_number(it) == option_number) {
            retval = fetch_value(_avs_coap_optit_current(it), out_opt_size,
                                 buffer, buffer_size);
            break;
        }
    }

    if (!retval) {
        _avs_coap_optit_next(it);
    }

    return retval;
}

static int fetch_bytes(const avs_coap_option_t *opt,
                       size_t *out_option_size,
                       void *buffer,
                       size_t buffer_size) {
    *out_option_size = _avs_coap_option_content_length(opt);

    if (buffer_size < *out_option_size) {
        LOG(DEBUG, _("buffer too small to hold entire option"));
        return -1;
    }

    memcpy(buffer, _avs_coap_option_value(opt), *out_option_size);
    return 0;
}

int avs_coap_options_skip_it(avs_coap_option_iterator_t *inout_it) {
    if (!_avs_coap_optit_end(inout_it)) {
        _avs_coap_optit_next(inout_it);
        return 0;
    }
    return -1;
}

int avs_coap_options_get_bytes_it(const avs_coap_options_t *opts,
                                  uint16_t option_number,
                                  avs_coap_option_iterator_t *it,
                                  size_t *out_option_size,
                                  void *buffer,
                                  size_t buffer_size) {
    return get_option_it(opts, option_number, it, out_option_size, buffer,
                         buffer_size, fetch_bytes);
}

int avs_coap_options_get_etag_it(const avs_coap_options_t *opts,
                                 avs_coap_option_iterator_t *it,
                                 avs_coap_etag_t *out_etag) {
    size_t bytes_read = 0;
    int retval = get_option_it(opts, AVS_COAP_OPTION_ETAG, it, &bytes_read,
                               out_etag->bytes, sizeof(out_etag->bytes),
                               fetch_bytes);
    if (!retval) {
        assert(bytes_read <= sizeof(out_etag->bytes));
        out_etag->size = (uint8_t) bytes_read;
    } else {
        *out_etag = (avs_coap_etag_t) { 0 };
    }
    return retval;
}

static int fetch_string(const avs_coap_option_t *opt,
                        size_t *out_option_size,
                        void *buffer,
                        size_t buffer_size) {
    return _avs_coap_option_string_value(opt, out_option_size, (char *) buffer,
                                         buffer_size);
}

int avs_coap_options_get_string_it(const avs_coap_options_t *opts,
                                   uint16_t option_number,
                                   avs_coap_option_iterator_t *it,
                                   size_t *out_option_size,
                                   char *buffer,
                                   size_t buffer_size) {
    return get_option_it(opts, option_number, it, out_option_size, buffer,
                         buffer_size, fetch_string);
}

bool _avs_coap_option_exists(const avs_coap_options_t *opts,
                             uint16_t opt_number) {
    return _avs_coap_options_find_first_opt(opts, opt_number) != NULL;
}

static bool is_option_identical(avs_coap_option_t *a, avs_coap_option_t *b) {
    size_t len_a = _avs_coap_option_content_length(a);
    size_t len_b = _avs_coap_option_content_length(b);
    return len_a == len_b
           && memcmp(_avs_coap_option_value(a), _avs_coap_option_value(b),
                     len_a)
                      == 0;
}

static void optit_skip_until(avs_coap_option_iterator_t *optit,
                             bool (*predicate)(uint16_t)) {
    while (!_avs_coap_optit_end(optit)) {
        uint32_t opt_num = _avs_coap_optit_number(optit);
        AVS_ASSERT(opt_num <= UINT16_MAX, "malformed options");
        if (predicate((uint16_t) opt_num)) {
            return;
        }

        _avs_coap_optit_next(optit);
    }
}

static void optit_next_matching(avs_coap_option_iterator_t *optit,
                                bool (*selector)(uint16_t)) {
    _avs_coap_optit_next(optit);
    // NOTE: elective == !critical
    optit_skip_until(optit, selector);
}

bool _avs_coap_selected_options_equal(const avs_coap_options_t *first,
                                      const avs_coap_options_t *second,
                                      bool (*selector)(uint16_t)) {
    avs_coap_option_iterator_t it_first =
            _avs_coap_optit_begin((avs_coap_options_t *) (intptr_t) first);
    avs_coap_option_iterator_t it_second =
            _avs_coap_optit_begin((avs_coap_options_t *) (intptr_t) second);

    optit_skip_until(&it_first, selector);
    optit_skip_until(&it_second, selector);

    while (!_avs_coap_optit_end(&it_first)
           && !_avs_coap_optit_end(&it_second)) {
        uint32_t opt_num_first = _avs_coap_optit_number(&it_first);
        uint32_t opt_num_second = _avs_coap_optit_number(&it_second);

        if (opt_num_first != opt_num_second) {
            LOG(TRACE,
                _("some option only exists in one set (") "%" PRIu32
                                                          "/%" PRIu32 _(")"),
                opt_num_first, opt_num_second);
            return false;
        }

        avs_coap_option_t *opt_first = _avs_coap_optit_current(&it_first);
        avs_coap_option_t *opt_second = _avs_coap_optit_current(&it_second);
        if (!is_option_identical(opt_first, opt_second)) {
            LOG(TRACE, _("different value of option ") "%" PRIu32,
                opt_num_first);
            return false;
        }

        optit_next_matching(&it_first, selector);
        optit_next_matching(&it_second, selector);
    }

    if (!_avs_coap_optit_end(&it_first)) {
        LOG(TRACE, _("excess ") "%" PRIu32 _(" option in `first` set"),
            _avs_coap_optit_number(&it_first));
        return false;
    } else if (!_avs_coap_optit_end(&it_second)) {
        LOG(TRACE, _("excess ") "%" PRIu32 _(" option in `second` set"),
            _avs_coap_optit_number(&it_second));
        return false;
    }

    return true;
}

static bool option_must_not_change_during_transfer(uint16_t opt_num) {
    return (_avs_coap_option_is_critical(opt_num)
            // BLOCK options *do* change during block transfer, even though
            // they are "critical"
            && opt_num != AVS_COAP_OPTION_BLOCK1
            && opt_num != AVS_COAP_OPTION_BLOCK2)
           // Content-Format is not critical, but if it changes, that's a pretty
           // big WTF.
           || opt_num == AVS_COAP_OPTION_CONTENT_FORMAT;
}

#ifdef WITH_AVS_COAP_BLOCK

static size_t get_block_offset(const avs_coap_options_t *opts,
                               avs_coap_option_block_type_t type,
                               size_t seq_num_offset) {
    avs_coap_option_block_t block;
    int result = avs_coap_options_get_block(opts, type, &block);

    if (result == AVS_COAP_OPTION_MISSING) {
        LOG(TRACE, _("BLOCK") "%d" _(" option missing, returning 0"),
            type == AVS_COAP_BLOCK1 ? 1 : 2);
        return 0;
    }

    return (block.seq_num + seq_num_offset) * block.size;
}

static inline size_t next_block1_offset(const avs_coap_options_t *prev) {
    return get_block_offset(prev, AVS_COAP_BLOCK1, 1);
}

static size_t block1_offset(const avs_coap_options_t *prev) {
    return get_block_offset(prev, AVS_COAP_BLOCK1, 0);
}

static size_t next_block2_offset(const avs_coap_options_t *prev) {
    return get_block_offset(prev, AVS_COAP_BLOCK2, 1);
}

static size_t block2_offset(const avs_coap_options_t *prev) {
    return get_block_offset(prev, AVS_COAP_BLOCK2, 0);
}

static bool block1_offset_matches(size_t expected_offset,
                                  const avs_coap_options_t *curr_request) {
    size_t actual_offset = block1_offset(curr_request);
    if (expected_offset != actual_offset) {
        LOG(TRACE, _("expected BLOCK1 offset ") "%u" _(", got ") "%u",
            (unsigned) expected_offset, (unsigned) actual_offset);
        return false;
    }
    return true;
}

static bool block2_offset_matches(const avs_coap_options_t *prev_response,
                                  const avs_coap_options_t *curr_request) {
    size_t expected_offset = next_block2_offset(prev_response);
    size_t actual_offset = block2_offset(curr_request);
    if (expected_offset != actual_offset) {
        LOG(TRACE, _("expected BLOCK2 offset ") "%u" _(", got ") "%u",
            (unsigned) expected_offset, (unsigned) actual_offset);
        return false;
    }
    return true;
}

/**
 * This function checks if expected request payload offset (calculated from
 * previous response to BLOCK request) matches the offset calculated using
 * incoming requests payload sizes.
 *
 * For BERT, expected offset calculated by next_block1_offset() may be smaller
 * than actually expected one, because BERT messages may contain multiple
 * BLOCKs.
 */
static inline bool request_block1_offset_valid(const avs_coap_options_t *prev,
                                               size_t offset) {
    avs_coap_option_block_t block;
    int result = avs_coap_options_get_block(prev, AVS_COAP_BLOCK1, &block);
    if (result == AVS_COAP_OPTION_MISSING) {
        LOG(TRACE, _("BLOCK1 option missing"));
        return (offset == 0);
    }
    size_t expected_offset_if_block = next_block1_offset(prev);
    if (!block.is_bert) {
        return (expected_offset_if_block == offset);
    } else {
        return (expected_offset_if_block <= offset);
    }
}

bool _avs_coap_options_is_sequential_block_request(
        const avs_coap_options_t *prev_response,
        const avs_coap_options_t *prev,
        const avs_coap_options_t *curr,
        size_t expected_request_payload_offset) {
    if (!_avs_coap_selected_options_equal(
                prev, curr, option_must_not_change_during_transfer)) {
        return false;
    }
    /**
     * Current request is said to match previous response in the following cases
     * only:
     *
     *  +-------------------+--------------------+
     *  |   PREV RESPONSE   |    CURR REQUEST    |
     *  +-------------------+--------------------+
     *  | BLOCK1(N-1, *)    |  BLOCK1(N, *)      | <- continuation of BLOCK1
     *  |                   |                    |    request
     *  +-------------------+--------------------+
     *  | BLOCK1(N-1, *)    |  BLOCK1(N, FINAL), | <- last part of BLOCK1
     *  |                   |  BLOCK2(0, *)      |    request, and client
     *  |                   |                    |    expects blockwise resp.
     *  |                   |                    |    (handled in lower layer)
     *  +-------------------+--------------------+
     *  | BLOCK2(N-1, MORE) |  BLOCK2(N, *)      | <- continuation of BLOCK2
     *  |                   |                    |    response
     *  +-------------------+--------------------+
     *  | BLOCK1(N, FINAL), |  BLOCK2(1, *)      | <- we accepted last BLOCK1
     *  | BLOCK2(0, *)      |                    |    request, and initiated
     *  +-------------------+--------------------+    blockwise response
     *
     * NOTE: For simplicity of the illustration, it was assumed that all BLOCKs
     * are of the same size, and thus size was omitted. BLOCK(k, has more)
     * means: it is a k-th (in terms of sequence number) block in exchange.
     */

    const bool prev_response_has_block1 =
            _avs_coap_option_exists(prev_response, AVS_COAP_OPTION_BLOCK1);
    const bool prev_response_has_block2 =
            _avs_coap_option_exists(prev_response, AVS_COAP_OPTION_BLOCK2);
    const bool curr_request_has_block1 =
            _avs_coap_option_exists(curr, AVS_COAP_OPTION_BLOCK1);
    const bool curr_request_has_block2 =
            _avs_coap_option_exists(curr, AVS_COAP_OPTION_BLOCK2);

    /* First case from the table above. */
    if (prev_response_has_block1 && !prev_response_has_block2) {
        /* NOTE: We are omitting second case check, becuase it is already
         * verified at the stage of parsing CoAP options. */
        AVS_ASSERT(request_block1_offset_valid(prev_response,
                                               expected_request_payload_offset),
                   "bug: expected_request_offset invalid");
        return curr_request_has_block1
               && block1_offset_matches(expected_request_payload_offset, curr);
    }

    /* Third and fourth case */
    if (prev_response_has_block2) {
        return !curr_request_has_block1 && curr_request_has_block2
               && block2_offset_matches(prev_response, curr);
    }

    return false;
}

static avs_error_t
validate_block2_in_block1_request(const avs_coap_options_t *opts) {
    /**
     * 2.2.  Structure of a Block Option:
     * [...]
     * > When a Block2 Option is used in a request to retrieve a specific block
     * > number ("control usage"), the M bit MUST be sent as zero and ignored
     * > on reception.
     *
     * Since it is a "MUST", we report Bad Option if the received request
     * contains incorrect BLOCK2 option.
     */
    avs_coap_option_block_t block1;
    memset(&block1, 0, sizeof(block1));

    int result = avs_coap_options_get_block(opts, AVS_COAP_BLOCK1, &block1);
    if (result == AVS_COAP_OPTION_MISSING
            || !_avs_coap_option_exists(opts, AVS_COAP_OPTION_BLOCK2)) {
        return AVS_OK;
    }
    AVS_ASSERT(!result, "BUG: malformed option passed option validation");

    if (block1.has_more) {
        LOG(TRACE, _("BLOCK2 can be used in conjunction with BLOCK1 only in "
                     "final BLOCK1 request exchange"));
        return _avs_coap_err(AVS_COAP_ERR_MALFORMED_OPTIONS);
    }

    return AVS_OK;
}

bool _avs_coap_options_block_payload_valid(const avs_coap_options_t *opts,
                                           uint8_t coap_code,
                                           size_t payload_size) {
    avs_coap_option_block_type_t type;
    if (avs_coap_code_is_request(coap_code)) {
        type = AVS_COAP_BLOCK1;
    } else if (avs_coap_code_is_response(coap_code)) {
        type = AVS_COAP_BLOCK2;
    } else {
        return true;
    }

    avs_coap_option_block_t block;
    int get_block_result = avs_coap_options_get_block(opts, type, &block);
    AVS_ASSERT(get_block_result >= 0,
               "bug: block option should pass validation before");

    if (get_block_result == AVS_COAP_OPTION_MISSING || !block.has_more) {
        return true;
    }
    if (block.is_bert) {
        return payload_size && payload_size % block.size == 0;
    } else {
        return payload_size == block.size;
    }
}
#endif // WITH_AVS_COAP_BLOCK

static bool is_request_key_option(uint16_t opt_num) {
    return option_must_not_change_during_transfer(opt_num)
           || opt_num == AVS_COAP_OPTION_BLOCK1
           || opt_num == AVS_COAP_OPTION_BLOCK2;
}

avs_error_t _avs_coap_options_parse(avs_coap_options_t *out_opts,
                                    bytes_dispenser_t *dispenser,
                                    bool *out_truncated_options,
                                    bool *out_payload_marker_reached) {
    /*
     * Temporarily assume the rest of a packet is options. We will adjust the
     * size accordingly later after validating options.
     *
     * TODO: const_cast
     */
    *out_opts = (avs_coap_options_t) {
        .begin = (void *) (intptr_t) dispenser->read_ptr,
        .size = dispenser->bytes_left,
        .capacity = dispenser->bytes_left
    };

    if (!_avs_coap_options_valid_until_payload_marker(
                out_opts,
                &out_opts->size,
                out_truncated_options,
                out_payload_marker_reached)) {
        return _avs_coap_err(AVS_COAP_ERR_MALFORMED_OPTIONS);
    }

    if (_avs_coap_bytes_extract(dispenser, NULL, out_opts->size)) {
        AVS_UNREACHABLE("parsed options size > bytes available: option "
                        "validation code is incorrect");
    }

    // TODO: maybe capacity == 0 could indicate "read-only" options?
    out_opts->capacity = out_opts->size;

#ifdef WITH_AVS_COAP_BLOCK
    /**
     * NOTE: we are assuming that whatever we parse is a BLOCK1 request (issued
     * by some Client, not by us). The same check could make sense, if the
     * tables were turned -- that is, if we ever were a Client-side that pushes
     * BLOCK1 requests. The thing is, we never are, and that's why we don't do
     * the validation in the other direction anywhere.
     */
    return validate_block2_in_block1_request(out_opts);
#else // WITH_AVS_COAP_BLOCK
    return AVS_OK;
#endif
}

size_t _avs_coap_options_request_key_size(const avs_coap_options_t *opts) {
    avs_coap_option_iterator_t it =
            _avs_coap_optit_begin((avs_coap_options_t *) (intptr_t) opts);

    size_t space_required = 0;
    uint32_t prev_opt_num = 0;

    for (; !_avs_coap_optit_end(&it); _avs_coap_optit_next(&it)) {
        const uint32_t opt_num = _avs_coap_optit_number(&it);
        assert(opt_num <= UINT16_MAX);

        if (is_request_key_option((uint16_t) opt_num)) {
            const avs_coap_option_t *opt = _avs_coap_optit_current(&it);
            const size_t delta = opt_num - prev_opt_num;
            const size_t size = _avs_coap_option_content_length(opt);

            // skipping some options may change header size of others, so
            // we need to recalculate header size
            space_required += _avs_coap_get_opt_header_size(delta, size) + size;
            prev_opt_num = opt_num;
        }
    }

    return space_required;
}

avs_coap_options_t _avs_coap_options_copy_request_key(
        const avs_coap_options_t *opts, void *buffer, size_t buffer_size) {
    AVS_ASSERT(_avs_coap_options_request_key_size(opts) <= buffer_size,
               "buffer too small");

    avs_coap_options_t copy =
            avs_coap_options_create_empty(buffer, buffer_size);
    avs_coap_option_iterator_t it =
            _avs_coap_optit_begin((avs_coap_options_t *) (intptr_t) opts);

    for (; !_avs_coap_optit_end(&it); _avs_coap_optit_next(&it)) {
        const uint32_t opt_num = _avs_coap_optit_number(&it);
        assert(opt_num <= UINT16_MAX);

        if (is_request_key_option((uint16_t) opt_num)) {
            const avs_coap_option_t *opt = _avs_coap_optit_current(&it);
            const void *data = _avs_coap_option_value(opt);
            const uint32_t size = _avs_coap_option_content_length(opt);
            assert(size <= UINT16_MAX);

            if (avs_is_err(avs_coap_options_add_opaque(
                        &copy, (uint16_t) opt_num, data, (uint16_t) size))) {
                AVS_UNREACHABLE();
            }
        }
    }

    return copy;
}
