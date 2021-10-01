/*
 * Copyright 2017-2021 AVSystem <avsystem@avsystem.com>
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

#include <avsystem/commons/avs_memory.h>
#include <avsystem/commons/avs_unit_mock_helpers.h>
#include <avsystem/commons/avs_unit_mocksock.h>
#include <avsystem/commons/avs_unit_test.h>

#include "tests/core/coap/utils.h"
#include "tests/utils/coap/socket.h"
#include "tests/utils/mock_clock.h"
#include "tests/utils/utils.h"

#define DIV_CEIL(a, b) (((a) + (b) -1) / (b))

#define ASSERT_ALMOST_EQ(a, b) \
    AVS_UNIT_ASSERT_TRUE(fabs((a) - (b)) < 0.01 /* epsilon */)

typedef struct {
    anjay_unlocked_t *anjay;
    avs_net_socket_t *mocksock[4];
    size_t num_mocksocks;
} dl_test_env_t;

dl_test_env_t ENV;

const avs_coap_udp_tx_params_t DETERMINISTIC_TX_PARAMS = {
    .ack_timeout = { 2, 0 },
    /* disable randomization */
    .ack_random_factor = 1.0,
    .max_retransmit = 4,
    .nstart = 1
};

static avs_error_t
allocate_mocksock(avs_net_socket_t **socket,
                  const avs_net_socket_configuration_t *configuration) {
    (void) configuration;

    AVS_UNIT_ASSERT_TRUE(ENV.num_mocksocks < AVS_ARRAY_SIZE(ENV.mocksock));
    *socket = ENV.mocksock[ENV.num_mocksocks++];

    return AVS_OK;
}

static void setup(void) {
    reset_token_generator();

    memset(&ENV, 0, sizeof(ENV));

    AVS_UNIT_MOCK(avs_net_udp_socket_create) = allocate_mocksock;

    for (size_t i = 0; i < AVS_ARRAY_SIZE(ENV.mocksock); ++i) {
        _anjay_mocksock_create(&ENV.mocksock[i], 1252, 1252);
        avs_unit_mocksock_enable_state_getopt(ENV.mocksock[i]);
    }

    anjay_t *anjay_locked =
            avs_calloc(1,
#ifdef ANJAY_WITH_THREAD_SAFETY
                       offsetof(anjay_t, anjay_unlocked_placeholder) +
#endif // ANJAY_WITH_THREAD_SAFETY
                               sizeof(anjay_unlocked_t));
    AVS_UNIT_ASSERT_NOT_NULL(anjay_locked);
    ENV.anjay =
#ifdef ANJAY_WITH_THREAD_SAFETY
            (anjay_unlocked_t *) &anjay_locked->anjay_unlocked_placeholder
#else  // ANJAY_WITH_THREAD_SAFETY
            anjay_locked
#endif // ANJAY_WITH_THREAD_SAFETY
            ;
#ifdef ANJAY_WITH_THREAD_SAFETY
    AVS_UNIT_ASSERT_SUCCESS(avs_mutex_create(&anjay_locked->mutex));
    AVS_UNIT_ASSERT_SUCCESS(avs_mutex_lock(anjay_locked->mutex));
    ENV.anjay->coap_sched = avs_sched_new("Anjay-test-CoAP", NULL);
#endif // ANJAY_WITH_THREAD_SAFETY
    ENV.anjay->online_transports = ANJAY_TRANSPORT_SET_ALL;
    ENV.anjay->sched = avs_sched_new("Anjay-test", anjay_locked);
    ENV.anjay->udp_tx_params = DETERMINISTIC_TX_PARAMS;
    ENV.anjay->prng_ctx = (anjay_prng_ctx_t) {
        .ctx = avs_crypto_prng_new(NULL, NULL),
        .allocated_by_user = true
    };

    AVS_UNIT_ASSERT_NOT_NULL(ENV.anjay->prng_ctx.ctx);

    _anjay_downloader_init(&ENV.anjay->downloader, ENV.anjay);

    // NOTE: Special initialization value is used to ensure CoAP Message ID
    // starts with 0.
    _anjay_mock_clock_start(
            avs_time_monotonic_from_scalar(4235699843U, AVS_TIME_S));

    enum { ARBITRARY_SIZE = 4096 };
    // used by the downloader internally
    ENV.anjay->out_shared_buffer = avs_shared_buffer_new(ARBITRARY_SIZE);
    AVS_UNIT_ASSERT_NOT_NULL(ENV.anjay->out_shared_buffer);

    ENV.anjay->in_shared_buffer = avs_shared_buffer_new(ARBITRARY_SIZE);
    AVS_UNIT_ASSERT_NOT_NULL(ENV.anjay->in_shared_buffer);
}

static void teardown() {
    _anjay_downloader_cleanup(&ENV.anjay->downloader);
    avs_sched_cleanup(&ENV.anjay->coap_sched);
#ifdef ANJAY_WITH_THREAD_SAFETY
    anjay_t *anjay_locked =
            AVS_CONTAINER_OF(ENV.anjay, anjay_t, anjay_unlocked_placeholder);
    avs_mutex_unlock(anjay_locked->mutex);
#endif // ANJAY_WITH_THREAD_SAFETY
    avs_sched_cleanup(&ENV.anjay->sched);

    for (size_t i = 0; i < AVS_ARRAY_SIZE(ENV.mocksock); ++i) {
        _anjay_mocksock_expect_stats_zero(ENV.mocksock[i]);
        _anjay_socket_cleanup(ENV.anjay, &ENV.mocksock[i]);
    }

    avs_free(ENV.anjay->out_shared_buffer);
    avs_free(ENV.anjay->in_shared_buffer);
    avs_crypto_prng_free(&ENV.anjay->prng_ctx.ctx);

#ifdef ANJAY_WITH_THREAD_SAFETY
    avs_mutex_cleanup(&anjay_locked->mutex);
    avs_free(anjay_locked);
#else  // ANJAY_WITH_THREAD_SAFETY
    avs_free(ENV.anjay);
#endif // ANJAY_WITH_THREAD_SAFETY

    memset(&ENV, 0, sizeof(ENV));

    _anjay_mock_clock_finish();
}

typedef struct {
    char data[1024];
    size_t data_size;
    const anjay_etag_t *etag;
    avs_error_t result;
} on_next_block_args_t;

typedef struct {
    anjay_unlocked_t *anjay;
    AVS_LIST(on_next_block_args_t) on_next_block_calls;
    bool finish_call_expected;
    anjay_download_status_t expected_download_status;
} handler_data_t;

