/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <avsystem/commons/avs_defs.h>

#include <fluf/fluf_defs.h>
#include <fluf/fluf_io.h>
#include <fluf/fluf_io_ctx.h>
#include <fluf/fluf_utils.h>

#include "fluf_internal.h"

#ifndef FLUF_WITHOUT_BOOTSTRAP_DISCOVER_CTX

int fluf_io_bootstrap_discover_ctx_init(fluf_io_bootstrap_discover_ctx_t *ctx,
                                        const fluf_uri_path_t *base_path) {
    assert(ctx && base_path);
    if (fluf_uri_path_has(base_path, FLUF_ID_IID)) {
        return FLUF_IO_ERR_INPUT_ARG;
    }
    memset(ctx, 0, sizeof(fluf_io_bootstrap_discover_ctx_t));
    ctx->base_path = *base_path;

    return 0;
}

int fluf_io_bootstrap_discover_ctx_new_entry(
        fluf_io_bootstrap_discover_ctx_t *ctx,
        const fluf_uri_path_t *path,
        const char *version,
        const uint16_t *ssid,
        const char *uri) {
    assert(ctx);

    if (ctx->buff.bytes_in_internal_buff) {
        return FLUF_IO_ERR_LOGIC;
    }
    if (!(fluf_uri_path_is(path, FLUF_ID_OID)
          || fluf_uri_path_is(path, FLUF_ID_IID))
            || fluf_uri_path_outside_base(path, &ctx->base_path)
            || !fluf_uri_path_increasing(&ctx->last_path, path)) {
        return FLUF_IO_ERR_INPUT_ARG;
    }
    if (ssid && path->ids[FLUF_ID_OID] != FLUF_OBJ_ID_SECURITY
            && path->ids[FLUF_ID_OID] != FLUF_OBJ_ID_SERVER
            && path->ids[FLUF_ID_OID] != FLUF_OBJ_ID_OSCORE) {
        return FLUF_IO_ERR_INPUT_ARG;
    }
    if (!ssid && path->ids[FLUF_ID_OID] == FLUF_OBJ_ID_SERVER) {
        return FLUF_IO_ERR_INPUT_ARG;
    }
    if (uri && path->ids[FLUF_ID_OID] != FLUF_OBJ_ID_SECURITY) {
        return FLUF_IO_ERR_INPUT_ARG;
    }
    if (fluf_uri_path_is(path, FLUF_ID_OID) && (uri || ssid)) {
        return FLUF_IO_ERR_INPUT_ARG;
    }
    if (fluf_uri_path_is(path, FLUF_ID_IID) && version) {
        return FLUF_IO_ERR_INPUT_ARG;
    }

    if (!ctx->first_record_added) {
#    ifdef FLUF_WITH_LWM2M12
        const char *payload_begin = "</>;lwm2m=1.2";
#    else  // FLUF_WITH_LWM2M12
        const char *payload_begin = "</>;lwm2m=1.1";
#    endif // FLUF_WITH_LWM2M12

        size_t record_len = strlen(payload_begin);
        memcpy(ctx->buff.internal_buff, payload_begin, record_len);
        ctx->buff.bytes_in_internal_buff = record_len;
        ctx->buff.remaining_bytes = record_len;
    }

    int res = _fluf_io_add_link_format_record(path, version, NULL, false,
                                              &ctx->buff);
    if (res) {
        return res;
    }

    if (ssid) {
        size_t write_pointer = ctx->buff.bytes_in_internal_buff;
        memcpy(&ctx->buff.internal_buff[write_pointer], ";ssid=", 6);
        write_pointer += 6;
        char ssid_str[FLUF_U16_STR_MAX_LEN];
        size_t ssid_str_len = fluf_uint16_to_string_value(*ssid, ssid_str);
        memcpy(&ctx->buff.internal_buff[write_pointer], ssid_str, ssid_str_len);
        write_pointer += ssid_str_len;
        ctx->buff.bytes_in_internal_buff = write_pointer;
        ctx->buff.remaining_bytes = write_pointer;
    }
    if (uri) {
        size_t write_pointer = ctx->buff.bytes_in_internal_buff;
        memcpy(&ctx->buff.internal_buff[write_pointer], ";uri=\"", 6);
        write_pointer += 6;
        ctx->buff.bytes_in_internal_buff = write_pointer;
        ctx->buff.remaining_bytes = write_pointer;
        ctx->buff.is_extended_type = true;
        // + 1 to add \" on the end
        ctx->buff.remaining_bytes += strlen(uri) + 1;
        ctx->uri = uri;
    }

    ctx->last_path = *path;
    ctx->first_record_added = true;
    return 0;
}

