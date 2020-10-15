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

#ifndef AVS_COAP_SRC_UDP_TEST_UTILS_H
#define AVS_COAP_SRC_UDP_TEST_UTILS_H

#include <avsystem/commons/avs_list.h>
#include <avsystem/commons/avs_memory.h>
#include <avsystem/commons/avs_sched.h>
#include <avsystem/commons/avs_shared_buffer.h>

#define AVS_UNIT_ENABLE_SHORT_ASSERTS
#include <avsystem/commons/avs_unit_mocksock.h>
#include <avsystem/commons/avs_unit_test.h>

#include <avsystem/coap/coap.h>

#include "avs_coap_code_utils.h"
#include "options/avs_coap_options.h"
#include "udp/avs_coap_udp_msg.h"

#include "tests/mock_clock.h"
#include "tests/utils.h"

// Workaround for failing self-sufficiency test
#ifdef WITH_AVS_COAP_UDP

struct coap_msg_args {
    avs_coap_udp_type_t type;
    uint8_t code;
    uint16_t id;
    avs_coap_token_t token;

    const uint16_t *content_format;
    const uint16_t *accept;
    const uint16_t *duplicated_accept;
    const uint32_t *observe;
    avs_coap_etag_t *etag;

#    ifdef WITH_AVS_COAP_BLOCK
    const avs_coap_option_block_t block1;
    const avs_coap_option_block_t block2;
#    endif // WITH_AVS_COAP_BLOCK

    const void *payload;
    size_t payload_size;

    // 15 = arbitrary limit on path segments
    const char *location_path[16];
    const char *uri_path[16];
    const char *uri_query[16];

    const char uri_host[64];
};

static inline void add_string_opts(avs_coap_options_t *opts,
                                   uint16_t opt_num,
                                   const char *const *const strings) {
    for (size_t i = 0; strings[i]; ++i) {
        ASSERT_OK(avs_coap_options_add_string(opts, opt_num, strings[i]));
    }
}

typedef struct {
    avs_coap_udp_msg_t msg;
    avs_coap_request_header_t request_header;
    avs_coap_response_header_t response_header;
    size_t size;
    uint8_t data[];
} test_msg_t;

static inline const test_msg_t *
coap_msg__(uint8_t *buf, size_t buf_size, const struct coap_msg_args *args) {
    char opts_buf[4096];
    avs_coap_options_t opts =
            avs_coap_options_create_empty(opts_buf, sizeof(opts_buf));

    add_string_opts(&opts, AVS_COAP_OPTION_LOCATION_PATH, args->location_path);
    add_string_opts(&opts, AVS_COAP_OPTION_URI_PATH, args->uri_path);
    add_string_opts(&opts, AVS_COAP_OPTION_URI_QUERY, args->uri_query);

    if (strlen(args->uri_host)) {
        ASSERT_OK(avs_coap_options_add_string(&opts, AVS_COAP_OPTION_URI_HOST,
                                              args->uri_host));
    }

#    ifdef WITH_AVS_COAP_BLOCK
    if (args->block1.size > 0) {
        ASSERT_OK(avs_coap_options_add_block(&opts, &args->block1));
    }
    if (args->block2.size > 0) {
        ASSERT_OK(avs_coap_options_add_block(&opts, &args->block2));
    }
#    endif // WITH_AVS_COAP_BLOCK

    if (args->content_format) {
        ASSERT_OK(avs_coap_options_set_content_format(&opts,
                                                      *args->content_format));
    }
    if (args->accept) {
        ASSERT_OK(avs_coap_options_add_u16(&opts, AVS_COAP_OPTION_ACCEPT,
                                           *args->accept));
    }
    if (args->duplicated_accept) {
        ASSERT_OK(avs_coap_options_add_u16(&opts, AVS_COAP_OPTION_ACCEPT,
                                           *args->duplicated_accept));
    }
#    ifdef WITH_AVS_COAP_OBSERVE
    if (args->observe) {
        ASSERT_OK(avs_coap_options_add_observe(&opts, *args->observe));
    }
#    endif // WITH_AVS_COAP_OBSERVE
    if (args->etag) {
        ASSERT_OK(avs_coap_options_add_etag(&opts, args->etag));
    }

    ASSERT_EQ((uintptr_t) buf % AVS_ALIGNOF(size_t), 0);
    test_msg_t *test_msg = (test_msg_t *) buf;

    test_msg->msg = (avs_coap_udp_msg_t) {
        .header = _avs_coap_udp_header_init(args->type, args->token.size,
                                            args->code, args->id),
        .token = args->token,
        // NOTE: this uses a stack-allocated buffer to hold options that gets
        // invalidated at the end of this function
        .options = opts,
        .payload = args->payload,
        .payload_size = args->payload_size
    };

    ASSERT_OK(_avs_coap_udp_msg_serialize(&test_msg->msg, test_msg->data,
                                          buf_size - sizeof(test_msg_t),
                                          &test_msg->size));
    // Adjust options field to point to test_msg and not to the stack-allocated
    // buffer. We could use _avs_coap_udp_msg_parse, but this function is also
    // used to construct invalid messages, which makes parse fail.
    test_msg->msg.options = (avs_coap_options_t) {
        .begin = &test_msg->data[sizeof(avs_coap_udp_header_t)
                                 + args->token.size],
        .size = opts.size,
        .capacity = opts.size
    };

    test_msg->request_header = (avs_coap_request_header_t) {
        .code = test_msg->msg.header.code,
        .options = test_msg->msg.options
    };
    test_msg->response_header = (avs_coap_response_header_t) {
        .code = test_msg->msg.header.code,
        .options = test_msg->msg.options
    };

    return test_msg;
}

