/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <fluf/fluf_config.h>

#include <ctype.h>
#include <errno.h>
#include <string.h>

#include <fluf/fluf_utils.h>

#include "fluf_cbor_decoder.h"
#include "fluf_internal.h"

#ifdef FLUF_WITH_SENML_CBOR

static int ensure_in_toplevel_array(fluf_io_in_ctx_t *ctx) {
    if (ctx->_decoder._senml_cbor.toplevel_array_entered) {
        return 0;
    }
    int result = fluf_cbor_ll_decoder_enter_array(
            &ctx->_decoder._senml_cbor.ctx,
            &ctx->_decoder._senml_cbor.entry_count);
    if (!result) {
        ctx->_decoder._senml_cbor.toplevel_array_entered = true;
    }
    return result;
}

static int get_i64(fluf_io_in_ctx_t *ctx, int64_t *out_value) {
    fluf_cbor_ll_number_t value;
    int result =
            fluf_cbor_ll_decoder_number(&ctx->_decoder._senml_cbor.ctx, &value);
    if (result) {
        return result;
    }
    return _fluf_cbor_get_i64_from_ll_number(&value, out_value, false);
}

static int
get_short_string(fluf_io_in_ctx_t *ctx, char *out_string, size_t size) {
    return _fluf_cbor_get_short_string(
            &ctx->_decoder._senml_cbor.ctx,
            &ctx->_decoder._senml_cbor.entry_parse.bytes_ctx,
            &ctx->_decoder._senml_cbor.entry_parse.bytes_consumed, out_string,
            size);
}

