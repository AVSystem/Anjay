/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
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

#include <anjay/anjay.h>

#include "../io.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef int *(*anjay_output_ctx_errno_ptr_t)(anjay_output_ctx_t *);
typedef anjay_ret_bytes_ctx_t *
(*anjay_output_ctx_bytes_begin_t)(anjay_output_ctx_t *, size_t);
typedef int (*anjay_output_ctx_string_t)(anjay_output_ctx_t *, const char *);
typedef int (*anjay_output_ctx_i32_t)(anjay_output_ctx_t *, int32_t);
typedef int (*anjay_output_ctx_i64_t)(anjay_output_ctx_t *, int64_t);
typedef int (*anjay_output_ctx_f32_t)(anjay_output_ctx_t *, float);
typedef int (*anjay_output_ctx_f64_t)(anjay_output_ctx_t *, double);
typedef int (*anjay_output_ctx_boolean_t)(anjay_output_ctx_t *, bool);
typedef int (*anjay_output_ctx_objlnk_t)(anjay_output_ctx_t *,
                                         anjay_oid_t,
                                         anjay_iid_t);
typedef anjay_output_ctx_t *
(*anjay_output_ctx_array_start_t)(anjay_output_ctx_t *);
typedef int (*anjay_output_ctx_array_index_t)(anjay_output_ctx_t *,
                                              anjay_riid_t);
typedef int (*anjay_output_ctx_array_finish_t)(anjay_output_ctx_t *);
typedef anjay_output_ctx_t *
(*anjay_output_ctx_object_start_t)(anjay_output_ctx_t *);
typedef int (*anjay_output_ctx_object_finish_t)(anjay_output_ctx_t *);
typedef int (*anjay_output_ctx_set_id_t)(anjay_output_ctx_t *,
                                         anjay_id_type_t, uint16_t);
typedef int (*anjay_output_ctx_close_t)(anjay_output_ctx_t *);

typedef struct {
    anjay_output_ctx_errno_ptr_t errno_ptr;
    anjay_output_ctx_bytes_begin_t bytes_begin;
    anjay_output_ctx_string_t string;
    anjay_output_ctx_i32_t i32;
    anjay_output_ctx_i64_t i64;
    anjay_output_ctx_f32_t f32;
    anjay_output_ctx_f64_t f64;
    anjay_output_ctx_boolean_t boolean;
    anjay_output_ctx_objlnk_t objlnk;
    anjay_output_ctx_array_start_t array_start;
    anjay_output_ctx_array_index_t array_index;
    anjay_output_ctx_array_finish_t array_finish;
    anjay_output_ctx_object_start_t object_start;
    anjay_output_ctx_object_finish_t object_finish;
    anjay_output_ctx_set_id_t set_id;
    anjay_output_ctx_close_t close;
} anjay_output_ctx_vtable_t;

typedef int (*anjay_ret_bytes_ctx_append_t)(anjay_ret_bytes_ctx_t *,
                                            const void *,
                                            size_t);

typedef struct {
    anjay_ret_bytes_ctx_append_t append;
} anjay_ret_bytes_ctx_vtable_t;

typedef int (*anjay_input_ctx_bytes_t)(anjay_input_ctx_t *,
                                       size_t *, bool *, void *, size_t);
typedef int (*anjay_input_ctx_string_t)(anjay_input_ctx_t *, char *, size_t);
typedef int (*anjay_input_ctx_i32_t)(anjay_input_ctx_t *, int32_t *);
typedef int (*anjay_input_ctx_i64_t)(anjay_input_ctx_t *, int64_t *);
typedef int (*anjay_input_ctx_f32_t)(anjay_input_ctx_t *, float *);
typedef int (*anjay_input_ctx_f64_t)(anjay_input_ctx_t *, double *);
typedef int (*anjay_input_ctx_boolean_t)(anjay_input_ctx_t *, bool *);
typedef int (*anjay_input_ctx_objlnk_t)(anjay_input_ctx_t *,
                                        anjay_oid_t *, anjay_iid_t *);
typedef int (*anjay_input_ctx_attach_child_t)(anjay_input_ctx_t *,
                                              anjay_input_ctx_t *);
typedef int (*anjay_input_ctx_get_id_t)(anjay_input_ctx_t *,
                                        anjay_id_type_t *, uint16_t *);
typedef int (*anjay_input_ctx_next_entry_t)(anjay_input_ctx_t *);
typedef int (*anjay_input_ctx_close_t)(anjay_input_ctx_t *);

typedef struct {
    anjay_input_ctx_bytes_t some_bytes;
    anjay_input_ctx_string_t string;
    anjay_input_ctx_i32_t i32;
    anjay_input_ctx_i64_t i64;
    anjay_input_ctx_f32_t f32;
    anjay_input_ctx_f64_t f64;
    anjay_input_ctx_boolean_t boolean;
    anjay_input_ctx_objlnk_t objlnk;
    anjay_input_ctx_attach_child_t attach_child;
    anjay_input_ctx_get_id_t get_id;
    anjay_input_ctx_next_entry_t next_entry;
    anjay_input_ctx_close_t close;
} anjay_input_ctx_vtable_t;

VISIBILITY_PRIVATE_HEADER_END

#endif	/* ANJAY_IO_VTABLE_H */

