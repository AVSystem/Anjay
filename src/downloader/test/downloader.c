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

#include <avsystem/commons/unit/test.h>
#include <avsystem/commons/unit/mock_helpers.h>
#include <anjay_test/coap/socket.h>
#include <anjay_test/coap/stream.h>
#include <anjay_test/mock_clock.h>

#include "../../coap/id_source/auto.h"
#include "../../coap/test/utils.h"

#define DIV_CEIL(a, b) (((a) + (b) - 1) / (b))

#define _ENSURE_0_OR_1_ARGS(a)
#define _ASSERT_ALMOST_EQ(a, b, epsilon, ...) \
    AVS_UNIT_ASSERT_TRUE(fabs((a) - (b)) < (epsilon)) \
    _ENSURE_0_OR_1_ARGS(__VA_ARGS__)

#define ASSERT_ALMOST_EQ(a, ... /* b [, epsilon=0.01] */) \
    _ASSERT_ALMOST_EQ((a), __VA_ARGS__, 0.01)

typedef struct {
    anjay_t anjay;
    avs_net_abstract_socket_t *mocksock[4];
    size_t num_mocksocks;
} dl_test_env_t;

static avs_net_abstract_socket_t *
allocate_mocksock(anjay_t *anjay,
                  avs_net_socket_type_t type,
                  const char *bind_port,
                  const void *config,
                  const anjay_url_t *uri) {
    (void) type; (void) bind_port; (void) config;

    dl_test_env_t *env = AVS_CONTAINER_OF(anjay, dl_test_env_t, anjay);
    AVS_UNIT_ASSERT_TRUE(env->num_mocksocks < AVS_ARRAY_SIZE(env->mocksock));
    avs_net_abstract_socket_t *sock = env->mocksock[env->num_mocksocks++];

    AVS_UNIT_ASSERT_SUCCESS(avs_net_socket_connect(sock, uri->host, uri->port));

    return sock;
}

static void setup(dl_test_env_t *env) {
    *env = (dl_test_env_t){0};

    AVS_UNIT_MOCK(_anjay_create_connected_udp_socket) = allocate_mocksock;

    for (size_t i = 0; i < AVS_ARRAY_SIZE(env->mocksock); ++i) {
        _anjay_mocksock_create(&env->mocksock[i], 1252, 1252);
    }

    env->anjay = (anjay_t) {
        .sched = _anjay_sched_new(&env->anjay),
        .udp_tx_params = ANJAY_COAP_DEFAULT_UDP_TX_PARAMS
    };
    AVS_UNIT_ASSERT_SUCCESS(avs_coap_ctx_create(&env->anjay.coap_ctx, 0));

    // this particular seed ensures generated message IDs start from 0
    coap_id_source_t *id_source = _anjay_coap_id_source_auto_new(4235699843U, 0);
    _anjay_downloader_init(&env->anjay.downloader, &env->anjay, &id_source);

    _anjay_mock_clock_start(avs_time_monotonic_from_scalar(1, AVS_TIME_S));

    enum { ARBITRARY_SIZE = 4096 };
    // used by the downloader internally
    env->anjay.out_buffer = (uint8_t *) malloc(ARBITRARY_SIZE);
    env->anjay.out_buffer_size = ARBITRARY_SIZE;
    AVS_UNIT_ASSERT_NOT_NULL(env->anjay.out_buffer);

    env->anjay.in_buffer = (uint8_t *) malloc(ARBITRARY_SIZE);
    env->anjay.in_buffer_size = ARBITRARY_SIZE;
    AVS_UNIT_ASSERT_NOT_NULL(env->anjay.in_buffer);
}

static void teardown(dl_test_env_t *env) {
    _anjay_mock_clock_finish();

    _anjay_downloader_cleanup(&env->anjay.downloader);
    _anjay_sched_delete(&env->anjay.sched);
    avs_coap_ctx_cleanup(&env->anjay.coap_ctx);

    for (size_t i = 0; i < AVS_ARRAY_SIZE(env->mocksock); ++i) {
        avs_net_socket_cleanup(&env->mocksock[i]);
    }

    free(env->anjay.out_buffer);
    free(env->anjay.in_buffer);

    memset(env, 0, sizeof(*env));
}

typedef struct {
    uint8_t data[1024];
    size_t data_size;
    anjay_etag_t etag;
    int result;
} on_next_block_args_t;

typedef struct {
    anjay_t *anjay;
    AVS_LIST(on_next_block_args_t) on_next_block_calls;
    bool finish_call_expected;
    int expected_download_result;
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
                                     int expected_result) {
    data->expected_download_result = expected_result;
    data->finish_call_expected = true;
}