static int get_senml_cbor_label(fluf_io_in_ctx_t *ctx) {
    fluf_internal_senml_entry_parse_state_t *const state =
            &ctx->_decoder._senml_cbor.entry_parse;
    fluf_cbor_ll_value_type_t type;
    int result = fluf_cbor_ll_decoder_current_value_type(
            &ctx->_decoder._senml_cbor.ctx, &type);
    if (result) {
        return result;
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
    if (type == FLUF_CBOR_LL_VALUE_TEXT_STRING) {
        if ((result = get_short_string(ctx, state->short_string_buf,
                                       sizeof(state->short_string_buf)))) {
            return result;
        }
        if (strcmp(state->short_string_buf, SENML_EXT_OBJLNK_REPR)) {
            return FLUF_IO_ERR_FORMAT;
        }
        state->label = SENML_EXT_LABEL_OBJLNK;
        return 0;
    }
    int64_t numeric_label;
    if ((result = get_i64(ctx, &numeric_label))) {
        return result;
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
        state->label = (senml_label_t) numeric_label;
        return 0;
    default:
        return FLUF_IO_ERR_FORMAT;
    }
}

static int parse_id(uint16_t *out_id, const char **id_begin) {
    const char *id_end = *id_begin;
    while (isdigit(*id_end)) {
        ++id_end;
    }
    uint32_t value;
    int result = fluf_string_to_uint32_value(&value, *id_begin,
                                             (size_t) (id_end - *id_begin));
    if (result) {
        return result;
    }
    if (value >= FLUF_ID_INVALID) {
        return -1;
    }
    *out_id = (uint16_t) value;
    *id_begin = id_end;
    return 0;
}

static int parse_absolute_path(fluf_uri_path_t *out_path, const char *input) {
    if (!*input) {
        return FLUF_IO_ERR_FORMAT;
    }
    *out_path = FLUF_MAKE_ROOT_PATH();

    if (!strcmp(input, "/")) {
        return 0;
    }
    for (const char *ch = input; *ch;) {
        if (*ch++ != '/') {
            return FLUF_IO_ERR_FORMAT;
        }
        if (out_path->uri_len >= AVS_ARRAY_SIZE(out_path->ids)) {
            return FLUF_IO_ERR_FORMAT;
        }
        if (parse_id(&out_path->ids[out_path->uri_len], &ch)) {
            return FLUF_IO_ERR_FORMAT;
        }
        out_path->uri_len++;
    }
    return 0;
}

static int parse_next_absolute_path(fluf_io_in_ctx_t *ctx) {
    fluf_internal_senml_cbor_decoder_t *const senml =
            &ctx->_decoder._senml_cbor;
    char full_path[_FLUF_IO_MAX_PATH_STRING_SIZE];
    size_t len1 = strlen(senml->basename);
    size_t len2 = strlen(senml->entry.path);
    if (len1 + len2 >= sizeof(full_path)) {
        return FLUF_IO_ERR_FORMAT;
    }
    memcpy(full_path, senml->basename, len1);
    memcpy(full_path + len1, senml->entry.path, len2 + 1);
    if (parse_absolute_path(&ctx->_out_path, full_path)
            || fluf_uri_path_outside_base(&ctx->_out_path, &senml->base)
            || (!senml->composite_read
                && !fluf_uri_path_has(&ctx->_out_path, FLUF_ID_RID))) {
        return FLUF_IO_ERR_FORMAT;
    }
    return 0;
}

static int parse_senml_name(fluf_io_in_ctx_t *ctx) {
    fluf_internal_senml_cbor_decoder_t *const senml =
            &ctx->_decoder._senml_cbor;
    if (senml->entry_parse.has_name) {
        return FLUF_IO_ERR_FORMAT;
    }

    fluf_cbor_ll_value_type_t type;
    int result = fluf_cbor_ll_decoder_current_value_type(&senml->ctx, &type);
    if (result) {
        return result;
    }
    if (type != FLUF_CBOR_LL_VALUE_TEXT_STRING) {
        return FLUF_IO_ERR_FORMAT;
    }

    if (!(result = get_short_string(ctx, senml->entry.path,
                                    sizeof(senml->entry.path)))) {
        senml->entry_parse.has_name = true;
    }
    return result;
}

static int process_bytes_value(fluf_io_in_ctx_t *ctx) {
    fluf_internal_senml_entry_parse_state_t *const state =
            &ctx->_decoder._senml_cbor.entry_parse;
    fluf_bytes_or_string_value_t *const value =
            &ctx->_decoder._senml_cbor.entry.value.bytes;
    int result;
    if (!state->bytes_ctx) {
        assert(value->offset == 0);
        assert(value->chunk_length == 0);
        assert(value->full_length_hint == 0);
        ptrdiff_t total_size = 0;
        if ((result = fluf_cbor_ll_decoder_bytes(&ctx->_decoder._senml_cbor.ctx,
                                                 &state->bytes_ctx,
                                                 &total_size))) {
            return result;
        }
        if (total_size >= 0) {
            value->full_length_hint = (size_t) total_size;
        }
    }
    value->offset += value->chunk_length;
    value->chunk_length = 0;
    bool message_finished;
    result = fluf_cbor_ll_decoder_bytes_get_some(
            state->bytes_ctx, (const void **) (intptr_t) &value->data,
            &value->chunk_length, &message_finished);
    if (!result && message_finished) {
        state->bytes_ctx = NULL;
        value->full_length_hint = value->offset + value->chunk_length;
        state->has_value = true;
    }
    return result;
}

static int parse_senml_value(fluf_io_in_ctx_t *ctx) {
    fluf_internal_senml_entry_parse_state_t *const state =
            &ctx->_decoder._senml_cbor.entry_parse;
    fluf_internal_senml_cached_entry_t *const entry =
            &ctx->_decoder._senml_cbor.entry;
    if (state->has_value) {
        return FLUF_IO_ERR_FORMAT;
    }

    fluf_cbor_ll_value_type_t type;
    int result = fluf_cbor_ll_decoder_current_value_type(
            &ctx->_decoder._senml_cbor.ctx, &type);
    if (result) {
        return result;
    }
    switch (type) {
    case FLUF_CBOR_LL_VALUE_NULL:
        if (state->label != SENML_LABEL_VALUE) {
            return FLUF_IO_ERR_FORMAT;
        }
        entry->type = FLUF_DATA_TYPE_NULL;
        if ((result = fluf_cbor_ll_decoder_null(
                     &ctx->_decoder._senml_cbor.ctx))) {
            return result;
        }
        state->has_value = true;
        return 0;
    case FLUF_CBOR_LL_VALUE_BYTE_STRING:
        if (state->label != SENML_LABEL_VALUE_OPAQUE) {
            return FLUF_IO_ERR_FORMAT;
        }
        entry->type = FLUF_DATA_TYPE_BYTES;
        return process_bytes_value(ctx);
    case FLUF_CBOR_LL_VALUE_TEXT_STRING:
        switch (state->label) {
        case SENML_LABEL_VALUE_STRING:
            entry->type = FLUF_DATA_TYPE_STRING;
            return process_bytes_value(ctx);
        case SENML_EXT_LABEL_OBJLNK:
            entry->type = FLUF_DATA_TYPE_OBJLNK;
            if ((result = get_short_string(ctx, state->short_string_buf,
                                           sizeof(state->short_string_buf)))) {
                return result;
            }
            if (fluf_string_to_objlnk_value(&entry->value.objlnk,
                                            state->short_string_buf)) {
                return FLUF_IO_ERR_FORMAT;
            }
            state->has_value = true;
            return 0;
        default:
            return FLUF_IO_ERR_FORMAT;
        }
    case FLUF_CBOR_LL_VALUE_BOOL:
        if (state->label != SENML_LABEL_VALUE_BOOL) {
            return FLUF_IO_ERR_FORMAT;
        }
        entry->type = FLUF_DATA_TYPE_BOOL;
        if ((result = fluf_cbor_ll_decoder_bool(&ctx->_decoder._senml_cbor.ctx,
                                                &entry->value.boolean))) {
            return result;
        }
        state->has_value = true;
        return 0;
    default:
        if (state->label != SENML_LABEL_VALUE) {
            return FLUF_IO_ERR_FORMAT;
        }
        if (type == FLUF_CBOR_LL_VALUE_TIMESTAMP) {
            entry->type = FLUF_DATA_TYPE_TIME;
        } else {
            entry->type = FLUF_DATA_TYPE_INT | FLUF_DATA_TYPE_DOUBLE
                          | FLUF_DATA_TYPE_UINT;
        }
        if ((result = fluf_cbor_ll_decoder_number(
                     &ctx->_decoder._senml_cbor.ctx, &entry->value.number))) {
            return result;
        }
        state->has_value = true;
        return 0;
    }
}

static int parse_senml_basename(fluf_io_in_ctx_t *ctx) {
    fluf_internal_senml_cbor_decoder_t *const senml =
            &ctx->_decoder._senml_cbor;
    if (senml->entry_parse.has_basename) {
        return FLUF_IO_ERR_FORMAT;
    }

    fluf_cbor_ll_value_type_t type;
    int result = fluf_cbor_ll_decoder_current_value_type(&senml->ctx, &type);
    if (result) {
        return result;
    }
    if (type != FLUF_CBOR_LL_VALUE_TEXT_STRING) {
        return FLUF_IO_ERR_FORMAT;
    }

    if (!(result = get_short_string(ctx, senml->basename,
                                    sizeof(senml->basename)))) {
        senml->entry_parse.has_basename = true;
    }
    return result;
}

int _fluf_senml_cbor_decoder_init(fluf_io_in_ctx_t *ctx,
                                  fluf_op_t operation_type,
                                  const fluf_uri_path_t *base_path) {
    fluf_cbor_ll_decoder_init(&ctx->_decoder._senml_cbor.ctx);
    ctx->_decoder._senml_cbor.base = *base_path;
    ctx->_decoder._senml_cbor.composite_read =
            (operation_type == FLUF_OP_DM_READ_COMP);
    return 0;
}

int _fluf_senml_cbor_decoder_feed_payload(fluf_io_in_ctx_t *ctx,
                                          void *buff,
                                          size_t buff_size,
                                          bool payload_finished) {
    return fluf_cbor_ll_decoder_feed_payload(&ctx->_decoder._senml_cbor.ctx,
                                             buff, buff_size, payload_finished);
}

static bool entry_has_pairs_remaining(fluf_io_in_ctx_t *ctx, int *out_error) {
    assert(!*out_error);
    if (ctx->_decoder._senml_cbor.entry_parse.pairs_remaining == 0) {
        return false;
    }
    if (ctx->_decoder._senml_cbor.entry_parse.pairs_remaining > 0) {
        return true;
    }
    // ctx->_decoder._senml_cbor.entry_parse.pairs_remaining < 0
    // i.e. indefinite map
    size_t current_level;
    int result =
            fluf_cbor_ll_decoder_nesting_level(&ctx->_decoder._senml_cbor.ctx,
                                               &current_level);
    if (result) {
        *out_error = result;
        return false;
    }
    if (current_level > 1) {
        return true;
    }
    ctx->_decoder._senml_cbor.entry_parse.pairs_remaining = 0;
    return false;
}

int _fluf_senml_cbor_decoder_get_entry(fluf_io_in_ctx_t *ctx,
                                       fluf_data_type_t *inout_type_bitmask,
                                       const fluf_res_value_t **out_value,
                                       const fluf_uri_path_t **out_path) {
    fluf_internal_senml_entry_parse_state_t *const state =
            &ctx->_decoder._senml_cbor.entry_parse;
    fluf_internal_senml_cached_entry_t *const entry =
            &ctx->_decoder._senml_cbor.entry;
    *out_value = NULL;
    *out_path = NULL;
    int result;
    if ((result = ensure_in_toplevel_array(ctx))) {
        return result;
    }
    if (!state->map_entered) {
        size_t nesting_level;
        if ((result =
                     fluf_cbor_ll_decoder_errno(&ctx->_decoder._senml_cbor.ctx))
                || (result = fluf_cbor_ll_decoder_nesting_level(
                            &ctx->_decoder._senml_cbor.ctx, &nesting_level))) {
            return result;
        }
        if (nesting_level != 1) {
            return FLUF_IO_ERR_FORMAT;
        }
        if ((result = fluf_cbor_ll_decoder_enter_map(
                     &ctx->_decoder._senml_cbor.ctx,
                     &state->pairs_remaining))) {
            return result;
        }
        state->map_entered = true;
        memset(entry, 0, sizeof(*entry));
    }
    result = 0;
    while (!result && entry_has_pairs_remaining(ctx, &result)) {
        if (!state->label_ready) {
            if ((result = get_senml_cbor_label(ctx))) {
                return result;
            }
            state->label_ready = true;
        }
        switch (state->label) {
        case SENML_LABEL_NAME:
            result = parse_senml_name(ctx);
            break;
        case SENML_LABEL_VALUE:
        case SENML_LABEL_VALUE_BOOL:
        case SENML_LABEL_VALUE_OPAQUE:
        case SENML_LABEL_VALUE_STRING:
        case SENML_EXT_LABEL_OBJLNK:
            if (ctx->_decoder._senml_cbor.composite_read) {
                result = FLUF_IO_ERR_FORMAT;
            } else {
                result = parse_senml_value(ctx);
            }
            break;
        case SENML_LABEL_BASE_NAME:
            result = parse_senml_basename(ctx);
            break;
        default:
            result = FLUF_IO_ERR_FORMAT;
        }
        if (!result) {
            if (state->bytes_ctx) {
                // We only have a partial byte or text string
                // Don't advance as we need to pass all the chunks to the user
                assert(entry->type
                       & (FLUF_DATA_TYPE_BYTES | FLUF_DATA_TYPE_STRING));
                assert(entry->value.bytes.offset
                               + entry->value.bytes.chunk_length
                       != entry->value.bytes.full_length_hint);
                break;
            }
            if (state->pairs_remaining >= 0) {
                --state->pairs_remaining;
            }
            state->label_ready = false;
        }
    }
    if (entry->type & (FLUF_DATA_TYPE_BYTES | FLUF_DATA_TYPE_STRING)) {
        // Bytes or String
        if (result
                || (result = fluf_cbor_ll_decoder_errno(
                            &ctx->_decoder._senml_cbor.ctx))
                               < 0) {
            return result;
        }
        if (!state->path_processed
                && ((state->has_basename && state->has_name)
                    || !state->pairs_remaining
                    || (state->bytes_ctx && state->pairs_remaining == 1))) {
            int parse_path_result = parse_next_absolute_path(ctx);
            if (parse_path_result) {
                return parse_path_result;
            }
            state->path_processed = true;
        }
        if (state->path_processed) {
            *out_path = &ctx->_out_path;
        }
        switch ((*inout_type_bitmask &= entry->type)) {
        default:
            AVS_UNREACHABLE("Bytes and String types are explicitly marked and "
                            "shall not require disambiguation");
            return FLUF_IO_WANT_TYPE_DISAMBIGUATION;
        case FLUF_DATA_TYPE_NULL:
            return FLUF_IO_ERR_FORMAT;
        case FLUF_DATA_TYPE_BYTES:
        case FLUF_DATA_TYPE_STRING:;
        }
        ctx->_out_value.bytes_or_string = entry->value.bytes;
        *out_value = &ctx->_out_value;
        if (state->path_processed
                && entry->value.bytes.offset + entry->value.bytes.chunk_length
                               == entry->value.bytes.full_length_hint) {
            memset(state, 0, sizeof(*state));
        }
        assert(!result || result == FLUF_IO_EOF
               || result == FLUF_IO_WANT_NEXT_PAYLOAD);
        // Note: In case of FLUF_IO_EOF, it will be delivered next time.
        // In case of FLUF_IO_WANT_NEXT_PAYLOAD, it will also be delivered next
        // time, through either the fluf_cbor_ll_decoder_errno() near the top of
        // this function, or one of the get/parse functions in the while loop
        // above.
        return 0;
    } else {
        // simple data types
        if (result
                || (result = fluf_cbor_ll_decoder_errno(
                            &ctx->_decoder._senml_cbor.ctx))
                               < 0) {
            return result;
        }
        if (!state->path_processed) {
            int parse_path_result = parse_next_absolute_path(ctx);
            if (parse_path_result) {
                return parse_path_result;
            }
            state->path_processed = true;
        }
        *out_path = &ctx->_out_path;
        switch ((*inout_type_bitmask &= entry->type)) {
        case FLUF_DATA_TYPE_NULL:
            if (entry->type == FLUF_DATA_TYPE_NULL) {
                *out_value = NULL;
                memset(state, 0, sizeof(*state));
                return 0;
            }
            return FLUF_IO_ERR_FORMAT;
        case FLUF_DATA_TYPE_INT:
            if ((result = _fluf_cbor_get_i64_from_ll_number(
                         &entry->value.number, &ctx->_out_value.int_value,
                         false))) {
                return result;
            }
            break;
        case FLUF_DATA_TYPE_DOUBLE:
            if ((result = _fluf_cbor_get_double_from_ll_number(
                         &entry->value.number,
                         &ctx->_out_value.double_value))) {
                return result;
            }
            break;
        case FLUF_DATA_TYPE_BOOL:
            ctx->_out_value.bool_value = entry->value.boolean;
            break;
        case FLUF_DATA_TYPE_OBJLNK:
            ctx->_out_value.objlnk = entry->value.objlnk;
            break;
        case FLUF_DATA_TYPE_UINT:
            if ((result = _fluf_cbor_get_u64_from_ll_number(
                         &entry->value.number, &ctx->_out_value.uint_value))) {
                return result;
            }
            break;
        case FLUF_DATA_TYPE_TIME:
            if ((result = _fluf_cbor_get_i64_from_ll_number(
                         &entry->value.number, &ctx->_out_value.time_value,
                         true))) {
                return result;
            }
            break;
        default:
            return FLUF_IO_WANT_TYPE_DISAMBIGUATION;
        }
        *out_value = &ctx->_out_value;
        memset(state, 0, sizeof(*state));
        return 0;
    }
}

int _fluf_senml_cbor_decoder_get_entry_count(fluf_io_in_ctx_t *ctx,
                                             size_t *out_count) {
    int result = ensure_in_toplevel_array(ctx);
    if (result) {
        return result < 0 ? result : FLUF_IO_ERR_LOGIC;
    }
    if (ctx->_decoder._senml_cbor.entry_count < 0) {
        return FLUF_IO_ERR_FORMAT;
    }
    *out_count = (size_t) ctx->_decoder._senml_cbor.entry_count;
    return 0;
}

#endif // FLUF_WITH_SENML_CBOR
