/*
 * Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#if defined(ANJAY_WITH_CBOR) && defined(ANJAY_WITH_LWM2M12)

#    include <avsystem/commons/avs_base64.h>
#    include <avsystem/commons/avs_stream_membuf.h>
#    include <avsystem/commons/avs_stream_v_table.h>
#    include <avsystem/commons/avs_utils.h>

#    ifdef ANJAY_WITH_LWM2M_GATEWAY
#        include <anjay/lwm2m_gateway.h>
#    endif // ANJAY_WITH_LWM2M_GATEWAY

#    include "../anjay_utils_private.h"

#    include "cbor/anjay_json_like_cbor_decoder.h"

#    include "anjay_common.h"
#    include "anjay_vtable.h"

#    include <errno.h>
#    include <math.h>

VISIBILITY_SOURCE_BEGIN

#    define LOG(...) _anjay_log(lwm2m_cbor_in, __VA_ARGS__)

#    ifdef ANJAY_WITH_LWM2M_GATEWAY
// 5 relative paths: optional prefix and up to 4 IDs
#        define MAX_RELATIVE_PATHS (1 + _ANJAY_URI_PATH_MAX_LENGTH)
#    else
// 4 relative paths with one ID
#        define MAX_RELATIVE_PATHS _ANJAY_URI_PATH_MAX_LENGTH
#    endif // ANJAY_WITH_LWM2M_GATEWAY

typedef struct {
#    ifdef ANJAY_WITH_LWM2M_GATEWAY
    char prefix[ANJAY_GATEWAY_MAX_PREFIX_LEN];
#    endif // ANJAY_WITH_LWM2M_GATEWAY
    uint16_t ids[_ANJAY_URI_PATH_MAX_LENGTH];
    uint8_t ids_length;
} lwm2m_cbor_relative_path_t;

typedef struct {
    anjay_uri_path_t path;
    uint8_t relative_paths_ids_lengths[MAX_RELATIVE_PATHS];
    uint8_t relative_paths_num;
} path_stack_t;

static void relative_path_init(lwm2m_cbor_relative_path_t *out_path) {
    out_path->ids_length = 0;
#    ifdef ANJAY_WITH_LWM2M_GATEWAY
    out_path->prefix[0] = '\0';
#    endif // ANJAY_WITH_LWM2M_GATEWAY
}

#    ifdef ANJAY_WITH_LWM2M_GATEWAY
static bool relative_path_has_prefix(const lwm2m_cbor_relative_path_t *path) {
    return path->prefix[0] != '\0';
}
#    endif // ANJAY_WITH_LWM2M_GATEWAY

static int path_push(path_stack_t *stack,
                     const lwm2m_cbor_relative_path_t *relative_path) {
    size_t relative_path_length = relative_path->ids_length;
    size_t stored_path_ids_length = _anjay_uri_path_length(&stack->path);

#    ifdef ANJAY_WITH_LWM2M_GATEWAY
    // TODO: rework anjay_uri_path APIs so that it's clear when we care about
    // the whole URI, or just the numerical part
    size_t stored_path_length = stored_path_ids_length;

    if (relative_path_has_prefix(relative_path)) {
        relative_path_length++;
    }
    if (_anjay_uri_path_has_prefix(&stack->path)) {
        stored_path_length++;
    }
#    endif // ANJAY_WITH_LWM2M_GATEWAY

    if (relative_path_length == 0
            || stored_path_ids_length + relative_path->ids_length
                           > _ANJAY_URI_PATH_MAX_LENGTH) {
        return -1;
    }
#    ifdef ANJAY_WITH_LWM2M_GATEWAY
    if (relative_path_has_prefix(relative_path)) {
        if (stored_path_length > 0) {
            LOG(DEBUG, _("prefix can only be the first element of a path"));
            return -1;
        }
        strcpy(stack->path.prefix, relative_path->prefix);
    }
#    endif // ANJAY_WITH_LWM2M_GATEWAY

    assert(stack->relative_paths_num < MAX_RELATIVE_PATHS);

    stack->relative_paths_ids_lengths[stack->relative_paths_num] =
            relative_path->ids_length;
    stack->relative_paths_num++;

    for (uint8_t i = 0; i < relative_path->ids_length; i++) {
        stack->path.ids[stored_path_ids_length++] = relative_path->ids[i];
    }

    return 0;
}

static int path_pop(path_stack_t *stack) {
    if (stack->relative_paths_num == 0) {
        return -1;
    }

    size_t path_ids_length = _anjay_uri_path_length(&stack->path);
    uint8_t last_relative_path_ids_length =
            stack->relative_paths_ids_lengths[stack->relative_paths_num - 1];

#    ifdef ANJAY_WITH_LWM2M_GATEWAY
    assert(stack->relative_paths_num == 1 || last_relative_path_ids_length > 0);

    if (stack->relative_paths_num == 1) {
        stack->path.prefix[0] = '\0';
    }
#    else  // ANJAY_WITH_LWM2M_GATEWAY
    assert(last_relative_path_ids_length > 0);
#    endif // ANJAY_WITH_LWM2M_GATEWAY

    assert(path_ids_length >= last_relative_path_ids_length);

    while (last_relative_path_ids_length--) {
        stack->path.ids[--path_ids_length] = ANJAY_ID_INVALID;
    }

    stack->relative_paths_num--;
    return 0;
}

static int path_pop_n(path_stack_t *stack, size_t n) {
    while (n--) {
        if (path_pop(stack)) {
            return -1;
        }
    }
    return 0;
}

static anjay_uri_path_t path_get(path_stack_t *stack) {
    return stack->path;
}

typedef struct {
    const anjay_input_ctx_vtable_t *input_ctx_vtable;
    anjay_json_like_decoder_t *ctx;

    /* A path which must be a prefix of the currently processed `path`. */
    anjay_uri_path_t base;
    /* Currently processed path. */
    anjay_uri_path_t path;
    /* Keeps info about path nesting. */
    path_stack_t path_stack;

    bool value_read;

    bool decoding_bytes;
    size_t bytes_initial_nesting_level;
    anjay_io_cbor_bytes_ctx_t bytes_ctx;
} lwm2m_cbor_in_t;