/* Convenience macros for use in COAP_MSG */
#    define CON AVS_COAP_UDP_TYPE_CONFIRMABLE
#    define NON AVS_COAP_UDP_TYPE_NON_CONFIRMABLE
#    define ACK AVS_COAP_UDP_TYPE_ACKNOWLEDGEMENT
#    define RST AVS_COAP_UDP_TYPE_RESET

/* Allocates a 64k buffer on the stack, constructs a message inside it and
 * returns the message pointer.
 *
 * @p Type    - one of AVS_COAP_MSG_* constants or CON, NON, ACK, RST.
 * @p Code    - suffix of one of AVS_COAP_CODE_* constants, e.g. GET
 *              or BAD_REQUEST.
 * @p Opts... - additional options, e.g. ETAG(), PATH(), QUERY().
 *
 * Example usage:
 * @code
 * const avs_coap_msg_t *msg = COAP_MSG(CON, GET, ID(0), NO_PAYLOAD);
 * const avs_coap_msg_t *msg = COAP_MSG(ACK, CONTENT, ID(0),
 *                                      BLOCK2(0, 16, "full_payload"));
 * @endcode
 */
#    define COAP_MSG(Type, Code, ... /* Payload, Opts... */)    \
        coap_msg__((uint8_t *) (size_t[(65535 + sizeof(size_t)) \
                                       / sizeof(size_t)]){ 0 }, \
                   65536,                                       \
                   &(struct coap_msg_args) {                    \
                       .type = (Type),                          \
                       .code = CODE__(Code),                    \
                       __VA_ARGS__                              \
                   })

/* Used in COAP_MSG() to define message ID. */
#    define ID(MsgId) .id = (MsgId)

/* Used in COAP_MSG() to specify ETag option value. */
#    define ETAG(Tag)                \
        .etag = &(avs_coap_etag_t) { \
            .size = sizeof(Tag) - 1, \
            .bytes = Tag             \
        }

/* Used in COAP_MSG() to specify a list of Location-Path options. */
#    define LOCATION_PATH(... /* Segments */) .location_path = { __VA_ARGS__ }

/* Used in COAP_MSG() to specify a list of Uri-Query options. */
#    define QUERY(... /* Segments */) .uri_query = { __VA_ARGS__ }

/* Used in COAP_MSG() to specify the Content-Format option even with
 * unsupported value. */
