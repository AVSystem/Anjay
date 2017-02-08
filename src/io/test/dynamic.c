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

#include <avsystem/commons/stream_v_table.h>

#include <avsystem/commons/unit/mocksock.h>
#include <avsystem/commons/unit/test.h>

#include <anjay_test/coap/stream.h>

#include <anjay/anjay.h>

/////////////////////////////////////////////////////////////////////// ENCODING

static int32_t COAP_FORMAT;

static int test_setup_for_sending(avs_stream_abstract_t *stream,
                                  const anjay_msg_details_t *details) {
    (void) stream;
    AVS_UNIT_ASSERT_TRUE(COAP_FORMAT < 0);
    COAP_FORMAT = details->format;
    return 0;
}

static const anjay_coap_stream_ext_t COAPIZATION = {
    .setup_response = test_setup_for_sending,
};


static const avs_stream_v_table_extension_t COAPIZED_VTABLE_EXT[] = {
    { ANJAY_COAP_STREAM_EXTENSION, &COAPIZATION },
    AVS_STREAM_V_TABLE_EXTENSION_NULL
};

static avs_stream_v_table_t COAPIZED_VTABLE;

AVS_UNIT_SUITE_INIT(dynamic_out, verbose) {
    (void) verbose;
    memcpy(&COAPIZED_VTABLE, AVS_STREAM_OUTBUF_STATIC_INITIALIZER.vtable,
           sizeof(COAPIZED_VTABLE));

    COAPIZED_VTABLE.extension_list = COAPIZED_VTABLE_EXT;
}

#define DETAILS_TEMPLATE(Format) { \
    .msg_type = ANJAY_COAP_MSG_NON_CONFIRMABLE, \
    .format = Format \
}

static const avs_stream_outbuf_t COAPIZED_OUTBUF
        = {&COAPIZED_VTABLE, NULL, 0, 0, 0};

#define TEST_ENV_WITH_FORMAT(Size, Format) \
    char buf[Size]; \
    anjay_msg_details_t details = DETAILS_TEMPLATE(Format); \
    avs_stream_outbuf_t outbuf = COAPIZED_OUTBUF; \
    avs_stream_outbuf_set_buffer(&outbuf, buf, sizeof(buf)); \
    COAP_FORMAT = -1; \
    int outctx_errno = 0; \
    anjay_output_ctx_t *out = \
            _anjay_output_dynamic_create((avs_stream_abstract_t *) &outbuf, \
                                         &outctx_errno, &details)

#define TEST_ENV(Size) TEST_ENV_WITH_FORMAT(Size, ANJAY_COAP_FORMAT_NONE)

#define VERIFY_BYTES(Data) do { \
    AVS_UNIT_ASSERT_EQUAL(avs_stream_outbuf_offset(&outbuf), \
                          sizeof(Data) - 1); \
    AVS_UNIT_ASSERT_EQUAL_BYTES(buf, Data); \
} while (0)

AVS_UNIT_TEST(dynamic_out, bytes) {
    TEST_ENV(512);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_set_id(out, ANJAY_ID_RID, 42));
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_bytes(out, "1234567890", 10));
    AVS_UNIT_ASSERT_FAILED(anjay_ret_bytes(out, "0987654321", 10));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out));

    VERIFY_BYTES("1234567890");
    AVS_UNIT_ASSERT_EQUAL(COAP_FORMAT, ANJAY_COAP_FORMAT_OPAQUE);
}

AVS_UNIT_TEST(dynamic_out, string) {
    TEST_ENV(512);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_set_id(out, ANJAY_ID_RID, 42));
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_string(out, "0987654321"));
    AVS_UNIT_ASSERT_FAILED(anjay_ret_string(out, "1234567890"));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out));

    VERIFY_BYTES("0987654321");
    AVS_UNIT_ASSERT_EQUAL(COAP_FORMAT, ANJAY_COAP_FORMAT_PLAINTEXT);
}

