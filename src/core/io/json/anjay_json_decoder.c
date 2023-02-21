/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#ifdef ANJAY_WITH_SENML_JSON

#    include <ctype.h>
#    include <errno.h>
#    include <string.h>

#    include <avsystem/commons/avs_memory.h>

#    include "../../anjay_utils_private.h"
#    include "../anjay_json_like_decoder_vtable.h"
#    include "anjay_json_decoder.h"

VISIBILITY_SOURCE_BEGIN

#    define LOG(...) _anjay_log(json, __VA_ARGS__)

#    define MAX_NEST_STACK_SIZE 2

typedef enum {
    JSON_NESTED_NONE,
    JSON_NESTED_ARRAY_ELEMENT,
    JSON_NESTED_MAP_KEY,
    JSON_NESTED_MAP_VALUE
} json_nested_type_t;

typedef struct {
    const anjay_json_like_decoder_vtable_t *vtable;
    avs_stream_t *stream;
    anjay_json_like_decoder_state_t state;
    anjay_json_like_value_type_t current_item_type;
    json_nested_type_t nested_types[MAX_NEST_STACK_SIZE];
} anjay_json_decoder_t;

static anjay_json_like_decoder_state_t
json_decoder_state(const anjay_json_like_decoder_t *ctx) {
    return ((const anjay_json_decoder_t *) ctx)->state;
}

static int
json_decoder_current_value_type(anjay_json_like_decoder_t *ctx_,
                                anjay_json_like_value_type_t *out_type) {
    anjay_json_decoder_t *ctx = (anjay_json_decoder_t *) ctx_;
    if (ctx->state == ANJAY_JSON_LIKE_DECODER_STATE_OK) {
        *out_type = ctx->current_item_type;
        return 0;
    }
    return -1;
}

static bool is_json_whitespace(int ch) {
    return strchr(" \r\n\t", ch);
}

static size_t json_decoder_nesting_level(anjay_json_like_decoder_t *ctx_) {
    anjay_json_decoder_t *ctx = (anjay_json_decoder_t *) ctx_;
    if (ctx->state != ANJAY_JSON_LIKE_DECODER_STATE_OK) {
        return 0;
    }
    size_t nesting_level = 0;
    while (nesting_level < AVS_ARRAY_SIZE(ctx->nested_types)
           && ctx->nested_types[nesting_level] != JSON_NESTED_NONE) {
        ++nesting_level;
    }
    return nesting_level;
}

static json_nested_type_t *top_level_nesting_ptr(anjay_json_decoder_t *ctx) {
    size_t nesting_level =
            json_decoder_nesting_level((anjay_json_like_decoder_t *) ctx);
    return nesting_level ? &ctx->nested_types[nesting_level - 1] : NULL;
}

static int preprocess_possible_value(anjay_json_decoder_t *ctx) {
    assert(ctx->state == ANJAY_JSON_LIKE_DECODER_STATE_OK);
    json_nested_type_t *nested_type = top_level_nesting_ptr(ctx);
    while (true) {
        unsigned char value;
        avs_error_t err = avs_stream_peek(ctx->stream, 0, (char *) &value);
        if (avs_is_eof(err)) {
            ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_FINISHED;
            return value;
        } else if (avs_is_err(err)) {
            LOG(DEBUG,
                _("JSON parse error: could not read input stream: ") "%s",
                AVS_COAP_STRERROR(err));
            ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_ERROR;
            return value;
        }

        if (is_json_whitespace(value)) {
            err = avs_stream_getch(ctx->stream, (char *) &value, NULL);
            assert(avs_is_ok(err));
            assert(is_json_whitespace(value));
            (void) err;
            continue;
        }

        if (isdigit(value) || value == '-') {
            ctx->current_item_type = ANJAY_JSON_LIKE_VALUE_DOUBLE;
        } else {
            switch (value) {
            case 'n':
                ctx->current_item_type = ANJAY_JSON_LIKE_VALUE_NULL;
                break;

            case '"':
                ctx->current_item_type = ANJAY_JSON_LIKE_VALUE_TEXT_STRING;
                break;

            case '{':
                ctx->current_item_type = ANJAY_JSON_LIKE_VALUE_MAP;
                break;

            case '[':
                ctx->current_item_type = ANJAY_JSON_LIKE_VALUE_ARRAY;
                break;

            case 't':
            case 'f':
                ctx->current_item_type = ANJAY_JSON_LIKE_VALUE_BOOL;
                break;

            default:
                return value;
            }
        }
        if (nested_type && *nested_type == JSON_NESTED_MAP_KEY
                && ctx->current_item_type
                               != ANJAY_JSON_LIKE_VALUE_TEXT_STRING) {
            LOG(DEBUG, _("JSON parse error: only strings can be map keys"));
            ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_ERROR;
        }
        return 0;
    }
}