#    define CONTENT_FORMAT_VALUE(Format)        \
        .content_format = (const uint16_t[1]) { \
            (Format)                            \
        }

/* Used in COAP_MSG() to specify the Content-Format option using predefined
 * constants. */
#    define CONTENT_FORMAT(Format) CONTENT_FORMAT_VALUE(FORMAT__(Format))

typedef struct {
    avs_coap_exchange_id_t exchange_id;
    avs_coap_client_request_state_t result;
    bool has_response;
    avs_coap_client_async_response_t response;
    size_t next_response_payload_offset;
} test_response_handler_expected_t;

typedef struct {
    const void *payload;
    size_t expected_payload_offset;
    size_t payload_size;
    avs_coap_ctx_t *coap_ctx;
    avs_coap_exchange_id_t exchange_id;
    bool cancel_exchange;
    size_t messages_until_fail;
} test_payload_writer_args_t;

typedef struct {
    avs_coap_server_request_state_t state;
    avs_coap_server_async_request_t request;
    avs_coap_observe_id_t observe_id;

    const avs_coap_response_header_t *response;
    avs_coap_payload_writer_t *response_writer;
    test_payload_writer_args_t *response_writer_args;

    bool start_observe;
    bool send_request;
} test_request_handler_expected_t;

typedef enum {
    OBSERVE_START,
    OBSERVE_CANCEL,
} test_observe_state_change_t;

typedef struct {
    test_observe_state_change_t state;
    avs_coap_observe_id_t id;
} test_observe_expect_t;

typedef struct {
    enum {
        EXPECT_RESPONSE_HANDLER,
        EXPECT_REQUEST_HANDLER,
        EXPECT_OBSERVE,
        EXPECT_OBSERVE_DELIVERY
    } type;
    union {
        test_response_handler_expected_t response_handler;
        test_request_handler_expected_t request_handler;
        test_observe_expect_t observe;
        avs_error_t observe_delivery;
    } impl;
} test_handler_expected_t;

typedef struct {
    avs_sched_t *sched;
    avs_net_socket_t *mocksock;
    avs_coap_udp_tx_params_t tx_params;
    avs_shared_buffer_t *in_buffer;
    avs_shared_buffer_t *out_buffer;
    AVS_LIST(test_handler_expected_t) expects_list;

    avs_coap_ctx_t *coap_ctx;
    avs_coap_udp_response_cache_t *response_cache;
    avs_crypto_prng_ctx_t *prng_ctx;
} test_env_t;

static inline test_env_t
test_setup_without_socket(const avs_coap_udp_tx_params_t *tx_params,
                          size_t in_buffer_size,
                          size_t out_buffer_size,
                          avs_coap_udp_response_cache_t *cache) {
    reset_token_generator();
    avs_shared_buffer_t *in_buf = avs_shared_buffer_new(in_buffer_size);
    avs_shared_buffer_t *out_buf = avs_shared_buffer_new(out_buffer_size);

    avs_sched_t *sched = avs_sched_new("udp_ctx_test", NULL);
    ASSERT_NOT_NULL(sched);

    avs_crypto_prng_ctx_t *prng_ctx = avs_crypto_prng_new(NULL, NULL);
    ASSERT_NOT_NULL(prng_ctx);

    test_env_t env = {
        .sched = sched,
        .mocksock = NULL,
        .tx_params = tx_params ? *tx_params : AVS_COAP_DEFAULT_UDP_TX_PARAMS,
        .in_buffer = in_buf,
        .out_buffer = out_buf,
        .coap_ctx = avs_coap_udp_ctx_create(sched, &env.tx_params, in_buf,
                                            out_buf, cache, prng_ctx),
        .response_cache = cache,
        .prng_ctx = prng_ctx
    };

    ASSERT_NOT_NULL(in_buf);
    ASSERT_NOT_NULL(out_buf);
    ASSERT_NOT_NULL(env.coap_ctx);

    _avs_mock_clock_start(avs_time_monotonic_from_scalar(0, AVS_TIME_S));

    return env;
}

