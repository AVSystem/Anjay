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

#include <config.h>

#include <stdbool.h>
#include <string.h>

#include <avsystem/commons/list.h>
#include <avsystem/commons/unit/test.h>

#include <anjay_test/mock_dm.h>

#include "../../src/dm/execute.h"

typedef enum {
    MOCK_DM_OBJECT_READ_DEFAULT_ATTRS,
    MOCK_DM_OBJECT_WRITE_DEFAULT_ATTRS,
    MOCK_DM_INSTANCE_RESET,
    MOCK_DM_INSTANCE_IT,
    MOCK_DM_INSTANCE_PRESENT,
    MOCK_DM_INSTANCE_CREATE,
    MOCK_DM_INSTANCE_REMOVE,
    MOCK_DM_INSTANCE_READ_DEFAULT_ATTRS,
    MOCK_DM_INSTANCE_WRITE_DEFAULT_ATTRS,
    MOCK_DM_RESOURCE_PRESENT,
    MOCK_DM_RESOURCE_SUPPORTED,
    MOCK_DM_RESOURCE_OPERATIONS,
    MOCK_DM_RESOURCE_READ,
    MOCK_DM_RESOURCE_WRITE,
    MOCK_DM_RESOURCE_EXECUTE,
    MOCK_DM_RESOURCE_DIM,
    MOCK_DM_RESOURCE_READ_ATTRS,
    MOCK_DM_RESOURCE_WRITE_ATTRS
} anjay_mock_dm_expected_command_type_t;

typedef struct {
    anjay_mock_dm_expected_command_type_t command;
    anjay_t *anjay;
    const anjay_dm_object_def_t *const *obj_ptr;
    union {
        uintptr_t iteration;
        anjay_iid_t iid;
        anjay_rid_t rid;
        anjay_ssid_t ssid;
        struct {
            anjay_iid_t iid;
            anjay_rid_t rid;
        } iid_and_rid;
        struct {
            anjay_ssid_t ssid;
            anjay_iid_t iid;
        } ssid_and_iid;
        struct {
            anjay_ssid_t ssid;
            anjay_iid_t iid;
            anjay_rid_t rid;
        } ssid_iid_rid;
    } input;
    union {
        anjay_iid_t output_iid;
        anjay_mock_dm_data_t data;
        anjay_dm_attributes_t attributes;
        anjay_dm_resource_op_mask_t mask;
    } value;
    int retval;
} anjay_mock_dm_expected_command_t;

static AVS_LIST(anjay_mock_dm_expected_command_t) EXPECTED_COMMANDS;

#define DM_ACTION_COMMON(UName) do { \
    AVS_UNIT_ASSERT_NOT_NULL(EXPECTED_COMMANDS); \
    AVS_UNIT_ASSERT_TRUE(EXPECTED_COMMANDS->command == MOCK_DM_##UName); \
    AVS_UNIT_ASSERT_TRUE(EXPECTED_COMMANDS->anjay == anjay); \
    AVS_UNIT_ASSERT_TRUE(EXPECTED_COMMANDS->obj_ptr == obj_ptr); \
} while (0)

#define DM_ACTION_RETURN do { \
    int retval = EXPECTED_COMMANDS->retval; \
    AVS_LIST_DELETE(&EXPECTED_COMMANDS); \
    return retval; \
} while (0)

void _anjay_mock_dm_assert_attributes_equal(const anjay_dm_attributes_t *a,
                                            const anjay_dm_attributes_t *b) {
    AVS_UNIT_ASSERT_FIELD_EQUAL(a, b, min_period);
    AVS_UNIT_ASSERT_FIELD_EQUAL(a, b, max_period);
    AVS_UNIT_ASSERT_FIELD_EQUAL(a, b, greater_than);
    AVS_UNIT_ASSERT_FIELD_EQUAL(a, b, less_than);
    AVS_UNIT_ASSERT_FIELD_EQUAL(a, b, step);
}

