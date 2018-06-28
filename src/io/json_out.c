/*
 * Copyright 2017-2018 AVSystem <avsystem@avsystem.com>
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

#include <anjay_config.h>

#include <assert.h>
#include <string.h>
#include <inttypes.h>


#include <avsystem/commons/list.h>
#include <avsystem/commons/stream.h>
#include <avsystem/commons/utils.h>

#include "../coap/content_format.h"

#include "../io_core.h"
#include "base64_out.h"
#include "vtable.h"

#define json_log(level, ...) _anjay_log(json, level, __VA_ARGS__)

VISIBILITY_SOURCE_BEGIN

typedef struct {
    anjay_id_type_t type;
    int32_t id;
} json_id_t;

typedef enum {
    JSON_DATA_UNKNOWN,
    JSON_DATA_F32,
    JSON_DATA_F64,
    JSON_DATA_I32,
    JSON_DATA_I64,
    JSON_DATA_BOOL,
    JSON_DATA_OBJLNK,
    JSON_DATA_STRING
} json_data_type_t;

static const char *data_type_to_string(json_data_type_t type) {
    switch(type) {
    case JSON_DATA_F32:
    case JSON_DATA_F64:
    case JSON_DATA_I32:
    case JSON_DATA_I64:
        return "v";
    case JSON_DATA_BOOL:
        return "bv";
    case JSON_DATA_OBJLNK:
        return "ov";
    default:
        return "sv";
    }
}

typedef struct {
    json_id_t id;
    json_data_type_t type;
    const void *value;
} json_entry_t;

typedef enum {
    EXPECT_INDEX,
    EXPECT_VALUE
} json_expected_write_t;

typedef struct {
    json_expected_write_t expected_write;
    json_data_type_t value_type;
    anjay_riid_t riid;
} json_out_array_t;

typedef struct json_out_struct {
    const anjay_output_ctx_vtable_t *vtable;
    avs_stream_abstract_t *stream;
    int *errno_ptr;

    /* Path to the currently processed child of the base request node.
       E.g. if the request was made on /X, and we are processing the
       /X/Y/Z/W, then the list contains X, Y, Z, W. */
    json_id_t path[4];
    size_t num_path_elems;
    /* Number of elements in the node_path which form a basename */
    size_t num_base_path_elems;

    bool needs_separator;
    json_out_array_t array_ctx;
    bool returning_array;
    anjay_ret_bytes_ctx_t *bytes;
    json_id_t next_id;
} json_out_t;

static json_id_t *last_path_elem(json_out_t *ctx) {
    if (!ctx->num_path_elems) {
        return NULL;
    }
    return &ctx->path[ctx->num_path_elems - 1];
}

static void
push_path_elem(json_out_t *ctx, anjay_id_type_t type, uint16_t id) {
    const size_t max_num_elems = sizeof(ctx->path) / sizeof(*ctx->path);

    if (ctx->num_path_elems >= max_num_elems) {
        json_log(ERROR, "BUG: cannot append basename elements");
        return;
    }
    ctx->path[ctx->num_path_elems++] = (json_id_t) {
        .type = type,
        .id = id
    };
}

static void
update_node_path(json_out_t *ctx, anjay_id_type_t type, uint16_t id) {
    AVS_STATIC_ASSERT(ANJAY_ID_OID < ANJAY_ID_IID, bad_ordering_oid_iid);
    AVS_STATIC_ASSERT(ANJAY_ID_IID < ANJAY_ID_RID, bad_ordering_iid_rid);
    AVS_STATIC_ASSERT(ANJAY_ID_RID < ANJAY_ID_RIID, bad_ordering_rid_riid);
    json_id_t *last = last_path_elem(ctx);

    /**
     * The idea behind this is: children must be pinned under their parents
     * in the LwM2M data-model tree hierarchy.
     *
     * Example:
     *  - Say ctx->path = [ (OID, 1), (IID, 2), (RID, 3) ]
     *
     *  - We just got (IID, 9) which is higher in the hierarchy than (RID, *)
     *
     *  - We therefore throw away everything till we reach node, which could
     *    be the parent node in terms of types (i.e. OID is a parent of IID,
     *    etc.)
     *
     *  - In the end, we append new node and ctx->path = [ (OID, 1), (IID, 9) ]
     */
    while (last && type <= last->type) {
        --ctx->num_path_elems;
        last = last_path_elem(ctx);
    }
    push_path_elem(ctx, type, id);

    if (ctx->num_base_path_elems > ctx->num_path_elems) {
        AVS_UNREACHABLE("Should never happen");
        /* But we need to be prepared for that on production. */
        ctx->num_base_path_elems = ctx->num_path_elems;
        json_log(ERROR, "num_path_elems < num_base_path_elems!");
    }
}

