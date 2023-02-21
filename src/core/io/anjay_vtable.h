/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_IO_VTABLE_H
#define ANJAY_IO_VTABLE_H

#include <anjay/core.h>

#include "../anjay_io_core.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef void (*anjay_dm_list_ctx_emit_t)(anjay_unlocked_dm_list_ctx_t *,
                                         uint16_t id);

typedef struct {
    anjay_dm_list_ctx_emit_t emit;
} anjay_dm_list_ctx_vtable_t;

typedef int (*anjay_output_ctx_bytes_begin_t)(
        anjay_unlocked_output_ctx_t *,
        size_t,
        anjay_unlocked_ret_bytes_ctx_t **);
typedef int (*anjay_output_ctx_string_t)(anjay_unlocked_output_ctx_t *,
                                         const char *);
typedef int (*anjay_output_ctx_integer_t)(anjay_unlocked_output_ctx_t *,
                                          int64_t);
#ifdef ANJAY_WITH_LWM2M11
typedef int (*anjay_output_ctx_uint_t)(anjay_unlocked_output_ctx_t *, uint64_t);
#endif // ANJAY_WITH_LWM2M11
typedef int (*anjay_output_ctx_floating_t)(anjay_unlocked_output_ctx_t *,
                                           double);
typedef int (*anjay_output_ctx_boolean_t)(anjay_unlocked_output_ctx_t *, bool);
typedef int (*anjay_output_ctx_objlnk_t)(anjay_unlocked_output_ctx_t *,
                                         anjay_oid_t,
                                         anjay_iid_t);
#if defined(ANJAY_WITH_SECURITY_STRUCTURED)
typedef int (*anjay_output_ctx_security_info_t)(
        anjay_unlocked_output_ctx_t *,
        const avs_crypto_security_info_union_t *);
#endif /* defined(ANJAY_WITH_SECURITY_STRUCTURED) || \
          defined(ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT) */
typedef int (*anjay_output_ctx_start_aggregate_t)(
        anjay_unlocked_output_ctx_t *);
typedef int (*anjay_output_ctx_set_path_t)(anjay_unlocked_output_ctx_t *,
                                           const anjay_uri_path_t *);
typedef int (*anjay_output_ctx_clear_path_t)(anjay_unlocked_output_ctx_t *);
typedef int (*anjay_output_ctx_set_time_t)(anjay_unlocked_output_ctx_t *,
                                           double);
typedef int (*anjay_output_ctx_close_t)(anjay_unlocked_output_ctx_t *);

struct anjay_output_ctx_vtable_struct {
    anjay_output_ctx_bytes_begin_t bytes_begin;
    anjay_output_ctx_string_t string;
    anjay_output_ctx_integer_t integer;
#ifdef ANJAY_WITH_LWM2M11
    anjay_output_ctx_uint_t uint;
#endif // ANJAY_WITH_LWM2M11
    anjay_output_ctx_floating_t floating;
    anjay_output_ctx_boolean_t boolean;
    anjay_output_ctx_objlnk_t objlnk;
#if defined(ANJAY_WITH_SECURITY_STRUCTURED)
    anjay_output_ctx_security_info_t security_info;
#endif /* defined(ANJAY_WITH_SECURITY_STRUCTURED) || \
          defined(ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT) */
    anjay_output_ctx_start_aggregate_t start_aggregate;
    anjay_output_ctx_set_path_t set_path;
    anjay_output_ctx_clear_path_t clear_path;
    anjay_output_ctx_set_time_t set_time;
    anjay_output_ctx_close_t close;
};

typedef int (*anjay_ret_bytes_ctx_append_t)(anjay_unlocked_ret_bytes_ctx_t *,
                                            const void *,
                                            size_t);

typedef struct {
    anjay_ret_bytes_ctx_append_t append;
} anjay_ret_bytes_ctx_vtable_t;

typedef int (*anjay_input_ctx_bytes_t)(
        anjay_unlocked_input_ctx_t *, size_t *, bool *, void *, size_t);
typedef int (*anjay_input_ctx_string_t)(anjay_unlocked_input_ctx_t *,
                                        char *,
                                        size_t);
typedef int (*anjay_input_ctx_integer_t)(anjay_unlocked_input_ctx_t *,
                                         int64_t *);
#ifdef ANJAY_WITH_LWM2M11
typedef int (*anjay_input_ctx_uint_t)(anjay_unlocked_input_ctx_t *, uint64_t *);
#endif // ANJAY_WITH_LWM2M11
typedef int (*anjay_input_ctx_floating_t)(anjay_unlocked_input_ctx_t *,
                                          double *);
typedef int (*anjay_input_ctx_boolean_t)(anjay_unlocked_input_ctx_t *, bool *);
typedef int (*anjay_input_ctx_objlnk_t)(anjay_unlocked_input_ctx_t *,
                                        anjay_oid_t *,
                                        anjay_iid_t *);
typedef int (*anjay_input_ctx_next_entry_t)(anjay_unlocked_input_ctx_t *);
typedef int (*anjay_input_ctx_close_t)(anjay_unlocked_input_ctx_t *);

typedef int (*anjay_input_ctx_get_path_t)(anjay_unlocked_input_ctx_t *,
                                          anjay_uri_path_t *,
                                          bool *);
typedef int (*anjay_input_ctx_update_root_path_t)(anjay_unlocked_input_ctx_t *,
                                                  const anjay_uri_path_t *);

struct anjay_input_ctx_vtable_struct {
    anjay_input_ctx_bytes_t some_bytes;
    anjay_input_ctx_string_t string;
    anjay_input_ctx_integer_t integer;
#ifdef ANJAY_WITH_LWM2M11
    anjay_input_ctx_uint_t uint;
#endif // ANJAY_WITH_LWM2M11
    anjay_input_ctx_floating_t floating;
    anjay_input_ctx_boolean_t boolean;
    anjay_input_ctx_objlnk_t objlnk;
    anjay_input_ctx_get_path_t get_path;
    anjay_input_ctx_next_entry_t next_entry;
    anjay_input_ctx_update_root_path_t update_root_path;
    anjay_input_ctx_close_t close;
};

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_IO_VTABLE_H */
