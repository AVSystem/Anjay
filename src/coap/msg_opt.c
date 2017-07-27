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

#include <config.h>

#include "content_format.h"
#include "log.h"
#include "msg_info.h"
#include "msg_opt.h"

VISIBILITY_SOURCE_BEGIN

int _anjay_coap_msg_find_unique_opt(const anjay_coap_msg_t *msg,
                                    uint16_t opt_number,
                                    const anjay_coap_opt_t **out_opt) {
    *out_opt = NULL;

    for (anjay_coap_opt_iterator_t it = _anjay_coap_opt_begin(msg);
            !_anjay_coap_opt_end(&it);
            _anjay_coap_opt_next(&it)) {
        uint32_t curr_opt_number = _anjay_coap_opt_number(&it);

        if (curr_opt_number == opt_number) {
            if (*out_opt) {
                // multiple options with such opt_number
                return -1;
            }

            *out_opt = it.curr_opt;
        } else if (curr_opt_number > opt_number) {
            break;
        }
    }

    return *out_opt ? 0 : -1;
}

int _anjay_coap_msg_get_option_uint(const anjay_coap_msg_t *msg,
                                    uint16_t option_number,
                                    void *out_fmt,
                                    size_t out_fmt_size) {
    const anjay_coap_opt_t *opt;
    if (_anjay_coap_msg_find_unique_opt(msg, option_number, &opt)) {
        if (opt) {
            coap_log(DEBUG, "multiple instances of option %d found",
                     option_number);
            return -1;
        } else {
            coap_log(TRACE, "option %d not found", option_number);
            return ANJAY_COAP_OPTION_MISSING;
        }
    }

    return _anjay_coap_opt_uint_value(opt, out_fmt, out_fmt_size);
}

int _anjay_coap_msg_get_option_string_it(const anjay_coap_msg_t *msg,
                                         uint16_t option_number,
                                         anjay_coap_opt_iterator_t *it,
                                         size_t *out_bytes_read,
                                         char *buffer,
                                         size_t buffer_size) {
    if (!it->msg) {
        anjay_coap_opt_iterator_t begin = _anjay_coap_opt_begin(msg);
        memcpy(it, &begin, sizeof(*it));
    } else {
        assert(it->msg == msg);
        _anjay_coap_opt_next(it);
    }

    for (; !_anjay_coap_opt_end(it); _anjay_coap_opt_next(it)) {
        if (_anjay_coap_opt_number(it) == option_number) {
            return _anjay_coap_opt_string_value(it->curr_opt, out_bytes_read,
                                                buffer, buffer_size);
        }
    }

    return 1;
}

int _anjay_coap_msg_get_content_format(const anjay_coap_msg_t *msg,
                                       uint16_t *out_value) {
    int result = _anjay_coap_msg_get_option_u16(
            msg, ANJAY_COAP_OPT_CONTENT_FORMAT, out_value);

    if (result == ANJAY_COAP_OPTION_MISSING) {
        *out_value = ANJAY_COAP_FORMAT_NONE;
        return 0;
    }

    return result;
}

static bool is_opt_critical(uint32_t opt_number) {
    return opt_number % 2;
}

static bool is_critical_opt_valid(uint8_t msg_code, uint32_t opt_number,
            anjay_coap_critical_option_validator_t fallback_validator) {
    switch (opt_number) {
    case ANJAY_COAP_OPT_BLOCK1:
        return msg_code == ANJAY_COAP_CODE_PUT
            || msg_code == ANJAY_COAP_CODE_POST;
    case ANJAY_COAP_OPT_BLOCK2:
        return msg_code == ANJAY_COAP_CODE_GET
            || msg_code == ANJAY_COAP_CODE_PUT
            || msg_code == ANJAY_COAP_CODE_POST;
    default:
        return fallback_validator(msg_code, opt_number);
    }
}

int _anjay_coap_msg_validate_critical_options(
        const anjay_coap_msg_t *msg,
        anjay_coap_critical_option_validator_t validator) {
    int result = 0;
    for (anjay_coap_opt_iterator_t it = _anjay_coap_opt_begin(msg);
            !_anjay_coap_opt_end(&it);
            _anjay_coap_opt_next(&it)) {
        if (is_opt_critical(_anjay_coap_opt_number(&it))) {
            uint32_t opt_number = _anjay_coap_opt_number(&it);

            if (!is_critical_opt_valid(it.msg->header.code, opt_number,
                                       validator)) {
                coap_log(DEBUG,
                         "warning: invalid critical option in query %s: %u",
                         ANJAY_COAP_CODE_STRING(it.msg->header.code),
                         opt_number);
                result = -1;
            }
        }
    }

    return result;
}
