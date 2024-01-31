/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <string.h>

#include "dm_core.h"
#include "dm_utils/dm_utils.h"

struct dm_list_ctx_struct {
    dm_list_ctx_emit_t *emit;
};

void dm_emit(dm_list_ctx_t *ctx, uint16_t id) {
    assert(ctx && ctx->emit);
    ctx->emit(ctx, id);
}

int dm_ret_bytes(dm_output_ctx_t *ctx, void *data, size_t data_len) {
    dm_output_internal_ctx_t *internal_out_ctx =
            (dm_output_internal_ctx_t *) ctx;
    fluf_io_out_entry_t entry = {
        .path = internal_out_ctx->path,
        .type = FLUF_DATA_TYPE_BYTES,
        .value.bytes_or_string.data = data,
        .value.bytes_or_string.chunk_length = data_len,
        .value.bytes_or_string.full_length_hint = data_len
    };
    assert(internal_out_ctx->output_ctx->callback);
    return internal_out_ctx->output_ctx->callback(
            internal_out_ctx->output_ctx->arg, &entry);
}

int dm_ret_string(dm_output_ctx_t *ctx, char *value) {
    dm_output_internal_ctx_t *internal_out_ctx =
            (dm_output_internal_ctx_t *) ctx;
    fluf_io_out_entry_t entry = {
        .path = internal_out_ctx->path,
        .type = FLUF_DATA_TYPE_STRING,
        .value.bytes_or_string.data = value
    };
    entry.value.bytes_or_string.chunk_length = strlen(value);
    entry.value.bytes_or_string.full_length_hint =
            entry.value.bytes_or_string.chunk_length;
    assert(internal_out_ctx->output_ctx->callback);
    return internal_out_ctx->output_ctx->callback(
            internal_out_ctx->output_ctx->arg, &entry);
}

int dm_ret_external_bytes(dm_output_ctx_t *ctx,
                          fluf_get_external_data_t *get_external_data,
                          void *user_args,
                          size_t length) {
    dm_output_internal_ctx_t *internal_out_ctx =
            (dm_output_internal_ctx_t *) ctx;
    fluf_io_out_entry_t entry = {
        .path = internal_out_ctx->path,
        .type = FLUF_DATA_TYPE_EXTERNAL_BYTES,
        .value.external_data.get_external_data = get_external_data,
        .value.external_data.user_args = user_args,
        .value.external_data.length = length
    };
    assert(internal_out_ctx->output_ctx->callback);
    return internal_out_ctx->output_ctx->callback(
            internal_out_ctx->output_ctx->arg, &entry);
}

int dm_ret_external_string(dm_output_ctx_t *ctx,
                           fluf_get_external_data_t *get_external_data,
                           void *user_args,
                           size_t length) {
    dm_output_internal_ctx_t *internal_out_ctx =
            (dm_output_internal_ctx_t *) ctx;
    fluf_io_out_entry_t entry = {
        .path = internal_out_ctx->path,
        .type = FLUF_DATA_TYPE_EXTERNAL_STRING,
        .value.external_data.get_external_data = get_external_data,
        .value.external_data.user_args = user_args,
        .value.external_data.length = length
    };
    assert(internal_out_ctx->output_ctx->callback);
    return internal_out_ctx->output_ctx->callback(
            internal_out_ctx->output_ctx->arg, &entry);
}

int dm_ret_i64(dm_output_ctx_t *ctx, int64_t value) {
    dm_output_internal_ctx_t *internal_out_ctx =
            (dm_output_internal_ctx_t *) ctx;
    fluf_io_out_entry_t entry = {
        .path = internal_out_ctx->path,
        .type = FLUF_DATA_TYPE_INT,
        .value.int_value = value
    };
    assert(internal_out_ctx->output_ctx->callback);
    return internal_out_ctx->output_ctx->callback(
            internal_out_ctx->output_ctx->arg, &entry);
}