static bool can_return_value(lwm2m_cbor_in_t *in) {
    return in->path.ids[0] != ANJAY_ID_INVALID && !in->value_read;
}

// Must be called if the complete value has been read.
static int update_path_stack(lwm2m_cbor_in_t *in,
                             size_t initial_nesting_level) {
    size_t current_nesting_level =
            _anjay_json_like_decoder_nesting_level(in->ctx);

    assert(current_nesting_level <= initial_nesting_level);

    if (current_nesting_level > 0) {
        size_t paths_to_pop = initial_nesting_level - current_nesting_level + 1;
        return path_pop_n(&in->path_stack, paths_to_pop);
    } else if (_anjay_json_like_decoder_state(in->ctx)
               != ANJAY_JSON_LIKE_DECODER_STATE_FINISHED) {
        // If nesting level is 0, no more data is expected and the state should
        // be FINISHED
        return -1;
    }

    return 0;
}

static int get_some_bytes(lwm2m_cbor_in_t *in,
                          size_t *out_bytes_read,
                          bool *out_message_finished,
                          void *out_buf,
                          size_t buf_size) {
    if (!in->decoding_bytes) {
        // Read and cache it here, because the _anjay_io_cbor_get_bytes_ctx()
        // call may enter an indefinite length bytes struct and increase the
        // nesting level.
        in->bytes_initial_nesting_level =
                _anjay_json_like_decoder_nesting_level(in->ctx);
        if (_anjay_io_cbor_get_bytes_ctx(in->ctx, &in->bytes_ctx)) {
            return -1;
        }
        in->decoding_bytes = true;
    }

    if (_anjay_io_cbor_get_some_bytes(in->ctx, &in->bytes_ctx, out_buf,
                                      buf_size, out_bytes_read,
                                      out_message_finished)) {
        return -1;
    }

    if (*out_message_finished) {
        in->decoding_bytes = false;
        in->value_read = true;
        return update_path_stack(in, in->bytes_initial_nesting_level);
    }

    return 0;
}

