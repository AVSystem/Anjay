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

#    include <assert.h>
#    include <math.h>
#    include <string.h>

#    include <avsystem/commons/avs_memory.h>
#    include <avsystem/commons/avs_stream.h>
#    include <avsystem/commons/avs_stream_membuf.h>
#    include <avsystem/commons/avs_utils.h>

#    include "../anjay_common.h"
#    include "../anjay_senml_like_encoder_vtable.h"

#    include "anjay_cbor_encoder_ll.h"

VISIBILITY_SOURCE_BEGIN

#    define cbor_log(...) _anjay_log(cbor_encoder, __VA_ARGS__)

/**
 * Most complex scenario in SenML CBOR:
 * ROOT
 * |_ ARRAY
 *    |_ MAP
 *       |_ BYTES
 */
#    define MAX_NEST_STACK_SIZE 4

typedef enum {
    CBOR_CONTEXT_TYPE_ROOT = 0,
    CBOR_CONTEXT_TYPE_ARRAY,
    CBOR_CONTEXT_TYPE_BYTES,
    CBOR_CONTEXT_TYPE_MAP
} cbor_context_type_t;

typedef struct cbor_encoder_level {
    cbor_context_type_t context_type;
    avs_stream_t *stream;
    /* Number of values in current object or number of bytes remaining in bytes
     * value */
    size_t size;
} cbor_encoder_internal_t;

typedef struct cbor_encoder_struct {
    const anjay_senml_like_encoder_vtable_t *vtable;
    cbor_encoder_internal_t nest_stack[MAX_NEST_STACK_SIZE];
    uint8_t stack_size;

    // Used for definite-length map validation.
    uint8_t map_remaining_items;
    double last_encoded_time_s;
} cbor_encoder_t;

static inline cbor_encoder_internal_t *nested_context_top(cbor_encoder_t *ctx) {
    assert(ctx);
    assert(ctx->stack_size);

    return &ctx->nest_stack[ctx->stack_size - 1];
}

static inline cbor_encoder_internal_t *nested_context_push(
        cbor_encoder_t *ctx, avs_stream_t *stream, cbor_context_type_t type) {
    assert(ctx);
    assert(ctx->stack_size < MAX_NEST_STACK_SIZE);

    cbor_encoder_internal_t *new_ctx = &ctx->nest_stack[ctx->stack_size];
    ctx->stack_size++;

    new_ctx->stream = stream;
    new_ctx->context_type = type;
    new_ctx->size = 0;

    return new_ctx;
}

static inline void nested_context_pop(cbor_encoder_t *ctx) {
    (void) ctx;
    assert(ctx->stack_size);
    ctx->stack_size--;
}

static int cbor_encode_uint(cbor_encoder_t *ctx, uint64_t value) {
    cbor_encoder_internal_t *top_ctx = nested_context_top(ctx);
    assert(top_ctx->context_type != CBOR_CONTEXT_TYPE_BYTES);

    top_ctx->size++;
    return _anjay_cbor_ll_encode_uint(top_ctx->stream, value);
}

static int cbor_encode_int(cbor_encoder_t *ctx, int64_t value) {
    cbor_encoder_internal_t *top_ctx = nested_context_top(ctx);
    assert(top_ctx->context_type != CBOR_CONTEXT_TYPE_BYTES);

    top_ctx->size++;
    return _anjay_cbor_ll_encode_int(top_ctx->stream, value);
}

static int cbor_encode_bool(cbor_encoder_t *ctx, bool value) {
    cbor_encoder_internal_t *top_ctx = nested_context_top(ctx);
    assert(top_ctx->context_type != CBOR_CONTEXT_TYPE_BYTES);

    top_ctx->size++;
    return _anjay_cbor_ll_encode_bool(top_ctx->stream, value);
}

static int cbor_encode_double(cbor_encoder_t *ctx, double value) {
    cbor_encoder_internal_t *top_ctx = nested_context_top(ctx);
    assert(top_ctx->context_type != CBOR_CONTEXT_TYPE_BYTES);

    top_ctx->size++;
    return _anjay_cbor_ll_encode_double(top_ctx->stream, value);
}

static int cbor_bytes_begin(cbor_encoder_t *ctx, size_t size) {
    cbor_encoder_internal_t *top_ctx = nested_context_top(ctx);
    assert(top_ctx->context_type != CBOR_CONTEXT_TYPE_BYTES);

    // No caching required, so current stream can be used.
    cbor_encoder_internal_t *bytes_ctx =
            nested_context_push(ctx, top_ctx->stream, CBOR_CONTEXT_TYPE_BYTES);
    bytes_ctx->size = size;

    int retval = _anjay_cbor_ll_bytes_begin(bytes_ctx->stream, size);
    if (retval) {
        nested_context_pop(ctx);
    }
    return retval;
}