static int on_next_block(anjay_t *anjay,
                         const uint8_t *data,
                         size_t data_size,
                         const anjay_etag_t *etag,
                         void *user_data) {
    handler_data_t *hd = (handler_data_t *) user_data;
    AVS_UNIT_ASSERT_NOT_NULL(hd->on_next_block_calls);

    AVS_UNIT_ASSERT_TRUE(anjay == hd->anjay);

    on_next_block_args_t *args = AVS_LIST_DETACH(&hd->on_next_block_calls);
    AVS_UNIT_ASSERT_EQUAL(args->etag.size, etag->size);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(args->etag.value, etag->value, etag->size);
    AVS_UNIT_ASSERT_EQUAL(args->data_size, data_size);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(args->data, data, data_size);
    int result = args->result;

    AVS_LIST_DELETE(&args);
    return result;
}

static void on_download_finished(anjay_t *anjay,
                                 int result,
                                 void *user_data) {
    handler_data_t *hd = (handler_data_t *) user_data;

    AVS_UNIT_ASSERT_TRUE(anjay == hd->anjay);
    AVS_UNIT_ASSERT_TRUE(hd->finish_call_expected);
    AVS_UNIT_ASSERT_EQUAL(result, hd->expected_download_result);

    hd->finish_call_expected = false;
}

typedef struct {
    dl_test_env_t base;
    handler_data_t data;
    anjay_download_config_t cfg;
    avs_net_abstract_socket_t *mocksock; // alias for base.mocksock[0]
} dl_simple_test_env_t;

static void setup_simple(dl_simple_test_env_t *env,
                         const char *url) {
    setup(&env->base);
    env->data = (handler_data_t){ .anjay = &env->base.anjay };
    env->cfg = (anjay_download_config_t){
        .url = url,
        .on_next_block = on_next_block,
        .on_download_finished = on_download_finished,
        .user_data = &env->data,
    };
    env->mocksock = env->base.mocksock[0];
}

static void teardown_simple(dl_simple_test_env_t *env) {
    teardown(&env->base);
}

static int handle_packet(dl_simple_test_env_t *env) {
    AVS_LIST(avs_net_abstract_socket_t *const) sock = NULL;
    _anjay_downloader_get_sockets(&env->base.anjay.downloader, &sock);
    if (!sock) {
        return -1;
    }

    AVS_UNIT_ASSERT_EQUAL(1, AVS_LIST_SIZE(sock));
    AVS_UNIT_ASSERT_TRUE(env->mocksock == *sock);

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_downloader_handle_packet(&env->base.anjay.downloader, *sock));

    AVS_LIST_CLEAR(&sock);
    return 0;
}

static void perform_simple_download(dl_simple_test_env_t *env) {
    anjay_download_handle_t handle =
            _anjay_downloader_download(&env->base.anjay.downloader, &env->cfg);
    AVS_UNIT_ASSERT_NOT_NULL(handle);

    do {
        _anjay_sched_run(env->base.anjay.sched);
    } while (!handle_packet(env));

    avs_unit_mocksock_assert_expects_met(env->mocksock);
}

AVS_UNIT_TEST(downloader, empty_has_no_sockets) {
    dl_test_env_t env __attribute__((__cleanup__(teardown)));
    setup(&env);

    AVS_LIST(avs_net_abstract_socket_t *const) socks = NULL;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_downloader_get_sockets(&env.anjay.downloader,
                                                          &socks));
    AVS_UNIT_ASSERT_NULL(socks);
}

static void assert_download_not_possible(anjay_downloader_t *dl,
                                         const anjay_download_config_t *cfg) {
    size_t num_downloads = 0;

    AVS_LIST(avs_net_abstract_socket_t *const) socks = NULL;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_downloader_get_sockets(dl, &socks));
    num_downloads = AVS_LIST_SIZE(socks);
    AVS_LIST_CLEAR(&socks);

    AVS_UNIT_ASSERT_NULL(_anjay_downloader_download(dl, cfg));

    AVS_UNIT_ASSERT_SUCCESS(_anjay_downloader_get_sockets(dl, &socks));
    AVS_UNIT_ASSERT_EQUAL(num_downloads, AVS_LIST_SIZE(socks));
    AVS_LIST_CLEAR(&socks);
}