static int lwm2m_cbor_get_some_bytes(anjay_unlocked_input_ctx_t *ctx,
                                     size_t *out_bytes_read,
                                     bool *out_message_finished,
                                     void *out_buf,
                                     size_t buf_size) {
    lwm2m_cbor_in_t *in = (lwm2m_cbor_in_t *) ctx;
    if (!can_return_value(in)) {
        return -1;
    }

    anjay_json_like_value_type_t type;
    if (_anjay_json_like_decoder_current_value_type(in->ctx, &type)) {
        return -1;
    }

    if (type != ANJAY_JSON_LIKE_VALUE_BYTE_STRING) {
        return ANJAY_ERR_BAD_REQUEST;
    }

    return get_some_bytes(in, out_bytes_read, out_message_finished, out_buf,
                          buf_size);
}

static int lwm2m_cbor_get_string(anjay_unlocked_input_ctx_t *ctx,
                                 char *out_buf,
                                 size_t buf_size) {
    assert(buf_size);
    lwm2m_cbor_in_t *in = (lwm2m_cbor_in_t *) ctx;
    if (!can_return_value(in)) {
        return -1;
    }

    anjay_json_like_value_type_t type;
    if (_anjay_json_like_decoder_current_value_type(in->ctx, &type)) {
        return -1;
    }

    if (type != ANJAY_JSON_LIKE_VALUE_TEXT_STRING) {
        return ANJAY_ERR_BAD_REQUEST;
    }

    size_t bytes_read;
    bool msg_finished;
    if (get_some_bytes(in, &bytes_read, &msg_finished, out_buf, buf_size - 1)) {
        return -1;
    }
    out_buf[bytes_read] = '\0';

    if (!msg_finished) {
        return ANJAY_BUFFER_TOO_SHORT;
    }

    return 0;
}

static int lwm2m_cbor_get_integer(anjay_unlocked_input_ctx_t *ctx,
                                  int64_t *out_value) {
    lwm2m_cbor_in_t *in = (lwm2m_cbor_in_t *) ctx;
    if (!can_return_value(in)) {
        return -1;
    }

    size_t initial_nesting_level =
            _anjay_json_like_decoder_nesting_level(in->ctx);

    anjay_json_like_number_t value;
    if (_anjay_json_like_decoder_number(in->ctx, &value)
            || _anjay_json_like_decoder_get_i64_from_number(&value,
                                                            out_value)) {
        return ANJAY_ERR_BAD_REQUEST;
    }

    in->value_read = true;

    return update_path_stack(in, initial_nesting_level);
}

static int lwm2m_cbor_get_uint(anjay_unlocked_input_ctx_t *ctx,
                               uint64_t *out_value) {
    lwm2m_cbor_in_t *in = (lwm2m_cbor_in_t *) ctx;
    if (!can_return_value(in)) {
        return -1;
    }

    size_t initial_nesting_level =
            _anjay_json_like_decoder_nesting_level(in->ctx);

    anjay_json_like_number_t value;
    if (_anjay_json_like_decoder_number(in->ctx, &value)
            || _anjay_json_like_decoder_get_u64_from_number(&value,
                                                            out_value)) {
        return ANJAY_ERR_BAD_REQUEST;
    }

    in->value_read = true;

    return update_path_stack(in, initial_nesting_level);
}

static int lwm2m_cbor_get_double(anjay_unlocked_input_ctx_t *ctx,
                                 double *out_value) {
    lwm2m_cbor_in_t *in = (lwm2m_cbor_in_t *) ctx;
    if (!can_return_value(in)) {
        return -1;
    }

    size_t initial_nesting_level =
            _anjay_json_like_decoder_nesting_level(in->ctx);

    anjay_json_like_number_t value;
    if (_anjay_json_like_decoder_number(in->ctx, &value)
            || _anjay_json_like_decoder_get_double_from_number(&value,
                                                               out_value)) {
        return ANJAY_ERR_BAD_REQUEST;
    }

    in->value_read = true;

    return update_path_stack(in, initial_nesting_level);
}