AVS_UNIT_TEST(dynamic_out, i32) {
    TEST_ENV(512);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_set_id(out, ANJAY_ID_RID, 42));
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_i32(out, 514));
    AVS_UNIT_ASSERT_FAILED(anjay_ret_i32(out, 69));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out));

    VERIFY_BYTES("514");
    AVS_UNIT_ASSERT_EQUAL(COAP_FORMAT, ANJAY_COAP_FORMAT_PLAINTEXT);
}

AVS_UNIT_TEST(dynamic_out, i64) {
    TEST_ENV(512);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_set_id(out, ANJAY_ID_RID, 42));
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_i64(out, 424242424242LL));
    AVS_UNIT_ASSERT_FAILED(anjay_ret_i64(out, 69));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out));

    VERIFY_BYTES("424242424242");
    AVS_UNIT_ASSERT_EQUAL(COAP_FORMAT, ANJAY_COAP_FORMAT_PLAINTEXT);
}

AVS_UNIT_TEST(dynamic_out, f32) {
    TEST_ENV(512);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_set_id(out, ANJAY_ID_RID, 42));
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_float(out, 2.15625));
    AVS_UNIT_ASSERT_FAILED(anjay_ret_float(out, 3.14f));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out));

    VERIFY_BYTES("2.15625");
    AVS_UNIT_ASSERT_EQUAL(COAP_FORMAT, ANJAY_COAP_FORMAT_PLAINTEXT);
}

AVS_UNIT_TEST(dynamic_out, f64) {
    TEST_ENV(512);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_set_id(out, ANJAY_ID_RID, 42));
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_double(out, 4053.125267029));
    AVS_UNIT_ASSERT_FAILED(anjay_ret_double(out, 3.14));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out));

    VERIFY_BYTES("4053.125267029");
    AVS_UNIT_ASSERT_EQUAL(COAP_FORMAT, ANJAY_COAP_FORMAT_PLAINTEXT);
}

AVS_UNIT_TEST(dynamic_out, boolean) {
    TEST_ENV(512);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_set_id(out, ANJAY_ID_RID, 42));
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_bool(out, false));
    AVS_UNIT_ASSERT_FAILED(anjay_ret_bool(out, true));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out));

    VERIFY_BYTES("0");
    AVS_UNIT_ASSERT_EQUAL(COAP_FORMAT, ANJAY_COAP_FORMAT_PLAINTEXT);
}

AVS_UNIT_TEST(dynamic_out, objlnk) {
    TEST_ENV(512);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_set_id(out, ANJAY_ID_RID, 42));
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_objlnk(out, 514, 69));
    AVS_UNIT_ASSERT_FAILED(anjay_ret_objlnk(out, 66, 77));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out));

    VERIFY_BYTES("514:69");
    AVS_UNIT_ASSERT_EQUAL(COAP_FORMAT, ANJAY_COAP_FORMAT_PLAINTEXT);
}

AVS_UNIT_TEST(dynamic_out, array) {
    TEST_ENV(512);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_set_id(out, ANJAY_ID_RID, 42));
    anjay_output_ctx_t *array = anjay_ret_array_start(out);
    AVS_UNIT_ASSERT_NOT_NULL(array);
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_array_index(array, 5));
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_i32(array, 42));
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_array_index(array, 69));
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_string(array, "Hello, world!"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_array_finish(array));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out));

    VERIFY_BYTES(
            "\x88\x2A\x13" // array
            "\x41\x05\x2A" // first entry
            "\x48\x45\x0D" "Hello, world!" //second entry
            );
    AVS_UNIT_ASSERT_EQUAL(COAP_FORMAT, ANJAY_COAP_FORMAT_TLV);
}

