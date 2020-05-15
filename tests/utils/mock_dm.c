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

#include <anjay_init.h>

#include <stdbool.h>
#include <string.h>

#include <avsystem/commons/avs_list.h>
#include <avsystem/commons/avs_unit_test.h>

#include "src/core/dm/anjay_dm_execute.h"
#include "tests/utils/mock_dm.h"

typedef enum {
    MOCK_DM_OBJECT_READ_DEFAULT_ATTRS,
    MOCK_DM_OBJECT_WRITE_DEFAULT_ATTRS,
    MOCK_DM_INSTANCE_RESET,
    MOCK_DM_LIST_INSTANCES,
    MOCK_DM_INSTANCE_CREATE,
    MOCK_DM_INSTANCE_REMOVE,
    MOCK_DM_INSTANCE_READ_DEFAULT_ATTRS,
    MOCK_DM_INSTANCE_WRITE_DEFAULT_ATTRS,
    MOCK_DM_LIST_RESOURCES,
    MOCK_DM_RESOURCE_READ,
    MOCK_DM_RESOURCE_WRITE,
    MOCK_DM_RESOURCE_EXECUTE,
    MOCK_DM_RESOURCE_RESET,
    MOCK_DM_LIST_RESOURCE_INSTANCES,
    MOCK_DM_RESOURCE_READ_ATTRS,
    MOCK_DM_RESOURCE_WRITE_ATTRS,
    MOCK_DM_RESOURCE_INSTANCE_READ_ATTRS,
    MOCK_DM_RESOURCE_INSTANCE_WRITE_ATTRS
} anjay_mock_dm_expected_command_type_t;

typedef struct {
    anjay_mock_dm_expected_command_type_t command;
    const char *command_str;
    anjay_t *anjay;
    const anjay_dm_object_def_t *const *obj_ptr;
    union {
        anjay_iid_t iid;
        anjay_rid_t rid;
        anjay_ssid_t ssid;
        struct {
            anjay_iid_t iid;
            anjay_rid_t rid;
        } iid_and_rid;
        struct {
            anjay_iid_t iid;
            anjay_rid_t rid;
            anjay_riid_t riid;
        } iid_rid_riid;
        struct {
            anjay_ssid_t ssid;
            anjay_iid_t iid;
        } ssid_and_iid;
        struct {
            anjay_ssid_t ssid;
            anjay_iid_t iid;
            anjay_rid_t rid;
        } ssid_iid_rid;
        struct {
            anjay_ssid_t ssid;
            anjay_iid_t iid;
            anjay_rid_t rid;
            anjay_riid_t riid;
        } ssid_iid_rid_riid;
    } input;
    union {
        uint16_t *id_array;
        anjay_mock_dm_res_entry_t *res_array;
        anjay_mock_dm_data_t data;
        const anjay_mock_dm_execute_data_t *execute_data;
        anjay_dm_internal_oi_attrs_t common_attributes;
        anjay_dm_internal_r_attrs_t resource_attributes;
    } value;
    int retval;
} anjay_mock_dm_expected_command_t;

static AVS_LIST(anjay_mock_dm_expected_command_t) EXPECTED_COMMANDS;

