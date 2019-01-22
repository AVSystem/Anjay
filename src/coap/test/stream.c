/*
 * Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
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

#include <avsystem/commons/coap/msg_opt.h>
#include <avsystem/commons/stream.h>
#include <avsystem/commons/stream_v_table.h>
#include <avsystem/commons/unit/mocksock.h>
#include <avsystem/commons/unit/test.h>

#include <anjay_test/coap/socket.h>
#include <anjay_test/coap/stream.h>
#include <anjay_test/mock_clock.h>
#include <anjay_test/utils.h>

#include "servers.h"

#include "../coap_stream.h"
#include "../content_format.h"
#include "utils.h"

#define TEST_PORT_UDP_ECHO 4322
#define TEST_PORT_UDP_ACK 4323
#define TEST_PORT_UDP_RESET 4324
#define TEST_PORT_UDP_GARBAGE_ACK 4325
#define TEST_PORT_UDP_GARBAGE 4326
#define TEST_PORT_UDP_MISMATCHED 4327
#define TEST_PORT_UDP_MISMATCHED_RESET 4328
#define TEST_PORT_UDP_LONG_SEPARATE 4329

AVS_UNIT_TEST(coap_stream, udp_read_write) {
    avs_net_abstract_socket_t *socket =
            _anjay_test_setup_udp_ack_echo_socket(TEST_PORT_UDP_ACK);
    avs_stream_abstract_t *stream = NULL;

    anjay_msg_details_t details = {
        .msg_type = AVS_COAP_MSG_CONFIRMABLE,
        .msg_code = AVS_COAP_CODE_NOT_FOUND,
        .format = ANJAY_COAP_FORMAT_JSON,
        .uri_path = ANJAY_MAKE_STRING_LIST("1", "2", "3"),
        .uri_query = ANJAY_MAKE_STRING_LIST("foo=bar", "baz=qux")
    };
    SCOPED_MOCK_COAP_STREAM(ctx) =
            _anjay_mock_coap_stream_create(&stream, socket, 4096, 4096);

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_stream_setup_request(stream, &details, NULL));

    const char DATA[] = "look at my stream, my stream is amazing";
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_write(stream, DATA, sizeof(DATA)));
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_finish_message(stream));

    const avs_coap_msg_t *msg;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_stream_get_incoming_msg(stream, &msg));

    char buffer[sizeof(DATA) + 16] = "";
    size_t bytes_read;
    char message_finished;
    uint16_t format;
    AVS_UNIT_ASSERT_SUCCESS(avs_coap_msg_get_option_u16(
            msg, AVS_COAP_OPT_CONTENT_FORMAT, &format));
    AVS_UNIT_ASSERT_EQUAL(format, details.format);

    avs_coap_opt_iterator_t optit = AVS_COAP_OPT_ITERATOR_EMPTY;
    AVS_UNIT_ASSERT_SUCCESS(avs_coap_msg_get_option_string_it(
            msg, AVS_COAP_OPT_URI_PATH, &optit, &bytes_read, buffer,
            sizeof(buffer)));
    AVS_UNIT_ASSERT_EQUAL(
            bytes_read,
            strlen((*AVS_LIST_NTH_PTR(&details.uri_path, 0))->c_str) + 1);
    AVS_UNIT_ASSERT_EQUAL_STRING(
            buffer, (*AVS_LIST_NTH_PTR(&details.uri_path, 0))->c_str);

    AVS_UNIT_ASSERT_SUCCESS(avs_coap_msg_get_option_string_it(
            msg, AVS_COAP_OPT_URI_PATH, &optit, &bytes_read, buffer,
            sizeof(buffer)));
    AVS_UNIT_ASSERT_EQUAL(
            bytes_read,
            strlen((*AVS_LIST_NTH_PTR(&details.uri_path, 1))->c_str) + 1);
    AVS_UNIT_ASSERT_EQUAL_STRING(
            buffer, (*AVS_LIST_NTH_PTR(&details.uri_path, 1))->c_str);

    AVS_UNIT_ASSERT_SUCCESS(avs_coap_msg_get_option_string_it(
            msg, AVS_COAP_OPT_URI_PATH, &optit, &bytes_read, buffer,
            sizeof(buffer)));
    AVS_UNIT_ASSERT_EQUAL(
            bytes_read,
            strlen((*AVS_LIST_NTH_PTR(&details.uri_path, 2))->c_str) + 1);
    AVS_UNIT_ASSERT_EQUAL_STRING(
            buffer, (*AVS_LIST_NTH_PTR(&details.uri_path, 2))->c_str);

    AVS_UNIT_ASSERT_EQUAL(avs_coap_msg_get_option_string_it(
                                  msg, AVS_COAP_OPT_URI_PATH, &optit,
                                  &bytes_read, buffer, sizeof(buffer)),
                          AVS_COAP_OPTION_MISSING);

    memset(&optit, 0, sizeof(optit));
    AVS_UNIT_ASSERT_SUCCESS(avs_coap_msg_get_option_string_it(
            msg, AVS_COAP_OPT_URI_QUERY, &optit, &bytes_read, buffer,
            sizeof(buffer)));
    AVS_UNIT_ASSERT_EQUAL(
            bytes_read,
            strlen((*AVS_LIST_NTH_PTR(&details.uri_query, 0))->c_str) + 1);
    AVS_UNIT_ASSERT_EQUAL_STRING(
            buffer, (*AVS_LIST_NTH_PTR(&details.uri_query, 0))->c_str);

    AVS_UNIT_ASSERT_SUCCESS(avs_coap_msg_get_option_string_it(
            msg, AVS_COAP_OPT_URI_QUERY, &optit, &bytes_read, buffer,
            sizeof(buffer)));
    AVS_UNIT_ASSERT_EQUAL(
            bytes_read,
            strlen((*AVS_LIST_NTH_PTR(&details.uri_query, 1))->c_str) + 1);
    AVS_UNIT_ASSERT_EQUAL_STRING(
            buffer, (*AVS_LIST_NTH_PTR(&details.uri_query, 1))->c_str);

    AVS_UNIT_ASSERT_EQUAL(avs_coap_msg_get_option_string_it(
                                  msg, AVS_COAP_OPT_URI_QUERY, &optit,
                                  &bytes_read, buffer, sizeof(buffer)),
                          AVS_COAP_OPTION_MISSING);

    AVS_UNIT_ASSERT_SUCCESS(avs_stream_read(
            stream, &bytes_read, &message_finished, buffer, sizeof(buffer)));
    AVS_UNIT_ASSERT_EQUAL(bytes_read, sizeof(DATA));
    AVS_UNIT_ASSERT_TRUE(message_finished);
    AVS_UNIT_ASSERT_EQUAL_BYTES(buffer, DATA);

    AVS_UNIT_ASSERT_SUCCESS(avs_stream_cleanup(&stream));
    AVS_LIST_CLEAR(&details.uri_path);
    AVS_LIST_CLEAR(&details.uri_query);
}

AVS_UNIT_TEST(coap_stream, no_payload) {
    avs_net_abstract_socket_t *socket =
            _anjay_test_setup_udp_echo_socket(TEST_PORT_UDP_ECHO);
    avs_stream_abstract_t *stream = NULL;

    anjay_msg_details_t details = {
        .msg_code = AVS_COAP_CODE_GET,
        .msg_type = AVS_COAP_MSG_NON_CONFIRMABLE
    };

    SCOPED_MOCK_COAP_STREAM(ctx) =
            _anjay_mock_coap_stream_create(&stream, socket, 4096, 4096);

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_stream_setup_request(stream, &details, NULL));
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_finish_message(stream));

    // Non-Confirmable messages get no response, so read() should fail
    char message_finished;
    size_t bytes_read;
    AVS_UNIT_ASSERT_FAILED(
            avs_stream_read(stream, &bytes_read, &message_finished, NULL, 0));

    // After the reset, stream should interpret incoming message as a request
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_reset(stream));

    AVS_UNIT_ASSERT_SUCCESS(
            avs_stream_read(stream, &bytes_read, &message_finished, NULL, 0));
    AVS_UNIT_ASSERT_EQUAL(bytes_read, 0);
    AVS_UNIT_ASSERT_TRUE(message_finished);

    AVS_UNIT_ASSERT_SUCCESS(avs_stream_cleanup(&stream));
}

AVS_UNIT_TEST(coap_stream, msg_id) {
    avs_net_abstract_socket_t *mocksock = NULL;
    _anjay_mocksock_create(&mocksock, 1252, 1252);
    avs_unit_mocksock_expect_connect(mocksock, "", "");
    AVS_UNIT_ASSERT_SUCCESS(avs_net_socket_connect(mocksock, "", ""));

    avs_stream_abstract_t *stream = NULL;
    SCOPED_MOCK_COAP_STREAM(ctx) =
            _anjay_mock_coap_stream_create(&stream, mocksock, 4096, 4096);

    anjay_msg_details_t details = {
        .msg_code = AVS_COAP_CODE_CONTENT,
        .msg_type = AVS_COAP_MSG_NON_CONFIRMABLE,
        .format = AVS_COAP_FORMAT_NONE
    };

    {
        AVS_UNIT_ASSERT_SUCCESS(
                _anjay_coap_stream_setup_request(stream, &details, NULL));

        avs_coap_msg_identity_t id;
        AVS_UNIT_ASSERT_SUCCESS(
                _anjay_coap_stream_get_request_identity(stream, &id));
        AVS_UNIT_ASSERT_EQUAL(id.token.size, 0);
        AVS_UNIT_ASSERT_EQUAL(id.msg_id, 0x69ED);

        const avs_coap_msg_t *response =
                COAP_MSG(NON, CONTENT, ID(0x69ED), NO_PAYLOAD);
        avs_unit_mocksock_expect_output(mocksock, response->content,
                                        response->length);
        AVS_UNIT_ASSERT_SUCCESS(avs_stream_finish_message(stream));

        // last request identity should be available until the stream is reset
        AVS_UNIT_ASSERT_SUCCESS(
                _anjay_coap_stream_get_request_identity(stream, &id));
        AVS_UNIT_ASSERT_EQUAL(id.token.size, 0);
        AVS_UNIT_ASSERT_EQUAL(id.msg_id, 0x69ED);

        AVS_UNIT_ASSERT_SUCCESS(avs_stream_reset(stream));
        AVS_UNIT_ASSERT_FAILED(
                _anjay_coap_stream_get_request_identity(stream, &id));
    }
    {
        AVS_UNIT_ASSERT_SUCCESS(
                _anjay_coap_stream_setup_request(stream, &details, NULL));

        avs_coap_msg_identity_t id;
        AVS_UNIT_ASSERT_SUCCESS(
                _anjay_coap_stream_get_request_identity(stream, &id));
        AVS_UNIT_ASSERT_EQUAL(id.token.size, 0);
        AVS_UNIT_ASSERT_EQUAL(id.msg_id, 0x69EE);

        const avs_coap_msg_t *response =
                COAP_MSG(NON, CONTENT, ID(0x69EE), NO_PAYLOAD);
        avs_unit_mocksock_expect_output(mocksock, response->content,
                                        response->length);
        AVS_UNIT_ASSERT_SUCCESS(avs_stream_finish_message(stream));
    }
    {
#define TOKEN "AYY LMAO"
        static const uint8_t TOKEN_SIZE = sizeof(TOKEN) - 1;
        avs_coap_token_t token = {
            .size = TOKEN_SIZE
        };
        memcpy(token.bytes, TOKEN, TOKEN_SIZE);

        AVS_UNIT_ASSERT_SUCCESS(
                _anjay_coap_stream_setup_request(stream, &details, &token));

        avs_coap_msg_identity_t id;
        AVS_UNIT_ASSERT_SUCCESS(
                _anjay_coap_stream_get_request_identity(stream, &id));
        AVS_UNIT_ASSERT_EQUAL(id.token.size, TOKEN_SIZE);
        AVS_UNIT_ASSERT_EQUAL_BYTES(&id.token.bytes, TOKEN);
        AVS_UNIT_ASSERT_EQUAL(id.msg_id, 0x69EF);

        const avs_coap_msg_t *response =
                COAP_MSG(NON, CONTENT, ID_TOKEN(0x69EF, TOKEN), NO_PAYLOAD);
        avs_unit_mocksock_expect_output(mocksock, response->content,
                                        response->length);
        AVS_UNIT_ASSERT_SUCCESS(avs_stream_finish_message(stream));
#undef TOKEN
    }
    {
        AVS_UNIT_ASSERT_SUCCESS(
                _anjay_coap_stream_setup_request(stream, &details, NULL));

        avs_coap_msg_identity_t id;
        AVS_UNIT_ASSERT_SUCCESS(
                _anjay_coap_stream_get_request_identity(stream, &id));
        AVS_UNIT_ASSERT_EQUAL(id.token.size, 0);
        AVS_UNIT_ASSERT_EQUAL(id.msg_id, 0x69F0);

        const avs_coap_msg_t *response =
                COAP_MSG(NON, CONTENT, ID(0x69F0), NO_PAYLOAD);
        avs_unit_mocksock_expect_output(mocksock, response->content,
                                        response->length);
        AVS_UNIT_ASSERT_SUCCESS(avs_stream_finish_message(stream));
    }
    avs_unit_mocksock_assert_io_clean(mocksock);
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_cleanup(&stream));
}

AVS_UNIT_TEST(coap_stream, read_some) {
    avs_net_abstract_socket_t *socket =
            _anjay_test_setup_udp_ack_echo_socket(TEST_PORT_UDP_ACK);
    avs_stream_abstract_t *stream = NULL;

    anjay_msg_details_t details = {
        .msg_code = AVS_COAP_CODE_CONTENT,
        .msg_type = AVS_COAP_MSG_CONFIRMABLE
    };

    SCOPED_MOCK_COAP_STREAM(ctx) =
            _anjay_mock_coap_stream_create(&stream, socket, 4096, 4096);

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_stream_setup_request(stream, &details, NULL));

    const char DATA[] = "Bacon ipsum dolor amet";
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_write(stream, DATA, sizeof(DATA) - 1));
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_finish_message(stream));

    const avs_coap_msg_t *msg;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_stream_get_incoming_msg(stream, &msg));
    AVS_UNIT_ASSERT_EQUAL(details.msg_code, avs_coap_msg_get_code(msg));

    char message_finished;
    size_t bytes_read;
    char buffer[sizeof(DATA) + 1];
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_read(stream, &bytes_read,
                                            &message_finished, buffer, 11));
    AVS_UNIT_ASSERT_EQUAL(bytes_read, sizeof(DATA) / 2);
    AVS_UNIT_ASSERT_EQUAL_BYTES(buffer, "Bacon ipsum");
    AVS_UNIT_ASSERT_FALSE(message_finished);

    AVS_UNIT_ASSERT_SUCCESS(avs_stream_read(
            stream, &bytes_read, &message_finished, buffer, sizeof(buffer)));
    AVS_UNIT_ASSERT_EQUAL(bytes_read, sizeof(DATA) - 12);
    AVS_UNIT_ASSERT_EQUAL_BYTES(buffer, " dolor amet");
    AVS_UNIT_ASSERT_TRUE(message_finished);

    AVS_UNIT_ASSERT_SUCCESS(avs_stream_cleanup(&stream));
}

AVS_UNIT_TEST(coap_stream, confirmable) {
    avs_net_abstract_socket_t *socket =
            _anjay_test_setup_udp_ack_echo_socket(TEST_PORT_UDP_ACK);
    avs_stream_abstract_t *stream = NULL;

    anjay_msg_details_t details = {
        .msg_code = AVS_COAP_CODE_CONTENT,
        .msg_type = AVS_COAP_MSG_CONFIRMABLE
    };

    SCOPED_MOCK_COAP_STREAM(ctx) =
            _anjay_mock_coap_stream_create(&stream, socket, 4096, 4096);

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_stream_setup_request(stream, &details, NULL));

    const char DATA[] = "Bacon ipsum dolor amet";
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_write(stream, DATA, sizeof(DATA) - 1));
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_finish_message(stream));

    char message_finished;
    size_t bytes_read;
    char buffer[sizeof(DATA) + 1];
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_read(stream, &bytes_read,
                                            &message_finished, buffer, 11));
    AVS_UNIT_ASSERT_EQUAL(bytes_read, sizeof(DATA) / 2);
    AVS_UNIT_ASSERT_EQUAL_BYTES(buffer, "Bacon ipsum");
    AVS_UNIT_ASSERT_FALSE(message_finished);

    AVS_UNIT_ASSERT_SUCCESS(avs_stream_read(
            stream, &bytes_read, &message_finished, buffer, sizeof(buffer)));
    AVS_UNIT_ASSERT_EQUAL(bytes_read, sizeof(DATA) - 12);
    AVS_UNIT_ASSERT_EQUAL_BYTES(buffer, " dolor amet");
    AVS_UNIT_ASSERT_TRUE(message_finished);

    AVS_UNIT_ASSERT_SUCCESS(avs_stream_cleanup(&stream));
}

AVS_UNIT_TEST(coap_stream, reset_when_sending) {
    avs_net_abstract_socket_t *socket =
            _anjay_test_setup_udp_reset_socket(TEST_PORT_UDP_RESET);
    avs_stream_abstract_t *stream = NULL;

    anjay_msg_details_t details = {
        .msg_code = AVS_COAP_CODE_CONTENT,
        .msg_type = AVS_COAP_MSG_CONFIRMABLE
    };

    SCOPED_MOCK_COAP_STREAM(ctx) =
            _anjay_mock_coap_stream_create(&stream, socket, 4096, 4096);

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_stream_setup_request(stream, &details, NULL));
    AVS_UNIT_ASSERT_FAILED(avs_stream_finish_message(stream));

    AVS_UNIT_ASSERT_SUCCESS(avs_stream_cleanup(&stream));
}

AVS_UNIT_TEST(coap_stream, mismatched_reset) {
    avs_net_abstract_socket_t *socket =
            _anjay_test_setup_udp_mismatched_ack_then_reset_socket(
                    TEST_PORT_UDP_MISMATCHED_RESET);
    avs_stream_abstract_t *stream = NULL;

    anjay_msg_details_t details = {
        .msg_code = AVS_COAP_CODE_CONTENT,
        .msg_type = AVS_COAP_MSG_CONFIRMABLE
    };

    SCOPED_MOCK_COAP_STREAM(ctx) =
            _anjay_mock_coap_stream_create(&stream, socket, 4096, 4096);

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_stream_setup_request(stream, &details, NULL));
    AVS_UNIT_ASSERT_FAILED(avs_stream_finish_message(stream));

    AVS_UNIT_ASSERT_SUCCESS(avs_stream_cleanup(&stream));
}

AVS_UNIT_TEST(coap_stream, garbage_response_when_waiting_for_ack) {
    avs_net_abstract_socket_t *socket =
            _anjay_test_setup_udp_garbage_then_ack_socket(
                    TEST_PORT_UDP_GARBAGE_ACK);
    avs_stream_abstract_t *stream = NULL;

    anjay_msg_details_t details = {
        .msg_code = AVS_COAP_CODE_CONTENT,
        .msg_type = AVS_COAP_MSG_CONFIRMABLE
    };

    SCOPED_MOCK_COAP_STREAM(ctx) =
            _anjay_mock_coap_stream_create(&stream, socket, 4096, 4096);

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_stream_setup_request(stream, &details, NULL));
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_finish_message(stream));

    AVS_UNIT_ASSERT_SUCCESS(avs_stream_cleanup(&stream));
}

AVS_UNIT_TEST(coap_stream, ack_with_mismatched_id) {
    avs_net_abstract_socket_t *socket =
            _anjay_test_setup_udp_mismatched_reset_then_ack_socket(
                    TEST_PORT_UDP_MISMATCHED);
    avs_stream_abstract_t *stream = NULL;

    anjay_msg_details_t details = {
        .msg_code = AVS_COAP_CODE_CONTENT,
        .msg_type = AVS_COAP_MSG_CONFIRMABLE
    };

    SCOPED_MOCK_COAP_STREAM(ctx) =
            _anjay_mock_coap_stream_create(&stream, socket, 4096, 4096);

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_stream_setup_request(stream, &details, NULL));
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_finish_message(stream));

    AVS_UNIT_ASSERT_SUCCESS(avs_stream_cleanup(&stream));
}

AVS_UNIT_TEST(coap_stream, long_separate) {
    avs_net_abstract_socket_t *socket =
            _anjay_test_setup_udp_long_separate_socket(
                    TEST_PORT_UDP_LONG_SEPARATE);
    avs_stream_abstract_t *stream = NULL;

    anjay_msg_details_t details = {
        .msg_code = AVS_COAP_CODE_CONTENT,
        .msg_type = AVS_COAP_MSG_CONFIRMABLE
    };

    char out_data[] = "follow the white rabbit";

    SCOPED_MOCK_COAP_STREAM(ctx) =
            _anjay_mock_coap_stream_create(&stream, socket, 4096, 4096);

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_stream_setup_request(stream, &details, NULL));
    AVS_UNIT_ASSERT_SUCCESS(
            avs_stream_write(stream, out_data, sizeof(out_data) - 1));
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_finish_message(stream));

    size_t in_data_size;
    char message_finished;
    char in_data[256];
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_read(stream, &in_data_size,
                                            &message_finished, in_data,
                                            sizeof(in_data)));
    AVS_UNIT_ASSERT_EQUAL(in_data_size, sizeof(out_data) - 1);
    AVS_UNIT_ASSERT_TRUE(message_finished);
    AVS_UNIT_ASSERT_EQUAL_BYTES(in_data, out_data);

    AVS_UNIT_ASSERT_SUCCESS(avs_stream_cleanup(&stream));
}

AVS_UNIT_TEST(coap_stream, receive_garbage) {
    avs_net_abstract_socket_t *socket =
            _anjay_test_setup_udp_garbage_then_ack_socket(
                    TEST_PORT_UDP_GARBAGE);
    avs_stream_abstract_t *stream = NULL;

    anjay_msg_details_t details = {
        .msg_code = AVS_COAP_CODE_CONTENT,
        .msg_type = AVS_COAP_MSG_NON_CONFIRMABLE
    };

    SCOPED_MOCK_COAP_STREAM(ctx) =
            _anjay_mock_coap_stream_create(&stream, socket, 4096, 4096);

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_stream_setup_request(stream, &details, NULL));
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_finish_message(stream));

    // avs_stream_finish_message should return without waiting for a response
    // here, but a garbage packet should be received, so the read will not block
    char message_finished;
    size_t bytes_read;
    char buffer[256];
    AVS_UNIT_ASSERT_FAILED(avs_stream_read(stream, &bytes_read,
                                           &message_finished, buffer, 11));

    AVS_UNIT_ASSERT_SUCCESS(avs_stream_cleanup(&stream));
}

AVS_UNIT_TEST(coap_stream, add_observe_option) {
    avs_net_abstract_socket_t *mocksock = NULL;
    _anjay_mocksock_create(&mocksock, 1252, 1252);
    avs_unit_mocksock_expect_connect(mocksock, "", "");
    AVS_UNIT_ASSERT_SUCCESS(avs_net_socket_connect(mocksock, "", ""));

    avs_stream_abstract_t *stream = NULL;
    SCOPED_MOCK_COAP_STREAM(ctx) =
            _anjay_mock_coap_stream_create(&stream, mocksock, 4096, 4096);

    anjay_msg_details_t details = {
        .msg_code = AVS_COAP_CODE_CONTENT,
        .msg_type = AVS_COAP_MSG_NON_CONFIRMABLE,
        .format = AVS_COAP_FORMAT_NONE
    };

    {
        details.observe_serial = true;
        _anjay_mock_clock_start((avs_time_monotonic_t) { { 514, 777 << 15 } });
        AVS_UNIT_ASSERT_SUCCESS(
                _anjay_coap_stream_setup_request(stream, &details, NULL));
        const avs_coap_msg_t *response =
                COAP_MSG(NON, CONTENT, ID(0x69ED), OBSERVE(0x010309),
                         NO_PAYLOAD);
        avs_unit_mocksock_expect_output(mocksock, response->content,
                                        response->length);
        AVS_UNIT_ASSERT_SUCCESS(avs_stream_finish_message(stream));
        _anjay_mock_clock_finish();
    }
    {
        details.observe_serial = true;
        _anjay_mock_clock_start((avs_time_monotonic_t) { { 777, 514 << 15 } });
        AVS_UNIT_ASSERT_SUCCESS(
                _anjay_coap_stream_setup_request(stream, &details, NULL));
        const avs_coap_msg_t *response =
                COAP_MSG(NON, CONTENT, ID(0x69EE), OBSERVE(0x848202),
                         NO_PAYLOAD);
        avs_unit_mocksock_expect_output(mocksock, response->content,
                                        response->length);
        AVS_UNIT_ASSERT_SUCCESS(avs_stream_finish_message(stream));
        _anjay_mock_clock_finish();
    }
    avs_unit_mocksock_assert_io_clean(mocksock);
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_cleanup(&stream));
}

typedef struct test_data {
    avs_net_abstract_socket_t *mock_socket;
    avs_stream_abstract_t *stream;
    anjay_mock_coap_stream_ctx_t mock_stream;
} test_data_t;

static test_data_t setup_test(void) {
    test_data_t data = {
        .mock_socket = NULL,
        .stream = NULL
    };

    _anjay_mocksock_create(&data.mock_socket, 1252, 1252);
    avs_unit_mocksock_expect_connect(data.mock_socket, "", "");

    AVS_UNIT_ASSERT_SUCCESS(avs_net_socket_connect(data.mock_socket, "", ""));
    data.mock_stream =
            _anjay_mock_coap_stream_create(&data.stream, data.mock_socket, 4096,
                                           4096);

    return data;
}

static void teardown_test(test_data_t *data) {
    avs_unit_mocksock_assert_expects_met(data->mock_socket);
    avs_unit_mocksock_assert_io_clean(data->mock_socket);
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_cleanup(&data->stream));
    _anjay_mock_coap_stream_cleanup(&data->mock_stream);
    memset(data, 0, sizeof(*data));
}

static void mock_receive_request(test_data_t *test,
                                 const char *request,
                                 size_t request_size) {
    avs_unit_mocksock_input(test->mock_socket, (const uint8_t *) request,
                            request_size);

    const avs_coap_msg_t *msg;
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_stream_get_incoming_msg(test->stream, &msg));
}

AVS_UNIT_TEST(coap_stream, response_empty) {
    test_data_t test = setup_test();

    const avs_coap_msg_t *request = COAP_MSG(CON, PUT, ID(0x0001), NO_PAYLOAD);
    mock_receive_request(&test, (const char *) request->content,
                         request->length);

    const avs_coap_msg_t *response =
            COAP_MSG(ACK, CHANGED, ID(0x0001), NO_PAYLOAD);
    avs_unit_mocksock_expect_output(test.mock_socket, response->content,
                                    response->length);

    const anjay_msg_details_t details = {
        .msg_type = AVS_COAP_MSG_ACKNOWLEDGEMENT,
        .msg_code = AVS_COAP_CODE_CHANGED,
        .format = AVS_COAP_FORMAT_NONE
    };
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_stream_setup_response(test.stream, &details));
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_finish_message(test.stream));

    teardown_test(&test);
}

AVS_UNIT_TEST(coap_stream, response_token) {
    test_data_t test = setup_test();

#define TOKEN "TOKEN123"

    const avs_coap_msg_t *request =
            COAP_MSG(CON, PUT, ID_TOKEN(0x0001, TOKEN), NO_PAYLOAD);
    mock_receive_request(&test, (const char *) request->content,
                         request->length);

    const avs_coap_msg_t *response =
            COAP_MSG(ACK, CHANGED, ID_TOKEN(0x0001, TOKEN), NO_PAYLOAD);
    avs_unit_mocksock_expect_output(test.mock_socket, response->content,
                                    response->length);
#undef TOKEN

    const anjay_msg_details_t details = {
        .msg_type = AVS_COAP_MSG_ACKNOWLEDGEMENT,
        .msg_code = AVS_COAP_CODE_CHANGED,
        .format = AVS_COAP_FORMAT_NONE
    };
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_stream_setup_response(test.stream, &details));
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_finish_message(test.stream));

    teardown_test(&test);
}

AVS_UNIT_TEST(coap_stream, response_content) {
    test_data_t test = setup_test();

#define CONTENT   \
    "jeden cios " \
    "tak by zlamal sie nos"
    const avs_coap_msg_t *request = COAP_MSG(CON, PUT, ID(0x0001), NO_PAYLOAD);
    mock_receive_request(&test, (const char *) request->content,
                         request->length);

    const avs_coap_msg_t *response =
            COAP_MSG(ACK, CHANGED, ID(0x0001), PAYLOAD(CONTENT));
    avs_unit_mocksock_expect_output(test.mock_socket, response->content,
                                    response->length);

    const anjay_msg_details_t details = {
        .msg_type = AVS_COAP_MSG_ACKNOWLEDGEMENT,
        .msg_code = AVS_COAP_CODE_CHANGED,
        .format = AVS_COAP_FORMAT_NONE
    };
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_stream_setup_response(test.stream, &details));
    AVS_UNIT_ASSERT_SUCCESS(
            avs_stream_write((avs_stream_abstract_t *) test.stream, CONTENT,
                             sizeof(CONTENT) - 1));
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_finish_message(test.stream));
#undef CONTENT

    teardown_test(&test);
}

AVS_UNIT_TEST(coap_stream, response_options) {
    test_data_t test = setup_test();

    const avs_coap_msg_t *request = COAP_MSG(CON, PUT, ID(0x0001), NO_PAYLOAD);
    mock_receive_request(&test, (const char *) request->content,
                         request->length);

    const avs_coap_msg_t *response = COAP_MSG(
            ACK, CHANGED, ID(0x0001),
            LOCATION_PATH("slychac", "trzask", "bylo", "zalozyc", "kask"),
            PATH("w", "ryj", "z", "kopa"), QUERY("albo=lepiej", "w=jadra"));
    avs_unit_mocksock_expect_output(test.mock_socket, response->content,
                                    response->length);

    const anjay_msg_details_t details = {
        .msg_type = AVS_COAP_MSG_ACKNOWLEDGEMENT,
        .msg_code = AVS_COAP_CODE_CHANGED,
        .format = AVS_COAP_FORMAT_NONE,
        .uri_path = ANJAY_MAKE_STRING_LIST("w", "ryj", "z", "kopa"),
        .uri_query = ANJAY_MAKE_STRING_LIST("albo=lepiej", "w=jadra"),
        .location_path = ANJAY_MAKE_STRING_LIST("slychac", "trzask", "bylo",
                                                "zalozyc", "kask")
    };
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_stream_setup_response(test.stream, &details));
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_finish_message(test.stream));

    AVS_LIST_CLEAR((AVS_LIST(anjay_string_t) *) (intptr_t) &details.uri_path);
    AVS_LIST_CLEAR((AVS_LIST(anjay_string_t) *) (intptr_t) &details.uri_query);
    AVS_LIST_CLEAR(
            (AVS_LIST(anjay_string_t) *) (intptr_t) &details.location_path);
    teardown_test(&test);
}

AVS_UNIT_TEST(coap_stream, response_no_request) {
    test_data_t test = setup_test();

    const anjay_msg_details_t details = {
        .msg_type = AVS_COAP_MSG_ACKNOWLEDGEMENT,
        .msg_code = AVS_COAP_CODE_CHANGED,
        .format = AVS_COAP_FORMAT_NONE
    };
    AVS_UNIT_ASSERT_FAILED(
            _anjay_coap_stream_setup_response(test.stream, &details));

    teardown_test(&test);
}

AVS_UNIT_TEST(coap_stream, fuzz_1_invalid_block_size) {
    // According to [ietf-core-block-21], 2.2 "Structure of a Block Option":
    // > The value 7 for SZX (which would indicate a block size of 2048) is
    // > reserved, i.e. MUST NOT be sent and MUST lead to a 4.00 Bad Request
    // > response code upon reception in a request.

    // Cannot use BLOCK macro because there are assertions forbidding setting
    // wrong block size.
    static const char MESSAGE[] =
            "\x40"     // Confirmable, token size = 0
            "\x03"     // 0.03 Put
            "\x00\x01" // message ID
            "\xd1\x0e" // delta = 13 + 14, length = 1
            "\x07";    // seq_num = 0, has_more = 0, block_size = 2048

    const avs_coap_msg_t *bad_option_res =
            COAP_MSG(ACK, BAD_REQUEST, ID(0x0001), NO_PAYLOAD);

    avs_net_abstract_socket_t *mocksock = NULL;
    _anjay_mocksock_create(&mocksock, 1252, 1252);
    avs_unit_mocksock_expect_connect(mocksock, "", "");
    avs_unit_mocksock_input(mocksock, (const uint8_t *) MESSAGE,
                            sizeof(MESSAGE) - 1);
    avs_unit_mocksock_expect_output(mocksock, bad_option_res->content,
                                    bad_option_res->length);

    AVS_UNIT_ASSERT_SUCCESS(avs_net_socket_connect(mocksock, "", ""));

    avs_stream_abstract_t *stream = NULL;
    SCOPED_MOCK_COAP_STREAM(ctx) =
            _anjay_mock_coap_stream_create(&stream, mocksock, 4096, 4096);

    char message_finished;
    size_t bytes_read;
    char buffer[256];
    AVS_UNIT_ASSERT_FAILED(avs_stream_read(
            stream, &bytes_read, &message_finished, buffer, sizeof(buffer)));

    AVS_UNIT_ASSERT_SUCCESS(avs_stream_cleanup(&stream));
}
