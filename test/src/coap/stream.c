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

#include <avsystem/commons/stream/stream_net.h>
#include <avsystem/commons/unit/test.h>

#include <anjay_test/coap/stream.h>

#include "../../../src/coap/id_source/auto.h"

void _anjay_mock_coap_stream_setup(coap_stream_t *stream) {
    stream->data.common.in.rand_seed = 4;
    _anjay_coap_id_source_release(&stream->id_source);
    stream->id_source = _anjay_coap_id_source_auto_new(4, 0);
    AVS_UNIT_ASSERT_NOT_NULL(stream->id_source);
}

anjay_mock_coap_stream_ctx_t
_anjay_mock_coap_stream_create(avs_stream_abstract_t **stream_,
                               avs_net_abstract_socket_t *socket,
                               size_t in_buffer_size,
                               size_t out_buffer_size) {
    // see init() in src/anjay.c for more details
    in_buffer_size += offsetof(avs_coap_msg_t, content);
    out_buffer_size += offsetof(avs_coap_msg_t, content);
    anjay_mock_coap_stream_ctx_t ctx = {
        .in_buffer = (uint8_t *) avs_malloc(in_buffer_size),
        .out_buffer = (uint8_t *) avs_malloc(out_buffer_size),
    };
    avs_coap_ctx_t *coap_ctx = NULL;
    AVS_UNIT_ASSERT_SUCCESS(avs_coap_ctx_create(&coap_ctx, 0));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_stream_create(
            stream_, coap_ctx, ctx.in_buffer, in_buffer_size, ctx.out_buffer,
            out_buffer_size));
    _anjay_mock_coap_stream_setup((coap_stream_t *) *stream_);
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_net_setsock(*stream_, socket));
    return ctx;
}
