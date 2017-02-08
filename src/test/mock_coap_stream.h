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

#ifndef ANJAY_TEST_MOCKCOAPSTREAM_H
#define ANJAY_TEST_MOCKCOAPSTREAM_H

#include <avsystem/commons/unit/mock_helpers.h>

#include "../coap/stream.h"

AVS_UNIT_MOCK_CREATE(_anjay_coap_stream_get_tx_params)
#define _anjay_coap_stream_get_tx_params(...) AVS_UNIT_MOCK_WRAPPER(_anjay_coap_stream_get_tx_params)(__VA_ARGS__)

AVS_UNIT_MOCK_CREATE(_anjay_coap_stream_set_tx_params)
#define _anjay_coap_stream_set_tx_params(...) AVS_UNIT_MOCK_WRAPPER(_anjay_coap_stream_set_tx_params)(__VA_ARGS__)

AVS_UNIT_MOCK_CREATE(_anjay_coap_stream_setup_request)
#define _anjay_coap_stream_setup_request(...) AVS_UNIT_MOCK_WRAPPER(_anjay_coap_stream_setup_request)(__VA_ARGS__)

AVS_UNIT_MOCK_CREATE(_anjay_coap_stream_set_error)
#define _anjay_coap_stream_set_error(...) AVS_UNIT_MOCK_WRAPPER(_anjay_coap_stream_set_error)(__VA_ARGS__)

AVS_UNIT_MOCK_CREATE(_anjay_coap_stream_get_code)
#define _anjay_coap_stream_get_code(...) AVS_UNIT_MOCK_WRAPPER(_anjay_coap_stream_get_code)(__VA_ARGS__)

AVS_UNIT_MOCK_CREATE(_anjay_coap_stream_get_msg_type)
#define _anjay_coap_stream_get_msg_type(...) AVS_UNIT_MOCK_WRAPPER(_anjay_coap_stream_get_msg_type)(__VA_ARGS__)

AVS_UNIT_MOCK_CREATE(_anjay_coap_stream_get_option_u16)
#define _anjay_coap_stream_get_option_u16(...) AVS_UNIT_MOCK_WRAPPER(_anjay_coap_stream_get_option_u16)(__VA_ARGS__)

AVS_UNIT_MOCK_CREATE(_anjay_coap_stream_get_option_u32)
#define _anjay_coap_stream_get_option_u32(...) AVS_UNIT_MOCK_WRAPPER(_anjay_coap_stream_get_option_u32)(__VA_ARGS__)

AVS_UNIT_MOCK_CREATE(_anjay_coap_stream_get_option_string_it)
#define _anjay_coap_stream_get_option_string_it(...) AVS_UNIT_MOCK_WRAPPER(_anjay_coap_stream_get_option_string_it)(__VA_ARGS__)

AVS_UNIT_MOCK_CREATE(_anjay_coap_stream_get_request_identity)
#define _anjay_coap_stream_get_request_identity(...) AVS_UNIT_MOCK_WRAPPER(_anjay_coap_stream_get_request_identity)(__VA_ARGS__)

AVS_UNIT_MOCK_CREATE(_anjay_coap_stream_validate_critical_options)
#define _anjay_coap_stream_validate_critical_options(...) AVS_UNIT_MOCK_WRAPPER(_anjay_coap_stream_validate_critical_options)(__VA_ARGS__)

#endif // ANJAY_TEST_MOCKCOAPSTREAM_H
