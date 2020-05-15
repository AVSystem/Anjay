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

#ifndef ANJAY_TEST_MOCK_DM_H
#define ANJAY_TEST_MOCK_DM_H

#include <anjay/dm.h>

#include <anjay_modules/dm/anjay_attributes.h>

typedef enum {
    MOCK_DATA_NONE,
    MOCK_DATA_BYTES,
    MOCK_DATA_STRING,
    MOCK_DATA_INT,
    MOCK_DATA_FLOAT,
    MOCK_DATA_BOOL,
    MOCK_DATA_OBJLNK
} anjay_mock_dm_data_type_t;

typedef union {
    struct {
        const void *data;
        size_t length;
    } bytes;
    const char *str;
    int64_t i;
    double f;
    bool b;
    struct {
        anjay_oid_t oid;
        anjay_iid_t iid;
    } objlnk;
} anjay_mock_dm_data_value_t;

typedef struct {
    anjay_mock_dm_data_type_t type;
    anjay_mock_dm_data_value_t data;
    int expected_retval;
} anjay_mock_dm_data_t;

#define ANJAY_MOCK_DM_NONE           \
    (&(const anjay_mock_dm_data_t) { \
        .type = MOCK_DATA_NONE       \
    })

#pragma GCC diagnostic ignored "-Wunused-value"

#define ANJAY_MOCK_DM_BYTES(Retval, Str)  \
    (&(const anjay_mock_dm_data_t) {      \
        .type = MOCK_DATA_BYTES,          \
        .data = {                         \
            .bytes = {                    \
                .data = Str,              \
                .length = sizeof(Str) - 1 \
            }                             \
        },                                \
        .expected_retval = Retval         \
    })

#define ANJAY_MOCK_DM_STRING(Retval, Str) \
    (&(const anjay_mock_dm_data_t) {      \
        .type = MOCK_DATA_STRING,         \
        .data = {                         \
            .str = Str                    \
        },                                \
        .expected_retval = Retval         \
    })

#define ANJAY_MOCK_DM_INT(Retval, Value) \
    (&(const anjay_mock_dm_data_t) {     \
        .type = MOCK_DATA_INT,           \
        .data = {                        \
            .i = Value                   \
        },                               \
        .expected_retval = Retval        \
    })

#define ANJAY_MOCK_DM_FLOAT(Retval, Value) \
    (&(const anjay_mock_dm_data_t) {       \
        .type = MOCK_DATA_FLOAT,           \
        .data = {                          \
            .f = Value                     \
        },                                 \
        .expected_retval = Retval          \
    })

#define ANJAY_MOCK_DM_BOOL(Retval, Value) \
    (&(const anjay_mock_dm_data_t) {      \
        .type = MOCK_DATA_BOOL,           \
        .data = {                         \
            .b = Value                    \
        },                                \
        .expected_retval = Retval         \
    })

#define ANJAY_MOCK_DM_OBJLNK(Retval, Oid, Iid) \
    (&(const anjay_mock_dm_data_t) {           \
        .type = MOCK_DATA_OBJLNK,              \
        .data = {                              \
            .objlnk = {                        \
                .oid = Oid,                    \
                .iid = Iid                     \
            }                                  \
        },                                     \
        .expected_retval = Retval              \
    })

typedef struct {
    int expected_retval;
    int arg;
    const char *value;
} anjay_mock_dm_execute_arg_t;

#define ANJAY_MOCK_DM_EXECUTE_ARG(Retval, ...) \
    (&(const anjay_mock_dm_execute_arg_t) { Retval, __VA_ARGS__ })

typedef const anjay_mock_dm_execute_arg_t *anjay_mock_dm_execute_data_t[];

#define ANJAY_MOCK_DM_EXECUTE(...) \
    (&(const anjay_mock_dm_execute_arg_t *[]) { __VA_ARGS__, NULL })

void _anjay_mock_dm_assert_common_attributes_equal(
        const anjay_dm_internal_oi_attrs_t *a,
        const anjay_dm_internal_oi_attrs_t *b);

void _anjay_mock_dm_assert_attributes_equal(
        const anjay_dm_internal_r_attrs_t *a,
        const anjay_dm_internal_r_attrs_t *b);

typedef struct {
    anjay_rid_t rid;
    anjay_dm_resource_kind_t kind;
    anjay_dm_resource_presence_t presence;
} anjay_mock_dm_res_entry_t;

#define ANJAY_MOCK_DM_RES_END \
    { ANJAY_ID_INVALID, ANJAY_DM_RES_R, ANJAY_DM_RES_ABSENT }

anjay_dm_object_read_default_attrs_t _anjay_mock_dm_object_read_default_attrs;
anjay_dm_object_write_default_attrs_t _anjay_mock_dm_object_write_default_attrs;
anjay_dm_instance_reset_t _anjay_mock_dm_instance_reset;
anjay_dm_list_instances_t _anjay_mock_dm_list_instances;
anjay_dm_instance_create_t _anjay_mock_dm_instance_create;
anjay_dm_instance_remove_t _anjay_mock_dm_instance_remove;
anjay_dm_instance_read_default_attrs_t
        _anjay_mock_dm_instance_read_default_attrs;
