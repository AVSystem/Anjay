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
        && defined(WITH_AVS_COAP_OBSERVE)

#    include <avsystem/commons/avs_errno.h>

#    include <avsystem/coap/coap.h>

#    define MODULE_NAME test
#    include <avs_coap_x_log_config.h>

#    include "./utils.h"

AVS_UNIT_TEST(udp_observe, start) {
    test_env_t env __attribute__((cleanup(test_teardown_late_expects_check))) =
            test_setup_default();

    const test_msg_t *request = COAP_MSG(CON, GET, ID(0), MAKE_TOKEN("Obserw"),
                                         OBSERVE(0), NO_PAYLOAD);
    // Note: Observe option values start at 0 (in a response to the initial
    // Observe) and get incremented by one with each sent notification
    const test_msg_t *response =
            COAP_MSG(ACK, CONTENT, ID(0), MAKE_TOKEN("Obserw"), OBSERVE(0),
                     NO_PAYLOAD);

    expect_recv(&env, request);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_RECEIVED, request,
                                &(avs_coap_response_header_t) {
                                    .code = response->response_header.code
                                },
                                NULL);
    expect_observe_start(&env, MAKE_TOKEN("Obserw"));

    expect_send(&env, response);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                NULL, NULL);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));

    // should be canceled by cleanup
    expect_observe_cancel(&env, MAKE_TOKEN("Obserw"));
}

AVS_UNIT_TEST(udp_observe, start_twice) {
    test_env_t env __attribute__((cleanup(test_teardown_late_expects_check))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), MAKE_TOKEN("Obserw"), OBSERVE(0), NO_PAYLOAD),
        COAP_MSG(CON, GET, ID(1), MAKE_TOKEN("Obserw"), OBSERVE(0), NO_PAYLOAD)
    };

    const test_msg_t *responses[] = { COAP_MSG(ACK, CONTENT, ID(0),
                                               MAKE_TOKEN("Obserw"), OBSERVE(0),
                                               NO_PAYLOAD),
                                      COAP_MSG(ACK, CONTENT, ID(1),
                                               MAKE_TOKEN("Obserw"), OBSERVE(0),
                                               NO_PAYLOAD) };

    for (size_t i = 0; i < AVS_ARRAY_SIZE(requests); i++) {
        expect_recv(&env, requests[i]);
        expect_request_handler_call(
                &env, AVS_COAP_SERVER_REQUEST_RECEIVED, requests[i],
                &(avs_coap_response_header_t) {
                    .code = responses[i]->response_header.code
                },
                NULL);
        expect_observe_start(&env, MAKE_TOKEN("Obserw"));
        expect_send(&env, responses[i]);
        expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                    NULL, NULL);
        expect_timeout(&env);
        ASSERT_OK(avs_coap_async_handle_incoming_packet(
                env.coap_ctx, test_accept_new_request, &env));
        expect_observe_cancel(&env, MAKE_TOKEN("Obserw"));
    }
}

AVS_UNIT_TEST(udp_observe, cancel_with_observe_option) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), MAKE_TOKEN("Obserw"), OBSERVE(0), NO_PAYLOAD),
        COAP_MSG(CON, GET, ID(1), MAKE_TOKEN("Obserw"), OBSERVE(1), NO_PAYLOAD),
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(0), MAKE_TOKEN("Obserw"), OBSERVE(0),
                 NO_PAYLOAD),
        COAP_MSG(ACK, CONTENT, ID(1), MAKE_TOKEN("Obserw"), NO_PAYLOAD),
    };

    expect_recv(&env, requests[0]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_RECEIVED,
                                requests[0],
                                &(avs_coap_response_header_t) {
                                    .code = responses[0]->response_header.code
                                },
                                NULL);
    expect_observe_start(&env, MAKE_TOKEN("Obserw"));
    expect_send(&env, responses[0]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                NULL, NULL);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));

    expect_recv(&env, requests[1]);
    expect_observe_cancel(&env, MAKE_TOKEN("Obserw"));
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_RECEIVED,
                                requests[1],
                                &(avs_coap_response_header_t) {
                                    .code = responses[1]->response_header.code
                                },
                                NULL);
    expect_send(&env, responses[1]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                NULL, NULL);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));
}

