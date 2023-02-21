/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#if defined(ANJAY_WITH_CBOR) || defined(ANJAY_WITH_SENML_JSON)

#    include <avsystem/commons/avs_base64.h>
#    include <avsystem/commons/avs_stream_membuf.h>
#    include <avsystem/commons/avs_stream_v_table.h>
#    include <avsystem/commons/avs_utils.h>

#    include "../anjay_utils_private.h"

#    ifdef ANJAY_WITH_CBOR
#        include "cbor/anjay_json_like_cbor_decoder.h"
#    endif // ANJAY_WITH_CBOR

#    ifdef ANJAY_WITH_SENML_JSON
#        include "json/anjay_json_decoder.h"
#    endif // ANJAY_WITH_SENML_JSON

#    include "anjay_common.h"
#    include "anjay_vtable.h"

#    include <errno.h>
#    include <math.h>

VISIBILITY_SOURCE_BEGIN

#    define LOG(...) _anjay_log(senml_in, __VA_ARGS__)

typedef struct senml_in senml_in_t;

typedef int get_senml_label_t(senml_in_t *in, senml_label_t *out_label);

typedef int parse_opaque_value_t(senml_in_t *in);

typedef struct {
    get_senml_label_t *get_senml_label;
    parse_opaque_value_t *parse_opaque_value;
} senml_deserialization_vtable_t;

typedef struct {
    /* NOTE: empty state of cached entry is represented by 0-length path */
    char path[MAX_PATH_STRING_SIZE];
    anjay_json_like_value_type_t type;
    union {
        bool boolean;
        anjay_json_like_number_t number;
        struct {
            void *data;
            size_t size;
            size_t bytes_read;
        } bytes;
    } value;
} senml_cached_entry_t;

static void cached_entry_reset(senml_cached_entry_t *entry) {
    if (!*entry->path) {
        return;
    }
    if (entry->type == ANJAY_JSON_LIKE_VALUE_TEXT_STRING
            || entry->type == ANJAY_JSON_LIKE_VALUE_BYTE_STRING) {
        avs_free(entry->value.bytes.data);
    }
    memset(entry, 0, sizeof(*entry));
}

struct senml_in {
    const anjay_input_ctx_vtable_t *input_ctx_vtable;
    const senml_deserialization_vtable_t *deserialization_vtable;
    anjay_json_like_decoder_t *ctx;

    bool composite_read;

    /* Currently processed entry - shared between entire context chain. */
    senml_cached_entry_t *entry;
    /* Set to true if the value associated with an entry has been read. */
    bool value_read;
    /* Current basename set in the payload. */
    char basename[MAX_PATH_STRING_SIZE];
    /* A path which must be a prefix of the currently processed `path`. */
    anjay_uri_path_t base;

    /* Currently processed path. */
    anjay_uri_path_t path;
};

static int get_i64(senml_in_t *in, int64_t *out_value) {
    anjay_json_like_number_t value;
    if (_anjay_json_like_decoder_number(in->ctx, &value)) {
        return -1;
    }
    return _anjay_json_like_decoder_get_i64_from_number(&value, out_value);
}

static int get_short_string(senml_in_t *in, char *out_string, size_t size) {
    assert(size > 0);
    avs_stream_outbuf_t stream = AVS_STREAM_OUTBUF_STATIC_INITIALIZER;
    avs_stream_outbuf_set_buffer(&stream, out_string, size - 1);
    if (_anjay_json_like_decoder_bytes(in->ctx, (avs_stream_t *) &stream)) {
        return -1;
    }
    out_string[avs_stream_outbuf_offset(&stream)] = '\0';
    return 0;
}

static bool can_return_value(senml_in_t *in) {
    return in->path.ids[0] != ANJAY_ID_INVALID && !in->value_read;
}

