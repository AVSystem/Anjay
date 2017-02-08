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

#include "msg.h"

#include "log.h"
#include "msg_internal.h"
#include "../utils.h"

#include <string.h>
#include <stdio.h>
#include <assert.h>

VISIBILITY_SOURCE_BEGIN

const char *_anjay_coap_msg_code_to_string(uint8_t code,
                                           char *buf,
                                           size_t buf_size) {
    static const struct {
        uint8_t code;
        const char *name;
    } CODE_NAMES[] = {
        { ANJAY_COAP_CODE_GET,                        "Get"                        },
        { ANJAY_COAP_CODE_POST,                       "Post"                       },
        { ANJAY_COAP_CODE_PUT,                        "Put"                        },
        { ANJAY_COAP_CODE_DELETE,                     "Delete"                     },

        { ANJAY_COAP_CODE_CREATED,                    "Created"                    },
        { ANJAY_COAP_CODE_DELETED,                    "Deleted"                    },
        { ANJAY_COAP_CODE_VALID,                      "Valid"                      },
        { ANJAY_COAP_CODE_CHANGED,                    "Changed"                    },
        { ANJAY_COAP_CODE_CONTENT,                    "Content"                    },
        { ANJAY_COAP_CODE_CONTINUE,                   "Continue"                   },

        { ANJAY_COAP_CODE_BAD_REQUEST,                "Bad Request"                },
        { ANJAY_COAP_CODE_UNAUTHORIZED,               "Unauthorized"               },
        { ANJAY_COAP_CODE_BAD_OPTION,                 "Bad Option"                 },
        { ANJAY_COAP_CODE_FORBIDDEN,                  "Forbidden"                  },
        { ANJAY_COAP_CODE_NOT_FOUND,                  "Not Found"                  },
        { ANJAY_COAP_CODE_METHOD_NOT_ALLOWED,         "Method Not Allowed"         },
        { ANJAY_COAP_CODE_NOT_ACCEPTABLE,             "Not Acceptable"             },
        { ANJAY_COAP_CODE_REQUEST_ENTITY_INCOMPLETE,  "Request Entity Incomplete"  },
        { ANJAY_COAP_CODE_PRECONDITION_FAILED,        "Precondition Failed"        },
        { ANJAY_COAP_CODE_REQUEST_ENTITY_TOO_LARGE,   "Entity Too Large"           },
        { ANJAY_COAP_CODE_UNSUPPORTED_CONTENT_FORMAT, "Unsupported Content Format" },

        { ANJAY_COAP_CODE_INTERNAL_SERVER_ERROR,      "Internal Server Error"      },
        { ANJAY_COAP_CODE_NOT_IMPLEMENTED,            "Not Implemented"            },
        { ANJAY_COAP_CODE_BAD_GATEWAY,                "Bad Gateway"                },
        { ANJAY_COAP_CODE_SERVICE_UNAVAILABLE,        "Service Unavailable"        },
        { ANJAY_COAP_CODE_GATEWAY_TIMEOUT,            "Gateway Timeout"            },
        { ANJAY_COAP_CODE_PROXYING_NOT_SUPPORTED,     "Proxying Not Supported"     },
    };

    const char *name = "unknown";
    for (size_t i = 0; i < ANJAY_ARRAY_SIZE(CODE_NAMES); ++i) {
        if (CODE_NAMES[i].code == code) {
            name = CODE_NAMES[i].name;
            break;
        }
    }

    if (_anjay_snprintf(buf, buf_size, "%u.%02u %s",
                        _anjay_coap_msg_code_get_class(&code),
                        _anjay_coap_msg_code_get_detail(&code), name) < 0) {
        assert(0 && "buffer too small for CoAP msg code string");
        return "<error>";
    }

    return buf;
}

bool _anjay_coap_msg_is_request(const anjay_coap_msg_t *msg) {
    return _anjay_coap_msg_code_get_class(&msg->header.code) == 0
            && _anjay_coap_msg_code_get_detail(&msg->header.code) > 0;
}