static inline test_env_t test_setup(const avs_coap_udp_tx_params_t *tx_params,
                                    size_t in_buffer_size,
                                    size_t out_buffer_size,
                                    avs_coap_udp_response_cache_t *cache) {
    test_env_t env = test_setup_without_socket(tx_params, in_buffer_size,
                                               out_buffer_size, cache);

    avs_net_socket_t *socket = NULL;
    avs_unit_mocksock_create_datagram(&socket);
    avs_unit_mocksock_enable_inner_mtu_getopt(socket, 1500);
    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            socket, avs_time_duration_from_scalar(30, AVS_TIME_S));
    env.mocksock = socket;

    avs_unit_mocksock_expect_connect(socket, NULL, NULL);
    avs_net_socket_connect(socket, NULL, NULL);

    ASSERT_OK(avs_coap_ctx_set_socket(env.coap_ctx, socket));

    avs_unit_mocksock_enable_remote_host(socket, "7.7.7.7");
    avs_unit_mocksock_enable_remote_port(socket, "997");

    return env;
}

static inline test_env_t test_setup_with_nstart(size_t nstart) {
    avs_coap_udp_tx_params_t tx_params = AVS_COAP_DEFAULT_UDP_TX_PARAMS;
    tx_params.nstart = nstart;
    return test_setup(&tx_params, 4096, 4096, NULL);
}

static inline test_env_t
test_setup_with_max_retransmit(unsigned max_retransmit) {
    avs_coap_udp_tx_params_t tx_params = AVS_COAP_DEFAULT_UDP_TX_PARAMS;
    tx_params.max_retransmit = max_retransmit;
    return test_setup(&tx_params, 4096, 4096, NULL);
}

static inline test_env_t test_setup_default(void) {
    return test_setup_with_nstart(999);
}

static inline test_env_t test_setup_with_cache(size_t size) {
    return test_setup(NULL, 4096, 4096,
                      avs_coap_udp_response_cache_create(size));
}

static inline test_env_t test_setup_deterministic(void) {
    avs_coap_udp_tx_params_t tx_params = AVS_COAP_DEFAULT_UDP_TX_PARAMS;
    tx_params.ack_random_factor = 1.0;
    return test_setup(&tx_params, 4096, 4096, NULL);
}

static inline void test_teardown_no_expects_check(test_env_t *env) {
    avs_coap_ctx_cleanup(&env->coap_ctx);
    avs_sched_cleanup(&env->sched);
    if (env->mocksock) {
        avs_unit_mocksock_assert_expects_met(env->mocksock);
    }
    avs_net_socket_cleanup(&env->mocksock);
    avs_free(env->in_buffer);
    avs_free(env->out_buffer);
    avs_coap_udp_response_cache_release(&env->response_cache);
    avs_crypto_prng_free(&env->prng_ctx);
    _avs_mock_clock_finish();
}

static inline void test_teardown_late_expects_check(test_env_t *env) {
    test_teardown_no_expects_check(env);
    ASSERT_NULL(env->expects_list); // all expected handler calls done?
}

static inline void test_teardown(test_env_t *env) {
    ASSERT_NULL(env->expects_list); // all expected handler calls done?
    test_teardown_no_expects_check(env);
}

static inline void expect_send(test_env_t *env, const test_msg_t *msg) {
    avs_unit_mocksock_expect_output(env->mocksock, msg->data, msg->size);
}

static inline void expect_recv(test_env_t *env, const test_msg_t *msg) {
    avs_unit_mocksock_input(env->mocksock, msg->data, msg->size);
}

static inline void expect_timeout(test_env_t *env) {
    avs_unit_mocksock_input_fail(env->mocksock, avs_errno(AVS_ETIMEDOUT));
}

typedef struct {
    test_env_t *env;
    const avs_coap_exchange_id_t *id;
    avs_coap_client_request_state_t result;
    const test_msg_t *msg;
    size_t next_response_payload_offset;
    size_t expected_payload_offset;
} expect_handler_call_args_t;

