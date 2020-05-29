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

#include <avs_coap_init.h>

#if defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_UDP) \
        && defined(WITH_AVS_COAP_STREAMING_API)

#    include <avsystem/coap/coap.h>

#    define MODULE_NAME test
#    include <avs_coap_x_log_config.h>

#    include "./utils.h"

AVS_UNIT_TEST(udp_streaming_client, streaming_request) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

#    define PAYLOAD_CONTENT DATA_1KB "?"

    const test_msg_t *request =
            COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)), NO_PAYLOAD);
    const test_msg_t *response =
            COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)),
                     PAYLOAD(PAYLOAD_CONTENT));

    expect_send(&env, request);
    expect_recv(&env, response);
    expect_timeout(&env);

    avs_stream_t *stream = NULL;
    avs_coap_response_header_t response_header;

    ASSERT_OK(avs_coap_streaming_send_request(env.coap_ctx,
                                              &request->request_header, NULL,
                                              NULL, &response_header, &stream));
    avs_coap_options_cleanup(&response_header.options);

    char buf[sizeof(PAYLOAD_CONTENT)];
    size_t bytes_read;
    bool finished;
    ASSERT_OK(
            avs_stream_read(stream, &bytes_read, &finished, buf, sizeof(buf)));

    ASSERT_EQ_BYTES(PAYLOAD_CONTENT, buf);
    ASSERT_EQ(bytes_read, sizeof(PAYLOAD_CONTENT) - 1);
    ASSERT_TRUE(finished);

#    undef PAYLOAD_CONTENT
}

AVS_UNIT_TEST(udp_streaming_client, reset_in_response) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

    const test_msg_t *expected_request =
            COAP_MSG(CON, POST, ID(0), TOKEN(nth_token(0)), NO_PAYLOAD);
    const test_msg_t *expected_response = COAP_MSG(RST, EMPTY, ID(0));

    expect_send(&env, expected_request);
    expect_recv(&env, expected_response);
    expect_timeout(&env);

    avs_coap_response_header_t response;
    ASSERT_FAIL(avs_coap_streaming_send_request(
            env.coap_ctx, &expected_request->request_header, NULL, NULL,
            &response, NULL));
    avs_coap_options_cleanup(&response.options);
}

#    ifdef WITH_AVS_COAP_BLOCK

AVS_UNIT_TEST(udp_streaming_client, streaming_request_block_response) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

#        define PAYLOAD_CONTENT DATA_1KB "?"

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)), NO_PAYLOAD),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)), BLOCK2_REQ(1, 1024))
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)),
                 BLOCK2_RES(0, 1024, PAYLOAD_CONTENT)),
        COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(1)),
                 BLOCK2_RES(1, 1024, PAYLOAD_CONTENT))
    };

    expect_send(&env, requests[0]);
    expect_recv(&env, responses[0]);
    expect_send(&env, requests[1]);
    expect_recv(&env, responses[1]);
    expect_timeout(&env);

    avs_stream_t *stream = NULL;
    avs_coap_response_header_t response;

    ASSERT_OK(avs_coap_streaming_send_request(env.coap_ctx,
                                              &requests[0]->request_header,
                                              NULL, NULL, &response, &stream));
    avs_coap_options_cleanup(&response.options);

    char buf[sizeof(PAYLOAD_CONTENT)];
    size_t bytes_read_total = 0;
    size_t bytes_read;
    bool finished = false;
    while (!finished) {
        ASSERT_OK(avs_stream_read(stream, &bytes_read, &finished,
                                  buf + bytes_read_total,
                                  sizeof(buf) - bytes_read_total));
        bytes_read_total += bytes_read;
    }

    ASSERT_EQ_BYTES(PAYLOAD_CONTENT, buf);
    ASSERT_EQ(bytes_read_total, sizeof(PAYLOAD_CONTENT) - 1);

#        undef PAYLOAD_CONTENT
}

AVS_UNIT_TEST(udp_streaming_client, streaming_request_peek) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