int dm_ret_u64(dm_output_ctx_t *ctx, uint64_t value) {
    dm_output_internal_ctx_t *internal_out_ctx =
            (dm_output_internal_ctx_t *) ctx;
    fluf_io_out_entry_t entry = {
        .path = internal_out_ctx->path,
        .type = FLUF_DATA_TYPE_UINT,
        .value.uint_value = value
    };
    assert(internal_out_ctx->output_ctx->callback);
    return internal_out_ctx->output_ctx->callback(
            internal_out_ctx->output_ctx->arg, &entry);
}

int dm_ret_double(dm_output_ctx_t *ctx, double value) {
    dm_output_internal_ctx_t *internal_out_ctx =
            (dm_output_internal_ctx_t *) ctx;
    fluf_io_out_entry_t entry = {
        .path = internal_out_ctx->path,
        .type = FLUF_DATA_TYPE_DOUBLE,
        .value.double_value = value
    };
    assert(internal_out_ctx->output_ctx->callback);
    return internal_out_ctx->output_ctx->callback(
            internal_out_ctx->output_ctx->arg, &entry);
}

int dm_ret_bool(dm_output_ctx_t *ctx, bool value) {
    dm_output_internal_ctx_t *internal_out_ctx =
            (dm_output_internal_ctx_t *) ctx;
    fluf_io_out_entry_t entry = {
        .path = internal_out_ctx->path,
        .type = FLUF_DATA_TYPE_BOOL,
        .value.bool_value = value
    };
    assert(internal_out_ctx->output_ctx->callback);
    return internal_out_ctx->output_ctx->callback(
            internal_out_ctx->output_ctx->arg, &entry);
}

int dm_ret_objlnk(dm_output_ctx_t *ctx, fluf_oid_t oid, fluf_iid_t iid) {
    dm_output_internal_ctx_t *internal_out_ctx =
            (dm_output_internal_ctx_t *) ctx;
    fluf_io_out_entry_t entry = {
        .path = internal_out_ctx->path,
        .type = FLUF_DATA_TYPE_OBJLNK,
        .value.objlnk.oid = oid,
        .value.objlnk.iid = iid
    };
    assert(internal_out_ctx->output_ctx->callback);
    return internal_out_ctx->output_ctx->callback(
            internal_out_ctx->output_ctx->arg, &entry);
}

int dm_ret_time(dm_output_ctx_t *ctx, uint64_t time) {
    dm_output_internal_ctx_t *internal_out_ctx =
            (dm_output_internal_ctx_t *) ctx;
    fluf_io_out_entry_t entry = {
        .path = internal_out_ctx->path,
        .type = FLUF_DATA_TYPE_TIME,
        .value.uint_value = time
    };
    assert(internal_out_ctx->output_ctx->callback);
    return internal_out_ctx->output_ctx->callback(
            internal_out_ctx->output_ctx->arg, &entry);
}

static int call_to_user_callback(dm_input_internal_ctx_t *internal_in_ctx,
                                 fluf_data_type_t type) {
    int result = 0;
    if (!internal_in_ctx->callback_called_flag) {
        assert(internal_in_ctx->input_ctx->callback);
        if ((result = internal_in_ctx->input_ctx->callback(
                     internal_in_ctx->input_ctx->arg,
                     type,
                     internal_in_ctx->provided_entry))) {
            return result;
        }
        internal_in_ctx->callback_called_flag = true;
        if (internal_in_ctx->provided_entry->type != type) {
            return FLUF_COAP_CODE_BAD_REQUEST;
        }
    }
    return result;
}