AVS_UNIT_TEST(downloader, cannot_download_without_handlers) {
    dl_simple_test_env_t env __attribute__((__cleanup__(teardown_simple)));
    setup_simple(&env, "coap://127.0.0.1:5683");

    env.cfg.on_next_block = NULL;
    env.cfg.on_download_finished = NULL;
    assert_download_not_possible(&env.base.anjay.downloader, &env.cfg);

    env.cfg.on_next_block = NULL;
    env.cfg.on_download_finished = on_download_finished;
    assert_download_not_possible(&env.base.anjay.downloader, &env.cfg);

    env.cfg.on_next_block = on_next_block;
    env.cfg.on_download_finished = NULL;
    assert_download_not_possible(&env.base.anjay.downloader, &env.cfg);
}

#define DESPAIR \
    "Despair is when you're debugging a kernel driver and you look " \
    "at a memory dump and you see that a pointer has a value of 7."

AVS_UNIT_TEST(downloader, coap_download_single_block) {
    dl_simple_test_env_t env __attribute__((__cleanup__(teardown_simple)));
    setup_simple(&env, "coap://127.0.0.1:5683");

    // expect packets
    const avs_coap_msg_t *req = COAP_MSG(CON, GET, ID(0), BLOCK2(0, 1024));
    const avs_coap_msg_t *res = COAP_MSG(ACK, CONTENT, ID(0),
                                         BLOCK2(0, 128, DESPAIR));

    avs_unit_mocksock_expect_connect(env.mocksock, "127.0.0.1", "5683");
    avs_unit_mocksock_expect_output(env.mocksock,
                                    &req->content, req->length);
    avs_unit_mocksock_input(env.mocksock, &res->content, res->length);

    // expect handler calls
    expect_next_block(&env.data, (on_next_block_args_t){
                          .data = DESPAIR,
                          .data_size = sizeof(DESPAIR) - 1,
                          .result = 0
                      });
    expect_download_finished(&env.data, 0);

    perform_simple_download(&env);
}

AVS_UNIT_TEST(downloader, coap_download_multiple_blocks) {
    static const size_t BLOCK_SIZE = 16;

    dl_simple_test_env_t env __attribute__((__cleanup__(teardown_simple)));
    setup_simple(&env, "coap://127.0.0.1:5683");

    // setup expects
    avs_unit_mocksock_expect_connect(env.mocksock, "127.0.0.1", "5683");

    size_t num_blocks = DIV_CEIL(sizeof(DESPAIR) - 1, BLOCK_SIZE);
    for (size_t i = 0; i < num_blocks; ++i) {
        const avs_coap_msg_t *req = COAP_MSG(CON, GET, ID(i),
                                             BLOCK2(i, i == 0 ? 1024 : BLOCK_SIZE));
        const avs_coap_msg_t *res = COAP_MSG(ACK, CONTENT, ID(i),
                                             BLOCK2(i, BLOCK_SIZE, DESPAIR));

        avs_unit_mocksock_expect_output(env.mocksock,
                                        &req->content, req->length);
        avs_unit_mocksock_input(env.mocksock, &res->content, res->length);

        bool is_last_block = (i + 1) * BLOCK_SIZE >= sizeof(DESPAIR) - 1;
        size_t size = is_last_block ? sizeof(DESPAIR) - 1 - i * BLOCK_SIZE
                                    : BLOCK_SIZE;

        on_next_block_args_t args = {
            .data_size = size,
            .result = 0
        };
        memcpy(args.data, &DESPAIR[i * BLOCK_SIZE], size);
        expect_next_block(&env.data, args);

        if (is_last_block) {
            expect_download_finished(&env.data, 0);
        }
    }

    perform_simple_download(&env);
}

AVS_UNIT_TEST(downloader, download_abort_on_cleanup) {
    dl_simple_test_env_t env __attribute__((__cleanup__(teardown_simple)));
    setup_simple(&env, "coap://127.0.0.1:5683");

    avs_unit_mocksock_expect_connect(env.mocksock, "127.0.0.1", "5683");

    anjay_download_handle_t handle =
            _anjay_downloader_download(&env.base.anjay.downloader, &env.cfg);
    AVS_UNIT_ASSERT_NOT_NULL(handle);

    expect_download_finished(&env.data, ANJAY_DOWNLOAD_ERR_ABORTED);
    _anjay_downloader_cleanup(&env.base.anjay.downloader);
}

AVS_UNIT_TEST(downloader, download_abort_on_reset_response) {
    dl_simple_test_env_t env __attribute__((__cleanup__(teardown_simple)));
    setup_simple(&env, "coap://127.0.0.1:5683");

    // expect packets
    const avs_coap_msg_t *req = COAP_MSG(CON, GET, ID(0), BLOCK2(0, 1024));
    const avs_coap_msg_t *res = COAP_MSG(RST, EMPTY, ID(0), NO_PAYLOAD);

    avs_unit_mocksock_expect_connect(env.mocksock, "127.0.0.1", "5683");
    avs_unit_mocksock_expect_output(env.mocksock, &req->content, req->length);
    avs_unit_mocksock_input(env.mocksock, &res->content, res->length);

    // expect handler calls
    expect_download_finished(&env.data, ANJAY_DOWNLOAD_ERR_FAILED);

    perform_simple_download(&env);
}