int _anjay_mock_dm_object_read_default_attrs(anjay_t *anjay,
                                             const anjay_dm_object_def_t *const *obj_ptr,
                                             anjay_ssid_t ssid,
                                             anjay_dm_attributes_t *out) {
    DM_ACTION_COMMON(OBJECT_READ_DEFAULT_ATTRS);
    EXPECTED_COMMANDS->input.ssid = ssid;
    *out = EXPECTED_COMMANDS->value.attributes;
    DM_ACTION_RETURN;
}

int _anjay_mock_dm_object_write_default_attrs(anjay_t *anjay,
                                              const anjay_dm_object_def_t *const *obj_ptr,
                                              anjay_ssid_t ssid,
                                              const anjay_dm_attributes_t *attrs) {
    DM_ACTION_COMMON(OBJECT_WRITE_DEFAULT_ATTRS);
    EXPECTED_COMMANDS->input.ssid = ssid;
    _anjay_mock_dm_assert_attributes_equal(
            attrs, &EXPECTED_COMMANDS->value.attributes);
    DM_ACTION_RETURN;
}

int _anjay_mock_dm_instance_reset(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr,
                                  anjay_iid_t iid) {
    DM_ACTION_COMMON(INSTANCE_RESET);
    AVS_UNIT_ASSERT_EQUAL(iid, EXPECTED_COMMANDS->input.iid);
    DM_ACTION_RETURN;
}

int _anjay_mock_dm_instance_it(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t *out,
                               void **cookie) {
    DM_ACTION_COMMON(INSTANCE_IT);
    AVS_UNIT_ASSERT_EQUAL((*(uintptr_t *) cookie)++, EXPECTED_COMMANDS->input.iteration);
    *out = EXPECTED_COMMANDS->value.output_iid;
    DM_ACTION_RETURN;
}

