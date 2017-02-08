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

#include <avsystem/commons/unit/mocksock.h>
#include <avsystem/commons/unit/test.h>
#include <avsystem/commons/stream.h>
#include <avsystem/commons/stream_v_table.h>

#include <anjay_test/mock_clock.h>
#include <anjay_test/coap/stream.h>

#include "servers.h"
#include "../stream.h"

#define TEST_PORT_UDP_ECHO 4322
#define TEST_PORT_UDP_ACK 4323
#define TEST_PORT_UDP_RESET 4324
#define TEST_PORT_UDP_GARBAGE_ACK 4325
#define TEST_PORT_UDP_GARBAGE 4326
#define TEST_PORT_UDP_MISMATCHED 4327
#define TEST_PORT_UDP_MISMATCHED_RESET 4328
#define TEST_PORT_UDP_LONG_SEPARATE 4329

AVS_UNIT_TEST(coap_stream, udp_read_write) {
    anjay_coap_socket_t *socket =
            _anjay_test_setup_udp_ack_echo_socket(TEST_PORT_UDP_ACK);
    avs_stream_abstract_t *stream = NULL;

    anjay_msg_details_t details = {
        .msg_type = ANJAY_COAP_MSG_CONFIRMABLE,
        .msg_code = ANJAY_COAP_CODE_NOT_FOUND,
        .format = ANJAY_COAP_FORMAT_JSON,
        .uri_path = _anjay_make_string_list("1", "2", "3", NULL),
        .uri_query = _anjay_make_string_list("foo=bar", "baz=qux", NULL)
    };

    _anjay_mock_coap_stream_create(&stream, socket, 4096, 4096);

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_stream_setup_request(stream, &details, NULL, 0));

    const char DATA[] = "look at my stream, my stream is amazing";
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_write(stream, DATA, sizeof(DATA)));
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_finish_message(stream));

    char buffer[sizeof(DATA) + 16] = "";
    size_t bytes_read;
    char message_finished;
    uint16_t format;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_stream_get_option_u16(
            stream, ANJAY_COAP_OPT_CONTENT_FORMAT, &format));
    AVS_UNIT_ASSERT_EQUAL(format, details.format);

    anjay_coap_opt_iterator_t optit = ANJAY_COAP_OPT_ITERATOR_EMPTY;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_stream_get_option_string_it(
            stream, ANJAY_COAP_OPT_URI_PATH, &optit, &bytes_read, buffer,
            sizeof(buffer)));
    AVS_UNIT_ASSERT_EQUAL(
            bytes_read,
            strlen((*AVS_LIST_NTH_PTR(&details.uri_path, 0))->c_str) + 1);
    AVS_UNIT_ASSERT_EQUAL_STRING(
            buffer, (*AVS_LIST_NTH_PTR(&details.uri_path, 0))->c_str);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_stream_get_option_string_it(
            stream, ANJAY_COAP_OPT_URI_PATH, &optit, &bytes_read, buffer,
            sizeof(buffer)));
    AVS_UNIT_ASSERT_EQUAL(
            bytes_read,
            strlen((*AVS_LIST_NTH_PTR(&details.uri_path, 1))->c_str) + 1);
    AVS_UNIT_ASSERT_EQUAL_STRING(
            buffer, (*AVS_LIST_NTH_PTR(&details.uri_path, 1))->c_str);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_stream_get_option_string_it(
            stream, ANJAY_COAP_OPT_URI_PATH, &optit, &bytes_read, buffer,
            sizeof(buffer)));
    AVS_UNIT_ASSERT_EQUAL(
            bytes_read,
            strlen((*AVS_LIST_NTH_PTR(&details.uri_path, 2))->c_str) + 1);
    AVS_UNIT_ASSERT_EQUAL_STRING(
            buffer, (*AVS_LIST_NTH_PTR(&details.uri_path, 2))->c_str);

    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_stream_get_option_string_it(
                                  stream, ANJAY_COAP_OPT_URI_PATH, &optit,
                                  &bytes_read, buffer, sizeof(buffer)),
                          ANJAY_COAP_OPTION_MISSING);

    memset(&optit, 0, sizeof(optit));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_stream_get_option_string_it(
            stream, ANJAY_COAP_OPT_URI_QUERY, &optit, &bytes_read, buffer,
            sizeof(buffer)));
    AVS_UNIT_ASSERT_EQUAL(
            bytes_read,
            strlen((*AVS_LIST_NTH_PTR(&details.uri_query, 0))->c_str) + 1);
    AVS_UNIT_ASSERT_EQUAL_STRING(
            buffer, (*AVS_LIST_NTH_PTR(&details.uri_query, 0))->c_str);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_stream_get_option_string_it(
            stream, ANJAY_COAP_OPT_URI_QUERY, &optit, &bytes_read, buffer,
            sizeof(buffer)));
    AVS_UNIT_ASSERT_EQUAL(
            bytes_read,
            strlen((*AVS_LIST_NTH_PTR(&details.uri_query, 1))->c_str) + 1);
    AVS_UNIT_ASSERT_EQUAL_STRING(
            buffer, (*AVS_LIST_NTH_PTR(&details.uri_query, 1))->c_str);

    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_stream_get_option_string_it(
                                  stream, ANJAY_COAP_OPT_URI_QUERY, &optit,
                                  &bytes_read, buffer, sizeof(buffer)),
                          ANJAY_COAP_OPTION_MISSING);

    AVS_UNIT_ASSERT_SUCCESS(avs_stream_read(
            stream, &bytes_read, &message_finished, buffer, sizeof(buffer)));
    AVS_UNIT_ASSERT_EQUAL(bytes_read, sizeof(DATA));
    AVS_UNIT_ASSERT_TRUE(message_finished);
    AVS_UNIT_ASSERT_EQUAL_BYTES(buffer, DATA);

    avs_stream_cleanup(&stream);
    AVS_LIST_CLEAR(&details.uri_path);
    AVS_LIST_CLEAR(&details.uri_query);
}

