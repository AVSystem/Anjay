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
#    include <avsystem/commons/avs_utils.h>

#    include <anjay/core.h>

#    include "anjay_common.h"
#    include "anjay_vtable.h"

#    include "cbor/anjay_cbor_encoder_ll.h"

VISIBILITY_SOURCE_BEGIN

typedef enum { STATE_INITIAL, STATE_PATH_SET, STATE_FINISHED } cbor_out_state_t;

typedef struct {
    const anjay_ret_bytes_ctx_vtable_t *vtable;
    size_t num_bytes_left;
} cbor_ret_bytes_ctx_t;

typedef struct {
    anjay_unlocked_output_ctx_t base;
    cbor_ret_bytes_ctx_t bytes;
    avs_stream_t *stream;
    cbor_out_state_t state;
} cbor_out_t;

static int cbor_bytes_append(anjay_unlocked_ret_bytes_ctx_t *ctx_,
                             const void *data,
                             size_t data_len) {
    cbor_ret_bytes_ctx_t *ctx = (cbor_ret_bytes_ctx_t *) ctx_;
    if (!data_len) {
        return 0;
    }
    if (data_len > ctx->num_bytes_left) {
        return -1;
    }
    cbor_out_t *cbor_ctx = AVS_CONTAINER_OF(ctx, cbor_out_t, bytes);
    int retval = _anjay_cbor_ll_bytes_append(cbor_ctx->stream, data, data_len);

    if (!retval) {
        ctx->num_bytes_left -= data_len;
    }
    if (!ctx->num_bytes_left) {
        cbor_ctx->state = STATE_FINISHED;
    }

    return retval;
}

static const anjay_ret_bytes_ctx_vtable_t CBOR_OUT_BYTES_VTABLE = {
    .append = cbor_bytes_append
};

static int
cbor_ret_bytes_begin(anjay_unlocked_output_ctx_t *ctx_,
                     size_t length,
                     anjay_unlocked_ret_bytes_ctx_t **out_bytes_ctx) {
    cbor_out_t *ctx = (cbor_out_t *) ctx_;
    if (ctx->bytes.num_bytes_left || ctx->state != STATE_PATH_SET) {
        return -1;
    }
    if (_anjay_cbor_ll_bytes_begin(ctx->stream, length)) {
        return -1;
    }

    ctx->bytes.vtable = &CBOR_OUT_BYTES_VTABLE;
    ctx->bytes.num_bytes_left = length;
    if (length == 0) {
        ctx->state = STATE_FINISHED;
    }
    *out_bytes_ctx = (anjay_unlocked_ret_bytes_ctx_t *) &ctx->bytes;
    return 0;
}

static int cbor_ret_string(anjay_unlocked_output_ctx_t *ctx_,
                           const char *value) {
    cbor_out_t *ctx = (cbor_out_t *) ctx_;
    if (ctx->bytes.num_bytes_left) {
        return -1;
    }

    int retval = -1;
    if (ctx->state == STATE_PATH_SET
            && !(retval = _anjay_cbor_ll_encode_string(ctx->stream, value))) {
        ctx->state = STATE_FINISHED;
    }
    return retval;
}

static int cbor_ret_integer(anjay_unlocked_output_ctx_t *ctx_, int64_t value) {
    cbor_out_t *ctx = (cbor_out_t *) ctx_;
    if (ctx->bytes.num_bytes_left) {
        return -1;
    }

    int retval = -1;
    if (ctx->state == STATE_PATH_SET
            && !(retval = _anjay_cbor_ll_encode_int(ctx->stream, value))) {
        ctx->state = STATE_FINISHED;
    }
    return retval;
}

static int cbor_ret_uint(anjay_unlocked_output_ctx_t *ctx_, uint64_t value) {
    cbor_out_t *ctx = (cbor_out_t *) ctx_;
    if (ctx->bytes.num_bytes_left) {
        return -1;
    }

    int retval = -1;
    if (ctx->state == STATE_PATH_SET
            && !(retval = _anjay_cbor_ll_encode_uint(ctx->stream, value))) {
        ctx->state = STATE_FINISHED;
    }
    return retval;
}