#define INSTANCE_ACTION(LName, UName) \
int _anjay_mock_dm_instance_##LName (anjay_t *anjay, \
                                     const anjay_dm_object_def_t *const *obj_ptr, \
                                     anjay_iid_t iid) { \
    DM_ACTION_COMMON(INSTANCE_##UName); \
    AVS_UNIT_ASSERT_EQUAL(iid, EXPECTED_COMMANDS->input.iid); \
    DM_ACTION_RETURN; \
}

INSTANCE_ACTION(present, PRESENT)
INSTANCE_ACTION(remove, REMOVE)

int _anjay_mock_dm_instance_create(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_iid_t *inout_iid,
                                   anjay_ssid_t ssid) {
    DM_ACTION_COMMON(INSTANCE_CREATE);
    AVS_UNIT_ASSERT_EQUAL(*inout_iid, EXPECTED_COMMANDS->input.ssid_and_iid.iid);
    AVS_UNIT_ASSERT_EQUAL(ssid, EXPECTED_COMMANDS->input.ssid_and_iid.ssid);
    *inout_iid = EXPECTED_COMMANDS->value.output_iid;
    DM_ACTION_RETURN;
}

int _anjay_mock_dm_instance_read_default_attrs(anjay_t *anjay,
                                               const anjay_dm_object_def_t *const *obj_ptr,
                                               anjay_iid_t iid,
                                               anjay_ssid_t ssid,
                                               anjay_dm_attributes_t *out) {
    DM_ACTION_COMMON(INSTANCE_READ_DEFAULT_ATTRS);
    AVS_UNIT_ASSERT_EQUAL(ssid, EXPECTED_COMMANDS->input.ssid_and_iid.ssid);
    AVS_UNIT_ASSERT_EQUAL(iid, EXPECTED_COMMANDS->input.ssid_and_iid.iid);
    *out = EXPECTED_COMMANDS->value.attributes;
    DM_ACTION_RETURN;
}

int _anjay_mock_dm_instance_write_default_attrs(anjay_t *anjay,
                                                const anjay_dm_object_def_t *const *obj_ptr,
                                                anjay_iid_t iid,
                                                anjay_ssid_t ssid,
                                                const anjay_dm_attributes_t *attrs) {
    DM_ACTION_COMMON(INSTANCE_WRITE_DEFAULT_ATTRS);
    AVS_UNIT_ASSERT_EQUAL(ssid, EXPECTED_COMMANDS->input.ssid_and_iid.ssid);
    AVS_UNIT_ASSERT_EQUAL(iid, EXPECTED_COMMANDS->input.ssid_and_iid.iid);
    _anjay_mock_dm_assert_attributes_equal(
            attrs, &EXPECTED_COMMANDS->value.attributes);
    DM_ACTION_RETURN;
}

int _anjay_mock_dm_resource_present(anjay_t *anjay,
                                    const anjay_dm_object_def_t *const *obj_ptr,
                                    anjay_iid_t iid,
                                    anjay_rid_t rid) {
    DM_ACTION_COMMON(RESOURCE_PRESENT);
    AVS_UNIT_ASSERT_EQUAL(iid, EXPECTED_COMMANDS->input.iid_and_rid.iid);
    AVS_UNIT_ASSERT_EQUAL(rid, EXPECTED_COMMANDS->input.iid_and_rid.rid);
    DM_ACTION_RETURN;
}

int _anjay_mock_dm_resource_supported(anjay_t *anjay,
                                      const anjay_dm_object_def_t *const *obj_ptr,
                                      anjay_rid_t rid) {
    DM_ACTION_COMMON(RESOURCE_SUPPORTED);
    AVS_UNIT_ASSERT_EQUAL(rid, EXPECTED_COMMANDS->input.rid);
    DM_ACTION_RETURN;
}

int _anjay_mock_dm_resource_operations(anjay_t *anjay,
                                       const anjay_dm_object_def_t *const *obj_ptr,
                                       anjay_rid_t rid,
                                       anjay_dm_resource_op_mask_t *out) {
    DM_ACTION_COMMON(RESOURCE_OPERATIONS);
    AVS_UNIT_ASSERT_EQUAL(rid, EXPECTED_COMMANDS->input.rid);
    *out = EXPECTED_COMMANDS->value.mask;
    DM_ACTION_RETURN;
}

static int output_array(anjay_output_ctx_t *ctx,
                        const anjay_mock_dm_data_array_t *const *array,
                        bool finish);

static void perform_output(anjay_output_ctx_t *ctx,
                           const anjay_mock_dm_data_t *output) {
    int retval;
    switch (output->type) {
    case MOCK_DATA_NONE:
        return;
    case MOCK_DATA_BYTES:
        retval = anjay_ret_bytes(ctx, output->data.bytes.data,
                                 output->data.bytes.length);
        break;
    case MOCK_DATA_STRING:
        retval = anjay_ret_string(ctx, output->data.str);
        break;
    case MOCK_DATA_INT:
        retval = anjay_ret_i64(ctx, output->data.i);
        break;
    case MOCK_DATA_FLOAT:
        retval = anjay_ret_double(ctx, output->data.f);
        break;
    case MOCK_DATA_BOOL:
        retval = anjay_ret_bool(ctx, output->data.b);
        break;
    case MOCK_DATA_OBJLNK:
        retval = anjay_ret_objlnk(ctx, output->data.objlnk.oid,
                                  output->data.objlnk.iid);
        break;
    case MOCK_DATA_ARRAY:
        retval = output_array(ctx, output->data.array, true);
        break;
    case MOCK_DATA_ARRAY_NOFINISH:
        retval = output_array(ctx, output->data.array, false);
        break;
    }
    AVS_UNIT_ASSERT_EQUAL(retval, output->expected_retval);
}

static int output_array(anjay_output_ctx_t *ctx,
                        const anjay_mock_dm_data_array_t *const *array,
                        bool finish) {
    anjay_output_ctx_t *array_ctx = anjay_ret_array_start(ctx);
    if (!array_ctx) {
        AVS_UNIT_ASSERT_FALSE(array && *array);
        AVS_UNIT_ASSERT_FALSE(finish);
        return -1;
    }
    for (const anjay_mock_dm_data_array_t *const *entry = array;
            entry && *entry; ++entry) {
        int retval = anjay_ret_array_index(array_ctx, (*entry)->index);
        if (retval) {
            return retval;
        }
        perform_output(array_ctx, &(*entry)->value);
    }
    if (finish) {
        return anjay_ret_array_finish(array_ctx);
    } else {
        return 0;
    }
}

int _anjay_mock_dm_resource_read(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj_ptr,
                                 anjay_iid_t iid,
                                 anjay_rid_t rid,
                                 anjay_output_ctx_t *ctx) {
    DM_ACTION_COMMON(RESOURCE_READ);
    AVS_UNIT_ASSERT_EQUAL(iid, EXPECTED_COMMANDS->input.iid_and_rid.iid);
    AVS_UNIT_ASSERT_EQUAL(rid, EXPECTED_COMMANDS->input.iid_and_rid.rid);
    perform_output(ctx, &EXPECTED_COMMANDS->value.data);
    DM_ACTION_RETURN;
}

static int input_array(anjay_input_ctx_t *ctx,
                       const anjay_mock_dm_data_array_t *const *array);

static void perform_input(anjay_input_ctx_t *ctx,
                          const anjay_mock_dm_data_t *input) {
    int retval = 0;
    switch (input->type) {
    case MOCK_DATA_NONE:
        return;
    case MOCK_DATA_BYTES:
    {
        size_t bytes_read;
        bool message_finished;
        char buf[input->data.bytes.length];
        retval = anjay_get_bytes(ctx, &bytes_read, &message_finished,
                                 buf, sizeof (buf));
        if (!retval) {
            AVS_UNIT_ASSERT_EQUAL(bytes_read, sizeof (buf));
            AVS_UNIT_ASSERT_TRUE(message_finished);
            AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(buf, input->data.bytes.data,
                                              input->data.bytes.length);
        }
        break;
    }
    case MOCK_DATA_STRING:
    {
        char buf[strlen(input->data.str) + 1];
        retval = anjay_get_string(ctx, buf, sizeof(buf));
        if (!retval) {
            AVS_UNIT_ASSERT_EQUAL_STRING(buf, input->data.str);
        }
        break;
    }
    case MOCK_DATA_INT:
    {
        int64_t value;
        retval = anjay_get_i64(ctx, &value);
        if (!retval) {
            AVS_UNIT_ASSERT_EQUAL(value, input->data.i);
        }
        break;
    }
    case MOCK_DATA_FLOAT:
    {
        double value;
        retval = anjay_get_double(ctx, &value);
        if (!retval) {
            AVS_UNIT_ASSERT_EQUAL(value, input->data.f);
        }
        break;
    }
    case MOCK_DATA_BOOL:
    {
        bool value;
        retval = anjay_get_bool(ctx, &value);
        if (!retval) {
            AVS_UNIT_ASSERT_EQUAL(value, input->data.b);
        }
        break;
    }
    case MOCK_DATA_OBJLNK:
    {
        anjay_oid_t oid;
        anjay_iid_t iid;
        retval = anjay_get_objlnk(ctx, &oid, &iid);
        if (!retval) {
            AVS_UNIT_ASSERT_EQUAL(oid, input->data.objlnk.oid);
            AVS_UNIT_ASSERT_EQUAL(iid, input->data.objlnk.iid);
        }
        break;
    }
    case MOCK_DATA_ARRAY:
        retval = input_array(ctx, input->data.array);
        break;
    default:
        AVS_UNIT_ASSERT_NOT_EQUAL(input->type, input->type);
    }
    AVS_UNIT_ASSERT_EQUAL(retval, input->expected_retval);
}

static int input_array(anjay_input_ctx_t *ctx,
                       const anjay_mock_dm_data_array_t *const *array) {
    anjay_input_ctx_t *array_ctx = anjay_get_array(ctx);
    if (!array_ctx) {
        return -1;
    }
    for (const anjay_mock_dm_data_array_t *const *entry = array;
            entry && *entry; ++entry) {
        perform_input(array_ctx, &(*entry)->value);
    }
    return 0;
}

int _anjay_mock_dm_resource_write(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr,
                                  anjay_iid_t iid,
                                  anjay_rid_t rid,
                                  anjay_input_ctx_t *ctx) {
    DM_ACTION_COMMON(RESOURCE_WRITE);
    AVS_UNIT_ASSERT_EQUAL(iid, EXPECTED_COMMANDS->input.iid_and_rid.iid);
    AVS_UNIT_ASSERT_EQUAL(rid, EXPECTED_COMMANDS->input.iid_and_rid.rid);
    perform_input(ctx, &EXPECTED_COMMANDS->value.data);
    DM_ACTION_RETURN;
}

int _anjay_mock_dm_resource_execute(anjay_t *anjay,
                                    const anjay_dm_object_def_t *const *obj_ptr,
                                    anjay_iid_t iid,
                                    anjay_rid_t rid,
                                    anjay_execute_ctx_t *ctx) {
    DM_ACTION_COMMON(RESOURCE_EXECUTE);
    AVS_UNIT_ASSERT_EQUAL(iid, EXPECTED_COMMANDS->input.iid_and_rid.iid);
    AVS_UNIT_ASSERT_EQUAL(rid, EXPECTED_COMMANDS->input.iid_and_rid.rid);
    perform_input(ctx->input_ctx, &EXPECTED_COMMANDS->value.data);
    DM_ACTION_RETURN;
}

int _anjay_mock_dm_resource_dim(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr,
                                anjay_iid_t iid,
                                anjay_rid_t rid) {
    DM_ACTION_COMMON(RESOURCE_DIM);
    AVS_UNIT_ASSERT_EQUAL(iid, EXPECTED_COMMANDS->input.iid_and_rid.iid);
    AVS_UNIT_ASSERT_EQUAL(rid, EXPECTED_COMMANDS->input.iid_and_rid.rid);
    DM_ACTION_RETURN;
}

int _anjay_mock_dm_resource_read_attrs(anjay_t *anjay,
                                       const anjay_dm_object_def_t *const *obj_ptr,
                                       anjay_iid_t iid,
                                       anjay_rid_t rid,
                                       anjay_ssid_t ssid,
                                       anjay_dm_attributes_t *out) {
    DM_ACTION_COMMON(RESOURCE_READ_ATTRS);
    AVS_UNIT_ASSERT_EQUAL(ssid, EXPECTED_COMMANDS->input.ssid_iid_rid.ssid);
    AVS_UNIT_ASSERT_EQUAL(iid, EXPECTED_COMMANDS->input.ssid_iid_rid.iid);
    AVS_UNIT_ASSERT_EQUAL(rid, EXPECTED_COMMANDS->input.ssid_iid_rid.rid);
    *out = EXPECTED_COMMANDS->value.attributes;
    DM_ACTION_RETURN;
}

int _anjay_mock_dm_resource_write_attrs(anjay_t *anjay,
                                        const anjay_dm_object_def_t *const *obj_ptr,
                                        anjay_iid_t iid,
                                        anjay_rid_t rid,
                                        anjay_ssid_t ssid,
                                        const anjay_dm_attributes_t *attrs) {
    DM_ACTION_COMMON(RESOURCE_WRITE_ATTRS);
    AVS_UNIT_ASSERT_EQUAL(ssid, EXPECTED_COMMANDS->input.ssid_iid_rid.ssid);
    AVS_UNIT_ASSERT_EQUAL(iid, EXPECTED_COMMANDS->input.ssid_iid_rid.iid);
    AVS_UNIT_ASSERT_EQUAL(rid, EXPECTED_COMMANDS->input.ssid_iid_rid.rid);
    _anjay_mock_dm_assert_attributes_equal(attrs, &EXPECTED_COMMANDS->value.attributes);
    DM_ACTION_RETURN;
}

static anjay_mock_dm_expected_command_t *new_expected_command() {
    anjay_mock_dm_expected_command_t *new_command =
            AVS_LIST_NEW_ELEMENT(anjay_mock_dm_expected_command_t);
    AVS_UNIT_ASSERT_NOT_NULL(new_command);
    AVS_LIST_APPEND(&EXPECTED_COMMANDS, new_command);
    return new_command;
}

void _anjay_mock_dm_expect_object_read_default_attrs(anjay_t *anjay,
                                                     const anjay_dm_object_def_t *const *obj_ptr,
                                                     anjay_ssid_t ssid,
                                                     int retval,
                                                     const anjay_dm_attributes_t *attrs) {
    anjay_mock_dm_expected_command_t *command = new_expected_command();
    command->command = MOCK_DM_OBJECT_READ_DEFAULT_ATTRS;
    command->anjay = anjay;
    command->obj_ptr = obj_ptr;
    command->input.ssid = ssid;
    command->retval = retval;
    if (attrs) {
        command->value.attributes = *attrs;
    } else {
        AVS_UNIT_ASSERT_FAILED(retval);
    }
}

void _anjay_mock_dm_expect_object_write_default_attrs(anjay_t *anjay,
                                                      const anjay_dm_object_def_t *const *obj_ptr,
                                                      anjay_ssid_t ssid,
                                                      const anjay_dm_attributes_t *attrs,
                                                      int retval) {
    anjay_mock_dm_expected_command_t *command = new_expected_command();
    command->command = MOCK_DM_OBJECT_WRITE_DEFAULT_ATTRS;
    command->anjay = anjay;
    command->obj_ptr = obj_ptr;
    command->input.ssid = ssid;
    command->retval = retval;
    command->value.attributes = *attrs;
}

void _anjay_mock_dm_expect_instance_reset(anjay_t *anjay,
                                          const anjay_dm_object_def_t *const *obj_ptr,
                                          anjay_iid_t iid,
                                          int retval) {
    anjay_mock_dm_expected_command_t *command = new_expected_command();
    command->command = MOCK_DM_INSTANCE_RESET;
    command->anjay = anjay;
    command->obj_ptr = obj_ptr;
    command->input.iid = iid;
    command->retval = retval;
}

void _anjay_mock_dm_expect_instance_it(anjay_t *anjay,
                                       const anjay_dm_object_def_t *const *obj_ptr,
                                       uintptr_t iteration,
                                       int retval,
                                       anjay_iid_t out) {
    anjay_mock_dm_expected_command_t *command = new_expected_command();
    command->command = MOCK_DM_INSTANCE_IT;
    command->anjay = anjay;
    command->obj_ptr = obj_ptr;
    command->input.iteration = iteration;
    command->value.output_iid = out;
    command->retval = retval;
}

#define EXPECT_INSTANCE_ACTION(LName, UName) \
void _anjay_mock_dm_expect_instance_##LName (anjay_t *anjay, \
                                             const anjay_dm_object_def_t *const *obj_ptr, \
                                             anjay_iid_t iid, \
                                             int retval) { \
    anjay_mock_dm_expected_command_t *command = new_expected_command(); \
    command->command = MOCK_DM_INSTANCE_##UName; \
    command->anjay = anjay; \
    command->obj_ptr = obj_ptr; \
    command->input.iid = iid; \
    command->retval = retval; \
}