AVS_UNIT_TEST(udp_observe, notify_async) {
#    define NOTIFY_PAYLOAD "Notifaj"
    test_env_t env __attribute__((cleanup(test_teardown_late_expects_check))) =
            test_setup_default();

    const test_msg_t *request =
            COAP_MSG(CON, GET, ID(100), MAKE_TOKEN("Obserw"), OBSERVE(0),
                     NO_PAYLOAD);
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(100), MAKE_TOKEN("Obserw"), OBSERVE(0),
                 NO_PAYLOAD),
        COAP_MSG(NON, CONTENT, ID(0), MAKE_TOKEN("Obserw"), OBSERVE(1),
                 PAYLOAD(NOTIFY_PAYLOAD)),
    };

    expect_recv(&env, request);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_RECEIVED, request,
                                &(avs_coap_response_header_t) {
                                    .code = responses[0]->response_header.code
                                },
                                NULL);
    expect_observe_start(&env, MAKE_TOKEN("Obserw"));
    expect_send(&env, responses[0]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                NULL, NULL);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));

    avs_coap_observe_id_t observe_id = {
        .token = request->msg.token
    };
    test_payload_writer_args_t test_payload = {
        .payload = NOTIFY_PAYLOAD,
        .payload_size = sizeof(NOTIFY_PAYLOAD) - 1
    };

    expect_send(&env, responses[1]);

    avs_coap_exchange_id_t id;
    ASSERT_OK(avs_coap_notify_async(
            env.coap_ctx, &id, observe_id, &responses[1]->response_header,
            AVS_COAP_NOTIFY_PREFER_NON_CONFIRMABLE, test_payload_writer,
            &test_payload, NULL, NULL));
    ASSERT_FALSE(avs_coap_exchange_id_valid(id));

    // should be canceled by cleanup
    expect_observe_cancel(&env, MAKE_TOKEN("Obserw"));
#    undef NOTIFY_PAYLOAD
}

AVS_UNIT_TEST(udp_observe, notify_async_confirmable) {
#    define NOTIFY_PAYLOAD "Notifaj"
    test_env_t env __attribute__((cleanup(test_teardown_late_expects_check))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(100), MAKE_TOKEN("Obserw"), OBSERVE(0),
                 NO_PAYLOAD),
        COAP_MSG(ACK, EMPTY, ID(0), NO_PAYLOAD),
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(100), MAKE_TOKEN("Obserw"), OBSERVE(0),
                 NO_PAYLOAD),
        COAP_MSG(CON, CONTENT, ID(0), MAKE_TOKEN("Obserw"), OBSERVE(1),
                 PAYLOAD(NOTIFY_PAYLOAD)),
    };

    expect_recv(&env, requests[0]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_RECEIVED,
                                requests[0],
                                &(avs_coap_response_header_t) {
                                    .code = responses[0]->response_header.code
                                },
                                NULL);
    expect_observe_start(&env, MAKE_TOKEN("Obserw"));
    expect_send(&env, responses[0]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                NULL, NULL);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));

    avs_coap_observe_id_t observe_id = {
        .token = requests[0]->msg.token
    };
    test_payload_writer_args_t test_payload = {
        .payload = NOTIFY_PAYLOAD,
        .payload_size = sizeof(NOTIFY_PAYLOAD) - 1
    };

    expect_send(&env, responses[1]);

    avs_coap_exchange_id_t id;
    ASSERT_OK(avs_coap_notify_async(
            env.coap_ctx, &id, observe_id, &responses[1]->response_header,
            AVS_COAP_NOTIFY_PREFER_CONFIRMABLE, test_payload_writer,
            &test_payload, test_observe_delivery_handler, &env));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_recv(&env, requests[1]);
    expect_observe_delivery(&env, AVS_OK);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    // should be canceled by cleanup
    expect_observe_cancel(&env, MAKE_TOKEN("Obserw"));
