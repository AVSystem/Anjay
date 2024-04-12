/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <avsystem/commons/avs_unit_test.h>

#include <fluf/fluf_defs.h>
#include <fluf/fluf_io.h>
#include <fluf/fluf_utils.h>

#include <anj/sdm_impl.h>
#include <anj/sdm_io.h>

static sdm_res_spec_t res_spec_0 = {
    .rid = 0,
    .operation = SDM_RES_R,
    .type = FLUF_DATA_TYPE_INT
};
static const sdm_res_spec_t res_spec_1 = {
    .rid = 1,
    .operation = SDM_RES_W,
    .type = FLUF_DATA_TYPE_INT
};
static const sdm_res_spec_t res_spec_2 = {
    .rid = 2,
    .operation = SDM_RES_RWM,
    .type = FLUF_DATA_TYPE_INT
};
static const sdm_res_spec_t res_spec_3 = {
    .rid = 3,
    .operation = SDM_RES_WM,
    .type = FLUF_DATA_TYPE_INT
};
static sdm_res_spec_t res_spec_4 = {
    .rid = 4,
    .operation = SDM_RES_RW,
    .type = FLUF_DATA_TYPE_STRING
};
static int res_execute_counter;
static int res_execute(sdm_obj_t *obj,
                       sdm_obj_inst_t *obj_inst,
                       sdm_res_t *res,
                       const char *execute_arg,
                       size_t execute_arg_len) {
    (void) obj;
    (void) obj_inst;
    (void) res;
    (void) execute_arg;
    (void) execute_arg_len;
    res_execute_counter++;
    return 0;
}
static sdm_res_handlers_t res_handlers = {
    .res_execute = res_execute
};
static const sdm_res_spec_t res_spec_5 = {
    .rid = 5,
    .operation = SDM_RES_E
};
static sdm_res_t inst_1_res[] = {
    {
        .res_spec = &res_spec_0,
        .value.res_value =
                &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(0, SDM_INIT_RES_VAL_I64(1))
    },
    {
        .res_spec = &res_spec_1,
        .value.res_value =
                &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(0, SDM_INIT_RES_VAL_I64(2))
    }
};
static sdm_res_inst_t res_inst_1 = {
    .riid = 1,
    .res_value = &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(0, SDM_INIT_RES_VAL_I64(6))
};
static sdm_res_inst_t res_inst_2 = {
    .riid = 2,
    .res_value = &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(0, SDM_INIT_RES_VAL_I64(7))
};
static sdm_res_inst_t *res_insts[] = { &res_inst_1, &res_inst_2 };
static char res_4_buff[32];
static sdm_res_t inst_2_res[] = {
    {
        .res_spec = &res_spec_0,
        .value.res_value =
                &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(0, SDM_INIT_RES_VAL_I64(3))
    },
    {
        .res_spec = &res_spec_1,
        .value.res_value =
                &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(0, SDM_INIT_RES_VAL_I64(4))
    },
    {
        .res_spec = &res_spec_2,
        .value.res_inst.inst_count = 2,
        .value.res_inst.max_inst_count = 2,
        .value.res_inst.insts = res_insts
    },
    {
        .res_spec = &res_spec_3,
        .value.res_inst.inst_count = 0
    },
    {
        .res_spec = &res_spec_4,
        .value.res_value = &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(
                32, SDM_INIT_RES_VAL_BYTES(res_4_buff, 0))
    },
    {
        .res_spec = &res_spec_5,
        .res_handlers = &res_handlers
    }
};
static sdm_obj_inst_t obj_1_inst_1 = {
    .iid = 1,
    .res_count = 2,
    .resources = inst_1_res
};
static sdm_obj_inst_t obj_1_inst_2 = {
    .iid = 2,
    .res_count = 6,
    .resources = inst_2_res
};
static sdm_obj_inst_t *obj_1_insts[2] = { &obj_1_inst_1, &obj_1_inst_2 };
static bool validate_error = false;
static int operation_validate(sdm_obj_t *obj) {
    (void) obj;
    if (validate_error) {
        return SDM_ERR_BAD_REQUEST;
    }
    return 0;
}
static const sdm_obj_handlers_t validate_handler = {
    .operation_validate = operation_validate
};
static sdm_obj_t obj_1 = {
    .oid = 111,
    .version = "1.1",
    .insts = obj_1_insts,
    .inst_count = 2,
    .max_inst_count = 2,
    .obj_handlers = &validate_handler
};

static sdm_res_inst_t obj_2_res_inst_1 = {
    .riid = 1,
    .res_value = &SDM_MAKE_RES_VALUE(0)
};
static sdm_res_inst_t obj_2_res_inst_2 = {
    .riid = 2,
    .res_value = &SDM_MAKE_RES_VALUE(0)
};
static sdm_res_inst_t *obj_2_res_insts[] = { &obj_2_res_inst_1,
                                             &obj_2_res_inst_2 };
