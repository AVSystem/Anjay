/*
 * Copyright 2017-2021 AVSystem <avsystem@avsystem.com>
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

#ifndef ANJAY_IO_CORE_H
#define ANJAY_IO_CORE_H

#include <avsystem/commons/avs_stream.h>
#include <avsystem/commons/avs_stream_outbuf.h>

#include <anjay/core.h>

#include <anjay_modules/anjay_dm_utils.h>
#include <anjay_modules/anjay_io_utils.h>

#include "coap/anjay_msg_details.h"

#include "anjay_dm_core.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

int _anjay_input_dynamic_construct(anjay_unlocked_input_ctx_t **out,
                                   avs_stream_t *stream,
                                   const anjay_request_t *request);

int _anjay_output_dynamic_construct(anjay_unlocked_output_ctx_t **out_ctx,
                                    avs_stream_t *stream,
                                    const anjay_uri_path_t *uri,
                                    uint16_t format,
                                    anjay_request_action_t action);

#ifdef ANJAY_WITH_LEGACY_CONTENT_FORMAT_SUPPORT
uint16_t _anjay_translate_legacy_content_format(uint16_t format);
#else
#    define _anjay_translate_legacy_content_format(fmt) (fmt)
#endif

#define ANJAY_OUTCTXERR_FORMAT_MISMATCH (-0xCE0)
#define ANJAY_OUTCTXERR_METHOD_NOT_IMPLEMENTED (-0xCE1)
/* returned from _anjay_output_ctx_destroy if no anjay_ret_* function was
 * called, making it impossible to determine actual resource format */
#define ANJAY_OUTCTXERR_ANJAY_RET_NOT_CALLED (-0xCE2)

/** A value returned from @ref anjay_input_ctx_get_path_t to indicate end of the
 * path listing. */
#define ANJAY_GET_PATH_END 1

typedef struct anjay_output_ctx_vtable_struct anjay_output_ctx_vtable_t;

#ifndef ANJAY_WITH_THREAD_SAFETY

#    define anjay_unlocked_dm_list_ctx_struct anjay_dm_list_ctx_struct
#    define anjay_unlocked_dm_resource_list_ctx_struct \
        anjay_dm_resource_list_ctx_struct
#    define anjay_unlocked_output_ctx_struct anjay_output_ctx_struct
#    define anjay_unlocked_ret_bytes_ctx_struct anjay_ret_bytes_ctx_struct
#    define anjay_unlocked_input_ctx_struct anjay_input_ctx_struct
#    define anjay_unlocked_execute_ctx_struct anjay_execute_ctx_struct

#endif // ANJAY_WITH_THREAD_SAFETY

struct anjay_unlocked_output_ctx_struct {
    const anjay_output_ctx_vtable_t *vtable;
    int error;
};

typedef struct anjay_input_ctx_vtable_struct anjay_input_ctx_vtable_t;

struct anjay_unlocked_input_ctx_struct {
    const anjay_input_ctx_vtable_t *vtable;
};

#ifdef ANJAY_WITH_THREAD_SAFETY
struct anjay_ret_bytes_ctx_struct {
    anjay_unlocked_ret_bytes_ctx_t *unlocked_ctx;
};

static inline anjay_unlocked_ret_bytes_ctx_t *
_anjay_ret_bytes_get_unlocked(anjay_ret_bytes_ctx_t *ctx) {
    return ctx->unlocked_ctx;
}

struct anjay_output_ctx_struct {
    anjay_t *anjay_locked;
    anjay_unlocked_output_ctx_t *unlocked_ctx;
    anjay_ret_bytes_ctx_t bytes_ctx;
};

static inline anjay_unlocked_output_ctx_t *
_anjay_output_get_unlocked(anjay_output_ctx_t *ctx) {
    return ctx->unlocked_ctx;
}

struct anjay_input_ctx_struct {
    anjay_t *anjay_locked;
    anjay_unlocked_input_ctx_t *unlocked_ctx;
};

static inline anjay_unlocked_input_ctx_t *
_anjay_input_get_unlocked(anjay_input_ctx_t *ctx) {
    return ctx->unlocked_ctx;
}

struct anjay_execute_ctx_struct {
    anjay_t *anjay_locked;
    anjay_unlocked_execute_ctx_t *unlocked_ctx;
};

static inline anjay_unlocked_execute_ctx_t *
_anjay_execute_get_unlocked(anjay_execute_ctx_t *ctx) {
    return ctx->unlocked_ctx;
}

struct anjay_dm_list_ctx_struct {
    anjay_t *anjay_locked;
    anjay_unlocked_dm_list_ctx_t *unlocked_ctx;
};

static inline anjay_unlocked_dm_list_ctx_t *
_anjay_dm_list_get_unlocked(anjay_dm_list_ctx_t *ctx) {
    return ctx->unlocked_ctx;
}

struct anjay_dm_resource_list_ctx_struct {
    anjay_unlocked_dm_resource_list_ctx_t *unlocked_ctx;
};

static inline anjay_unlocked_dm_resource_list_ctx_t *
_anjay_dm_resource_list_get_unlocked(anjay_dm_resource_list_ctx_t *ctx) {
    return ctx->unlocked_ctx;
}

#else // ANJAY_WITH_THREAD_SAFETY

