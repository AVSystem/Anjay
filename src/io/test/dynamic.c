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

#include <anjay_config.h>

#include <avsystem/commons/stream.h>
#include <avsystem/commons/stream/stream_membuf.h>
#include <avsystem/commons/stream_v_table.h>

#include <avsystem/coap/code.h>

#define AVS_UNIT_ENABLE_SHORT_ASSERTS
#include <avsystem/commons/unit/mocksock.h>
#include <avsystem/commons/unit/test.h>

#include <anjay/core.h>

#include <anjay_test/dm.h>

#include "../../anjay_core.h"
#include "../../coap/test/utils.h"

/////////////////////////////////////////////////////////////////////// ENCODING

#define TEST_ENV(Size, Format, Uri)                                           \
    char buf[Size];                                                           \
    avs_stream_outbuf_t outbuf = AVS_STREAM_OUTBUF_STATIC_INITIALIZER;        \
    avs_stream_outbuf_set_buffer(&outbuf, buf, sizeof(buf));                  \
    anjay_output_ctx_t *out = NULL;                                           \
    ASSERT_OK(_anjay_output_dynamic_construct(&out, (avs_stream_t *) &outbuf, \
                                              (Uri), (Format),                \
                                              ANJAY_ACTION_READ));

#define PATH_HIERARCHICAL true
#define PATH_SIMPLE false

#define VERIFY_BYTES(Data)                                              \
    do {                                                                \
        ASSERT_EQ(avs_stream_outbuf_offset(&outbuf), sizeof(Data) - 1); \
        ASSERT_EQ_BYTES(buf, Data);                                     \
    } while (0)

AVS_UNIT_TEST(dynamic_out, bytes) {
    TEST_ENV(512, AVS_COAP_FORMAT_PLAINTEXT, &MAKE_RESOURCE_PATH(0, 0, 0));

    ASSERT_OK(_anjay_output_set_path(out, &MAKE_RESOURCE_PATH(0, 0, 42)));
    ASSERT_OK(anjay_ret_bytes(out, "1234567890", 10));
    ASSERT_FAIL(anjay_ret_bytes(out, "0987654321", 10));
    ASSERT_FAIL(_anjay_output_ctx_destroy(&out));

    VERIFY_BYTES("MTIzNDU2Nzg5MA==");
}

AVS_UNIT_TEST(dynamic_out, string) {
    TEST_ENV(512, AVS_COAP_FORMAT_PLAINTEXT, &MAKE_RESOURCE_PATH(0, 0, 0));

    ASSERT_OK(_anjay_output_set_path(out, &MAKE_RESOURCE_PATH(0, 0, 42)));
    ASSERT_OK(anjay_ret_string(out, "0987654321"));
    ASSERT_FAIL(anjay_ret_string(out, "1234567890"));
    ASSERT_FAIL(_anjay_output_ctx_destroy(&out));

    VERIFY_BYTES("0987654321");
}

AVS_UNIT_TEST(dynamic_out, i32) {
    TEST_ENV(512, AVS_COAP_FORMAT_PLAINTEXT, &MAKE_RESOURCE_PATH(0, 0, 0));

    ASSERT_OK(_anjay_output_set_path(out, &MAKE_RESOURCE_PATH(0, 0, 42)));
    ASSERT_OK(anjay_ret_i32(out, 514));
    ASSERT_FAIL(anjay_ret_i32(out, 69));
    ASSERT_FAIL(_anjay_output_ctx_destroy(&out));

    VERIFY_BYTES("514");
}

AVS_UNIT_TEST(dynamic_out, i64) {
    TEST_ENV(512, AVS_COAP_FORMAT_PLAINTEXT, &MAKE_RESOURCE_PATH(0, 0, 0));

    ASSERT_OK(_anjay_output_set_path(out, &MAKE_RESOURCE_PATH(0, 0, 42)));
    ASSERT_OK(anjay_ret_i64(out, 424242424242LL));
    ASSERT_FAIL(anjay_ret_i64(out, 69));
    ASSERT_FAIL(_anjay_output_ctx_destroy(&out));

    VERIFY_BYTES("424242424242");
}