static sdm_res_t obj_2_res = {
    .res_spec = &res_spec_2,
    .value.res_inst.inst_count = 1,
    .value.res_inst.max_inst_count = 2,
    .value.res_inst.insts = obj_2_res_insts
};
static sdm_obj_inst_t obj_2_inst_1 = {
    .iid = 1,
    .res_count = 1,
    .resources = &obj_2_res
};
static sdm_res_t obj_2_inst_2_res = {
    .res_spec = &res_spec_1,
    .value.res_value =
            &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(0, SDM_INIT_RES_VAL_I64(1))
};
static sdm_obj_inst_t obj_2_inst_2 = {
    .res_count = 1,
    .resources = &obj_2_inst_2_res
};
static sdm_obj_inst_t *obj_2_insts[2] = { &obj_2_inst_1, NULL };
static int
inst_create(sdm_obj_t *obj, sdm_obj_inst_t **out_obj_inst, fluf_iid_t iid) {
    (void) obj;
    (void) iid;
    *out_obj_inst = &obj_2_inst_2;
    return 0;
}
static int inst_delete(sdm_obj_t *obj, sdm_obj_inst_t *obj_inst) {
    (void) obj;
    (void) obj_inst;
    return 0;
}
static const sdm_obj_handlers_t handlers = {
    .inst_create = inst_create,
    .inst_delete = inst_delete
};
static sdm_obj_t obj_2 = {
    .oid = 222,
    .insts = obj_2_insts,
    .inst_count = 1,
    .max_inst_count = 2,
    .obj_handlers = &handlers
};

#define SET_UP()                                       \
    char buff[512];                                    \
    size_t buff_len = sizeof(buff);                    \
    fluf_data_t msg;                                   \
    memset(&msg, 0, sizeof(msg));                      \
    sdm_data_model_t dm;                               \
    sdm_process_ctx_t ctx;                             \
    memset(&ctx, 0, sizeof(ctx));                      \
    sdm_obj_t *objs[2];                                \
    sdm_initialize(&dm, objs, 2);                      \
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&dm, &obj_1)); \
    AVS_UNIT_ASSERT_SUCCESS(sdm_add_obj(&dm, &obj_2));

#define VERIFY_PAYLOAD(Payload, Buff, Len)                     \
    do {                                                       \
        AVS_UNIT_ASSERT_EQUAL(Len, sizeof(Payload) - 1);       \
        AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(Payload, Buff, Len); \
    } while (0)

AVS_UNIT_TEST(sdm_impl, register_operation) {
    SET_UP();
    msg.operation = FLUF_OP_REGISTER;
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_process(&ctx, &dm, &msg, false, buff, buff_len));

    AVS_UNIT_ASSERT_EQUAL(msg.operation, FLUF_OP_REGISTER);
    AVS_UNIT_ASSERT_EQUAL(msg.content_format, FLUF_COAP_FORMAT_LINK_FORMAT);
    AVS_UNIT_ASSERT_TRUE(buff == msg.payload);

    VERIFY_PAYLOAD("</111>;ver=1.1,</111/1>,</111/2>,</222>,</222/1>", buff,
                   msg.payload_size);
}

AVS_UNIT_TEST(sdm_impl, update_operation) {
    SET_UP();
    msg.operation = FLUF_OP_UPDATE;
    msg.msg_code = FLUF_COAP_CODE_POST;
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_process(&ctx, &dm, &msg, false, buff, buff_len));

    AVS_UNIT_ASSERT_EQUAL(msg.operation, FLUF_OP_UPDATE);
    AVS_UNIT_ASSERT_EQUAL(msg.content_format, FLUF_COAP_FORMAT_LINK_FORMAT);
    AVS_UNIT_ASSERT_EQUAL(msg.msg_code, FLUF_COAP_CODE_POST);
    AVS_UNIT_ASSERT_TRUE(buff == msg.payload);

    VERIFY_PAYLOAD("</111>;ver=1.1,</111/1>,</111/2>,</222>,</222/1>", buff,
                   msg.payload_size);
}

AVS_UNIT_TEST(sdm_impl, discover_operation) {
    SET_UP();
    msg.operation = FLUF_OP_DM_DISCOVER;
    msg.accept = FLUF_COAP_FORMAT_LINK_FORMAT;
    msg.uri = FLUF_MAKE_OBJECT_PATH(111);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_process(&ctx, &dm, &msg, false, buff, buff_len));

    AVS_UNIT_ASSERT_EQUAL(msg.operation, FLUF_OP_RESPONSE);
    AVS_UNIT_ASSERT_EQUAL(msg.content_format, FLUF_COAP_FORMAT_LINK_FORMAT);
    AVS_UNIT_ASSERT_EQUAL(msg.msg_code, FLUF_COAP_CODE_CONTENT);
    AVS_UNIT_ASSERT_TRUE(buff == msg.payload);

    VERIFY_PAYLOAD(
            "</111>;ver=1.1,</111/1>,</111/1/0>,</111/1/1>,</111/2>,</111/2/"
            "0>,</111/"
            "2/1>,</111/2/2>;dim=2,</111/2/3>;dim=0,</111/2/4>,</111/2/5>",
            buff, msg.payload_size);
}

