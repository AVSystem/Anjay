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

#if defined(ANJAY_WITH_OBSERVE) || defined(ANJAY_WITH_SEND)

#    include "../anjay_access_utils_private.h"
#    include "../dm/anjay_dm_read.h"
#    include "anjay_batch_builder.h"
#    include "anjay_vtable.h"

#    include <avsystem/commons/avs_list.h>
#    include <avsystem/commons/avs_utils.h>

#    include <anjay_modules/anjay_dm_utils.h>

#    include <string.h>

VISIBILITY_SOURCE_BEGIN

#    define batch_log(level, ...) _anjay_log(batch_builder, level, __VA_ARGS__)

typedef enum {
    ANJAY_BATCH_DATA_BYTES,
    ANJAY_BATCH_DATA_STRING,
    ANJAY_BATCH_DATA_INT,
    ANJAY_BATCH_DATA_DOUBLE,
    ANJAY_BATCH_DATA_BOOL,
    ANJAY_BATCH_DATA_OBJLNK,
    ANJAY_BATCH_DATA_START_AGGREGATE
} anjay_batch_data_type_t;

typedef struct {
    anjay_batch_data_type_t type;
    union {
        struct {
            const void *data;
            size_t length;
        } bytes;
        const char *string;
        int64_t int_value;
        double double_value;
        bool bool_value;
        struct {
            anjay_oid_t oid;
            anjay_iid_t iid;
        } objlnk;
    } value;
} anjay_batch_data_t;

typedef struct {
    anjay_uri_path_t path;
    anjay_batch_data_t data;
    avs_time_real_t timestamp;
} anjay_batch_entry_t;

struct anjay_batch_struct {
    AVS_LIST(anjay_batch_entry_t) list;
    size_t ref_count;
    avs_time_real_t compilation_time;
};

struct anjay_batch_data_output_state_struct {
    anjay_batch_entry_t entry;
};

typedef struct {
    const anjay_ret_bytes_ctx_vtable_t *vtable;
    void *data;
    size_t remaining_bytes;
} builder_bytes_t;

typedef struct builder_out_struct {
    anjay_output_ctx_t base;
    anjay_batch_builder_t *builder;
    builder_bytes_t bytes;

    anjay_uri_path_t root_path;
    anjay_uri_path_t path;
} builder_out_ctx_t;

struct anjay_batch_builder_struct {
    AVS_LIST(anjay_batch_entry_t) list;
    AVS_LIST(anjay_batch_entry_t) *last_element;
};

anjay_batch_builder_t *_anjay_batch_builder_new(void) {
    anjay_batch_builder_t *builder =
            (anjay_batch_builder_t *) avs_calloc(1,
                                                 sizeof(anjay_batch_builder_t));
    if (!builder) {
        return NULL;
    }
    builder->last_element = &builder->list;
    return builder;
}

static int make_data_with_duplicated_string(anjay_batch_data_t *batch_data,
                                            const char *str) {
    assert(batch_data);
    assert(str);
    char *new_str = avs_strdup(str);
    if (!new_str) {
        return -1;
    }
    *batch_data = (anjay_batch_data_t) {
        .type = ANJAY_BATCH_DATA_STRING,
        .value = {
            .string = new_str
        }
    };
    return 0;
}

static int batch_data_add(anjay_batch_builder_t *builder,
                          const anjay_uri_path_t *uri,
                          avs_time_real_t timestamp,
                          anjay_batch_data_t data) {
    assert(builder);
    if (data.type != ANJAY_BATCH_DATA_START_AGGREGATE
            && !_anjay_uri_path_has(uri, ANJAY_ID_RID)) {
        return -1;
    }
    *builder->last_element = AVS_LIST_NEW_ELEMENT(anjay_batch_entry_t);
    if (!*builder->last_element) {
        return -1;
    }
    **builder->last_element = (anjay_batch_entry_t) {
        .path = *uri,
        .timestamp = timestamp,
        .data = data
    };
    AVS_LIST_ADVANCE_PTR(&builder->last_element);
    return 0;
}

