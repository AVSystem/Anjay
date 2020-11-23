/*
 * Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
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

#include <anjay_init.h>

#ifndef ANJAY_WITHOUT_TLV

#    include <avsystem/commons/avs_stream_v_table.h>
#    include <avsystem/commons/avs_utils.h>

#    include "../anjay_utils_private.h"

#    include "anjay_common.h"
#    include "anjay_tlv.h"
#    include "anjay_vtable.h"

VISIBILITY_SOURCE_BEGIN

#    define LOG(...) _anjay_log(tlv_in, __VA_ARGS__)

typedef struct {
    anjay_id_type_t type;
    size_t length;
    size_t bytes_read;
} tlv_entry_t;

typedef struct {
    const anjay_input_ctx_vtable_t *vtable;
    avs_stream_t *stream;

    anjay_uri_path_t uri_path;
    // Currently processed path
    bool has_path;
    bool is_array;
    anjay_uri_path_t current_path;
    // we need to store separate length
    // because there might be a "hole" for unspecified IID
    size_t current_path_len;

    AVS_LIST(tlv_entry_t) entries;
    bool finished;
} tlv_in_t;

static tlv_entry_t *tlv_entry_push(tlv_in_t *ctx) {
    return AVS_LIST_INSERT_NEW(tlv_entry_t, &ctx->entries);
}

static void tlv_entry_pop(tlv_in_t *ctx) {
    AVS_LIST_DELETE(&ctx->entries);
}

static int tlv_get_some_bytes(anjay_input_ctx_t *ctx_,
                              size_t *out_bytes_read,
                              bool *out_message_finished,
                              void *out_buf,
                              size_t buf_size) {
    tlv_in_t *ctx = (tlv_in_t *) ctx_;
    if (!ctx->has_path) {
        int result = _anjay_input_get_path(ctx_, NULL, NULL);
        if (result == ANJAY_GET_PATH_END) {
            *out_message_finished = true;
            *out_bytes_read = 0;
            return 0;
        } else if (result) {
            return result;
        }
    }
    if (!ctx->entries) {
        return -1;
    }
    bool stream_finished;
    *out_bytes_read = 0;
    buf_size =
            AVS_MIN(buf_size, ctx->entries->length - ctx->entries->bytes_read);
    avs_error_t err = avs_stream_read(ctx->stream, out_bytes_read,
                                      &stream_finished, out_buf, buf_size);
    ctx->entries->bytes_read += *out_bytes_read;
    if (avs_is_err(err)) {
        return -1;
    }
    ctx->finished = stream_finished;
    if (!(*out_message_finished =
                  (ctx->entries->bytes_read == ctx->entries->length))
            && stream_finished) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    return 0;
}

static int tlv_read_to_end(anjay_input_ctx_t *ctx,
                           size_t *out_bytes_read,
                           void *out_buf,
                           size_t buf_size) {
    bool message_finished;
    char *ptr = (char *) out_buf;
    char *endptr = ptr + buf_size;
    do {
        size_t bytes_read = 0;
        int retval = tlv_get_some_bytes(ctx, &bytes_read, &message_finished,
                                        ptr, (size_t) (endptr - ptr));
        if (retval) {
            return retval;
        }
        ptr += bytes_read;
    } while (!message_finished && ptr < endptr);

    *out_bytes_read = (size_t) (ptr - (char *) out_buf);
    return message_finished ? 0 : ANJAY_BUFFER_TOO_SHORT;
}

static int tlv_read_whole_entry(anjay_input_ctx_t *ctx_,
                                size_t *out_bytes_read,
                                void *out_buf,
                                size_t buf_size) {
    tlv_in_t *ctx = (tlv_in_t *) ctx_;
    if (!ctx->has_path) {
        int result = _anjay_input_get_path(ctx_, NULL, NULL);
        if (result == ANJAY_GET_PATH_END) {
            *out_bytes_read = 0;
            return 0;
        } else if (result) {
            return result;
        }
    }
    if (!ctx->entries || ctx->entries->bytes_read) {
        return -1;
    }
    return tlv_read_to_end(ctx_, out_bytes_read, out_buf, buf_size);
}

static int
tlv_get_string(anjay_input_ctx_t *ctx, char *out_buf, size_t buf_size) {
    assert(buf_size);
    size_t bytes_read = 0;
    int retval = tlv_read_to_end(ctx, &bytes_read, out_buf, buf_size - 1);
    out_buf[bytes_read] = '\0';
    return retval;
}

static int tlv_get_integer(anjay_input_ctx_t *ctx, int64_t *value) {
    uint8_t bytes[8];
    size_t bytes_read = 0;
    int retval;
    if ((retval = tlv_read_whole_entry(ctx, &bytes_read, bytes, sizeof(bytes)))
            || !avs_is_power_of_2(bytes_read)) {
        return retval ? retval : ANJAY_ERR_BAD_REQUEST;
    }
    *value = (bytes_read > 0 && ((int8_t) bytes[0]) < 0) ? -1 : 0;
    for (size_t i = 0; i < bytes_read; ++i) {
        *(uint64_t *) value <<= 8;
        *value += bytes[i];
    }
    return 0;
}

static int tlv_get_double(anjay_input_ctx_t *ctx, double *value) {
    union {
        uint32_t f32;
        uint64_t f64;
    } data;
    size_t bytes_read = 0;
    int retval = tlv_read_whole_entry(ctx, &bytes_read, &data, 8);
    if (retval) {
        return retval;
    }
    switch (bytes_read) {
    case 4:
        *value = avs_ntohf(data.f32);
        return 0;
    case 8:
        *value = avs_ntohd(data.f64);
        return 0;
    default:
        return ANJAY_ERR_BAD_REQUEST;
    }
}

static int tlv_get_bool(anjay_input_ctx_t *ctx, bool *value) {
    char raw;
    size_t bytes_read = 0;
    int retval = tlv_read_whole_entry(ctx, &bytes_read, &raw, 1);
    if (retval == ANJAY_BUFFER_TOO_SHORT || bytes_read != 1) {
        return ANJAY_ERR_BAD_REQUEST;
    } else if (retval) {
        return retval;
    }
    switch (raw) {
    case 0:
        *value = false;
        return 0;
    case 1:
        *value = true;
        return 0;
    default:
        return ANJAY_ERR_BAD_REQUEST;
    }
}

static int tlv_get_objlnk(anjay_input_ctx_t *ctx,
                          anjay_oid_t *out_oid,
                          anjay_iid_t *out_iid) {
    AVS_STATIC_ASSERT(sizeof(uint16_t[2]) == 4, uint16_t_array_size);
    uint16_t raw[2];
    size_t bytes_read = 0;
    int retval = tlv_read_whole_entry(ctx, &bytes_read, raw, 4);
    if (retval == ANJAY_BUFFER_TOO_SHORT || bytes_read != 4) {
        return ANJAY_ERR_BAD_REQUEST;
    } else if (retval) {
        return retval;
    }
    *out_oid = avs_convert_be16(raw[0]);
    *out_iid = avs_convert_be16(raw[1]);
    return 0;
}

#    define DEF_READ_SHORTENED(Type)                                           \
        static int read_shortened_##Type(avs_stream_t *stream, size_t length,  \
                                         Type *out) {                          \
            uint8_t bytes[sizeof(Type)];                                       \
            if (avs_is_err(avs_stream_read_reliably(stream, bytes, length))) { \
                return -1;                                                     \
            }                                                                  \
            *out = 0;                                                          \
            for (size_t i = 0; i < length; ++i) {                              \
                *out = (Type) ((*out << 8) + bytes[i]);                        \
            }                                                                  \
            return 0;                                                          \
        }

DEF_READ_SHORTENED(uint16_t)
DEF_READ_SHORTENED(size_t)

static tlv_id_type_t tlv_type_from_typefield(uint8_t typefield) {
    return (tlv_id_type_t) ((typefield >> 6) & 3);
}

static anjay_id_type_t convert_id_type(uint8_t typefield) {
    switch (tlv_type_from_typefield(typefield)) {
    default:
        AVS_UNREACHABLE("Invalid TLV ID type");
    case TLV_ID_IID:
        return ANJAY_ID_IID;
    case TLV_ID_RIID:
        return ANJAY_ID_RIID;
    case TLV_ID_RID_ARRAY:
    case TLV_ID_RID:
        return ANJAY_ID_RID;
    }
}

static int get_id(tlv_in_t *ctx,
                  anjay_id_type_t *out_type,
                  uint16_t *out_id,
                  bool *out_has_value,
                  size_t *out_bytes_read,
                  bool *out_is_array) {
    uint8_t typefield;
    avs_error_t err = avs_stream_read_reliably(ctx->stream, &typefield, 1);
    if (avs_is_eof(err)) {
        return ANJAY_GET_PATH_END;
    } else if (avs_is_err(err)) {
        return -1;
    }
    *out_bytes_read = 1;
    tlv_id_type_t tlv_type = tlv_type_from_typefield(typefield);
    *out_is_array = (tlv_type == TLV_ID_RID_ARRAY);
    *out_type = convert_id_type(typefield);
    size_t id_length = (typefield & 0x20) ? 2 : 1;
    if (read_shortened_uint16_t(ctx->stream, id_length, out_id)) {
        return -1;
    }
    *out_bytes_read += id_length;

    size_t length_length = ((typefield >> 3) & 3);
    if (!length_length) {
        ctx->entries->length = (typefield & 7);
    } else if (read_shortened_size_t(ctx->stream, length_length,
                                     &ctx->entries->length)) {
        return -1;
    }
    *out_bytes_read += length_length;
    /**
     * This may seem a little bit strange, but entries that do not have any
     * payload may be considered as having a value - that is, an empty one. On
     * the other hand, if they DO have the payload, then it only makes sense to
     * return them if they're "terminal" - i.e. they're either resource
     * instances or single resources with value.
     */
    *out_has_value = !ctx->entries->length || tlv_type == TLV_ID_RIID
                     || tlv_type == TLV_ID_RID;
    ctx->entries->bytes_read = 0;
    ctx->entries->type = *out_type;
    return 0;
}