AVS_UNIT_TEST(sdm_impl, read_operation) {
    SET_UP();
    msg.operation = FLUF_OP_DM_READ;
    msg.accept = FLUF_COAP_FORMAT_SENML_CBOR;
    msg.uri = FLUF_MAKE_OBJECT_PATH(111);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_process(&ctx, &dm, &msg, false, buff, buff_len));

    AVS_UNIT_ASSERT_EQUAL(msg.operation, FLUF_OP_RESPONSE);
    AVS_UNIT_ASSERT_EQUAL(msg.content_format, FLUF_COAP_FORMAT_SENML_CBOR);
    AVS_UNIT_ASSERT_EQUAL(msg.msg_code, FLUF_COAP_CODE_CONTENT);
    AVS_UNIT_ASSERT_TRUE(buff == msg.payload);

    VERIFY_PAYLOAD("\x85\xA3"
                   "\x21\x64\x2F\x31\x31\x31" // /111
                   "\x00\x64\x2F\x31\x2F\x30" // /1/0
                   "\x02\x01"                 // 1
                   "\xA2"
                   "\x00\x64\x2F\x32\x2F\x30" // /2/0
                   "\x02\x03"                 // 3
                   "\xA2"
                   "\x00\x66\x2F\x32\x2F\x32\x2F\x31" // /2/2/1
                   "\x02\x06"                         // 6
                   "\xA2"
                   "\x00\x66\x2F\x32\x2F\x32\x2F\x32" // /2/2/2
                   "\x02\x07"                         // 7
                   "\xA2"
                   "\x00\x64\x2F\x32\x2F\x34" // /2/4
                   "\x03\x60",
                   buff, msg.payload_size);
}

AVS_UNIT_TEST(sdm_impl, empty_read_1) {
    SET_UP();
    obj_1_inst_1.res_count = 0;
    msg.operation = FLUF_OP_DM_READ;
    msg.accept = FLUF_COAP_FORMAT_SENML_CBOR;
    msg.uri = FLUF_MAKE_INSTANCE_PATH(111, 1);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_process(&ctx, &dm, &msg, false, buff, buff_len));

    AVS_UNIT_ASSERT_EQUAL(msg.operation, FLUF_OP_RESPONSE);
    AVS_UNIT_ASSERT_EQUAL(msg.content_format, FLUF_COAP_FORMAT_SENML_CBOR);
    AVS_UNIT_ASSERT_EQUAL(msg.msg_code, FLUF_COAP_CODE_CONTENT);
    AVS_UNIT_ASSERT_TRUE(buff == msg.payload);

    VERIFY_PAYLOAD("\x80", buff, msg.payload_size);
    obj_1_inst_1.res_count = 2;
}

AVS_UNIT_TEST(sdm_impl, empty_read_2) {
    SET_UP();
    obj_1_inst_1.res_count = 0;
    msg.operation = FLUF_OP_DM_READ;
    msg.accept = FLUF_COAP_FORMAT_NOT_DEFINED;
    msg.uri = FLUF_MAKE_INSTANCE_PATH(111, 1);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_process(&ctx, &dm, &msg, false, buff, buff_len));

    AVS_UNIT_ASSERT_EQUAL(msg.operation, FLUF_OP_RESPONSE);
    AVS_UNIT_ASSERT_EQUAL(msg.content_format, FLUF_COAP_FORMAT_SENML_CBOR);
    AVS_UNIT_ASSERT_EQUAL(msg.msg_code, FLUF_COAP_CODE_CONTENT);
    AVS_UNIT_ASSERT_TRUE(buff == msg.payload);

    VERIFY_PAYLOAD("\x80", buff, msg.payload_size);
    obj_1_inst_1.res_count = 2;
}

AVS_UNIT_TEST(sdm_impl, empty_read_3) {
    SET_UP();
    obj_1_inst_1.res_count = 0;
    obj_1_inst_2.res_count = 0;
    msg.operation = FLUF_OP_DM_READ;
    msg.accept = FLUF_COAP_FORMAT_OMA_LWM2M_CBOR;
    msg.uri = FLUF_MAKE_OBJECT_PATH(111);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_process(&ctx, &dm, &msg, false, buff, buff_len));

    AVS_UNIT_ASSERT_EQUAL(msg.operation, FLUF_OP_RESPONSE);
    AVS_UNIT_ASSERT_EQUAL(msg.content_format, FLUF_COAP_FORMAT_OMA_LWM2M_CBOR);
    AVS_UNIT_ASSERT_EQUAL(msg.msg_code, FLUF_COAP_CODE_CONTENT);
    AVS_UNIT_ASSERT_TRUE(buff == msg.payload);

    VERIFY_PAYLOAD("\xBF\xFF", buff, msg.payload_size);
    obj_1_inst_1.res_count = 2;
    obj_1_inst_2.res_count = 6;
}