int _anjay_batch_add_int(anjay_batch_builder_t *builder,
                         const anjay_uri_path_t *uri,
                         avs_time_real_t timestamp,
                         int64_t value) {
    const anjay_batch_data_t data = {
        .type = ANJAY_BATCH_DATA_INT,
        .value = {
            .int_value = value
        }
    };
    return batch_data_add(builder, uri, timestamp, data);
}

int _anjay_batch_add_double(anjay_batch_builder_t *builder,
                            const anjay_uri_path_t *uri,
                            avs_time_real_t timestamp,
                            double value) {
    const anjay_batch_data_t data = {
        .type = ANJAY_BATCH_DATA_DOUBLE,
        .value = {
            .double_value = value
        }
    };
    return batch_data_add(builder, uri, timestamp, data);
}

int _anjay_batch_add_bool(anjay_batch_builder_t *builder,
                          const anjay_uri_path_t *uri,
                          avs_time_real_t timestamp,
                          bool value) {
    const anjay_batch_data_t data = {
        .type = ANJAY_BATCH_DATA_BOOL,
        .value = {
            .bool_value = value
        }
    };
    return batch_data_add(builder, uri, timestamp, data);
}

int _anjay_batch_add_string(anjay_batch_builder_t *builder,
                            const anjay_uri_path_t *uri,
                            avs_time_real_t timestamp,
                            const char *str) {
    anjay_batch_data_t str_data;
    if (make_data_with_duplicated_string(&str_data, str)) {
        return -1;
    }
    return batch_data_add(builder, uri, timestamp, str_data);
}

int _anjay_batch_add_objlnk(anjay_batch_builder_t *builder,
                            const anjay_uri_path_t *uri,
                            avs_time_real_t timestamp,
                            anjay_oid_t objlnk_oid,
                            anjay_iid_t objlnk_iid) {
    const anjay_batch_data_t data = {
        .type = ANJAY_BATCH_DATA_OBJLNK,
        .value = {
            .objlnk = {
                .oid = objlnk_oid,
                .iid = objlnk_iid
            }
        }
    };
    return batch_data_add(builder, uri, timestamp, data);
}

static void batch_entry_cleanup(void *entry_) {
    anjay_batch_entry_t *entry = (anjay_batch_entry_t *) entry_;
    if (entry->data.type == ANJAY_BATCH_DATA_STRING) {
        avs_free((void *) (intptr_t) entry->data.value.string);
    } else if (entry->data.type == ANJAY_BATCH_DATA_BYTES) {
        avs_free((void *) (intptr_t) entry->data.value.bytes.data);
    }
}

static void list_cleanup(AVS_LIST(anjay_batch_entry_t) list) {
    AVS_LIST_CLEAR(&list) {
        batch_entry_cleanup(list);
    }
}

void _anjay_batch_builder_cleanup(anjay_batch_builder_t **builder) {
    if (builder && *builder) {
        list_cleanup((*builder)->list);
        avs_free(*builder);
        *builder = NULL;
    }
}

anjay_batch_t *_anjay_batch_builder_compile(anjay_batch_builder_t **builder) {
    assert(builder && *builder);
    anjay_batch_t *batch =
            (anjay_batch_t *) avs_calloc(1, sizeof(anjay_batch_t));
    if (!batch) {
        return NULL;
    }
    batch->list = (*builder)->list;
    batch->ref_count = 1;
    batch->compilation_time = avs_time_real_now();
    avs_free(*builder);
    *builder = NULL;
    return batch;
}

anjay_batch_t *_anjay_batch_acquire(const anjay_batch_t *batch_) {
    assert(batch_);
    anjay_batch_t *batch = (anjay_batch_t *) (intptr_t) batch_;
    batch->ref_count++;
    return batch;
}

void _anjay_batch_release(anjay_batch_t **batch) {
    assert(batch && *batch);
    assert((*batch)->ref_count);

    if (--((*batch)->ref_count) == 0) {
        list_cleanup((*batch)->list);
        avs_free(*batch);
    }
    *batch = NULL;
}

static void value_returned(builder_out_ctx_t *ctx) {
    ctx->path = MAKE_ROOT_PATH();
}