static int get_bytes_impl(dm_input_internal_ctx_t *internal_in_ctx,
                          size_t *out_bytes_read,
                          bool *out_message_finished,
                          void *out_buf,
                          size_t buf_size) {
    if (buf_size > (internal_in_ctx->provided_entry->value.bytes_or_string
                            .chunk_length
                    - internal_in_ctx->buff_indicator)) {
        // TODO: Only single chunk values are supported for now
        if (internal_in_ctx->provided_entry->value.bytes_or_string.offset
                        + internal_in_ctx->provided_entry->value.bytes_or_string
                                  .chunk_length
                != internal_in_ctx->provided_entry->value.bytes_or_string
                           .full_length_hint) {
            return -1;
        }
        memcpy(out_buf,
               (const char *) internal_in_ctx->provided_entry->value
                               .bytes_or_string.data
                       + internal_in_ctx->buff_indicator,
               internal_in_ctx->provided_entry->value.bytes_or_string
                               .chunk_length
                       - internal_in_ctx->buff_indicator);
        if (out_bytes_read) {
            *out_bytes_read = internal_in_ctx->provided_entry->value
                                      .bytes_or_string.chunk_length
                              - internal_in_ctx->buff_indicator;
        }
        if (out_message_finished) {
            *out_message_finished = true;
        }
    } else {
        size_t to_write = AVS_MIN(buf_size,
                                  internal_in_ctx->provided_entry->value
                                                  .bytes_or_string.chunk_length
                                          - internal_in_ctx->buff_indicator);
        memcpy(out_buf,
               (const char *) internal_in_ctx->provided_entry->value
                               .bytes_or_string.data
                       + internal_in_ctx->buff_indicator,
               to_write);
        internal_in_ctx->buff_indicator += to_write;
        if (out_bytes_read) {
            *out_bytes_read = to_write;
        }
        if (out_message_finished) {
            *out_message_finished = false;
        }
    }
    return 0;
}

int dm_get_bytes(dm_input_ctx_t *ctx,
                 size_t *out_bytes_read,
                 bool *out_message_finished,
                 void *out_buf,
                 size_t buf_size) {
    if (buf_size == 0) {
        return -1;
    }

    dm_input_internal_ctx_t *internal_in_ctx = (dm_input_internal_ctx_t *) ctx;

    int result = call_to_user_callback(internal_in_ctx, FLUF_DATA_TYPE_BYTES);
    if (result) {
        return result;
    }

    return get_bytes_impl(internal_in_ctx,
                          out_bytes_read,
                          out_message_finished,
                          out_buf,
                          buf_size);
}

int dm_get_string(dm_input_ctx_t *ctx, char *out_buf, size_t buf_size) {
    if (buf_size == 0) {
        // At least terminating nullbyte must fit into the buffer!
        return DM_BUFFER_TOO_SHORT;
    }

    dm_input_internal_ctx_t *internal_in_ctx = (dm_input_internal_ctx_t *) ctx;

    int result = call_to_user_callback(internal_in_ctx, FLUF_DATA_TYPE_STRING);
    if (result) {
        return result;
    }

    size_t bytes_read;
    bool message_finished;
    if ((result = get_bytes_impl(internal_in_ctx,
                                 &bytes_read,
                                 &message_finished,
                                 out_buf,
                                 buf_size - 1))) {
        return result;
    }
    assert(bytes_read < buf_size);
    out_buf[bytes_read] = '\0';
    return message_finished ? 0 : DM_BUFFER_TOO_SHORT;
}

int dm_get_external_bytes(dm_input_ctx_t *ctx,
                          fluf_get_external_data_t **out_get_external_data,
                          void **out_user_args,
                          size_t *out_length) {
    dm_input_internal_ctx_t *internal_in_ctx = (dm_input_internal_ctx_t *) ctx;

    int result = call_to_user_callback(internal_in_ctx,
                                       FLUF_DATA_TYPE_EXTERNAL_BYTES);
    if (result) {
        return result;
    }

    *out_get_external_data = internal_in_ctx->provided_entry->value
                                     .external_data.get_external_data;
    *out_user_args =
            internal_in_ctx->provided_entry->value.external_data.user_args;
    *out_length = internal_in_ctx->provided_entry->value.external_data.length;
    return 0;
}