static int tlv_get_path(anjay_input_ctx_t *ctx,
                        anjay_uri_path_t *out_path,
                        bool *out_is_array) {
    tlv_in_t *in = (tlv_in_t *) ctx;
    if (in->finished) {
        return ANJAY_GET_PATH_END;
    }
    if (in->has_path) {
        *out_path = in->current_path;
        *out_is_array = in->is_array;
        return 0;
    }
    bool has_value = false;
    anjay_id_type_t type;
    uint16_t id;
    int result = 0;
    while (!has_value) {
        tlv_entry_t *parent = in->entries;
        if (!tlv_entry_push(in)) {
            return ANJAY_ERR_INTERNAL;
        }
        size_t header_len;
        if ((result = get_id(in, &type, &id, &has_value, &header_len,
                             &in->is_array))) {
            break;
        }
        if (id == ANJAY_ID_INVALID) {
            return ANJAY_ERR_BAD_REQUEST;
        }
        if (parent) {
            // Assume the child entry is fully read (which is in fact necessary
            // to be able to return back to the parent).
            parent->bytes_read += in->entries->length + header_len;
            if (parent->bytes_read > parent->length) {
                LOG(DEBUG, _("child entry is longer than its parent"));
                return ANJAY_ERR_BAD_REQUEST;
            }
        }
        in->current_path.ids[(size_t) type] = id;
        in->current_path_len = (size_t) type + 1;

        if (_anjay_uri_path_outside_base(&in->current_path, &in->uri_path)) {
            LOG(LAZY_DEBUG,
                _("parsed path ") "%s" _(" would be outside of uri-path ") "%s",
                ANJAY_DEBUG_MAKE_PATH(&in->current_path),
                ANJAY_DEBUG_MAKE_PATH(&in->uri_path));
            return ANJAY_ERR_BAD_REQUEST;
        }
    }
    *out_path = in->current_path;
    *out_is_array = in->is_array;
    in->has_path = true;
    return result;
}