static int
bytes_append(anjay_ret_bytes_ctx_t *ctx, const void *data, size_t length) {
    builder_bytes_t *bytes = (builder_bytes_t *) ctx;
    if (length > bytes->remaining_bytes) {
        batch_log(DEBUG,
                  _("tried to write too many bytes, expected ") "%u" _(
                          ", got ") "%u",
                  (unsigned) bytes->remaining_bytes, (unsigned) length);
        return -1;
    }

    memcpy(bytes->data, data, length);
    bytes->data = ((uint8_t *) bytes->data) + length;
    bytes->remaining_bytes -= length;
    return 0;
}

static const anjay_ret_bytes_ctx_vtable_t BYTES_VTABLE = {
    .append = bytes_append
};

static int bytes_begin(anjay_output_ctx_t *ctx_,
                       size_t length,
                       anjay_ret_bytes_ctx_t **out_bytes_ctx) {
    builder_out_ctx_t *ctx = (builder_out_ctx_t *) ctx_;
    if (ctx->bytes.remaining_bytes) {
        batch_log(ERROR, _("bytes already being returned"));
        return -1;
    }

    if (!_anjay_uri_path_has(&ctx->path, ANJAY_ID_RID)) {
        return -1;
    }

    void *buf = NULL;
    if (length) {
        buf = avs_malloc(length);
        if (!buf) {
            return -1;
        }
    }

    anjay_batch_data_t data = {
        .type = ANJAY_BATCH_DATA_BYTES,
        .value.bytes = {
            .data = buf,
            .length = length
        }
    };

    if (batch_data_add(ctx->builder, &ctx->path, avs_time_real_now(), data)) {
        avs_free(buf);
        return -1;
    }

    value_returned(ctx);

    // Doesn't change owner of buf
    ctx->bytes.data = buf;
    ctx->bytes.remaining_bytes = length;
    *out_bytes_ctx = (anjay_ret_bytes_ctx_t *) &ctx->bytes;
    return 0;
}

static int ret_string(anjay_output_ctx_t *ctx_, const char *str) {
    builder_out_ctx_t *ctx = (builder_out_ctx_t *) ctx_;
    int result = -1;
    if (_anjay_uri_path_has(&ctx->path, ANJAY_ID_RID)) {
        result = _anjay_batch_add_string(ctx->builder, &ctx->path,
                                         avs_time_real_now(), str);
        value_returned(ctx);
    }
    return result;
}

static int ret_integer(anjay_output_ctx_t *ctx_, int64_t value) {
    builder_out_ctx_t *ctx = (builder_out_ctx_t *) ctx_;
    int result = -1;
    if (_anjay_uri_path_has(&ctx->path, ANJAY_ID_RID)) {
        result = _anjay_batch_add_int(ctx->builder, &ctx->path,
                                      avs_time_real_now(), value);
        value_returned(ctx);
    }
    return result;
}

static int ret_double(anjay_output_ctx_t *ctx_, double value) {
    builder_out_ctx_t *ctx = (builder_out_ctx_t *) ctx_;
    int result = -1;
    if (_anjay_uri_path_has(&ctx->path, ANJAY_ID_RID)) {
        result = _anjay_batch_add_double(ctx->builder, &ctx->path,
                                         avs_time_real_now(), value);
        value_returned(ctx);
    }
    return result;
}

static int ret_bool(anjay_output_ctx_t *ctx_, bool value) {
    builder_out_ctx_t *ctx = (builder_out_ctx_t *) ctx_;
    int result = -1;
    if (_anjay_uri_path_has(&ctx->path, ANJAY_ID_RID)) {
        result = _anjay_batch_add_bool(ctx->builder, &ctx->path,
                                       avs_time_real_now(), value);
        value_returned(ctx);
    }
    return result;
}

static int ret_objlnk(anjay_output_ctx_t *ctx_,
                      anjay_oid_t objlnk_oid,
                      anjay_iid_t objlnk_iid) {
    builder_out_ctx_t *ctx = (builder_out_ctx_t *) ctx_;
    int result = -1;
    if (_anjay_uri_path_has(&ctx->path, ANJAY_ID_RID)) {
        result = _anjay_batch_add_objlnk(ctx->builder,
                                         &ctx->path,
                                         avs_time_real_now(),
                                         objlnk_oid,
                                         objlnk_iid);
        value_returned(ctx);
    }
    return result;
}