static int read_some_cached_bytes(senml_in_t *in,
                                  anjay_json_like_value_type_t bytes_type,
                                  void *out_buf,
                                  size_t *inout_size,
                                  bool *out_message_finished) {
    assert(bytes_type == ANJAY_JSON_LIKE_VALUE_BYTE_STRING
           || bytes_type == ANJAY_JSON_LIKE_VALUE_TEXT_STRING);

    if (!can_return_value(in)) {
        return -1;
    }
    if (in->entry->type != bytes_type) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    const size_t bytes_left =
            in->entry->value.bytes.size - in->entry->value.bytes.bytes_read;
    const size_t bytes_to_write = AVS_MIN(*inout_size, bytes_left);
    memcpy(out_buf,
           &((const uint8_t *) in->entry->value.bytes
                     .data)[in->entry->value.bytes.bytes_read],
           bytes_to_write);
    in->entry->value.bytes.bytes_read += bytes_to_write;
    *inout_size = bytes_to_write;

    const bool finished =
            (in->entry->value.bytes.bytes_read == in->entry->value.bytes.size);
    if (out_message_finished) {
        *out_message_finished = finished;
    }
    if (!finished) {
        return ANJAY_BUFFER_TOO_SHORT;
    }
    in->value_read = true;
    return 0;
}

static int senml_get_some_bytes(anjay_unlocked_input_ctx_t *ctx,
                                size_t *out_bytes_read,
                                bool *out_message_finished,
                                void *out_buf,
                                size_t buf_size) {
    senml_in_t *in = (senml_in_t *) ctx;
    int retval = read_some_cached_bytes(in,
                                        ANJAY_JSON_LIKE_VALUE_BYTE_STRING,
                                        out_buf,
                                        &buf_size,
                                        out_message_finished);
    *out_bytes_read = buf_size;
    if (retval == ANJAY_BUFFER_TOO_SHORT) {
        return 0;
    }
    return retval;
}

static int senml_get_string(anjay_unlocked_input_ctx_t *ctx,
                            char *out_buf,
                            size_t buf_size) {
    assert(buf_size);
    senml_in_t *in = (senml_in_t *) ctx;

    /* make space for null terminator */
    --buf_size;
    int retval = read_some_cached_bytes(in, ANJAY_JSON_LIKE_VALUE_TEXT_STRING,
                                        out_buf, &buf_size, NULL);
    out_buf[buf_size] = '\0';
    return retval;
}

static bool is_type_numeric(anjay_json_like_value_type_t type) {
    switch (type) {
    case ANJAY_JSON_LIKE_VALUE_UINT:
    case ANJAY_JSON_LIKE_VALUE_NEGATIVE_INT:
    case ANJAY_JSON_LIKE_VALUE_FLOAT:
    case ANJAY_JSON_LIKE_VALUE_DOUBLE:
        return true;
    default:
        return false;
    }
}

static int senml_get_integer(anjay_unlocked_input_ctx_t *ctx,
                             int64_t *out_value) {
    senml_in_t *in = (senml_in_t *) ctx;
    if (!can_return_value(in)) {
        return -1;
    }

    if (!is_type_numeric(in->entry->type)
            || _anjay_json_like_decoder_get_i64_from_number(
                       &in->entry->value.number, out_value)) {
        return ANJAY_ERR_BAD_REQUEST;
    }

    in->value_read = true;
    return 0;
}

static int senml_get_uint(anjay_unlocked_input_ctx_t *ctx,
                          uint64_t *out_value) {
    senml_in_t *in = (senml_in_t *) ctx;
    if (!can_return_value(in)) {
        return -1;
    }

    if (!is_type_numeric(in->entry->type)
            || _anjay_json_like_decoder_get_u64_from_number(
                       &in->entry->value.number, out_value)) {
        return ANJAY_ERR_BAD_REQUEST;
    }

    in->value_read = true;
    return 0;
}

static int senml_get_double(anjay_unlocked_input_ctx_t *ctx,
                            double *out_value) {
    senml_in_t *in = (senml_in_t *) ctx;
    if (!can_return_value(in)) {
        return -1;
    }

    if (!is_type_numeric(in->entry->type)
            || _anjay_json_like_decoder_get_double_from_number(
                       &in->entry->value.number, out_value)) {
        return ANJAY_ERR_BAD_REQUEST;
    }

    in->value_read = true;
    return 0;
}

static int senml_get_bool(anjay_unlocked_input_ctx_t *ctx, bool *out_value) {
    senml_in_t *in = (senml_in_t *) ctx;

    if (!can_return_value(in)) {
        return -1;
    }
    if (in->entry->type != ANJAY_JSON_LIKE_VALUE_BOOL) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    *out_value = in->entry->value.boolean;
    in->value_read = true;
    return 0;
}