#    undef NOTIFY_PAYLOAD
}

AVS_UNIT_TEST(udp_observe, notify_async_cancel_with_error_response) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *request =
            COAP_MSG(CON, GET, ID(100), MAKE_TOKEN("Obserw"), OBSERVE(0),
                     NO_PAYLOAD);
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(100), MAKE_TOKEN("Obserw"), OBSERVE(0),
                 NO_PAYLOAD),
        COAP_MSG(NON, NOT_FOUND, ID(0), MAKE_TOKEN("Obserw"), OBSERVE(1),
                 NO_PAYLOAD),
    };

    expect_recv(&env, request);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_RECEIVED, request,
                                &(avs_coap_response_header_t) {
                                    .code = responses[0]->response_header.code
                                },
                                NULL);
    expect_observe_start(&env, request->msg.token);
    expect_send(&env, responses[0]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                NULL, NULL);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));
    avs_coap_observe_id_t observe_id = {
        .token = request->msg.token
    };

    expect_send(&env, responses[1]);
    expect_observe_cancel(&env, observe_id.token);

    avs_coap_exchange_id_t id;
    ASSERT_OK(avs_coap_notify_async(
            env.coap_ctx, &id, observe_id, &responses[1]->response_header,
            AVS_COAP_NOTIFY_PREFER_NON_CONFIRMABLE, NULL, NULL, NULL, NULL));
    ASSERT_FALSE(avs_coap_exchange_id_valid(id));
}

AVS_UNIT_TEST(udp_observe,
              notify_async_cancel_with_confirmable_error_response) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(100), MAKE_TOKEN("Obserw"), OBSERVE(0),
                 NO_PAYLOAD),
        COAP_MSG(ACK, EMPTY, ID(0), NO_PAYLOAD),
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(100), MAKE_TOKEN("Obserw"), OBSERVE(0),
                 NO_PAYLOAD),
        COAP_MSG(CON, NOT_FOUND, ID(0), MAKE_TOKEN("Obserw"), OBSERVE(1),
                 NO_PAYLOAD),
    };

    expect_recv(&env, requests[0]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_RECEIVED,
                                requests[0],
                                &(avs_coap_response_header_t) {
                                    .code = responses[0]->response_header.code
                                },
                                NULL);
    expect_observe_start(&env, requests[0]->msg.token);
    expect_send(&env, responses[0]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                NULL, NULL);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));
    avs_coap_observe_id_t observe_id = {
        .token = requests[0]->msg.token
    };

    expect_send(&env, responses[1]);
    expect_observe_cancel(&env, observe_id.token);

    avs_coap_exchange_id_t id;
    ASSERT_OK(avs_coap_notify_async(env.coap_ctx, &id, observe_id,
                                    &responses[1]->response_header,
                                    AVS_COAP_NOTIFY_PREFER_CONFIRMABLE, NULL,
                                    NULL, test_observe_delivery_handler, &env));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_recv(&env, requests[1]);
    expect_observe_delivery(&env, AVS_OK);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

