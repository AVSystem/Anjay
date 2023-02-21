/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#ifdef ANJAY_WITH_CBOR

#    include <ctype.h>
#    include <errno.h>
#    include <inttypes.h>
#    include <limits.h>
#    include <stdlib.h>
#    include <string.h>

#    include <avsystem/commons/avs_stream.h>
#    include <avsystem/commons/avs_stream_membuf.h>
#    include <avsystem/commons/avs_utils.h>

#    include <anjay/core.h>

#    include "../anjay_utils_private.h"
#    include "../coap/anjay_content_format.h"
#    include "anjay_common.h"
#    include "anjay_json_like_decoder.h"
#    include "anjay_vtable.h"

#    include "cbor/anjay_json_like_cbor_decoder.h"

VISIBILITY_SOURCE_BEGIN

typedef struct {
    const anjay_input_ctx_vtable_t *vtable;
    avs_stream_t *stream;
    anjay_uri_path_t request_uri;
    bool msg_finished;

    anjay_json_like_decoder_t *cbor_decoder;

    bool is_bytes_ctx;
    anjay_io_cbor_bytes_ctx_t bytes_ctx;
} cbor_in_t;

static int cbor_get_some_bytes(anjay_unlocked_input_ctx_t *ctx_,
                               size_t *out_bytes_read,
                               bool *out_msg_finished,
                               void *out_buf,
                               size_t buf_size) {
    cbor_in_t *ctx = (cbor_in_t *) ctx_;
    *out_msg_finished = false;
    *out_bytes_read = 0;

    anjay_json_like_value_type_t value_type;
    if (_anjay_json_like_decoder_current_value_type(ctx->cbor_decoder,
                                                    &value_type)
            || value_type != ANJAY_JSON_LIKE_VALUE_BYTE_STRING) {
        return ANJAY_ERR_BAD_REQUEST;
    }

    if (!ctx->is_bytes_ctx) {
        if (_anjay_io_cbor_get_bytes_ctx(ctx->cbor_decoder, &ctx->bytes_ctx)) {
            return -1;
        }
        ctx->is_bytes_ctx = true;
    }

    if (_anjay_io_cbor_get_some_bytes(ctx->cbor_decoder,
                                      &ctx->bytes_ctx,
                                      out_buf,
                                      buf_size,
                                      out_bytes_read,
                                      out_msg_finished)) {
        return -1;
    }
    ctx->msg_finished = *out_msg_finished;
    if (*out_msg_finished) {
        ctx->is_bytes_ctx = false;
    }
    return 0;
}

static int cbor_get_string(anjay_unlocked_input_ctx_t *ctx_,
                           char *out_buf,
                           size_t buf_size) {
    assert(buf_size);
    cbor_in_t *ctx = (cbor_in_t *) ctx_;

    anjay_json_like_value_type_t value_type;
    if (_anjay_json_like_decoder_current_value_type(ctx->cbor_decoder,
                                                    &value_type)
            || value_type != ANJAY_JSON_LIKE_VALUE_TEXT_STRING) {
        return ANJAY_ERR_BAD_REQUEST;
    }

    if (!ctx->is_bytes_ctx) {
        if (_anjay_io_cbor_get_bytes_ctx(ctx->cbor_decoder, &ctx->bytes_ctx)) {
            return -1;
        }
        ctx->is_bytes_ctx = true;
    }

    size_t bytes_read;
    if (_anjay_io_cbor_get_some_bytes(ctx->cbor_decoder,
                                      &ctx->bytes_ctx,
                                      out_buf,
                                      // make space for null terminator
                                      buf_size - 1,
                                      &bytes_read,
                                      &ctx->msg_finished)) {
        return -1;
    }

    assert(bytes_read < buf_size);
    out_buf[bytes_read] = '\0';
    if (!ctx->msg_finished) {
        return ANJAY_BUFFER_TOO_SHORT;
    }
    ctx->is_bytes_ctx = false;
    return 0;
}