static void preprocess_value(anjay_json_decoder_t *ctx) {
    int value = preprocess_possible_value(ctx);
    if (ctx->state == ANJAY_JSON_LIKE_DECODER_STATE_FINISHED) {
        LOG(DEBUG, _("JSON parse error: empty input"));
        ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_ERROR;
    } else if (value > 0) {
        LOG(DEBUG, _("JSON parse error: unexpected character \\x") "%02" PRIX8,
            (uint8_t) value);
        ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_ERROR;
    }
}

static int push_nested_type(anjay_json_decoder_t *ctx,
                            json_nested_type_t nested_type) {
    size_t nesting_level =
            json_decoder_nesting_level((anjay_json_like_decoder_t *) ctx);
    if (nesting_level >= AVS_ARRAY_SIZE(ctx->nested_types)) {
        ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_ERROR;
        return -1;
    }
    ctx->nested_types[nesting_level] = nested_type;
    return 0;
}

static void preprocess_next_value(anjay_json_decoder_t *ctx);

static void preprocess_first_nested_value(anjay_json_decoder_t *ctx) {
    json_nested_type_t *nested_type = top_level_nesting_ptr(ctx);
    assert(nested_type);
    assert(*nested_type == JSON_NESTED_ARRAY_ELEMENT
           || *nested_type == JSON_NESTED_MAP_KEY);
    int value = preprocess_possible_value(ctx);
    if (ctx->state == ANJAY_JSON_LIKE_DECODER_STATE_FINISHED) {
        LOG(DEBUG, _("JSON parse error: unexpected end-of-file"));
        ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_ERROR;
    } else if ((*nested_type == JSON_NESTED_ARRAY_ELEMENT && value == ']')
               || (*nested_type == JSON_NESTED_MAP_KEY && value == '}')) {
        unsigned char ch;
        avs_error_t err = avs_stream_getch(ctx->stream, (char *) &ch, NULL);
        assert(avs_is_ok(err));
        assert(ch == value);
        (void) ch;
        (void) err;
        *nested_type = JSON_NESTED_NONE;
        preprocess_next_value(ctx);
    } else if (value > 0) {
        LOG(DEBUG, _("JSON parse error: unexpected character \\x") "%02" PRIX8,
            (uint8_t) value);
        ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_ERROR;
    }
}

static void preprocess_next_value(anjay_json_decoder_t *ctx) {
    while (true) {
        json_nested_type_t *nested_type = top_level_nesting_ptr(ctx);
        unsigned char ch;
        avs_error_t err;
        do {
            err = avs_stream_getch(ctx->stream, (char *) &ch, NULL);
        } while (avs_is_ok(err) && is_json_whitespace(ch));

        if (avs_is_eof(err) && !nested_type) {
            ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_FINISHED;
            return;
        } else if (avs_is_err(err)) {
            LOG(DEBUG,
                _("JSON parse error: could not read input stream: ") "%s",
                AVS_COAP_STRERROR(err));
            ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_ERROR;
            return;
        } else if (nested_type) {
            if (*nested_type == JSON_NESTED_ARRAY_ELEMENT && ch == ',') {
                preprocess_value(ctx);
                return;
            } else if (*nested_type == JSON_NESTED_MAP_KEY && ch == ':') {
                *nested_type = JSON_NESTED_MAP_VALUE;
                preprocess_value(ctx);
                return;
            } else if (*nested_type == JSON_NESTED_MAP_VALUE && ch == ',') {
                *nested_type = JSON_NESTED_MAP_KEY;
                preprocess_value(ctx);
                return;
            } else if (((*nested_type == JSON_NESTED_ARRAY_ELEMENT && ch == ']')
                        || (*nested_type == JSON_NESTED_MAP_VALUE
                            && ch == '}'))) {
                *nested_type = JSON_NESTED_NONE;
                continue;
            }
        }
        LOG(DEBUG, _("JSON parse error: unexpected character \\x") "%02" PRIX8,
            (uint8_t) ch);
        ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_ERROR;
        return;
    }
}

static int json_decoder_bool(anjay_json_like_decoder_t *ctx_, bool *out_value) {
    anjay_json_decoder_t *ctx = (anjay_json_decoder_t *) ctx_;
    if (ctx->state != ANJAY_JSON_LIKE_DECODER_STATE_OK
            || ctx->current_item_type != ANJAY_JSON_LIKE_VALUE_BOOL) {
        return -1;
    }
    char buf[4];
    if (avs_is_err(avs_stream_read_reliably(ctx->stream, buf, sizeof(buf)))) {
        goto error;
    }
    if (memcmp(buf, "true", 4) == 0) {
        *out_value = true;
    } else if (memcmp(buf, "fals", 4) == 0) {
        char ch;
        if (avs_is_err(avs_stream_getch(ctx->stream, &ch, NULL)) || ch != 'e') {
            goto error;
        }
        *out_value = false;
    } else {
        LOG(DEBUG, _("JSON parse error: invalid boolean value"));
        goto error;
    }
    preprocess_next_value(ctx);
    return 0;
error:
    ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_ERROR;
    return -1;
}

