/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef FLUF_UTILS_H
#define FLUF_UTILS_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <avsystem/commons/avs_defs.h>

#include <fluf/fluf_defs.h>
#include <fluf/fluf_io.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLUF_U16_STR_MAX_LEN (sizeof("65535") - 1)
#define FLUF_U32_STR_MAX_LEN (sizeof("4294967295") - 1)
#define FLUF_U64_STR_MAX_LEN (sizeof("18446744073709551615") - 1)
#define FLUF_DOUBLE_STR_MAX_LEN (sizeof("-2.2250738585072014E-308") - 1)

#define FLUF_ID_INVALID UINT16_MAX

#define _FLUF_URI_PATH_INITIALIZER(Oid, Iid, Rid, Riid, Len) \
    {                                                        \
        .uri_len = Len,                                      \
        .ids = {                                             \
            [FLUF_ID_OID] = (Oid),                           \
            [FLUF_ID_IID] = (Iid),                           \
            [FLUF_ID_RID] = (Rid),                           \
            [FLUF_ID_RIID] = (Riid)                          \
        }                                                    \
    }

#define _FLUF_MAKE_URI_PATH(...) \
    ((fluf_uri_path_t) _FLUF_URI_PATH_INITIALIZER(__VA_ARGS__))

#define FLUF_MAKE_RESOURCE_INSTANCE_PATH(Oid, Iid, Rid, Riid) \
    _FLUF_MAKE_URI_PATH(Oid, Iid, Rid, Riid, 4)

#define FLUF_MAKE_RESOURCE_PATH(Oid, Iid, Rid) \
    _FLUF_MAKE_URI_PATH(Oid, Iid, Rid, FLUF_ID_INVALID, 3)

#define FLUF_MAKE_INSTANCE_PATH(Oid, Iid) \
    _FLUF_MAKE_URI_PATH(Oid, Iid, FLUF_ID_INVALID, FLUF_ID_INVALID, 2)

#define FLUF_MAKE_OBJECT_PATH(Oid) \
    _FLUF_MAKE_URI_PATH(           \
            Oid, FLUF_ID_INVALID, FLUF_ID_INVALID, FLUF_ID_INVALID, 1)

#define FLUF_MAKE_ROOT_PATH()            \
    _FLUF_MAKE_URI_PATH(FLUF_ID_INVALID, \
                        FLUF_ID_INVALID, \
                        FLUF_ID_INVALID, \
                        FLUF_ID_INVALID, \
                        0)

static inline bool fluf_uri_path_equal(const fluf_uri_path_t *left,
                                       const fluf_uri_path_t *right) {
    if (left->uri_len != right->uri_len) {
        return false;
    }
    for (size_t i = 0; i < left->uri_len; ++i) {
        if (left->ids[i] < right->ids[i]) {
            return false;
        } else if (left->ids[i] > right->ids[i]) {
            return false;
        }
    }
    return true;
}

static inline size_t fluf_uri_path_length(const fluf_uri_path_t *path) {
    return path->uri_len;
}

static inline bool fluf_uri_path_has(const fluf_uri_path_t *path,
                                     fluf_id_type_t id_type) {
    return path->uri_len > id_type;
}

static inline bool fluf_uri_path_is(const fluf_uri_path_t *path,
                                    fluf_id_type_t id_type) {
    return path->uri_len == (size_t) id_type + 1u;
}

static inline bool fluf_uri_path_outside_base(const fluf_uri_path_t *path,
                                              const fluf_uri_path_t *base) {
    if (path->uri_len < base->uri_len) {
        return true;
    }
    for (size_t i = 0; i < base->uri_len; ++i) {
        if (path->ids[i] != base->ids[i]) {
            return true;
        }
    }
    return false;
}

bool fluf_uri_path_increasing(const fluf_uri_path_t *previous_path,
                              const fluf_uri_path_t *current_path);

/**
 * Validates the version of the object - accepted format is X.Y where X and Y
 * are digits.
 *
 * @param version  Object version.
 *
 * @returns 0 on success or NULL @p version pointer, a @ref
 * FLUF_IO_ERR_INPUT_ARG value in case of incorrect format.
 */
int fluf_validate_obj_version(const char *version);

/**
 * Converts uint16_t value to string and copies it to @p out_buff (without the
 * terminating nullbyte). The minimum required @p out_buff size is @ref
 * FLUF_U16_STR_MAX_LEN.
 *
 * @param      value    Input value.
 * @param[out] out_buff Output buffer.
 *
 * @return Number of bytes written.
 */
size_t fluf_uint16_to_string_value(uint16_t value, char *out_buff);

/**
 * Converts uint32_t value to string and copies it to @p out_buff (without the
 * terminating nullbyte). The minimum required @p out_buff size is @ref
 * FLUF_U32_STR_MAX_LEN.
 *
 * @param      value    Input value.
 * @param[out] out_buff Output buffer.
 *
 * @return Number of bytes written.
 */
size_t fluf_uint32_to_string_value(uint32_t value, char *out_buff);

/**
 * Converts uint64_t value to string and copies it to @p out_buff (without the
 * terminating nullbyte). The minimum required @p out_buff size is @ref
 * FLUF_U64_STR_MAX_LEN.
 *
 * @param      value    Input value.
 * @param[out] out_buff Output buffer.
 *
 * @return Number of bytes written.
 */
size_t fluf_uint64_to_string_value(uint64_t value, char *out_buff);

/**
 * Converts double value to string and copies it to @p out_buff (without the
 * terminating nullbyte). The minimum required @p out_buff size is @ref
 * FLUF_DOUBLE_STR_MAX_LEN.
 *
 * IMPORTANT: This function is used to encode LwM2M attributes whose
 * float/double format is defined by LwM2M Specification: 1*DIGIT ["."1*DIGIT].
 * However for absolute values greater than @ref UINT64_MAX and less than
 * <c>1e-10</c> exponential notation is used. Since the specification does not
 * define the format for the value of NaN and infinite, so in this case "nan"
 * and "inf" will be set.
 *
 * IMPORTANT: This function doesn't use sprintf() and is intended to be
 * lightweight. For very large and very small numbers, a rounding error may
 * occur.
 *
 * @param      value      Input value.
 * @param[out] out_buff   Output buffer.
 *
 * @return Number of bytes written.
 */
size_t fluf_double_to_simple_str_value(double value, char *out_buff);

/**
 * Converts string representation of numerical value to uint32_t value.
 *
 * @param      buff      Input buffer.
 * @param      buff_len  Input buffer length.
 * @param[out] out_val   Output value.
 *
 * @return 0 in case of success and -1 if @p buff_len is equal to 0 or there are
 * characters in the @p buff that are not digits.
 */
int fluf_string_to_uint32_value(const char *buff,
                                size_t buff_len,
                                uint32_t *out_val);

/**
 * Converts string representation of numerical value to double value. Does not
 * supports exponential notation, infinitive and NAN values (LwM2M attributes
 * representation doesn't allow for this).
 *
 * @param      buff      Input buffer.
 * @param      buff_len  Input buffer length.
 * @param[out] out_val   Output value.
 *
 * @return 0 in case of success and -1 if @p buff_len is equal to 0 or there are
 * characters in the @p buff that are not digits (exceptions shown above).
 */
int fluf_string_to_simple_double_value(const char *buff,
                                       size_t buff_len,
                                       double *out_val);

#ifdef __cplusplus
}
#endif

#endif // FLUF_UTILS_H