static inline void
expect_handler_call_impl(const expect_handler_call_args_t *args) {
    test_handler_expected_t *expect =
            AVS_LIST_NEW_ELEMENT(test_handler_expected_t);

    expect->type = EXPECT_RESPONSE_HANDLER;
    expect->impl.response_handler.exchange_id = *args->id;
    expect->impl.response_handler.result = args->result;
    expect->impl.response_handler.has_response = (args->msg != NULL);
    if (args->msg) {
        assert(args->expected_payload_offset <= args->msg->msg.payload_size);
        expect->impl.response_handler.response =
                (avs_coap_client_async_response_t) {
                    .header = {
                        .code = args->msg->msg.header.code,
                        .options = args->msg->msg.options
                    },
                    .payload = (const char *) args->msg->msg.payload
                               + args->expected_payload_offset,
                    .payload_size = args->msg->msg.payload_size
                                    - args->expected_payload_offset
                };
    }
    expect->impl.response_handler.next_response_payload_offset =
            args->next_response_payload_offset;

    AVS_LIST_APPEND(&args->env->expects_list, expect);
}

#    define expect_handler_call(Env, Id, Result, ... /* Msg */)        \
        expect_handler_call_impl(&(const expect_handler_call_args_t) { \
            .env = (Env),                                              \
            .id = (Id),                                                \
            .result = (Result),                                        \
            .msg = __VA_ARGS__                                         \
        })

static inline void
test_response_handler(avs_coap_ctx_t *ctx,
                      avs_coap_exchange_id_t exchange_id,
                      avs_coap_client_request_state_t result,
                      const avs_coap_client_async_response_t *response,
                      avs_error_t err,
                      void *expects_list_) {
    (void) ctx;
    (void) err;

    AVS_LIST(test_handler_expected_t) *expects_list =
            (AVS_LIST(test_handler_expected_t) *) expects_list_;
    ASSERT_NOT_NULL(expects_list);
    ASSERT_NOT_NULL(*expects_list);
    ASSERT_EQ((*expects_list)->type, EXPECT_RESPONSE_HANDLER);

    const test_response_handler_expected_t *expected =
            &(*expects_list)->impl.response_handler;

    ASSERT_TRUE(avs_coap_exchange_id_equal(exchange_id, expected->exchange_id));
    ASSERT_EQ(result, expected->result);

    if (expected->has_response) {
        const avs_coap_client_async_response_t *actual_res = response;
        const avs_coap_client_async_response_t *expected_res =
                &expected->response;

        ASSERT_EQ(actual_res->header.code, expected_res->header.code);
        ASSERT_EQ(actual_res->header.options.size,
                  expected_res->header.options.size);
        ASSERT_EQ(actual_res->payload_size, expected_res->payload_size);
        ASSERT_EQ_BYTES_SIZED(actual_res->payload, expected_res->payload,
                              actual_res->payload_size);
    } else {
        ASSERT_NULL(response);
    }

    if (expected->next_response_payload_offset) {
        ASSERT_OK(avs_coap_client_set_next_response_payload_offset(
                ctx, exchange_id, expected->next_response_payload_offset));
    }
    AVS_LIST_DELETE(expects_list);
}

static inline void
test_response_abort_handler(avs_coap_ctx_t *ctx,
                            avs_coap_exchange_id_t exchange_id,
                            avs_coap_client_request_state_t result,
                            const avs_coap_client_async_response_t *response,
                            avs_error_t err,
                            void *expects_list) {
    test_response_handler(ctx, exchange_id, result, response, err,
                          expects_list);
    avs_coap_exchange_cancel(ctx, exchange_id);
}

static inline int test_payload_writer(size_t payload_offset,
                                      void *payload_buf,
                                      size_t payload_buf_size,
                                      size_t *out_payload_chunk_size,
                                      void *arg) {
    test_payload_writer_args_t *args = (test_payload_writer_args_t *) arg;

    ASSERT_EQ(payload_offset, args->expected_payload_offset);
    ASSERT_TRUE(payload_offset <= args->payload_size);

    *out_payload_chunk_size =
            AVS_MIN(payload_buf_size, args->payload_size - payload_offset);
    args->expected_payload_offset += *out_payload_chunk_size;
    memcpy(payload_buf, (const uint8_t *) args->payload + payload_offset,
           *out_payload_chunk_size);

    if (args->cancel_exchange) {
        avs_coap_exchange_cancel(args->coap_ctx, args->exchange_id);
    }
    if (args->messages_until_fail && !--args->messages_until_fail) {
        return -1;
    }
    return 0;
}