static bool is_valid_json_number_character(int ch) {
    return isdigit(ch) || strchr("+-.Ee", ch);
}

static int validate_number(const char *str) {
    // strtod() is a bit more lenient than the JSON spec
    // make sure we don't accept invalid strings
    if (*str == '-') {
        ++str;
    }
    if (!isdigit(*(const unsigned char *) str)) {
        // leading decimal point is invalid
        return -1;
    }
    if (*str == '0' && isdigit(((const unsigned char *) str)[1])) {
        // leading zero must be followed by
        // decimal point, exponent, or end-of-string - never another digit
        return -1;
    }
    // other cases are handled by strtod()
    return 0;
}

static int json_decoder_number(anjay_json_like_decoder_t *ctx_,
                               anjay_json_like_number_t *out_value) {
    anjay_json_decoder_t *ctx = (anjay_json_decoder_t *) ctx_;
    if (ctx->state != ANJAY_JSON_LIKE_DECODER_STATE_OK
            || ctx->current_item_type != ANJAY_JSON_LIKE_VALUE_DOUBLE) {
        return -1;
    }
    size_t length = 0;
    char buf[ANJAY_MAX_DOUBLE_STRING_SIZE];
    while (true) {
        unsigned char ch;
        avs_error_t err = avs_stream_peek(ctx->stream, 0, (char *) &ch);
        if (avs_is_err(err) && !avs_is_eof(err)) {
            goto error;
        } else if (avs_is_eof(err) || !is_valid_json_number_character(ch)) {
            buf[length] = '\0';
            break;
        }

        if (avs_is_err(avs_stream_read_reliably(ctx->stream, buf + length, 1))
                || ++length >= sizeof(buf)) {
            goto error;
        }
        assert(((const unsigned char *) buf)[length - 1] == ch);
    }
    if (validate_number(buf)
            || _anjay_safe_strtod(buf, &out_value->value.f64)) {
        goto error;
    }
    out_value->type = ANJAY_JSON_LIKE_VALUE_DOUBLE;
    preprocess_next_value(ctx);
    return 0;
error:
    ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_ERROR;
    return -1;
}

static int handle_unicode_escape(anjay_json_decoder_t *ctx,
                                 avs_stream_t *target_stream) {
    char hex[5] = "";
    if (avs_is_err(avs_stream_read_reliably(ctx->stream, &hex, sizeof(hex) - 1))
            || !hex[0] || isspace((unsigned char) hex[0])) {
        return -1;
    }
    errno = 0;
    char *endptr = NULL;
    long codepoint = strtol(hex, &endptr, 16);
    if (errno || !endptr || *endptr || codepoint < 0 || codepoint > 0xFFFF) {
        return -1;
    }
    // UTF-8 multibyte encodings
    if (codepoint < 0x80) {
        return avs_is_ok(avs_stream_write(
                       target_stream,
                       &(unsigned char) { (unsigned char) codepoint }, 1))
                       ? 0
                       : -1;
    } else if (codepoint < 0x800) {
        return avs_is_ok(avs_stream_write(
                       target_stream,
                       &(const unsigned char[]) {
                               (unsigned char) (0xC0 | (codepoint >> 6)),
                               (unsigned char) (0x80 | (codepoint & 0x3F)) }[0],
                       2))
                       ? 0
                       : -1;
    } else {
        return avs_is_ok(avs_stream_write(
                       target_stream,
                       &(const unsigned char[]) {
                               (unsigned char) (0xE0 | (codepoint >> 12)),
                               (unsigned char) (0x80
                                                | ((codepoint >> 6) & 0x3F)),
                               (unsigned char) (0x80 | (codepoint & 0x3F)) }[0],
                       3))
                       ? 0
                       : -1;
    }
}

static int handle_string_escape(anjay_json_decoder_t *ctx,
                                avs_stream_t *target_stream) {
    unsigned char ch;
    if (avs_is_err(avs_stream_getch(ctx->stream, (char *) &ch, NULL))) {
        return -1;
    }
    switch (ch) {
    case '"':
    case '\\':
    case '/':
        break;
    case 'b':
        ch = '\b';
        break;
    case 'f':
        ch = '\f';
        break;
    case 'n':
        ch = '\n';
        break;
    case 'r':
        ch = '\r';
        break;
    case 't':
        ch = '\t';
        break;
    case 'u':
        return handle_unicode_escape(ctx, target_stream);

    default:
        return -1;
    }
    return avs_is_ok(avs_stream_write(target_stream, &ch, 1)) ? 0 : -1;
}