AVS_UNIT_TEST(udp_observe, notify_async_confirmable_reset_response) {
#    define NOTIFY_PAYLOAD "Notifaj"
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(100), MAKE_TOKEN("Obserw"), OBSERVE(0),
                 NO_PAYLOAD),
        COAP_MSG(RST, EMPTY, ID(0), NO_PAYLOAD),
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(100), MAKE_TOKEN("Obserw"), OBSERVE(0),
                 NO_PAYLOAD),
        COAP_MSG(CON, CONTENT, ID(0), MAKE_TOKEN("Obserw"), OBSERVE(1),
                 PAYLOAD(NOTIFY_PAYLOAD)),
    };

    expect_recv(&env, requests[0]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_RECEIVED,
                                requests[0],
                                &(avs_coap_response_header_t) {
                                    .code = responses[0]->response_header.code
                                },
                                NULL);
    expect_observe_start(&env, MAKE_TOKEN("Obserw"));
    expect_send(&env, responses[0]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                NULL, NULL);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));

    avs_coap_observe_id_t observe_id = {
        .token = requests[0]->msg.token
    };
    test_payload_writer_args_t test_payload = {
        .payload = NOTIFY_PAYLOAD,
        .payload_size = sizeof(NOTIFY_PAYLOAD) - 1
    };

    expect_send(&env, responses[1]);

    avs_coap_exchange_id_t id;
    ASSERT_OK(avs_coap_notify_async(
            env.coap_ctx, &id, observe_id, &responses[1]->response_header,
            AVS_COAP_NOTIFY_PREFER_CONFIRMABLE, test_payload_writer,
            &test_payload, test_observe_delivery_handler, &env));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_recv(&env, requests[1]);
    // Reset response should trigger FAIL result and observe cancellation
    expect_observe_delivery(&env,
                            _avs_coap_err(AVS_COAP_ERR_UDP_RESET_RECEIVED));
    expect_observe_cancel(&env, MAKE_TOKEN("Obserw"));

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
#    undef NOTIFY_PAYLOAD
}

AVS_UNIT_TEST(udp_observe, notify_async_non_confirmable_reset_response) {
#    define NOTIFY_PAYLOAD "Notifaj"
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(100), MAKE_TOKEN("Obserw"), OBSERVE(0),
                 NO_PAYLOAD),
        COAP_MSG(RST, EMPTY, ID(0), NO_PAYLOAD),
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(100), MAKE_TOKEN("Obserw"), OBSERVE(0),
                 NO_PAYLOAD),
        COAP_MSG(NON, CONTENT, ID(0), MAKE_TOKEN("Obserw"), OBSERVE(1),
                 PAYLOAD(NOTIFY_PAYLOAD)),
    };

    expect_recv(&env, requests[0]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_RECEIVED,
                                requests[0],
                                &(avs_coap_response_header_t) {
                                    .code = responses[0]->response_header.code
                                },
                                NULL);
    expect_observe_start(&env, MAKE_TOKEN("Obserw"));
    expect_send(&env, responses[0]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                NULL, NULL);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));

    avs_coap_observe_id_t observe_id = {
        .token = requests[0]->msg.token
    };
    test_payload_writer_args_t test_payload = {
        .payload = NOTIFY_PAYLOAD,
        .payload_size = sizeof(NOTIFY_PAYLOAD) - 1
    };

    expect_send(&env, responses[1]);

    avs_coap_exchange_id_t id;
    ASSERT_OK(avs_coap_notify_async(
            env.coap_ctx, &id, observe_id, &responses[1]->response_header,
            AVS_COAP_NOTIFY_PREFER_NON_CONFIRMABLE, test_payload_writer,
            &test_payload, NULL, NULL));
    ASSERT_FALSE(avs_coap_exchange_id_valid(id));

    expect_recv(&env, requests[1]);
    // Reset response should trigger observe cancellation
    expect_observe_cancel(&env, MAKE_TOKEN("Obserw"));

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
#    undef NOTIFY_PAYLOAD
}