static int senml_get_objlnk(anjay_unlocked_input_ctx_t *ctx,
                            anjay_oid_t *out_oid,
                            anjay_iid_t *out_iid) {
    senml_in_t *in = (senml_in_t *) ctx;
    if (in->entry->type != ANJAY_JSON_LIKE_VALUE_TEXT_STRING) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    char objlnk[MAX_OBJLNK_STRING_SIZE];
    if (_anjay_get_string_unlocked(ctx, objlnk, sizeof(objlnk))) {
        return ANJAY_ERR_BAD_REQUEST;
    }

    if (_anjay_io_parse_objlnk(objlnk, out_oid, out_iid)) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    return 0;
}

static int parse_id(uint16_t *out_id, const char **id_begin) {
    errno = 0;
    char *endptr = NULL;
    long value = strtol(*id_begin, &endptr, 10);
    // clang-format off
    if (errno == ERANGE
            || endptr == *id_begin
            || value < 0
            || value >= ANJAY_ID_INVALID) {
        return -1;
    }
    // clang-format on
    *id_begin = endptr;
    *out_id = (uint16_t) value;
    return 0;
}

static int parse_absolute_path(anjay_uri_path_t *out_path, const char *input) {
    if (!*input) {
        return -1;
    }
    *out_path = MAKE_ROOT_PATH();

    if (!strcmp(input, "/")) {
        return 0;
    }
    size_t curr_len = 0;
    for (const char *ch = input; *ch;) {
        if (*ch++ != '/') {
            return -1;
        }
        if (curr_len >= AVS_ARRAY_SIZE(out_path->ids)) {
            LOG(DEBUG, _("absolute path is too long"));
            return -1;
        }
        if (parse_id(&out_path->ids[curr_len], &ch)) {
            return -1;
        }
        curr_len++;
    }
    return 0;
}

static int parse_next_absolute_path(senml_in_t *in) {
    char full_path[MAX_PATH_STRING_SIZE];
    if (avs_simple_snprintf(full_path,
                            sizeof(full_path),
                            "%s%s",
                            in->basename,
                            in->entry->path)
            < 0) {
        LOG(DEBUG, _("basename + path is longer than a maximum path length"));
        return ANJAY_ERR_BAD_REQUEST;
    }
    if (parse_absolute_path(&in->path, full_path)) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    if (_anjay_uri_path_outside_base(&in->path, &in->base)) {
        LOG(LAZY_DEBUG,
            _("parsed path ") "%s" _(" would be outside of uri-path ") "%s",
            ANJAY_DEBUG_MAKE_PATH(&in->path), ANJAY_DEBUG_MAKE_PATH(&in->base));
        return ANJAY_ERR_BAD_REQUEST;
    }
    if (!in->composite_read && !_anjay_uri_path_has(&in->path, ANJAY_ID_RID)) {
        LOG(LAZY_DEBUG,
            _("path ") "%s" _(" inappropriate for this context, Resource or "
                              "Resource Instance path expected"),
            ANJAY_DEBUG_MAKE_PATH(&in->path));
        return ANJAY_ERR_BAD_REQUEST;
    }

    return 0;
}

static int parse_senml_name(senml_in_t *in, bool *has_name) {
    if (*has_name) {
        LOG(DEBUG, _("duplicated SenML Name in entry"));
        return -1;
    }
    *has_name = true;

    char in_path[MAX_PATH_STRING_SIZE];
    if (get_short_string(in, in_path, sizeof(in_path))) {
        return -1;
    }
    const size_t len = strlen(in->entry->path);
    if (avs_simple_snprintf(in->entry->path + len,
                            sizeof(in->entry->path) - len,
                            "%s",
                            in_path)
            < 0) {
        return -1;
    }
    return 0;
}

static int read_all_bytes(anjay_json_like_decoder_t *ctx,
                          void **out_data,
                          size_t *out_size) {
    avs_stream_t *membuf = avs_stream_membuf_create();
    if (!membuf) {
        LOG(DEBUG, _("could not allocate membuf for value cache"));
        return -1;
    }
    int result = _anjay_json_like_decoder_bytes(ctx, membuf);
    if (!result
            && avs_is_err(avs_stream_membuf_take_ownership(membuf, out_data,
                                                           out_size))) {
        result = -1;
    }
    avs_stream_cleanup(&membuf);
    return result;
}