static void expect_next_block(handler_data_t *data,
                              on_next_block_args_t expected_args) {
    on_next_block_args_t *args = AVS_LIST_NEW_ELEMENT(on_next_block_args_t);
    AVS_UNIT_ASSERT_NOT_NULL(args);

    *args = expected_args;
    AVS_UNIT_ASSERT_TRUE(args->data_size <= sizeof(args->data));

    AVS_LIST_APPEND(&data->on_next_block_calls, args);
}

static void expect_download_finished(handler_data_t *data,
                                     anjay_download_status_t expected_status) {
    data->expected_download_status = expected_status;
    data->finish_call_expected = true;
}

static avs_error_t on_next_block(anjay_t *anjay,
                                 const uint8_t *data,
                                 size_t data_size,
                                 const anjay_etag_t *etag,
                                 void *user_data) {
    handler_data_t *hd = (handler_data_t *) user_data;
    AVS_UNIT_ASSERT_NOT_NULL(hd->on_next_block_calls);

    ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);
    AVS_UNIT_ASSERT_TRUE(anjay_unlocked == hd->anjay);
    ANJAY_MUTEX_UNLOCK(anjay);

    on_next_block_args_t *args = AVS_LIST_DETACH(&hd->on_next_block_calls);
    if (etag && etag->size > 0) {
        AVS_UNIT_ASSERT_EQUAL(args->etag->size, etag->size);
        AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(args->etag->value, etag->value,
                                          etag->size);
    } else {
        AVS_UNIT_ASSERT_NULL(args->etag);
    }
    AVS_UNIT_ASSERT_EQUAL(args->data_size, data_size);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(args->data, data, data_size);
    avs_error_t result = args->result;

    AVS_LIST_DELETE(&args);
    return result;
}

static void on_download_finished(anjay_t *anjay,
                                 anjay_download_status_t status,
                                 void *user_data) {
    handler_data_t *hd = (handler_data_t *) user_data;

    ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);
    AVS_UNIT_ASSERT_TRUE(anjay_unlocked == hd->anjay);
    ANJAY_MUTEX_UNLOCK(anjay);
    AVS_UNIT_ASSERT_TRUE(hd->finish_call_expected);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(&status, &hd->expected_download_status,
                                      sizeof(status));

    hd->finish_call_expected = false;
}

typedef struct {
    dl_test_env_t *base;
    handler_data_t data;
    anjay_download_config_t cfg;
    avs_net_socket_t *mocksock; // alias for SIMPLE_ENV.base->mocksock[0]
} dl_simple_test_env_t;

dl_simple_test_env_t SIMPLE_ENV;

static void setup_simple_with_etag(const char *url, const anjay_etag_t *etag) {
    memset(&SIMPLE_ENV, 0, sizeof(SIMPLE_ENV));
    setup();
    SIMPLE_ENV.base = &ENV;
    SIMPLE_ENV.data = (handler_data_t) {
        .anjay = SIMPLE_ENV.base->anjay
    };
    SIMPLE_ENV.cfg = (anjay_download_config_t) {
        .url = url,
        .on_next_block = on_next_block,
        .on_download_finished = on_download_finished,
        .user_data = &SIMPLE_ENV.data,
        .etag = etag
    };
    SIMPLE_ENV.mocksock = SIMPLE_ENV.base->mocksock[0];
}

static void setup_simple(const char *url) {
    setup_simple_with_etag(url, NULL);
}

static void teardown_simple() {
    teardown();
    memset(&SIMPLE_ENV, 0, sizeof(SIMPLE_ENV));
}

static int handle_packet(void) {
    AVS_LIST(anjay_socket_entry_t) sock = NULL;
    _anjay_downloader_get_sockets(&SIMPLE_ENV.base->anjay->downloader, &sock);
    if (!sock) {
        return -1;
    }

    AVS_UNIT_ASSERT_EQUAL(1, AVS_LIST_SIZE(sock));
    AVS_UNIT_ASSERT_TRUE(SIMPLE_ENV.mocksock == sock->socket);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_downloader_handle_packet(
            &SIMPLE_ENV.base->anjay->downloader, sock->socket));

    AVS_LIST_CLEAR(&sock);
    return 0;
}

static void perform_simple_download(void) {
    anjay_download_handle_t handle = NULL;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_downloader_download(
            &SIMPLE_ENV.base->anjay->downloader, &handle, &SIMPLE_ENV.cfg));
    AVS_UNIT_ASSERT_NOT_NULL(handle);

    do {
        ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, SIMPLE_ENV.base->anjay);
        while (avs_time_duration_equal(avs_sched_time_to_next(
                                               SIMPLE_ENV.base->anjay->sched),
                                       AVS_TIME_DURATION_ZERO)) {
            avs_sched_run(SIMPLE_ENV.base->anjay->sched);
        }
        ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    } while (!handle_packet());

    avs_unit_mocksock_assert_expects_met(SIMPLE_ENV.mocksock);
}

AVS_UNIT_TEST(downloader, empty_has_no_sockets) {
    setup();

    AVS_LIST(anjay_socket_entry_t) socks = NULL;
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_downloader_get_sockets(&ENV.anjay->downloader, &socks));
    AVS_UNIT_ASSERT_NULL(socks);

    teardown();
}

static void assert_download_not_possible(anjay_downloader_t *dl,
                                         const anjay_download_config_t *cfg) {
    size_t num_downloads = 0;

    AVS_LIST(anjay_socket_entry_t) socks = NULL;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_downloader_get_sockets(dl, &socks));
    num_downloads = AVS_LIST_SIZE(socks);
    AVS_LIST_CLEAR(&socks);

    anjay_download_handle_t handle = NULL;
    avs_error_t err = _anjay_downloader_download(dl, &handle, cfg

    );
    AVS_UNIT_ASSERT_EQUAL(err.category, AVS_ERRNO_CATEGORY);
    AVS_UNIT_ASSERT_EQUAL(err.code, AVS_EINVAL);
    AVS_UNIT_ASSERT_NULL(handle);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_downloader_get_sockets(dl, &socks));
    AVS_UNIT_ASSERT_EQUAL(num_downloads, AVS_LIST_SIZE(socks));
    AVS_LIST_CLEAR(&socks);
}

AVS_UNIT_TEST(downloader, cannot_download_without_handlers) {
    setup_simple("coap://127.0.0.1:5683");

    SIMPLE_ENV.cfg.on_next_block = NULL;
    SIMPLE_ENV.cfg.on_download_finished = NULL;
    assert_download_not_possible(&SIMPLE_ENV.base->anjay->downloader,
                                 &SIMPLE_ENV.cfg);

    SIMPLE_ENV.cfg.on_next_block = NULL;
    SIMPLE_ENV.cfg.on_download_finished = on_download_finished;
    assert_download_not_possible(&SIMPLE_ENV.base->anjay->downloader,
                                 &SIMPLE_ENV.cfg);

    SIMPLE_ENV.cfg.on_next_block = on_next_block;
    SIMPLE_ENV.cfg.on_download_finished = NULL;
    assert_download_not_possible(&SIMPLE_ENV.base->anjay->downloader,
                                 &SIMPLE_ENV.cfg);

    teardown_simple();
}