#define DM_ACTION_COMMON(UName)                                         \
    do {                                                                \
        AVS_UNIT_ASSERT_NOT_NULL(EXPECTED_COMMANDS);                    \
        AVS_UNIT_ASSERT_EQUAL_STRING(EXPECTED_COMMANDS->command_str,    \
                                     AVS_QUOTE_MACRO(MOCK_DM_##UName)); \
        AVS_UNIT_ASSERT_TRUE(EXPECTED_COMMANDS->anjay == anjay);        \
        AVS_UNIT_ASSERT_TRUE(EXPECTED_COMMANDS->obj_ptr == obj_ptr);    \
    } while (0)

#define DM_ACTION_RETURN                                  \
    do {                                                  \
        int retval##__LINE__ = EXPECTED_COMMANDS->retval; \
        AVS_LIST_DELETE(&EXPECTED_COMMANDS);              \
        return retval##__LINE__;                          \
    } while (0)

void _anjay_mock_dm_assert_common_attributes_equal(
        const anjay_dm_internal_oi_attrs_t *a,
        const anjay_dm_internal_oi_attrs_t *b) {
#ifdef WITH_CUSTOM_ATTRIBUTES
    AVS_UNIT_ASSERT_FIELD_EQUAL(a, b, custom.data.con);
#endif // WITH_CUSTOM_ATTRIBUTES
    AVS_UNIT_ASSERT_FIELD_EQUAL(a, b, standard.min_period);
    AVS_UNIT_ASSERT_FIELD_EQUAL(a, b, standard.max_period);
    AVS_UNIT_ASSERT_FIELD_EQUAL(a, b, standard.min_eval_period);
    AVS_UNIT_ASSERT_FIELD_EQUAL(a, b, standard.max_eval_period);
}

void _anjay_mock_dm_assert_attributes_equal(
        const anjay_dm_internal_r_attrs_t *a,
        const anjay_dm_internal_r_attrs_t *b) {
    _anjay_mock_dm_assert_common_attributes_equal(
            _anjay_dm_get_internal_oi_attrs_const(&a->standard.common),
            _anjay_dm_get_internal_oi_attrs_const(&b->standard.common));
    AVS_UNIT_ASSERT_FIELD_EQUAL(a, b, standard.greater_than);
    AVS_UNIT_ASSERT_FIELD_EQUAL(a, b, standard.less_than);
    AVS_UNIT_ASSERT_FIELD_EQUAL(a, b, standard.step);
}

int _anjay_mock_dm_object_read_default_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_ssid_t ssid,
        anjay_dm_oi_attributes_t *out) {
    DM_ACTION_COMMON(OBJECT_READ_DEFAULT_ATTRS);
    EXPECTED_COMMANDS->input.ssid = ssid;
    *_anjay_dm_get_internal_oi_attrs(out) =
            EXPECTED_COMMANDS->value.common_attributes;
    DM_ACTION_RETURN;
}

int _anjay_mock_dm_object_write_default_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_ssid_t ssid,
        const anjay_dm_oi_attributes_t *attrs) {
    DM_ACTION_COMMON(OBJECT_WRITE_DEFAULT_ATTRS);
    EXPECTED_COMMANDS->input.ssid = ssid;
    _anjay_mock_dm_assert_common_attributes_equal(
            _anjay_dm_get_internal_oi_attrs_const(attrs),
            &EXPECTED_COMMANDS->value.common_attributes);
    DM_ACTION_RETURN;
}

int _anjay_mock_dm_list_instances(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr,
                                  anjay_dm_list_ctx_t *ctx) {
    DM_ACTION_COMMON(LIST_INSTANCES);
    // anjay_dm_emit() may call other handlers,
    // so pop the command from the queue early
    AVS_LIST(anjay_mock_dm_expected_command_t) command =
            AVS_LIST_DETACH(&EXPECTED_COMMANDS);
    for (const anjay_iid_t *iid = command->value.id_array;
         *iid != ANJAY_ID_INVALID;
         ++iid) {
        anjay_dm_emit(ctx, *iid);
    }
    avs_free(command->value.id_array);
    int retval = command->retval;
    AVS_LIST_DELETE(&command);
    return retval;
}

#define INSTANCE_ACTION(LName, UName)                                    \
    int _anjay_mock_dm_instance_##LName(                                 \
            anjay_t *anjay, const anjay_dm_object_def_t *const *obj_ptr, \
            anjay_iid_t iid) {                                           \
        DM_ACTION_COMMON(INSTANCE_##UName);                              \
        AVS_UNIT_ASSERT_EQUAL(iid, EXPECTED_COMMANDS->input.iid);        \
        DM_ACTION_RETURN;                                                \
    }

INSTANCE_ACTION(reset, RESET)
INSTANCE_ACTION(remove, REMOVE)
INSTANCE_ACTION(create, CREATE)

int _anjay_mock_dm_instance_read_default_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_ssid_t ssid,
        anjay_dm_oi_attributes_t *out) {
    DM_ACTION_COMMON(INSTANCE_READ_DEFAULT_ATTRS);
    AVS_UNIT_ASSERT_EQUAL(ssid, EXPECTED_COMMANDS->input.ssid_and_iid.ssid);
    AVS_UNIT_ASSERT_EQUAL(iid, EXPECTED_COMMANDS->input.ssid_and_iid.iid);
    *_anjay_dm_get_internal_oi_attrs(out) =
            EXPECTED_COMMANDS->value.common_attributes;
    DM_ACTION_RETURN;
}