AVS_UNIT_TEST(downloader, unsupported_protocol) {
    dl_simple_test_env_t env __attribute__((__cleanup__(teardown_simple)));
    setup_simple(&env, "gopher://127.0.0.1:5683");

    AVS_UNIT_ASSERT_NULL(_anjay_downloader_download(&env.base.anjay.downloader,
                                                    &env.cfg));
}

AVS_UNIT_TEST(downloader, unrelated_socket) {
    dl_test_env_t env __attribute__((__cleanup__(teardown)));
    setup(&env);

    AVS_UNIT_ASSERT_FAILED(_anjay_downloader_handle_packet(&env.anjay.downloader,
                                                           env.mocksock[0]));
}

AVS_UNIT_TEST(downloader, coap_download_separate_response) {
    dl_simple_test_env_t env __attribute__((__cleanup__(teardown_simple)));
    setup_simple(&env, "coap://127.0.0.1:5683");

    // expect packets
    const avs_coap_msg_t *req = COAP_MSG(CON, GET, ID(0), BLOCK2(0, 1024));
    const avs_coap_msg_t *res = COAP_MSG(CON, CONTENT, ID(1),
                                         BLOCK2(0, 128, DESPAIR));
    const avs_coap_msg_t *res_res = COAP_MSG(ACK, EMPTY, ID(1), NO_PAYLOAD);

    avs_unit_mocksock_expect_connect(env.mocksock, "127.0.0.1", "5683");
    avs_unit_mocksock_expect_output(env.mocksock, &req->content, req->length);
    avs_unit_mocksock_input(env.mocksock, &res->content, res->length);
    avs_unit_mocksock_expect_output(env.mocksock,
                                    &res_res->content, res_res->length);

    // expect handler calls
    expect_next_block(&env.data, (on_next_block_args_t){
                          .data = DESPAIR,
                          .data_size = sizeof(DESPAIR) - 1,
                          .result = 0
                      });
    expect_download_finished(&env.data, 0);

    perform_simple_download(&env);
}

AVS_UNIT_TEST(downloader, coap_download_unexpected_packet) {
    dl_simple_test_env_t env __attribute__((__cleanup__(teardown_simple)));
    setup_simple(&env, "coap://127.0.0.1:5683");

    // expect packets
    const avs_coap_msg_t *req = COAP_MSG(CON, GET, ID(0), BLOCK2(0, 1024));
    const avs_coap_msg_t *unk1 = COAP_MSG(RST, CONTENT, ID(1), NO_PAYLOAD);
    const avs_coap_msg_t *unk2 = COAP_MSG(NON, CONTENT, ID(2), NO_PAYLOAD);
    const avs_coap_msg_t *res = COAP_MSG(ACK, CONTENT, ID(0),
                                         BLOCK2(0, 128, DESPAIR));

    avs_unit_mocksock_expect_connect(env.mocksock, "127.0.0.1", "5683");
    avs_unit_mocksock_expect_output(env.mocksock, &req->content, req->length);
    avs_unit_mocksock_input(env.mocksock, &unk1->content, unk1->length);
    avs_unit_mocksock_input(env.mocksock, &unk2->content, unk2->length);
    avs_unit_mocksock_input(env.mocksock, &res->content, res->length);

    // expect handler calls
    expect_next_block(&env.data, (on_next_block_args_t){
                          .data = DESPAIR,
                          .data_size = sizeof(DESPAIR) - 1,
                          .result = 0
                      });
    expect_download_finished(&env.data, 0);

    perform_simple_download(&env);
}

AVS_UNIT_TEST(downloader, coap_download_abort_from_handler) {
    dl_simple_test_env_t env __attribute__((__cleanup__(teardown_simple)));
    setup_simple(&env, "coap://127.0.0.1:5683");

    // expect packets
    const avs_coap_msg_t *req = COAP_MSG(CON, GET, ID(0), BLOCK2(0, 1024));
    const avs_coap_msg_t *res = COAP_MSG(ACK, CONTENT, ID(0),
                                         BLOCK2(0, 128, DESPAIR));

    avs_unit_mocksock_expect_connect(env.mocksock, "127.0.0.1", "5683");
    avs_unit_mocksock_expect_output(env.mocksock, &req->content, req->length);
    avs_unit_mocksock_input(env.mocksock, &res->content, res->length);

    // expect handler calls
    expect_next_block(&env.data, (on_next_block_args_t){
                          .data = DESPAIR,
                          .data_size = sizeof(DESPAIR) - 1,
                          .result = -1 // request abort
                      });
    expect_download_finished(&env.data, ANJAY_DOWNLOAD_ERR_FAILED);

    perform_simple_download(&env);
}