static int
parse_senml_value(senml_in_t *in, bool *has_value, senml_label_t label) {
    if (*has_value) {
        LOG(DEBUG, _("duplicated SenML value type in entry"));
        return -1;
    }
    *has_value = true;

    if (_anjay_json_like_decoder_current_value_type(in->ctx,
                                                    &in->entry->type)) {
        return -1;
    }
    if (label == SENML_LABEL_VALUE_OPAQUE) {
        return in->deserialization_vtable->parse_opaque_value(in);
    }
    switch (in->entry->type) {
    case ANJAY_JSON_LIKE_VALUE_BYTE_STRING:
        return ANJAY_ERR_BAD_REQUEST;
    case ANJAY_JSON_LIKE_VALUE_TEXT_STRING:
        if (label != SENML_LABEL_VALUE_STRING
                && label != SENML_EXT_LABEL_OBJLNK) {
            return ANJAY_ERR_BAD_REQUEST;
        }
        return read_all_bytes(in->ctx, &in->entry->value.bytes.data,
                              &in->entry->value.bytes.size);
    case ANJAY_JSON_LIKE_VALUE_BOOL:
        if (label != SENML_LABEL_VALUE_BOOL) {
            return ANJAY_ERR_BAD_REQUEST;
        }
        return _anjay_json_like_decoder_bool(in->ctx,
                                             &in->entry->value.boolean);
    default:
        if (label != SENML_LABEL_VALUE) {
            return ANJAY_ERR_BAD_REQUEST;
        }
        return _anjay_json_like_decoder_number(in->ctx,
                                               &in->entry->value.number);
    }
}

static int parse_senml_basename(senml_in_t *in, bool *has_basename) {
    if (*has_basename) {
        LOG(DEBUG, _("duplicated SenML Base Name in entry"));
        return -1;
    }
    *has_basename = true;
    if (get_short_string(in, in->basename, sizeof(in->basename))) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    return 0;
}

static int parse_next_entry(senml_in_t *in) {
    if (_anjay_json_like_decoder_state(in->ctx)
            == ANJAY_JSON_LIKE_DECODER_STATE_FINISHED) {
        return ANJAY_GET_PATH_END;
    }

    cached_entry_reset(in->entry);

    senml_label_t senml_label;
    size_t outer_level = _anjay_json_like_decoder_nesting_level(in->ctx);
    if (_anjay_json_like_decoder_enter_map(in->ctx)) {
        return ANJAY_ERR_BAD_REQUEST;
    }

    bool has_name = false;
    bool has_value = false;
    bool has_basename = false;
    while (_anjay_json_like_decoder_nesting_level(in->ctx) >= outer_level + 1) {
        if (in->deserialization_vtable->get_senml_label(in, &senml_label)) {
            return ANJAY_ERR_BAD_REQUEST;
        }
        int result = 0;
        switch (senml_label) {
        case SENML_LABEL_NAME:
            result = parse_senml_name(in, &has_name);
            break;
        case SENML_LABEL_VALUE:
        case SENML_LABEL_VALUE_BOOL:
        case SENML_LABEL_VALUE_OPAQUE:
        case SENML_LABEL_VALUE_STRING:
        case SENML_EXT_LABEL_OBJLNK:
            if (in->composite_read) {
                LOG(DEBUG, _("unexpected value in SenML payload"));
                result = ANJAY_ERR_BAD_REQUEST;
            } else {
                result = parse_senml_value(in, &has_value, senml_label);
            }
            break;
        case SENML_LABEL_BASE_NAME:
            result = parse_senml_basename(in, &has_basename);
            break;
        default:
            LOG(DEBUG,
                _("unsupported entry SenML Label ") "%d" _(" - ignoring"),
                (int) senml_label);
            break;
        }
        if (result) {
            return ANJAY_ERR_BAD_REQUEST;
        }
    }
    if (_anjay_json_like_decoder_state(in->ctx)
            == ANJAY_JSON_LIKE_DECODER_STATE_ERROR) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    return parse_next_absolute_path(in);
}

static int senml_get_path(anjay_unlocked_input_ctx_t *ctx,
                          anjay_uri_path_t *out_path,
                          bool *out_is_array) {
    senml_in_t *in = (senml_in_t *) ctx;
    if (in->path.ids[0] == ANJAY_ID_INVALID) {
        int retval = parse_next_entry(in);
        if (retval) {
            return retval;
        }
    }
    *out_path = in->path;
    /**
     * This is never true, because there is no way we are able to figure out
     * that a path /OID/IID/RID is path to a Multiple Resource - it is simply
     * non-representable in SenML.
     */
    *out_is_array = false;
    return 0;
}

