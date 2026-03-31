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

#    include <anjay/core.h>

#    include <assert.h>
#    include <inttypes.h>
#    include <string.h>

#    include <avsystem/commons/avs_stream.h>
#    include <avsystem/commons/avs_utils.h>

#    include "anjay_common.h"
#    include "anjay_vtable.h"

#    include "cbor/anjay_cbor_encoder_ll.h"

#    define lwm2m_cbor_log(level, ...) \
        _anjay_log(lwm2m_cbor_out, level, __VA_ARGS__)

VISIBILITY_SOURCE_BEGIN

typedef struct {
    const anjay_ret_bytes_ctx_vtable_t *vtable;
    size_t remaining_bytes;
} lwm2m_cbor_bytes_t;

typedef struct senml_out_struct {
    anjay_unlocked_output_ctx_t base;
    avs_stream_t *stream;

    anjay_uri_path_t path;
    anjay_uri_path_t previous_path;
    anjay_uri_path_t base_path;
    bool base_path_processed;

    lwm2m_cbor_bytes_t bytes;
    bool returning_bytes;

    uint8_t maps_opened;
} lwm2m_cbor_out_t;

static int encode_base_path(avs_stream_t *stream,
                            const anjay_uri_path_t *path,
                            size_t path_length) {
    assert(path_length);
#    ifdef ANJAY_WITH_LWM2M_GATEWAY
    bool has_prefix = _anjay_uri_path_has_prefix(path);
#    endif // ANJAY_WITH_LWM2M_GATEWAY

    if (path_length == 1) {
#    ifdef ANJAY_WITH_LWM2M_GATEWAY
        if (has_prefix) {
            return _anjay_cbor_ll_encode_string(stream, path->prefix);
        }
#    endif // ANJAY_WITH_LWM2M_GATEWAY
        return _anjay_cbor_ll_encode_uint(stream, path->ids[0]);
    } else {
        if (_anjay_cbor_ll_definite_array_begin(stream, path_length)) {
            return -1;
        }

#    ifdef ANJAY_WITH_LWM2M_GATEWAY
        if (has_prefix) {
            if (_anjay_cbor_ll_encode_string(stream, path->prefix)) {
                return -1;
            }
            path_length--;
        }
#    endif // ANJAY_WITH_LWM2M_GATEWAY

        for (size_t i = 0; i < path_length; i++) {
            if (_anjay_cbor_ll_encode_uint(stream, path->ids[i])) {
                return -1;
            }
        }
        return 0;
    }
}

static int encode_subpath(lwm2m_cbor_out_t *ctx,
                          const anjay_uri_path_t *path,
                          size_t start_index) {
    size_t path_length = _anjay_uri_path_length(path);
    assert(start_index < path_length);

#    ifdef ANJAY_WITH_LWM2M_GATEWAY
    if (start_index == 0 && _anjay_uri_path_has_prefix(path)
            && !_anjay_uri_path_prefix_equal(&ctx->base_path, path)) {
        if (_anjay_cbor_ll_encode_string(ctx->stream, path->prefix)) {
            return -1;
        }
    } else {
#    endif // ANJAY_WITH_LWM2M_GATEWAY
        if (_anjay_cbor_ll_encode_uint(ctx->stream, path->ids[start_index])) {
            return -1;
        }
        start_index++;
#    ifdef ANJAY_WITH_LWM2M_GATEWAY
    }
#    endif // ANJAY_WITH_LWM2M_GATEWAY

    for (; start_index < path_length; start_index++) {
        if (_anjay_cbor_ll_indefinite_map_begin(ctx->stream)
                || _anjay_cbor_ll_encode_uint(ctx->stream,
                                              path->ids[start_index])) {
            return -1;
        }
        ctx->maps_opened++;
    }

    return 0;
}

/**
 * @param a First path
 * @param b Second path
 *
 * @returns Number of consecutive equal IDs counted from the beginning of paths.
 * @c _ANJAY_URI_PATH_MAX_LENGTH if paths are equal, even if they're shorter.
 */