EXPECT_INSTANCE_ACTION(present, PRESENT)
EXPECT_INSTANCE_ACTION(remove, REMOVE)

void _anjay_mock_dm_expect_instance_create(anjay_t *anjay,
                                           const anjay_dm_object_def_t *const *obj_ptr,
                                           anjay_iid_t iid,
                                           anjay_ssid_t ssid,
                                           int retval,
                                           anjay_iid_t out_iid) {
    anjay_mock_dm_expected_command_t *command = new_expected_command();
    command->command = MOCK_DM_INSTANCE_CREATE;
    command->anjay = anjay;
    command->obj_ptr = obj_ptr;
    command->input.ssid_and_iid.iid = iid;
    command->input.ssid_and_iid.ssid = ssid;
    command->retval = retval;
    command->value.output_iid = out_iid;
}

void _anjay_mock_dm_expect_instance_read_default_attrs(anjay_t *anjay,
                                                       const anjay_dm_object_def_t *const *obj_ptr,
                                                       anjay_iid_t iid,
                                                       anjay_ssid_t ssid,
                                                       int retval,
                                                       const anjay_dm_attributes_t *attrs) {
    anjay_mock_dm_expected_command_t *command = new_expected_command();
    command->command = MOCK_DM_INSTANCE_READ_DEFAULT_ATTRS;
    command->anjay = anjay;
    command->obj_ptr = obj_ptr;
    command->input.ssid_and_iid.ssid = ssid;
    command->input.ssid_and_iid.iid = iid;
    command->retval = retval;
    if (attrs) {
        command->value.attributes = *attrs;
    } else {
        AVS_UNIT_ASSERT_FAILED(retval);
    }
}