AVS_UNIT_TEST(coap_stream, no_payload) {
    anjay_coap_socket_t *socket =
            _anjay_test_setup_udp_echo_socket(TEST_PORT_UDP_ECHO);
    avs_stream_abstract_t *stream = NULL;

    anjay_msg_details_t details = {.msg_code = ANJAY_COAP_CODE_GET,
                                   .msg_type = ANJAY_COAP_MSG_NON_CONFIRMABLE };

    _anjay_mock_coap_stream_create(&stream, socket, 4096, 4096);

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_stream_setup_request(stream, &details, NULL, 0));
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

    avs_stream_cleanup(&stream);
}

AVS_UNIT_TEST(coap_stream, msg_id) {
    avs_net_abstract_socket_t *mocksock = NULL;
    avs_unit_mocksock_create(&mocksock);
    avs_unit_mocksock_expect_connect(mocksock, "", "");
    AVS_UNIT_ASSERT_SUCCESS(avs_net_socket_connect(mocksock, "", ""));

    anjay_coap_socket_t *socket = NULL;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_socket_create(&socket, mocksock));

    avs_stream_abstract_t *stream = NULL;
    _anjay_mock_coap_stream_create(&stream, socket, 4096, 4096);

    anjay_msg_details_t details = {.msg_code = ANJAY_COAP_CODE_CONTENT,
                                   .msg_type = ANJAY_COAP_MSG_NON_CONFIRMABLE,
                                   .format = ANJAY_COAP_FORMAT_NONE };

    {
        avs_unit_mocksock_expect_get_opt(mocksock, AVS_NET_SOCKET_OPT_INNER_MTU,
                                         (avs_net_socket_opt_value_t)(int)1252);
        AVS_UNIT_ASSERT_SUCCESS(
                _anjay_coap_stream_setup_request(stream, &details, NULL, 0));

        anjay_coap_msg_identity_t id;
        AVS_UNIT_ASSERT_SUCCESS(
                _anjay_coap_stream_get_request_identity(stream, &id));
        AVS_UNIT_ASSERT_EQUAL(id.token_size, 0);
        AVS_UNIT_ASSERT_EQUAL(id.msg_id, 0x69ED);

        static const char RESPONSE[] = "\x50\x45\x69\xED";
        avs_unit_mocksock_expect_output(mocksock, RESPONSE,
                                        sizeof(RESPONSE) - 1);
        AVS_UNIT_ASSERT_SUCCESS(avs_stream_finish_message(stream));

        // last request identity should be available until the stream is reset
        AVS_UNIT_ASSERT_SUCCESS(
                _anjay_coap_stream_get_request_identity(stream, &id));
        AVS_UNIT_ASSERT_EQUAL(id.token_size, 0);
        AVS_UNIT_ASSERT_EQUAL(id.msg_id, 0x69ED);

        AVS_UNIT_ASSERT_SUCCESS(avs_stream_reset(stream));
        AVS_UNIT_ASSERT_FAILED(
                _anjay_coap_stream_get_request_identity(stream, &id));
    }
    {
        avs_unit_mocksock_expect_get_opt(mocksock, AVS_NET_SOCKET_OPT_INNER_MTU,
                                         (avs_net_socket_opt_value_t)(int)1252);
        AVS_UNIT_ASSERT_SUCCESS(
                _anjay_coap_stream_setup_request(stream, &details, NULL, 0));

        anjay_coap_msg_identity_t id;
        AVS_UNIT_ASSERT_SUCCESS(
                _anjay_coap_stream_get_request_identity(stream, &id));
        AVS_UNIT_ASSERT_EQUAL(id.token_size, 0);
        AVS_UNIT_ASSERT_EQUAL(id.msg_id, 0x69EE);

        static const char RESPONSE[] = "\x50\x45\x69\xEE";
        avs_unit_mocksock_expect_output(mocksock, RESPONSE,
                                        sizeof(RESPONSE) - 1);
        AVS_UNIT_ASSERT_SUCCESS(avs_stream_finish_message(stream));
    }
    {
#define TOKEN "AYY LMAO"
        static const size_t TOKEN_SIZE = sizeof(TOKEN) - 1;
        anjay_coap_token_t token;
        memcpy(&token, TOKEN, TOKEN_SIZE);

        avs_unit_mocksock_expect_get_opt(mocksock, AVS_NET_SOCKET_OPT_INNER_MTU,
                                         (avs_net_socket_opt_value_t)(int)1252);
        AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_stream_setup_request(
                stream, &details, &token, TOKEN_SIZE));

        anjay_coap_msg_identity_t id;
        AVS_UNIT_ASSERT_SUCCESS(
                _anjay_coap_stream_get_request_identity(stream, &id));
        AVS_UNIT_ASSERT_EQUAL(id.token_size, TOKEN_SIZE);
        AVS_UNIT_ASSERT_EQUAL_BYTES(&id.token, TOKEN);
        AVS_UNIT_ASSERT_EQUAL(id.msg_id, 0x69EF);

        static const char RESPONSE[] = "\x58\x45\x69\xEF" TOKEN;
        avs_unit_mocksock_expect_output(mocksock, RESPONSE,
                                        sizeof(RESPONSE) - 1);
        AVS_UNIT_ASSERT_SUCCESS(avs_stream_finish_message(stream));