size_t _anjay_coap_msg_get_token(const anjay_coap_msg_t *msg,
                                 anjay_coap_token_t *out_token) {
    size_t token_length = _anjay_coap_msg_header_get_token_length(&msg->header);
    assert(token_length <= ANJAY_COAP_MAX_TOKEN_LENGTH);

    memcpy(out_token, msg->content, token_length);
    return token_length;
}

static const anjay_coap_opt_t *get_first_opt(const anjay_coap_msg_t *msg) {
    size_t token_length = _anjay_coap_msg_header_get_token_length(&msg->header);
    assert(token_length <= ANJAY_COAP_MAX_TOKEN_LENGTH);

    return (const anjay_coap_opt_t *)(msg->content + token_length);
}

static bool is_payload_marker(const anjay_coap_opt_t *ptr) {
    return *(const uint8_t *)ptr == ANJAY_COAP_PAYLOAD_MARKER;
}

anjay_coap_opt_iterator_t _anjay_coap_opt_begin(const anjay_coap_msg_t *msg) {
    anjay_coap_opt_iterator_t optit = {
        .msg = msg,
        .curr_opt = get_first_opt(msg),
        .prev_opt_number = 0
    };

    return optit;
}

anjay_coap_opt_iterator_t *
_anjay_coap_opt_next(anjay_coap_opt_iterator_t *optit) {
    optit->prev_opt_number += _anjay_coap_opt_delta(optit->curr_opt);
    optit->curr_opt += _anjay_coap_opt_sizeof(optit->curr_opt);
    return optit;
}

bool _anjay_coap_opt_end(const anjay_coap_opt_iterator_t *optit) {
    assert((const uint8_t *)optit->curr_opt >= optit->msg->content);

    size_t offset = (size_t)((const uint8_t *)optit->curr_opt
                             - (const uint8_t *)&optit->msg->header);

    assert(offset <= optit->msg->length);
    return offset >= optit->msg->length
           || is_payload_marker(optit->curr_opt);
}

uint32_t _anjay_coap_opt_number(const anjay_coap_opt_iterator_t *optit) {
    return optit->prev_opt_number + _anjay_coap_opt_delta(optit->curr_opt);
}

static const uint8_t *coap_opt_find_end(const anjay_coap_msg_t *msg) {
    anjay_coap_opt_iterator_t optit = _anjay_coap_opt_begin(msg);
    while (!_anjay_coap_opt_end(&optit)) {
        _anjay_coap_opt_next(&optit);
    }
    return (const uint8_t *)optit.curr_opt;
}

size_t _anjay_coap_msg_count_opts(const anjay_coap_msg_t *msg) {
    size_t num_opts = 0;

    for (anjay_coap_opt_iterator_t optit = _anjay_coap_opt_begin(msg);
            !_anjay_coap_opt_end(&optit);
            _anjay_coap_opt_next(&optit)) {
        ++num_opts;
    }

    return num_opts;
}

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

const void *_anjay_coap_msg_payload(const anjay_coap_msg_t *msg) {
    const uint8_t *end = coap_opt_find_end(msg);

    if (end < (const uint8_t*)&msg->header + msg->length
            && *end == ANJAY_COAP_PAYLOAD_MARKER) {
        return end + 1;
    } else {
        return end;
    }
}

size_t _anjay_coap_msg_payload_length(const anjay_coap_msg_t *msg) {
    return (size_t)msg->length - (size_t)
           ((const uint8_t *)_anjay_coap_msg_payload(msg) - (const uint8_t *)&msg->header);
}