AVS_UNIT_TEST(dynamic_out, object) {
    TEST_ENV(512);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_set_id(out, ANJAY_ID_IID, 42));
    anjay_output_ctx_t *obj = _anjay_output_object_start(out);
    AVS_UNIT_ASSERT_NOT_NULL(obj);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_set_id(obj, ANJAY_ID_RID, 69));
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_i32(obj, 514));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_object_finish(obj));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out));

    VERIFY_BYTES(
            "\x04\x2A" // object
            "\xC2\x45\x02\x02" // entry
            );
    AVS_UNIT_ASSERT_EQUAL(COAP_FORMAT, ANJAY_COAP_FORMAT_TLV);
}

AVS_UNIT_TEST(dynamic_out, method_not_implemented) {
    TEST_ENV(512);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_set_id(out, ANJAY_ID_RID, 42));
    AVS_UNIT_ASSERT_SUCCESS(anjay_ret_i32(out, 514));
    AVS_UNIT_ASSERT_FAILED(anjay_ret_i32(out, 69));
    AVS_UNIT_ASSERT_EQUAL(outctx_errno, 0);
    AVS_UNIT_ASSERT_NULL(anjay_ret_array_start(out));
    AVS_UNIT_ASSERT_EQUAL(outctx_errno, ANJAY_OUTCTXERR_METHOD_NOT_IMPLEMENTED);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out));

    VERIFY_BYTES("514");
    AVS_UNIT_ASSERT_EQUAL(COAP_FORMAT, ANJAY_COAP_FORMAT_PLAINTEXT);
}

AVS_UNIT_TEST(dynamic_out, format_mismatch) {
    TEST_ENV_WITH_FORMAT(512, ANJAY_COAP_FORMAT_OPAQUE);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_set_id(out, ANJAY_ID_RID, 42));
    AVS_UNIT_ASSERT_EQUAL(outctx_errno, 0);
    AVS_UNIT_ASSERT_FAILED(anjay_ret_string(out, "data"));
    AVS_UNIT_ASSERT_EQUAL(outctx_errno, ANJAY_OUTCTXERR_FORMAT_MISMATCH);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out));

    AVS_UNIT_ASSERT_EQUAL(COAP_FORMAT, ANJAY_COAP_FORMAT_OPAQUE);
}

#undef VERIFY_BYTES
#undef TEST_ENV

/////////////////////////////////////////////////////////////////////// DECODING

#define TEST_ENV_COMMON(Data) \
    avs_net_abstract_socket_t *mocksock; \
    avs_unit_mocksock_create(&mocksock); \
    anjay_coap_socket_t *coapsock; \
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_socket_create(&coapsock, mocksock)); \
    avs_unit_mocksock_expect_connect(mocksock, "", ""); \
    AVS_UNIT_ASSERT_SUCCESS(avs_net_socket_connect(mocksock, "", "")); \
    avs_stream_abstract_t *coap = NULL; \
    _anjay_mock_coap_stream_create(&coap, coapsock, 256, 256); \
    avs_unit_mocksock_input(mocksock, Data, sizeof(Data) - 1)

#define TEST_ENV(Data) \
    TEST_ENV_COMMON(Data); \
    anjay_input_ctx_t *ctx; \
    AVS_UNIT_ASSERT_SUCCESS(_anjay_input_dynamic_create(&ctx, &coap, true)); \
    AVS_UNIT_ASSERT_NOT_NULL(ctx)

#define TEST_TEARDOWN _anjay_input_ctx_destroy(&ctx)

#define COAP_HEADER(ContentFormatFirstOpt) \
        "\x50\x01\x00\x00" ContentFormatFirstOpt "\xFF"

#define LITERAL_COAP_FORMAT_FIRSTOPT_PLAINTEXT "\xC0"
#define LITERAL_COAP_FORMAT_FIRSTOPT_TLV       "\xC2\x2d\x16"
#define LITERAL_COAP_FORMAT_FIRSTOPT_JSON      "\xC2\x2d\x17"
#define LITERAL_COAP_FORMAT_FIRSTOPT_OPAQUE    "\xC1\x2A"
#define LITERAL_COAP_FORMAT_FIRSTOPT_UNKNOWN   "\xC2\x69\x69"