static inline anjay_unlocked_ret_bytes_ctx_t *
_anjay_ret_bytes_get_unlocked(anjay_ret_bytes_ctx_t *ctx) {
    return ctx;
}

static inline anjay_unlocked_output_ctx_t *
_anjay_output_get_unlocked(anjay_output_ctx_t *ctx) {
    return ctx;
}

static inline anjay_unlocked_input_ctx_t *
_anjay_input_get_unlocked(anjay_input_ctx_t *ctx) {
    return ctx;
}

static inline anjay_unlocked_execute_ctx_t *
_anjay_execute_get_unlocked(anjay_execute_ctx_t *ctx) {
    return ctx;
}

static inline anjay_unlocked_dm_list_ctx_t *
_anjay_dm_list_get_unlocked(anjay_dm_list_ctx_t *ctx) {
    return ctx;
}

static inline anjay_unlocked_dm_resource_list_ctx_t *
_anjay_dm_resource_list_get_unlocked(anjay_dm_resource_list_ctx_t *ctx) {
    return ctx;
}

#endif // ANJAY_WITH_THREAD_SAFETY

anjay_unlocked_output_ctx_t *_anjay_output_opaque_create(avs_stream_t *stream);

#ifndef ANJAY_WITHOUT_PLAINTEXT
anjay_unlocked_output_ctx_t *_anjay_output_text_create(avs_stream_t *stream);
#endif // ANJAY_WITHOUT_PLAINTEXT

#ifndef ANJAY_WITHOUT_TLV
anjay_unlocked_output_ctx_t *
_anjay_output_tlv_create(avs_stream_t *stream, const anjay_uri_path_t *uri);
#endif // ANJAY_WITHOUT_TLV

#if defined(ANJAY_WITH_LWM2M_JSON) || defined(ANJAY_WITH_SENML_JSON) \
        || defined(ANJAY_WITH_CBOR)
anjay_unlocked_output_ctx_t *_anjay_output_senml_like_create(
        avs_stream_t *stream, const anjay_uri_path_t *uri, uint16_t format);
#endif

int _anjay_output_bytes_begin(anjay_unlocked_output_ctx_t *ctx,
                              size_t length,
                              anjay_unlocked_ret_bytes_ctx_t **out_bytes_ctx);

/**
 * Starts an aggregate object, which may be an Object Instance (aggregate of
 * Resources) or Multi-Instance Resource (aggregate of Resource Instances).
 *
 * Normally, this operation is implicit, as it's enough to call
 * @ref _anjay_output_set_path to inform the output context of the appropriate
 * nesting level of the data to serialize. However, there is a need to handle
 * empty aggregates specially - e.g. when a Read was issued on a Multi-Instance
 * Resource that exists, but has zero instances.
 *
 * Currently such empty aggregates are only representable in TLV format, so this
 * method is implemented as a no-op in the SenML context.
 *
 * Note that it wouldn't be enough to specially handle
 * @ref _anjay_output_set_path when not followed by data, because TLV requires
 * different serialization format for Single and Multiple Instance Resources.
 * Call to this function after having called @ref _anjay_output_set_path with a
 * Resource path also doubles as an information for the context that it's
 * dealing with a Multiple Resource.
 */
int _anjay_output_start_aggregate(anjay_unlocked_output_ctx_t *ctx);

int _anjay_output_set_path(anjay_unlocked_output_ctx_t *ctx,
                           const anjay_uri_path_t *path);

int _anjay_output_clear_path(anjay_unlocked_output_ctx_t *ctx);

int _anjay_output_set_time(anjay_unlocked_output_ctx_t *ctx, double value);

/**
 * @returns Code of the FIRST known error encountered on this output context,
 *          in the following precedence order:
 *          1. First known error code of any method call on this context
 *          2. Error code of the destroy operation
 */
int _anjay_output_ctx_destroy(anjay_unlocked_output_ctx_t **ctx_ptr);

int _anjay_output_ctx_destroy_and_process_result(
        anjay_unlocked_output_ctx_t **out_ctx_ptr, int result);

int _anjay_input_next_entry(anjay_unlocked_input_ctx_t *ctx);
int _anjay_input_get_path(anjay_unlocked_input_ctx_t *ctx,
                          anjay_uri_path_t *uri_path,
                          bool *out_is_array);
int _anjay_input_update_root_path(anjay_unlocked_input_ctx_t *ctx,
                                  const anjay_uri_path_t *root_path);

typedef struct anjay_output_buf_ctx {
    anjay_unlocked_output_ctx_t base;
    const void *ret_bytes_vtable;
    avs_stream_t *stream;
} anjay_output_buf_ctx_t;

anjay_output_buf_ctx_t _anjay_output_buf_ctx_init(avs_stream_t *stream);

typedef struct anjay_input_buf_ctx {
    anjay_unlocked_input_ctx_t base;
    avs_stream_t *stream;
    bool msg_finished;
    anjay_uri_path_t path;
} anjay_input_buf_ctx_t;

bool _anjay_is_supported_hierarchical_format(uint16_t content_format);

uint16_t _anjay_default_hierarchical_format(anjay_lwm2m_version_t version);

uint16_t _anjay_default_simple_format(anjay_unlocked_t *anjay,
                                      anjay_lwm2m_version_t version);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_IO_CORE_H */
