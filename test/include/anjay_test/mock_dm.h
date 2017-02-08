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

#ifndef ANJAY_TEST_MOCK_DM_H
#define	ANJAY_TEST_MOCK_DM_H

#include <anjay/anjay.h>

typedef enum {
    MOCK_DATA_NONE,
    MOCK_DATA_BYTES,
    MOCK_DATA_STRING,
    MOCK_DATA_INT,
    MOCK_DATA_FLOAT,
    MOCK_DATA_BOOL,
    MOCK_DATA_OBJLNK,
    MOCK_DATA_ARRAY,
    MOCK_DATA_ARRAY_NOFINISH
} anjay_mock_dm_data_type_t;

typedef struct anjay_mock_dm_data_array_struct anjay_mock_dm_data_array_t;

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
    const anjay_mock_dm_data_array_t *const *array;
} anjay_mock_dm_data_value_t;

typedef struct {
    anjay_mock_dm_data_type_t type;
    anjay_mock_dm_data_value_t data;
    int expected_retval;
} anjay_mock_dm_data_t;

struct anjay_mock_dm_data_array_struct {
    anjay_riid_t index;
    anjay_mock_dm_data_t value;
};

#define ANJAY_MOCK_DM_NONE (&(const anjay_mock_dm_data_t) { \
    .type = MOCK_DATA_NONE \
})

#pragma GCC diagnostic ignored "-Wunused-value"

#define ANJAY_MOCK_DM_BYTES(Retval, Str) (&(const anjay_mock_dm_data_t) { \
    .type = MOCK_DATA_BYTES, \
    .data = { \
        .bytes = { \
            .data = Str, \
            .length = sizeof(Str) - 1 \
        } \
    }, \
    .expected_retval = Retval \
})

#define ANJAY_MOCK_DM_STRING(Retval, Str) (&(const anjay_mock_dm_data_t) {\
    .type = MOCK_DATA_STRING, \
    .data = { \
        .str = Str \
    }, \
    .expected_retval = Retval \
})

#define ANJAY_MOCK_DM_INT(Retval, Value) (&(const anjay_mock_dm_data_t) { \
    .type = MOCK_DATA_INT, \
    .data = { \
        .i = Value \
    }, \
    .expected_retval = Retval \
})

#define ANJAY_MOCK_DM_FLOAT(Retval, Value) \
(&(const anjay_mock_dm_data_t) { \
    .type = MOCK_DATA_FLOAT, \
    .data = { \
        .f = Value \
    }, \
    .expected_retval = Retval \
})

#define ANJAY_MOCK_DM_BOOL(Retval, Value) (&(const anjay_mock_dm_data_t) {\
    .type = MOCK_DATA_BOOL, \
    .data = { \
        .b = Value \
    }, \
    .expected_retval = Retval \
})

#define ANJAY_MOCK_DM_OBJLNK(Retval, Oid, Iid) \
(&(const anjay_mock_dm_data_t) { \
    .type = MOCK_DATA_OBJLNK, \
    .data = { \
        .objlnk = { \
            .oid = Oid, \
            .iid = Iid \
        } \
    }, \
    .expected_retval = Retval \
})

#define ANJAY_MOCK_DM_VARARG0_IMPL__(Arg, ...) Arg
#define ANJAY_MOCK_DM_VARARG0__(...) \
        ANJAY_MOCK_DM_VARARG0_IMPL__(__VA_ARGS__, _)

#define ANJAY_MOCK_DM_ARRAY_REST__(Arg, ...) __VA_ARGS__

#define ANJAY_MOCK_DM_ARRAY_COMMON__(Type, ...) \
(&(const anjay_mock_dm_data_t) { \
    .type = Type, \
    .data = { \
        .array = &(const anjay_mock_dm_data_array_t *const []) { \
            ANJAY_MOCK_DM_ARRAY_REST__(__VA_ARGS__, NULL) \
        }[0] \
    }, \
    .expected_retval = ANJAY_MOCK_DM_VARARG0__(__VA_ARGS__) \
})

#define ANJAY_MOCK_DM_ARRAY(...) \
        ANJAY_MOCK_DM_ARRAY_COMMON__(MOCK_DATA_ARRAY, __VA_ARGS__)
#define ANJAY_MOCK_DM_ARRAY_NOFINISH(...) \
        ANJAY_MOCK_DM_ARRAY_COMMON__(MOCK_DATA_ARRAY, __VA_ARGS__)