static int cbor_get_integer(anjay_unlocked_input_ctx_t *ctx_, int64_t *value) {
    cbor_in_t *ctx = (cbor_in_t *) ctx_;

    anjay_json_like_value_type_t value_type;
    int result = _anjay_json_like_decoder_current_value_type(ctx->cbor_decoder,
                                                             &value_type);
    if (result
            || (value_type != ANJAY_JSON_LIKE_VALUE_UINT
                && value_type != ANJAY_JSON_LIKE_VALUE_NEGATIVE_INT
                && value_type != ANJAY_JSON_LIKE_VALUE_FLOAT
                && value_type != ANJAY_JSON_LIKE_VALUE_DOUBLE)) {
        return ANJAY_ERR_BAD_REQUEST;
    }

    anjay_json_like_number_t decoded_value;
    int retval =
            _anjay_json_like_decoder_number(ctx->cbor_decoder, &decoded_value);

    if (retval) {
        return retval;
    }

    switch (decoded_value.type) {
    case ANJAY_JSON_LIKE_VALUE_UINT:
        if (decoded_value.value.u64 > INT64_MAX) {
            return -1;
        }
        *value = (int64_t) decoded_value.value.u64;
        break;

    case ANJAY_JSON_LIKE_VALUE_NEGATIVE_INT:
        *value = decoded_value.value.i64;
        break;

    case ANJAY_JSON_LIKE_VALUE_FLOAT:
        if (avs_double_convertible_to_int64(decoded_value.value.f32)) {
            *value = (int64_t) decoded_value.value.f32;
        } else {
            return ANJAY_ERR_BAD_REQUEST;
        }
        break;

    case ANJAY_JSON_LIKE_VALUE_DOUBLE:
        if (avs_double_convertible_to_int64(decoded_value.value.f64)) {
            *value = (int64_t) decoded_value.value.f64;
        } else {
            return ANJAY_ERR_BAD_REQUEST;
        }
        break;

    default:
        AVS_UNREACHABLE("Bug in CBOR decoder");
        return -1;
    }

    ctx->msg_finished = true;
    return 0;
}

static int cbor_get_uint(anjay_unlocked_input_ctx_t *ctx_, uint64_t *value) {
    cbor_in_t *ctx = (cbor_in_t *) ctx_;

    anjay_json_like_value_type_t value_type;
    int result = _anjay_json_like_decoder_current_value_type(ctx->cbor_decoder,
                                                             &value_type);
    if (result
            || (value_type != ANJAY_JSON_LIKE_VALUE_UINT
                && value_type != ANJAY_JSON_LIKE_VALUE_FLOAT
                && value_type != ANJAY_JSON_LIKE_VALUE_DOUBLE)) {
        return ANJAY_ERR_BAD_REQUEST;
    }

    anjay_json_like_number_t decoded_value;
    int retval =
            _anjay_json_like_decoder_number(ctx->cbor_decoder, &decoded_value);
    if (retval) {
        return retval;
    }

    switch (decoded_value.type) {
    case ANJAY_JSON_LIKE_VALUE_UINT:
        *value = decoded_value.value.u64;
        break;

    case ANJAY_JSON_LIKE_VALUE_FLOAT:
        if (avs_double_convertible_to_uint64(decoded_value.value.f32)) {
            *value = (uint64_t) decoded_value.value.f32;
        } else {
            return ANJAY_ERR_BAD_REQUEST;
        }
        break;

    case ANJAY_JSON_LIKE_VALUE_DOUBLE:
        if (avs_double_convertible_to_uint64(decoded_value.value.f64)) {
            *value = (uint64_t) decoded_value.value.f64;
        } else {
            return ANJAY_ERR_BAD_REQUEST;
        }
        break;

    default:
        AVS_UNREACHABLE("Bug in CBOR decoder");
        return -1;
    }

    ctx->msg_finished = true;
    return 0;
}

static int cbor_get_bool(anjay_unlocked_input_ctx_t *ctx_, bool *value) {
    cbor_in_t *ctx = (cbor_in_t *) ctx_;
    anjay_json_like_value_type_t value_type;
    int result = _anjay_json_like_decoder_current_value_type(ctx->cbor_decoder,
                                                             &value_type);
    if (result || value_type != ANJAY_JSON_LIKE_VALUE_BOOL) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    ctx->msg_finished = true;
    return _anjay_json_like_decoder_bool(ctx->cbor_decoder, value);
}