int fluf_io_bootstrap_discover_ctx_get_payload(
        fluf_io_bootstrap_discover_ctx_t *ctx,
        void *out_buff,
        size_t out_buff_len,
        size_t *out_copied_bytes) {
    assert(ctx);
    return _fluf_io_get_payload(out_buff, out_buff_len, out_copied_bytes,
                                &ctx->buff, NULL, ctx->uri);
}

#endif // FLUF_WITHOUT_BOOTSTRAP_DISCOVER_CTX

#ifndef FLUF_WITHOUT_DISCOVER_CTX

static bool res_instances_will_be_written(const fluf_uri_path_t *base_path,
                                          uint8_t depth) {
    return (uint8_t) base_path->uri_len + depth > FLUF_ID_RIID;
}

static size_t add_attribute(fluf_io_buff_t *ctx,
                            const char *name,
                            const uint32_t *uint_value,
                            const double *double_value) {
    assert(!!uint_value != !!double_value);

    size_t record_len = 0;

    ctx->internal_buff[record_len++] = ';';
    memcpy(&ctx->internal_buff[record_len], name, strlen(name));
    record_len += strlen(name);
    ctx->internal_buff[record_len++] = '=';

    if (uint_value) {
        record_len += fluf_uint32_to_string_value(
                *uint_value, (char *) &ctx->internal_buff[record_len]);
    } else {
        record_len += fluf_double_to_simple_str_value(
                *double_value, (char *) &ctx->internal_buff[record_len]);
    }
    return record_len;
}

static size_t get_attribute_record(fluf_io_buff_t *ctx,
                                   fluf_attr_notification_t *attributes) {
    if (attributes->has_min_period) {
        attributes->has_min_period = false;
        return add_attribute(ctx, "pmin", &attributes->min_period, NULL);
    }
    if (attributes->has_max_period) {
        attributes->has_max_period = false;
        return add_attribute(ctx, "pmax", &attributes->max_period, NULL);
    }
    if (attributes->has_greater_than) {
        attributes->has_greater_than = false;
        return add_attribute(ctx, "gt", NULL, &attributes->greater_than);
    }
    if (attributes->has_less_than) {
        attributes->has_less_than = false;
        return add_attribute(ctx, "lt", NULL, &attributes->less_than);
    }
    if (attributes->has_step) {
        attributes->has_step = false;
        return add_attribute(ctx, "st", NULL, &attributes->step);
    }
    if (attributes->has_min_eval_period) {
        attributes->has_min_eval_period = false;
        return add_attribute(ctx, "epmin", &attributes->min_eval_period, NULL);
    }
    if (attributes->has_max_eval_period) {
        attributes->has_max_eval_period = false;
        return add_attribute(ctx, "epmax", &attributes->max_eval_period, NULL);
    }
#    ifdef FLUF_WITH_LWM2M12
    if (attributes->has_edge) {
        attributes->has_edge = false;
        return add_attribute(ctx, "edge", &attributes->edge, NULL);
    }
    if (attributes->has_con) {
        attributes->has_con = false;
        return add_attribute(ctx, "con", &attributes->con, NULL);
    }
    if (attributes->has_hqmax) {
        attributes->has_hqmax = false;
        return add_attribute(ctx, "hqmax", &attributes->hqmax, NULL);
    }
#    endif // FLUF_WITH_LWM2M12

    return 0;
}

