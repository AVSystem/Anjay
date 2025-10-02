/*
 * Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#include <avsystem/commons/avs_memory.h>
#include <avsystem/commons/avs_stream_outbuf.h>
#include <avsystem/commons/avs_utils.h>

#define AVS_UNIT_ENABLE_SHORT_ASSERTS
#include <avsystem/commons/avs_unit_test.h>

#ifdef ANJAY_WITH_LWM2M_GATEWAY
#    include <string.h>

#    include <anjay/core.h>
#endif // ANJAY_WITH_LWM2M_GATEWAY

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

#ifdef ANJAY_WITH_LWM2M_GATEWAY

static void set_prefix(anjay_uri_path_t *uri, const char *prefix) {
    assert(prefix);
    size_t prefix_len = strlen(prefix);
    assert(prefix_len < ANJAY_GATEWAY_MAX_PREFIX_LEN);
    strcpy((void *) uri->prefix, (const void *) prefix);
}

#    define CREATE_PATH_WITH_PREFIX(Var, Ids, Prefix) \
        anjay_uri_path_t(Var) = (Ids);                \
        set_prefix(&(Var), (Prefix));

AVS_UNIT_TEST(lwm2m_cbor_out, single_resource_with_prefix) {
    CREATE_PATH_WITH_PREFIX(test_path, TEST_OBJ_INST_RES(13, 26, 1), "dev1");

    TEST_ENV(test_path);

    ASSERT_OK(_anjay_ret_i64_unlocked(out, 42));

    // clang-format off
    static const char EXPECTED_DATA[] = {
        // {["dev1", 13, 26, 1]: 42}
        "\xBF"                         // map(*)
            "\x84"                     // array(4)
                "\x64"                 // text(4)
                    "\x64\x65\x76\x31" // "dev1"
                "\x0D"                 // unsigned(13)
                "\x18\x1A"             // unsigned(26)
                "\x01"                 // unsigned(1)
            "\x18\x2A"                 // unsigned(42)
            "\xFF"                     // primitive(*)
    };
    // clang-format on
    TEST_TEARDOWN(EXPECTED_DATA);
}

AVS_UNIT_TEST(lwm2m_cbor_out, two_resources_with_prefix) {
    CREATE_PATH_WITH_PREFIX(test_path1, TEST_OBJ_INST(13, 26), "dev1");
    CREATE_PATH_WITH_PREFIX(test_path2, TEST_OBJ_INST_RES(13, 26, 1), "dev1");
    CREATE_PATH_WITH_PREFIX(test_path3, TEST_OBJ_INST_RES(13, 26, 2), "dev1");

    TEST_ENV(test_path1);

    ASSERT_OK(_anjay_output_start_aggregate(out));
    ASSERT_OK(_anjay_output_set_path(out, &test_path2));
    ASSERT_OK(_anjay_ret_i64_unlocked(out, 42));
    ASSERT_OK(_anjay_output_set_path(out, &test_path3));
    ASSERT_OK(_anjay_ret_i64_unlocked(out, 21));

    // clang-format off
    static const char EXPECTED_DATA[] = {
        // {["dev1", 13, 26]: {1: 42, 2: 21}}
        "\xBF"                         // map(*)
            "\x83"                     // array(3)
                "\x64"                 // text(4)
                    "\x64\x65\x76\x31" // "dev1"
                "\x0D"                 // unsigned(13)
                "\x18\x1A"             // unsigned(26)
            "\xBF"                     // map(*)
                "\x01"                 // unsigned(1)
                "\x18\x2A"             // unsigned(42)
                "\x02"                 // unsigned(2)
                "\x15"                 // unsigned(21)
                "\xFF"                 // primitive(*)
           "\xFF"                      // primitive(*)
    };
    // clang-format on
    TEST_TEARDOWN(EXPECTED_DATA);
}

AVS_UNIT_TEST(lwm2m_cbor_out, two_objects_different_prefixes_max_nesting) {
    CREATE_PATH_WITH_PREFIX(test_root_path, TEST_ROOT_PATH, "");
    CREATE_PATH_WITH_PREFIX(
            test_path1, TEST_OBJ_INST_RES_INST(13, 26, 1, 7), "dev1");
    CREATE_PATH_WITH_PREFIX(
            test_path2, TEST_OBJ_INST_RES_INST(13, 26, 1, 8), "dev1");
    CREATE_PATH_WITH_PREFIX(
            test_path3, TEST_OBJ_INST_RES_INST(14, 27, 1, 5), "dev2");
    CREATE_PATH_WITH_PREFIX(
            test_path4, TEST_OBJ_INST_RES_INST(14, 27, 2, 6), "dev2");

    TEST_ENV(test_root_path);

    ASSERT_OK(_anjay_output_set_path(out, &test_path1));
    ASSERT_OK(_anjay_ret_i64_unlocked(out, 42));
    ASSERT_OK(_anjay_output_set_path(out, &test_path2));
    ASSERT_OK(_anjay_ret_i64_unlocked(out, 21));

    ASSERT_OK(_anjay_output_set_path(out, &test_path3));
    ASSERT_OK(_anjay_ret_i64_unlocked(out, 43));
    ASSERT_OK(_anjay_output_set_path(out, &test_path4));
    ASSERT_OK(_anjay_ret_i64_unlocked(out, 22));

    // clang-format off
    static const char EXPECTED_DATA[] = {
        // {
        //  "dev1": {13: {26: {1: {7: 42, 8: 21}}}},
        //  "dev2": {14: {27: {1: {5: 43}, 2: {6: 22}}}}
        // }
        "\xBF"                    // map(*)
           "\x64"                 // text(4)
              "\x64\x65\x76\x31"  // "dev1"
           "\xBF"                 // map(*)
              "\x0D"              // unsigned(13)
              "\xBF"              // map(*)
                 "\x18\x1A"       // unsigned(26)
                 "\xBF"           // map(*)
                    "\x01"        // unsigned(1)
                    "\xBF"        // map(*)
                       "\x07"     // unsigned(7)
                       "\x18\x2A" // unsigned(42)
                       "\x08"     // unsigned(8)
                       "\x15"     // unsigned(21)
                       "\xFF"     // primitive(*)
                    "\xFF"        // primitive(*)
                 "\xFF"           // primitive(*)
              "\xFF"              // primitive(*)
           "\x64"                 // text(4)
              "\x64\x65\x76\x32"  // "dev2"
           "\xBF"                 // map(*)
              "\x0E"              // unsigned(14)
              "\xBF"              // map(*)
                 "\x18\x1B"       // unsigned(27)
                 "\xBF"           // map(*)
                    "\x01"        // unsigned(1)
                    "\xBF"        // map(*)
                       "\x05"     // unsigned(5)
                       "\x18\x2B" // unsigned(43)
                       "\xFF"     // primitive(*)
                    "\x02"        // unsigned(2)
                    "\xBF"        // map(*)
                       "\x06"     // unsigned(6)
                       "\x16"     // unsigned(22)
                       "\xFF"     // primitive(*)
                    "\xFF"        // primitive(*)
                 "\xFF"           // primitive(*)
              "\xFF"              // primitive(*)
           "\xFF"                 // primitive(*)
    };
    // clang-format on
    TEST_TEARDOWN(EXPECTED_DATA);
}

AVS_UNIT_TEST(lwm2m_cbor_out, three_objects_different_prefixes) {
    CREATE_PATH_WITH_PREFIX(test_root_path, TEST_ROOT_PATH, "");
    CREATE_PATH_WITH_PREFIX(test_path1, TEST_OBJ_INST_RES(13, 26, 1), "dev1");
    CREATE_PATH_WITH_PREFIX(test_path2, TEST_OBJ_INST_RES(14, 27, 1), "dev2");
    CREATE_PATH_WITH_PREFIX(test_path3, TEST_OBJ_INST_RES(15, 28, 1), "dev3");

    TEST_ENV(test_root_path);

    ASSERT_OK(_anjay_output_set_path(out, &test_path1));
    ASSERT_OK(_anjay_ret_i64_unlocked(out, 42));
    ASSERT_OK(_anjay_output_set_path(out, &test_path2));
    ASSERT_OK(_anjay_ret_i64_unlocked(out, 43));
    ASSERT_OK(_anjay_output_set_path(out, &test_path3));
    ASSERT_OK(_anjay_ret_i64_unlocked(out, 44));

    // clang-format off
    static const char EXPECTED_DATA[] = {
        // {
        //  "dev1": {13: {26: {1: 42}}},
        //  "dev2": {14: {27: {1: 43}}},
        //  "dev3": {15: {28: {1: 44}}}
        // }
        "\xBF"                    // map(*)
           "\x64"                 // text(4)
              "\x64\x65\x76\x31"  // "dev1"
           "\xBF"                 // map(*)
              "\x0D"              // unsigned(13)
              "\xBF"              // map(*)
                 "\x18\x1A"       // unsigned(26)
                 "\xBF"           // map(*)
                    "\x01"        // unsigned(1)
                       "\x18\x2A" // unsigned(42)
                    "\xFF"        // primitive(*)
                 "\xFF"           // primitive(*)
              "\xFF"              // primitive(*)
           "\x64"                 // text(4)
              "\x64\x65\x76\x32"  // "dev2"
           "\xBF"                 // map(*)
              "\x0E"              // unsigned(14)
              "\xBF"              // map(*)
                 "\x18\x1B"       // unsigned(27)
                 "\xBF"           // map(*)
                    "\x01"        // unsigned(1)
                       "\x18\x2B" // unsigned(43)
                    "\xFF"        // primitive(*)
                 "\xFF"           // primitive(*)
              "\xFF"              // primitive(*)
           "\x64"                 // text(4)
              "\x64\x65\x76\x33"  // "dev3"
           "\xBF"                 // map(*)
              "\x0F"              // unsigned(15)
              "\xBF"              // map(*)
                 "\x18\x1C"       // unsigned(28)
                 "\xBF"           // map(*)
                    "\x01"        // unsigned(1)
                       "\x18\x2C" // unsigned(44)
                    "\xFF"        // primitive(*)
                 "\xFF"           // primitive(*)
              "\xFF"              // primitive(*)
           "\xFF"                 // primitive(*)
    };
    // clang-format on
    TEST_TEARDOWN(EXPECTED_DATA);
}

AVS_UNIT_TEST(lwm2m_cbor_out, root_path_with_prefix) {
    CREATE_PATH_WITH_PREFIX(test_root_path, TEST_ROOT_PATH, "dev1");
    CREATE_PATH_WITH_PREFIX(
            test_path1, TEST_OBJ_INST_RES_INST(13, 26, 1, 7), "dev1");
    CREATE_PATH_WITH_PREFIX(
            test_path2, TEST_OBJ_INST_RES_INST(13, 26, 1, 8), "dev1");

    TEST_ENV(test_root_path);

    ASSERT_OK(_anjay_output_clear_path(out));
    ASSERT_OK(_anjay_output_set_path(out, &test_path1));
    ASSERT_OK(_anjay_ret_i64_unlocked(out, 42));
    ASSERT_OK(_anjay_output_set_path(out, &test_path2));
    ASSERT_OK(_anjay_ret_i64_unlocked(out, 21));

    // clang-format off
    static const char EXPECTED_DATA[] = {
        // {"dev1": {13: {26: {1: {7: 42, 8: 21}}}}}
        "\xBF"                    // map(*)
           "\x64"                 // text(4)
              "\x64\x65\x76\x31"  // "dev1"
           "\xBF"                 // map(*)
              "\x0D"              // unsigned(13)
              "\xBF"              // map(*)
                 "\x18\x1A"       // unsigned(26)
                 "\xBF"           // map(*)
                    "\x01"        // unsigned(1)
                    "\xBF"        // map(*)
                       "\x07"     // unsigned(7)
                       "\x18\x2A" // unsigned(42)
                       "\x08"     // unsigned(8)
                       "\x15"     // unsigned(21)
                       "\xFF"     // primitive(*)
                    "\xFF"        // primitive(*)
                 "\xFF"           // primitive(*)
              "\xFF"              // primitive(*)
           "\xFF"                 // primitive(*)
    };
    // clang-format on
    TEST_TEARDOWN(EXPECTED_DATA);
}

AVS_UNIT_TEST(lwm2m_cbor_out, mixed_data_end_device_with_gateway) {
    CREATE_PATH_WITH_PREFIX(test_root_path, TEST_ROOT_PATH, "");
    CREATE_PATH_WITH_PREFIX(test_path1, TEST_OBJ_INST_RES(13, 26, 1), "dev1");
    CREATE_PATH_WITH_PREFIX(test_path2, TEST_OBJ_INST_RES(14, 27, 1), "");

    TEST_ENV(test_root_path);

    ASSERT_OK(_anjay_output_set_path(out, &test_path1));
    ASSERT_OK(_anjay_ret_i64_unlocked(out, 42));
    ASSERT_OK(_anjay_output_set_path(out, &test_path2));
    ASSERT_OK(_anjay_ret_i64_unlocked(out, 21));

    // clang-format off
    static const char EXPECTED_DATA[] = {
        // {"dev1": {13: {26: {1: 42}}}, 14: {27: {1: 21}}}
        "\xBF"                     // map(*)
            "\x64"                 // text(4)
                "\x64\x65\x76\x31" // "dev1"
            "\xBF"                 // map(*)
                "\x0D"             // unsigned(13)
                "\xBF"             // map(*)
                    "\x18\x1A"     // unsigned(26)
                    "\xBF"         // map(*)
                        "\x01"     // unsigned(1)
                        "\x18\x2A" // unsigned(42)
                        "\xFF"     // primitive(*)
                    "\xFF"         // primitive(*)
                "\xFF"             // primitive(*)
            "\x0E"                 // unsigned(14)
            "\xBF"                 // map(*)
                "\x18\x1B"         // unsigned(27)
                "\xBF"             // map(*)
                    "\x01"         // unsigned(1)
                    "\x15"         // unsigned(21)
                    "\xFF"         // primitive(*)
                "\xFF"             // primitive(*)
            "\xFF"                 // primitive(*)
    };
    // clang-format on
    TEST_TEARDOWN(EXPECTED_DATA);
}

#endif // ANJAY_WITH_LWM2M_GATEWAY