#undef TOKEN
    }
    {
        avs_unit_mocksock_expect_get_opt(mocksock, AVS_NET_SOCKET_OPT_INNER_MTU,
                                         (avs_net_socket_opt_value_t)(int)1252);
        AVS_UNIT_ASSERT_SUCCESS(
                _anjay_coap_stream_setup_request(stream, &details, NULL, 0));

        anjay_coap_msg_identity_t id;
        AVS_UNIT_ASSERT_SUCCESS(
                _anjay_coap_stream_get_request_identity(stream, &id));
        AVS_UNIT_ASSERT_EQUAL(id.token_size, 0);
        AVS_UNIT_ASSERT_EQUAL(id.msg_id, 0x69F0);

        static const char RESPONSE[] = "\x50\x45\x69\xF0";
        avs_unit_mocksock_expect_output(mocksock, RESPONSE,
                                        sizeof(RESPONSE) - 1);
        AVS_UNIT_ASSERT_SUCCESS(avs_stream_finish_message(stream));
    }
    avs_unit_mocksock_assert_io_clean(mocksock);
    avs_stream_cleanup(&stream);
}

AVS_UNIT_TEST(coap_stream, read_some) {
    anjay_coap_socket_t *socket =
            _anjay_test_setup_udp_ack_echo_socket(TEST_PORT_UDP_ACK);
    avs_stream_abstract_t *stream = NULL;

    anjay_msg_details_t details = {.msg_code = ANJAY_COAP_CODE_CONTENT,
                                   .msg_type = ANJAY_COAP_MSG_CONFIRMABLE };

    _anjay_mock_coap_stream_create(&stream, socket, 4096, 4096);

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_stream_setup_request(stream, &details, NULL, 0));

    const char DATA[] = "Bacon ipsum dolor amet";
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_write(stream, DATA, sizeof(DATA) - 1));
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_finish_message(stream));

    uint8_t code;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_stream_get_code(stream, &code));
    AVS_UNIT_ASSERT_EQUAL(details.msg_code, code);

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

    avs_stream_cleanup(&stream);
}

