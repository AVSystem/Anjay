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

#define BUFFER_SIZE 64

#define TEST_ROOT_PATH (anjay_uri_path_t) ROOT_PATH_INITIALIZER()
#define TEST_OBJ_INST(Obj, Inst) \
    (anjay_uri_path_t) INSTANCE_PATH_INITIALIZER(Obj, Inst)
#define TEST_OBJ_INST_RES(Obj, Inst, Res) \
    (anjay_uri_path_t) RESOURCE_PATH_INITIALIZER(Obj, Inst, Res)
#define TEST_OBJ_INST_RES_INST(Obj, Inst, Res, ResInst) \
    (anjay_uri_path_t)                                  \
            RESOURCE_INSTANCE_PATH_INITIALIZER(Obj, Inst, Res, ResInst)

#define TEST_ENV(Path)                                                        \
    static char stream_buffer[BUFFER_SIZE] = { 0 };                           \
    avs_stream_outbuf_t stream = AVS_STREAM_OUTBUF_STATIC_INITIALIZER;        \
    avs_stream_outbuf_set_buffer(&stream, stream_buffer, BUFFER_SIZE);        \
    anjay_unlocked_output_ctx_t *out;                                         \
    out = _anjay_output_lwm2m_cbor_create((avs_stream_t *) &stream, &(Path)); \
    ASSERT_NOT_NULL(out);                                                     \
    ASSERT_OK(_anjay_output_set_path(out, &(Path)));

#define TEST_TEARDOWN(ExpectedData)                                       \
    do {                                                                  \
        ASSERT_OK(_anjay_output_ctx_destroy(&out));                       \
        ASSERT_EQ_BYTES_SIZED(                                            \
                stream_buffer, (ExpectedData), sizeof(ExpectedData) - 1); \
    } while (0)

AVS_UNIT_TEST(lwm2m_cbor_out, single_resource) {
    TEST_ENV(TEST_OBJ_INST_RES(13, 26, 1));

    ASSERT_OK(_anjay_ret_i64_unlocked(out, 42));

    // clang-format off
    static const char EXPECTED_DATA[] = {
        // {[13, 26, 1]: 42}
        "\xBF"             // map(*)
            "\x83"         // array(3)
                "\x0D"     // unsigned(13)
                "\x18\x1A" // unsigned(26)
                "\x01"     // unsigned(1)
            "\x18\x2A"     // unsigned(42)
            "\xFF"         // primitive(*)
    };
    // clang-format on
    TEST_TEARDOWN(EXPECTED_DATA);
}

AVS_UNIT_TEST(lwm2m_cbor_out, two_resources) {
    TEST_ENV(TEST_OBJ_INST(13, 26));

    ASSERT_OK(_anjay_output_start_aggregate(out));
    ASSERT_OK(_anjay_output_set_path(out, &TEST_OBJ_INST_RES(13, 26, 1)));
    ASSERT_OK(_anjay_ret_i64_unlocked(out, 42));
    ASSERT_OK(_anjay_output_set_path(out, &TEST_OBJ_INST_RES(13, 26, 2)));
    ASSERT_OK(_anjay_ret_i64_unlocked(out, 21));

    // clang-format off
    static const char EXPECTED_DATA[] = {
        // {[13, 26]: {1: 42, 2: 21}}
        "\xBF"             // map(*)
            "\x82"         // array(2)
                "\x0D"     // unsigned(13)
                "\x18\x1A" // unsigned(26)
            "\xBF"         // map(*)
                "\x01"     // unsigned(1)
                "\x18\x2A" // unsigned(42)
                "\x02"     // unsigned(2)
                "\x15"     // unsigned(21)
                "\xFF"     // primitive(*)
           "\xFF"          // primitive(*)
    };
    // clang-format on
    TEST_TEARDOWN(EXPECTED_DATA);
}

AVS_UNIT_TEST(lwm2m_cbor_out, resource_instances_nested_maps) {
    TEST_ENV(TEST_OBJ_INST(13, 26));

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
        // {[13, 26]: {1: 42, 3: {21: 69.68, 37: false}}}
        "\xBF"                                             // map(*)
            "\x82"                                         // array(2)
                "\x0D"                                     // unsigned(13)
                "\x18\x1A"                                 // unsigned(26)
            "\xBF"                                         // map(*)
                "\x01"                                     // unsigned(1)
                "\x18\x2A"                                 // unsigned(42)
                "\x03"                                     // unsigned(2)
                "\xBF"                                     // map(*)
                    "\x15"                                 // unsigned(21)
                    "\xFB\x40\x51\x6B\x85\x1E\xB8\x51\xEC" // primitive(69.68)
                    "\x18\x25"                             // unsigned(37)
                    "\xF4"                                 // primitive(false)
                    "\xFF"                                 // primitive(*)
                "\xFF"                                     // primitive(*)
           "\xFF"                                          // primitive(*)
    };
    // clang-format on
    TEST_TEARDOWN(EXPECTED_DATA);
}

AVS_UNIT_TEST(lwm2m_cbor_out, two_objects_one_instance_two_resources) {
    TEST_ENV(TEST_ROOT_PATH);

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
        // {13: {26: {1: 42, 2: 21}}, 14: {27: {1: 43, 2: 22}}}
        "\xBF"                 // map(*)
            "\x0D"             // unsigned(13)
            "\xBF"             // map(*)
                "\x18\x1A"     // unsigned(26)
                "\xBF"         // map(*)
                    "\x01"     // unsigned(1)
                    "\x18\x2A" // unsigned(42)
                    "\x02"     // unsigned(2)
                    "\x15"     // unsigned(21)
                    "\xFF"     // primitive(*)
                "\xFF"         // primitive(*)
            "\x0E"             // unsigned(14)
            "\xBF"             // map(*)
                "\x18\x1B"     // unsigned(27)
                "\xBF"         // map(*)
                    "\x01"     // unsigned(1)
                    "\x18\x2B" // unsigned(43)
                    "\x02"     // unsigned(2)
                    "\x16"     // unsigned(22)
                    "\xFF"     // primitive(*)
                "\xFF"         // primitive(*)
            "\xFF"             // primitive(*)

    };
    // clang-format on
    TEST_TEARDOWN(EXPECTED_DATA);
}