static inline void
expect_observe_state_change(test_env_t *env,
                            test_observe_state_change_t state,
                            const avs_coap_token_t token) {
    *AVS_LIST_APPEND_NEW(test_handler_expected_t, &env->expects_list) =
            (test_handler_expected_t) {
                .type = EXPECT_OBSERVE,
                .impl = {
                    .observe = {
                        .state = state,
                        .id = {
                            .token = token
                        }
                    }
                }
            };
}

static inline void expect_observe_start(test_env_t *env,
                                        const avs_coap_token_t token) {
    expect_observe_state_change(env, OBSERVE_START, token);
}

static inline void expect_observe_cancel(test_env_t *env,
                                         const avs_coap_token_t token) {
    expect_observe_state_change(env, OBSERVE_CANCEL, token);
}

static inline void
assert_observe_state_change_expected(test_env_t *env,
                                     test_observe_state_change_t state,
                                     avs_coap_observe_id_t id) {
    ASSERT_NOT_NULL(env->expects_list);
    ASSERT_EQ(env->expects_list->type, EXPECT_OBSERVE);
    ASSERT_EQ(env->expects_list->impl.observe.state, state);
    ASSERT_TRUE(avs_coap_token_equal(&env->expects_list->impl.observe.id.token,
                                     &id.token));

    AVS_LIST_DELETE(&env->expects_list);
}

static inline void expect_observe_delivery(test_env_t *env, avs_error_t err) {
    *AVS_LIST_APPEND_NEW(test_handler_expected_t, &env->expects_list) =
            (test_handler_expected_t) {
                .type = EXPECT_OBSERVE_DELIVERY,
                .impl = {
                    .observe_delivery = err
                }
            };
}

static inline void test_observe_delivery_handler(avs_coap_ctx_t *ctx,
                                                 avs_error_t err,
                                                 void *env_) {
    (void) ctx;
    test_env_t *env = (test_env_t *) env_;

    ASSERT_NOT_NULL(env->expects_list);
    ASSERT_EQ(env->expects_list->type, EXPECT_OBSERVE_DELIVERY);
    if (avs_is_ok(env->expects_list->impl.observe_delivery)) {
        ASSERT_OK(err);
    } else {
        ASSERT_EQ(env->expects_list->impl.observe_delivery.category,
                  err.category);
        ASSERT_EQ(env->expects_list->impl.observe_delivery.code, err.code);
    }
    AVS_LIST_DELETE(&env->expects_list);
}

#    ifdef WITH_AVS_COAP_OBSERVE
static void test_on_observe_cancel(avs_coap_observe_id_t id, void *env) {
    assert_observe_state_change_expected((test_env_t *) env, OBSERVE_CANCEL,
                                         id);
}
#    endif // WITH_AVS_COAP_OBSERVE

