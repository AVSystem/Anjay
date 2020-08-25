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

#ifndef ANJAY_IO_VTABLE_H
#define ANJAY_IO_VTABLE_H

#include <anjay/core.h>

#include "../anjay_io_core.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef void (*anjay_dm_list_ctx_emit_t)(anjay_dm_list_ctx_t *, uint16_t id);

typedef struct {
    anjay_dm_list_ctx_emit_t emit;
} anjay_dm_list_ctx_vtable_t;

typedef int (*anjay_output_ctx_bytes_begin_t)(anjay_output_ctx_t *,
                                              size_t,
                                              anjay_ret_bytes_ctx_t **);
typedef int (*anjay_output_ctx_string_t)(anjay_output_ctx_t *, const char *);
typedef int (*anjay_output_ctx_integer_t)(anjay_output_ctx_t *, int64_t);
typedef int (*anjay_output_ctx_floating_t)(anjay_output_ctx_t *, double);
typedef int (*anjay_output_ctx_boolean_t)(anjay_output_ctx_t *, bool);
typedef int (*anjay_output_ctx_objlnk_t)(anjay_output_ctx_t *,
                                         anjay_oid_t,
                                         anjay_iid_t);
typedef int (*anjay_output_ctx_start_aggregate_t)(anjay_output_ctx_t *);
typedef int (*anjay_output_ctx_set_path_t)(anjay_output_ctx_t *,
                                           const anjay_uri_path_t *);
typedef int (*anjay_output_ctx_clear_path_t)(anjay_output_ctx_t *);
typedef int (*anjay_output_ctx_set_time_t)(anjay_output_ctx_t *, double);
typedef int (*anjay_output_ctx_close_t)(anjay_output_ctx_t *);

struct anjay_output_ctx_vtable_struct {
    anjay_output_ctx_bytes_begin_t bytes_begin;
    anjay_output_ctx_string_t string;
    anjay_output_ctx_integer_t integer;
    anjay_output_ctx_floating_t floating;
    anjay_output_ctx_boolean_t boolean;
    anjay_output_ctx_objlnk_t objlnk;
    anjay_output_ctx_start_aggregate_t start_aggregate;
    anjay_output_ctx_set_path_t set_path;
    anjay_output_ctx_clear_path_t clear_path;
    anjay_output_ctx_set_time_t set_time;
    anjay_output_ctx_close_t close;
};

typedef int (*anjay_ret_bytes_ctx_append_t)(anjay_ret_bytes_ctx_t *,
                                            const void *,
                                            size_t);

typedef struct {
    anjay_ret_bytes_ctx_append_t append;
} anjay_ret_bytes_ctx_vtable_t;

typedef int (*anjay_input_ctx_bytes_t)(
        anjay_input_ctx_t *, size_t *, bool *, void *, size_t);
typedef int (*anjay_input_ctx_string_t)(anjay_input_ctx_t *, char *, size_t);
typedef int (*anjay_input_ctx_integer_t)(anjay_input_ctx_t *, int64_t *);
typedef int (*anjay_input_ctx_floating_t)(anjay_input_ctx_t *, double *);
typedef int (*anjay_input_ctx_boolean_t)(anjay_input_ctx_t *, bool *);
typedef int (*anjay_input_ctx_objlnk_t)(anjay_input_ctx_t *,
                                        anjay_oid_t *,
                                        anjay_iid_t *);
typedef int (*anjay_input_ctx_next_entry_t)(anjay_input_ctx_t *);
typedef int (*anjay_input_ctx_close_t)(anjay_input_ctx_t *);

typedef int (*anjay_input_ctx_get_path_t)(anjay_input_ctx_t *,
                                          anjay_uri_path_t *,
                                          bool *);
typedef int (*anjay_input_ctx_update_root_path_t)(anjay_input_ctx_t *,
                                                  const anjay_uri_path_t *);

struct anjay_input_ctx_vtable_struct {
    anjay_input_ctx_bytes_t some_bytes;
    anjay_input_ctx_string_t string;
    anjay_input_ctx_integer_t integer;
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