AVS_UNIT_TEST(sdm_impl, read_composite) {
    SET_UP();
    msg.operation = FLUF_OP_DM_READ_COMP;
    msg.accept = FLUF_COAP_FORMAT_NOT_DEFINED;
    msg.uri = FLUF_MAKE_OBJECT_PATH(111);
    AVS_UNIT_ASSERT_EQUAL(sdm_process(&ctx, &dm, &msg, false, buff, buff_len),
                          -1);
    AVS_UNIT_ASSERT_EQUAL(msg.payload_size, 0);
    AVS_UNIT_ASSERT_EQUAL(msg.operation, FLUF_OP_RESPONSE);
    AVS_UNIT_ASSERT_EQUAL(msg.msg_code, FLUF_COAP_CODE_BAD_REQUEST);
}

AVS_UNIT_TEST(sdm_impl, read_block_operation) {
    SET_UP();
    msg.operation = FLUF_OP_DM_READ;
    msg.accept = FLUF_COAP_FORMAT_SENML_CBOR;
    msg.uri = FLUF_MAKE_OBJECT_PATH(111);
    buff_len = 32;
    AVS_UNIT_ASSERT_EQUAL(sdm_process(&ctx, &dm, &msg, false, buff, buff_len),
                          SDM_IMPL_BLOCK_TRANSFER_NEEDED);
    AVS_UNIT_ASSERT_EQUAL(msg.operation, FLUF_OP_RESPONSE);
    AVS_UNIT_ASSERT_EQUAL(msg.content_format, FLUF_COAP_FORMAT_SENML_CBOR);
    AVS_UNIT_ASSERT_EQUAL(msg.msg_code, FLUF_COAP_CODE_CONTENT);
    AVS_UNIT_ASSERT_EQUAL(msg.block.size, 32);
    AVS_UNIT_ASSERT_EQUAL(msg.block.number, 0);
    AVS_UNIT_ASSERT_EQUAL(msg.block.block_type, FLUF_OPTION_BLOCK_2);
    AVS_UNIT_ASSERT_EQUAL(msg.block.more_flag, true);
    AVS_UNIT_ASSERT_TRUE(buff == msg.payload);

    VERIFY_PAYLOAD("\x85\xA3"
                   "\x21\x64\x2F\x31\x31\x31" // /111
                   "\x00\x64\x2F\x31\x2F\x30" // /1/0
                   "\x02\x01"                 // 1
                   "\xA2"
                   "\x00\x64\x2F\x32\x2F\x30" // /2/0
                   "\x02\x03"                 // 3
                   "\xA2"
                   "\x00\x66\x2F\x32\x2F\x32" // /2/
                   ,
                   buff, msg.payload_size);
    msg.block.number++;
    msg.operation = FLUF_OP_DM_READ;
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_process(&ctx, &dm, &msg, false, buff, buff_len));

    AVS_UNIT_ASSERT_EQUAL(msg.operation, FLUF_OP_RESPONSE);
    AVS_UNIT_ASSERT_EQUAL(msg.content_format, FLUF_COAP_FORMAT_SENML_CBOR);
    AVS_UNIT_ASSERT_EQUAL(msg.msg_code, FLUF_COAP_CODE_CONTENT);
    AVS_UNIT_ASSERT_EQUAL(msg.block.size, 32);
    AVS_UNIT_ASSERT_EQUAL(msg.block.number, 1);
    AVS_UNIT_ASSERT_EQUAL(msg.block.block_type, FLUF_OPTION_BLOCK_2);
    AVS_UNIT_ASSERT_EQUAL(msg.block.more_flag, false);
    AVS_UNIT_ASSERT_TRUE(buff == msg.payload);

    VERIFY_PAYLOAD("\x2F\x31" // /2/1
                   "\x02\x06" // 6
                   "\xA2"
                   "\x00\x66\x2F\x32\x2F\x32\x2F\x32" // /2/2/2
                   "\x02\x07"                         // 7
                   "\xA2"
                   "\x00\x64\x2F\x32\x2F\x34" // /2/4
                   "\x03\x60",
                   buff, msg.payload_size);
}