#define MAX_CHILD_PATH_LEN sizeof("/65535/65535/65535")

static size_t
count_child_path_elems(json_out_t *ctx) {
    return ctx->num_path_elems - ctx->num_base_path_elems;
}

static ssize_t child_path_to_string(json_out_t *ctx, char *dest, size_t size) {
    switch (count_child_path_elems(ctx)) {
    case 1:
        return avs_simple_snprintf(dest, size, "/%"PRId32,
                                   ctx->path[ctx->num_base_path_elems].id);
    case 2:
        return avs_simple_snprintf(dest, size, "/%"PRId32"/%"PRId32,
                                   ctx->path[ctx->num_base_path_elems].id,
                                   ctx->path[ctx->num_base_path_elems + 1].id);
    case 3:
        return avs_simple_snprintf(dest, size, "/%"PRId32"/%"PRId32"/%"PRId32,
                                   ctx->path[ctx->num_base_path_elems].id,
                                   ctx->path[ctx->num_base_path_elems + 1].id,
                                   ctx->path[ctx->num_base_path_elems + 2].id);
    default:
        return -1;
    }
}

typedef struct {
    anjay_oid_t oid;
    anjay_iid_t iid;
} packed_objlnk_t;

static int write_quoted_string(avs_stream_abstract_t *stream,
                               const char *value) {
    int retval = avs_stream_write(stream, "\"", 1);
    if (retval) {
        return retval;
    }
    for (size_t i = 0; i < strlen(value); ++i) {
        /**
         * RFC 4627 section 2.5 Strings:
         *
         * "(...)
         *  All Unicode characters may be placed within the
         *  quotation marks except for the characters that must be escaped:
         *  quotation mark, reverse solidus, and the control characters (U+0000
         *  through U+001F).
         * "
         */
        if (value[i] == '\\' || value[i] == '"') {
            retval = avs_stream_write(stream, "\\", 1);
        }
        if (retval) {
            return retval;
        }

        if (value[i] == '\b') {
            retval = avs_stream_write(stream, "\\b", 2);
        } else if (value[i] == '\f') {
            retval = avs_stream_write(stream, "\\f", 2);
        } else if (value[i] == '\n') {
            retval = avs_stream_write(stream, "\\n", 2);
        } else if (value[i] == '\r') {
            retval = avs_stream_write(stream, "\\r", 2);
        } else if (value[i] == '\t') {
            retval = avs_stream_write(stream, "\\t", 2);
        } else if ((uint8_t) value[i] < 0x20) {
            const int nibble0 = ((uint8_t)value[i] >> 4) & 0xF;
            const int nibble1 = ((uint8_t)value[i]) & 0xF;
            retval =
                    avs_stream_write_f(stream, "\\u00%x%x", nibble0, nibble1);
        } else {
            retval = avs_stream_write(stream, &value[i], 1);
        }
        if (retval) {
            return retval;
        }
    }
    return avs_stream_write(stream, "\"", 1);
}