static int tlv_next_entry(anjay_input_ctx_t *ctx) {
    tlv_in_t *in = (tlv_in_t *) ctx;
    if (!in->has_path) {
        // Next entry is already available and should be processed.
        return 0;
    }
    if (!in->entries) {
        return -1;
    }
    bool finished = false;
    while (!finished) {
        char ignored[64];
        size_t bytes_read;
        int retval = tlv_get_some_bytes(ctx, &bytes_read, &finished, ignored,
                                        sizeof(ignored));
        if (retval) {
            return retval;
        }
    }
    in->has_path = false;
    in->is_array = false;
    while (in->entries && in->entries->length == in->entries->bytes_read) {
        in->current_path.ids[in->entries->type] = ANJAY_ID_INVALID;
        in->current_path_len = (size_t) in->entries->type;
        tlv_entry_pop(in);
    }
    return 0;
}

static int tlv_update_root_path(anjay_input_ctx_t *ctx_,
                                const anjay_uri_path_t *root_path) {
    tlv_in_t *ctx = (tlv_in_t *) ctx_;
    anjay_uri_path_t new_path = (root_path ? *root_path : MAKE_ROOT_PATH());
    size_t i;
    for (i = 0; i < AVS_ARRAY_SIZE(new_path.ids)
                && new_path.ids[i] != ANJAY_ID_INVALID;
         ++i) {
        if (ctx->uri_path.ids[i] == ANJAY_ID_INVALID
                && i < ctx->current_path_len
                && ctx->current_path.ids[i] != ANJAY_ID_INVALID) {
            // updating the root path would overwrite value actually read from
            // payload
            return -1;
        } else {
            ctx->current_path.ids[i] = ctx->uri_path.ids[i] = new_path.ids[i];
        }
    }
    ctx->current_path_len = AVS_MAX(ctx->current_path_len, i);
    return 0;
}

