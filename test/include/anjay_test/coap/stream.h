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

#ifndef ANJAY_TEST_COAP_STREAM_H
#define ANJAY_TEST_COAP_STREAM_H

#include <avsystem/commons/stream.h>

// Hack to get to the coap_stream_t structure.
#define ANJAY_COAP_STREAM_INTERNALS
#include "../../../src/coap/stream/stream.h"

void _anjay_mock_coap_stream_setup(coap_stream_t *stream);

void _anjay_mock_coap_stream_create(avs_stream_abstract_t **stream_,
                                    anjay_coap_socket_t *socket,
                                    size_t in_buffer_size,
                                    size_t out_buffer_size);

#endif /* ANJAY_TEST_COAP_STREAM_H */