static int
cbor_bytes_append(cbor_encoder_t *ctx, const void *data, size_t size) {
    cbor_encoder_internal_t *top_ctx = nested_context_top(ctx);
    assert(top_ctx->context_type == CBOR_CONTEXT_TYPE_BYTES);

    if (size > top_ctx->size) {
        cbor_log(DEBUG, _("passed more bytes than declared"));
        return -1;
    }

    top_ctx->size -= size;
    return _anjay_cbor_ll_bytes_append(top_ctx->stream, data, size);
}

static int cbor_bytes_end(cbor_encoder_t *ctx) {
    assert(ctx);
    cbor_encoder_internal_t *top_ctx = nested_context_top(ctx);
    assert(top_ctx->context_type == CBOR_CONTEXT_TYPE_BYTES);

    int retval = 0;
    if (top_ctx->size) {
        cbor_log(DEBUG, _("not all bytes were written, invalid data encoded"));
        retval = -1;
    }
    nested_context_pop(ctx);

    top_ctx = nested_context_top(ctx);
    top_ctx->size++;
    return retval;
}

static int cbor_encode_string(cbor_encoder_t *ctx, const char *data) {
    cbor_encoder_internal_t *top_ctx = nested_context_top(ctx);
    assert(top_ctx->context_type != CBOR_CONTEXT_TYPE_BYTES);

    top_ctx->size++;
    return _anjay_cbor_ll_encode_string(top_ctx->stream, data);
}

static int cbor_definite_map_begin(cbor_encoder_t *ctx, size_t items_count) {
    assert(ctx);
    cbor_encoder_internal_t *top_ctx = nested_context_top(ctx);
    assert(top_ctx->context_type != CBOR_CONTEXT_TYPE_BYTES);

    // No caching required, so current stream can be used.
    cbor_encoder_internal_t *map_ctx =
            nested_context_push(ctx, top_ctx->stream, CBOR_CONTEXT_TYPE_MAP);

    int retval =
            _anjay_cbor_ll_definite_map_begin(map_ctx->stream, items_count);
    if (retval) {
        nested_context_pop(ctx);
    }
    return retval;
}

static int cbor_definite_map_end(cbor_encoder_t *ctx) {
    assert(ctx);
    cbor_encoder_internal_t *top_ctx = nested_context_top(ctx);
    if (top_ctx->context_type != CBOR_CONTEXT_TYPE_MAP) {
        cbor_log(DEBUG, _("trying to finish map, but it is not started"));
        return -1;
    }

    int retval = 0;
    if (top_ctx->size % 2 != 0) {
        cbor_log(DEBUG,
                 _("invalid map encoded, not all keys have value assigned"));
        retval = -1;
    }

    nested_context_pop(ctx);

    top_ctx = nested_context_top(ctx);
    top_ctx->size++;
    return retval;
}

static int cbor_definite_array_begin(cbor_encoder_t *ctx) {
    cbor_encoder_internal_t *top_ctx = nested_context_top(ctx);
    assert(top_ctx->context_type != CBOR_CONTEXT_TYPE_BYTES);
    (void) top_ctx;

    avs_stream_t *stream = avs_stream_membuf_create();
    if (!stream) {
        return -1;
    }

    nested_context_push(ctx, stream, CBOR_CONTEXT_TYPE_ARRAY);
    return 0;
}

static int copy_stream(avs_stream_t *dst, avs_stream_t *src) {
    char buffer[128];
    bool msg_finished;
    do {
        size_t bytes_read;
        if (avs_is_err(avs_stream_read(src, &bytes_read, &msg_finished, buffer,
                                       sizeof(buffer)))
                || avs_is_err(avs_stream_write(dst, buffer, bytes_read))) {
            return -1;
        }
    } while (!msg_finished);
    return 0;
}

static int cbor_definite_array_end(cbor_encoder_t *ctx) {
    assert(ctx);
    cbor_encoder_internal_t *top_ctx = nested_context_top(ctx);
    if (top_ctx->context_type != CBOR_CONTEXT_TYPE_ARRAY) {
        cbor_log(DEBUG, _("trying to finish array, but it is not started"));
        return -1;
    }
    avs_stream_t *array_stream = top_ctx->stream;
    size_t entries = top_ctx->size;
    nested_context_pop(ctx);

    top_ctx = nested_context_top(ctx);

    int retval;
    (void) ((retval = _anjay_cbor_ll_definite_array_begin(top_ctx->stream,
                                                          entries))
            || (retval = copy_stream(top_ctx->stream, array_stream)));
    avs_stream_cleanup(&array_stream);
    top_ctx->size++;
    return retval;
}