AVS_UNIT_TEST(sdm_impl, read_block_with_termination) {
    SET_UP();
    msg.operation = FLUF_OP_DM_READ;
    msg.accept = FLUF_COAP_FORMAT_SENML_CBOR;
    msg.uri = FLUF_MAKE_OBJECT_PATH(111);
    buff_len = 32;
    AVS_UNIT_ASSERT_EQUAL(sdm_process(&ctx, &dm, &msg, false, buff, buff_len),
                          SDM_IMPL_BLOCK_TRANSFER_NEEDED);
    AVS_UNIT_ASSERT_SUCCESS(sdm_process_stop(&ctx, &dm));

    msg.operation = FLUF_OP_DM_READ;
    msg.accept = FLUF_COAP_FORMAT_SENML_CBOR;
    msg.uri = FLUF_MAKE_OBJECT_PATH(111);
    buff_len = 32;
    AVS_UNIT_ASSERT_EQUAL(sdm_process(&ctx, &dm, &msg, false, buff, buff_len),
                          SDM_IMPL_BLOCK_TRANSFER_NEEDED);
    AVS_UNIT_ASSERT_EQUAL(msg.operation, FLUF_OP_RESPONSE);
    AVS_UNIT_ASSERT_EQUAL(msg.content_format, FLUF_COAP_FORMAT_SENML_CBOR);
    AVS_UNIT_ASSERT_EQUAL(msg.msg_code, FLUF_COAP_CODE_CONTENT);
    AVS_UNIT_ASSERT_EQUAL(msg.block.size, 32);
    AVS_UNIT_ASSERT_EQUAL(msg.block.number, 0);
    AVS_UNIT_ASSERT_EQUAL(msg.block.block_type, FLUF_OPTION_BLOCK_2);
    AVS_UNIT_ASSERT_EQUAL(msg.block.more_flag, true);
    AVS_UNIT_ASSERT_TRUE(buff == msg.payload);

    VERIFY_PAYLOAD("\x85\xA3"
                   "\x21\x64\x2F\x31\x31\x31" // /111
                   "\x00\x64\x2F\x31\x2F\x30" // /1/0
                   "\x02\x01"                 // 1
                   "\xA2"
                   "\x00\x64\x2F\x32\x2F\x30" // /2/0
                   "\x02\x03"                 // 3
                   "\xA2"
                   "\x00\x66\x2F\x32\x2F\x32" // /2/
                   ,
                   buff, msg.payload_size);
    msg.block.number++;
    msg.operation = FLUF_OP_DM_READ;
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_process(&ctx, &dm, &msg, false, buff, buff_len));

    AVS_UNIT_ASSERT_EQUAL(msg.operation, FLUF_OP_RESPONSE);
    AVS_UNIT_ASSERT_EQUAL(msg.content_format, FLUF_COAP_FORMAT_SENML_CBOR);
    AVS_UNIT_ASSERT_EQUAL(msg.msg_code, FLUF_COAP_CODE_CONTENT);
    AVS_UNIT_ASSERT_EQUAL(msg.block.size, 32);
    AVS_UNIT_ASSERT_EQUAL(msg.block.number, 1);
    AVS_UNIT_ASSERT_EQUAL(msg.block.block_type, FLUF_OPTION_BLOCK_2);
    AVS_UNIT_ASSERT_EQUAL(msg.block.more_flag, false);
    AVS_UNIT_ASSERT_TRUE(buff == msg.payload);

    VERIFY_PAYLOAD("\x2F\x31" // /2/1
                   "\x02\x06" // 6
                   "\xA2"
                   "\x00\x66\x2F\x32\x2F\x32\x2F\x32" // /2/2/2
                   "\x02\x07"                         // 7
                   "\xA2"
                   "\x00\x64\x2F\x32\x2F\x34" // /2/4
                   "\x03\x60",
                   buff, msg.payload_size);
}

AVS_UNIT_TEST(sdm_impl, bootstrap_discover_operation) {
    SET_UP();
    msg.operation = FLUF_OP_DM_DISCOVER;
    msg.accept = FLUF_COAP_FORMAT_LINK_FORMAT;
    msg.uri = FLUF_MAKE_OBJECT_PATH(222);
    AVS_UNIT_ASSERT_SUCCESS(sdm_process(&ctx, &dm, &msg, true, buff, buff_len));

    AVS_UNIT_ASSERT_EQUAL(msg.operation, FLUF_OP_RESPONSE);
    AVS_UNIT_ASSERT_EQUAL(msg.content_format, FLUF_COAP_FORMAT_LINK_FORMAT);
    AVS_UNIT_ASSERT_EQUAL(msg.msg_code, FLUF_COAP_CODE_CONTENT);
    AVS_UNIT_ASSERT_TRUE(buff == msg.payload);

#ifdef FLUF_WITH_LWM2M12
    VERIFY_PAYLOAD("</>;lwm2m=1.2,</222>,</222/1>", buff, msg.payload_size);
#else  // FLUF_WITH_LWM2M12
    VERIFY_PAYLOAD("</>;lwm2m=1.1,</222>,</222/1>", buff, msg.payload_size);
#endif // FLUF_WITH_LWM2M12
}

AVS_UNIT_TEST(sdm_impl, execute_operation) {
    SET_UP();
    res_execute_counter = 0;
    msg.content_format = FLUF_COAP_FORMAT_NOT_DEFINED;
    msg.operation = FLUF_OP_DM_EXECUTE;
    msg.uri = FLUF_MAKE_RESOURCE_PATH(111, 2, 5);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_process(&ctx, &dm, &msg, false, buff, buff_len));
    AVS_UNIT_ASSERT_EQUAL(msg.operation, FLUF_OP_RESPONSE);
    AVS_UNIT_ASSERT_EQUAL(msg.content_format, FLUF_COAP_FORMAT_NOT_DEFINED);
    AVS_UNIT_ASSERT_EQUAL(msg.msg_code, FLUF_COAP_CODE_CHANGED);
    AVS_UNIT_ASSERT_EQUAL(res_execute_counter, 1);

    res_execute_counter = 0;
}

