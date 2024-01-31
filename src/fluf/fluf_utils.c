/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <avsystem/commons/avs_defs.h>

#include <fluf/fluf_defs.h>
#include <fluf/fluf_utils.h>

bool fluf_uri_path_increasing(const fluf_uri_path_t *previous_path,
                              const fluf_uri_path_t *current_path) {
    size_t path_to_check_len =
            AVS_MIN(previous_path->uri_len, current_path->uri_len);

    for (size_t i = 0; i < path_to_check_len; i++) {
        if (current_path->ids[i] > previous_path->ids[i]) {
            return true;
        } else if (current_path->ids[i] < previous_path->ids[i]) {
            return false;
        }
    }
    return previous_path->uri_len < current_path->uri_len;
}

int fluf_validate_obj_version(const char *version) {
    if (!version) {
        return 0;
    }
    // accepted format is X.Y where X and Y are digits
    if (!isdigit(version[0]) || version[1] != '.' || !isdigit(version[2])
            || version[3] != '\0') {
        return FLUF_IO_ERR_INPUT_ARG;
    }
    return 0;
}

static size_t uint64_to_string_value_internal(uint64_t value,
                                              char *out_buff,
                                              size_t *dot_position,
                                              bool ignore_zeros) {
    char buff[FLUF_U64_STR_MAX_LEN + 1] = { 0 };
    int idx = sizeof(buff) - 1;
    size_t msg_size = 0;
    do {
        char digit = (char) ('0' + (value % 10));
        value /= 10;
        if (ignore_zeros) {
            if (digit == '0') {
                continue;
            } else {
                ignore_zeros = false;
            }
        }
        buff[idx--] = digit;
        msg_size++;
        if (dot_position && *dot_position == msg_size) {
            buff[idx--] = '.';
            msg_size++;
        }
    } while (value > 0);
    assert(!dot_position || *dot_position < msg_size);

    memcpy(out_buff, &buff[idx + 1], msg_size);
    return msg_size;
}

size_t fluf_uint64_to_string_value(uint64_t value, char *out_buff) {
    return uint64_to_string_value_internal(value, out_buff, NULL, false);
}

size_t fluf_uint32_to_string_value(uint32_t value, char *out_buff) {
    return uint64_to_string_value_internal(
            (uint64_t) value, out_buff, NULL, false);
}

size_t fluf_uint16_to_string_value(uint16_t value, char *out_buff) {
    return uint64_to_string_value_internal(
            (uint64_t) value, out_buff, NULL, false);
}

#define _MAX_FRACTION_SIZE_IN_EXPONENTIAL_NOTATION \
    (sizeof("2.2250738585072014") - 1 - 2)