static int senml_next_entry(anjay_unlocked_input_ctx_t *ctx) {
    senml_in_t *in = (senml_in_t *) ctx;
    if (in->path.ids[0] == ANJAY_ID_INVALID) {
        return 0;
    }
    in->path = MAKE_ROOT_PATH();
    cached_entry_reset(in->entry);
    in->value_read = false;
    return 0;
}

static int senml_close(anjay_unlocked_input_ctx_t *ctx) {
    senml_in_t *in = (senml_in_t *) ctx;

    int result = 0;
    if (_anjay_json_like_decoder_state(in->ctx)
            != ANJAY_JSON_LIKE_DECODER_STATE_FINISHED) {
        LOG(WARNING, _("SenML payload contains extraneous data"));
        result = ANJAY_ERR_BAD_REQUEST;
    }

    cached_entry_reset(in->entry);
    avs_free(in->entry);
    _anjay_json_like_decoder_delete(&in->ctx);
    return result;
}

static const anjay_input_ctx_vtable_t SENML_IN_VTABLE = {
    .some_bytes = senml_get_some_bytes,
    .string = senml_get_string,
    .integer = senml_get_integer,
    .uint = senml_get_uint,
    .floating = senml_get_double,
    .boolean = senml_get_bool,
    .objlnk = senml_get_objlnk,
    .get_path = senml_get_path,
    .next_entry = senml_next_entry,
    .close = senml_close
};

static int
input_senml_create(anjay_unlocked_input_ctx_t **out,
                   anjay_json_like_decoder_t *ctx,
                   const anjay_uri_path_t *request_uri,
                   const senml_deserialization_vtable_t *deserialization_vtable,
                   bool composite_read) {
    int retval = -1;
    senml_in_t *in = NULL;
    assert(_anjay_json_like_decoder_nesting_level(ctx) == 0);
    if (_anjay_json_like_decoder_enter_array(ctx)) {
        retval = ANJAY_ERR_BAD_REQUEST;
        goto error;
    }
    in = (senml_in_t *) avs_calloc(1, sizeof(senml_in_t));
    if (!in) {
        goto error;
    }
    in->entry =
            (senml_cached_entry_t *) avs_calloc(1,
                                                sizeof(senml_cached_entry_t));
    if (!in->entry) {
        goto error;
    }
    in->input_ctx_vtable = &SENML_IN_VTABLE;
    in->deserialization_vtable = deserialization_vtable;
    in->ctx = ctx;
    in->base = *request_uri;
    in->path = MAKE_ROOT_PATH();
    in->composite_read = composite_read;
    *out = (anjay_unlocked_input_ctx_t *) in;
    return 0;

error:
    _anjay_json_like_decoder_delete(&ctx);
    avs_free(in);
    return retval;
}

#    ifdef ANJAY_WITH_CBOR
static int get_senml_cbor_label(senml_in_t *in, senml_label_t *out_label) {
    anjay_json_like_value_type_t type;
    if (_anjay_json_like_decoder_current_value_type(in->ctx, &type)) {
        return -1;
    }
    /**
     * SenML numerical labels do not contain anything related to LwM2M objlnk
     * datatype. Additionally:
     *
     * > 6.  CBOR Representation (application/senml+cbor)
     * > [...]
     * >
     * > For compactness, the CBOR representation uses integers for the
     * > labels, as defined in Table 4.  This table is conclusive, i.e.,
     * > there is no intention to define any additional integer map keys;
     * > any extensions will use **string** map keys.
     */
    if (type == ANJAY_JSON_LIKE_VALUE_TEXT_STRING) {
        char label[sizeof(SENML_EXT_OBJLNK_REPR)];
        if (get_short_string(in, label, sizeof(label))
                || strcmp(label, SENML_EXT_OBJLNK_REPR)) {
            return -1;
        }
        *out_label = SENML_EXT_LABEL_OBJLNK;
        return 0;
    }
    int64_t numeric_label;
    if (get_i64(in, &numeric_label)) {
        return -1;
    }
    switch (numeric_label) {
    case SENML_LABEL_BASE_TIME:
    case SENML_LABEL_BASE_NAME:
    case SENML_LABEL_NAME:
    case SENML_LABEL_VALUE:
    case SENML_LABEL_VALUE_STRING:
    case SENML_LABEL_VALUE_BOOL:
    case SENML_LABEL_TIME:
    case SENML_LABEL_VALUE_OPAQUE:
        *out_label = (senml_label_t) numeric_label;
        return 0;
    default:
        return -1;
    }
}