static inline int
test_handle_request(avs_coap_request_ctx_t *ctx,
                    avs_coap_exchange_id_t request_id,
                    avs_coap_server_request_state_t state,
                    const avs_coap_server_async_request_t *request,
                    const avs_coap_observe_id_t *observe_id,
                    void *arg_) {
    test_env_t *env = (test_env_t *) arg_;

    ASSERT_NOT_NULL(env->expects_list);
    ASSERT_EQ(env->expects_list->type, EXPECT_REQUEST_HANDLER);
    ASSERT_EQ(env->expects_list->impl.request_handler.state, state);

    AVS_LIST(test_handler_expected_t) expected =
            AVS_LIST_DETACH(&env->expects_list);

    if (state == AVS_COAP_SERVER_REQUEST_CLEANUP) {
        ASSERT_NULL(request);
        ASSERT_NULL(observe_id);
    } else {
        ASSERT_NOT_NULL(ctx);
        ASSERT_TRUE(avs_coap_exchange_id_valid(request_id));

        const avs_coap_server_async_request_t *expected_request =
                &expected->impl.request_handler.request;

        ASSERT_EQ(expected_request->header.code, request->header.code);
        ASSERT_EQ(expected_request->header.options.size,
                  request->header.options.size);
        ASSERT_OK(memcmp(expected_request->header.options.begin,
                         request->header.options.begin,
                         expected_request->header.options.size));
        ASSERT_EQ(expected_request->payload_offset, request->payload_offset);
        ASSERT_EQ(expected_request->payload_size, request->payload_size);
        ASSERT_OK(memcmp(expected_request->payload, request->payload,
                         expected_request->payload_size));

#    ifdef WITH_AVS_COAP_OBSERVE
        uint32_t observe_value;
        if (avs_coap_options_get_observe(&expected_request->header.options,
                                         &observe_value)
                        == 0
                // 0 == request observe; 1 == cancel
                && observe_value == 0) {
            ASSERT_NOT_NULL(observe_id);
            ASSERT_TRUE(avs_coap_token_equal(
                    &expected->impl.request_handler.observe_id.token,
                    &observe_id->token));
        } else {
            ASSERT_NULL(observe_id);
        }
#    endif // WITH_AVS_COAP_OBSERVE

        if (expected->impl.request_handler.start_observe) {
#    ifdef WITH_AVS_COAP_OBSERVE
            assert_observe_state_change_expected((test_env_t *) env,
                                                 OBSERVE_START, *observe_id);
            ASSERT_OK(avs_coap_observe_async_start(
                    ctx, *observe_id, test_on_observe_cancel, env));
#    else  // WITH_AVS_COAP_OBSERVE
            ASSERT_TRUE(!"observe test, but observes are disabled");
#    endif // WITH_AVS_COAP_OBSERVE
        }

        if (expected->impl.request_handler.response) {
            ASSERT_OK(avs_coap_server_setup_async_response(
                    ctx, expected->impl.request_handler.response,
                    expected->impl.request_handler.response_writer,
                    (void *) (intptr_t) expected->impl.request_handler
                            .response_writer_args));
        }

        if (expected->impl.request_handler.send_request) {
            avs_coap_request_header_t header = {
                .code = AVS_COAP_CODE_GET
            };
            ASSERT_OK(avs_coap_client_send_async_request(
                    ctx->coap_ctx, NULL, &header, NULL, NULL, NULL, NULL));
        }
    }

    AVS_LIST_DELETE(&expected);
    return 0;
}

static inline void _expect_request_handler_call_impl(
        test_env_t *env,
        avs_coap_server_request_state_t state,
        const test_msg_t *request,
        const avs_coap_response_header_t *response,
        test_payload_writer_args_t *response_writer_args,
        bool send_request) {
    test_handler_expected_t *expected =
            AVS_LIST_APPEND_NEW(test_handler_expected_t, &env->expects_list);

    *expected = (test_handler_expected_t) {
        .type = EXPECT_REQUEST_HANDLER,
        .impl = {
            .request_handler = {
                .state = state
            }
        }
    };

    if (request) {
        avs_coap_option_block_t block1 = {
            .seq_num = 0,
            .size = 0
        };
#    ifdef WITH_AVS_COAP_BLOCK
        avs_coap_options_get_block(&request->msg.options, AVS_COAP_BLOCK1,
                                   &block1);
#    endif // WITH_AVS_COAP_BLOCK
        expected->impl.request_handler.request =
                (avs_coap_server_async_request_t) {
                    .header = request->request_header,
                    .payload_offset = block1.seq_num * block1.size,
                    .payload = request->msg.payload,
                    .payload_size = request->msg.payload_size
                };

        expected->impl.request_handler.observe_id = (avs_coap_observe_id_t) {
            .token = request->msg.token
        };

        expected->impl.request_handler.send_request = send_request;
    }

    if (response) {
        expected->impl.request_handler.response = response;

        if (response_writer_args) {
            expected->impl.request_handler.response_writer =
                    test_payload_writer;
            expected->impl.request_handler.response_writer_args =
                    response_writer_args;
        }

#    ifdef WITH_AVS_COAP_OBSERVE
        uint32_t observe_opt;
        if (request
                && !avs_coap_options_get_observe(&request->msg.options,
                                                 &observe_opt)) {
            expected->impl.request_handler.start_observe = (observe_opt == 0);
        }
#    endif // WITH_AVS_COAP_OBSERVE
    }
}