AVS_UNIT_TEST(downloader, coap_download_expired) {
    dl_simple_test_env_t env __attribute__((__cleanup__(teardown_simple)));
    setup_simple(&env, "coap://127.0.0.1:5683");

    // expect packets
    const avs_coap_msg_t *req1 = COAP_MSG(CON, GET, ID(0), BLOCK2(0, 1024));
    const avs_coap_msg_t *res1 = COAP_MSG(ACK, CONTENT, ID(0), ETAG("tag"),
                                          BLOCK2(0, 64, DESPAIR));

    const avs_coap_msg_t *req2 = COAP_MSG(CON, GET, ID(1), BLOCK2(1, 64));
    const avs_coap_msg_t *res2 = COAP_MSG(ACK, CONTENT, ID(1), ETAG("nje"),
                                          BLOCK2(1, 64, DESPAIR));

    avs_unit_mocksock_expect_connect(env.mocksock, "127.0.0.1", "5683");
    avs_unit_mocksock_expect_output(env.mocksock, &req1->content, req1->length);
    avs_unit_mocksock_input(env.mocksock, &res1->content, res1->length);
    avs_unit_mocksock_expect_output(env.mocksock, &req2->content, req2->length);
    avs_unit_mocksock_input(env.mocksock, &res2->content, res2->length);

    // expect handler calls
    expect_next_block(&env.data, (on_next_block_args_t){
                          .data = DESPAIR,
                          .data_size = 64,
                          .etag = { .size = 3, .value = "tag" },
                          .result = 0 // request abort
                      });
    expect_download_finished(&env.data, ANJAY_DOWNLOAD_ERR_EXPIRED);

    perform_simple_download(&env);
}

AVS_UNIT_TEST(downloader, buffer_too_small_to_download) {
    dl_simple_test_env_t env __attribute__((__cleanup__(teardown_simple)));
    setup_simple(&env, "coap://127.0.0.1:5683");

    env.base.anjay.out_buffer_size = 3;
    avs_unit_mocksock_expect_connect(env.mocksock, "127.0.0.1", "5683");

    AVS_UNIT_ASSERT_NOT_NULL(
            _anjay_downloader_download(&env.base.anjay.downloader, &env.cfg));

    expect_download_finished(&env.data, ANJAY_DOWNLOAD_ERR_FAILED);
    _anjay_sched_run(env.base.anjay.sched);
}

AVS_UNIT_TEST(downloader, retry) {
    dl_simple_test_env_t env __attribute__((__cleanup__(teardown_simple)));
    setup_simple(&env, "coap://127.0.0.1:5683");

    const avs_coap_msg_t *req = COAP_MSG(CON, GET, ID(0), BLOCK2(0, 1024));
    const avs_coap_msg_t *res = COAP_MSG(ACK, CONTENT, ID(0), ETAG("tag"),
                                         BLOCK2(0, 128, DESPAIR));

    avs_unit_mocksock_expect_connect(env.mocksock, "127.0.0.1", "5683");

    anjay_download_handle_t handle =
            _anjay_downloader_download(&env.base.anjay.downloader, &env.cfg);
    AVS_UNIT_ASSERT_NOT_NULL(handle);

    // initial request
    avs_unit_mocksock_expect_output(env.mocksock, &req->content, req->length);
    _anjay_sched_run(env.base.anjay.sched);

    // request retransmissions
    avs_time_duration_t last_time_to_next = AVS_TIME_DURATION_INVALID;
    for (size_t i = 0; i < 4; ++i) {
        // make sure there's a retransmission job scheduled
        avs_time_duration_t time_to_next;
        AVS_UNIT_ASSERT_SUCCESS(
                _anjay_sched_time_to_next(env.base.anjay.sched,
                                          &time_to_next));
        _anjay_mock_clock_advance(time_to_next);

        avs_unit_mocksock_expect_output(env.mocksock,
                                        &req->content, req->length);
        _anjay_sched_run(env.base.anjay.sched);

        // ...and it's roughly exponential backoff
        if (avs_time_duration_valid(last_time_to_next)) {
            double ratio = avs_time_duration_to_fscalar(time_to_next,
                                                        AVS_TIME_S)
                           / avs_time_duration_to_fscalar(last_time_to_next,
                                                          AVS_TIME_S);
            ASSERT_ALMOST_EQ(ratio, 2.0);
        }
        last_time_to_next = time_to_next;
    }

    // handle response
    avs_unit_mocksock_input(env.mocksock, &res->content, res->length);

    expect_next_block(&env.data, (on_next_block_args_t){
                          .data = DESPAIR,
                          .data_size = sizeof(DESPAIR) - 1,
                          .etag = { .size = 3, .value = "tag" },
                          .result = 0 // request abort
                      });
    expect_download_finished(&env.data, 0);

    handle_packet(&env);

    // retransmission job should be canceled
    AVS_UNIT_ASSERT_FAILED(
            _anjay_sched_time_to_next(env.base.anjay.sched, NULL));

    avs_unit_mocksock_assert_expects_met(env.mocksock);
}