AVS_UNIT_TEST(coap_stream, confirmable) {
    anjay_coap_socket_t *socket =
            _anjay_test_setup_udp_ack_echo_socket(TEST_PORT_UDP_ACK);
    avs_stream_abstract_t *stream = NULL;

    anjay_msg_details_t details = {.msg_code = ANJAY_COAP_CODE_CONTENT,
                                   .msg_type = ANJAY_COAP_MSG_CONFIRMABLE };

    _anjay_mock_coap_stream_create(&stream, socket, 4096, 4096);

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_stream_setup_request(stream, &details, NULL, 0));

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

    avs_stream_cleanup(&stream);
}

AVS_UNIT_TEST(coap_stream, reset_when_sending) {
    anjay_coap_socket_t *socket =
            _anjay_test_setup_udp_reset_socket(TEST_PORT_UDP_RESET);
    avs_stream_abstract_t *stream = NULL;

    anjay_msg_details_t details = {.msg_code = ANJAY_COAP_CODE_CONTENT,
                                   .msg_type = ANJAY_COAP_MSG_CONFIRMABLE };

    _anjay_mock_coap_stream_create(&stream, socket, 4096, 4096);

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_stream_setup_request(stream, &details, NULL, 0));
    AVS_UNIT_ASSERT_FAILED(avs_stream_finish_message(stream));

    avs_stream_cleanup(&stream);
}

AVS_UNIT_TEST(coap_stream, mismatched_reset) {
    anjay_coap_socket_t *socket =
            _anjay_test_setup_udp_mismatched_ack_then_reset_socket(
                    TEST_PORT_UDP_MISMATCHED_RESET);
    avs_stream_abstract_t *stream = NULL;

    anjay_msg_details_t details = {.msg_code = ANJAY_COAP_CODE_CONTENT,
                                   .msg_type = ANJAY_COAP_MSG_CONFIRMABLE };

    _anjay_mock_coap_stream_create(&stream, socket, 4096, 4096);

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_stream_setup_request(stream, &details, NULL, 0));
    AVS_UNIT_ASSERT_FAILED(avs_stream_finish_message(stream));

    avs_stream_cleanup(&stream);
}

AVS_UNIT_TEST(coap_stream, garbage_response_when_waiting_for_ack) {
    anjay_coap_socket_t *socket = _anjay_test_setup_udp_garbage_then_ack_socket(
            TEST_PORT_UDP_GARBAGE_ACK);
    avs_stream_abstract_t *stream = NULL;

    anjay_msg_details_t details = {.msg_code = ANJAY_COAP_CODE_CONTENT,
                                   .msg_type = ANJAY_COAP_MSG_CONFIRMABLE };

    _anjay_mock_coap_stream_create(&stream, socket, 4096, 4096);

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_stream_setup_request(stream, &details, NULL, 0));
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_finish_message(stream));

    avs_stream_cleanup(&stream);
}

AVS_UNIT_TEST(coap_stream, ack_with_mismatched_id) {
    anjay_coap_socket_t *socket =
            _anjay_test_setup_udp_mismatched_reset_then_ack_socket(
                    TEST_PORT_UDP_MISMATCHED);
    avs_stream_abstract_t *stream = NULL;

    anjay_msg_details_t details = {.msg_code = ANJAY_COAP_CODE_CONTENT,
                                   .msg_type = ANJAY_COAP_MSG_CONFIRMABLE };

    _anjay_mock_coap_stream_create(&stream, socket, 4096, 4096);

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_stream_setup_request(stream, &details, NULL, 0));
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_finish_message(stream));

    avs_stream_cleanup(&stream);
}

AVS_UNIT_TEST(coap_stream, long_separate) {
    anjay_coap_socket_t *socket = _anjay_test_setup_udp_long_separate_socket(
            TEST_PORT_UDP_LONG_SEPARATE);
    avs_stream_abstract_t *stream = NULL;

    anjay_msg_details_t details = {.msg_code = ANJAY_COAP_CODE_CONTENT,
                                   .msg_type = ANJAY_COAP_MSG_CONFIRMABLE };

    char out_data[] = "follow the white rabbit";

    _anjay_mock_coap_stream_create(&stream, socket, 4096, 4096);

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_stream_setup_request(stream, &details, NULL, 0));
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

    avs_stream_cleanup(&stream);
}