static int write_variable(avs_stream_abstract_t *stream,
                          json_data_type_t type,
                          const void *value) {
    int retval =
            avs_stream_write_f(stream, "\"%s\":", data_type_to_string(type));
    if (retval) {
        return retval;
    }

    switch (type) {
    case JSON_DATA_I32:
        return avs_stream_write_f(stream, "%" PRIi32, *(const int32_t *) value);
    case JSON_DATA_I64:
        return avs_stream_write_f(stream, "%" PRIi64, *(const int64_t *) value);
    case JSON_DATA_F32:
        return avs_stream_write_f(stream, "%f", *(const float *) value);
    case JSON_DATA_F64:
        return avs_stream_write_f(stream, "%f", *(const double *) value);
    case JSON_DATA_BOOL:
        return avs_stream_write_f(stream, "%s",
                                  (*(const bool *) value) ? "true" : "false");
    case JSON_DATA_OBJLNK:
        {
            const packed_objlnk_t objlnk = *(const packed_objlnk_t *) value;
            return avs_stream_write_f(stream, "\"%" PRIu16 ":%" PRIu16 "\"",
                                      objlnk.oid, objlnk.iid);
        }
    case JSON_DATA_STRING:
        return write_quoted_string(stream, (const char *) value);
    default:
        json_log(ERROR, "Unsupported json data type: %d", (int) type);
        return -1;
    }
}

static int write_uri(avs_stream_abstract_t *stream,
                     const anjay_uri_path_t *path) {
    int retval = avs_stream_write_f(stream, "/%d", path->oid);
    if (!retval && path->has_iid) {
        retval = avs_stream_write_f(stream, "/%d", path->iid);
    }
    if (!retval && path->has_rid) {
        retval = avs_stream_write_f(stream, "/%d", path->rid);
    }
    return retval;
}

static int write_element_name(json_out_t *ctx) {
    const char *name = NULL;
    char buf[MAX_CHILD_PATH_LEN];
    if (count_child_path_elems(ctx)) {
        if (child_path_to_string(ctx, buf, sizeof(buf)) < 0) {
            return -1;
        }
        name = buf;
    }

    int retval;
    if (name) {
        retval = avs_stream_write_f(ctx->stream, "{\"n\":\"%s\",", name);
    } else {
        retval = avs_stream_write(ctx->stream, "{", 1);
    }
    return retval;
}

static int write_response_element(json_out_t *ctx,
                                  json_data_type_t type,
                                  const void *value) {
    int retval;
    (void) ((retval = write_element_name(ctx))
            || (retval = write_variable(ctx->stream, type, value))
            || (retval = avs_stream_write(ctx->stream, "}", 1)));
    return retval;
}

static int process_array_value(json_out_t *ctx,
                               json_data_type_t type,
                               const void *value) {
    if (ctx->array_ctx.expected_write != EXPECT_VALUE) {
        json_log(ERROR, "expected array index, but got a value instead");
        return -1;
    }
    if (ctx->array_ctx.value_type == JSON_DATA_UNKNOWN) {
        ctx->array_ctx.value_type = type;
    } else if (ctx->array_ctx.value_type != type) {
        json_log(ERROR, "type mismatch, expected %d but got %d",
                 (int) ctx->array_ctx.value_type, (int) type);
        return -1;
    }

    int retval = write_response_element(ctx, type, value);
    if (retval) {
        return retval;
    }
    ctx->array_ctx.expected_write = EXPECT_INDEX;
    return 0;
}

static int finish_ret_bytes(json_out_t *ctx) {
    if (!ctx->bytes) {
        return -1;
    }
    int result;
    (void) ((result = _anjay_base64_ret_bytes_ctx_close(ctx->bytes))
            || (result = avs_stream_write(ctx->stream, "\"}", 2)));
    _anjay_base64_ret_bytes_ctx_delete(&ctx->bytes);
    return result;
}

static int maybe_write_separator(json_out_t *ctx) {
    if (ctx->needs_separator
            && avs_stream_write(ctx->stream, ",", 1)) {
        return -1;
    }
    ctx->needs_separator = true;
    return 0;
}

static int process_single_entry(json_out_t *ctx,
                                json_data_type_t type,
                                const void *value) {
    if (ctx->bytes || maybe_write_separator(ctx)) {
        return -1;
    }

    if (ctx->returning_array) {
        return process_array_value(ctx, type, value);
    } else {
        return write_response_element(ctx, type, value);
    }
}