AVS_UNIT_TEST(downloader, missing_separate_response) {
    dl_simple_test_env_t env __attribute__((__cleanup__(teardown_simple)));
    setup_simple(&env, "coap://127.0.0.1:5683");

    const avs_coap_msg_t *req = COAP_MSG(CON, GET, ID(0), BLOCK2(0, 1024));
    const avs_coap_msg_t *req_ack = COAP_MSG(ACK, EMPTY, ID(0), NO_PAYLOAD);

    avs_unit_mocksock_expect_connect(env.mocksock, "127.0.0.1", "5683");

    anjay_download_handle_t handle =
            _anjay_downloader_download(&env.base.anjay.downloader, &env.cfg);
    AVS_UNIT_ASSERT_NOT_NULL(handle);

    // initial request
    avs_unit_mocksock_expect_output(env.mocksock, &req->content, req->length);
    _anjay_sched_run(env.base.anjay.sched);

    // retransmission job should be scheduled
    avs_time_duration_t time_to_next;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_sched_time_to_next(env.base.anjay.sched,
                                                      &time_to_next));
    AVS_UNIT_ASSERT_TRUE(avs_time_duration_to_fscalar(time_to_next,
                                                      AVS_TIME_S) < 5.0);

    // separate ACK
    avs_unit_mocksock_input(env.mocksock, &req_ack->content, req_ack->length);
    AVS_UNIT_ASSERT_SUCCESS(handle_packet(&env));

    // abort job should be scheduled to run after EXCHANGE_LIFETIME
    AVS_UNIT_ASSERT_SUCCESS(_anjay_sched_time_to_next(env.base.anjay.sched,
                                                      &time_to_next));

    static const avs_coap_tx_params_t tx_params =
            ANJAY_COAP_DEFAULT_UDP_TX_PARAMS;
    ASSERT_ALMOST_EQ(avs_time_duration_to_fscalar(time_to_next, AVS_TIME_S),
                     avs_time_duration_to_fscalar(
                             avs_coap_exchange_lifetime(&tx_params),
                             AVS_TIME_S));

    // no separate response should abort the transfer after EXCHANGE_LIFETIME
    expect_download_finished(&env.data, ANJAY_DOWNLOAD_ERR_FAILED);

    _anjay_mock_clock_advance(time_to_next);
    _anjay_sched_run(env.base.anjay.sched);

    avs_unit_mocksock_assert_expects_met(env.mocksock);
}

static size_t num_downloads_in_progress(dl_simple_test_env_t *env) {
    AVS_LIST(avs_net_abstract_socket_t *const) sock = NULL;
    _anjay_downloader_get_sockets(&env->base.anjay.downloader, &sock);
    size_t result = AVS_LIST_SIZE(sock);
    AVS_LIST_CLEAR(&sock);
    return result;
}

AVS_UNIT_TEST(downloader, abort) {
    dl_simple_test_env_t env __attribute__((__cleanup__(teardown_simple)));
    setup_simple(&env, "coap://127.0.0.1:5683");

    avs_unit_mocksock_expect_connect(env.mocksock, "127.0.0.1", "5683");

    anjay_download_handle_t handle =
            _anjay_downloader_download(&env.base.anjay.downloader, &env.cfg);
    AVS_UNIT_ASSERT_NOT_NULL(handle);

    // retransmission job scheduled
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_sched_time_to_next(env.base.anjay.sched, NULL));
    AVS_UNIT_ASSERT_EQUAL(1, num_downloads_in_progress(&env));

    expect_download_finished(&env.data, ANJAY_DOWNLOAD_ERR_ABORTED);
    _anjay_downloader_abort(&env.base.anjay.downloader, handle);

    // retransmission job canceled
    AVS_UNIT_ASSERT_FAILED(
            _anjay_sched_time_to_next(env.base.anjay.sched, NULL));
    AVS_UNIT_ASSERT_EQUAL(0, num_downloads_in_progress(&env));
}