int _anjay_mock_dm_instance_write_default_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_ssid_t ssid,
        const anjay_dm_oi_attributes_t *attrs) {
    DM_ACTION_COMMON(INSTANCE_WRITE_DEFAULT_ATTRS);
    AVS_UNIT_ASSERT_EQUAL(ssid, EXPECTED_COMMANDS->input.ssid_and_iid.ssid);
    AVS_UNIT_ASSERT_EQUAL(iid, EXPECTED_COMMANDS->input.ssid_and_iid.iid);
    _anjay_mock_dm_assert_common_attributes_equal(
            _anjay_dm_get_internal_oi_attrs_const(attrs),
            &EXPECTED_COMMANDS->value.common_attributes);
    DM_ACTION_RETURN;
}

int _anjay_mock_dm_list_resources(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr,
                                  anjay_iid_t iid,
                                  anjay_dm_resource_list_ctx_t *ctx) {
    DM_ACTION_COMMON(LIST_RESOURCES);
    AVS_UNIT_ASSERT_EQUAL(iid, EXPECTED_COMMANDS->input.iid);
    // avs_dm_emit_res() may call other handlers,
    // so pop the command from the queue early
    AVS_LIST(anjay_mock_dm_expected_command_t) command =
            AVS_LIST_DETACH(&EXPECTED_COMMANDS);
    if (command->value.res_array) {
        for (const anjay_mock_dm_res_entry_t *res = command->value.res_array;
             res->rid != ANJAY_ID_INVALID;
             ++res) {
            anjay_dm_emit_res(ctx, res->rid, res->kind, res->presence);
        }
        avs_free(command->value.id_array);
    }
    int retval = command->retval;
    AVS_LIST_DELETE(&command);
    return retval;
}

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
    }
    AVS_UNIT_ASSERT_EQUAL(retval, output->expected_retval);
}

int _anjay_mock_dm_resource_read(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj_ptr,
                                 anjay_iid_t iid,
                                 anjay_rid_t rid,
                                 anjay_riid_t riid,
                                 anjay_output_ctx_t *ctx) {
    DM_ACTION_COMMON(RESOURCE_READ);
    AVS_UNIT_ASSERT_EQUAL(iid, EXPECTED_COMMANDS->input.iid_rid_riid.iid);
    AVS_UNIT_ASSERT_EQUAL(rid, EXPECTED_COMMANDS->input.iid_rid_riid.rid);
    AVS_UNIT_ASSERT_EQUAL(riid, EXPECTED_COMMANDS->input.iid_rid_riid.riid);
    perform_output(ctx, &EXPECTED_COMMANDS->value.data);
    DM_ACTION_RETURN;
}