static int lwm2m_cbor_get_bool(anjay_unlocked_input_ctx_t *ctx,
                               bool *out_value) {
    lwm2m_cbor_in_t *in = (lwm2m_cbor_in_t *) ctx;

    if (!can_return_value(in)) {
        return -1;
    }

    size_t initial_nesting_level =
            _anjay_json_like_decoder_nesting_level(in->ctx);

    if (_anjay_json_like_decoder_bool(in->ctx, out_value)) {
        return ANJAY_ERR_BAD_REQUEST;
    }

    in->value_read = true;

    return update_path_stack(in, initial_nesting_level);
}

static int lwm2m_cbor_get_objlnk(anjay_unlocked_input_ctx_t *ctx,
                                 anjay_oid_t *out_oid,
                                 anjay_iid_t *out_iid) {
    char objlnk[MAX_OBJLNK_STRING_SIZE];
    if (_anjay_get_string_unlocked(ctx, objlnk, sizeof(objlnk))
            || _anjay_io_parse_objlnk(objlnk, out_oid, out_iid)) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    return 0;
}

static int lwm2m_cbor_get_null(anjay_unlocked_input_ctx_t *ctx) {
    lwm2m_cbor_in_t *in = (lwm2m_cbor_in_t *) ctx;

    if (!can_return_value(in)) {
        return -1;
    }

    size_t initial_nesting_level =
            _anjay_json_like_decoder_nesting_level(in->ctx);

    if (_anjay_json_like_decoder_null(in->ctx)) {
        return -1;
    }

    in->value_read = true;

    return update_path_stack(in, initial_nesting_level);
}

#    ifdef ANJAY_WITH_LWM2M_GATEWAY
static int get_prefix(anjay_json_like_decoder_t *ctx, char *out_prefix) {
    anjay_io_cbor_bytes_ctx_t bytes_ctx;
    if (_anjay_io_cbor_get_bytes_ctx(ctx, &bytes_ctx)) {
        return -1;
    }

    bool message_finished;
    size_t bytes_read;
    if (_anjay_io_cbor_get_some_bytes(ctx, &bytes_ctx, out_prefix,
                                      ANJAY_GATEWAY_MAX_PREFIX_LEN - 1,
                                      &bytes_read, &message_finished)) {
        return -1;
    }
    out_prefix[bytes_read] = '\0';

    return message_finished && bytes_read > 0 ? 0 : -1;
}
#    endif // ANJAY_WITH_LWM2M_GATEWAY

static int get_id(anjay_json_like_decoder_t *ctx, uint16_t *out_id) {
    anjay_json_like_number_t number;
    if (_anjay_json_like_decoder_number(ctx, &number)
            || number.type != ANJAY_JSON_LIKE_VALUE_UINT
            || number.value.u64 >= ANJAY_ID_INVALID) {
        LOG(DEBUG, _("invalid path ID"));
        return -1;
    }

    *out_id = (uint16_t) number.value.u64;
    return 0;
}

static int
array_to_relative_path(anjay_json_like_decoder_t *ctx,
                       lwm2m_cbor_relative_path_t *out_relative_path) {
    size_t initial_nesting_level = _anjay_json_like_decoder_nesting_level(ctx);

    if (_anjay_json_like_decoder_enter_array(ctx)
            || _anjay_json_like_decoder_nesting_level(ctx)
                           == initial_nesting_level) {
        return -1;
    }

#    ifdef ANJAY_WITH_LWM2M_GATEWAY
    anjay_json_like_value_type_t type;
    if (_anjay_json_like_decoder_current_value_type(ctx, &type)) {
        return -1;
    }

    if (type == ANJAY_JSON_LIKE_VALUE_TEXT_STRING
            && get_prefix(ctx, out_relative_path->prefix)) {
        return -1;
    }
#    endif // ANJAY_WITH_LWM2M_GATEWAY

    while (_anjay_json_like_decoder_nesting_level(ctx)
           > initial_nesting_level) {
        if (out_relative_path->ids_length == _ANJAY_URI_PATH_MAX_LENGTH) {
            LOG(DEBUG, _("path too long"));
            return -1;
        }

        if (get_id(ctx,
                   &out_relative_path->ids[out_relative_path->ids_length])) {
            return -1;
        }
        out_relative_path->ids_length++;
    }

    return 0;
}