void _anjay_mock_dm_expect_instance_write_default_attrs(anjay_t *anjay,
                                                        const anjay_dm_object_def_t *const *obj_ptr,
                                                        anjay_iid_t iid,
                                                        anjay_ssid_t ssid,
                                                        const anjay_dm_attributes_t *attrs,
                                                        int retval) {
    anjay_mock_dm_expected_command_t *command = new_expected_command();
    command->command = MOCK_DM_INSTANCE_WRITE_DEFAULT_ATTRS;
    command->anjay = anjay;
    command->obj_ptr = obj_ptr;
    command->input.ssid_and_iid.ssid = ssid;
    command->input.ssid_and_iid.iid = iid;
    command->retval = retval;
    command->value.attributes = *attrs;
}

#define EXPECT_RESOURCE_ACTION_COMMON(UName) \
    anjay_mock_dm_expected_command_t *command = new_expected_command(); \
    command->command = MOCK_DM_RESOURCE_##UName; \
    command->anjay = anjay; \
    command->obj_ptr = obj_ptr; \
    command->input.iid_and_rid.iid = iid; \
    command->input.iid_and_rid.rid = rid; \
    command->retval = retval;

void _anjay_mock_dm_expect_resource_present(anjay_t *anjay,
                                            const anjay_dm_object_def_t *const *obj_ptr,
                                            anjay_iid_t iid,
                                            anjay_rid_t rid,
                                            int retval) {
    EXPECT_RESOURCE_ACTION_COMMON(PRESENT);
}