static void perform_input(anjay_input_ctx_t *ctx,
                          const anjay_mock_dm_data_t *input) {
    int retval = 0;
    switch (input->type) {
    case MOCK_DATA_NONE:
        return;
    case MOCK_DATA_BYTES: {
        size_t bytes_read;
        bool message_finished;
        char buf[input->data.bytes.length];
        retval = anjay_get_bytes(ctx, &bytes_read, &message_finished, buf,
                                 sizeof(buf));
        if (!retval) {
            AVS_UNIT_ASSERT_EQUAL(bytes_read, sizeof(buf));
            AVS_UNIT_ASSERT_TRUE(message_finished);
            AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(buf, input->data.bytes.data,
                                              input->data.bytes.length);
        }
        break;
    }
    case MOCK_DATA_STRING: {
        char buf[strlen(input->data.str) + 1];
        retval = anjay_get_string(ctx, buf, sizeof(buf));
        if (!retval) {
            AVS_UNIT_ASSERT_EQUAL_STRING(buf, input->data.str);
        }
        break;
    }
    case MOCK_DATA_INT: {
        int64_t value;
        retval = anjay_get_i64(ctx, &value);
        if (!retval) {
            AVS_UNIT_ASSERT_EQUAL(value, input->data.i);
        }
        break;
    }
    case MOCK_DATA_FLOAT: {
        double value;
        retval = anjay_get_double(ctx, &value);
        if (!retval) {
            AVS_UNIT_ASSERT_EQUAL(value, input->data.f);
        }
        break;
    }
    case MOCK_DATA_BOOL: {
        bool value;
        retval = anjay_get_bool(ctx, &value);
        if (!retval) {
            AVS_UNIT_ASSERT_EQUAL(value, input->data.b);
        }
        break;
    }
    case MOCK_DATA_OBJLNK: {
        anjay_oid_t oid;
        anjay_iid_t iid;
        retval = anjay_get_objlnk(ctx, &oid, &iid);
        if (!retval) {
            AVS_UNIT_ASSERT_EQUAL(oid, input->data.objlnk.oid);
            AVS_UNIT_ASSERT_EQUAL(iid, input->data.objlnk.iid);
        }
        break;
    }
    default:
        AVS_UNIT_ASSERT_NOT_EQUAL(input->type, input->type);
    }
    AVS_UNIT_ASSERT_EQUAL(retval, input->expected_retval);
}

int _anjay_mock_dm_resource_write(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr,
                                  anjay_iid_t iid,
                                  anjay_rid_t rid,
                                  anjay_riid_t riid,
                                  anjay_input_ctx_t *ctx) {
    DM_ACTION_COMMON(RESOURCE_WRITE);
    AVS_UNIT_ASSERT_EQUAL(iid, EXPECTED_COMMANDS->input.iid_rid_riid.iid);
    AVS_UNIT_ASSERT_EQUAL(rid, EXPECTED_COMMANDS->input.iid_rid_riid.rid);
    AVS_UNIT_ASSERT_EQUAL(riid, EXPECTED_COMMANDS->input.iid_rid_riid.riid);
    perform_input(ctx, &EXPECTED_COMMANDS->value.data);
    DM_ACTION_RETURN;
}

int _anjay_mock_dm_resource_execute(anjay_t *anjay,
                                    const anjay_dm_object_def_t *const *obj_ptr,
                                    anjay_iid_t iid,
                                    anjay_rid_t rid,
                                    anjay_execute_ctx_t *ctx) {
    int retval = 0;
    int arg;
    bool has_value;
    DM_ACTION_COMMON(RESOURCE_EXECUTE);
    AVS_UNIT_ASSERT_EQUAL(iid, EXPECTED_COMMANDS->input.iid_and_rid.iid);
    AVS_UNIT_ASSERT_EQUAL(rid, EXPECTED_COMMANDS->input.iid_and_rid.rid);
    if (EXPECTED_COMMANDS->value.execute_data) {
        for (const anjay_mock_dm_execute_arg_t *const *mock_arg =
                     *EXPECTED_COMMANDS->value.execute_data;
             mock_arg && *mock_arg;
             ++mock_arg) {
            retval = anjay_execute_get_next_arg(ctx, &arg, &has_value);
            if (!retval) {
                AVS_UNIT_ASSERT_EQUAL(arg, (*mock_arg)->arg);
                AVS_UNIT_ASSERT_EQUAL(has_value, !!(*mock_arg)->value);
                if ((*mock_arg)->value) {
                    char buf[strlen((*mock_arg)->value) + 1];
                    size_t bytes_read;
                    retval = anjay_execute_get_arg_value(ctx, &bytes_read, buf,
                                                         sizeof(buf));
                    if (!retval) {
                        AVS_UNIT_ASSERT_EQUAL(bytes_read,
                                              strlen((*mock_arg)->value));
                        AVS_UNIT_ASSERT_EQUAL_STRING(buf, (*mock_arg)->value);
                    }
                }
            }
            AVS_UNIT_ASSERT_EQUAL(retval, (*mock_arg)->expected_retval);
        }
        if (!retval) {
            AVS_UNIT_ASSERT_EQUAL(anjay_execute_get_next_arg(ctx, &arg,
                                                             &has_value),
                                  ANJAY_EXECUTE_GET_ARG_END);
            AVS_UNIT_ASSERT_EQUAL(arg, -1);
            AVS_UNIT_ASSERT_FALSE(has_value);
        }
    }
    DM_ACTION_RETURN;
}