int dm_get_external_string(dm_input_ctx_t *ctx,
                           fluf_get_external_data_t **out_get_external_data,
                           void **out_user_args,
                           size_t *out_length) {
    dm_input_internal_ctx_t *internal_in_ctx = (dm_input_internal_ctx_t *) ctx;

    int result = call_to_user_callback(internal_in_ctx,
                                       FLUF_DATA_TYPE_EXTERNAL_BYTES);
    if (result) {
        return result;
    }

    *out_get_external_data = internal_in_ctx->provided_entry->value
                                     .external_data.get_external_data;
    *out_user_args =
            internal_in_ctx->provided_entry->value.external_data.user_args;
    *out_length = internal_in_ctx->provided_entry->value.external_data.length;
    return 0;
}

int dm_get_i64(dm_input_ctx_t *ctx, int64_t *out) {
    dm_input_internal_ctx_t *internal_in_ctx = (dm_input_internal_ctx_t *) ctx;

    int result = call_to_user_callback(internal_in_ctx, FLUF_DATA_TYPE_INT);
    if (result) {
        return result;
    }

    *out = internal_in_ctx->provided_entry->value.int_value;
    return 0;
}

int dm_get_u64(dm_input_ctx_t *ctx, uint64_t *out) {
    dm_input_internal_ctx_t *internal_in_ctx = (dm_input_internal_ctx_t *) ctx;

    int result = call_to_user_callback(internal_in_ctx, FLUF_DATA_TYPE_UINT);
    if (result) {
        return result;
    }

    *out = internal_in_ctx->provided_entry->value.uint_value;
    return 0;
}

int dm_get_u32(dm_input_ctx_t *ctx, uint32_t *out) {
    uint64_t tmp;
    int result = dm_get_u64(ctx, &tmp);
    if (!result) {
        if (tmp > UINT32_MAX) {
            result = FLUF_COAP_CODE_BAD_REQUEST;
        } else {
            *out = (uint32_t) tmp;
        }
    }
    return result;
}

int dm_get_double(dm_input_ctx_t *ctx, double *out) {
    dm_input_internal_ctx_t *internal_in_ctx = (dm_input_internal_ctx_t *) ctx;

    int result = call_to_user_callback(internal_in_ctx, FLUF_DATA_TYPE_DOUBLE);
    if (result) {
        return result;
    }

    *out = internal_in_ctx->provided_entry->value.double_value;
    return 0;
}

int dm_get_bool(dm_input_ctx_t *ctx, bool *out) {
    dm_input_internal_ctx_t *internal_in_ctx = (dm_input_internal_ctx_t *) ctx;

    int result = call_to_user_callback(internal_in_ctx, FLUF_DATA_TYPE_BOOL);
    if (result) {
        return result;
    }

    *out = internal_in_ctx->provided_entry->value.bool_value;
    return 0;
}

int dm_get_objlnk(dm_input_ctx_t *ctx,
                  fluf_oid_t *out_oid,
                  fluf_iid_t *out_iid) {
    dm_input_internal_ctx_t *internal_in_ctx = (dm_input_internal_ctx_t *) ctx;

    int result = call_to_user_callback(internal_in_ctx, FLUF_DATA_TYPE_OBJLNK);
    if (result) {
        return result;
    }

    *out_oid = internal_in_ctx->provided_entry->value.objlnk.oid;
    *out_iid = internal_in_ctx->provided_entry->value.objlnk.iid;
    return 0;
}

int dm_get_time(dm_input_ctx_t *ctx, int64_t *time) {
    dm_input_internal_ctx_t *internal_in_ctx = (dm_input_internal_ctx_t *) ctx;

    int result = call_to_user_callback(internal_in_ctx, FLUF_DATA_TYPE_TIME);
    if (result) {
        return result;
    }

    *time = internal_in_ctx->provided_entry->value.time_value;
    return 0;
}