#define DESPAIR                                                      \
    "Despair is when you're debugging a kernel driver and you look " \
    "at a memory dump and you see that a pointer has a value of 7."

AVS_UNIT_TEST(downloader, coap_download_single_block) {
    setup_simple("coap://127.0.0.1:5683");

    // expect packets
    const coap_test_msg_t *req =
            COAP_MSG(CON, GET, ID_TOKEN_RAW(0, nth_token(0)), NO_PAYLOAD);
    const coap_test_msg_t *res =
            COAP_MSG(ACK, CONTENT, ID_TOKEN_RAW(0, nth_token(0)),
                     BLOCK2(0, 128, DESPAIR));

    avs_unit_mocksock_expect_connect(SIMPLE_ENV.mocksock, "127.0.0.1", "5683");
    avs_unit_mocksock_expect_output(SIMPLE_ENV.mocksock, &req->content,
                                    req->length);
    avs_unit_mocksock_input(SIMPLE_ENV.mocksock, &res->content, res->length);
    expect_timeout(SIMPLE_ENV.mocksock);

    // expect handler calls
    expect_next_block(&SIMPLE_ENV.data,
                      (on_next_block_args_t) {
                          .data = DESPAIR,
                          .data_size = sizeof(DESPAIR) - 1,
                          .result = AVS_OK
                      });
    expect_download_finished(&SIMPLE_ENV.data,
                             _anjay_download_status_success());

    perform_simple_download();

    teardown_simple();
}

AVS_UNIT_TEST(downloader, coap_download_multiple_blocks) {
    static const size_t BLOCK_SIZE = 16;

    setup_simple("coap://127.0.0.1:5683");

    // setup expects
    avs_unit_mocksock_expect_connect(SIMPLE_ENV.mocksock, "127.0.0.1", "5683");

    size_t num_blocks = DIV_CEIL(sizeof(DESPAIR) - 1, BLOCK_SIZE);
    for (size_t i = 0; i < num_blocks; ++i) {
        const coap_test_msg_t *req =
                i == 0 ? COAP_MSG(CON, GET, ID_TOKEN_RAW(i, nth_token(i)),
                                  NO_PAYLOAD)
                       : COAP_MSG(CON, GET, ID_TOKEN_RAW(i, nth_token(i)),
                                  BLOCK2(i, BLOCK_SIZE, ""));
        const coap_test_msg_t *res =
                COAP_MSG(ACK, CONTENT, ID_TOKEN_RAW(i, nth_token(i)),
                         BLOCK2(i, BLOCK_SIZE, DESPAIR));

        avs_unit_mocksock_expect_output(SIMPLE_ENV.mocksock, &req->content,
                                        req->length);
        avs_unit_mocksock_input(SIMPLE_ENV.mocksock, &res->content,
                                res->length);

        bool is_last_block = (i + 1) * BLOCK_SIZE >= sizeof(DESPAIR) - 1;
        size_t size = is_last_block ? sizeof(DESPAIR) - 1 - i * BLOCK_SIZE
                                    : BLOCK_SIZE;

        on_next_block_args_t args = {
            .data_size = size,
            .result = AVS_OK
        };
        memcpy(args.data, &DESPAIR[i * BLOCK_SIZE], size);
        expect_next_block(&SIMPLE_ENV.data, args);

        if (is_last_block) {
            expect_timeout(SIMPLE_ENV.mocksock);
            expect_download_finished(&SIMPLE_ENV.data,
                                     _anjay_download_status_success());
        }
    }

    perform_simple_download();

    teardown_simple();
}

AVS_UNIT_TEST(downloader, download_abort_on_cleanup) {
    setup_simple("coap://127.0.0.1:5683");

    avs_unit_mocksock_expect_connect(SIMPLE_ENV.mocksock, "127.0.0.1", "5683");

    anjay_download_handle_t handle = NULL;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_downloader_download(
            &SIMPLE_ENV.base->anjay->downloader, &handle, &SIMPLE_ENV.cfg

            ));
    AVS_UNIT_ASSERT_NOT_NULL(handle);

    expect_download_finished(&SIMPLE_ENV.data,
                             _anjay_download_status_aborted());
    _anjay_downloader_cleanup(&SIMPLE_ENV.base->anjay->downloader);

    teardown_simple();
}

AVS_UNIT_TEST(downloader, download_abort_on_reset_response) {
    setup_simple("coap://127.0.0.1:5683");

    // expect packets
    const coap_test_msg_t *req =
            COAP_MSG(CON, GET, ID_TOKEN_RAW(0, nth_token(0)), NO_PAYLOAD);
    const coap_test_msg_t *res = COAP_MSG(RST, EMPTY, ID(0), NO_PAYLOAD);

    avs_unit_mocksock_expect_connect(SIMPLE_ENV.mocksock, "127.0.0.1", "5683");
    avs_unit_mocksock_expect_output(SIMPLE_ENV.mocksock, &req->content,
                                    req->length);
    avs_unit_mocksock_input(SIMPLE_ENV.mocksock, &res->content, res->length);
    expect_timeout(SIMPLE_ENV.mocksock);

    // expect handler calls
    expect_download_finished(&SIMPLE_ENV.data,
                             _anjay_download_status_failed((avs_error_t) {
                                 .category = AVS_COAP_ERR_CATEGORY,
                                 .code = AVS_COAP_ERR_UDP_RESET_RECEIVED
                             }));

    perform_simple_download();

    teardown_simple();
}

AVS_UNIT_TEST(downloader, unsupported_protocol) {
    setup_simple("gopher://127.0.0.1:5683");

    anjay_download_handle_t handle = NULL;
    avs_error_t err =
            _anjay_downloader_download(&SIMPLE_ENV.base->anjay->downloader,
                                       &handle, &SIMPLE_ENV.cfg);
    AVS_UNIT_ASSERT_EQUAL(err.category, AVS_ERRNO_CATEGORY);
    AVS_UNIT_ASSERT_EQUAL(err.code, AVS_EPROTONOSUPPORT);
    AVS_UNIT_ASSERT_NULL(handle);

    teardown_simple();
}

AVS_UNIT_TEST(downloader, unrelated_socket) {
    setup();

    AVS_UNIT_ASSERT_FAILED(_anjay_downloader_handle_packet(
            &ENV.anjay->downloader, ENV.mocksock[0]));

    teardown();
}