AVS_UNIT_TEST(sdm_impl, write_composite) {
    SET_UP();
    msg.operation = FLUF_OP_DM_WRITE_COMP;
    msg.accept = FLUF_COAP_FORMAT_NOT_DEFINED;
    msg.content_format = FLUF_COAP_FORMAT_SENML_CBOR;
    msg.uri = FLUF_MAKE_ROOT_PATH();
    AVS_UNIT_ASSERT_EQUAL(sdm_process(&ctx, &dm, &msg, false, buff, buff_len),
                          -1);
    AVS_UNIT_ASSERT_EQUAL(msg.payload_size, 0);
    AVS_UNIT_ASSERT_EQUAL(msg.operation, FLUF_OP_RESPONSE);
    AVS_UNIT_ASSERT_EQUAL(msg.msg_code, FLUF_COAP_CODE_BAD_REQUEST);
}

AVS_UNIT_TEST(sdm_impl, delete_operation) {
    SET_UP();
    obj_2.inst_count++;
    obj_2_insts[1] = &obj_2_inst_1;
    obj_2_insts[0] = &obj_2_inst_2;
    obj_2_inst_2.iid = 0;

    msg.content_format = FLUF_COAP_FORMAT_NOT_DEFINED;
    msg.operation = FLUF_OP_DM_DELETE;
    msg.uri = FLUF_MAKE_INSTANCE_PATH(222, 0);
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_process(&ctx, &dm, &msg, false, buff, buff_len));
    AVS_UNIT_ASSERT_EQUAL(msg.operation, FLUF_OP_RESPONSE);
    AVS_UNIT_ASSERT_EQUAL(msg.content_format, FLUF_COAP_FORMAT_NOT_DEFINED);
    AVS_UNIT_ASSERT_EQUAL(msg.msg_code, FLUF_COAP_CODE_DELETED);
    AVS_UNIT_ASSERT_EQUAL(obj_2.inst_count, 1);
}

AVS_UNIT_TEST(sdm_impl, write_update_operation) {
    SET_UP();
    msg.content_format = FLUF_COAP_FORMAT_OMA_LWM2M_TLV;
    msg.operation = FLUF_OP_DM_WRITE_PARTIAL_UPDATE;
    msg.uri = FLUF_MAKE_INSTANCE_PATH(111, 1);
    msg.payload = "\xC1\x01\x2A";
    msg.payload_size = 3;
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_process(&ctx, &dm, &msg, false, buff, buff_len));
    AVS_UNIT_ASSERT_EQUAL(msg.operation, FLUF_OP_RESPONSE);
    AVS_UNIT_ASSERT_EQUAL(msg.content_format, FLUF_COAP_FORMAT_NOT_DEFINED);
    AVS_UNIT_ASSERT_EQUAL(msg.msg_code, FLUF_COAP_CODE_CHANGED);
    AVS_UNIT_ASSERT_EQUAL(inst_1_res[1].value.res_value->value.int_value, 42);
}

AVS_UNIT_TEST(sdm_impl, write_replace_operation) {
    SET_UP();
    msg.content_format = FLUF_COAP_FORMAT_OMA_LWM2M_TLV;
    msg.operation = FLUF_OP_DM_WRITE_REPLACE;
    msg.uri = FLUF_MAKE_RESOURCE_PATH(111, 1, 1);
    msg.payload = "\xC1\x01\x0A";
    msg.payload_size = 3;
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_process(&ctx, &dm, &msg, false, buff, buff_len));
    AVS_UNIT_ASSERT_EQUAL(msg.operation, FLUF_OP_RESPONSE);
    AVS_UNIT_ASSERT_EQUAL(msg.content_format, FLUF_COAP_FORMAT_NOT_DEFINED);
    AVS_UNIT_ASSERT_EQUAL(msg.msg_code, FLUF_COAP_CODE_CHANGED);
    AVS_UNIT_ASSERT_EQUAL(inst_1_res[1].value.res_value->value.int_value, 10);
}

