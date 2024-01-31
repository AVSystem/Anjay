/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <stdio.h>
#include <string.h>

#include <fluf/fluf.h>
#include <fluf/fluf_utils.h>

#include "fluf_attributes.h"

#define _RET_IF_ERROR(Val) \
    if (Val) {             \
        return Val;        \
    }

static int get_attr(const fluf_coap_options_t *opts,
                    const char *attr,
                    uint32_t *uint_value,
                    double *double_value,
                    bool *variable_flag) {
    assert(!!uint_value != !!double_value);

    size_t it = 0;
    uint8_t attr_buff[FLUF_ATTR_OPTION_MAX_SIZE];

    while (1) {
        size_t attr_option_size = 0;
        memset(attr_buff, 0, sizeof(attr_buff));
        int res = _fluf_coap_options_get_data_iterate(
                opts, _FLUF_COAP_OPTION_URI_QUERY, &it, &attr_option_size,
                attr_buff, sizeof(attr_buff));

        if (res == _FLUF_COAP_OPTION_MISSING) {
            return 0;
        } else if (res) {
            return FLUF_ERR_ATTR_BUFF;
        }

        if (!strncmp(attr, (const char *) attr_buff, strlen(attr))) {
            // format pmin={value}
            size_t value_offset = strlen(attr) + 1;
            if (value_offset > attr_option_size) {
                return FLUF_ERR_MALFORMED_MESSAGE;
            }
            *variable_flag = true;
            if (uint_value) {
                res = fluf_string_to_uint32_value(
                        (const char *) (attr_buff + value_offset),
                        attr_option_size - value_offset,
                        (uint32_t *) uint_value);
            } else if (double_value) {
                res = fluf_string_to_simple_double_value(
                        (const char *) (attr_buff + value_offset),
                        attr_option_size - value_offset,
                        (double *) double_value);
            }
            if (res) {
                return FLUF_ERR_ATTR_BUFF;
            }
        }
    }
}

static int add_str_attr(fluf_coap_options_t *opts,
                        const char *attr_name,
                        const char *attr_value,
                        bool value_present) {
    if (!value_present) {
        return 0;
    }

    char atrr_buff[FLUF_ATTR_OPTION_MAX_SIZE] = { 0 };

    size_t name_len = strlen(attr_name);
    if (name_len >= FLUF_ATTR_OPTION_MAX_SIZE) {
        return FLUF_ERR_ATTR_BUFF;
    }
    memcpy(atrr_buff, attr_name, name_len);

    // not empty string
    if (attr_value) {
        size_t value_len = strlen(attr_value);
        atrr_buff[name_len] = '=';

        if (name_len + value_len + 1 >= FLUF_ATTR_OPTION_MAX_SIZE) {
            return FLUF_ERR_ATTR_BUFF;
        }
        memcpy(&atrr_buff[name_len + 1], attr_value, value_len);
    }

    return _fluf_coap_options_add_string(opts, _FLUF_COAP_OPTION_URI_QUERY,
                                         atrr_buff);
}

int fluf_attr_discover_decode(const fluf_coap_options_t *opts,
                              fluf_attr_discover_t *attr) {
    memset(attr, 0, sizeof(fluf_attr_discover_t));

    return get_attr(opts, "depth", &attr->depth, NULL, &attr->has_depth);
}

int fluf_attr_notification_attr_decode(const fluf_coap_options_t *opts,
                                       fluf_attr_notification_t *attr) {
    memset(attr, 0, sizeof(fluf_attr_notification_t));

    int res = get_attr(opts, "pmin", &attr->min_period, NULL,
                       &attr->has_min_period);
    _RET_IF_ERROR(res);
    res = get_attr(opts, "pmax", &attr->max_period, NULL,
                   &attr->has_max_period);
    _RET_IF_ERROR(res);
    res = get_attr(opts, "gt", NULL, &attr->greater_than,
                   &attr->has_greater_than);
    _RET_IF_ERROR(res);
    res = get_attr(opts, "lt", NULL, &attr->less_than, &attr->has_less_than);
    _RET_IF_ERROR(res);
    res = get_attr(opts, "st", NULL, &attr->step, &attr->has_step);
    _RET_IF_ERROR(res);
    res = get_attr(opts, "epmin", &attr->min_eval_period, NULL,
                   &attr->has_min_eval_period);
    _RET_IF_ERROR(res);
    res = get_attr(opts, "epmax", &attr->max_eval_period, NULL,
                   &attr->has_max_eval_period);
#ifdef FLUF_WITH_LWM2M12
    _RET_IF_ERROR(res);
    res = get_attr(opts, "edge", &attr->edge, NULL, &attr->has_edge);
    _RET_IF_ERROR(res);
    res = get_attr(opts, "con", &attr->con, NULL, &attr->has_con);
    _RET_IF_ERROR(res);
    res = get_attr(opts, "hqmax", &attr->hqmax, NULL, &attr->has_hqmax);
#endif // FLUF_WITH_LWM2M12

    return res;
}

int fluf_attr_register_prepare(fluf_coap_options_t *opts,
                               const fluf_attr_register_t *attr) {
    int res = add_str_attr(opts, "ep", attr->endpoint, attr->has_endpoint);
    _RET_IF_ERROR(res);
    if (attr->has_lifetime) {
        char lifetime_buff[FLUF_U32_STR_MAX_LEN + 1] = { 0 };
        fluf_uint32_to_string_value(attr->lifetime, lifetime_buff);
        res = add_str_attr(opts, "lt", lifetime_buff, attr->has_lifetime);
        _RET_IF_ERROR(res);
    }
    res = add_str_attr(opts, "lwm2m", attr->lwm2m_ver, attr->has_lwm2m_ver);
    _RET_IF_ERROR(res);
    res = add_str_attr(opts, "b", attr->binding, attr->has_binding);
    _RET_IF_ERROR(res);
    res = add_str_attr(opts, "sms", attr->sms_number, attr->has_sms_number);
    _RET_IF_ERROR(res);
    res = add_str_attr(opts, "Q", NULL, attr->has_Q);

    return res;
}

int fluf_attr_bootstrap_prepare(fluf_coap_options_t *opts,
                                const fluf_attr_bootstrap_t *attr) {
    int res = add_str_attr(opts, "ep", attr->endpoint, attr->has_endpoint);
    _RET_IF_ERROR(res);
    if (attr->has_pct) {
        char pct_buff[FLUF_U32_STR_MAX_LEN + 1] = { 0 };
        fluf_uint16_to_string_value(attr->pct, pct_buff);
        res = add_str_attr(opts, "pct", pct_buff, attr->has_pct);
    }

    return res;
}