static int tlv_in_close(anjay_input_ctx_t *ctx_) {
    tlv_in_t *ctx = (tlv_in_t *) ctx_;
    if (ctx->entries && !ctx->finished) {
        LOG(DEBUG, _("input context is destroyed but not fully processed yet"));
    }
    AVS_LIST_CLEAR(&ctx->entries);
    return 0;
}

static const anjay_input_ctx_vtable_t TLV_IN_VTABLE = {
    .some_bytes = tlv_get_some_bytes,
    .string = tlv_get_string,
    .integer = tlv_get_integer,
    .floating = tlv_get_double,
    .boolean = tlv_get_bool,
    .objlnk = tlv_get_objlnk,
    .get_path = tlv_get_path,
    .next_entry = tlv_next_entry,
    .update_root_path = tlv_update_root_path,
    .close = tlv_in_close
};

int _anjay_input_tlv_create(anjay_input_ctx_t **out,
                            avs_stream_t **stream_ptr,
                            const anjay_uri_path_t *request_uri) {
    tlv_in_t *ctx = (tlv_in_t *) avs_calloc(1, sizeof(tlv_in_t));
    *out = (anjay_input_ctx_t *) ctx;
    if (!ctx) {
        return -1;
    }
    ctx->vtable = &TLV_IN_VTABLE;
    ctx->stream = *stream_ptr;
    ctx->uri_path = (request_uri ? *request_uri : MAKE_ROOT_PATH());
    ctx->current_path = ctx->uri_path;

    return 0;
}

#    ifdef ANJAY_TEST
#        include "tests/core/io/tlv_in.c"
#    endif

#endif // ANJAY_WITHOUT_TLV