static int parse_cbor_opaque_value(senml_in_t *in) {
    if (in->entry->type != ANJAY_JSON_LIKE_VALUE_BYTE_STRING) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    return read_all_bytes(in->ctx, &in->entry->value.bytes.data,
                          &in->entry->value.bytes.size);
}

static const senml_deserialization_vtable_t
        SENML_CBOR_DESERIALIZATION_VTABLE = {
            .get_senml_label = get_senml_cbor_label,
            .parse_opaque_value = parse_cbor_opaque_value
        };

int _anjay_input_senml_cbor_create(anjay_unlocked_input_ctx_t **out,
                                   avs_stream_t *stream_ptr,
                                   const anjay_uri_path_t *request_uri) {
    anjay_json_like_decoder_t *cbor_ctx =
            _anjay_cbor_decoder_new(stream_ptr, MAX_SENML_CBOR_NEST_STACK_SIZE);
    if (!cbor_ctx) {
        return -1;
    }
    return input_senml_create(out, cbor_ctx, request_uri,
                              &SENML_CBOR_DESERIALIZATION_VTABLE, false);
}

int _anjay_input_senml_cbor_composite_read_create(
        anjay_unlocked_input_ctx_t **out,
        avs_stream_t *stream_ptr,
        const anjay_uri_path_t *request_uri) {
    anjay_json_like_decoder_t *cbor_ctx =
            _anjay_cbor_decoder_new(stream_ptr, MAX_SENML_CBOR_NEST_STACK_SIZE);
    if (!cbor_ctx) {
        return -1;
    }
    return input_senml_create(out, cbor_ctx, request_uri,
                              &SENML_CBOR_DESERIALIZATION_VTABLE, true);
}
#    endif // ANJAY_WITH_CBOR

#    ifdef ANJAY_WITH_SENML_JSON
static int get_senml_json_label(senml_in_t *in, senml_label_t *out_label) {
    anjay_json_like_value_type_t type;
    if (_anjay_json_like_decoder_current_value_type(in->ctx, &type)) {
        return -1;
    }
    char label[sizeof(SENML_EXT_OBJLNK_REPR)];
    if (type != ANJAY_JSON_LIKE_VALUE_TEXT_STRING
            || get_short_string(in, label, sizeof(label))) {
        return -1;
    }
    if (strcmp(label, SENML_EXT_OBJLNK_REPR) == 0) {
        *out_label = SENML_EXT_LABEL_OBJLNK;
    } else if (strcmp(label, "bt") == 0) {
        *out_label = SENML_LABEL_BASE_TIME;
    } else if (strcmp(label, "bn") == 0) {
        *out_label = SENML_LABEL_BASE_NAME;
    } else if (strcmp(label, "n") == 0) {
        *out_label = SENML_LABEL_NAME;
    } else if (strcmp(label, "v") == 0) {
        *out_label = SENML_LABEL_VALUE;
    } else if (strcmp(label, "vs") == 0) {
        *out_label = SENML_LABEL_VALUE_STRING;
    } else if (strcmp(label, "vb") == 0) {
        *out_label = SENML_LABEL_VALUE_BOOL;
    } else if (strcmp(label, "t") == 0) {
        *out_label = SENML_LABEL_TIME;
    } else if (strcmp(label, "vd") == 0) {
        *out_label = SENML_LABEL_VALUE_OPAQUE;
    } else {
        return -1;
    }
    return 0;
}

typedef struct {
    const avs_stream_v_table_t *const vtable;
    avs_stream_t *backend;
    char buffer[5]; // 4 bytes + null terminator
    size_t buffer_pos;
} base64_stream_wrapper_t;

static avs_error_t base64_flush(base64_stream_wrapper_t *stream) {
    static const avs_base64_config_t BASE64_CONFIG = {
        .alphabet = AVS_BASE64_URL_SAFE_CHARS,
        .padding_char = '\0',
        .allow_whitespace = false,
        .require_padding = false
    };
    if (stream->buffer_pos == 0) {
        return AVS_OK;
    }
    assert(stream->buffer_pos < sizeof(stream->buffer));
    stream->buffer[stream->buffer_pos] = '\0';
    size_t decoded;
    if (avs_base64_decode_custom(&decoded, (uint8_t *) stream->buffer,
                                 sizeof(stream->buffer) - 1, stream->buffer,
                                 BASE64_CONFIG)) {
        return avs_errno(AVS_EBADMSG);
    }
    stream->buffer_pos = 0;
    return avs_stream_write(stream->backend, stream->buffer, decoded);
}