AVS_UNIT_TEST(dynamic_out, f32) {
    TEST_ENV(512, AVS_COAP_FORMAT_PLAINTEXT, &MAKE_RESOURCE_PATH(0, 0, 0));

    ASSERT_OK(_anjay_output_set_path(out, &MAKE_RESOURCE_PATH(0, 0, 42)));
    ASSERT_OK(anjay_ret_float(out, 2.15625));
    ASSERT_FAIL(anjay_ret_float(out, 3.14f));
    ASSERT_FAIL(_anjay_output_ctx_destroy(&out));

    VERIFY_BYTES("2.15625");
}

AVS_UNIT_TEST(dynamic_out, f64) {
    TEST_ENV(512, AVS_COAP_FORMAT_PLAINTEXT, &MAKE_RESOURCE_PATH(0, 0, 0));

    ASSERT_OK(_anjay_output_set_path(out, &MAKE_RESOURCE_PATH(0, 0, 42)));
    ASSERT_OK(anjay_ret_double(out, 4053.125267029));
    ASSERT_FAIL(anjay_ret_double(out, 3.14));
    ASSERT_FAIL(_anjay_output_ctx_destroy(&out));

    VERIFY_BYTES("4053.125267029");
}

AVS_UNIT_TEST(dynamic_out, boolean) {
    TEST_ENV(512, AVS_COAP_FORMAT_PLAINTEXT, &MAKE_RESOURCE_PATH(0, 0, 0));

    ASSERT_OK(_anjay_output_set_path(out, &MAKE_RESOURCE_PATH(0, 0, 42)));
    ASSERT_OK(anjay_ret_bool(out, false));
    ASSERT_FAIL(anjay_ret_bool(out, true));
    ASSERT_FAIL(_anjay_output_ctx_destroy(&out));

    VERIFY_BYTES("0");
}

AVS_UNIT_TEST(dynamic_out, objlnk) {
    TEST_ENV(512, AVS_COAP_FORMAT_PLAINTEXT, &MAKE_RESOURCE_PATH(0, 0, 0));

    ASSERT_OK(_anjay_output_set_path(out, &MAKE_RESOURCE_PATH(0, 0, 42)));
    ASSERT_OK(anjay_ret_objlnk(out, 514, 69));
    ASSERT_FAIL(anjay_ret_objlnk(out, 66, 77));
    ASSERT_FAIL(_anjay_output_ctx_destroy(&out));

    VERIFY_BYTES("514:69");
}

AVS_UNIT_TEST(dynamic_out, array_from_instance) {
    TEST_ENV(512, AVS_COAP_FORMAT_OMA_LWM2M_TLV, &MAKE_INSTANCE_PATH(0, 0));

    ASSERT_OK(_anjay_output_set_path(out, &MAKE_RESOURCE_INSTANCE_PATH(0, 0, 42,
                                                                       5)));
    ASSERT_OK(anjay_ret_i32(out, 42));
    ASSERT_OK(_anjay_output_set_path(out, &MAKE_RESOURCE_INSTANCE_PATH(0, 0, 42,
                                                                       69)));
    ASSERT_OK(anjay_ret_string(out, "Hello, world!"));
    ASSERT_OK(_anjay_output_ctx_destroy(&out));

    VERIFY_BYTES("\x88\x2A\x13" // array
                 "\x41\x05\x2A" // first entry
                 "\x48\x45\x0D"
                 "Hello, world!" // second entry
    );
}

AVS_UNIT_TEST(dynamic_out, array_from_resource) {
    TEST_ENV(512, AVS_COAP_FORMAT_OMA_LWM2M_TLV, &MAKE_RESOURCE_PATH(0, 0, 42));

    ASSERT_OK(_anjay_output_set_path(out, &MAKE_RESOURCE_INSTANCE_PATH(0, 0, 42,
                                                                       5)));
    ASSERT_OK(anjay_ret_i32(out, 42));
    ASSERT_OK(_anjay_output_set_path(out, &MAKE_RESOURCE_INSTANCE_PATH(0, 0, 42,
                                                                       69)));
    ASSERT_OK(anjay_ret_string(out, "Hello, world!"));
    ASSERT_OK(_anjay_output_ctx_destroy(&out));

    VERIFY_BYTES("\x88\x2A\x13" // array
                 "\x41\x05\x2A" // first entry
                 "\x48\x45\x0D"
                 "Hello, world!" // second entry
    );
}