AVS_UNIT_TEST(sdm_impl, write_update_block_operation) {
    SET_UP();
    msg.content_format = FLUF_COAP_FORMAT_OMA_LWM2M_TLV;
    msg.operation = FLUF_OP_DM_WRITE_PARTIAL_UPDATE;
    msg.uri = FLUF_MAKE_RESOURCE_PATH(111, 2, 4);
    msg.payload =
            "\xC8\x04\x10\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33";
    msg.payload_size = 16;
    msg.block.size = 16;
    msg.block.number = 0;
    msg.block.block_type = FLUF_OPTION_BLOCK_1;
    msg.block.more_flag = true;

    AVS_UNIT_ASSERT_EQUAL(sdm_process(&ctx, &dm, &msg, false, buff, buff_len),
                          SDM_IMPL_WANT_NEXT_MSG);
    AVS_UNIT_ASSERT_EQUAL(msg.operation, FLUF_OP_RESPONSE);
    AVS_UNIT_ASSERT_EQUAL(msg.content_format, FLUF_COAP_FORMAT_NOT_DEFINED);
    AVS_UNIT_ASSERT_EQUAL(msg.msg_code, FLUF_COAP_CODE_CONTINUE);

    AVS_UNIT_ASSERT_EQUAL(msg.block.size, 16);
    AVS_UNIT_ASSERT_EQUAL(msg.block.number, 0);
    AVS_UNIT_ASSERT_EQUAL(msg.block.block_type, FLUF_OPTION_BLOCK_1);
    AVS_UNIT_ASSERT_EQUAL(msg.block.more_flag, true);

    msg.content_format = FLUF_COAP_FORMAT_OMA_LWM2M_TLV;
    msg.operation = FLUF_OP_DM_WRITE_PARTIAL_UPDATE;
    msg.uri = FLUF_MAKE_RESOURCE_PATH(111, 2, 4);
    msg.payload = "\x33\x33\x33";
    msg.payload_size = 3;
    msg.block.size = 16;
    msg.block.number = 1;
    msg.block.block_type = FLUF_OPTION_BLOCK_1;
    msg.block.more_flag = false;

    AVS_UNIT_ASSERT_SUCCESS(
            sdm_process(&ctx, &dm, &msg, false, buff, buff_len));
    AVS_UNIT_ASSERT_EQUAL(msg.operation, FLUF_OP_RESPONSE);
    AVS_UNIT_ASSERT_EQUAL(msg.content_format, FLUF_COAP_FORMAT_NOT_DEFINED);
    AVS_UNIT_ASSERT_EQUAL(msg.msg_code, FLUF_COAP_CODE_CHANGED);

    AVS_UNIT_ASSERT_EQUAL(msg.block.size, 16);
    AVS_UNIT_ASSERT_EQUAL(msg.block.number, 1);
    AVS_UNIT_ASSERT_EQUAL(msg.block.block_type, FLUF_OPTION_BLOCK_1);
    AVS_UNIT_ASSERT_EQUAL(msg.block.more_flag, false);

    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(
            inst_2_res[4].value.res_value->value.bytes_or_string.data,
            "3333333333333333", 16);
}

AVS_UNIT_TEST(sdm_impl, create_with_write) {
    SET_UP();

    msg.content_format = FLUF_COAP_FORMAT_NOT_DEFINED;
    msg.operation = FLUF_OP_DM_CREATE;
    msg.uri = FLUF_MAKE_OBJECT_PATH(222);
    msg.content_format = FLUF_COAP_FORMAT_OMA_LWM2M_TLV;
    msg.payload = "\x03\x00\xC1\x01\x2B";
    msg.payload_size = 5;
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_process(&ctx, &dm, &msg, false, buff, buff_len));
    AVS_UNIT_ASSERT_EQUAL(msg.operation, FLUF_OP_RESPONSE);
    AVS_UNIT_ASSERT_EQUAL(msg.content_format, FLUF_COAP_FORMAT_NOT_DEFINED);
    AVS_UNIT_ASSERT_EQUAL(msg.msg_code, FLUF_COAP_CODE_CREATED);
    AVS_UNIT_ASSERT_EQUAL(obj_2_inst_2_res.value.res_value->value.int_value,
                          43);
    obj_2.inst_count--;
    obj_2_insts[0] = &obj_2_inst_1;
    obj_2_insts[1] = NULL;
}

AVS_UNIT_TEST(sdm_impl, create_with_write_no_iid_specify) {
    SET_UP();

    msg.content_format = FLUF_COAP_FORMAT_NOT_DEFINED;
    msg.operation = FLUF_OP_DM_CREATE;
    msg.uri = FLUF_MAKE_OBJECT_PATH(222);
    msg.content_format = FLUF_COAP_FORMAT_OMA_LWM2M_TLV;
    msg.payload = "\xC1\x01\x2A";
    msg.payload_size = 3;
    AVS_UNIT_ASSERT_SUCCESS(
            sdm_process(&ctx, &dm, &msg, false, buff, buff_len));
    AVS_UNIT_ASSERT_EQUAL(msg.operation, FLUF_OP_RESPONSE);
    AVS_UNIT_ASSERT_EQUAL(msg.content_format, FLUF_COAP_FORMAT_NOT_DEFINED);
    AVS_UNIT_ASSERT_EQUAL(msg.msg_code, FLUF_COAP_CODE_CREATED);
    AVS_UNIT_ASSERT_EQUAL(obj_2_inst_2_res.value.res_value->value.int_value,
                          42);
}

AVS_UNIT_TEST(sdm_impl, format_error) {
    SET_UP();
    msg.operation = FLUF_OP_DM_READ;
    msg.uri = FLUF_MAKE_OBJECT_PATH(222);
    msg.content_format = 333;
    msg.accept = FLUF_COAP_FORMAT_NOT_DEFINED - 1;
    AVS_UNIT_ASSERT_EQUAL(sdm_process(&ctx, &dm, &msg, false, buff, buff_len),
                          FLUF_IO_ERR_FORMAT);
    AVS_UNIT_ASSERT_EQUAL(msg.msg_code, FLUF_COAP_CODE_BAD_REQUEST);
}