#        define PAYLOAD_CONTENT (DATA_1KB "?")

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)), NO_PAYLOAD),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)), BLOCK2_REQ(1, 1024))
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)),
                 BLOCK2_RES(0, 1024, PAYLOAD_CONTENT)),
        COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(1)),
                 BLOCK2_RES(1, 1024, PAYLOAD_CONTENT))
    };

    expect_send(&env, requests[0]);
    expect_recv(&env, responses[0]);
    expect_send(&env, requests[1]);
    expect_recv(&env, responses[1]);
    expect_timeout(&env);

    avs_stream_t *stream = NULL;
    avs_coap_response_header_t response;

    ASSERT_OK(avs_coap_streaming_send_request(env.coap_ctx,
                                              &requests[0]->request_header,
                                              NULL, NULL, &response, &stream));
    avs_coap_options_cleanup(&response.options);

    char buf[sizeof(PAYLOAD_CONTENT) / 64];
    size_t bytes_read_total = 0;
    size_t bytes_read;
    bool finished = false;
    while (!finished) {
        char ch;
        ASSERT_OK(avs_stream_peek(stream, 0, &ch));
        ASSERT_OK(avs_stream_read(stream, &bytes_read, &finished, buf,
                                  sizeof(buf)));
        ASSERT_EQ(buf[0], ch);
        bytes_read_total += bytes_read;
    }

    ASSERT_EQ(bytes_read_total, sizeof(PAYLOAD_CONTENT) - 1);

#        undef PAYLOAD_CONTENT
}

AVS_UNIT_TEST(udp_streaming_client, streaming_request_block_error) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

#        define PAYLOAD_CONTENT DATA_1KB "?"

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)), NO_PAYLOAD),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)), BLOCK2_REQ(1, 1024))
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)),
                 BLOCK2_RES(0, 1024, PAYLOAD_CONTENT))
    };

    expect_send(&env, requests[0]);
    expect_recv(&env, responses[0]);
    expect_send(&env, requests[1]);
    expect_timeout(&env);

    avs_stream_t *stream = NULL;
    avs_coap_response_header_t response;

    ASSERT_OK(avs_coap_streaming_send_request(env.coap_ctx,
                                              &requests[0]->request_header,
                                              NULL, NULL, &response, &stream));
    avs_coap_options_cleanup(&response.options);

    char buf[sizeof(PAYLOAD_CONTENT)];
    size_t bytes_read;
    bool finished = false;
    ASSERT_OK(
            avs_stream_read(stream, &bytes_read, &finished, buf, sizeof(buf)));
    ASSERT_EQ(bytes_read, 1024);
    ASSERT_FALSE(finished);
    ASSERT_EQ_BYTES_SIZED(PAYLOAD_CONTENT, buf, bytes_read);

    avs_unit_mocksock_input_fail(env.mocksock, avs_errno(AVS_ECONNREFUSED));

    avs_error_t err = avs_stream_peek(stream, 0, &(char) { 0 });
    ASSERT_FAIL(err);
    ASSERT_FALSE(avs_is_eof(err));
    ASSERT_FAIL(
            avs_stream_read(stream, &bytes_read, &finished, buf, sizeof(buf)));

#        undef PAYLOAD_CONTENT
}

AVS_UNIT_TEST(udp_streaming_client, streaming_block_request) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

#        define PAYLOAD_CONTENT DATA_1KB "?"

    test_streaming_payload_t payload = {
        .data = PAYLOAD_CONTENT,
        .size = sizeof(PAYLOAD_CONTENT) - 1
    };

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_REQ(0, 1024, PAYLOAD_CONTENT)),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_REQ(1, 1024, PAYLOAD_CONTENT))
    };
    const test_msg_t *responses[] = { COAP_MSG(ACK, CONTINUE, ID(0),
                                               TOKEN(nth_token(0)),
                                               BLOCK1_RES(0, 1024, true)),
                                      COAP_MSG(ACK, CONTENT, ID(1),
                                               TOKEN(nth_token(1)),
                                               BLOCK1_RES(1, 1024, false)) };

    expect_send(&env, requests[0]);
    expect_recv(&env, responses[0]);
    expect_send(&env, requests[1]);
    expect_recv(&env, responses[1]);
    expect_timeout(&env);

    avs_stream_t *stream = NULL;
    avs_coap_response_header_t response;
    ASSERT_OK(avs_coap_streaming_send_request(
            env.coap_ctx, &requests[0]->request_header, test_streaming_writer,
            &payload, &response, &stream));
    avs_coap_options_cleanup(&response.options);

    char buf[1];
    size_t bytes_read;
    bool finished;
    ASSERT_OK(
            avs_stream_read(stream, &bytes_read, &finished, buf, sizeof(buf)));

    ASSERT_EQ(bytes_read, 0);
    ASSERT_TRUE(finished);

#        undef PAYLOAD_CONTENT
}

AVS_UNIT_TEST(udp_streaming_client, small_block_request) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