static int json_decoder_bytes(anjay_json_like_decoder_t *ctx_,
                              avs_stream_t *target_stream) {
    anjay_json_decoder_t *ctx = (anjay_json_decoder_t *) ctx_;
    if (ctx->state != ANJAY_JSON_LIKE_DECODER_STATE_OK
            || ctx->current_item_type != ANJAY_JSON_LIKE_VALUE_TEXT_STRING) {
        return -1;
    }
    unsigned char ch;
    avs_error_t err = avs_stream_getch(ctx->stream, (char *) &ch, NULL);
    assert(avs_is_ok(err));
    assert(ch == '"'); // previously checked using peek in preprocess_next_value
    (void) err;
    while (avs_is_ok(avs_stream_getch(ctx->stream, (char *) &ch, NULL))) {
        AVS_STATIC_ASSERT(' ' == 0x20, ascii);
        if (ch == '"') {
            preprocess_next_value(ctx);
            return 0;
        } else if (ch < ' ') {
            // Note: includes EOF and other negative values
            break;
        } else if (ch == '\\') {
            if (handle_string_escape(ctx, target_stream)) {
                break;
            }
        } else if (avs_is_err(avs_stream_write(target_stream,
                                               &(unsigned char) {
                                                       (unsigned char) ch },
                                               1))) {
            // Note: the "ch < ' '" case includes EOF and other negative values
            break;
        }
    }
    ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_ERROR;
    return -1;
}

static int json_decoder_enter_array(anjay_json_like_decoder_t *ctx_) {
    anjay_json_decoder_t *ctx = (anjay_json_decoder_t *) ctx_;
    if (ctx->state != ANJAY_JSON_LIKE_DECODER_STATE_OK
            || ctx->current_item_type != ANJAY_JSON_LIKE_VALUE_ARRAY
            || push_nested_type(ctx, JSON_NESTED_ARRAY_ELEMENT)) {
        return -1;
    }
    unsigned char ch;
    avs_error_t err = avs_stream_getch(ctx->stream, (char *) &ch, NULL);
    assert(avs_is_ok(err));
    assert(ch == '['); // previously checked using peek in preprocess_next_value
    (void) err;
    (void) ch;
    preprocess_first_nested_value(ctx);
    return 0;
}

static int json_decoder_enter_map(anjay_json_like_decoder_t *ctx_) {
    anjay_json_decoder_t *ctx = (anjay_json_decoder_t *) ctx_;
    if (ctx->state != ANJAY_JSON_LIKE_DECODER_STATE_OK
            || ctx->current_item_type != ANJAY_JSON_LIKE_VALUE_MAP
            || push_nested_type(ctx, JSON_NESTED_MAP_KEY)) {
        return -1;
    }
    unsigned char ch;
    avs_error_t err = avs_stream_getch(ctx->stream, (char *) &ch, NULL);
    assert(avs_is_ok(err));
    assert(ch == '{'); // previously checked using peek in preprocess_next_value
    (void) err;
    (void) ch;
    preprocess_first_nested_value(ctx);
    return 0;
}

static void json_decoder_cleanup(anjay_json_like_decoder_t **ctx) {
    if (ctx && *ctx) {
        avs_free(*ctx);
        *ctx = NULL;
    }
}

static const anjay_json_like_decoder_vtable_t VTABLE = {
    .state = json_decoder_state,
    .current_value_type = json_decoder_current_value_type,
    .read_bool = json_decoder_bool,
    .number = json_decoder_number,
    .bytes = json_decoder_bytes,
    .enter_array = json_decoder_enter_array,
    .enter_map = json_decoder_enter_map,
    .nesting_level = json_decoder_nesting_level,
    .cleanup = json_decoder_cleanup
};

anjay_json_like_decoder_t *_anjay_json_decoder_new(avs_stream_t *stream) {
    anjay_json_decoder_t *ctx =
            (anjay_json_decoder_t *) avs_calloc(1, sizeof(*ctx));
    if (ctx) {
        ctx->vtable = &VTABLE;
        ctx->stream = stream;
        ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_OK;
        preprocess_value(ctx);
    }
    return (anjay_json_like_decoder_t *) ctx;
}

#    ifdef ANJAY_TEST
#        include "tests/core/io/json/json_decoder.c"
#    endif

#endif // ANJAY_WITH_SENML_JSON