AVS_UNIT_TEST(dynamic_out, object) {
    TEST_ENV(512, AVS_COAP_FORMAT_OMA_LWM2M_TLV, &MAKE_OBJECT_PATH(0));

    ASSERT_OK(_anjay_output_set_path(out, &MAKE_RESOURCE_PATH(0, 42, 69)));
    ASSERT_OK(anjay_ret_i32(out, 514));
    ASSERT_OK(_anjay_output_ctx_destroy(&out));

    VERIFY_BYTES("\x04\x2A"         // object
                 "\xC2\x45\x02\x02" // entry
    );
}

AVS_UNIT_TEST(dynamic_out, method_not_implemented) {
    TEST_ENV(512, AVS_COAP_FORMAT_PLAINTEXT, &MAKE_RESOURCE_PATH(0, 0, 42));

    ASSERT_OK(_anjay_output_set_path(out, &MAKE_RESOURCE_PATH(0, 0, 42)));
    ASSERT_OK(anjay_ret_i32(out, 514));
    ASSERT_EQ(anjay_ret_i32(out, 69), -1);
    ASSERT_EQ(_anjay_output_start_aggregate(out),
              ANJAY_OUTCTXERR_METHOD_NOT_IMPLEMENTED);
    ASSERT_EQ(_anjay_output_ctx_destroy(&out), -1);

    VERIFY_BYTES("514");
}

AVS_UNIT_TEST(dynamic_out, format_mismatch) {
    TEST_ENV(512, AVS_COAP_FORMAT_OCTET_STREAM, &MAKE_RESOURCE_PATH(0, 0, 0));

    ASSERT_OK(_anjay_output_set_path(out, &MAKE_RESOURCE_PATH(0, 0, 42)));
    ASSERT_FAIL(anjay_ret_string(out, "data"));
    ASSERT_EQ(_anjay_output_ctx_destroy(&out),
              ANJAY_OUTCTXERR_METHOD_NOT_IMPLEMENTED);
}

#undef VERIFY_BYTES
#undef TEST_ENV

/////////////////////////////////////////////////////////////////////// DECODING

typedef struct {
    anjay_request_t request;
    anjay_input_ctx_t *input;
} dynamic_test_env_t;

typedef struct {
    const void *payload;
    size_t size;
} payload_view_t;

#define PAYLOAD_BYTES(Payload)  \
    (payload_view_t) {          \
        .payload = (Payload),   \
        .size = sizeof(Payload) \
    }

#define PAYLOAD_STRING(Payload)     \
    (payload_view_t) {              \
        .payload = (Payload),       \
        .size = sizeof(Payload) - 1 \
    }

typedef struct {
    uint16_t content_format;
    payload_view_t payload_view;
    anjay_request_action_t action;
    anjay_uri_path_t uri;
    int expected_error;
} dynamic_test_def_t;

static dynamic_test_env_t dynamic_test_env(const dynamic_test_def_t def) {
    dynamic_test_env_t env;
    memset(&env, 0, sizeof(env));

    avs_stream_t *payload_stream = avs_stream_membuf_create();
    ASSERT_NOT_NULL(payload_stream);
    ASSERT_OK(avs_stream_write(payload_stream, def.payload_view.payload,
                               def.payload_view.size));

    env.request.payload_stream = payload_stream;
    env.request.action = def.action;
    env.request.content_format = def.content_format;
    env.request.requested_format = AVS_COAP_FORMAT_NONE;
    env.request.request_code = AVS_COAP_CODE_POST;
    env.request.uri = def.uri;

    int result = _anjay_input_dynamic_construct(
            &env.input, env.request.payload_stream, &env.request);
    ASSERT_EQ(result, def.expected_error);
    if (!def.expected_error) {
        ASSERT_NOT_NULL(env.input);
    }

    return env;
}