anjay_dm_instance_write_default_attrs_t
        _anjay_mock_dm_instance_write_default_attrs;
anjay_dm_list_resources_t _anjay_mock_dm_list_resources;
anjay_dm_resource_read_t _anjay_mock_dm_resource_read;
anjay_dm_resource_write_t _anjay_mock_dm_resource_write;
anjay_dm_resource_execute_t _anjay_mock_dm_resource_execute;
anjay_dm_resource_reset_t _anjay_mock_dm_resource_reset;
anjay_dm_list_resource_instances_t _anjay_mock_dm_list_resource_instances;
anjay_dm_resource_read_attrs_t _anjay_mock_dm_resource_read_attrs;
anjay_dm_resource_write_attrs_t _anjay_mock_dm_resource_write_attrs;

#define ANJAY_MOCK_DM_HANDLERS_NOATTRS                   \
    .list_instances = _anjay_mock_dm_list_instances,     \
    .instance_create = _anjay_mock_dm_instance_create,   \
    .instance_remove = _anjay_mock_dm_instance_remove,   \
    .list_resources = _anjay_mock_dm_list_resources,     \
    .resource_read = _anjay_mock_dm_resource_read,       \
    .resource_write = _anjay_mock_dm_resource_write,     \
    .resource_execute = _anjay_mock_dm_resource_execute, \
    .resource_reset = _anjay_mock_dm_resource_reset,     \
    .list_resource_instances = _anjay_mock_dm_list_resource_instances

#define ANJAY_MOCK_DM_HANDLERS                                           \
    ANJAY_MOCK_DM_HANDLERS_NOATTRS,                                      \
            .object_read_default_attrs =                                 \
                    _anjay_mock_dm_object_read_default_attrs,            \
            .object_write_default_attrs =                                \
                    _anjay_mock_dm_object_write_default_attrs,           \
            .instance_read_default_attrs =                               \
                    _anjay_mock_dm_instance_read_default_attrs,          \
            .instance_write_default_attrs =                              \
                    _anjay_mock_dm_instance_write_default_attrs,         \
            .resource_read_attrs = _anjay_mock_dm_resource_read_attrs,   \
            .resource_write_attrs = _anjay_mock_dm_resource_write_attrs, \
            .transaction_begin = anjay_dm_transaction_NOOP,              \
            .transaction_validate = anjay_dm_transaction_NOOP,           \
            .transaction_commit = anjay_dm_transaction_NOOP,             \
            .transaction_rollback = anjay_dm_transaction_NOOP

void _anjay_mock_dm_expect_object_read_default_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_ssid_t ssid,
        int retval,
        const anjay_dm_internal_oi_attrs_t *attrs);
void _anjay_mock_dm_expect_object_write_default_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_ssid_t ssid,
        const anjay_dm_internal_oi_attrs_t *attrs,
        int retval);
void _anjay_mock_dm_expect_instance_reset(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        int retval);
void _anjay_mock_dm_expect_list_instances(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        int retval,
        const anjay_iid_t *iid_array);
void _anjay_mock_dm_expect_instance_create(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        int retval);
void _anjay_mock_dm_expect_instance_remove(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        int retval);
void _anjay_mock_dm_expect_instance_read_default_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_ssid_t ssid,
        int retval,
        const anjay_dm_internal_oi_attrs_t *attrs);
void _anjay_mock_dm_expect_instance_write_default_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_ssid_t ssid,
        const anjay_dm_internal_oi_attrs_t *attrs,
        int retval);
void _anjay_mock_dm_expect_list_resources(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        int retval,
        const anjay_mock_dm_res_entry_t *res_array);
void _anjay_mock_dm_expect_resource_read(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_riid_t riid,
        int retval,
        const anjay_mock_dm_data_t *data);
void _anjay_mock_dm_expect_resource_write(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_riid_t riid,
        const anjay_mock_dm_data_t *data,
        int retval);
void _anjay_mock_dm_expect_resource_execute(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        const anjay_mock_dm_execute_data_t *data,
        int retval);
void _anjay_mock_dm_expect_resource_reset(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        int retval);
void _anjay_mock_dm_expect_list_resource_instances(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        int retval,
        const anjay_riid_t *riid_array);
void _anjay_mock_dm_expect_resource_read_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_ssid_t ssid,
        int retval,
        const anjay_dm_internal_r_attrs_t *attrs);
void _anjay_mock_dm_expect_resource_write_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_ssid_t ssid,
        const anjay_dm_internal_r_attrs_t *attrs,
        int retval);
void _anjay_mock_dm_expect_resource_instance_read_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_riid_t riid,
        anjay_ssid_t ssid,
        int retval,
        const anjay_dm_internal_r_attrs_t *attrs);
void _anjay_mock_dm_expect_resource_instance_write_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_riid_t riid,
        anjay_ssid_t ssid,
        const anjay_dm_internal_r_attrs_t *attrs,
        int retval);
void _anjay_mock_dm_expect_clean(void);
void _anjay_mock_dm_expected_commands_clear(void);

#endif /* ANJAY_TEST_MOCK_DM_H */