#define ANJAY_MOCK_DM_ARRAY_ENTRY(...) (&(const anjay_mock_dm_data_array_t) { \
    .index = ANJAY_MOCK_DM_VARARG0__(__VA_ARGS__), \
    .value = *ANJAY_MOCK_DM_ARRAY_REST__(__VA_ARGS__) \
})

void _anjay_mock_dm_assert_attributes_equal(const anjay_dm_attributes_t *a,
                                            const anjay_dm_attributes_t *b);

anjay_dm_object_read_default_attrs_t _anjay_mock_dm_object_read_default_attrs;
anjay_dm_object_write_default_attrs_t _anjay_mock_dm_object_write_default_attrs;
anjay_dm_instance_reset_t _anjay_mock_dm_instance_reset;
anjay_dm_instance_it_t _anjay_mock_dm_instance_it;
anjay_dm_instance_present_t _anjay_mock_dm_instance_present;
anjay_dm_instance_create_t _anjay_mock_dm_instance_create;
anjay_dm_instance_remove_t _anjay_mock_dm_instance_remove;
anjay_dm_instance_read_default_attrs_t _anjay_mock_dm_instance_read_default_attrs;
anjay_dm_instance_write_default_attrs_t _anjay_mock_dm_instance_write_default_attrs;
anjay_dm_resource_present_t _anjay_mock_dm_resource_present;
anjay_dm_resource_read_t _anjay_mock_dm_resource_read;
anjay_dm_resource_write_t _anjay_mock_dm_resource_write;
anjay_dm_resource_execute_t _anjay_mock_dm_resource_execute;
anjay_dm_resource_dim_t _anjay_mock_dm_resource_dim;
anjay_dm_resource_read_attrs_t _anjay_mock_dm_resource_read_attrs;
anjay_dm_resource_write_attrs_t _anjay_mock_dm_resource_write_attrs;
anjay_dm_resource_supported_t _anjay_mock_dm_resource_supported;
anjay_dm_resource_operations_t _anjay_mock_dm_resource_operations;

#define ANJAY_MOCK_DM_HANDLERS_NOATTRS \
    .instance_it = _anjay_mock_dm_instance_it, \
    .instance_present = _anjay_mock_dm_instance_present, \
    .instance_create = _anjay_mock_dm_instance_create, \
    .instance_remove = _anjay_mock_dm_instance_remove, \
    .resource_present = _anjay_mock_dm_resource_present, \
    .resource_read = _anjay_mock_dm_resource_read, \
    .resource_write = _anjay_mock_dm_resource_write, \
    .resource_execute = _anjay_mock_dm_resource_execute, \
    .resource_dim = _anjay_mock_dm_resource_dim, \
    .resource_supported = _anjay_mock_dm_resource_supported

#define ANJAY_MOCK_DM_HANDLERS \
    ANJAY_MOCK_DM_HANDLERS_NOATTRS, \
    .object_read_default_attrs = _anjay_mock_dm_object_read_default_attrs, \
    .object_write_default_attrs = _anjay_mock_dm_object_write_default_attrs, \
    .instance_read_default_attrs = _anjay_mock_dm_instance_read_default_attrs, \
    .instance_write_default_attrs = _anjay_mock_dm_instance_write_default_attrs, \
    .resource_read_attrs = _anjay_mock_dm_resource_read_attrs, \
    .resource_write_attrs = _anjay_mock_dm_resource_write_attrs, \
    .transaction_begin = anjay_dm_transaction_NOOP, \
    .transaction_validate = anjay_dm_transaction_NOOP, \
    .transaction_commit = anjay_dm_transaction_NOOP, \
    .transaction_rollback = anjay_dm_transaction_NOOP \

void _anjay_mock_dm_expect_object_read_default_attrs(anjay_t *anjay,
                                                     const anjay_dm_object_def_t *const *obj_ptr,
                                                     anjay_ssid_t ssid,
                                                     int retval,
                                                     const anjay_dm_attributes_t *attrs);
void _anjay_mock_dm_expect_object_write_default_attrs(anjay_t *anjay,
                                                      const anjay_dm_object_def_t *const *obj_ptr,
                                                      anjay_ssid_t ssid,
                                                      const anjay_dm_attributes_t *attrs,
                                                      int retval);
void _anjay_mock_dm_expect_instance_reset(anjay_t *anjay,
                                          const anjay_dm_object_def_t *const *obj_ptr,
                                          anjay_iid_t iid,
                                          int retval);