static size_t uri_path_span(const anjay_uri_path_t *a,
                            const anjay_uri_path_t *b) {
    size_t equal_ids = 0;
    for (; equal_ids < _ANJAY_URI_PATH_MAX_LENGTH; equal_ids++) {
        if (a->ids[equal_ids] != b->ids[equal_ids]) {
            break;
        }
    }
    return equal_ids;
}

static int encode_path(lwm2m_cbor_out_t *ctx) {
    if (!ctx->base_path_processed) {
        size_t base_path_length = _anjay_uri_path_length(&ctx->base_path);
#    ifdef ANJAY_WITH_LWM2M_GATEWAY
        base_path_length +=
                (size_t) _anjay_uri_path_has_prefix(&ctx->base_path);
#    endif // ANJAY_WITH_LWM2M_GATEWAY
        ctx->base_path_processed = true;
        ctx->previous_path = ctx->base_path;
        if (base_path_length) {
            // If base path is specified, encode it as a single ID or an array
            // of them.
            if (encode_base_path(
                        ctx->stream, &ctx->base_path, base_path_length)) {
                return -1;
            }

            // If the currently processed path is the same as the base path,
            // return immediately, as the map element is already started.
            // Otherwise, start a map, so the next ID could be written.
            if (_anjay_uri_path_equal(&ctx->base_path, &ctx->path)
#    ifdef ANJAY_WITH_LWM2M_GATEWAY
                    && _anjay_uri_path_prefix_equal(&ctx->base_path, &ctx->path)
#    endif // ANJAY_WITH_LWM2M_GATEWAY
            ) {
                return 0;
            } else {
                if (_anjay_cbor_ll_indefinite_map_begin(ctx->stream)) {
                    return -1;
                }
                ctx->maps_opened++;
            }
        }
    }

    size_t path_span = uri_path_span(&ctx->previous_path, &ctx->path);
    size_t subpath_start_index = 0;
    if (path_span == _ANJAY_URI_PATH_MAX_LENGTH) {
        // Previous path is the same as current path, so we don't need to close
        // the map, but only encode the last ID of the new path.
        assert(_anjay_uri_path_length(&ctx->path) != 0);
        subpath_start_index = _anjay_uri_path_length(&ctx->path) - 1;
    } else {
        // Paths are different. If the previous path without the last ID doesn't
        // match the beginning of the new one, then some of the opened maps must
        // be closed first.
        subpath_start_index = path_span;
        size_t previous_path_length =
                _anjay_uri_path_length(&ctx->previous_path);
        if (path_span < previous_path_length) {
            size_t maps_to_pop = previous_path_length - path_span - 1;
#    ifdef ANJAY_WITH_LWM2M_GATEWAY
            if (!_anjay_uri_path_prefix_equal(&ctx->previous_path,
                                              &ctx->path)) {
                maps_to_pop++;
            }
#    endif // ANJAY_WITH_LWM2M_GATEWAY
            assert(maps_to_pop < ctx->maps_opened);
            while (maps_to_pop--) {
                if (_anjay_cbor_ll_indefinite_map_end(ctx->stream)) {
                    return -1;
                }
                ctx->maps_opened--;
            }
        }
    }

    return encode_subpath(ctx, &ctx->path, subpath_start_index);
}

static int finish_ret_bytes(lwm2m_cbor_out_t *ctx) {
    ctx->returning_bytes = false;
    return ctx->bytes.remaining_bytes ? -1 : 0;
}

static int element_begin(lwm2m_cbor_out_t *ctx) {
    if (!_anjay_uri_path_has(&ctx->path, ANJAY_ID_RID)) {
        return -1;
    }

    if (ctx->returning_bytes) {
        int result = finish_ret_bytes(ctx);
        if (result) {
            return result;
        }
    }

    if (encode_path(ctx)) {
        return -1;
    }

    ctx->previous_path = ctx->path;
    ctx->path = MAKE_ROOT_PATH();
    return 0;
}

static int streamed_bytes_append(anjay_unlocked_ret_bytes_ctx_t *ctx_,
                                 const void *data,
                                 size_t length) {
    lwm2m_cbor_bytes_t *bytes_ctx = (lwm2m_cbor_bytes_t *) ctx_;
    if (length > bytes_ctx->remaining_bytes) {
        return -1;
    }

    lwm2m_cbor_out_t *ctx =
            AVS_CONTAINER_OF(bytes_ctx, lwm2m_cbor_out_t, bytes);
    int retval = _anjay_cbor_ll_bytes_append(ctx->stream, data, length);
    if (!retval) {
        bytes_ctx->remaining_bytes -= length;
    }
    return retval;
}