AVS_UNIT_TEST(sdm_impl, not_found_error) {
    SET_UP();
    msg.operation = FLUF_OP_DM_READ;
    msg.uri = FLUF_MAKE_INSTANCE_PATH(222, 2);
    msg.content_format = 333;
    AVS_UNIT_ASSERT_EQUAL(sdm_process(&ctx, &dm, &msg, false, buff, buff_len),
                          SDM_ERR_NOT_FOUND);
    AVS_UNIT_ASSERT_EQUAL(msg.msg_code, FLUF_COAP_CODE_NOT_FOUND);
}

AVS_UNIT_TEST(sdm_impl, block_op_error_1) {
    SET_UP();
    msg.content_format = FLUF_COAP_FORMAT_OMA_LWM2M_TLV;
    msg.operation = FLUF_OP_DM_WRITE_PARTIAL_UPDATE;
    msg.uri = FLUF_MAKE_RESOURCE_PATH(111, 2, 4);
    msg.payload =
            "\xC8\x04\x10\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33";
    msg.payload_size = 16;
    msg.block.size = 16;
    msg.block.number = 0;
    msg.block.block_type = FLUF_OPTION_BLOCK_1;
    msg.block.more_flag = true;

    AVS_UNIT_ASSERT_EQUAL(sdm_process(&ctx, &dm, &msg, false, buff, buff_len),
                          SDM_IMPL_WANT_NEXT_MSG);
    msg.content_format = FLUF_COAP_FORMAT_OMA_LWM2M_TLV;
    msg.operation = FLUF_OP_DM_WRITE_PARTIAL_UPDATE;
    msg.uri = FLUF_MAKE_RESOURCE_PATH(111, 2, 4);
    msg.payload = "\x33\x33\x33";
    msg.payload_size = 3;
    msg.block.size = 16;
    msg.block.number = 0;
    msg.block.block_type = FLUF_OPTION_BLOCK_1;
    msg.block.more_flag = false;
    AVS_UNIT_ASSERT_EQUAL(sdm_process(&ctx, &dm, &msg, false, buff, buff_len),
                          SDM_ERR_INPUT_ARG);
    AVS_UNIT_ASSERT_EQUAL(msg.msg_code,
                          FLUF_COAP_CODE_REQUEST_ENTITY_INCOMPLETE);
}

AVS_UNIT_TEST(sdm_impl, block_op_error_2) {
    SET_UP();
    msg.content_format = FLUF_COAP_FORMAT_OMA_LWM2M_TLV;
    msg.operation = FLUF_OP_DM_WRITE_PARTIAL_UPDATE;
    msg.uri = FLUF_MAKE_RESOURCE_PATH(111, 2, 4);
    msg.payload =
            "\xC8\x04\x10\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33";
    msg.payload_size = 16;
    msg.block.size = 16;
    msg.block.number = 0;
    msg.block.block_type = FLUF_OPTION_BLOCK_1;
    msg.block.more_flag = true;

    AVS_UNIT_ASSERT_EQUAL(sdm_process(&ctx, &dm, &msg, false, buff, buff_len),
                          SDM_IMPL_WANT_NEXT_MSG);
    msg.content_format = FLUF_COAP_FORMAT_OMA_LWM2M_TLV;
    msg.operation = FLUF_OP_DM_WRITE_PARTIAL_UPDATE;
    msg.uri = FLUF_MAKE_RESOURCE_PATH(111, 2, 4);
    msg.payload = "\x33\x33\x33";
    msg.payload_size = 3;
    msg.block.size = 16;
    msg.block.number = 2;
    msg.block.block_type = FLUF_OPTION_BLOCK_1;
    msg.block.more_flag = false;
    AVS_UNIT_ASSERT_EQUAL(sdm_process(&ctx, &dm, &msg, false, buff, buff_len),
                          SDM_ERR_INPUT_ARG);
    AVS_UNIT_ASSERT_EQUAL(msg.msg_code,
                          FLUF_COAP_CODE_REQUEST_ENTITY_INCOMPLETE);
}

AVS_UNIT_TEST(sdm_impl, validate_error) {
    SET_UP();
    validate_error = true;
    msg.content_format = FLUF_COAP_FORMAT_OMA_LWM2M_TLV;
    msg.operation = FLUF_OP_DM_WRITE_PARTIAL_UPDATE;
    msg.uri = FLUF_MAKE_INSTANCE_PATH(111, 1);
    msg.payload = "\xC1\x01\x2A";
    msg.payload_size = 3;
    AVS_UNIT_ASSERT_EQUAL(sdm_process(&ctx, &dm, &msg, false, buff, buff_len),
                          SDM_ERR_BAD_REQUEST);
    AVS_UNIT_ASSERT_EQUAL(msg.operation, FLUF_OP_RESPONSE);
    AVS_UNIT_ASSERT_EQUAL(msg.content_format, FLUF_COAP_FORMAT_NOT_DEFINED);
    AVS_UNIT_ASSERT_EQUAL(msg.payload_size, 0);
    AVS_UNIT_ASSERT_EQUAL(msg.msg_code, FLUF_COAP_CODE_BAD_REQUEST);
    validate_error = false;
}
