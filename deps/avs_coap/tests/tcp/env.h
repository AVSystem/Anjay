/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <avsystem/coap/coap.h>

#define AVS_UNIT_ENABLE_SHORT_ASSERTS
#include <avsystem/commons/avs_unit_mocksock.h>
#include <avsystem/commons/avs_unit_test.h>

#include "./utils.h"
#include "tests/mock_clock.h"
#include "tests/utils.h"

#define IN_BUFFER_SIZE 32
#define OUT_BUFFER_SIZE 1024

#define MAX_OPTS_SIZE (IN_BUFFER_SIZE - sizeof(AVS_COAP_PAYLOAD_MARKER))
#define OPTS_BUFFER_SIZE (MAX_OPTS_SIZE + sizeof(AVS_COAP_PAYLOAD_MARKER))

typedef struct {
    avs_net_socket_t *mocksock;

    avs_sched_t *sched;
    avs_time_duration_t timeout;
    avs_shared_buffer_t *inbuf;
    avs_shared_buffer_t *outbuf;
    avs_coap_ctx_t *coap_ctx;
    avs_crypto_prng_ctx_t *prng_ctx;

    // Set if Abort message is expected to be sent and we don't expect Release
    // message.
    bool aborted;
} test_env_t;

typedef struct {
    avs_net_socket_t *mocksock;
    avs_shared_buffer_t *inbuf;
    avs_shared_buffer_t *outbuf;
} test_env_args_t;

static inline void expect_send(test_env_t *env, const test_msg_t *msg) {
    avs_unit_mocksock_expect_output(env->mocksock, msg->data, msg->size);
}

static inline void expect_recv(test_env_t *env, const test_msg_t *msg) {
    avs_unit_mocksock_input(env->mocksock, msg->data, msg->size);
}

static inline void expect_recv_with_limited_size(test_env_t *env,
                                                 const test_msg_t *msg,
                                                 size_t size) {
    size_t size_to_send = AVS_MIN(size, msg->size);
    avs_unit_mocksock_input(env->mocksock, msg->data, size_to_send);
}

static inline void expect_has_buffered_data_check(test_env_t *env,
                                                  bool has_buffered_data) {
    avs_unit_mocksock_expect_get_opt(env->mocksock,
                                     AVS_NET_SOCKET_HAS_BUFFERED_DATA,
                                     (avs_net_socket_opt_value_t) {
                                         .flag = has_buffered_data
                                     });
}

static inline test_env_t test_setup_from_args(const test_env_args_t *args) {
    avs_sched_t *sched = avs_sched_new("test", NULL);
    ASSERT_NOT_NULL(sched);

    avs_time_duration_t timeout = avs_time_duration_from_scalar(5, AVS_TIME_S);

    avs_crypto_prng_ctx_t *prng_ctx = avs_crypto_prng_new(NULL, NULL);
    ASSERT_NOT_NULL(prng_ctx);

    test_env_t env = {
        .mocksock = args->mocksock,
        .sched = sched,
        .inbuf = args->inbuf,
        .outbuf = args->outbuf,
        .timeout = timeout,
#ifdef WITH_AVS_COAP_TCP
        // Workaround for self-sufficiency test. This header should never be
        // included if WITH_AVS_COAP_TCP is not defined. If somehow it'll be,
        // then assertion will fail.
        .coap_ctx = avs_coap_tcp_ctx_create(sched,
                                            args->inbuf,
                                            args->outbuf,
                                            MAX_OPTS_SIZE,
                                            timeout,
                                            prng_ctx),
#endif // WITH_AVS_COAP_TCP
        .prng_ctx = prng_ctx
    };

    ASSERT_NOT_NULL(env.coap_ctx);
    return env;
}

static inline test_env_t test_setup_without_socket(void) {
    avs_shared_buffer_t *inbuf = avs_shared_buffer_new(IN_BUFFER_SIZE);
    ASSERT_NOT_NULL(inbuf);
    avs_shared_buffer_t *outbuf = avs_shared_buffer_new(OUT_BUFFER_SIZE);
    ASSERT_NOT_NULL(outbuf);
    _avs_mock_clock_start(avs_time_monotonic_from_scalar(0, AVS_TIME_S));
    return test_setup_from_args(&(const test_env_args_t) {
        .inbuf = inbuf,
        .outbuf = outbuf
    });
}