static const anjay_ret_bytes_ctx_vtable_t STREAMED_BYTES_VTABLE = {
    .append = streamed_bytes_append
};

static int
lwm2m_cbor_ret_bytes(anjay_unlocked_output_ctx_t *ctx_,
                     size_t length,
                     anjay_unlocked_ret_bytes_ctx_t **out_bytes_ctx) {
    lwm2m_cbor_out_t *ctx = (lwm2m_cbor_out_t *) ctx_;

    if (element_begin(ctx) || _anjay_cbor_ll_bytes_begin(ctx->stream, length)) {
        return -1;
    }

    ctx->returning_bytes = true;
    ctx->bytes.remaining_bytes = length;
    *out_bytes_ctx = (anjay_unlocked_ret_bytes_ctx_t *) &ctx->bytes;
    return 0;
}

static int lwm2m_cbor_ret_string(anjay_unlocked_output_ctx_t *ctx_,
                                 const char *value) {
    lwm2m_cbor_out_t *ctx = (lwm2m_cbor_out_t *) ctx_;
    if (element_begin(ctx)
            || _anjay_cbor_ll_encode_string(ctx->stream, value)) {
        return -1;
    }
    return 0;
}

static int lwm2m_cbor_ret_integer(anjay_unlocked_output_ctx_t *ctx_,
                                  int64_t value) {
    lwm2m_cbor_out_t *ctx = (lwm2m_cbor_out_t *) ctx_;
    if (element_begin(ctx) || _anjay_cbor_ll_encode_int(ctx->stream, value)) {
        return -1;
    }
    return 0;
}

static int lwm2m_cbor_ret_uint(anjay_unlocked_output_ctx_t *ctx_,
                               uint64_t value) {
    lwm2m_cbor_out_t *ctx = (lwm2m_cbor_out_t *) ctx_;
    if (element_begin(ctx) || _anjay_cbor_ll_encode_uint(ctx->stream, value)) {
        return -1;
    }
    return 0;
}

static int lwm2m_cbor_ret_double(anjay_unlocked_output_ctx_t *ctx_,
                                 double value) {
    lwm2m_cbor_out_t *ctx = (lwm2m_cbor_out_t *) ctx_;
    if (element_begin(ctx)
            || _anjay_cbor_ll_encode_double(ctx->stream, value)) {
        return -1;
    }
    return 0;
}

static int lwm2m_cbor_ret_bool(anjay_unlocked_output_ctx_t *ctx_, bool value) {
    lwm2m_cbor_out_t *ctx = (lwm2m_cbor_out_t *) ctx_;
    if (element_begin(ctx) || _anjay_cbor_ll_encode_bool(ctx->stream, value)) {
        return -1;
    }
    return 0;
}

static int lwm2m_cbor_ret_objlnk(anjay_unlocked_output_ctx_t *ctx_,
                                 anjay_oid_t oid,
                                 anjay_iid_t iid) {
    char buf[MAX_OBJLNK_STRING_SIZE];
    if (avs_simple_snprintf(buf, sizeof(buf), "%" PRIu16 ":%" PRIu16, oid, iid)
            < 0) {
        return -1;
    }

    return lwm2m_cbor_ret_string(ctx_, buf);
}