int _anjay_mock_dm_resource_reset(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr,
                                  anjay_iid_t iid,
                                  anjay_rid_t rid) {
    DM_ACTION_COMMON(RESOURCE_RESET);
    AVS_UNIT_ASSERT_EQUAL(iid, EXPECTED_COMMANDS->input.iid_and_rid.iid);
    AVS_UNIT_ASSERT_EQUAL(rid, EXPECTED_COMMANDS->input.iid_and_rid.rid);
    DM_ACTION_RETURN;
}

int _anjay_mock_dm_list_resource_instances(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_dm_list_ctx_t *ctx) {
    DM_ACTION_COMMON(LIST_RESOURCE_INSTANCES);
    AVS_UNIT_ASSERT_EQUAL(iid, EXPECTED_COMMANDS->input.iid_and_rid.iid);
    AVS_UNIT_ASSERT_EQUAL(rid, EXPECTED_COMMANDS->input.iid_and_rid.rid);
    // avs_dm_emit() may call other handlers,
    // so pop the command from the queue early
    AVS_LIST(anjay_mock_dm_expected_command_t) command =
            AVS_LIST_DETACH(&EXPECTED_COMMANDS);
    if (command->value.id_array) {
        for (const anjay_riid_t *riid = command->value.id_array;
             *riid != ANJAY_ID_INVALID;
             ++riid) {
            anjay_dm_emit(ctx, *riid);
        }
        avs_free(command->value.id_array);
    }
    int retval = command->retval;
    AVS_LIST_DELETE(&command);
    return retval;
}

int _anjay_mock_dm_resource_read_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_ssid_t ssid,
        anjay_dm_r_attributes_t *out) {
    DM_ACTION_COMMON(RESOURCE_READ_ATTRS);
    AVS_UNIT_ASSERT_EQUAL(ssid, EXPECTED_COMMANDS->input.ssid_iid_rid.ssid);
    AVS_UNIT_ASSERT_EQUAL(iid, EXPECTED_COMMANDS->input.ssid_iid_rid.iid);
    AVS_UNIT_ASSERT_EQUAL(rid, EXPECTED_COMMANDS->input.ssid_iid_rid.rid);
    *_anjay_dm_get_internal_r_attrs(out) =
            EXPECTED_COMMANDS->value.resource_attributes;
    DM_ACTION_RETURN;
}

int _anjay_mock_dm_resource_write_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_ssid_t ssid,
        const anjay_dm_r_attributes_t *attrs) {
    DM_ACTION_COMMON(RESOURCE_WRITE_ATTRS);
    AVS_UNIT_ASSERT_EQUAL(ssid, EXPECTED_COMMANDS->input.ssid_iid_rid.ssid);
    AVS_UNIT_ASSERT_EQUAL(iid, EXPECTED_COMMANDS->input.ssid_iid_rid.iid);
    AVS_UNIT_ASSERT_EQUAL(rid, EXPECTED_COMMANDS->input.ssid_iid_rid.rid);
    _anjay_mock_dm_assert_attributes_equal(
            _anjay_dm_get_internal_r_attrs_const(attrs),
            &EXPECTED_COMMANDS->value.resource_attributes);
    DM_ACTION_RETURN;
}