size_t fluf_double_to_simple_str_value(double value, char *out_buff) {
    size_t out_len = 0;
    char buff[FLUF_U64_STR_MAX_LEN + 1] = { 0 };
    size_t bytes_to_copy;

    if (isnan(value)) {
        memcpy(out_buff, "nan", 3);
        return 3;
    } else if (value == 0.0) {
        out_buff[0] = '0';
        return 1;
    }

    if (value < 0.0) {
        out_buff[out_len++] = '-';
        value = -value;
    }
    if (isinf(value)) {
        memcpy(&out_buff[out_len], "inf", 3);
        out_len += 3;
        return out_len;
    }

    // X.Y format
    if (value > 1.0 && value < UINT64_MAX && (value - (uint64_t) value)) {
        size_t dot_position = 0;
        while (value - (uint64_t) value) {
            value = value * 10.0;
            dot_position++;
        }
        out_len += uint64_to_string_value_internal(
                (uint64_t) value, &out_buff[out_len], &dot_position, false);
    } else if (value >= 1.0 && value < UINT64_MAX) { // X format
        size_t nil_counter = 0;
        while (value > UINT64_MAX) {
            value = value / 10.0;
            nil_counter++;
        }
        out_len += uint64_to_string_value_internal(
                (uint64_t) value, &out_buff[out_len], NULL, false);
        memset(&out_buff[out_len], '0', nil_counter);
        out_len += nil_counter;
    } else if (value >= UINT64_MAX) { // X.YeZ format
        size_t exponential_value = 0;
        double temp_value = value;
        while (temp_value > 10.0) {
            temp_value = temp_value / 10.0;
            exponential_value++;
        }
        while (value > UINT64_MAX) {
            value = value / 10.0;
        }
        bytes_to_copy = uint64_to_string_value_internal(
                (uint64_t) value, buff, NULL, true);
        out_buff[out_len++] = buff[0];
        bytes_to_copy--;
        out_buff[out_len++] = '.';
        bytes_to_copy = AVS_MIN(bytes_to_copy,
                                _MAX_FRACTION_SIZE_IN_EXPONENTIAL_NOTATION);
        memcpy(&out_buff[out_len], &buff[1], bytes_to_copy);
        out_len += bytes_to_copy;
        out_buff[out_len++] = 'e';
        out_len += uint64_to_string_value_internal(
                (uint64_t) exponential_value, &out_buff[out_len], NULL, false);
    } else if (value < 1 && value > 1e-10) { // 0.X format
        size_t nil_counter = 0;
        while (value - (uint64_t) value) {
            value = value * 10.0;
            nil_counter++;
        }

        bytes_to_copy = uint64_to_string_value_internal(
                (uint64_t) value, buff, NULL, false);
        size_t nil_to_add = nil_counter - bytes_to_copy;
        memcpy(&out_buff[out_len], "0.", 2);
        out_len += 2;
        if (nil_to_add > 0) {
            memset(&out_buff[out_len], '0', nil_to_add);
            out_len += nil_to_add;
        }
        memcpy(&out_buff[out_len], buff, bytes_to_copy);
        out_len += bytes_to_copy;
    } else { // X.Ye-Z format
        size_t exponential_value = 0;
        double temp_value = value;
        while (temp_value < 1.0) {
            temp_value = temp_value * 10.0;
            exponential_value++;
        }
        while (value - (uint64_t) value) {
            value = value * 10.0;
        }
        bytes_to_copy = uint64_to_string_value_internal(
                (uint64_t) value, buff, NULL, true);
        out_buff[out_len++] = buff[0];
        bytes_to_copy--;
        if (bytes_to_copy) {
            out_buff[out_len++] = '.';
            bytes_to_copy = AVS_MIN(bytes_to_copy,
                                    _MAX_FRACTION_SIZE_IN_EXPONENTIAL_NOTATION);
        }
        memcpy(&out_buff[out_len], &buff[1], bytes_to_copy);
        out_len += bytes_to_copy;
        out_buff[out_len++] = 'e';
        out_buff[out_len++] = '-';
        out_len += uint64_to_string_value_internal(
                (uint64_t) exponential_value, &out_buff[out_len], NULL, false);
    }

    return out_len;
}

int fluf_string_to_uint32_value(const char *buff,
                                size_t buff_len,
                                uint32_t *out_val) {
    uint32_t value = 0;
    uint32_t multiplicator = 1;
    uint8_t single_value = 0;

    if (!buff_len) {
        return -1;
    }

    for (size_t i = buff_len; i > 0; i--) {
        single_value = (uint8_t) buff[i - 1] - '0';
        if (single_value > 9) {
            return -1; // incorrect buffer
        }
        value += (uint32_t) single_value * multiplicator;
        multiplicator = multiplicator * 10;
    }
    *out_val = value;
    return 0;
}

int fluf_string_to_simple_double_value(const char *buff,
                                       size_t buff_len,
                                       double *out_val) {
    bool is_negative = false;
    if (!buff_len) {
        return -1;
    }
    if (buff[0] == '-') {
        is_negative = true;
        buff++;
        buff_len--;
    }
    if (!buff_len) {
        return -1;
    }

    bool is_fractional_part = false;
    for (size_t i = buff_len; i > 0; i--) {
        if (buff[i - 1] == '.') {
            is_fractional_part = true;
            break;
        }
    }

    uint8_t single_value;
    double fractional_value = 0;
    double fractional_divider = 1.0;
    double integer_value = 0;
    double multiplicator = 1.0;
    for (size_t i = buff_len; i > 0; i--) {
        if (buff[i - 1] == '.') {
            is_fractional_part = false;
            multiplicator = 1;
        } else {
            single_value = (uint8_t) buff[i - 1] - '0';
            if (single_value > 9) {
                return -1; // incorrect buffer
            }
            if (is_fractional_part) {
                fractional_value += single_value * multiplicator;
                fractional_divider *= 10;
            } else {
                integer_value += single_value * multiplicator;
            }
            multiplicator = multiplicator * 10;
        }
    }

    *out_val = integer_value;

    if (fractional_value) {
        *out_val += fractional_value / fractional_divider;
    }
    if (is_negative) {
        *out_val = -(*out_val);
    }

    return 0;
}