AVS_UNIT_TEST(udp_observe, notify_async_delayed_reset_response) {
#    define NOTIFY_PAYLOAD "Notifaj"
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *request =
            COAP_MSG(CON, GET, ID(100), MAKE_TOKEN("Obserw"), OBSERVE(0),
                     NO_PAYLOAD);
    const test_msg_t *response =
            COAP_MSG(ACK, CONTENT, ID(100), MAKE_TOKEN("Obserw"), OBSERVE(0),
                     NO_PAYLOAD);

    expect_recv(&env, request);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_RECEIVED, request,
                                &(avs_coap_response_header_t) {
                                    .code = response->response_header.code
                                },
                                NULL);
    expect_observe_start(&env, MAKE_TOKEN("Obserw"));
    expect_send(&env, response);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                NULL, NULL);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));

    avs_coap_observe_id_t observe_id = {
        .token = request->msg.token
    };
    test_payload_writer_args_t test_payload = {
        .payload = NOTIFY_PAYLOAD,
        .payload_size = sizeof(NOTIFY_PAYLOAD) - 1
    };

    // Send multiple notifications, make sure a delayed response to the first
    // one causes cancellation if the cache is big enough
    static const size_t NUM_NOTIFICATIONS = AVS_COAP_UDP_NOTIFY_CACHE_SIZE;
    for (size_t i = 0; i < NUM_NOTIFICATIONS; ++i) {
        const test_msg_t *notify =
                COAP_MSG(NON, CONTENT, ID((uint16_t) i), MAKE_TOKEN("Obserw"),
                         OBSERVE((uint32_t) (i + 1)), PAYLOAD(NOTIFY_PAYLOAD));

        expect_send(&env, notify);

        avs_coap_exchange_id_t id;
        test_payload.expected_payload_offset = 0;
        ASSERT_OK(avs_coap_notify_async(
                env.coap_ctx, &id, observe_id, &notify->response_header,
                AVS_COAP_NOTIFY_PREFER_NON_CONFIRMABLE, test_payload_writer,
                &test_payload, NULL, NULL));
        ASSERT_FALSE(avs_coap_exchange_id_valid(id));
    };

    // first Notify had ID = 0
    const uint16_t oldest_id_in_cache = 0;
    const test_msg_t *reset =
            COAP_MSG(RST, EMPTY, ID(oldest_id_in_cache), NO_PAYLOAD);

    expect_recv(&env, reset);
    // Reset response should trigger observe cancellation
    expect_observe_cancel(&env, MAKE_TOKEN("Obserw"));

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
#    undef NOTIFY_PAYLOAD
}

AVS_UNIT_TEST(udp_observe, notify_async_send_error) {
#    define NOTIFY_PAYLOAD "Notifaj"
    test_env_t env __attribute__((cleanup(test_teardown_late_expects_check))) =
            test_setup_default();

    const test_msg_t *request =
            COAP_MSG(CON, GET, ID(100), MAKE_TOKEN("Obserw"), OBSERVE(0),
                     NO_PAYLOAD);
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(100), MAKE_TOKEN("Obserw"), OBSERVE(0),
                 NO_PAYLOAD),
        COAP_MSG(NON, CONTENT, ID(0), MAKE_TOKEN("Obserw"), OBSERVE(1),
                 PAYLOAD(NOTIFY_PAYLOAD)),
    };

    expect_recv(&env, request);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_RECEIVED, request,
                                &(avs_coap_response_header_t) {
                                    .code = responses[0]->response_header.code
                                },
                                NULL);
    expect_observe_start(&env, MAKE_TOKEN("Obserw"));
    expect_send(&env, responses[0]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                NULL, NULL);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));

    avs_coap_observe_id_t observe_id = {
        .token = request->msg.token
    };
    test_payload_writer_args_t test_payload = {
        .payload = NOTIFY_PAYLOAD,
        .payload_size = sizeof(NOTIFY_PAYLOAD) - 1
    };

    avs_unit_mocksock_output_fail(env.mocksock, avs_errno(AVS_ECONNREFUSED));

    ASSERT_FAIL(avs_coap_notify_async(
            env.coap_ctx, NULL, observe_id, &responses[1]->response_header,
            AVS_COAP_NOTIFY_PREFER_NON_CONFIRMABLE, test_payload_writer,
            &test_payload, NULL, NULL));

    // should be canceled by cleanup
    expect_observe_cancel(&env, MAKE_TOKEN("Obserw"));
#    undef NOTIFY_PAYLOAD
}