static int senml_cbor_encode_uint(anjay_senml_like_encoder_t *ctx_,
                                  uint64_t value) {
    cbor_encoder_t *ctx = (cbor_encoder_t *) ctx_;
    assert(ctx->map_remaining_items);
    int retval;
    (void) ((retval = cbor_encode_uint(ctx, SENML_LABEL_VALUE))
            || (retval = cbor_encode_uint(ctx, value)));
    ctx->map_remaining_items--;
    return retval;
}

static int senml_cbor_encode_int(anjay_senml_like_encoder_t *ctx_,
                                 int64_t value) {
    cbor_encoder_t *ctx = (cbor_encoder_t *) ctx_;
    assert(ctx->map_remaining_items);
    int retval;
    (void) ((retval = cbor_encode_uint(ctx, SENML_LABEL_VALUE))
            || (retval = cbor_encode_int(ctx, value)));
    ctx->map_remaining_items--;
    return retval;
}

static int senml_cbor_encode_double(anjay_senml_like_encoder_t *ctx_,
                                    double value) {
    cbor_encoder_t *ctx = (cbor_encoder_t *) ctx_;
    assert(ctx->map_remaining_items);
    int retval;
    (void) ((retval = cbor_encode_uint(ctx, SENML_LABEL_VALUE))
            || (retval = cbor_encode_double(ctx, value)));
    ctx->map_remaining_items--;
    return retval;
}

static int senml_cbor_encode_bool(anjay_senml_like_encoder_t *ctx_,
                                  bool value) {
    cbor_encoder_t *ctx = (cbor_encoder_t *) ctx_;
    assert(ctx->map_remaining_items);
    int retval;
    (void) ((retval = cbor_encode_uint(ctx, SENML_LABEL_VALUE_BOOL))
            || (retval = cbor_encode_bool(ctx, value)));
    ctx->map_remaining_items--;
    return retval;
}

static int senml_cbor_encode_string(anjay_senml_like_encoder_t *ctx_,
                                    const char *value) {
    cbor_encoder_t *ctx = (cbor_encoder_t *) ctx_;
    assert(ctx->map_remaining_items);
    int retval;
    (void) ((retval = cbor_encode_uint(ctx, SENML_LABEL_VALUE_STRING))
            || (retval = cbor_encode_string(ctx, value)));
    ctx->map_remaining_items--;
    return retval;
}

static int senml_cbor_encode_objlnk(anjay_senml_like_encoder_t *ctx_,
                                    const char *value) {
    cbor_encoder_t *ctx = (cbor_encoder_t *) ctx_;
    assert(ctx->map_remaining_items);
    int retval;
    (void) ((retval = cbor_encode_string(ctx, SENML_EXT_OBJLNK_REPR))
            || (retval = cbor_encode_string(ctx, value)));
    ctx->map_remaining_items--;
    return retval;
}

static int maybe_encode_basetime(cbor_encoder_t *ctx, double time_s) {
    if (ctx->last_encoded_time_s == time_s) {
        return 0;
    }

    ctx->last_encoded_time_s = time_s;

    assert(ctx->map_remaining_items);
    int retval;
    (void) ((retval = cbor_encode_int(ctx, SENML_LABEL_BASE_TIME))
            || (retval = cbor_encode_double(ctx, time_s)));
    ctx->map_remaining_items--;
    return retval;
}

static inline int maybe_encode_basename(cbor_encoder_t *ctx,
                                        const char *basename) {
    if (basename) {
        assert(ctx->map_remaining_items);
        int retval;
        (void) ((retval = cbor_encode_int(ctx, SENML_LABEL_BASE_NAME))
                || (retval = cbor_encode_string(ctx, basename)));
        ctx->map_remaining_items--;
        return retval;
    }
    return 0;
}

static inline int maybe_encode_name(cbor_encoder_t *ctx, const char *name) {
    if (name) {
        assert(ctx->map_remaining_items);
        int retval;
        (void) ((retval = cbor_encode_int(ctx, SENML_LABEL_NAME))
                || (retval = cbor_encode_string(ctx, name)));
        ctx->map_remaining_items--;
        return retval;
    }
    return 0;
}