static int ret_start_aggregate(anjay_output_ctx_t *ctx_) {
    builder_out_ctx_t *ctx = (builder_out_ctx_t *) ctx_;
    int result = -1;
    if (_anjay_uri_path_leaf_is(&ctx->path, ANJAY_ID_IID)
            || _anjay_uri_path_leaf_is(&ctx->path, ANJAY_ID_RID)) {
        result = batch_data_add(ctx->builder, &ctx->path, AVS_TIME_REAL_INVALID,
                                (const anjay_batch_data_t) {
                                    .type = ANJAY_BATCH_DATA_START_AGGREGATE
                                });
        // Start Aggregate MUST be followed by some kind of set_path(),
        // so it's safe to treat it as a quasi-value of itself
        value_returned(ctx);
    }
    return result;
}

static int set_path(anjay_output_ctx_t *ctx_, const anjay_uri_path_t *path) {
    builder_out_ctx_t *ctx = (builder_out_ctx_t *) ctx_;
    AVS_ASSERT(!_anjay_uri_path_outside_base(path, &ctx->root_path),
               "Attempted to use batch builder context with resources outside "
               "the declared root path");
    if (_anjay_uri_path_length(&ctx->path) > 0) {
        batch_log(ERROR, _("Path already set"));
        return -1;
    }
    ctx->path = *path;
    return 0;
}

static int clear_path(anjay_output_ctx_t *ctx_) {
    builder_out_ctx_t *ctx = (builder_out_ctx_t *) ctx_;
    if (_anjay_uri_path_length(&ctx->path) == 0) {
        batch_log(ERROR, _("Path not set"));
        return -1;
    }
    ctx->path = MAKE_ROOT_PATH();
    return 0;
}

static int output_close(anjay_output_ctx_t *ctx_) {
    builder_out_ctx_t *ctx = (builder_out_ctx_t *) ctx_;
    if (ctx->bytes.remaining_bytes) {
        batch_log(ERROR,
                  _("not all declared bytes passed by user, buffer is filled "
                    "with random bytes"));
        return -1;
    } else if (_anjay_uri_path_length(&ctx->path) > 0) {
        batch_log(ERROR, _("set_path() called without returning a value"));
        return ANJAY_OUTCTXERR_ANJAY_RET_NOT_CALLED;
    }
    return 0;
}

static const anjay_output_ctx_vtable_t BUILDER_OUT_VTABLE = {
    .bytes_begin = bytes_begin,
    .string = ret_string,
    .integer = ret_integer,
    .floating = ret_double,
    .boolean = ret_bool,
    .objlnk = ret_objlnk,
    .start_aggregate = ret_start_aggregate,
    .set_path = set_path,
    .clear_path = clear_path,
    .close = output_close
};

static inline builder_out_ctx_t
builder_out_ctx_new(anjay_batch_builder_t *builder,
                    const anjay_uri_path_t *uri) {
    builder_out_ctx_t ctx = {
        .base = {
            .vtable = &BUILDER_OUT_VTABLE
        },
        .builder = builder,
        .bytes = {
            .vtable = &BYTES_VTABLE,
            .remaining_bytes = 0
        },
        .root_path = *uri,
        .path = MAKE_ROOT_PATH()
    };
    return ctx;
}

int _anjay_dm_read_into_batch(anjay_batch_builder_t *builder,
                              anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj,
                              const anjay_dm_path_info_t *path_info,
                              anjay_ssid_t requesting_ssid) {
    assert(builder);
    assert(anjay);
    assert(!obj || path_info->uri.ids[ANJAY_ID_OID] == (*obj)->oid);
    builder_out_ctx_t ctx = builder_out_ctx_new(builder, &path_info->uri);
    int retval = _anjay_dm_read(anjay, obj, path_info, requesting_ssid,
                                (anjay_output_ctx_t *) &ctx);
    int close_retval = output_close((anjay_output_ctx_t *) &ctx);
    return close_retval ? close_retval : retval;
}

