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

#include <avsystem/commons/unit/test.h>

#include <anjay_test/coap/stream.h>

#include "../../../src/coap/id_source/auto.h"

void _anjay_mock_coap_stream_setup(coap_stream_t *stream) {
    stream->in.rand_seed = 4;
    _anjay_coap_id_source_release(&stream->id_source);
    stream->id_source = _anjay_coap_id_source_auto_new(4, 0);
    AVS_UNIT_ASSERT_NOT_NULL(stream->id_source);
}

void _anjay_mock_coap_stream_create(avs_stream_abstract_t **stream_,
                                    anjay_coap_socket_t *socket,
                                    size_t in_buffer_size,
                                    size_t out_buffer_size) {
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_stream_create(
            stream_, socket, in_buffer_size, out_buffer_size));
    _anjay_mock_coap_stream_setup((coap_stream_t *) *stream_);
}