AVS_UNIT_TEST(downloader, coap_download_separate_response) {
    setup_simple("coap://127.0.0.1:5683");

    // expect packets
    const coap_test_msg_t *req =
            COAP_MSG(CON, GET, ID_TOKEN_RAW(0, nth_token(0)), NO_PAYLOAD);
    const coap_test_msg_t *res =
            COAP_MSG(CON, CONTENT, ID_TOKEN_RAW(1, nth_token(0)),
                     BLOCK2(0, 128, DESPAIR));
    const coap_test_msg_t *res_res = COAP_MSG(ACK, EMPTY, ID(1), NO_PAYLOAD);

    avs_unit_mocksock_expect_connect(SIMPLE_ENV.mocksock, "127.0.0.1", "5683");
    avs_unit_mocksock_expect_output(SIMPLE_ENV.mocksock, &req->content,
                                    req->length);
    avs_unit_mocksock_input(SIMPLE_ENV.mocksock, &res->content, res->length);
    avs_unit_mocksock_expect_output(SIMPLE_ENV.mocksock, &res_res->content,
                                    res_res->length);
    expect_timeout(SIMPLE_ENV.mocksock);

    // expect handler calls
    expect_next_block(&SIMPLE_ENV.data,
                      (on_next_block_args_t) {
                          .data = DESPAIR,
                          .data_size = sizeof(DESPAIR) - 1,
                          .result = AVS_OK
                      });
    expect_download_finished(&SIMPLE_ENV.data,
                             _anjay_download_status_success());

    perform_simple_download();

    teardown_simple();
}

AVS_UNIT_TEST(downloader, coap_download_unexpected_packet) {
    setup_simple("coap://127.0.0.1:5683");

    // expect packets
    const coap_test_msg_t *req =
            COAP_MSG(CON, GET, ID_TOKEN_RAW(0, nth_token(0)), NO_PAYLOAD);
    const coap_test_msg_t *unk1 = COAP_MSG(RST, CONTENT, ID(1), NO_PAYLOAD);
    const coap_test_msg_t *unk2 = COAP_MSG(NON, CONTENT, ID(2), NO_PAYLOAD);
    const coap_test_msg_t *res =
            COAP_MSG(ACK, CONTENT, ID_TOKEN_RAW(0, nth_token(0)),
                     BLOCK2(0, 128, DESPAIR));

    avs_unit_mocksock_expect_connect(SIMPLE_ENV.mocksock, "127.0.0.1", "5683");
    avs_unit_mocksock_expect_output(SIMPLE_ENV.mocksock, &req->content,
                                    req->length);
    avs_unit_mocksock_input(SIMPLE_ENV.mocksock, &unk1->content, unk1->length);
    avs_unit_mocksock_input(SIMPLE_ENV.mocksock, &unk2->content, unk2->length);
    avs_unit_mocksock_input(SIMPLE_ENV.mocksock, &res->content, res->length);
    expect_timeout(SIMPLE_ENV.mocksock);

    // expect handler calls
    expect_next_block(&SIMPLE_ENV.data,
                      (on_next_block_args_t) {
                          .data = DESPAIR,
                          .data_size = sizeof(DESPAIR) - 1,
                          .result = AVS_OK
                      });
    expect_download_finished(&SIMPLE_ENV.data,
                             _anjay_download_status_success());

    perform_simple_download();

    teardown_simple();
}

AVS_UNIT_TEST(downloader, coap_download_abort_from_handler) {
    setup_simple("coap://127.0.0.1:5683");

    // expect packets
    const coap_test_msg_t *req =
            COAP_MSG(CON, GET, ID_TOKEN_RAW(0, nth_token(0)), NO_PAYLOAD);
    const coap_test_msg_t *res =
            COAP_MSG(ACK, CONTENT, ID_TOKEN_RAW(0, nth_token(0)),
                     BLOCK2(0, 128, DESPAIR));

    avs_unit_mocksock_expect_connect(SIMPLE_ENV.mocksock, "127.0.0.1", "5683");
    avs_unit_mocksock_expect_output(SIMPLE_ENV.mocksock, &req->content,
                                    req->length);
    avs_unit_mocksock_input(SIMPLE_ENV.mocksock, &res->content, res->length);
    expect_timeout(SIMPLE_ENV.mocksock);

    // expect handler calls
    expect_next_block(&SIMPLE_ENV.data,
                      (on_next_block_args_t) {
                          .data = DESPAIR,
                          .data_size = sizeof(DESPAIR) - 1,
                          .result = avs_errno(AVS_EINTR) // request abort
                      });
    expect_download_finished(&SIMPLE_ENV.data,
                             _anjay_download_status_failed(
                                     avs_errno(AVS_EINTR)));

    perform_simple_download();

    teardown_simple();
}

AVS_UNIT_TEST(downloader, coap_download_expired) {
    setup_simple("coap://127.0.0.1:5683");

    // expect packets
    const coap_test_msg_t *req1 =
            COAP_MSG(CON, GET, ID_TOKEN_RAW(0, nth_token(0)), NO_PAYLOAD);
    const coap_test_msg_t *res1 =
            COAP_MSG(ACK, CONTENT, ID_TOKEN_RAW(0, nth_token(0)), ETAG("tag"),
                     BLOCK2(0, 64, DESPAIR));

    const coap_test_msg_t *req2 =
            COAP_MSG(CON, GET, ID_TOKEN_RAW(1, nth_token(1)),
                     BLOCK2(1, 64, ""));
    const coap_test_msg_t *res2 =
            COAP_MSG(ACK, CONTENT, ID_TOKEN_RAW(1, nth_token(1)), ETAG("nje"),
                     BLOCK2(1, 64, DESPAIR));

    avs_unit_mocksock_expect_connect(SIMPLE_ENV.mocksock, "127.0.0.1", "5683");
    avs_unit_mocksock_expect_output(SIMPLE_ENV.mocksock, &req1->content,
                                    req1->length);
    avs_unit_mocksock_input(SIMPLE_ENV.mocksock, &res1->content, res1->length);
    avs_unit_mocksock_expect_output(SIMPLE_ENV.mocksock, &req2->content,
                                    req2->length);
    avs_unit_mocksock_input(SIMPLE_ENV.mocksock, &res2->content, res2->length);
    expect_timeout(SIMPLE_ENV.mocksock);

    static const avs_coap_etag_t etag = {
        .size = 3,
        .bytes = "tag"
    };
    // expect handler calls
    expect_next_block(&SIMPLE_ENV.data,
                      (on_next_block_args_t) {
                          .data = DESPAIR,
                          .data_size = 64,
                          .etag = (const anjay_etag_t *) &etag,
                          .result = AVS_OK // request abort
                      });
    expect_download_finished(&SIMPLE_ENV.data,
                             _anjay_download_status_expired());

    perform_simple_download();

    teardown_simple();
}