AVS_UNIT_TEST(udp_observe, notify_async_confirmable_send_error) {
#    define NOTIFY_PAYLOAD "Notifaj"
    test_env_t env __attribute__((cleanup(test_teardown_late_expects_check))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(100), MAKE_TOKEN("Obserw"), OBSERVE(0),
                 NO_PAYLOAD),
        COAP_MSG(ACK, EMPTY, ID(0), NO_PAYLOAD),
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(100), MAKE_TOKEN("Obserw"), OBSERVE(0),
                 NO_PAYLOAD),
        COAP_MSG(CON, CONTENT, ID(0), MAKE_TOKEN("Obserw"), OBSERVE(1),
                 PAYLOAD(NOTIFY_PAYLOAD)),
    };

    expect_recv(&env, requests[0]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_RECEIVED,
                                requests[0],
                                &(avs_coap_response_header_t) {
                                    .code = responses[0]->response_header.code
                                },
                                NULL);
    expect_observe_start(&env, MAKE_TOKEN("Obserw"));
    expect_send(&env, responses[0]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                NULL, NULL);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));

    avs_coap_observe_id_t observe_id = {
        .token = requests[0]->msg.token
    };
    test_payload_writer_args_t test_payload = {
        .payload = NOTIFY_PAYLOAD,
        .payload_size = sizeof(NOTIFY_PAYLOAD) - 1
    };

    avs_unit_mocksock_output_fail(env.mocksock, avs_errno(AVS_ECONNREFUSED));

    ASSERT_FAIL(avs_coap_notify_async(
            env.coap_ctx, NULL, observe_id, &responses[1]->response_header,
            AVS_COAP_NOTIFY_PREFER_CONFIRMABLE, test_payload_writer,
            &test_payload, test_observe_delivery_handler, &env));

    // should be canceled by cleanup
    expect_observe_cancel(&env, MAKE_TOKEN("Obserw"));
#    undef NOTIFY_PAYLOAD
}

#    ifdef WITH_AVS_COAP_BLOCK

AVS_UNIT_TEST(udp_observe, notify_async_block) {
#        define NOTIFY_PAYLOAD DATA_1KB "Notifaj"
    test_env_t env __attribute__((cleanup(test_teardown_late_expects_check))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(100), MAKE_TOKEN("Obserw"), OBSERVE(0),
                 NO_PAYLOAD),

        // request for second block of Notify
        COAP_MSG(CON, GET, ID(101), MAKE_TOKEN("Notifaj"), BLOCK2_REQ(1, 1024)),
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(100), MAKE_TOKEN("Obserw"), OBSERVE(0),
                 NO_PAYLOAD),

        // BLOCK Notify
        COAP_MSG(NON, CONTENT, ID(0), MAKE_TOKEN("Obserw"), OBSERVE(1),
                 BLOCK2_RES(0, 1024, NOTIFY_PAYLOAD)),
        // Note: further blocks should not contain the Observe option
        // see RFC 7959, Figure 12: "Observe Sequence with Block-Wise Response"
        COAP_MSG(ACK, CONTENT, ID(101), MAKE_TOKEN("Notifaj"),
                 BLOCK2_RES(1, 1024, NOTIFY_PAYLOAD)),
    };

    expect_recv(&env, requests[0]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_RECEIVED,
                                requests[0],
                                &(avs_coap_response_header_t) {
                                    .code = responses[0]->response_header.code
                                },
                                NULL);
    expect_observe_start(&env, requests[0]->msg.token);
    expect_send(&env, responses[0]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                NULL, NULL);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));
    avs_coap_observe_id_t observe_id = {
        .token = requests[0]->msg.token
    };
    test_payload_writer_args_t test_payload = {
        .payload = NOTIFY_PAYLOAD,
        .payload_size = sizeof(NOTIFY_PAYLOAD) - 1
    };

    expect_send(&env, responses[1]);

    avs_coap_exchange_id_t id;
    ASSERT_OK(avs_coap_notify_async(
            env.coap_ctx, &id, observe_id, &responses[1]->response_header,
            AVS_COAP_NOTIFY_PREFER_NON_CONFIRMABLE, test_payload_writer,
            &test_payload, NULL, NULL));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_recv(&env, requests[1]);
    expect_send(&env, responses[2]);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
#        undef NOTIFY_PAYLOAD

    // should be canceled by cleanup
    expect_observe_cancel(&env, requests[0]->msg.token);
}

static void free_deref_arg_delivery_handler(avs_coap_ctx_t *ctx,
                                            avs_error_t err,
                                            void *arg) {
    (void) ctx;

    ASSERT_OK(err);
    void **free_me_ptr = (void **) arg;
    free(*free_me_ptr);
    *free_me_ptr = NULL;
}