static int get_attributes_payload(fluf_io_discover_ctx_t *ctx,
                                  void *out_buff,
                                  size_t out_buff_len,
                                  size_t *copied_bytes) {
    while (1) {
        if (ctx->attr_record_offset == ctx->attr_record_len) {
            ctx->attr_record_len = get_attribute_record(&ctx->buff, &ctx->attr);
            ctx->attr_record_offset = 0;
        }

        size_t bytes_to_copy =
                AVS_MIN(ctx->attr_record_len - ctx->attr_record_offset,
                        out_buff_len - *copied_bytes);

        memcpy(&((uint8_t *) out_buff)[*copied_bytes],
               &(ctx->buff.internal_buff[ctx->attr_record_offset]),
               bytes_to_copy);
        *copied_bytes += bytes_to_copy;

        // no more records
        if (ctx->attr_record_len == ctx->attr_record_offset) {
            ctx->buff.remaining_bytes = 0;
            ctx->buff.offset = 0;
            ctx->buff.bytes_in_internal_buff = 0;
            ctx->buff.is_extended_type = false;
            return 0;
        }
        ctx->attr_record_offset += bytes_to_copy;

        if (!(out_buff_len - *copied_bytes)) {
            return FLUF_IO_NEED_NEXT_CALL;
        }
    }
}

int fluf_io_discover_ctx_init(fluf_io_discover_ctx_t *ctx,
                              const fluf_uri_path_t *base_path,
                              const uint8_t *depth) {
    assert(ctx && base_path);

    if ((depth && *depth > 3) || !fluf_uri_path_has(base_path, FLUF_ID_OID)
            || fluf_uri_path_is(base_path, FLUF_ID_RIID)) {
        return FLUF_IO_ERR_INPUT_ARG;
    }
    memset(ctx, 0, sizeof(fluf_io_discover_ctx_t));
    ctx->base_path = *base_path;

    if (depth) {
        ctx->depth = *depth;
    } else {
        // default depth value
        if (fluf_uri_path_is(base_path, FLUF_ID_OID)) {
            ctx->depth = 2;
        } else {
            ctx->depth = 1;
        }
    }

    return 0;
}

int fluf_io_discover_ctx_new_entry(fluf_io_discover_ctx_t *ctx,
                                   const fluf_uri_path_t *path,
                                   const fluf_attr_notification_t *attributes,
                                   const char *version,
                                   const uint16_t *dim) {
    assert(ctx && path);

    if (ctx->buff.bytes_in_internal_buff) {
        return FLUF_IO_ERR_LOGIC;
    }

    if (path->uri_len - ctx->base_path.uri_len > ctx->depth) {
        return FLUF_IO_WARNING_DEPTH;
    }

    if ((ctx->dim_counter && !fluf_uri_path_is(path, FLUF_ID_RIID))
            || (!ctx->dim_counter && fluf_uri_path_is(path, FLUF_ID_RIID))) {
        return FLUF_IO_ERR_LOGIC;
    }

    if (fluf_uri_path_outside_base(path, &ctx->base_path)
            || !fluf_uri_path_has(path, FLUF_ID_OID)
            || !fluf_uri_path_increasing(&ctx->last_path, path)
            || (version && !fluf_uri_path_is(path, FLUF_ID_OID))
            || (dim && !fluf_uri_path_is(path, FLUF_ID_RID))) {
        return FLUF_IO_ERR_INPUT_ARG;
    }

    if (dim && res_instances_will_be_written(&ctx->base_path, ctx->depth)) {
        ctx->dim_counter = *dim;
    }

    int res = _fluf_io_add_link_format_record(
            path, version, dim, !ctx->first_record_added, &ctx->buff);
    if (res) {
        return res;
    }

    if (attributes) {
        ctx->attr = *attributes;
        ctx->buff.is_extended_type = true;
        // HACK: add one byte to add attributes when copying
        ctx->buff.remaining_bytes++;
    }

    ctx->first_record_added = true;
    ctx->last_path = *path;
    if (ctx->dim_counter && fluf_uri_path_is(path, FLUF_ID_RIID)) {
        ctx->dim_counter--;
    }
    return 0;
}

int fluf_io_discover_ctx_get_payload(fluf_io_discover_ctx_t *ctx,
                                     void *out_buff,
                                     size_t out_buff_len,
                                     size_t *out_copied_bytes) {
    assert(ctx && out_buff && out_buff_len && out_copied_bytes);

    int ret = _fluf_io_get_payload(out_buff, out_buff_len, out_copied_bytes,
                                   &ctx->buff, NULL, NULL);

    // there are attributes left and link_format record is copied
    if (ctx->buff.is_extended_type
            && ctx->buff.offset >= ctx->buff.bytes_in_internal_buff) {
        return get_attributes_payload(ctx, out_buff, out_buff_len,
                                      out_copied_bytes);
    }
    return ret;
}

#endif // FLUF_WITHOUT_DISCOVER_CTX