static bool is_header_valid(const anjay_coap_msg_t *msg) {
    uint8_t version = _anjay_coap_msg_header_get_version(&msg->header);
    if (version != 1) {
        coap_log(DEBUG, "unsupported CoAP version: %u", version);
        return false;
    }

    uint8_t token_length = _anjay_coap_msg_header_get_token_length(&msg->header);
    if (token_length > ANJAY_COAP_MAX_TOKEN_LENGTH) {
        coap_log(DEBUG, "token too long (%dB, expected 0 <= size <= %d)",
                 token_length, ANJAY_COAP_MAX_TOKEN_LENGTH);
        return false;
    }

    if (sizeof(msg->header) + token_length > msg->length) {
        coap_log(DEBUG, "missing/incomplete token (got %uB, expected %u)",
                 msg->length - (uint32_t)sizeof(msg->header), token_length);
        return false;
    }

    return true;
}

static bool are_options_valid(const anjay_coap_msg_t *msg) {
    size_t length_so_far = sizeof(msg->header)
            + _anjay_coap_msg_header_get_token_length(&msg->header);

    if (length_so_far == msg->length) {
        return true;
    }

    anjay_coap_opt_iterator_t optit = _anjay_coap_opt_begin(msg);
    for (; length_so_far != msg->length && !_anjay_coap_opt_end(&optit);
            _anjay_coap_opt_next(&optit)) {
        if (!_anjay_coap_opt_is_valid(optit.curr_opt,
                                      msg->length - length_so_far)) {
            coap_log(DEBUG, "option validation failed");
            return false;
        }

        length_so_far += _anjay_coap_opt_sizeof(optit.curr_opt);

        if (length_so_far > msg->length) {
            coap_log(DEBUG, "invalid option length (ends %lu bytes after end of message)",
                     (unsigned long)(length_so_far - msg->length));
            return false;
        }

        uint32_t opt_number = _anjay_coap_opt_number(&optit);
        if (opt_number > UINT16_MAX) {
            coap_log(DEBUG, "invalid option number (%u)", opt_number);
            return false;
        }
    }

    if (length_so_far + 1 == msg->length
            && is_payload_marker(optit.curr_opt)) {
        // RFC 7252 3.1: The presence of a Payload Marker followed by a
        // zero-length payload MUST be processed as a message format error.
        coap_log(DEBUG, "validation failed: payload marker at end of message");
        return false;
    }

    return true;
}

bool _anjay_coap_msg_is_valid(const anjay_coap_msg_t *msg) {
    if (msg->length < ANJAY_COAP_MSG_MIN_SIZE) {
        coap_log(DEBUG, "message too short (%uB, expected >= %u)",
                 msg->length, ANJAY_COAP_MSG_MIN_SIZE);
        return false;
    }

    return is_header_valid(msg)
        && are_options_valid(msg)
        // [RFC 7272, 1.2]
        // Empty Message: A message with a Code of 0.00; neither a request nor
        // a response. An Empty message only contains the 4-byte header.
        && (msg->header.code != ANJAY_COAP_CODE_EMPTY
                || msg->length == ANJAY_COAP_MSG_MIN_SIZE);
}

static const char *msg_type_string(anjay_coap_msg_type_t type) {
     static const char *TYPES[] = {
         "CONFIRMABLE",
         "NON_CONFIRMABLE",
         "ACKNOWLEDGEMENT",
         "RESET"
     };
     assert((unsigned)type < ANJAY_ARRAY_SIZE(TYPES));
     return TYPES[type];
}

void _anjay_coap_msg_debug_print(const anjay_coap_msg_t *msg) {
    coap_log(DEBUG, "sizeof(*msg) = %lu, sizeof(len) = %lu, sizeof(header) = %lu",
             (unsigned long)sizeof(*msg), (unsigned long)sizeof(msg->length),
             (unsigned long)sizeof(msg->header));
    coap_log(DEBUG, "message (length = %u):", msg->length);
    coap_log(DEBUG, "type: %u (%s)",
             _anjay_coap_msg_header_get_type(&msg->header),
             msg_type_string(_anjay_coap_msg_header_get_type(&msg->header)));

    coap_log(DEBUG, "  version: %u",
             _anjay_coap_msg_header_get_version(&msg->header));
    coap_log(DEBUG, "  token_length: %u",
             _anjay_coap_msg_header_get_token_length(&msg->header));
    coap_log(DEBUG, "  code: %s", ANJAY_COAP_CODE_STRING(msg->header.code));
    coap_log(DEBUG, "  message_id: %u", _anjay_coap_msg_get_id(msg));
    coap_log(DEBUG, "  content:");

    for (size_t i = 0; i < msg->length - sizeof(msg->header); i += 8) {
        coap_log(DEBUG, "%02x", msg->content[i]);
    }

    coap_log(DEBUG, "opts:");
    for (anjay_coap_opt_iterator_t optit = _anjay_coap_opt_begin(msg);
            !_anjay_coap_opt_end(&optit);
            _anjay_coap_opt_next(&optit)) {
        _anjay_coap_opt_debug_print(optit.curr_opt);
    }
}