void _anjay_mock_dm_expect_resource_supported(anjay_t *anjay,
                                              const anjay_dm_object_def_t *const *obj_ptr,
                                              anjay_rid_t rid,
                                              int retval) {
    anjay_mock_dm_expected_command_t *command = new_expected_command();
    command->command = MOCK_DM_RESOURCE_SUPPORTED;
    command->anjay = anjay;
    command->obj_ptr = obj_ptr;
    command->input.rid = rid;
    command->retval = retval;
}

void _anjay_mock_dm_expect_resource_operations(anjay_t *anjay,
                                               const anjay_dm_object_def_t *const *obj_ptr,
                                               anjay_rid_t rid,
                                               anjay_dm_resource_op_mask_t mask,
                                               int retval) {
    anjay_mock_dm_expected_command_t *command = new_expected_command();
    command->command = MOCK_DM_RESOURCE_OPERATIONS;
    command->anjay = anjay;
    command->obj_ptr = obj_ptr;
    command->input.rid = rid;
    command->retval = retval;
    command->value.mask = mask;
}

void _anjay_mock_dm_expect_resource_read(anjay_t *anjay,
                                         const anjay_dm_object_def_t *const *obj_ptr,
                                         anjay_iid_t iid,
                                         anjay_rid_t rid,
                                         int retval,
                                         const anjay_mock_dm_data_t *data) {
    EXPECT_RESOURCE_ACTION_COMMON(READ);
    command->value.data = *data;
}