void _anjay_mock_dm_expect_instance_it(anjay_t *anjay,
                                       const anjay_dm_object_def_t *const *obj_ptr,
                                       uintptr_t iteration,
                                       int retval,
                                       anjay_iid_t out);
void _anjay_mock_dm_expect_instance_present(anjay_t *anjay,
                                            const anjay_dm_object_def_t *const *obj_ptr,
                                            anjay_iid_t iid,
                                            int retval);
void _anjay_mock_dm_expect_instance_create(anjay_t *anjay,
                                           const anjay_dm_object_def_t *const *obj_ptr,
                                           anjay_iid_t iid,
                                           anjay_ssid_t ssid,
                                           int retval,
                                           anjay_iid_t out_iid);
void _anjay_mock_dm_expect_instance_remove(anjay_t *anjay,
                                           const anjay_dm_object_def_t *const *obj_ptr,
                                           anjay_iid_t iid,
                                           int retval);
void _anjay_mock_dm_expect_instance_read_default_attrs(anjay_t *anjay,
                                                       const anjay_dm_object_def_t *const *obj_ptr,
                                                       anjay_iid_t iid,
                                                       anjay_ssid_t ssid,
                                                       int retval,
                                                       const anjay_dm_attributes_t *attrs);
void _anjay_mock_dm_expect_instance_write_default_attrs(anjay_t *anjay,
                                                        const anjay_dm_object_def_t *const *obj_ptr,
                                                        anjay_iid_t iid,
                                                        anjay_ssid_t ssid,
                                                        const anjay_dm_attributes_t *attrs,
                                                        int retval);
void _anjay_mock_dm_expect_resource_present(anjay_t *anjay,
                                            const anjay_dm_object_def_t *const *obj_ptr,
                                            anjay_iid_t iid,
                                            anjay_rid_t rid,
                                            int retval);
void _anjay_mock_dm_expect_resource_supported(anjay_t *anjay,
                                              const anjay_dm_object_def_t *const *obj_ptr,
                                              anjay_rid_t rid,
                                              int retval);
void _anjay_mock_dm_expect_resource_operations(anjay_t *anjay,
                                               const anjay_dm_object_def_t *const *obj_ptr,
                                               anjay_rid_t rid,
                                               anjay_dm_resource_op_mask_t mask,
                                               int retval);
void _anjay_mock_dm_expect_resource_read(anjay_t *anjay,
                                         const anjay_dm_object_def_t *const *obj_ptr,
                                         anjay_iid_t iid,
                                         anjay_rid_t rid,
                                         int retval,
                                         const anjay_mock_dm_data_t *data);
void _anjay_mock_dm_expect_resource_write(anjay_t *anjay,
                                          const anjay_dm_object_def_t *const *obj_ptr,
                                          anjay_iid_t iid,
                                          anjay_rid_t rid,
                                          const anjay_mock_dm_data_t *data,
                                          int retval);
void _anjay_mock_dm_expect_resource_execute(anjay_t *anjay,
                                            const anjay_dm_object_def_t *const *obj_ptr,
                                            anjay_iid_t iid,
                                            anjay_rid_t rid,
                                            const anjay_mock_dm_data_t *data,
                                            int retval);
void _anjay_mock_dm_expect_resource_dim(anjay_t *anjay,
                                        const anjay_dm_object_def_t *const *obj_ptr,
                                        anjay_iid_t iid,
                                        anjay_rid_t rid,
                                        int retval);
void _anjay_mock_dm_expect_resource_read_attrs(anjay_t *anjay,
                                               const anjay_dm_object_def_t *const *obj_ptr,
                                               anjay_iid_t iid,
                                               anjay_rid_t rid,
                                               anjay_ssid_t ssid,
                                               int retval,
                                               const anjay_dm_attributes_t *attrs);
void _anjay_mock_dm_expect_resource_write_attrs(anjay_t *anjay,
                                                const anjay_dm_object_def_t *const *obj_ptr,
                                                anjay_iid_t iid,
                                                anjay_rid_t rid,
                                                anjay_ssid_t ssid,
                                                const anjay_dm_attributes_t *attrs,
                                                int retval);
void _anjay_mock_dm_expect_clean(void);
void _anjay_mock_dm_expected_commands_clear(void);

#endif	/* ANJAY_TEST_MOCK_DM_H */