static bool is_timestamp_absolute(avs_time_real_t timestamp) {
    /**
     * timestamp.since_real_epoch contatins time measured since reboot if no
     * source of real time is provided. We assume that no device will run for
     * SENML_TIME_SECONDS_THRESHOLD or longer without reboot.
     */
    return timestamp.since_real_epoch.seconds >= SENML_TIME_SECONDS_THRESHOLD;
}

static bool is_timestamp_relative(avs_time_real_t timestamp) {
    return !is_timestamp_absolute(timestamp);
}

static double convert_to_senml_time(avs_time_real_t timestamp,
                                    avs_time_real_t serialization_time) {
    if (avs_time_real_before(serialization_time, timestamp)) {
        batch_log(DEBUG, _("serialization time precedes timestamp, time "
                           "measurement may be corrupted"));
        return NAN;
    }
    if (is_timestamp_absolute(timestamp)
            && is_timestamp_absolute(serialization_time)) {
        return avs_time_real_to_fscalar(timestamp, AVS_TIME_S);
    } else if (is_timestamp_relative(timestamp)
               && is_timestamp_relative(serialization_time)) {
        double result = avs_time_duration_to_fscalar(
                avs_time_real_diff(timestamp, serialization_time), AVS_TIME_S);
        AVS_ASSERT(result <= 0, "relative time must not be positive");
        return result;
    } else {
        batch_log(DEBUG, _("timestamp and serialization time should be both "
                           "absolute or both relative"));
        return NAN;
    }
}

static int serialize_batch_entry(const anjay_batch_entry_t *entry,
                                 avs_time_real_t serialization_time,
                                 anjay_output_ctx_t *output) {
    int result = _anjay_output_set_path(output, &entry->path);
    if (result) {
        return result;
    }
    if (avs_time_real_valid(entry->timestamp)
            && (result = _anjay_output_set_time(
                        output,
                        convert_to_senml_time(entry->timestamp,
                                              serialization_time)))) {
        return result;
    }
    switch (entry->data.type) {
    case ANJAY_BATCH_DATA_BYTES:
        return anjay_ret_bytes(output,
                               entry->data.value.bytes.data,
                               entry->data.value.bytes.length);
    case ANJAY_BATCH_DATA_STRING:
        return anjay_ret_string(output, entry->data.value.string);
    case ANJAY_BATCH_DATA_INT:
        return anjay_ret_i64(output, entry->data.value.int_value);
    case ANJAY_BATCH_DATA_DOUBLE:
        return anjay_ret_double(output, entry->data.value.double_value);
    case ANJAY_BATCH_DATA_BOOL:
        return anjay_ret_bool(output, entry->data.value.bool_value);
    case ANJAY_BATCH_DATA_OBJLNK:
        return anjay_ret_objlnk(output,
                                entry->data.value.objlnk.oid,
                                entry->data.value.objlnk.iid);
    case ANJAY_BATCH_DATA_START_AGGREGATE:
        return _anjay_output_start_aggregate(output);
    default:
        AVS_UNREACHABLE("invalid enum value");
        return -1;
    }
}

static bool is_server_allowed_to_read(anjay_t *anjay,
                                      anjay_oid_t oid,
                                      anjay_iid_t iid,
                                      anjay_ssid_t ssid) {
    const anjay_action_info_t action_info = {
        .oid = oid,
        .iid = iid,
        .ssid = ssid,
        .action = ANJAY_ACTION_READ
    };
    return _anjay_instance_action_allowed(anjay, &action_info);
}

int _anjay_batch_data_output(anjay_t *anjay,
                             const anjay_batch_t *batch,
                             anjay_ssid_t target_ssid,
                             anjay_output_ctx_t *out_ctx) {
    const avs_time_real_t serialization_time = avs_time_real_now();
    const anjay_batch_data_output_state_t *state = NULL;
    int result = 0;
    do {
        result = _anjay_batch_data_output_entry(
                anjay, batch, target_ssid, serialization_time, &state, out_ctx);
    } while (!result && state);
    return result;
}