static int cbor_get_double(anjay_unlocked_input_ctx_t *ctx_, double *value) {
    cbor_in_t *ctx = (cbor_in_t *) ctx_;

    anjay_json_like_value_type_t value_type;
    int result = _anjay_json_like_decoder_current_value_type(ctx->cbor_decoder,
                                                             &value_type);
    if (result
            || (value_type != ANJAY_JSON_LIKE_VALUE_FLOAT
                && value_type != ANJAY_JSON_LIKE_VALUE_DOUBLE
                && value_type != ANJAY_JSON_LIKE_VALUE_NEGATIVE_INT
                && value_type != ANJAY_JSON_LIKE_VALUE_UINT)) {
        return ANJAY_ERR_BAD_REQUEST;
    }

    anjay_json_like_number_t decoded_value;
    int retval =
            _anjay_json_like_decoder_number(ctx->cbor_decoder, &decoded_value);
    if (retval) {
        return retval;
    }

    switch (decoded_value.type) {
    case ANJAY_JSON_LIKE_VALUE_FLOAT:
        *value = decoded_value.value.f32;
        break;

    case ANJAY_JSON_LIKE_VALUE_DOUBLE:
        *value = decoded_value.value.f64;
        break;

    case ANJAY_JSON_LIKE_VALUE_UINT:
        *value = (double) decoded_value.value.u64;
        break;

    case ANJAY_JSON_LIKE_VALUE_NEGATIVE_INT:
        *value = (double) decoded_value.value.i64;
        break;

    default:
        AVS_UNREACHABLE("Bug in CBOR decoder");
        return -1;
    }

    ctx->msg_finished = true;
    return 0;
}

static int cbor_get_objlnk(anjay_unlocked_input_ctx_t *ctx_,
                           anjay_oid_t *out_oid,
                           anjay_iid_t *out_iid) {
    char buf[MAX_OBJLNK_STRING_SIZE];

    int retval = cbor_get_string(ctx_, buf, sizeof(buf));
    if (retval) {
        if (retval == ANJAY_BUFFER_TOO_SHORT) {
            retval = ANJAY_ERR_BAD_REQUEST;
        }
        return retval;
    }

    if (_anjay_io_parse_objlnk(buf, out_oid, out_iid)) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    return 0;
}

static int cbor_in_close(anjay_unlocked_input_ctx_t *ctx_) {
    cbor_in_t *ctx = (cbor_in_t *) ctx_;
    _anjay_json_like_decoder_delete(&ctx->cbor_decoder);
    return 0;
}

static int cbor_get_path(anjay_unlocked_input_ctx_t *ctx,
                         anjay_uri_path_t *out_path,
                         bool *out_is_array) {
    cbor_in_t *in = (cbor_in_t *) ctx;
    if (in->msg_finished) {
        return ANJAY_GET_PATH_END;
    }
    if (!_anjay_uri_path_has(&in->request_uri, ANJAY_ID_RID)) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    *out_is_array = false;
    *out_path = in->request_uri;
    return 0;
}

static int cbor_next_entry(anjay_unlocked_input_ctx_t *ctx) {
    (void) ctx;
    return 0;
}

static const anjay_input_ctx_vtable_t CBOR_IN_VTABLE = {
    .some_bytes = cbor_get_some_bytes,
    .string = cbor_get_string,
    .integer = cbor_get_integer,
    .uint = cbor_get_uint,
    .floating = cbor_get_double,
    .boolean = cbor_get_bool,
    .objlnk = cbor_get_objlnk,
    .get_path = cbor_get_path,
    .next_entry = cbor_next_entry,
    .close = cbor_in_close
};

int _anjay_input_cbor_create(anjay_unlocked_input_ctx_t **out,
                             avs_stream_t *stream_ptr,
                             const anjay_uri_path_t *request_uri) {
    cbor_in_t *ctx = (cbor_in_t *) avs_calloc(1, sizeof(cbor_in_t));
    *out = (anjay_unlocked_input_ctx_t *) ctx;
    if (!ctx) {
        return -1;
    }

    ctx->vtable = &CBOR_IN_VTABLE;
    ctx->stream = stream_ptr;
    ctx->request_uri = request_uri ? *request_uri : MAKE_ROOT_PATH();
    ctx->cbor_decoder =
            _anjay_cbor_decoder_new(stream_ptr,
                                    MAX_SIMPLE_CBOR_NEST_STACK_SIZE);

    if (!ctx->cbor_decoder) {
        avs_free(ctx);
        return -1;
    }
    return 0;
}

#    ifdef ANJAY_TEST
#        include "tests/core/io/raw_cbor_in.c"
#    endif // ANJAY_TEST

#endif // ANJAY_WITH_CBOR
