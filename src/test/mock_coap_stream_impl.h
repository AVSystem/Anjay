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

#ifndef ANJAY_TEST_MOCKCOAPSTREAMIMPL_H
#define ANJAY_TEST_MOCKCOAPSTREAMIMPL_H

#include <avsystem/commons/stream.h>
#include <avsystem/commons/unit/mock_helpers.h>

#include "../coap/stream.h"

static int fail(void) {
    AVS_UNIT_ASSERT_TRUE(false);
    return -1;
}

typedef struct coap_stream_mock {
    const avs_stream_v_table_t *vtable;
    anjay_coap_msg_type_t msg_type;
    uint8_t msg_code;
    uint16_t expected_option_number;
    const char **next_opt_value_string;
    int32_t next_opt_value_uint;
} coap_stream_mock_t;

static int mock_get_msg_type(avs_stream_abstract_t *stream,
                             anjay_coap_msg_type_t *out_msg_type) {
    coap_stream_mock_t *mock = (coap_stream_mock_t*)stream;
    AVS_UNIT_ASSERT_TRUE(mock->msg_type >= _ANJAY_COAP_MSG_FIRST);
    AVS_UNIT_ASSERT_TRUE(mock->msg_type <= _ANJAY_COAP_MSG_LAST);
    *out_msg_type = mock->msg_type;
    return 0;
}

static int mock_get_code(avs_stream_abstract_t *stream,
                         uint8_t *out_code) {
    coap_stream_mock_t *mock = (coap_stream_mock_t*)stream;
    AVS_UNIT_ASSERT_NOT_EQUAL(mock->msg_code, -1);
    *out_code = mock->msg_code;
    return 0;
}

#define MOCK_GET_OPTION_UINT(Bits) \
static int mock_get_option_u##Bits (avs_stream_abstract_t *stream, \
                                    uint16_t option_number, \
                                    uint##Bits##_t *out_value) { \
    coap_stream_mock_t *mock = (coap_stream_mock_t*)stream; \
    AVS_UNIT_ASSERT_EQUAL(option_number, mock->expected_option_number); \
    AVS_UNIT_ASSERT_NULL(mock->next_opt_value_string); \
    \
    if (mock->next_opt_value_uint == -1) { \
        return ANJAY_COAP_OPTION_MISSING; \
    } \
    \
    AVS_UNIT_ASSERT_TRUE(mock->next_opt_value_uint >= 0); \
    AVS_UNIT_ASSERT_EQUAL((intmax_t) mock->next_opt_value_uint, \
            (intmax_t) (uint##Bits##_t) mock->next_opt_value_uint); \
    \
    *out_value = (uint##Bits##_t) mock->next_opt_value_uint; \
    mock->next_opt_value_uint = -1; \
    return 0; \
}

MOCK_GET_OPTION_UINT(16)
MOCK_GET_OPTION_UINT(32)

static int mock_get_option_string_it(avs_stream_abstract_t *stream,
                                     uint16_t option_number,
                                     anjay_coap_opt_iterator_t *it,
                                     size_t *out_bytes_read,
                                     char *buffer,
                                     size_t buffer_size) {
    coap_stream_mock_t *mock = (coap_stream_mock_t*)stream;
    AVS_UNIT_ASSERT_EQUAL(option_number, mock->expected_option_number);
    AVS_UNIT_ASSERT_NOT_NULL(mock->next_opt_value_string);
    AVS_UNIT_ASSERT_EQUAL(mock->next_opt_value_uint, -1);

    const char *opt = mock->next_opt_value_string[it->prev_opt_number];
    if (opt == NULL) {
        return ANJAY_COAP_OPTION_MISSING;
    }

    ssize_t result = snprintf(buffer, buffer_size, "%s", opt);
    ++it->prev_opt_number;

    if (result < 0 || (size_t)result >= buffer_size) {
        return -1;
    }

    *out_bytes_read = (size_t)result;
    return 0;
}

#define DECLARE_COAP_STREAM_MOCK(VarName) \
        coap_stream_mock_t VarName = { \
            .vtable = &(const avs_stream_v_table_t) { \
                .write =          (avs_stream_write_t) fail, \
                .finish_message = (avs_stream_finish_message_t) fail, \
                .read =           (avs_stream_read_t) fail, \
                .peek =           (avs_stream_peek_t) fail, \
                .reset =          (avs_stream_reset_t) fail, \
                .close =          (avs_stream_close_t) fail, \
                .get_errno =      (avs_stream_errno_t) fail, \
                .extension_list = &(const avs_stream_v_table_extension_t[]) { \
                    { \
                        ANJAY_COAP_STREAM_EXTENSION, \
                        &(anjay_coap_stream_ext_t) { \
                            .setup_response = \
                                    (anjay_coap_stream_setup_response_t*) fail \
                        } \
                    }, \
                    AVS_STREAM_V_TABLE_EXTENSION_NULL \
                }[0] \
            }, \
            .msg_type = (anjay_coap_msg_type_t) -1, \
            .msg_code = (uint8_t)-1, \
            .expected_option_number = 0, \
            .next_opt_value_string = NULL, \
            .next_opt_value_uint = -1 \
        }; \
        AVS_UNIT_MOCK(_anjay_coap_stream_setup_request) = \
                (AVS_CONFIG_TYPEOF(&_anjay_coap_stream_setup_request)) fail; \
        AVS_UNIT_MOCK(_anjay_coap_stream_get_msg_type) = mock_get_msg_type; \
        AVS_UNIT_MOCK(_anjay_coap_stream_get_code) = mock_get_code; \
        AVS_UNIT_MOCK(_anjay_coap_stream_get_option_u16) = \
                mock_get_option_u16; \
        AVS_UNIT_MOCK(_anjay_coap_stream_get_option_u32) = \
                mock_get_option_u32; \
        AVS_UNIT_MOCK(_anjay_coap_stream_get_option_string_it) = \
                mock_get_option_string_it

#endif // ANJAY_TEST_MOCKCOAPSTREAMIMPL_H