static inline test_env_t test_setup_with_external_buffers_without_mock_clock(
        avs_shared_buffer_t *inbuf, avs_shared_buffer_t *outbuf) {
    reset_token_generator();

    avs_net_socket_t *socket = NULL;
    avs_unit_mocksock_create(&socket);
    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            socket, avs_time_duration_from_scalar(0, AVS_TIME_S));
    ASSERT_NOT_NULL(socket);

    avs_unit_mocksock_expect_connect(socket, NULL, NULL);
    avs_net_socket_connect(socket, NULL, NULL);

    const test_msg_t *csm = COAP_MSG(CSM,
                                     TOKEN(nth_token(0)),
#ifdef WITH_AVS_COAP_BLOCK
                                     BLOCK_WISE_TRANSFER_CAPABLE,
#endif // WITH_AVS_COAP_BLOCK
                                     MAX_MESSAGE_SIZE(SIZE_MAX));
    avs_unit_mocksock_expect_output(socket, csm->data, csm->size);

    const test_msg_t *peer_csm = COAP_MSG(CSM);
    avs_unit_mocksock_input(socket, peer_csm->data, peer_csm->size);

    test_env_t env = test_setup_from_args(&(const test_env_args_t) {
        .mocksock = socket,
        .inbuf = inbuf,
        .outbuf = outbuf
    });

    ASSERT_NOT_NULL(env.coap_ctx);
    ASSERT_OK(avs_coap_ctx_set_socket(env.coap_ctx, socket));
    return env;
}

static inline test_env_t
test_setup_with_external_buffers(avs_shared_buffer_t *inbuf,
                                 avs_shared_buffer_t *outbuf) {
    test_env_t env =
            test_setup_with_external_buffers_without_mock_clock(inbuf, outbuf);
    _avs_mock_clock_start(avs_time_monotonic_from_scalar(0, AVS_TIME_S));
    return env;
}

static inline test_env_t
test_setup_with_custom_sized_buffers(size_t inbuf_size, size_t outbuf_size) {
    avs_shared_buffer_t *inbuf = avs_shared_buffer_new(inbuf_size);
    ASSERT_NOT_NULL(inbuf);
    avs_shared_buffer_t *outbuf = avs_shared_buffer_new(outbuf_size);
    ASSERT_NOT_NULL(outbuf);
    return test_setup_with_external_buffers(inbuf, outbuf);
}

static inline test_env_t test_setup(void) {
    avs_shared_buffer_t *inbuf = avs_shared_buffer_new(IN_BUFFER_SIZE);
    ASSERT_NOT_NULL(inbuf);
    avs_shared_buffer_t *outbuf = avs_shared_buffer_new(OUT_BUFFER_SIZE);
    ASSERT_NOT_NULL(outbuf);
    return test_setup_with_external_buffers(inbuf, outbuf);
}

static inline void test_teardown_impl(test_env_t *env) {
    if (!env->aborted && env->mocksock) {
        expect_send(env, COAP_MSG(RELEASE, TOKEN(current_token())));
    }

    avs_coap_ctx_cleanup(&env->coap_ctx);
    if (env->mocksock) {
        avs_unit_mocksock_assert_expects_met(env->mocksock);
    }
    avs_net_socket_cleanup(&env->mocksock);
    avs_crypto_prng_free(&env->prng_ctx);
}

static inline void test_teardown_without_freeing_coap_ctx(test_env_t *env) {
    if (env->mocksock) {
        avs_unit_mocksock_assert_expects_met(env->mocksock);
    }
    avs_net_socket_cleanup(&env->mocksock);
    avs_sched_cleanup(&env->sched);
    avs_free(env->inbuf);
    avs_free(env->outbuf);
    avs_crypto_prng_free(&env->prng_ctx);
    _avs_mock_clock_finish();
}

static inline void
test_teardown_without_freeing_shared_buffers_and_mock_clock(test_env_t *env) {
    test_teardown_impl(env);
    avs_sched_cleanup(&env->sched);
}

static inline void
test_teardown_without_freeing_shared_buffers(test_env_t *env) {
    test_teardown_without_freeing_shared_buffers_and_mock_clock(env);
    _avs_mock_clock_finish();
}

static inline void test_teardown(test_env_t *env) {
    test_teardown_without_freeing_shared_buffers(env);
    avs_free(env->inbuf);
    avs_free(env->outbuf);
}

static inline void test_teardown_without_freeing_scheduler(test_env_t *env) {
    test_teardown_impl(env);
    _avs_mock_clock_finish();
    avs_free(env->inbuf);
    avs_free(env->outbuf);
}