AVS_UNIT_TEST(downloader, buffer_too_small_to_download) {
    setup_simple("coap://127.0.0.1:5683");

    size_t new_capacity = 3;
    memcpy((void *) (intptr_t) &SIMPLE_ENV.base->anjay->out_shared_buffer
                   ->capacity,
           &new_capacity, sizeof(new_capacity));
    avs_unit_mocksock_expect_connect(SIMPLE_ENV.mocksock, "127.0.0.1", "5683");

    anjay_download_handle_t handle = NULL;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_downloader_download(
            &SIMPLE_ENV.base->anjay->downloader, &handle, &SIMPLE_ENV.cfg));
    AVS_UNIT_ASSERT_NOT_NULL(handle);

    expect_download_finished(&SIMPLE_ENV.data,
                             _anjay_download_status_failed((avs_error_t) {
                                 .category = AVS_COAP_ERR_CATEGORY,
                                 .code = AVS_COAP_ERR_MESSAGE_TOO_BIG
                             }));
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, SIMPLE_ENV.base->anjay);
    while (avs_time_duration_equal(avs_sched_time_to_next(
                                           SIMPLE_ENV.base->anjay->sched),
                                   AVS_TIME_DURATION_ZERO)) {
        avs_sched_run(SIMPLE_ENV.base->anjay->sched);
    }
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);

    teardown_simple();
}

AVS_UNIT_TEST(downloader, retry) {
    setup_simple("coap://127.0.0.1:5683");

    const coap_test_msg_t *req =
            COAP_MSG(CON, GET, ID_TOKEN_RAW(0, nth_token(0)), NO_PAYLOAD);
    const coap_test_msg_t *res =
            COAP_MSG(ACK, CONTENT, ID_TOKEN_RAW(0, nth_token(0)), ETAG("tag"),
                     BLOCK2(0, 128, DESPAIR));

    avs_unit_mocksock_expect_connect(SIMPLE_ENV.mocksock, "127.0.0.1", "5683");

    anjay_download_handle_t handle = NULL;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_downloader_download(
            &SIMPLE_ENV.base->anjay->downloader, &handle, &SIMPLE_ENV.cfg));
    AVS_UNIT_ASSERT_NOT_NULL(handle);

    // initial request
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, SIMPLE_ENV.base->anjay);
    avs_unit_mocksock_expect_output(SIMPLE_ENV.mocksock, &req->content,
                                    req->length);
    while (avs_time_duration_equal(avs_sched_time_to_next(
                                           SIMPLE_ENV.base->anjay->sched),
                                   AVS_TIME_DURATION_ZERO)) {
        avs_sched_run(SIMPLE_ENV.base->anjay->sched);
    }

    // request retransmissions
    avs_time_duration_t last_time_to_next = AVS_TIME_DURATION_INVALID;
    for (size_t i = 0; i < 4; ++i) {
        // make sure there's a retransmission job scheduled
        avs_time_duration_t time_to_next =
                avs_sched_time_to_next(SIMPLE_ENV.base->anjay->sched);
        AVS_UNIT_ASSERT_TRUE(avs_time_duration_valid(time_to_next));
        _anjay_mock_clock_advance(time_to_next);

        avs_unit_mocksock_expect_output(SIMPLE_ENV.mocksock, &req->content,
                                        req->length);
        avs_sched_run(SIMPLE_ENV.base->anjay->sched);

        // ...and it's roughly exponential backoff
        if (avs_time_duration_valid(last_time_to_next)) {
            double ratio =
                    avs_time_duration_to_fscalar(time_to_next, AVS_TIME_S)
                    / avs_time_duration_to_fscalar(last_time_to_next,
                                                   AVS_TIME_S);
            ASSERT_ALMOST_EQ(ratio, 2.0);
        }
        last_time_to_next = time_to_next;
    }
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);

    // handle response
    avs_unit_mocksock_input(SIMPLE_ENV.mocksock, &res->content, res->length);
    expect_timeout(SIMPLE_ENV.mocksock);

    static const avs_coap_etag_t etag = {
        .size = 3,
        .bytes = "tag"
    };
    expect_next_block(&SIMPLE_ENV.data,
                      (on_next_block_args_t) {
                          .data = DESPAIR,
                          .data_size = sizeof(DESPAIR) - 1,
                          .etag = (const anjay_etag_t *) &etag,
                          .result = AVS_OK // request abort
                      });
    expect_download_finished(&SIMPLE_ENV.data,
                             _anjay_download_status_success());

    handle_packet();

    // TODO: remove after T2217.
    // CoAP context cleanup. It's a side effect of a hack in
    // coap.c:cleanup_coap_transfer().
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, SIMPLE_ENV.base->anjay);
    avs_sched_run(SIMPLE_ENV.base->anjay->sched);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);

    // retransmission job should be canceled
    AVS_UNIT_ASSERT_FALSE(avs_time_duration_valid(
            avs_sched_time_to_next(SIMPLE_ENV.base->anjay->sched)));

    avs_unit_mocksock_assert_expects_met(SIMPLE_ENV.mocksock);

    teardown_simple();
}