static int *json_errno_ptr(anjay_output_ctx_t *ctx) {
    return ((json_out_t *) ctx)->errno_ptr;
}

static anjay_ret_bytes_ctx_t *json_ret_bytes(anjay_output_ctx_t *ctx_,
                                             size_t length) {
    json_out_t *ctx = (json_out_t *) ctx_;
    if (ctx->bytes) {
        json_log(ERROR, "bytes are already being returned");
        return NULL;
    }

    int retval;
    (void) ((retval = maybe_write_separator(ctx))
            || (retval = write_element_name(ctx))
            || (retval = avs_stream_write_f(ctx->stream, "\"%s\":\"",
                                            data_type_to_string(
                                                    JSON_DATA_STRING))));
    if (retval) {
        return NULL;
    }
    ctx->bytes = _anjay_base64_ret_bytes_ctx_new(ctx->stream, length);
    if (ctx->bytes && ctx->returning_array) {
        ctx->array_ctx.expected_write = EXPECT_INDEX;
    }
    return ctx->bytes;
}

static int json_ret_string(anjay_output_ctx_t *ctx, const char *value) {
    return process_single_entry((json_out_t *) ctx, JSON_DATA_STRING, value);
}

static int json_ret_i32(anjay_output_ctx_t *ctx, int32_t value) {
    return process_single_entry((json_out_t *) ctx, JSON_DATA_I32, &value);
}

static int json_ret_i64(anjay_output_ctx_t *ctx, int64_t value) {
    return process_single_entry((json_out_t *) ctx, JSON_DATA_I64, &value);
}

static int json_ret_float(anjay_output_ctx_t *ctx, float value) {
    return process_single_entry((json_out_t *) ctx, JSON_DATA_F32, &value);
}

static int json_ret_double(anjay_output_ctx_t *ctx, double value) {
    return process_single_entry((json_out_t *) ctx, JSON_DATA_F64, &value);
}

static int json_ret_bool(anjay_output_ctx_t *ctx, bool value) {
    return process_single_entry((json_out_t *) ctx, JSON_DATA_BOOL, &value);
}

static int
json_ret_objlnk(anjay_output_ctx_t *ctx, anjay_oid_t oid, anjay_iid_t iid) {
    const packed_objlnk_t objlnk = {
        .oid = oid,
        .iid = iid
    };
    return process_single_entry((json_out_t *) ctx, JSON_DATA_OBJLNK, &objlnk);
}

static anjay_output_ctx_t *json_ret_array_start(anjay_output_ctx_t *ctx_) {
    json_out_t *ctx = (json_out_t *) ctx_;
    if (ctx->returning_array) {
        json_log(ERROR, "attempted to start array while already started");
        return NULL;
    }
    if (ctx->bytes) {
        json_log(ERROR, "attempted to start array while returning bytes");
        return NULL;
    }
    memset(&ctx->array_ctx, 0, sizeof(ctx->array_ctx));
    ctx->array_ctx.expected_write = EXPECT_INDEX;
    ctx->array_ctx.value_type = JSON_DATA_UNKNOWN;
    ctx->returning_array = true;
    return ctx_;
}

static int json_ret_array_index(anjay_output_ctx_t *ctx_, anjay_riid_t riid) {
    json_out_t *ctx = (json_out_t *) ctx_;
    if (!ctx->returning_array) {
        json_log(ERROR, "cannot return array index on non-started array");
        return -1;
    }
    if (ctx->array_ctx.expected_write != EXPECT_INDEX) {
        json_log(ERROR, "expected value instead of an index");
        return -1;
    }
    if (ctx->bytes) {
        int result = finish_ret_bytes(ctx);
        if (result) {
            return result;
        }
    }
    ctx->array_ctx.expected_write = EXPECT_VALUE;
    ctx->array_ctx.riid = riid;
    update_node_path(ctx, ANJAY_ID_RIID, riid);
    return 0;
}