void _anjay_mock_dm_expect_resource_write(anjay_t *anjay,
                                          const anjay_dm_object_def_t *const *obj_ptr,
                                          anjay_iid_t iid,
                                          anjay_rid_t rid,
                                          const anjay_mock_dm_data_t *data,
                                          int retval) {
    EXPECT_RESOURCE_ACTION_COMMON(WRITE);
    command->value.data = *data;
}

void _anjay_mock_dm_expect_resource_execute(anjay_t *anjay,
                                            const anjay_dm_object_def_t *const *obj_ptr,
                                            anjay_iid_t iid,
                                            anjay_rid_t rid,
                                            const anjay_mock_dm_data_t *data,
                                            int retval) {
    EXPECT_RESOURCE_ACTION_COMMON(EXECUTE);
    command->value.data = *data;
}

void _anjay_mock_dm_expect_resource_dim(anjay_t *anjay,
                                        const anjay_dm_object_def_t *const *obj_ptr,
                                        anjay_iid_t iid,
                                        anjay_rid_t rid,
                                        int retval) {
    EXPECT_RESOURCE_ACTION_COMMON(DIM);
}

void _anjay_mock_dm_expect_resource_read_attrs(anjay_t *anjay,
                                               const anjay_dm_object_def_t *const *obj_ptr,
                                               anjay_iid_t iid,
                                               anjay_rid_t rid,
                                               anjay_ssid_t ssid,
                                               int retval,
                                               const anjay_dm_attributes_t *attrs) {
    anjay_mock_dm_expected_command_t *command = new_expected_command();
    command->command = MOCK_DM_RESOURCE_READ_ATTRS;
    command->anjay = anjay;
    command->obj_ptr = obj_ptr;
    command->input.ssid_iid_rid.ssid = ssid;
    command->input.ssid_iid_rid.iid = iid;
    command->input.ssid_iid_rid.rid = rid;
    command->retval = retval;
    if (attrs) {
        command->value.attributes = *attrs;
    } else {
        AVS_UNIT_ASSERT_FAILED(retval);
    }
}

void _anjay_mock_dm_expect_resource_write_attrs(anjay_t *anjay,
                                                const anjay_dm_object_def_t *const *obj_ptr,
                                                anjay_iid_t iid,
                                                anjay_rid_t rid,
                                                anjay_ssid_t ssid,
                                                const anjay_dm_attributes_t *attrs,
                                                int retval) {
    anjay_mock_dm_expected_command_t *command = new_expected_command();
    command->command = MOCK_DM_RESOURCE_WRITE_ATTRS;
    command->anjay = anjay;
    command->obj_ptr = obj_ptr;
    command->input.ssid_iid_rid.ssid = ssid;
    command->input.ssid_iid_rid.iid = iid;
    command->input.ssid_iid_rid.rid = rid;
    command->retval = retval;
    command->value.attributes = *attrs;
}

void _anjay_mock_dm_expect_clean(void) {
    AVS_UNIT_ASSERT_NULL(EXPECTED_COMMANDS);
}

void _anjay_mock_dm_expected_commands_clear(void) {
    AVS_LIST_CLEAR(&EXPECTED_COMMANDS);
}