AVS_UNIT_TEST(downloader, missing_separate_response) {
    setup_simple("coap://127.0.0.1:5683");

    const coap_test_msg_t *req =
            COAP_MSG(CON, GET, ID_TOKEN_RAW(0, nth_token(0)), NO_PAYLOAD);
    const coap_test_msg_t *req_ack = COAP_MSG(ACK, EMPTY, ID(0), NO_PAYLOAD);

    avs_unit_mocksock_expect_connect(SIMPLE_ENV.mocksock, "127.0.0.1", "5683");

    anjay_download_handle_t handle = NULL;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_downloader_download(
            &SIMPLE_ENV.base->anjay->downloader, &handle, &SIMPLE_ENV.cfg));
    AVS_UNIT_ASSERT_NOT_NULL(handle);

    // initial request
    avs_unit_mocksock_expect_output(SIMPLE_ENV.mocksock, &req->content,
                                    req->length);
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, SIMPLE_ENV.base->anjay);
    while (avs_time_duration_equal(avs_sched_time_to_next(
                                           SIMPLE_ENV.base->anjay->sched),
                                   AVS_TIME_DURATION_ZERO)) {
        avs_sched_run(SIMPLE_ENV.base->anjay->sched);
    }
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);

    // retransmission job should be scheduled
    avs_time_duration_t time_to_next =
            avs_sched_time_to_next(SIMPLE_ENV.base->anjay->sched);
    AVS_UNIT_ASSERT_TRUE(avs_time_duration_to_fscalar(time_to_next, AVS_TIME_S)
                         < 5.0);

    // separate ACK
    avs_unit_mocksock_input(SIMPLE_ENV.mocksock, &req_ack->content,
                            req_ack->length);
    expect_timeout(SIMPLE_ENV.mocksock);
    AVS_UNIT_ASSERT_SUCCESS(handle_packet());

    time_to_next = avs_sched_time_to_next(SIMPLE_ENV.base->anjay->sched);
    AVS_UNIT_ASSERT_TRUE(avs_time_duration_valid(time_to_next));

    // no separate response should abort the transfer after
    // EXCHANGE_LIFETIME
    expect_download_finished(&SIMPLE_ENV.data,
                             _anjay_download_status_failed((avs_error_t) {
                                 .category = AVS_COAP_ERR_CATEGORY,
                                 .code = AVS_COAP_ERR_TIMEOUT
                             }));

    // abort job should be scheduled to run after EXCHANGE_LIFETIME
    _anjay_mock_clock_advance(
            avs_coap_udp_exchange_lifetime(&DETERMINISTIC_TX_PARAMS));
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, SIMPLE_ENV.base->anjay);
    avs_sched_run(SIMPLE_ENV.base->anjay->sched);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);

    avs_unit_mocksock_assert_expects_met(SIMPLE_ENV.mocksock);

    teardown_simple();
}

static size_t num_downloads_in_progress(void) {
    AVS_LIST(anjay_socket_entry_t) sock = NULL;
    _anjay_downloader_get_sockets(&SIMPLE_ENV.base->anjay->downloader, &sock);
    size_t result = AVS_LIST_SIZE(sock);
    AVS_LIST_CLEAR(&sock);
    return result;
}

AVS_UNIT_TEST(downloader, abort) {
    setup_simple("coap://127.0.0.1:5683");

    avs_unit_mocksock_expect_connect(SIMPLE_ENV.mocksock, "127.0.0.1", "5683");

    anjay_download_handle_t handle = NULL;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_downloader_download(
            &SIMPLE_ENV.base->anjay->downloader, &handle, &SIMPLE_ENV.cfg));
    AVS_UNIT_ASSERT_NOT_NULL(handle);

    // start_download_job is scheduled
    AVS_UNIT_ASSERT_TRUE(avs_time_duration_valid(
            avs_sched_time_to_next(SIMPLE_ENV.base->anjay->sched)));
    AVS_UNIT_ASSERT_EQUAL(1, num_downloads_in_progress());

    expect_download_finished(&SIMPLE_ENV.data,
                             _anjay_download_status_aborted());
    _anjay_downloader_abort(&SIMPLE_ENV.base->anjay->downloader, handle);

    // TODO: remove after T2217.
    // CoAP context cleanup. It's a side effect of a hack in
    // coap.c:cleanup_coap_transfer().
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, SIMPLE_ENV.base->anjay);
    avs_sched_run(SIMPLE_ENV.base->anjay->sched);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);

    // start_download_job is canceled
    AVS_UNIT_ASSERT_FALSE(avs_time_duration_valid(
            avs_sched_time_to_next(SIMPLE_ENV.base->anjay->sched)));
    AVS_UNIT_ASSERT_EQUAL(0, num_downloads_in_progress());

    teardown_simple();
}

AVS_UNIT_TEST(downloader, uri_path_query) {
    setup_simple("coap://127.0.0.1:5683/uri/path?query=string&another");

    // expect packets
    const coap_test_msg_t *req =
            COAP_MSG(CON, GET, ID_TOKEN_RAW(0, nth_token(0)),
                     PATH("uri", "path"), QUERY("query=string", "another"),
                     NO_PAYLOAD);
    const coap_test_msg_t *res =
            COAP_MSG(ACK, CONTENT, ID_TOKEN_RAW(0, nth_token(0)),
                     BLOCK2(0, 128, DESPAIR));

    avs_unit_mocksock_expect_connect(SIMPLE_ENV.mocksock, "127.0.0.1", "5683");
    avs_unit_mocksock_expect_output(SIMPLE_ENV.mocksock, &req->content,
                                    req->length);
    avs_unit_mocksock_input(SIMPLE_ENV.mocksock, &res->content, res->length);
    expect_timeout(SIMPLE_ENV.mocksock);

    // expect handler calls
    expect_next_block(&SIMPLE_ENV.data,
                      (on_next_block_args_t) {
                          .data = DESPAIR,
                          .data_size = sizeof(DESPAIR) - 1,
                          .result = AVS_OK
                      });
    expect_download_finished(&SIMPLE_ENV.data,
                             _anjay_download_status_success());

    perform_simple_download();

    teardown_simple();
}

AVS_UNIT_TEST(downloader, in_buffer_size_enforces_smaller_initial_block_size) {
    setup_simple("coap://127.0.0.1:5683");

    // the downloader should realize it cannot hold blocks bigger than 128
    // bytes and request that size
    size_t new_capacity = 256;
    memcpy((void *) (intptr_t) &SIMPLE_ENV.base->anjay->in_shared_buffer
                   ->capacity,
           &new_capacity, sizeof(new_capacity));

    // expect packets
    const coap_test_msg_t *req =
            COAP_MSG(CON, GET, ID_TOKEN_RAW(0, nth_token(0)), NO_PAYLOAD);
    const coap_test_msg_t *res =
            COAP_MSG(ACK, CONTENT, ID_TOKEN_RAW(0, nth_token(0)),
                     BLOCK2(0, 128, DESPAIR));

    avs_unit_mocksock_expect_connect(SIMPLE_ENV.mocksock, "127.0.0.1", "5683");
    avs_unit_mocksock_expect_output(SIMPLE_ENV.mocksock, &req->content,
                                    req->length);
    avs_unit_mocksock_input(SIMPLE_ENV.mocksock, &res->content, res->length);
    expect_timeout(SIMPLE_ENV.mocksock);

    // expect handler calls
    expect_next_block(&SIMPLE_ENV.data,
                      (on_next_block_args_t) {
                          .data = DESPAIR,
                          .data_size = sizeof(DESPAIR) - 1,
                          .result = AVS_OK
                      });
    expect_download_finished(&SIMPLE_ENV.data,
                             _anjay_download_status_success());

    perform_simple_download();

    teardown_simple();
}