static void fill_block_summary(const anjay_coap_msg_t *msg,
                               uint16_t block_opt_num,
                               char *buf,
                               size_t buf_size) {
    assert(block_opt_num == ANJAY_COAP_OPT_BLOCK1
           || block_opt_num == ANJAY_COAP_OPT_BLOCK2);

    const int num = block_opt_num == ANJAY_COAP_OPT_BLOCK1 ? 1 : 2;

    const anjay_coap_opt_t *opt;
    if (_anjay_coap_msg_find_unique_opt(msg, block_opt_num, &opt)) {
        if (opt && _anjay_snprintf(buf, buf_size,
                                   ", multiple BLOCK%d options", num) < 0) {
           assert(0 && "should never happen");
           *buf = '\0';
        }
        return;
    }

    uint32_t seq_num;
    bool has_more;
    uint16_t block_size;

    if (_anjay_coap_opt_block_seq_number(opt, &seq_num)
            || _anjay_coap_opt_block_has_more(opt, &has_more)) {
        if (_anjay_snprintf(buf, buf_size, ", BLOCK%d (bad content)", num) < 0) {
            assert(0 && "should never happen");
            *buf = '\0';
        }
        return;
    }

    if (_anjay_coap_opt_block_size(opt, &block_size)) {
        if (_anjay_snprintf(buf, buf_size, ", BLOCK%d (bad size)", num) < 0) {
            assert(0 && "should never happen");
            *buf = '\0';
        }
        return;
    }

    if (_anjay_snprintf(buf, buf_size, ", BLOCK%d (seq %u, size %u, more %d)",
                        num, seq_num, block_size, (int)has_more) < 0) {
        assert(0 && "should never happen");
        *buf = '\0';
    }
}

const char *_anjay_coap_msg_summary(const anjay_coap_msg_t *msg,
                                    char *buf,
                                    size_t buf_size) {
    assert(_anjay_coap_msg_is_valid(msg));

    anjay_coap_token_t token;
    size_t token_size = _anjay_coap_msg_get_token(msg, &token);
    char token_string[sizeof(token) * 2 + 1] = "";
    for (size_t i = 0; i < token_size; ++i) {
        snprintf(token_string + 2 * i, sizeof(token_string) - 2 * i,
                 "%02x", (uint8_t)token.bytes[i]);
    }

    char block1[64] = "";
    fill_block_summary(msg, ANJAY_COAP_OPT_BLOCK1, block1, sizeof(block1));

    char block2[64] = "";
    fill_block_summary(msg, ANJAY_COAP_OPT_BLOCK2, block2, sizeof(block2));

    if (_anjay_snprintf(
             buf, buf_size, "%s, %s, id %u, token %s (%luB)%s%s",
             ANJAY_COAP_CODE_STRING(msg->header.code),
             msg_type_string(_anjay_coap_msg_header_get_type(&msg->header)),
             _anjay_coap_msg_get_id(msg),
             token_string, (unsigned long)token_size,
             block1, block2) < 0) {
        assert(0 && "should never happen");
        return "(cannot create summary)";
    }
    return buf;
}

#ifdef ANJAY_TEST
#include "test/msg.c"
#endif // ANJAY_TEST