static avs_error_t base64_write_some(avs_stream_t *stream_,
                                     const void *buffer_,
                                     size_t *inout_data_length) {
    base64_stream_wrapper_t *stream = (base64_stream_wrapper_t *) stream_;
    const char *buffer = (const char *) buffer_;
    size_t bytes_remaining = *inout_data_length;
    while (bytes_remaining) {
        size_t bytes_to_process =
                AVS_MIN(bytes_remaining,
                        sizeof(stream->buffer) - 1 - stream->buffer_pos);
        bytes_remaining -= bytes_to_process;
        memcpy(stream->buffer + stream->buffer_pos, buffer, bytes_to_process);
        buffer += bytes_to_process;
        stream->buffer_pos += bytes_to_process;
        if (stream->buffer_pos == sizeof(stream->buffer) - 1) {
            avs_error_t err = base64_flush(stream);
            if (avs_is_err(err)) {
                return err;
            }
        }
    }
    return AVS_OK;
}

static const avs_stream_v_table_t BASE64_STREAM_WRAPPER_VTABLE = {
    .write_some = base64_write_some
};

static int parse_json_opaque_value(senml_in_t *in) {
    if (in->entry->type != ANJAY_JSON_LIKE_VALUE_TEXT_STRING) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    base64_stream_wrapper_t stream = {
        .vtable = &BASE64_STREAM_WRAPPER_VTABLE,
        .backend = avs_stream_membuf_create()
    };
    if (!stream.backend) {
        LOG(DEBUG, _("could not allocate membuf for value cache"));
        return -1;
    }
    int result = 0;
    if (_anjay_json_like_decoder_bytes(in->ctx, (avs_stream_t *) &stream)
            || avs_is_err(base64_flush(&stream))
            || avs_is_err(avs_stream_membuf_take_ownership(
                       stream.backend, &in->entry->value.bytes.data,
                       &in->entry->value.bytes.size))) {
        result = -1;
    }
    avs_stream_cleanup(&stream.backend);
    if (!result) {
        in->entry->type = ANJAY_JSON_LIKE_VALUE_BYTE_STRING;
    }
    return result;
}

static const senml_deserialization_vtable_t
        SENML_JSON_DESERIALIZATION_VTABLE = {
            .get_senml_label = get_senml_json_label,
            .parse_opaque_value = parse_json_opaque_value
        };

int _anjay_input_json_create(anjay_unlocked_input_ctx_t **out,
                             avs_stream_t *stream_ptr,
                             const anjay_uri_path_t *request_uri) {
    anjay_json_like_decoder_t *json_ctx = _anjay_json_decoder_new(stream_ptr);
    if (!json_ctx) {
        return -1;
    }
    return input_senml_create(out, json_ctx, request_uri,
                              &SENML_JSON_DESERIALIZATION_VTABLE, false);
}

int _anjay_input_json_composite_read_create(
        anjay_unlocked_input_ctx_t **out,
        avs_stream_t *stream_ptr,
        const anjay_uri_path_t *request_uri) {
    anjay_json_like_decoder_t *json_ctx = _anjay_json_decoder_new(stream_ptr);
    if (!json_ctx) {
        return -1;
    }
    return input_senml_create(out, json_ctx, request_uri,
                              &SENML_JSON_DESERIALIZATION_VTABLE, true);
}
#    endif // ANJAY_WITH_SENML_JSON

#    ifdef ANJAY_WITH_CBOR
#        ifdef ANJAY_TEST
#            include "tests/core/io/cbor_in.c"
#        endif // ANJAY_TEST
#    endif     // ANJAY_WITH_CBOR

#    ifdef ANJAY_WITH_SENML_JSON
#        ifdef ANJAY_TEST
#            include "tests/core/io/json_in.c"
#        endif // ANJAY_TEST
#    endif     // ANJAY_WITH_SENML_JSON

#endif // defined(ANJAY_WITH_CBOR) || defined(ANJAY_WITH_SENML_JSON)