static anjay_mock_dm_expected_command_t *
new_expected_command_impl(anjay_mock_dm_expected_command_type_t type,
                          const char *type_str) {
    anjay_mock_dm_expected_command_t *new_command =
            AVS_LIST_NEW_ELEMENT(anjay_mock_dm_expected_command_t);
    AVS_UNIT_ASSERT_NOT_NULL(new_command);
    new_command->command = type;
    new_command->command_str = type_str;
    AVS_LIST_APPEND(&EXPECTED_COMMANDS, new_command);
    return new_command;
}

#define NEW_EXPECTED_COMMAND(Type) \
    (new_expected_command_impl((Type), AVS_QUOTE_MACRO(Type)))

void _anjay_mock_dm_expect_object_read_default_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_ssid_t ssid,
        int retval,
        const anjay_dm_internal_oi_attrs_t *attrs) {
    anjay_mock_dm_expected_command_t *command =
            NEW_EXPECTED_COMMAND(MOCK_DM_OBJECT_READ_DEFAULT_ATTRS);
    command->anjay = anjay;
    command->obj_ptr = obj_ptr;
    command->input.ssid = ssid;
    command->retval = retval;
    if (attrs) {
        command->value.common_attributes = *attrs;
    } else {
        AVS_UNIT_ASSERT_FAILED(retval);
    }
}

void _anjay_mock_dm_expect_object_write_default_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_ssid_t ssid,
        const anjay_dm_internal_oi_attrs_t *attrs,
        int retval) {
    anjay_mock_dm_expected_command_t *command =
            NEW_EXPECTED_COMMAND(MOCK_DM_OBJECT_WRITE_DEFAULT_ATTRS);
    command->anjay = anjay;
    command->obj_ptr = obj_ptr;
    command->input.ssid = ssid;
    command->retval = retval;
    command->value.common_attributes = *attrs;
}

void _anjay_mock_dm_expect_list_instances(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        int retval,
        const anjay_iid_t *iid_array) {
    anjay_mock_dm_expected_command_t *command =
            NEW_EXPECTED_COMMAND(MOCK_DM_LIST_INSTANCES);
    command->anjay = anjay;
    command->obj_ptr = obj_ptr;
    size_t array_size = 1;
    while (iid_array[array_size - 1] != ANJAY_ID_INVALID) {
        ++array_size;
    }
    command->value.id_array = avs_malloc(array_size * sizeof(anjay_iid_t));
    AVS_UNIT_ASSERT_NOT_NULL(command->value.id_array);
    memcpy(command->value.id_array, iid_array,
           array_size * sizeof(anjay_iid_t));
    command->retval = retval;
}

#define EXPECT_INSTANCE_ACTION(LName, UName)                             \
    void _anjay_mock_dm_expect_instance_##LName(                         \
            anjay_t *anjay, const anjay_dm_object_def_t *const *obj_ptr, \
            anjay_iid_t iid, int retval) {                               \
        anjay_mock_dm_expected_command_t *command =                      \
                NEW_EXPECTED_COMMAND(MOCK_DM_INSTANCE_##UName);          \
        command->anjay = anjay;                                          \
        command->obj_ptr = obj_ptr;                                      \
        command->input.iid = iid;                                        \
        command->retval = retval;                                        \
    }

EXPECT_INSTANCE_ACTION(reset, RESET)
EXPECT_INSTANCE_ACTION(remove, REMOVE)
EXPECT_INSTANCE_ACTION(create, CREATE)

void _anjay_mock_dm_expect_instance_read_default_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_ssid_t ssid,
        int retval,
        const anjay_dm_internal_oi_attrs_t *attrs) {
    anjay_mock_dm_expected_command_t *command =
            NEW_EXPECTED_COMMAND(MOCK_DM_INSTANCE_READ_DEFAULT_ATTRS);
    command->anjay = anjay;
    command->obj_ptr = obj_ptr;
    command->input.ssid_and_iid.ssid = ssid;
    command->input.ssid_and_iid.iid = iid;
    command->retval = retval;
    if (attrs) {
        command->value.common_attributes = *attrs;
    } else {
        AVS_UNIT_ASSERT_FAILED(retval);
    }
}