static int
uint_to_relative_path(anjay_json_like_decoder_t *ctx,
                      lwm2m_cbor_relative_path_t *out_relative_path) {
    if (get_id(ctx, &out_relative_path->ids[0])) {
        return -1;
    }
    out_relative_path->ids_length = 1;
    return 0;
}

#    ifdef ANJAY_WITH_LWM2M_GATEWAY
static int
string_to_relative_path(anjay_json_like_decoder_t *ctx,
                        lwm2m_cbor_relative_path_t *out_relative_path) {
    return get_prefix(ctx, out_relative_path->prefix);
}
#    endif // ANJAY_WITH_LWM2M_GATEWAY

static int decode_path_fragment_and_update_stack(anjay_json_like_decoder_t *ctx,
                                                 path_stack_t *stack) {
    anjay_json_like_value_type_t type;
    if (_anjay_json_like_decoder_current_value_type(ctx, &type)) {
        return -1;
    }

    lwm2m_cbor_relative_path_t relative_path;
    relative_path_init(&relative_path);

    if (type == ANJAY_JSON_LIKE_VALUE_ARRAY) {
        if (array_to_relative_path(ctx, &relative_path)) {
            return -1;
        }
    } else if (type == ANJAY_JSON_LIKE_VALUE_UINT) {
        if (uint_to_relative_path(ctx, &relative_path)) {
            return -1;
        }
#    ifdef ANJAY_WITH_LWM2M_GATEWAY
    } else if (type == ANJAY_JSON_LIKE_VALUE_TEXT_STRING) {
        if (string_to_relative_path(ctx, &relative_path)) {
            return -1;
        }
#    endif // ANJAY_WITH_LWM2M_GATEWAY
    } else {
        LOG(DEBUG, _("unexpected value in key"));
        return -1;
    }

    return path_push(stack, &relative_path);
}

static int decode_and_get_path(lwm2m_cbor_in_t *cbor_ctx,
                               anjay_json_like_decoder_t *ctx,
                               anjay_uri_path_t *out_path) {
    anjay_json_like_value_type_t type;
    do {
        if (decode_path_fragment_and_update_stack(ctx, &cbor_ctx->path_stack)) {
            return -1;
        }

        if (_anjay_json_like_decoder_current_value_type(ctx, &type)
                || type == ANJAY_JSON_LIKE_VALUE_ARRAY) {
            LOG(DEBUG, _("expected map or value"));
            return -1;
        }

        if (type == ANJAY_JSON_LIKE_VALUE_MAP
                && _anjay_json_like_decoder_enter_map(ctx)) {
            return -1;
        }
    } while (type == ANJAY_JSON_LIKE_VALUE_MAP);

    *out_path = path_get(&cbor_ctx->path_stack);
    return 0;
}

static bool uri_path_outside_base(const anjay_uri_path_t *path,
                                  const anjay_uri_path_t *base) {
#    ifdef ANJAY_WITH_LWM2M_GATEWAY
    bool base_set = _anjay_uri_path_length(base) > 0
                    || _anjay_uri_path_has_prefix(base);
    if (base_set && !_anjay_uri_path_prefix_equal(path, base)) {
        return true;
    }
#    endif // ANJAY_WITH_LWM2M_GATEWAY
    return _anjay_uri_path_outside_base(path, base);
}