static int senml_cbor_element_begin(anjay_senml_like_encoder_t *ctx_,
                                    const char *basename,
                                    const char *name,
                                    double time_s) {
    cbor_encoder_t *ctx = (cbor_encoder_t *) ctx_;
    if (isnan(time_s)) {
        time_s = 0.0;
    }

    ctx->map_remaining_items =
            (uint8_t) (!!basename + !!name
                       + (ctx->last_encoded_time_s != time_s) + 1);
    int retval;
    (void) ((retval = cbor_definite_map_begin(ctx, ctx->map_remaining_items))
            || (retval = maybe_encode_basename(ctx, basename))
            || (retval = maybe_encode_name(ctx, name))
            || (retval = maybe_encode_basetime(ctx, time_s)));
    return retval;
}

static int senml_cbor_element_end(anjay_senml_like_encoder_t *ctx_) {
    cbor_encoder_t *ctx = (cbor_encoder_t *) ctx_;
    assert(ctx->map_remaining_items == 0);
    return cbor_definite_map_end(ctx);
}

static int senml_cbor_bytes_begin(anjay_senml_like_encoder_t *ctx_,
                                  size_t size) {
    cbor_encoder_t *ctx = (cbor_encoder_t *) ctx_;
    int retval;
    (void) ((retval = cbor_encode_uint(ctx, SENML_LABEL_VALUE_OPAQUE))
            || (retval = cbor_bytes_begin(ctx, size)));
    return retval;
}

static int senml_cbor_bytes_append(anjay_senml_like_encoder_t *ctx_,
                                   const void *data,
                                   size_t size) {
    return cbor_bytes_append((cbor_encoder_t *) ctx_, data, size);
}

static int senml_cbor_bytes_end(anjay_senml_like_encoder_t *ctx_) {
    cbor_encoder_t *ctx = (cbor_encoder_t *) ctx_;
    assert(ctx->map_remaining_items);
    ctx->map_remaining_items--;
    return cbor_bytes_end(ctx);
}

static int senml_cbor_encoder_cleanup(anjay_senml_like_encoder_t **ctx_) {
    cbor_encoder_t *ctx = (cbor_encoder_t *) *ctx_;

    int retval = cbor_definite_array_end(ctx);
    if (retval) {
        cbor_log(DEBUG, _("failed to close CBOR array"));
    }
    if (!retval && ctx->stack_size > 1) {
        cbor_log(DEBUG,
                 _("some not closed objects left, serialized data may be "
                   "invalid"));
        retval = -1;
    }

    while (ctx->stack_size > 1) {
        cbor_encoder_internal_t *top_ctx = nested_context_top(ctx);
        if (top_ctx->context_type == CBOR_CONTEXT_TYPE_ARRAY) {
            avs_stream_cleanup(&top_ctx->stream);
        }
        nested_context_pop(ctx);
    }

    avs_free(*ctx_);
    *ctx_ = NULL;
    return retval;
}

static const anjay_senml_like_encoder_vtable_t SENML_CBOR_ENCODER_VTABLE = {
    .senml_like_encode_uint = senml_cbor_encode_uint,
    .senml_like_encode_int = senml_cbor_encode_int,
    .senml_like_encode_double = senml_cbor_encode_double,
    .senml_like_encode_bool = senml_cbor_encode_bool,
    .senml_like_encode_string = senml_cbor_encode_string,
    .senml_like_encode_objlnk = senml_cbor_encode_objlnk,
    .senml_like_element_begin = senml_cbor_element_begin,
    .senml_like_element_end = senml_cbor_element_end,
    .senml_like_bytes_begin = senml_cbor_bytes_begin,
    .senml_like_bytes_append = senml_cbor_bytes_append,
    .senml_like_bytes_end = senml_cbor_bytes_end,
    .senml_like_encoder_cleanup = senml_cbor_encoder_cleanup
};

anjay_senml_like_encoder_t *
_anjay_senml_cbor_encoder_new(avs_stream_t *stream) {
    if (!stream) {
        cbor_log(DEBUG, _("no stream provided"));
        return NULL;
    }

    cbor_encoder_t *ctx =
            (cbor_encoder_t *) avs_calloc(1, sizeof(cbor_encoder_t));
    if (!ctx) {
        cbor_log(DEBUG, _("failed to allocate encoder context"));
        return NULL;
    }

    nested_context_push(ctx, stream, CBOR_CONTEXT_TYPE_ROOT);
    if (cbor_definite_array_begin(ctx)) {
        avs_free(ctx);
        return NULL;
    }
    ctx->vtable = &SENML_CBOR_ENCODER_VTABLE;

    return (anjay_senml_like_encoder_t *) ctx;
}

#    ifdef ANJAY_TEST
#        include "tests/core/io/cbor/cbor_encoder.c"
#    endif

#endif // ANJAY_WITH_CBOR