AVS_UNIT_TEST(udp_observe, notify_async_non_confirmable_block_with_cleanup) {
#        define NOTIFY_PAYLOAD DATA_1KB "Notifaj"
    test_env_t env __attribute__((cleanup(test_teardown_late_expects_check))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(100), MAKE_TOKEN("Obserw"), OBSERVE(0),
                 NO_PAYLOAD),

        // request for second block of Notify
        COAP_MSG(CON, GET, ID(101), MAKE_TOKEN("Notifaj"), BLOCK2_REQ(1, 1024)),
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(100), MAKE_TOKEN("Obserw"), OBSERVE(0),
                 NO_PAYLOAD),

        // BLOCK Notify
        COAP_MSG(NON, CONTENT, ID(0), MAKE_TOKEN("Obserw"), OBSERVE(1),
                 BLOCK2_RES(0, 1024, NOTIFY_PAYLOAD)),
        // Note: further blocks should not contain the Observe option
        // see RFC 7959, Figure 12: "Observe Sequence with Block-Wise Response"
        COAP_MSG(ACK, CONTENT, ID(101), MAKE_TOKEN("Notifaj"),
                 BLOCK2_RES(1, 1024, NOTIFY_PAYLOAD)),
    };

    expect_recv(&env, requests[0]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_RECEIVED,
                                requests[0],
                                &(avs_coap_response_header_t) {
                                    .code = responses[0]->response_header.code
                                },
                                NULL);
    expect_observe_start(&env, MAKE_TOKEN("Obserw"));
    expect_send(&env, responses[0]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                NULL, NULL);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));
    avs_coap_observe_id_t observe_id = {
        .token = requests[0]->msg.token
    };

    test_payload_writer_args_t *test_payload =
            (test_payload_writer_args_t *) malloc(sizeof(*test_payload));
    *test_payload = (test_payload_writer_args_t) {
        .payload = NOTIFY_PAYLOAD,
        .payload_size = sizeof(NOTIFY_PAYLOAD) - 1
    };

    expect_send(&env, responses[1]);

    avs_coap_exchange_id_t id;
    ASSERT_OK(avs_coap_notify_async(
            env.coap_ctx, &id, observe_id, &responses[1]->response_header,
            AVS_COAP_NOTIFY_PREFER_NON_CONFIRMABLE, test_payload_writer,
            test_payload, free_deref_arg_delivery_handler, &test_payload));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    // request for the second notification block should be handled
    expect_recv(&env, requests[1]);
    expect_send(&env, responses[2]);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    // if all went well, free_arg_delivery_handler was called
    ASSERT_NULL(test_payload);

    // should be canceled by cleanup
    expect_observe_cancel(&env, requests[0]->msg.token);
#        undef NOTIFY_PAYLOAD
}

#    endif // WITH_AVS_COAP_BLOCK