static int json_ret_array_finish(anjay_output_ctx_t *ctx_) {
    json_out_t *ctx = (json_out_t *) ctx_;
    if (!ctx->returning_array) {
        json_log(ERROR, "cannot finish non-started array");
        return -1;
    }
    if (ctx->array_ctx.expected_write != EXPECT_INDEX) {
        json_log(ERROR, "expected value for the associated index %" PRIu16,
                 ctx->array_ctx.riid);
        return -1;
    }
    ctx->returning_array = false;
    return 0;
}

static anjay_output_ctx_t *json_ret_object_start(anjay_output_ctx_t *ctx) {
    return ctx;
}

static int json_ret_object_finish(anjay_output_ctx_t *ctx) {
    (void) ctx;
    return 0;
}

static int json_set_id(anjay_output_ctx_t *ctx_,
                       anjay_id_type_t type,
                       uint16_t id) {
    if (type == ANJAY_ID_RIID) {
        return json_ret_array_index(ctx_, id);
    }
    json_out_t *ctx = (json_out_t *) ctx_;
    ctx->next_id.type = type;
    ctx->next_id.id = id;
    update_node_path(ctx, type, id);
    if (ctx->bytes) {
        int result = finish_ret_bytes(ctx);
        if (result) {
            return result;
        }
    }
    json_log(INFO, "set_id(%p, type=%d, id=%d)", (void *) ctx_, (int) type,
             (int) id);
    return 0;
}

static int write_response_finish(avs_stream_abstract_t *stream) {
    return avs_stream_write(stream, "]}", 2);
}

static int json_output_close(anjay_output_ctx_t *ctx_) {
    json_out_t *ctx = (json_out_t *) ctx_;
    if (ctx->bytes) {
        int result = finish_ret_bytes(ctx);
        if (result) {
            return result;
        }
    }
    return write_response_finish(ctx->stream);
}

static const anjay_output_ctx_vtable_t JSON_OUT_VTABLE = {
    json_errno_ptr,
    json_ret_bytes,
    json_ret_string,
    json_ret_i32,
    json_ret_i64,
    json_ret_float,
    json_ret_double,
    json_ret_bool,
    json_ret_objlnk,
    json_ret_array_start,
    json_ret_array_finish,
    json_ret_object_start,
    json_ret_object_finish,
    json_set_id,
    json_output_close
};

static int write_response_preamble(avs_stream_abstract_t *stream,
                                   const anjay_uri_path_t *base) {
    int retval;
    (void) ((retval = avs_stream_write_f(stream, "%s", "{\"bn\":\""))
            || (retval = write_uri(stream, base))
            || (retval = avs_stream_write_f(stream, "%s", "\",\"e\":[")));
    return retval;
}

anjay_output_ctx_t *
_anjay_output_json_create(avs_stream_abstract_t *stream,
                          int *errno_ptr,
                          anjay_msg_details_t *inout_details,
                          const anjay_uri_path_t *uri) {
    json_out_t *ctx = (json_out_t *) avs_calloc(1, sizeof(json_out_t));
    if (ctx) {
        ctx->vtable = &JSON_OUT_VTABLE;
        ctx->errno_ptr = errno_ptr;
        ctx->stream = stream;
        if (uri->has_oid) {
            update_node_path(ctx, ANJAY_ID_OID, uri->oid);
            ++ctx->num_base_path_elems;
        }
        if (uri->has_iid) {
            assert(uri->has_oid);
            update_node_path(ctx, ANJAY_ID_IID, uri->iid);
            ++ctx->num_base_path_elems;
        }
        if (uri->has_rid) {
            assert(uri->has_iid);
            update_node_path(ctx, ANJAY_ID_RID, uri->rid);
            ++ctx->num_base_path_elems;
        }

        if ((*errno_ptr = _anjay_handle_requested_format(
                     &inout_details->format, ANJAY_COAP_FORMAT_JSON))
            || _anjay_coap_stream_setup_response(stream, inout_details)) {
            goto error;
        }
        if (write_response_preamble(stream, uri)) {
            json_log(ERROR, "cannot write response preamble");
            goto error;
        }
        json_log(INFO, "created json context");
    }
    return (anjay_output_ctx_t *) ctx;
error:
    avs_free(ctx);
    return NULL;
}
