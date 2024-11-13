/*
 * Copyright 2017-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#include <avsystem/commons/avs_memory.h>
#include <avsystem/commons/avs_stream_outbuf.h>
#include <avsystem/commons/avs_utils.h>

#define AVS_UNIT_ENABLE_SHORT_ASSERTS
#include <avsystem/commons/avs_unit_test.h>

#define BUFFER_SIZE 128

#define TEST_ROOT_PATH (anjay_uri_path_t) ROOT_PATH_INITIALIZER()
#define TEST_OBJ_INST(Obj, Inst) \
    (anjay_uri_path_t) INSTANCE_PATH_INITIALIZER(Obj, Inst)
#define TEST_OBJ_INST_RES(Obj, Inst, Res) \
    (anjay_uri_path_t) RESOURCE_PATH_INITIALIZER(Obj, Inst, Res)
#define TEST_OBJ_INST_RES_INST(Obj, Inst, Res, ResInst) \
    (anjay_uri_path_t)                                  \
            RESOURCE_INSTANCE_PATH_INITIALIZER(Obj, Inst, Res, ResInst)

#define TEST_ENV(Path, ItemsCount)                                     \
    static char stream_buffer[BUFFER_SIZE] = { 0 };                    \
    avs_stream_outbuf_t stream = AVS_STREAM_OUTBUF_STATIC_INITIALIZER; \
    avs_stream_outbuf_set_buffer(&stream, stream_buffer, BUFFER_SIZE); \
    anjay_unlocked_output_ctx_t *out;                                  \
    const size_t items_size = (ItemsCount);                            \
    out = _anjay_output_senml_like_create((avs_stream_t *) &stream,    \
                                          &(Path),                     \
                                          AVS_COAP_FORMAT_SENML_CBOR,  \
                                          &items_size);                \
    ASSERT_NOT_NULL(out);                                              \
    ASSERT_OK(_anjay_output_set_path(out, &(Path)));

#define TEST_TEARDOWN(ExpectedData)                                       \
    do {                                                                  \
        ASSERT_OK(_anjay_output_ctx_destroy(&out));                       \
        ASSERT_EQ_BYTES_SIZED(                                            \
                stream_buffer, (ExpectedData), sizeof(ExpectedData) - 1); \
    } while (0)

AVS_UNIT_TEST(senml_cbor_out, single_resource) {
    TEST_ENV(TEST_OBJ_INST_RES(13, 26, 1), 1);

    ASSERT_OK(_anjay_ret_i64_unlocked(out, 42));

    // clang-format off
    static const char EXPECTED_DATA[] = {
        // [
        //   {"bn": "/13/26/1", "v": 42}
        // ]
        "\x81"                                         // array(1)
            "\xA2"                                     // map(2)
                "\x21"                                 // negative(1) "bn"
                "\x68"                                 // text(8)
                    "\x2F\x31\x33\x2F\x32\x36\x2F\x31" // "/13/26/1"
                "\x02"                                 // unsigned(2) "v"
                "\x18\x2A"                             // unsigned(42)
    };
    // clang-format on
    TEST_TEARDOWN(EXPECTED_DATA);
}

AVS_UNIT_TEST(senml_cbor_out, two_resources) {
    TEST_ENV(TEST_OBJ_INST(13, 26), 2);

    ASSERT_OK(_anjay_output_start_aggregate(out));
    ASSERT_OK(_anjay_output_set_path(out, &TEST_OBJ_INST_RES(13, 26, 1)));
    ASSERT_OK(_anjay_ret_i64_unlocked(out, 42));
    ASSERT_OK(_anjay_output_set_path(out, &TEST_OBJ_INST_RES(13, 26, 2)));
    ASSERT_OK(_anjay_ret_i64_unlocked(out, 21));

    // clang-format off
    static const char EXPECTED_DATA[] = {
        // [
        //   {"bn": "/13/26", "n": "/1", "v": 42},
        //   {"n": "/2", "v": 21}
        // ]
        "\x82"                                 // array(2)
            "\xA3"                             // map(3)
                "\x21"                         // negative(1) "bn"
                "\x66"                         // text(6)
                    "\x2F\x31\x33\x2F\x32\x36" // "/13/26"
                "\x00"                         // unsigned(0) "n"
                "\x62"                         // text(2)
                    "\x2F\x31"                 // "/1"
                "\x02"                         // unsigned(2) "v"
                "\x18\x2A"                     // unsigned(42)
            "\xA2"                             // map(2)
                "\x00"                         // unsigned(0) "n"
                "\x62"                         // text(2)
                    "\x2F\x32"                 // "/2"
                "\x02"                         // unsigned(2) "v"
                "\x15"                         // unsigned(21)

    };
    // clang-format on
    TEST_TEARDOWN(EXPECTED_DATA);
}

AVS_UNIT_TEST(senml_cbor_out, resource_instances_nested_maps) {
    TEST_ENV(TEST_OBJ_INST(13, 26), 3);

    ASSERT_OK(_anjay_output_start_aggregate(out));
    ASSERT_OK(_anjay_output_set_path(out, &TEST_OBJ_INST_RES(13, 26, 1)));
    ASSERT_OK(_anjay_ret_i64_unlocked(out, 42));
    ASSERT_OK(_anjay_output_set_path(out,
                                     &TEST_OBJ_INST_RES_INST(13, 26, 3, 21)));
    ASSERT_OK(_anjay_ret_double_unlocked(out, 69.68));
    ASSERT_OK(_anjay_output_set_path(out,
                                     &TEST_OBJ_INST_RES_INST(13, 26, 3, 37)));
    ASSERT_OK(_anjay_ret_bool_unlocked(out, false));

    // clang-format off
    static const char EXPECTED_DATA[] = {
        // [
        //   {"bn": "/13/26", "n": "/1", "v": 42},
        //   {"n": "/3/21", "v": 69.68},
        //   {"n": "/3/37", "vb": false}
        // ]
        "\x83"                                         // array(3)
            "\xA3"                                     // map(3)
                "\x21"                                 // negative(1) "bn"
                "\x66"                                 // text(6)
                    "\x2F\x31\x33\x2F\x32\x36"         // "/13/26"
                "\x00"                                 // unsigned(0) "n"
                "\x62"                                 // text(2)
                    "\x2F\x31"                         // "/1"
                "\x02"                                 // unsigned(2) "v"
                "\x18\x2A"                             // unsigned(42)
            "\xA2"                                     // map(2)
                "\x00"                                 // unsigned(0) "n"
                "\x65"                                 // text(5)
                    "\x2F\x33\x2F\x32\x31"             // "/3/21"
                "\x02"                                 // unsigned(2) "v"
                "\xFB\x40\x51\x6B\x85\x1E\xB8\x51\xEC" // primitive(4634603711031169516)
            "\xA2"                                     // map(2)
                "\x00"                                 // unsigned(0) "n"
                "\x65"                                 // text(5)
                    "\x2F\x33\x2F\x33\x37"             // "/3/37"
                "\x04"                                 // unsigned(4) "vb"
                "\xF4"                                 // primitive(20)
    };
    // clang-format on
    TEST_TEARDOWN(EXPECTED_DATA);
}

AVS_UNIT_TEST(senml_cbor_out, two_objects_one_instance_two_resources) {
    TEST_ENV(TEST_ROOT_PATH, 4);

    ASSERT_OK(_anjay_output_set_path(out, &TEST_OBJ_INST_RES(13, 26, 1)));
    ASSERT_OK(_anjay_ret_i64_unlocked(out, 42));
    ASSERT_OK(_anjay_output_set_path(out, &TEST_OBJ_INST_RES(13, 26, 2)));
    ASSERT_OK(_anjay_ret_i64_unlocked(out, 21));

    ASSERT_OK(_anjay_output_set_path(out, &TEST_OBJ_INST_RES(14, 27, 1)));
    ASSERT_OK(_anjay_ret_i64_unlocked(out, 43));
    ASSERT_OK(_anjay_output_set_path(out, &TEST_OBJ_INST_RES(14, 27, 2)));
    ASSERT_OK(_anjay_ret_i64_unlocked(out, 22));

    // clang-format off
    static const char EXPECTED_DATA[] = {
        // [
        //   {"n": "/13/26/1", "v": 42},
        //   {"n": "/13/26/2", "v": 21},
        //   {"n": "/14/27/1", "v": 43},
        //   {"n": "/14/27/2", "v": 22}
        // ]
        "\x84"                                         // array(4)
            "\xA2"                                     // map(2)
                "\x00"                                 // unsigned(0) "n"
                "\x68"                                 // text(8)
                    "\x2F\x31\x33\x2F\x32\x36\x2F\x31" // "/13/26/1"
                "\x02"                                 // unsigned(2) "v"
                "\x18\x2A"                             // unsigned(42)
            "\xA2"                                     // map(2)
                "\x00"                                 // unsigned(0) "n"
                "\x68"                                 // text(8)
                    "\x2F\x31\x33\x2F\x32\x36\x2F\x32" // "/13/26/2"
                "\x02"                                 // unsigned(2) "v"
                "\x15"                                 // unsigned(21)
            "\xA2"                                     // map(2)
                "\x00"                                 // unsigned(0) "n"
                "\x68"                                 // text(8)
                    "\x2F\x31\x34\x2F\x32\x37\x2F\x31" // "/14/27/1"
                "\x02"                                 // unsigned(2) "v"
                "\x18\x2B"                             // unsigned(43)
            "\xA2"                                     // map(2)
                "\x00"                                 // unsigned(0) "n"
                "\x68"                                 // text(8)
                    "\x2F\x31\x34\x2F\x32\x37\x2F\x32" // "/14/27/2"
                "\x02"                                 // unsigned(2) "v"
                "\x16"                                 // unsigned(22)
    };
    // clang-format on
    TEST_TEARDOWN(EXPECTED_DATA);
}