static void dynamic_test_delete(dynamic_test_env_t *env) {
    avs_stream_cleanup(&env->request.payload_stream);
    _anjay_input_ctx_destroy(&env->input);
}

AVS_UNIT_TEST(dynamic_in, plain) {
    dynamic_test_env_t env __attribute__((cleanup(dynamic_test_delete))) =
            dynamic_test_env((dynamic_test_def_t) {
                .content_format = AVS_COAP_FORMAT_PLAINTEXT,
                .payload_view = PAYLOAD_STRING("NDI="),
                .action = ANJAY_ACTION_WRITE
            });

    size_t bytes_read;
    bool message_finished;
    char buf[16];
    int32_t value;
    memset(buf, 0, sizeof(buf));
    ASSERT_OK(_anjay_input_get_path(env.input, NULL, NULL));
    ASSERT_OK(anjay_get_bytes(env.input, &bytes_read, &message_finished, buf,
                              sizeof(buf)));
    // It fails, because text context is in byte mode.
    ASSERT_FAIL(anjay_get_i32(env.input, &value));
    ASSERT_EQ_STR(buf, "42");
}

AVS_UNIT_TEST(dynamic_in, no_content_format) {
    dynamic_test_env_t env __attribute__((cleanup(dynamic_test_delete))) =
            dynamic_test_env((dynamic_test_def_t) {
                .content_format = AVS_COAP_FORMAT_NONE,
                .payload_view = PAYLOAD_STRING("514"),
                .action = ANJAY_ACTION_WRITE
            });
}

AVS_UNIT_TEST(dynamic_in, tlv) {
    dynamic_test_env_t env __attribute__((cleanup(dynamic_test_delete))) =
            dynamic_test_env((dynamic_test_def_t) {
                .content_format = AVS_COAP_FORMAT_OMA_LWM2M_TLV,
                .payload_view = PAYLOAD_BYTES("\xC1\x2A\x45"),
                .action = ANJAY_ACTION_WRITE,
                .uri = MAKE_RESOURCE_PATH(1, 2, 42),
                // NOTE: gcc complains if the last structure field is not
                // explicitlyinitialized
                .expected_error = 0
            });

    int32_t value;
    anjay_uri_path_t path;
    ASSERT_OK(_anjay_input_get_path(env.input, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(&path, &MAKE_RESOURCE_PATH(1, 2, 42)));
    ASSERT_OK(anjay_get_i32(env.input, &value));
    ASSERT_EQ(value, 69);
}

AVS_UNIT_TEST(dynamic_in, opaque) {
#define HELLO_WORLD "Hello, world!"
    dynamic_test_env_t env __attribute__((cleanup(dynamic_test_delete))) =
            dynamic_test_env((dynamic_test_def_t) {
                .content_format = AVS_COAP_FORMAT_OCTET_STREAM,
                .payload_view = PAYLOAD_STRING(HELLO_WORLD),
                .action = ANJAY_ACTION_WRITE
            });

    size_t bytes_read;
    bool message_finished;
    char buf[32];
    ASSERT_OK(_anjay_input_get_path(env.input, NULL, NULL));
    ASSERT_FAIL(anjay_get_string(env.input, buf, sizeof(buf)));
    ASSERT_OK(anjay_get_bytes(env.input, &bytes_read, &message_finished, buf,
                              sizeof(buf)));
    ASSERT_TRUE(message_finished);
    ASSERT_EQ(bytes_read, sizeof(HELLO_WORLD) - 1);
    ASSERT_EQ_BYTES(buf, HELLO_WORLD);

#undef HELLO_WORLD
}

AVS_UNIT_TEST(dynamic_in, unrecognized) {
    dynamic_test_env_t env __attribute__((cleanup(dynamic_test_delete))) =
            dynamic_test_env((dynamic_test_def_t) {
                .content_format = 0x6969,
                .payload_view = PAYLOAD_STRING("514"),
                .action = ANJAY_ACTION_WRITE,
                .uri = MAKE_RESOURCE_PATH(1, 2, 42),
                .expected_error = ANJAY_ERR_UNSUPPORTED_CONTENT_FORMAT
            });
}