static int cbor_ret_double(anjay_unlocked_output_ctx_t *ctx_, double value) {
    cbor_out_t *ctx = (cbor_out_t *) ctx_;
    if (ctx->bytes.num_bytes_left) {
        return -1;
    }

    int retval = -1;
    if (ctx->state == STATE_PATH_SET
            && !(retval = _anjay_cbor_ll_encode_double(ctx->stream, value))) {
        ctx->state = STATE_FINISHED;
    }
    return retval;
}

static int cbor_ret_bool(anjay_unlocked_output_ctx_t *ctx_, bool value) {
    cbor_out_t *ctx = (cbor_out_t *) ctx_;
    if (ctx->bytes.num_bytes_left) {
        return -1;
    }

    int retval = -1;
    if (ctx->state == STATE_PATH_SET
            && !(retval = _anjay_cbor_ll_encode_bool(ctx->stream, value))) {
        ctx->state = STATE_FINISHED;
    }
    return retval;
}

static int cbor_ret_objlnk(anjay_unlocked_output_ctx_t *ctx_,
                           anjay_oid_t oid,
                           anjay_iid_t iid) {
    char objlnk[MAX_OBJLNK_STRING_SIZE];
    int retval = avs_simple_snprintf(
            objlnk, sizeof(objlnk), "%" PRIu16 ":%" PRIu16, oid, iid);
    assert(retval > 0);
    (void) retval;

    return cbor_ret_string(ctx_, objlnk);
}

static int cbor_set_path(anjay_unlocked_output_ctx_t *ctx_,
                         const anjay_uri_path_t *path) {
    cbor_out_t *ctx = (cbor_out_t *) ctx_;
    if (ctx->state == STATE_PATH_SET) {
        return -1;
    } else if (ctx->state != STATE_INITIAL || ctx->bytes.num_bytes_left
               || !_anjay_uri_path_has(path, ANJAY_ID_RID)) {
        return ANJAY_OUTCTXERR_FORMAT_MISMATCH;
    }
    ctx->state = STATE_PATH_SET;
    return 0;
}

static int cbor_clear_path(anjay_unlocked_output_ctx_t *ctx_) {
    cbor_out_t *ctx = (cbor_out_t *) ctx_;
    if (ctx->state != STATE_PATH_SET || ctx->bytes.num_bytes_left) {
        return -1;
    }
    ctx->state = STATE_INITIAL;
    return 0;
}

static int cbor_ret_close(anjay_unlocked_output_ctx_t *ctx_) {
    cbor_out_t *ctx = (cbor_out_t *) ctx_;
    int result = 0;
    if (ctx->state != STATE_FINISHED) {
        result = ANJAY_OUTCTXERR_ANJAY_RET_NOT_CALLED;
    }
    return result;
}

static const anjay_output_ctx_vtable_t CBOR_OUT_VTABLE = {
    .bytes_begin = cbor_ret_bytes_begin,
    .string = cbor_ret_string,
    .integer = cbor_ret_integer,
    .uint = cbor_ret_uint,
    .floating = cbor_ret_double,
    .boolean = cbor_ret_bool,
    .objlnk = cbor_ret_objlnk,
    .set_path = cbor_set_path,
    .clear_path = cbor_clear_path,
    .close = cbor_ret_close
};

anjay_unlocked_output_ctx_t *_anjay_output_cbor_create(avs_stream_t *stream) {
    cbor_out_t *ctx = (cbor_out_t *) avs_calloc(1, sizeof(cbor_out_t));
    if (ctx) {
        ctx->base.vtable = &CBOR_OUT_VTABLE;
        ctx->stream = stream;
    }
    return (anjay_unlocked_output_ctx_t *) ctx;
}

#endif // ANJAY_WITH_CBOR