AVS_UNIT_TEST(downloader, renegotiation_while_requesting_more_than_available) {
    setup_simple("coap://127.0.0.1:5683");

    // We request as much as we can (i.e. 1024 bytes)
    const coap_test_msg_t *req =
            COAP_MSG(CON, GET, ID_TOKEN_RAW(0, nth_token(0)), NO_PAYLOAD);

    // However, the server responds with 128 bytes only, which triggers
    // block size negotiation logic
    const coap_test_msg_t *res =
            COAP_MSG(ACK, CONTENT, ID_TOKEN_RAW(0, nth_token(0)),
                     BLOCK2(0, 128, DESPAIR));

    avs_unit_mocksock_expect_connect(SIMPLE_ENV.mocksock, "127.0.0.1", "5683");
    avs_unit_mocksock_expect_output(SIMPLE_ENV.mocksock, &req->content,
                                    req->length);
    avs_unit_mocksock_input(SIMPLE_ENV.mocksock, &res->content, res->length);
    expect_timeout(SIMPLE_ENV.mocksock);

    // expect handler calls
    expect_next_block(&SIMPLE_ENV.data,
                      (on_next_block_args_t) {
                          .data = DESPAIR,
                          .data_size = sizeof(DESPAIR) - 1,
                          .result = AVS_OK
                      });
    expect_download_finished(&SIMPLE_ENV.data,
                             _anjay_download_status_success());

    perform_simple_download();

    teardown_simple();
}

AVS_UNIT_TEST(downloader, renegotiation_after_first_packet) {
    setup_simple("coap://127.0.0.1:5683");

    avs_unit_mocksock_expect_connect(SIMPLE_ENV.mocksock, "127.0.0.1", "5683");

    on_next_block_args_t args;
    memset(&args, 0, sizeof(args));

    // We request as much as we can (i.e. 64 bytes due to limit of
    // in_buffer_size)
    size_t new_capacity = 128;
    memcpy((void *) (intptr_t) &SIMPLE_ENV.base->anjay->in_shared_buffer
                   ->capacity,
           &new_capacity, sizeof(new_capacity));

    const coap_test_msg_t *req =
            COAP_MSG(CON, GET, ID_TOKEN_RAW(0, nth_token(0)), NO_PAYLOAD);

    // The server responds with 64 bytes of the first block
    const coap_test_msg_t *res =
            COAP_MSG(ACK, CONTENT, ID_TOKEN_RAW(0, nth_token(0)),
                     BLOCK2(0, 64, DESPAIR));
    avs_unit_mocksock_expect_output(SIMPLE_ENV.mocksock, &req->content,
                                    req->length);
    avs_unit_mocksock_input(SIMPLE_ENV.mocksock, &res->content, res->length);

    memset(args.data, 0, sizeof(args.data));
    assert(strlen(DESPAIR) > 64);
    memcpy(args.data, DESPAIR, 64);
    args.data_size = strlen(args.data);
    expect_next_block(&SIMPLE_ENV.data, args);

    // We then request another block with negotiated 64 bytes
    req = COAP_MSG(CON, GET, ID_TOKEN_RAW(1, nth_token(1)), BLOCK2(1, 64, ""));
    // But the server is weird, and responds with an even smaller block size
    // with a different seq-num that is however valid in terms of offset,
    // i.e. it has seq_num=2 which corresponds to the data past the first 64
    // bytes
    res = COAP_MSG(ACK, CONTENT, ID_TOKEN_RAW(1, nth_token(1)),
                   BLOCK2(2, 32, DESPAIR));
    avs_unit_mocksock_expect_output(SIMPLE_ENV.mocksock, &req->content,
                                    req->length);
    avs_unit_mocksock_input(SIMPLE_ENV.mocksock, &res->content, res->length);

    memset(args.data, 0, sizeof(args.data));
    strncpy(args.data, DESPAIR + 64, 32);
    args.data_size = strlen(args.data);
    expect_next_block(&SIMPLE_ENV.data, args);

    // Last block - no surprises this time.
    req = COAP_MSG(CON, GET, ID_TOKEN_RAW(2, nth_token(2)), BLOCK2(3, 32, ""));
    res = COAP_MSG(ACK, CONTENT, ID_TOKEN_RAW(2, nth_token(2)),
                   BLOCK2(3, 32, DESPAIR));
    avs_unit_mocksock_expect_output(SIMPLE_ENV.mocksock, &req->content,
                                    req->length);
    avs_unit_mocksock_input(SIMPLE_ENV.mocksock, &res->content, res->length);
    expect_timeout(SIMPLE_ENV.mocksock);

    memset(args.data, 0, sizeof(args.data));
    strncpy(args.data, DESPAIR + 64 + 32, 32);
    args.data_size = strlen(args.data);
    expect_next_block(&SIMPLE_ENV.data, args);

    expect_download_finished(&SIMPLE_ENV.data,
                             _anjay_download_status_success());

    perform_simple_download();

    teardown_simple();
}

AVS_UNIT_TEST(downloader, resumption_at_some_offset) {
    for (size_t offset = 0; offset < sizeof(DESPAIR); ++offset) {
        setup_simple("coap://127.0.0.1:5683");

        avs_unit_mocksock_expect_connect(SIMPLE_ENV.mocksock, "127.0.0.1",
                                         "5683");

        on_next_block_args_t args;
        memset(&args, 0, sizeof(args));

        size_t new_capacity = 64;
        memcpy((void *) (intptr_t) &SIMPLE_ENV.base->anjay->in_shared_buffer
                       ->capacity,
               &new_capacity, sizeof(new_capacity));

        enum { BLOCK_SIZE = 32 };

        size_t current_offset = offset;
        size_t msg_id = 0;
        while (sizeof(DESPAIR) - current_offset > 0) {
            size_t seq_num = current_offset / BLOCK_SIZE;
            const coap_test_msg_t *req =
                    seq_num == 0
                            ? COAP_MSG(CON, GET,
                                       ID_TOKEN_RAW(msg_id, nth_token(msg_id)),
                                       NO_PAYLOAD)
                            : COAP_MSG(CON, GET,
                                       ID_TOKEN_RAW(msg_id, nth_token(msg_id)),
                                       BLOCK2(seq_num, BLOCK_SIZE, ""));
            const coap_test_msg_t *res =
                    COAP_MSG(ACK, CONTENT,
                             ID_TOKEN_RAW(msg_id, nth_token(msg_id)),
                             BLOCK2(seq_num, BLOCK_SIZE, DESPAIR));
            avs_unit_mocksock_expect_output(SIMPLE_ENV.mocksock, &req->content,
                                            req->length);
            avs_unit_mocksock_input(SIMPLE_ENV.mocksock, &res->content,
                                    res->length);

            // Copy contents from the current_offset till the end of the
            // enclosing block.
            const size_t bytes_till_block_end =
                    AVS_MIN((seq_num + 1) * BLOCK_SIZE - current_offset,
                            sizeof(DESPAIR) - current_offset);

            memset(args.data, 0, sizeof(args.data));
            // User handler gets the data from a specified offset, even if
            // it is pointing at the middle of the block that has to be
            // received for a given offset.
            memcpy(args.data, DESPAIR + current_offset, bytes_till_block_end);
            // See BLOCK2 macro - it ignores terminating '\0', so strlen()
            // must be used to compute actual data length.
            args.data_size = strlen(args.data);
            expect_next_block(&SIMPLE_ENV.data, args);

            current_offset += bytes_till_block_end;
            ++msg_id;
        }
        expect_download_finished(&SIMPLE_ENV.data,
                                 _anjay_download_status_success());

        SIMPLE_ENV.cfg.start_offset = offset;
        anjay_download_handle_t handle = NULL;
        AVS_UNIT_ASSERT_SUCCESS(_anjay_downloader_download(
                &SIMPLE_ENV.base->anjay->downloader, &handle, &SIMPLE_ENV.cfg));
        AVS_UNIT_ASSERT_NOT_NULL(handle);

        expect_timeout(SIMPLE_ENV.mocksock);

        do {
            ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked,
                                            SIMPLE_ENV.base->anjay);
            while (avs_time_duration_equal(
                    avs_sched_time_to_next(SIMPLE_ENV.base->anjay->sched),
                    AVS_TIME_DURATION_ZERO)) {
                avs_sched_run(SIMPLE_ENV.base->anjay->sched);
            }
            ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
        } while (!handle_packet());

        avs_unit_mocksock_assert_expects_met(SIMPLE_ENV.mocksock);

        teardown_simple();
    }
}