#        define PAYLOAD_CONTENT DATA_16B "?"

    test_streaming_payload_t payload = {
        .data = PAYLOAD_CONTENT,
        .size = sizeof(PAYLOAD_CONTENT) - 1
    };

    // request packets & MTU crafted specifically so that accounting for option
    // size makes avs_coap use lower block size than without them. This used to
    // cause an assertion failure in streaming_client API (T2533)
    avs_unit_mocksock_enable_inner_mtu_getopt(env.mocksock, 75);

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)),
                 PATH("string that requires a lot of space"),
                 BLOCK1_REQ(0, 16, PAYLOAD_CONTENT)),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)),
                 PATH("string that requires a lot of space"),
                 BLOCK1_REQ(1, 16, PAYLOAD_CONTENT))
    };
    const test_msg_t *responses[] = { COAP_MSG(ACK, CONTINUE, ID(0),
                                               TOKEN(nth_token(0)),
                                               BLOCK1_RES(0, 16, true)),
                                      COAP_MSG(ACK, CONTENT, ID(1),
                                               TOKEN(nth_token(1)),
                                               BLOCK1_RES(1, 16, false)) };

    expect_send(&env, requests[0]);
    expect_recv(&env, responses[0]);
    expect_send(&env, requests[1]);
    expect_recv(&env, responses[1]);
    expect_timeout(&env);

    avs_stream_t *stream = NULL;
    avs_coap_response_header_t response;
    ASSERT_OK(avs_coap_streaming_send_request(
            env.coap_ctx, &requests[0]->request_header, test_streaming_writer,
            &payload, &response, &stream));
    avs_coap_options_cleanup(&response.options);

    char buf[1];
    size_t bytes_read;
    bool finished;
    ASSERT_OK(
            avs_stream_read(stream, &bytes_read, &finished, buf, sizeof(buf)));

    ASSERT_EQ(bytes_read, 0);
    ASSERT_TRUE(finished);

#        undef PAYLOAD_CONTENT
}

AVS_UNIT_TEST(udp_streaming_client, write_equal_to_block_size) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

#        define PAYLOAD_CONTENT DATA_1KB "?"

    test_streaming_payload_t payload = {
        .data = PAYLOAD_CONTENT,
        .size = sizeof(PAYLOAD_CONTENT) - 1,
        // Force test_streaming_writer to call avs_stream_write with data
        // chunks of size exactly equal to block size used. This used to
        // confuse streaming_client API enough to incorrectly assume there's
        // only 1024 bytes of request data because of having not enough data in
        // streaming API buffer.
        .chunk_size = 1024
    };

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_REQ(0, 1024, PAYLOAD_CONTENT)),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_REQ(1, 1024, PAYLOAD_CONTENT))
    };
    const test_msg_t *responses[] = { COAP_MSG(ACK, CONTINUE, ID(0),
                                               TOKEN(nth_token(0)),
                                               BLOCK1_RES(0, 1024, true)),
                                      COAP_MSG(ACK, CONTENT, ID(1),
                                               TOKEN(nth_token(1)),
                                               BLOCK1_RES(1, 1024, false)) };

    expect_send(&env, requests[0]);
    expect_recv(&env, responses[0]);
    expect_send(&env, requests[1]);
    expect_recv(&env, responses[1]);
    expect_timeout(&env);

    avs_coap_request_header_t req_without_block1 = requests[0]->request_header;
    ASSERT_OK(_avs_coap_options_copy_as_dynamic(
            &req_without_block1.options, &requests[0]->request_header.options));
    avs_coap_options_remove_by_number(&req_without_block1.options,
                                      AVS_COAP_OPTION_BLOCK1);

    avs_stream_t *stream = NULL;
    avs_coap_response_header_t response;
    ASSERT_OK(avs_coap_streaming_send_request(env.coap_ctx, &req_without_block1,
                                              test_streaming_writer, &payload,
                                              &response, &stream));
    avs_coap_options_cleanup(&response.options);

    char buf[1];
    size_t bytes_read;
    bool finished;
    ASSERT_OK(
            avs_stream_read(stream, &bytes_read, &finished, buf, sizeof(buf)));

    ASSERT_EQ(bytes_read, 0);
    ASSERT_TRUE(finished);

    avs_coap_options_cleanup(&req_without_block1.options);

#        undef PAYLOAD_CONTENT
}

#    endif // WITH_AVS_COAP_BLOCK

#endif // defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_UDP) &&
       // defined(WITH_AVS_COAP_STREAMING_API)