void _anjay_mock_dm_expect_instance_write_default_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_ssid_t ssid,
        const anjay_dm_internal_oi_attrs_t *attrs,
        int retval) {
    anjay_mock_dm_expected_command_t *command =
            NEW_EXPECTED_COMMAND(MOCK_DM_INSTANCE_WRITE_DEFAULT_ATTRS);
    command->anjay = anjay;
    command->obj_ptr = obj_ptr;
    command->input.ssid_and_iid.ssid = ssid;
    command->input.ssid_and_iid.iid = iid;
    command->retval = retval;
    command->value.common_attributes = *attrs;
}

void _anjay_mock_dm_expect_list_resources(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        int retval,
        const anjay_mock_dm_res_entry_t *res_array) {
    anjay_mock_dm_expected_command_t *command =
            NEW_EXPECTED_COMMAND(MOCK_DM_LIST_RESOURCES);
    command->anjay = anjay;
    command->obj_ptr = obj_ptr;
    command->input.iid = iid;
    if (res_array) {
        size_t array_size = 1;
        while (res_array[array_size - 1].rid != ANJAY_ID_INVALID) {
            ++array_size;
        }
        command->value.res_array =
                avs_malloc(array_size * sizeof(anjay_mock_dm_res_entry_t));
        AVS_UNIT_ASSERT_NOT_NULL(command->value.id_array);
        memcpy(command->value.res_array, res_array,
               array_size * sizeof(anjay_mock_dm_res_entry_t));
    }
    command->retval = retval;
}

#define EXPECT_RESOURCE_ACTION_COMMON(UName)                \
    anjay_mock_dm_expected_command_t *command =             \
            NEW_EXPECTED_COMMAND(MOCK_DM_RESOURCE_##UName); \
    command->anjay = anjay;                                 \
    command->obj_ptr = obj_ptr;                             \
    command->input.iid_and_rid.iid = iid;                   \
    command->input.iid_and_rid.rid = rid;                   \
    command->retval = retval;

void _anjay_mock_dm_expect_resource_read(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_riid_t riid,
        int retval,
        const anjay_mock_dm_data_t *data) {
    anjay_mock_dm_expected_command_t *command =
            NEW_EXPECTED_COMMAND(MOCK_DM_RESOURCE_READ);
    command->anjay = anjay;
    command->obj_ptr = obj_ptr;
    command->input.iid_rid_riid.iid = iid;
    command->input.iid_rid_riid.rid = rid;
    command->input.iid_rid_riid.riid = riid;
    command->retval = retval;
    command->value.data = *data;
}

void _anjay_mock_dm_expect_resource_write(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_rid_t riid,
        const anjay_mock_dm_data_t *data,
        int retval) {
    anjay_mock_dm_expected_command_t *command =
            NEW_EXPECTED_COMMAND(MOCK_DM_RESOURCE_WRITE);
    command->anjay = anjay;
    command->obj_ptr = obj_ptr;
    command->input.iid_rid_riid.iid = iid;
    command->input.iid_rid_riid.rid = rid;
    command->input.iid_rid_riid.riid = riid;
    command->retval = retval;
    command->value.data = *data;
}

void _anjay_mock_dm_expect_resource_execute(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        const anjay_mock_dm_execute_data_t *data,
        int retval) {
    EXPECT_RESOURCE_ACTION_COMMON(EXECUTE);
    command->value.execute_data = data;
}

void _anjay_mock_dm_expect_resource_reset(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        int retval) {
    EXPECT_RESOURCE_ACTION_COMMON(RESET);
}

void _anjay_mock_dm_expect_list_resource_instances(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        int retval,
        const anjay_riid_t *riid_array) {
    anjay_mock_dm_expected_command_t *command =
            NEW_EXPECTED_COMMAND(MOCK_DM_LIST_RESOURCE_INSTANCES);
    command->anjay = anjay;
    command->obj_ptr = obj_ptr;
    command->input.iid_and_rid.iid = iid;
    command->input.iid_and_rid.rid = rid;
    command->retval = retval;
    if (riid_array) {
        size_t array_size = 1;
        while (riid_array[array_size - 1] != ANJAY_ID_INVALID) {
            ++array_size;
        }
        command->value.id_array = avs_malloc(array_size * sizeof(anjay_riid_t));
        AVS_UNIT_ASSERT_NOT_NULL(command->value.id_array);
        memcpy(command->value.id_array, riid_array,
               array_size * sizeof(anjay_riid_t));
    }
}

void _anjay_mock_dm_expect_resource_read_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_ssid_t ssid,
        int retval,
        const anjay_dm_internal_r_attrs_t *attrs) {
    anjay_mock_dm_expected_command_t *command =
            NEW_EXPECTED_COMMAND(MOCK_DM_RESOURCE_READ_ATTRS);
    command->anjay = anjay;
    command->obj_ptr = obj_ptr;
    command->input.ssid_iid_rid.ssid = ssid;
    command->input.ssid_iid_rid.iid = iid;
    command->input.ssid_iid_rid.rid = rid;
    command->retval = retval;
    if (attrs) {
        command->value.resource_attributes = *attrs;
    } else {
        AVS_UNIT_ASSERT_FAILED(retval);
    }
}