AVS_UNIT_TEST(downloader, resumption_without_etag_and_block_estimation) {
    setup_simple("coap://127.0.0.1:5683");

    avs_unit_mocksock_expect_connect(SIMPLE_ENV.mocksock, "127.0.0.1", "5683");

    on_next_block_args_t args;
    memset(&args, 0, sizeof(args));

    size_t new_capacity = 64 + // max 64B block size
                          12 + // CoAP header
                          6 +  // max size of BLOCK2 option
                          9;   // ETag option
    memcpy((void *) (intptr_t) &SIMPLE_ENV.base->anjay->in_shared_buffer
                   ->capacity,
           &new_capacity, sizeof(new_capacity));

    SIMPLE_ENV.cfg.start_offset = 64;
    const coap_test_msg_t *req =
            COAP_MSG(CON, GET, ID_TOKEN_RAW(0, nth_token(0)),
                     BLOCK2(1, 64, ""));

    avs_unit_mocksock_expect_output(SIMPLE_ENV.mocksock, &req->content,
                                    req->length);

    anjay_download_handle_t handle = NULL;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_downloader_download(
            &SIMPLE_ENV.base->anjay->downloader, &handle, &SIMPLE_ENV.cfg));
    AVS_UNIT_ASSERT_NOT_NULL(handle);

    // We only care about verifying initial BLOCK2 size.
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, SIMPLE_ENV.base->anjay);
    while (avs_time_duration_equal(avs_sched_time_to_next(
                                           SIMPLE_ENV.base->anjay->sched),
                                   AVS_TIME_DURATION_ZERO)) {
        avs_sched_run(SIMPLE_ENV.base->anjay->sched);
    }
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);

    expect_download_finished(&SIMPLE_ENV.data,
                             _anjay_download_status_aborted());
    _anjay_downloader_abort(&SIMPLE_ENV.base->anjay->downloader, handle);
    teardown_simple();
}

AVS_UNIT_TEST(downloader, resumption_with_etag_and_block_estimation) {
#define DL_ETAG "AAAABBBB"
    anjay_etag_t *etag = anjay_etag_new(sizeof(DL_ETAG) - 1);
    memcpy(etag->value, DL_ETAG, sizeof(DL_ETAG) - 1);

    setup_simple_with_etag("coap://127.0.0.1:5683", etag);

    avs_unit_mocksock_expect_connect(SIMPLE_ENV.mocksock, "127.0.0.1", "5683");

    on_next_block_args_t args;
    memset(&args, 0, sizeof(args));

    size_t new_capacity = 64 + // max 64B block size
                          12 + // CoAP header
                          6;   // max size of BLOCK2 option
    // Intentionally not including ETag in calculations
    memcpy((void *) (intptr_t) &SIMPLE_ENV.base->anjay->in_shared_buffer
                   ->capacity,
           &new_capacity, sizeof(new_capacity));

    SIMPLE_ENV.cfg.start_offset = 96;
    // ETag is not taken into account during initial calculation
    const coap_test_msg_t *req =
            COAP_MSG(CON, GET, ID_TOKEN_RAW(0, nth_token(0)),
                     BLOCK2(1, 64, ""));

    avs_unit_mocksock_expect_output(SIMPLE_ENV.mocksock, &req->content,
                                    req->length);

    anjay_download_handle_t handle = NULL;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_downloader_download(
            &SIMPLE_ENV.base->anjay->downloader, &handle, &SIMPLE_ENV.cfg));
    AVS_UNIT_ASSERT_NOT_NULL(handle);
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, SIMPLE_ENV.base->anjay);
    while (avs_time_duration_equal(avs_sched_time_to_next(
                                           SIMPLE_ENV.base->anjay->sched),
                                   AVS_TIME_DURATION_ZERO)) {
        avs_sched_run(SIMPLE_ENV.base->anjay->sched);
    }
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);

    const coap_test_msg_t *res =
            COAP_MSG(ACK, CONTENT, ID_TOKEN_RAW(0, nth_token(0)),
                     BLOCK2(1, 64, DESPAIR), ETAG(DL_ETAG));

    avs_unit_mocksock_input(SIMPLE_ENV.mocksock, &res->content, res->length);

    // avs_coap will retry with smaller block size
    req = COAP_MSG(CON, GET, ID_TOKEN_RAW(1, nth_token(1)), BLOCK2(3, 32, ""));
    avs_unit_mocksock_expect_output(SIMPLE_ENV.mocksock, &req->content,
                                    req->length);

    expect_timeout(SIMPLE_ENV.mocksock);
    handle_packet();

    // We only care about verifying initial BLOCK2 size.
    expect_download_finished(&SIMPLE_ENV.data,
                             _anjay_download_status_aborted());
    _anjay_downloader_abort(&SIMPLE_ENV.base->anjay->downloader, handle);
    teardown_simple();

    avs_free(etag);
#undef DL_ETAG
}