static int lwm2m_cbor_get_path(anjay_unlocked_input_ctx_t *ctx,
                               anjay_uri_path_t *out_path,
                               bool *out_is_array) {
    lwm2m_cbor_in_t *in = (lwm2m_cbor_in_t *) ctx;

    // Not representable in LwM2M CBOR
    *out_is_array = false;

    if (in->path.ids[0] != ANJAY_ID_INVALID) {
        *out_path = in->path;
        return 0;
    }

    if (_anjay_json_like_decoder_state(in->ctx)
            == ANJAY_JSON_LIKE_DECODER_STATE_FINISHED) {
        return ANJAY_GET_PATH_END;
    }

    if (decode_and_get_path(in, in->ctx, &in->path)) {
        return ANJAY_ERR_BAD_REQUEST;
    }

    if (uri_path_outside_base(&in->path, &in->base)) {
        LOG(LAZY_DEBUG,
            _("parsed path ") "%s" _(" would be outside of uri-path ") "%s",
            ANJAY_DEBUG_MAKE_PATH(&in->path), ANJAY_DEBUG_MAKE_PATH(&in->base));
        return ANJAY_ERR_BAD_REQUEST;
    }

    *out_path = in->path;
    return 0;
}

static int lwm2m_cbor_next_entry(anjay_unlocked_input_ctx_t *ctx) {
    lwm2m_cbor_in_t *in = (lwm2m_cbor_in_t *) ctx;
    if (in->path.ids[0] == ANJAY_ID_INVALID) {
        return 0;
    }
    in->path = MAKE_ROOT_PATH();
    in->value_read = false;
    return 0;
}

static int lwm2m_cbor_close(anjay_unlocked_input_ctx_t *ctx) {
    lwm2m_cbor_in_t *in = (lwm2m_cbor_in_t *) ctx;

    int result = 0;
    if (_anjay_json_like_decoder_state(in->ctx)
            != ANJAY_JSON_LIKE_DECODER_STATE_FINISHED) {
        LOG(WARNING, _("LwM2M CBOR payload contains extraneous data"));
        result = ANJAY_ERR_BAD_REQUEST;
    }

    _anjay_json_like_decoder_delete(&in->ctx);
    return result;
}

static const anjay_input_ctx_vtable_t LWM2M_CBOR_IN_VTABLE = {
    .some_bytes = lwm2m_cbor_get_some_bytes,
    .string = lwm2m_cbor_get_string,
    .integer = lwm2m_cbor_get_integer,
    .uint = lwm2m_cbor_get_uint,
    .floating = lwm2m_cbor_get_double,
    .boolean = lwm2m_cbor_get_bool,
    .objlnk = lwm2m_cbor_get_objlnk,
    .null = lwm2m_cbor_get_null,
    .get_path = lwm2m_cbor_get_path,
    .next_entry = lwm2m_cbor_next_entry,
    .close = lwm2m_cbor_close
};

int _anjay_input_lwm2m_cbor_create(anjay_unlocked_input_ctx_t **out,
                                   avs_stream_t *stream_ptr,
                                   const anjay_uri_path_t *request_uri) {
    int retval = -1;
    lwm2m_cbor_in_t *in = NULL;
    anjay_json_like_decoder_t *ctx =
            _anjay_cbor_decoder_new(stream_ptr, MAX_LWM2M_CBOR_NEST_STACK_SIZE);
    if (!ctx) {
        return -1;
    }

    if (_anjay_json_like_decoder_enter_map(ctx)) {
        retval = ANJAY_ERR_BAD_REQUEST;
        goto error;
    }
    in = (lwm2m_cbor_in_t *) avs_calloc(1, sizeof(lwm2m_cbor_in_t));
    if (!in) {
        goto error;
    }
    in->input_ctx_vtable = &LWM2M_CBOR_IN_VTABLE;
    in->ctx = ctx;
    in->base = *request_uri;
    in->path = MAKE_ROOT_PATH();
    in->path_stack.path = MAKE_ROOT_PATH();
    *out = (anjay_unlocked_input_ctx_t *) in;
    return 0;

error:
    _anjay_json_like_decoder_delete(&ctx);
    avs_free(in);
    return retval;
}

#    ifdef ANJAY_TEST
#        include "tests/core/io/lwm2m_cbor_in.c"
#    endif // ANJAY_TEST

#endif // defined(ANJAY_WITH_CBOR) || defined(ANJAY_WITH_LWM2M12)