void _anjay_mock_dm_expect_resource_write_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_ssid_t ssid,
        const anjay_dm_internal_r_attrs_t *attrs,
        int retval) {
    anjay_mock_dm_expected_command_t *command =
            NEW_EXPECTED_COMMAND(MOCK_DM_RESOURCE_WRITE_ATTRS);
    command->anjay = anjay;
    command->obj_ptr = obj_ptr;
    command->input.ssid_iid_rid.ssid = ssid;
    command->input.ssid_iid_rid.iid = iid;
    command->input.ssid_iid_rid.rid = rid;
    command->retval = retval;
    command->value.resource_attributes = *attrs;
}

void _anjay_mock_dm_expect_resource_instance_read_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_riid_t riid,
        anjay_ssid_t ssid,
        int retval,
        const anjay_dm_internal_r_attrs_t *attrs) {
    anjay_mock_dm_expected_command_t *command =
            NEW_EXPECTED_COMMAND(MOCK_DM_RESOURCE_INSTANCE_READ_ATTRS);
    command->anjay = anjay;
    command->obj_ptr = obj_ptr;
    command->input.ssid_iid_rid_riid.ssid = ssid;
    command->input.ssid_iid_rid_riid.iid = iid;
    command->input.ssid_iid_rid_riid.rid = rid;
    command->input.ssid_iid_rid_riid.riid = riid;
    command->retval = retval;
    if (attrs) {
        command->value.resource_attributes = *attrs;
    } else {
        AVS_UNIT_ASSERT_FAILED(retval);
    }
}

void _anjay_mock_dm_expect_resource_instance_write_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_riid_t riid,
        anjay_ssid_t ssid,
        const anjay_dm_internal_r_attrs_t *attrs,
        int retval) {
    anjay_mock_dm_expected_command_t *command =
            NEW_EXPECTED_COMMAND(MOCK_DM_RESOURCE_INSTANCE_WRITE_ATTRS);
    command->anjay = anjay;
    command->obj_ptr = obj_ptr;
    command->input.ssid_iid_rid_riid.ssid = ssid;
    command->input.ssid_iid_rid_riid.iid = iid;
    command->input.ssid_iid_rid_riid.rid = rid;
    command->input.ssid_iid_rid_riid.riid = riid;
    command->retval = retval;
    command->value.resource_attributes = *attrs;
}

void _anjay_mock_dm_expect_clean(void) {
    AVS_UNIT_ASSERT_NULL(EXPECTED_COMMANDS);
}

void _anjay_mock_dm_expected_commands_clear(void) {
    AVS_LIST_CLEAR(&EXPECTED_COMMANDS);
}