// Not specified in RFC 7252 and RFC 7641, but specified in RFC 8613. Another
// request with the same token shouldn't affect already registered observation
// with the same token.
AVS_UNIT_TEST(udp_observe, request_with_the_same_token_as_observe_token) {
#    define NOTIFY_PAYLOAD "Notifaj"
    test_env_t env __attribute__((cleanup(test_teardown_late_expects_check))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(100), MAKE_TOKEN("Obserw"), OBSERVE(0),
                 NO_PAYLOAD),
        COAP_MSG(CON, GET, ID(101), MAKE_TOKEN("Obserw"), NO_PAYLOAD)
    };

    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(100), MAKE_TOKEN("Obserw"), OBSERVE(0),
                 NO_PAYLOAD),
        COAP_MSG(ACK, CONTENT, ID(101), MAKE_TOKEN("Obserw"), NO_PAYLOAD),
        COAP_MSG(NON, CONTENT, ID(0), MAKE_TOKEN("Obserw"), OBSERVE(1),
                 PAYLOAD(NOTIFY_PAYLOAD)),
    };

    // Request with Observe option
    expect_recv(&env, requests[0]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_RECEIVED,
                                requests[0],
                                &(avs_coap_response_header_t) {
                                    .code = responses[0]->response_header.code
                                },
                                NULL);
    expect_observe_start(&env, MAKE_TOKEN("Obserw"));
    expect_send(&env, responses[0]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                NULL, NULL);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));

    // Request without Observe option and the same token
    expect_recv(&env, requests[1]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_RECEIVED,
                                requests[1],
                                &(avs_coap_response_header_t) {
                                    .code = responses[0]->response_header.code
                                },
                                NULL);
    expect_send(&env, responses[1]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                NULL, NULL);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));

    avs_coap_observe_id_t observe_id = {
        .token = requests[0]->msg.token
    };
    test_payload_writer_args_t test_payload = {
        .payload = NOTIFY_PAYLOAD,
        .payload_size = sizeof(NOTIFY_PAYLOAD) - 1
    };

    expect_send(&env, responses[2]);

    avs_coap_exchange_id_t id;
    ASSERT_OK(avs_coap_notify_async(
            env.coap_ctx, &id, observe_id, &responses[1]->response_header,
            AVS_COAP_NOTIFY_PREFER_NON_CONFIRMABLE, test_payload_writer,
            &test_payload, NULL, NULL));
    ASSERT_FALSE(avs_coap_exchange_id_valid(id));

    // should be canceled by cleanup
    expect_observe_cancel(&env, MAKE_TOKEN("Obserw"));
#    undef NOTIFY_PAYLOAD
}

AVS_UNIT_TEST(udp_observe, cancel_confirmable_notification) {
#    define NOTIFY_PAYLOAD "Notifaj"
    test_env_t env __attribute__((cleanup(test_teardown_late_expects_check))) =
            test_setup_default();

    const test_msg_t *requests[] = { COAP_MSG(
            CON, GET, ID(100), MAKE_TOKEN("Obserw"), OBSERVE(0), NO_PAYLOAD) };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(100), MAKE_TOKEN("Obserw"), OBSERVE(0),
                 NO_PAYLOAD),
        COAP_MSG(CON, CONTENT, ID(0), MAKE_TOKEN("Obserw"), OBSERVE(1),
                 PAYLOAD(NOTIFY_PAYLOAD)),
    };

    expect_recv(&env, requests[0]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_RECEIVED,
                                requests[0],
                                &(avs_coap_response_header_t) {
                                    .code = responses[0]->response_header.code
                                },
                                NULL);
    expect_observe_start(&env, MAKE_TOKEN("Obserw"));
    expect_send(&env, responses[0]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                NULL, NULL);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));

    avs_coap_observe_id_t observe_id = {
        .token = requests[0]->msg.token
    };
    test_payload_writer_args_t test_payload = {
        .payload = NOTIFY_PAYLOAD,
        .payload_size = sizeof(NOTIFY_PAYLOAD) - 1
    };

    expect_send(&env, responses[1]);

    avs_coap_exchange_id_t id;
    ASSERT_OK(avs_coap_notify_async(
            env.coap_ctx, &id, observe_id, &responses[1]->response_header,
            AVS_COAP_NOTIFY_PREFER_CONFIRMABLE, test_payload_writer,
            &test_payload, test_observe_delivery_handler, &env));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    ASSERT_NOT_NULL(
            _avs_coap_get_base(env.coap_ctx)->retry_or_request_expired_job);

    expect_observe_delivery(&env,
                            _avs_coap_err(AVS_COAP_ERR_EXCHANGE_CANCELED));
    avs_coap_exchange_cancel(env.coap_ctx, id);

    ASSERT_FALSE(avs_time_monotonic_valid(
            _avs_coap_retry_or_request_expired_job(env.coap_ctx)));

    // should be canceled by cleanup
    expect_observe_cancel(&env, MAKE_TOKEN("Obserw"));
#    undef NOTIFY_PAYLOAD
}

#endif // defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_UDP) &&
       // defined(WITH_AVS_COAP_OBSERVE)