AVS_UNIT_TEST(dynamic_in, plain) {
    TEST_ENV(COAP_HEADER(LITERAL_COAP_FORMAT_FIRSTOPT_PLAINTEXT) "NDI=");

    size_t bytes_read;
    bool message_finished;
    char buf[16];
    int32_t value;
    anjay_id_type_t type;
    uint16_t id;
    memset(buf, 0, sizeof(buf));
    AVS_UNIT_ASSERT_FAILED(_anjay_input_get_id(ctx, &type, &id));
    AVS_UNIT_ASSERT_SUCCESS(anjay_get_bytes(ctx, &bytes_read, &message_finished,
                                            buf, sizeof(buf)));
    // It fails, because text context is in byte mode.
    AVS_UNIT_ASSERT_FAILED(anjay_get_i32(ctx, &value));
    AVS_UNIT_ASSERT_EQUAL_STRING(buf, "42");

    TEST_TEARDOWN;
}

AVS_UNIT_TEST(dynamic_in, no_content_format) {
    TEST_ENV_COMMON("\x50\x01\x00\x00\xFF" "514");
    anjay_input_ctx_t *ctx;
    AVS_UNIT_ASSERT_EQUAL(_anjay_input_dynamic_create(&ctx, &coap, true),
                          ANJAY_ERR_BAD_REQUEST);
    avs_stream_cleanup(&coap);
}

AVS_UNIT_TEST(dynamic_in, tlv) {
    TEST_ENV(COAP_HEADER(LITERAL_COAP_FORMAT_FIRSTOPT_TLV) "\xC1\x2A\x45");

    int32_t value;
    anjay_id_type_t type;
    uint16_t id;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_input_get_id(ctx, &type, &id));
    AVS_UNIT_ASSERT_EQUAL(type, ANJAY_ID_RID);
    AVS_UNIT_ASSERT_EQUAL(id, 42);
    AVS_UNIT_ASSERT_SUCCESS(anjay_get_i32(ctx, &value));
    AVS_UNIT_ASSERT_EQUAL(value, 69);

    TEST_TEARDOWN;
}

AVS_UNIT_TEST(dynamic_in, opaque) {
#define HELLO_WORLD "Hello, world!"
    TEST_ENV(COAP_HEADER(LITERAL_COAP_FORMAT_FIRSTOPT_OPAQUE) HELLO_WORLD);

    size_t bytes_read;
    bool message_finished;
    char buf[32];
    anjay_id_type_t type;
    uint16_t id;
    AVS_UNIT_ASSERT_FAILED(_anjay_input_get_id(ctx, &type, &id));
    AVS_UNIT_ASSERT_FAILED(anjay_get_string(ctx, buf, sizeof(buf)));
    AVS_UNIT_ASSERT_SUCCESS(anjay_get_bytes(ctx, &bytes_read, &message_finished,
                                            buf, sizeof(buf)));
    AVS_UNIT_ASSERT_TRUE(message_finished);
    AVS_UNIT_ASSERT_EQUAL(bytes_read, sizeof(HELLO_WORLD) - 1);
    AVS_UNIT_ASSERT_EQUAL_BYTES(buf, HELLO_WORLD);

    TEST_TEARDOWN;
#undef HELLO_WORLD
}

AVS_UNIT_TEST(dynamic_in, unrecognized) {
    TEST_ENV_COMMON(COAP_HEADER(LITERAL_COAP_FORMAT_FIRSTOPT_UNKNOWN) "514");
    anjay_input_ctx_t *ctx;
    AVS_UNIT_ASSERT_EQUAL(_anjay_input_dynamic_create(&ctx, &coap, true),
                          ANJAY_ERR_BAD_REQUEST);
    avs_stream_cleanup(&coap);
}

#undef COAP_HEADER
#undef TEST_TEARDOWN
#undef TEST_ENV