/**
 * @p env                  Test environment object to use.
 *
 * @p state                Expected value of the "state" argument to the
 *                         request handler.
 *
 * @p request              Request message that is supposed to be passed to
 *                         the request handler.
 *
 * @p response             If not NULL, indicates that
 *                         @ref avs_coap_server_setup_async_response should be
 *                         called from the request_handler call. Response code
 *                         and options are passed through this argument, and
 *                         payload - if any - through @p response_writer_args .
 *
 *                         If options included in @p response contain the
 *                         Observe option, @ref avs_coap_observe_async_start
 *                         will be called before setting up the response.
 *
 * @p response_writer_args If @p response is not NULL - the payload that should
 *                         be passed to the lib for inclusion in configured
 *                         response. Note: @ref test_payload_writer will be
 *                         used to feed that payload to the library.
 *
 *                         MUST live at least as long as @p env .
 *
 * If this function is used, @ref avs_coap_async_handle_incoming_packet
 * MUST be called with @ref avs_coap_async_handle_incoming_packet (as "request
 * handler") and @p env (as "request handler arg") whenever a request is
 * expected to be handled.
 */
static inline void
expect_request_handler_call(test_env_t *env,
                            avs_coap_server_request_state_t state,
                            const test_msg_t *request,
                            const avs_coap_response_header_t *response,
                            test_payload_writer_args_t *response_writer_args) {
    _expect_request_handler_call_impl(env, state, request, response,
                                      response_writer_args, false);
}

/**
 * Works like @ref expect_request_handler_call but also forces sending a new
 * request from request handler.
 */
static inline void expect_request_handler_call_and_force_sending_request(
        test_env_t *env,
        avs_coap_server_request_state_t state,
        const test_msg_t *request,
        const avs_coap_response_header_t *response,
        test_payload_writer_args_t *response_writer_args) {
    _expect_request_handler_call_impl(env, state, request, response,
                                      response_writer_args, true);
}

static inline int
test_accept_new_request(avs_coap_server_ctx_t *ctx,
                        const avs_coap_request_header_t *request,
                        void *env_) {
    (void) request;

    avs_coap_exchange_id_t id =
            avs_coap_server_accept_async_request(ctx, test_handle_request,
                                                 env_);
    if (!avs_coap_exchange_id_valid(id)) {
        return AVS_COAP_CODE_INTERNAL_SERVER_ERROR;
    }

    test_env_t *env = (test_env_t *) env_;

    test_handler_expected_t *element = AVS_LIST_NTH(env->expects_list, 0);
    if (element && element->impl.request_handler.response_writer_args) {
        element->impl.request_handler.response_writer_args->exchange_id = id;
    }
    return 0;
}

typedef struct {
    const void *data;
    size_t size;
    size_t chunk_size;
} test_streaming_payload_t;

static inline int test_streaming_writer(avs_stream_t *stream, void *payload_) {
    test_streaming_payload_t *payload = (test_streaming_payload_t *) payload_;

    const char *const begin = (const char *) payload->data;
    const char *const end = begin + payload->size;
    const size_t chunk_size =
            payload->chunk_size == 0 ? payload->size : payload->chunk_size;

    for (const char *p = begin; p < end; p += chunk_size) {
        size_t to_write = AVS_MIN(chunk_size, (size_t) (end - p));
        avs_stream_write(stream, p, to_write);
    }
    return 0;
}

#endif // WITH_AVS_COAP_UDP

#endif // AVS_COAP_SRC_UDP_TEST_UTILS_H