AVS_UNIT_TEST(coap_stream, receive_garbage) {
    anjay_coap_socket_t *socket = _anjay_test_setup_udp_garbage_then_ack_socket(
            TEST_PORT_UDP_GARBAGE);
    avs_stream_abstract_t *stream = NULL;

    anjay_msg_details_t details = {.msg_code = ANJAY_COAP_CODE_CONTENT,
                                   .msg_type = ANJAY_COAP_MSG_NON_CONFIRMABLE };

    _anjay_mock_coap_stream_create(&stream, socket, 4096, 4096);

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_stream_setup_request(stream, &details, NULL, 0));
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_finish_message(stream));

    // avs_stream_finish_message should return without waiting for a response
    // here, but a garbage packet should be received, so the read will not block
    char message_finished;
    size_t bytes_read;
    char buffer[256];
    AVS_UNIT_ASSERT_FAILED(avs_stream_read(stream, &bytes_read,
                                           &message_finished, buffer, 11));

    avs_stream_cleanup(&stream);
}

AVS_UNIT_TEST(coap_stream, add_observe_option) {
    avs_net_abstract_socket_t *mocksock = NULL;
    avs_unit_mocksock_create(&mocksock);
    avs_unit_mocksock_expect_connect(mocksock, "", "");
    AVS_UNIT_ASSERT_SUCCESS(avs_net_socket_connect(mocksock, "", ""));

    anjay_coap_socket_t *socket = NULL;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_socket_create(&socket, mocksock));

    avs_stream_abstract_t *stream = NULL;
    _anjay_mock_coap_stream_create(&stream, socket, 4096, 4096);

    anjay_msg_details_t details = {.msg_code = ANJAY_COAP_CODE_CONTENT,
                                   .msg_type = ANJAY_COAP_MSG_NON_CONFIRMABLE,
                                   .format = ANJAY_COAP_FORMAT_NONE };

    {
        details.observe_serial = true;
        _anjay_mock_clock_start(&(const struct timespec){ 514, 777 << 15 });
        avs_unit_mocksock_expect_get_opt(mocksock, AVS_NET_SOCKET_OPT_INNER_MTU,
                                         (avs_net_socket_opt_value_t)(int)1252);
        AVS_UNIT_ASSERT_SUCCESS(
                _anjay_coap_stream_setup_request(stream, &details, NULL, 0));
        static const char RESPONSE[] = "\x50\x45\x69\xED" // CoAP header
                                       "\x63\x01\x03\x09";
        avs_unit_mocksock_expect_output(mocksock, RESPONSE,
                                        sizeof(RESPONSE) - 1);
        AVS_UNIT_ASSERT_SUCCESS(avs_stream_finish_message(stream));
        _anjay_mock_clock_finish();
    }
    {
        details.observe_serial = true;
        _anjay_mock_clock_start(&(const struct timespec){ 777, 514 << 15 });
        avs_unit_mocksock_expect_get_opt(mocksock, AVS_NET_SOCKET_OPT_INNER_MTU,
                                         (avs_net_socket_opt_value_t)(int)1252);
        AVS_UNIT_ASSERT_SUCCESS(
                _anjay_coap_stream_setup_request(stream, &details, NULL, 0));
        static const char RESPONSE[] = "\x50\x45\x69\xEE" // CoAP header
                                       "\x63\x84\x82\x02";
        avs_unit_mocksock_expect_output(mocksock, RESPONSE,
                                        sizeof(RESPONSE) - 1);
        AVS_UNIT_ASSERT_SUCCESS(avs_stream_finish_message(stream));
        _anjay_mock_clock_finish();
    }
    avs_unit_mocksock_assert_io_clean(mocksock);
    avs_stream_cleanup(&stream);
}

typedef struct test_data {
    avs_net_abstract_socket_t *mock_socket;
    anjay_coap_socket_t *coap_socket;
    avs_stream_abstract_t *stream;
} test_data_t;

static test_data_t setup_test(void) {
    test_data_t data = {.mock_socket = NULL,
                        .coap_socket = NULL,
                        .stream = NULL };

    avs_unit_mocksock_create(&data.mock_socket);
    avs_unit_mocksock_expect_connect(data.mock_socket, "", "");

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_socket_create(&data.coap_socket, data.mock_socket));
    AVS_UNIT_ASSERT_SUCCESS(avs_net_socket_connect(data.mock_socket, "", ""));
    _anjay_mock_coap_stream_create(&data.stream, data.coap_socket, 4096, 4096);

    return data;
}