static int lwm2m_cbor_ret_start_aggregate(anjay_unlocked_output_ctx_t *ctx_) {
    lwm2m_cbor_out_t *ctx = (lwm2m_cbor_out_t *) ctx_;
    if (_anjay_uri_path_leaf_is(&ctx->path, ANJAY_ID_IID)
            || _anjay_uri_path_leaf_is(&ctx->path, ANJAY_ID_RID)) {
        ctx->path = MAKE_ROOT_PATH();
        return 0;
    } else {
        return -1;
    }
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

static int lwm2m_cbor_set_path(anjay_unlocked_output_ctx_t *ctx_,
                               const anjay_uri_path_t *uri) {
    lwm2m_cbor_out_t *ctx = (lwm2m_cbor_out_t *) ctx_;
    AVS_ASSERT(!uri_path_outside_base(uri, &ctx->base_path),
               "Attempted to set path outside the context's base path. "
               "This is a bug in resource reading logic.");
    if (_anjay_uri_path_length(&ctx->path) > 0
#    ifdef ANJAY_WITH_LWM2M_GATEWAY
            || _anjay_uri_path_has_prefix(&ctx->path)
#    endif // ANJAY_WITH_LWM2M_GATEWAY
    ) {
        lwm2m_cbor_log(ERROR, _("Path already set"));
        return -1;
    }
    ctx->path = *uri;
    return 0;
}

static int lwm2m_cbor_clear_path(anjay_unlocked_output_ctx_t *ctx_) {
    lwm2m_cbor_out_t *ctx = (lwm2m_cbor_out_t *) ctx_;
    if (_anjay_uri_path_length(&ctx->path) == 0
#    ifdef ANJAY_WITH_LWM2M_GATEWAY
            && !_anjay_uri_path_has_prefix(&ctx->path)
#    endif // ANJAY_WITH_LWM2M_GATEWAY
    ) {
        lwm2m_cbor_log(ERROR, _("Path not set"));
        return -1;
    }
    ctx->path = MAKE_ROOT_PATH();
    return 0;
}

static int lwm2m_cbor_output_close(anjay_unlocked_output_ctx_t *ctx_) {
    lwm2m_cbor_out_t *ctx = (lwm2m_cbor_out_t *) ctx_;
    int result = 0;
    if (ctx->returning_bytes) {
        result = finish_ret_bytes(ctx);
    }

    while (ctx->maps_opened--) {
        _anjay_update_ret(&result,
                          _anjay_cbor_ll_indefinite_map_end(ctx->stream));
    }

    if (_anjay_uri_path_length(&ctx->path) > 0
#    ifdef ANJAY_WITH_LWM2M_GATEWAY
            || _anjay_uri_path_has_prefix(&ctx->path)
#    endif // ANJAY_WITH_LWM2M_GATEWAY
    ) {
        _anjay_update_ret(&result, ANJAY_OUTCTXERR_ANJAY_RET_NOT_CALLED);
    }
    return result;
}

static const anjay_output_ctx_vtable_t LWM2M_CBOR_OUT_VTABLE = {
    .bytes_begin = lwm2m_cbor_ret_bytes,
    .string = lwm2m_cbor_ret_string,
    .integer = lwm2m_cbor_ret_integer,
    .uint = lwm2m_cbor_ret_uint,
    .floating = lwm2m_cbor_ret_double,
    .boolean = lwm2m_cbor_ret_bool,
    .objlnk = lwm2m_cbor_ret_objlnk,
    .start_aggregate = lwm2m_cbor_ret_start_aggregate,
    .set_path = lwm2m_cbor_set_path,
    .clear_path = lwm2m_cbor_clear_path,
    .close = lwm2m_cbor_output_close
};

anjay_unlocked_output_ctx_t *
_anjay_output_lwm2m_cbor_create(avs_stream_t *stream,
                                const anjay_uri_path_t *uri) {
    if (_anjay_cbor_ll_indefinite_map_begin(stream)) {
        return NULL;
    }

    lwm2m_cbor_out_t *ctx =
            (lwm2m_cbor_out_t *) avs_calloc(1, sizeof(lwm2m_cbor_out_t));
    if (!ctx) {
        return NULL;
    }

    ctx->maps_opened = 1;
    ctx->stream = stream;
    ctx->base.vtable = &LWM2M_CBOR_OUT_VTABLE;
    ctx->bytes.vtable = &STREAMED_BYTES_VTABLE;
    ctx->path = MAKE_ROOT_PATH();
    ctx->base_path = *uri;

    return (anjay_unlocked_output_ctx_t *) ctx;
}

#    ifdef ANJAY_TEST
#        include "tests/core/io/lwm2m_cbor_out.c"
#    endif // ANJAY_TEST

#endif // defined(ANJAY_WITH_CBOR) && defined(ANJAY_WITH_LWM2M12)