AVS_UNIT_TEST(downloader, uri_path_query) {
    dl_simple_test_env_t env __attribute__((__cleanup__(teardown_simple)));
    setup_simple(&env, "coap://127.0.0.1:5683/uri/path?query=string&another");

    // expect packets
    const avs_coap_msg_t *req = COAP_MSG(CON, GET, ID(0),
                                         PATH("uri", "path"),
                                         QUERY("query=string", "another"),
                                         BLOCK2(0, 1024));
    const avs_coap_msg_t *res = COAP_MSG(ACK, CONTENT, ID(0),
                                         BLOCK2(0, 128, DESPAIR));

    avs_unit_mocksock_expect_connect(env.mocksock, "127.0.0.1", "5683");
    avs_unit_mocksock_expect_output(env.mocksock,
                                    &req->content, req->length);
    avs_unit_mocksock_input(env.mocksock, &res->content, res->length);

    // expect handler calls
    expect_next_block(&env.data, (on_next_block_args_t){
                          .data = DESPAIR,
                          .data_size = sizeof(DESPAIR) - 1,
                          .result = 0
                      });
    expect_download_finished(&env.data, 0);

    perform_simple_download(&env);
}

AVS_UNIT_TEST(downloader, in_buffer_size_enforces_smaller_initial_block_size) {
    dl_simple_test_env_t env __attribute__((__cleanup__(teardown_simple)));
    setup_simple(&env, "coap://127.0.0.1:5683");

    // the downloader should realize it cannot hold blocks bigger than 128 bytes
    // and request that size
    env.base.anjay.in_buffer_size = 256;

    // expect packets
    const avs_coap_msg_t *req = COAP_MSG(CON, GET, ID(0), BLOCK2(0, 128));
    const avs_coap_msg_t *res = COAP_MSG(ACK, CONTENT, ID(0),
                                         BLOCK2(0, 128, DESPAIR));

    avs_unit_mocksock_expect_connect(env.mocksock, "127.0.0.1", "5683");
    avs_unit_mocksock_expect_output(env.mocksock,
                                    &req->content, req->length);
    avs_unit_mocksock_input(env.mocksock, &res->content, res->length);

    // expect handler calls
    expect_next_block(&env.data, (on_next_block_args_t){
                          .data = DESPAIR,
                          .data_size = sizeof(DESPAIR) - 1,
                          .result = 0
                      });
    expect_download_finished(&env.data, 0);

    perform_simple_download(&env);
}

AVS_UNIT_TEST(downloader, renegotiation_while_requesting_more_than_available) {
    dl_simple_test_env_t env __attribute__((__cleanup__(teardown_simple)));
    setup_simple(&env, "coap://127.0.0.1:5683");

    // We request as much as we can (i.e. 1024 bytes)
    const avs_coap_msg_t *req = COAP_MSG(CON, GET, ID(0), BLOCK2(0, 1024));

    // However, the server responds with 128 bytes only, which triggers block
    // size negotiation logic
    const avs_coap_msg_t *res =
            COAP_MSG(ACK, CONTENT, ID(0), BLOCK2(0, 128, DESPAIR));

    avs_unit_mocksock_expect_connect(env.mocksock, "127.0.0.1", "5683");
    avs_unit_mocksock_expect_output(env.mocksock,
                                    &req->content, req->length);
    avs_unit_mocksock_input(env.mocksock, &res->content, res->length);

    // expect handler calls
    expect_next_block(&env.data, (on_next_block_args_t) {
                            .data = DESPAIR,
                            .data_size = sizeof(DESPAIR) - 1,
                            .result = 0
                      });
    expect_download_finished(&env.data, 0);

    perform_simple_download(&env);
}

