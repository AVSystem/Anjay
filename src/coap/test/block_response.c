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

#include <avsystem/commons/unit/mocksock.h>
#include <avsystem/commons/unit/test.h>

#include <anjay_test/coap/socket.h>
#include <anjay_test/coap/stream.h>

#define ANJAY_COAP_STREAM_INTERNALS

#include <avsystem/commons/coap/ctx.h>

#include "../block/response.h"
#include "../block/transfer_impl.h"
#include "../coap_stream.h"
#include "../content_format.h"
#include "../stream/stream_internal.h"

typedef struct test_ctx {
    avs_net_abstract_socket_t *mocksock;
    avs_stream_abstract_t *stream;
    anjay_mock_coap_stream_ctx_t mock_stream;
} test_ctx_t;

static test_ctx_t setup(size_t in_buffer_size, size_t out_buffer_size) {
    test_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    _anjay_mocksock_create(&ctx.mocksock, 1252, 1252);
    ctx.mock_stream =
            _anjay_mock_coap_stream_create(&ctx.stream, ctx.mocksock,
                                           in_buffer_size, out_buffer_size);

    return ctx;
}

static void teardown(test_ctx_t *ctx) {
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_cleanup(&ctx->stream));
    _anjay_mock_coap_stream_cleanup(&ctx->mock_stream);
    memset(ctx, 0, sizeof(*ctx));
}

static coap_stream_t *coap_stream(test_ctx_t *ctx) {
    return (coap_stream_t *) ctx->stream;
}

static size_t block_size_for_buffer_size_and_mtu(size_t out_buffer_size,
                                                 size_t mtu) {
    // IMPLEMENTATION DETAIL: buffer size is increased by
    // avs_coap_msg_t#length so that it represents actual limit for the
    // size of a single **non-block** message. After the block-wise transfer
    // triggers, the whole buffer is used for payload storage only, effectively
    // increasing payload capacity by a few bytes for block-wise transfers.
    // By reducing out_buffer_size by the constant below we make sure that
    // effective payload capacity is exactly equal to out_buffer_size.
    //
    // If you feel like stabbing someone for this madness, marian is the person
    // you're looking for.
    //
    // Also, see T864.
    size_t MSG_LENGTH_SIZE = offsetof(avs_coap_msg_t, content);
    assert(out_buffer_size >= MSG_LENGTH_SIZE);

    test_ctx_t test = setup(4096, out_buffer_size - MSG_LENGTH_SIZE);

    avs_unit_mocksock_enable_inner_mtu_getopt(test.mocksock, (int) mtu);
    _anjay_coap_out_setup_mtu(&coap_stream(&test)->data.common.out,
                              coap_stream(&test)->data.common.socket);

    avs_coap_msg_identity_t id = AVS_COAP_MSG_IDENTITY_EMPTY;
    anjay_msg_details_t details = {
        .msg_type = AVS_COAP_MSG_CONFIRMABLE,
        .msg_code = AVS_COAP_CODE_GET,
        .format = AVS_COAP_FORMAT_NONE,
        .observe_serial = false,
        .uri_path = NULL,
        .uri_query = NULL,
        .location_path = NULL
    };

// size of the message described by id/details above:
// 4B header + token + no options + payload marker
// Even though the token size set in headers is 0, it may change during the
// block-wise transfer. Library should account for that, adjusting block
// size so that any token size can be safely handled.
#define EXPECTED_HEADER_BYTES \
    (AVS_COAP_MAX_HEADER_SIZE + AVS_COAP_MAX_TOKEN_LENGTH + 0 + 1)
// header size + max possible BLOCK option size
#define EXPECTED_HEADER_BYTES_WITH_BLOCK \
    (EXPECTED_HEADER_BYTES + AVS_COAP_OPT_BLOCK_MAX_SIZE)

    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_out_setup_msg(
            &coap_stream(&test)->data.common.out, &id, &details, NULL));
    static coap_id_source_t *FAKE_ID_SOURCE = (coap_id_source_t *) -1;
    coap_block_transfer_ctx_t *ctx =
            _anjay_coap_block_response_new(AVS_COAP_MSG_BLOCK_MAX_SIZE,
                                           &coap_stream(&test)->data.common,
                                           FAKE_ID_SOURCE, NULL);

    size_t block_size = 0;
    if (ctx) {
        block_size = ctx->block.size;
        _anjay_coap_block_transfer_delete(&ctx);
    }
    teardown(&test);

    return block_size;
}

AVS_UNIT_TEST(block_response, considers_mtu) {
    // block size: minimum possible
    AVS_UNIT_ASSERT_EQUAL(block_size_for_buffer_size_and_mtu(
                                  4096,
                                  EXPECTED_HEADER_BYTES_WITH_BLOCK
                                          + AVS_COAP_MSG_BLOCK_MIN_SIZE),
                          AVS_COAP_MSG_BLOCK_MIN_SIZE);

    // not quite enough for bigger block size
    AVS_UNIT_ASSERT_EQUAL(block_size_for_buffer_size_and_mtu(
                                  4096,
                                  EXPECTED_HEADER_BYTES_WITH_BLOCK
                                          + AVS_COAP_MSG_BLOCK_MIN_SIZE * 2
                                          - 1),
                          AVS_COAP_MSG_BLOCK_MIN_SIZE);

    // enough for bigger block size
    AVS_UNIT_ASSERT_EQUAL(block_size_for_buffer_size_and_mtu(
                                  4096,
                                  EXPECTED_HEADER_BYTES_WITH_BLOCK
                                          + AVS_COAP_MSG_BLOCK_MIN_SIZE * 2),
                          AVS_COAP_MSG_BLOCK_MIN_SIZE * 2);

    // MTU too low - should fail
    AVS_UNIT_ASSERT_EQUAL(block_size_for_buffer_size_and_mtu(
                                  4096, AVS_COAP_MSG_BLOCK_MIN_SIZE - 1),
                          0);
}

AVS_UNIT_TEST(block_response, block_size_range) {
    // block size does not exceed 1024, even if it could
    AVS_UNIT_ASSERT_EQUAL(block_size_for_buffer_size_and_mtu(4096, 4096),
                          AVS_COAP_MSG_BLOCK_MAX_SIZE);
}

AVS_UNIT_TEST(block_response, considers_buffer_size) {
    // IMPLEMENTATION DETAIL:
    // When restricted by buffer size, we need to have at least 1 byte MORE to
    // correctly handle write/finish flow. We DON'T need to account for headers,
    // though.
    size_t EXTRA_SPACE = 1;

    // MTU > buffer size, 1 byte short from enough for 64B of payload
    AVS_UNIT_ASSERT_EQUAL(
            block_size_for_buffer_size_and_mtu(EXTRA_SPACE + 64 - 1, 4096), 32);

    // MTU > buffer size, enough for 64B of payload
    AVS_UNIT_ASSERT_EQUAL(
            block_size_for_buffer_size_and_mtu(EXTRA_SPACE + 64, 4096), 64);

    // output buffer too small - should fail
    AVS_UNIT_ASSERT_EQUAL(block_size_for_buffer_size_and_mtu(
                                  EXTRA_SPACE + AVS_COAP_MSG_BLOCK_MIN_SIZE - 1,
                                  4096),
                          0);
}