static void teardown_test(test_data_t *data) {
    avs_unit_mocksock_assert_expects_met(data->mock_socket);
    avs_unit_mocksock_assert_io_clean(data->mock_socket);
    avs_stream_cleanup(&data->stream);
    memset(data, 0, sizeof(*data));
}

static void mock_receive_request(test_data_t *test,
                                 const char *request,
                                 size_t request_size) {
    avs_unit_mocksock_input(test->mock_socket, request, request_size);

    anjay_coap_msg_type_t type;
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_stream_get_msg_type(test->stream, &type));
}

AVS_UNIT_TEST(coap_stream, response_empty) {
    test_data_t test = setup_test();

    static const char REQUEST[] =
            "\x40\x03\x00\x01"; // Confirmable, 0.03 Put, id = 1
    mock_receive_request(&test, REQUEST, sizeof(REQUEST) - 1);

    static const char RESPONSE[] =
            "\x60\x44\x00\x01"; // Acknowledgement, 2.04 Changed, id = 1
    avs_unit_mocksock_expect_output(test.mock_socket, RESPONSE,
                                    sizeof(RESPONSE) - 1);

    const anjay_msg_details_t details = {.msg_type =
                                                 ANJAY_COAP_MSG_ACKNOWLEDGEMENT,
                                         .msg_code = ANJAY_COAP_CODE_CHANGED,
                                         .format = ANJAY_COAP_FORMAT_NONE };
    avs_unit_mocksock_expect_get_opt(test.mock_socket,
                                     AVS_NET_SOCKET_OPT_INNER_MTU,
                                     (avs_net_socket_opt_value_t) (int) 1252);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_stream_setup_response(test.stream, &details));
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_finish_message(test.stream));

    teardown_test(&test);
}

AVS_UNIT_TEST(coap_stream, response_token) {
    test_data_t test = setup_test();

#define TOKEN "TOKEN123"
    static const char REQUEST[] =
            "\x48\x03\x00\x01" // Confirmable, 8-char token, 0.03 Put, id 1
            TOKEN;
    mock_receive_request(&test, REQUEST, sizeof(REQUEST) - 1);

    static const char RESPONSE[] =
            "\x68\x44\x00\x01" // Acknowledgement, 8-char token, 2.04 Changed,
                               // id 1
            TOKEN;
    avs_unit_mocksock_expect_output(test.mock_socket, RESPONSE,
                                    sizeof(RESPONSE) - 1);
#undef TOKEN

    const anjay_msg_details_t details = {.msg_type =
                                                 ANJAY_COAP_MSG_ACKNOWLEDGEMENT,
                                         .msg_code = ANJAY_COAP_CODE_CHANGED,
                                         .format = ANJAY_COAP_FORMAT_NONE };
    avs_unit_mocksock_expect_get_opt(test.mock_socket,
                                     AVS_NET_SOCKET_OPT_INNER_MTU,
                                     (avs_net_socket_opt_value_t) (int) 1252);
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
    static const char REQUEST[] =
            "\x40\x03\x00\x01"; // Confirmable, 0.03 Put, id = 1
    mock_receive_request(&test, REQUEST, sizeof(REQUEST) - 1);

    static const char RESPONSE[] =
            "\x60\x44\x00\x01" // Acknowledgement, 2.04 Changed, id = 1
            "\xFF" CONTENT;
    avs_unit_mocksock_expect_output(test.mock_socket, RESPONSE,
                                    sizeof(RESPONSE) - 1);

    const anjay_msg_details_t details = {.msg_type =
                                                 ANJAY_COAP_MSG_ACKNOWLEDGEMENT,
                                         .msg_code = ANJAY_COAP_CODE_CHANGED,
                                         .format = ANJAY_COAP_FORMAT_NONE };
    avs_unit_mocksock_expect_get_opt(test.mock_socket,
                                     AVS_NET_SOCKET_OPT_INNER_MTU,
                                     (avs_net_socket_opt_value_t) (int) 1252);
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

    static const char REQUEST[] =
            "\x40\x03\x00\x01"; // Confirmable, 0.03 Put, id = 1
    mock_receive_request(&test, REQUEST, sizeof(REQUEST) - 1);

    static const char RESPONSE[] =
            "\x60\x44\x00\x01" // Acknowledgement, 2.04 Changed, id = 1
            "\x87"
            "slychac"
            "\x06"
            "trzask"
            "\x04"
            "bylo"
            "\x07"
            "zalozyc"
            "\x04"
            "kask"
            "\x31"
            "w"
            "\x03"
            "ryj"
            "\x01"
            "z"
            "\x04"
            "kopa"
            "\x4b"
            "albo=lepiej"
            "\x07"
            "w=jadra";
    avs_unit_mocksock_expect_output(test.mock_socket, RESPONSE,
                                    sizeof(RESPONSE) - 1);

    const anjay_msg_details_t details = {
        .msg_type = ANJAY_COAP_MSG_ACKNOWLEDGEMENT,
        .msg_code = ANJAY_COAP_CODE_CHANGED,
        .format = ANJAY_COAP_FORMAT_NONE,
        .uri_path = _anjay_make_string_list("w", "ryj", "z", "kopa", NULL),
        .uri_query = _anjay_make_string_list("albo=lepiej", "w=jadra", NULL),
        .location_path = _anjay_make_string_list("slychac", "trzask", "bylo",
                                                 "zalozyc", "kask", NULL)
    };
    avs_unit_mocksock_expect_get_opt(test.mock_socket,
                                     AVS_NET_SOCKET_OPT_INNER_MTU,
                                     (avs_net_socket_opt_value_t) (int) 1252);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_stream_setup_response(test.stream, &details));
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_finish_message(test.stream));

    AVS_LIST_CLEAR((AVS_LIST(anjay_string_t) *) (intptr_t) &details.uri_path);
    AVS_LIST_CLEAR((AVS_LIST(anjay_string_t) *) (intptr_t) &details.uri_query);
    AVS_LIST_CLEAR((AVS_LIST(anjay_string_t) *) (intptr_t) &details.location_path);
    teardown_test(&test);
}