AVS_UNIT_TEST(downloader, renegotiation_after_first_packet) {
    dl_simple_test_env_t env __attribute__((__cleanup__(teardown_simple)));
    setup_simple(&env, "coap://127.0.0.1:5683");

    avs_unit_mocksock_expect_connect(env.mocksock, "127.0.0.1", "5683");

    on_next_block_args_t args;
    memset(&args, 0, sizeof(args));

    // We request as much as we can (i.e. 64 bytes due to limit of in_buffer_size)
    env.base.anjay.in_buffer_size = 128;
    const avs_coap_msg_t *req = COAP_MSG(CON, GET, ID(0), BLOCK2(0, 64));

    // The server responds with 64 bytes of the first block
    const avs_coap_msg_t *res =
            COAP_MSG(ACK, CONTENT, ID(0), BLOCK2(0, 64, DESPAIR));
    avs_unit_mocksock_expect_output(env.mocksock, &req->content, req->length);
    avs_unit_mocksock_input(env.mocksock, &res->content, res->length);

    memset(args.data, 0, sizeof(args.data));
    strncpy(args.data, DESPAIR, 64);
    args.data_size = strlen(args.data);
    expect_next_block(&env.data, args);

    // We then request another block with negotiated 64 bytes
    req = COAP_MSG(CON, GET, ID(1), BLOCK2(1, 64));
    // But the server is weird, and responds with an even smaller block size
    // with a different seq-num that is however valid in terms of offset, i.e.
    // it has seq_num=2 which corresponds to the data past the first 64 bytes
    res = COAP_MSG(ACK, CONTENT, ID(1), BLOCK2(2, 32, DESPAIR));
    avs_unit_mocksock_expect_output(env.mocksock, &req->content, req->length);
    avs_unit_mocksock_input(env.mocksock, &res->content, res->length);

    memset(args.data, 0, sizeof(args.data));
    strncpy(args.data, DESPAIR + 64, 32);
    args.data_size = strlen(args.data);
    expect_next_block(&env.data, args);

    // Last block - no surprises this time.
    req = COAP_MSG(CON, GET, ID(2), BLOCK2(3, 32));
    res = COAP_MSG(ACK, CONTENT, ID(2), BLOCK2(3, 32, DESPAIR));
    avs_unit_mocksock_expect_output(env.mocksock, &req->content, req->length);
    avs_unit_mocksock_input(env.mocksock, &res->content, res->length);

    memset(args.data, 0, sizeof(args.data));
    strncpy(args.data, DESPAIR + 64 + 32, 32);
    args.data_size = strlen(args.data);
    expect_next_block(&env.data, args);

    expect_download_finished(&env.data, 0);

    perform_simple_download(&env);
}

AVS_UNIT_TEST(downloader, resumption_at_some_offset) {
    for (size_t offset = 0; offset < sizeof(DESPAIR); ++offset) {
        dl_simple_test_env_t env __attribute__((__cleanup__(teardown_simple)));
        setup_simple(&env, "coap://127.0.0.1:5683");

        avs_unit_mocksock_expect_connect(env.mocksock, "127.0.0.1", "5683");

        on_next_block_args_t args;
        memset(&args, 0, sizeof(args));

        env.base.anjay.in_buffer_size = 64;
        enum { BLOCK_SIZE = 32 };

        size_t current_offset = offset;
        size_t msg_id = 0;
        while (sizeof(DESPAIR) - current_offset > 0) {
            size_t seq_num = current_offset / BLOCK_SIZE;
            const avs_coap_msg_t *req = COAP_MSG(CON, GET, ID(msg_id),
                                                 BLOCK2(seq_num, BLOCK_SIZE));
            const avs_coap_msg_t *res =
                    COAP_MSG(ACK, CONTENT, ID(msg_id),
                             BLOCK2(seq_num, BLOCK_SIZE, DESPAIR));
            avs_unit_mocksock_expect_output(env.mocksock, &req->content,
                                            req->length);
            avs_unit_mocksock_input(env.mocksock, &res->content, res->length);

            // Copy contents from the current_offset till the end of the
            // enclosing block.
            const size_t bytes_till_block_end =
                    AVS_MIN((seq_num + 1) * BLOCK_SIZE - current_offset,
                              sizeof(DESPAIR) - current_offset);

            memset(args.data, 0, sizeof(args.data));
            // User handler gets the data from a specified offset, even if it is
            // pointing at the middle of the block that has to be received for
            // a given offset.
            memcpy(args.data, DESPAIR + current_offset, bytes_till_block_end);
            // See BLOCK2 macro - it ignores terminating '\0', so strlen() must
            // be used to compute actual data length.
            args.data_size = strlen(args.data);
            expect_next_block(&env.data, args);

            current_offset += bytes_till_block_end;
            ++msg_id;
        }
        expect_download_finished(&env.data, 0);

        env.cfg.start_offset = offset;
        anjay_download_handle_t handle =
                _anjay_downloader_download(&env.base.anjay.downloader, &env.cfg);
        AVS_UNIT_ASSERT_NOT_NULL(handle);

        do {
            _anjay_sched_run(env.base.anjay.sched);
        } while (!handle_packet(&env));

        avs_unit_mocksock_assert_expects_met(env.mocksock);
    }
}