int _anjay_batch_data_output_entry(
        anjay_t *anjay,
        const anjay_batch_t *batch,
        anjay_ssid_t target_ssid,
        avs_time_real_t serialization_time,
        const anjay_batch_data_output_state_t **state,
        anjay_output_ctx_t *out_ctx) {
    assert(state);
    const AVS_LIST(anjay_batch_entry_t) it;
    if (!*state) {
        it = batch->list;
    } else {
        it = &(*state)->entry;
        assert(AVS_LIST_FIND_PTR(&batch->list, it) != NULL);
    }
    while (it
           && !is_server_allowed_to_read(anjay, it->path.ids[ANJAY_ID_OID],
                                         it->path.ids[ANJAY_ID_IID],
                                         target_ssid)) {
        AVS_LIST_ADVANCE(&it);
    }
    int result = 0;
    if (it) {
        result = serialize_batch_entry(it, serialization_time, out_ctx);
        AVS_LIST_ADVANCE(&it);
    }
    *state = AVS_CONTAINER_OF(it, anjay_batch_data_output_state_t, entry);
    return result;
}

static bool batch_data_equal(const anjay_batch_data_t *a,
                             const anjay_batch_data_t *b) {
    if (a->type != b->type) {
        return false;
    }
    switch (a->type) {
    case ANJAY_BATCH_DATA_BYTES:
        return a->value.bytes.length == b->value.bytes.length
               && !memcmp(a->value.bytes.data,
                          b->value.bytes.data,
                          a->value.bytes.length);
    case ANJAY_BATCH_DATA_STRING:
        return !strcmp(a->value.string, b->value.string);
    case ANJAY_BATCH_DATA_INT:
        return a->value.int_value == b->value.int_value;
    case ANJAY_BATCH_DATA_DOUBLE:
        return a->value.double_value == b->value.double_value;
    case ANJAY_BATCH_DATA_BOOL:
        return a->value.bool_value == b->value.bool_value;
    case ANJAY_BATCH_DATA_OBJLNK:
        return a->value.objlnk.oid == b->value.objlnk.oid
               && a->value.objlnk.iid == b->value.objlnk.iid;
    case ANJAY_BATCH_DATA_START_AGGREGATE:;
    }
    return true;
}

bool _anjay_batch_values_equal(const anjay_batch_t *a, const anjay_batch_t *b) {
    if (!a || !b) {
        return !a && !b;
    }
    AVS_LIST(anjay_batch_entry_t) ait = a->list;
    AVS_LIST(anjay_batch_entry_t) bit = b->list;
    while (ait && bit) {
        if (!_anjay_uri_path_equal(&ait->path, &bit->path)
                || !batch_data_equal(&ait->data, &bit->data)) {
            return false;
        }
        AVS_LIST_ADVANCE(&ait);
        AVS_LIST_ADVANCE(&bit);
    }
    return !ait && !bit;
}

bool _anjay_batch_data_requires_hierarchical_format(
        const anjay_batch_t *batch) {
    if (!batch || !batch->list || AVS_LIST_NEXT(batch->list)) {
        // entry list is not exactly 1 element long
        return true;
    }
    const anjay_batch_entry_t *const entry = batch->list;
    if (entry->data.type == ANJAY_BATCH_DATA_START_AGGREGATE) {
        // batch consists of an empty aggregate, so isn't a single simple value
        return true;
    }
    assert(_anjay_uri_path_has(&entry->path, ANJAY_ID_RID));
    return false;
}

double _anjay_batch_data_numeric_value(const anjay_batch_t *batch) {
    if (_anjay_batch_data_requires_hierarchical_format(batch)) {
        // not a simple value
        return NAN;
    }
    const anjay_batch_entry_t *const entry = batch->list;
    switch (entry->data.type) {
    case ANJAY_BATCH_DATA_INT:
        return (double) entry->data.value.int_value;
    case ANJAY_BATCH_DATA_DOUBLE:
        return entry->data.value.double_value;
    default:
        return NAN;
    }
}

avs_time_real_t _anjay_batch_get_compilation_time(const anjay_batch_t *batch) {
    return batch->compilation_time;
}

#    ifdef ANJAY_TEST
#        include "tests/core/io/batch_builder.c"
#    endif

#endif // defined(ANJAY_WITH_OBSERVE) || defined(ANJAY_WITH_SEND)