AVS_UNIT_TEST(coap_stream, response_no_request) {
    test_data_t test = setup_test();

    const anjay_msg_details_t details = {.msg_type =
                                                 ANJAY_COAP_MSG_ACKNOWLEDGEMENT,
                                         .msg_code = ANJAY_COAP_CODE_CHANGED,
                                         .format = ANJAY_COAP_FORMAT_NONE };
    AVS_UNIT_ASSERT_FAILED(
            _anjay_coap_stream_setup_response(test.stream, &details));

    teardown_test(&test);
}

AVS_UNIT_TEST(coap_stream, fuzz_1_invalid_block_size) {
    // According to [ietf-core-block-21], 2.2 "Structure of a Block Option":
    // > The value 7 for SZX (which would indicate a block size of 2048) is
    // > reserved, i.e. MUST NOT be sent and MUST lead to a 4.00 Bad Request
    // > response code upon reception in a request.

    static const char MESSAGE[] =
            "\x40"     // Confirmable, token size = 0
            "\x03"     // 0.03 Put
            "\x00\x01" // message ID
            "\xd1\x0e" // delta = 13 + 14, length = 1
            "\x07";    // seq_num = 0, has_more = 0, block_size = 2048

    static const char BAD_OPTION_RES[] =
            "\x60"      // Acknowledgement, token size = 0
            "\x80"      // 4.00 Bad Request
            "\x00\x01"; // message ID

    avs_net_abstract_socket_t *mocksock = NULL;
    avs_unit_mocksock_create(&mocksock);
    avs_unit_mocksock_expect_connect(mocksock, "", "");
    avs_unit_mocksock_input(mocksock, MESSAGE, sizeof(MESSAGE) - 1);
    avs_unit_mocksock_expect_output(mocksock, BAD_OPTION_RES,
                                    sizeof(BAD_OPTION_RES) - 1);

    anjay_coap_socket_t *socket = NULL;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_socket_create(&socket, mocksock));
    AVS_UNIT_ASSERT_SUCCESS(avs_net_socket_connect(mocksock, "", ""));

    avs_stream_abstract_t *stream = NULL;
    _anjay_mock_coap_stream_create(&stream, socket, 4096, 4096);

    char message_finished;
    size_t bytes_read;
    char buffer[256];
    AVS_UNIT_ASSERT_FAILED(avs_stream_read(
            stream, &bytes_read, &message_finished, buffer, sizeof(buffer)));

    avs_stream_cleanup(&stream);
}
