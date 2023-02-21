/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <avs_coap_init.h>

#if defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_TCP)

#    include <avsystem/commons/avs_memory.h>

#    define AVS_UNIT_ENABLE_SHORT_ASSERTS
#    include <avsystem/commons/avs_unit_test.h>

#    include <avsystem/coap/code.h>

#    include "options/avs_coap_option.h"

#    define MODULE_NAME test
#    include <avs_coap_x_log_config.h>

#    include "tcp/avs_coap_tcp_msg.h"

#    include "./utils.h"

#    define TEST_DATA(Data) \
        .data = Data,       \
        .data_size = sizeof(Data) - 1

typedef struct header_serialize_test_struct {
    size_t payload_length;
    size_t options_length;
    uint8_t token_length;
    uint8_t code;
    uint8_t data[_AVS_COAP_TCP_MAX_HEADER_LENGTH];
    size_t data_size;
} header_test_data_t;

const header_test_data_t test_headers[] = {
    { 0, 0, 0, AVS_COAP_CODE(0, 0), TEST_DATA("\x00\x00") },
    { 0, 0, 0, AVS_COAP_CODE(1, 3), TEST_DATA("\x00\x23") },
    { 1, 0, 0, AVS_COAP_CODE(0, 0), TEST_DATA("\x20\x00") },
    { 12, 0, 0, AVS_COAP_CODE(1, 3), TEST_DATA("\xD0\x00\x23") },
    { 13, 0, 0, AVS_COAP_CODE(0, 0), TEST_DATA("\xD0\x01\x00") },
    { 267, 0, 0, AVS_COAP_CODE(0, 0), TEST_DATA("\xD0\xFF\x00") },
    { 268, 0, 0, AVS_COAP_CODE(1, 3), TEST_DATA("\xE0\x00\x00\x23") },
    { 269, 0, 0, AVS_COAP_CODE(0, 0), TEST_DATA("\xE0\x00\x01\x00") },
    { 65803, 0, 0, AVS_COAP_CODE(0, 0), TEST_DATA("\xE0\xFF\xFF\x00") },
    { 65804, 0, 0, AVS_COAP_CODE(1, 3), TEST_DATA("\xF0\x00\x00\x00\x00\x23") },
    { 65805, 0, 0, AVS_COAP_CODE(0, 0), TEST_DATA("\xF0\x00\x00\x00\x01\x00") },

    { 0, 0, 1, AVS_COAP_CODE(0, 0), TEST_DATA("\x01\x00") },
    { 0, 0, 2, AVS_COAP_CODE(0, 0), TEST_DATA("\x02\x00") },
    { 0, 0, 4, AVS_COAP_CODE(0, 0), TEST_DATA("\x04\x00") },
    { 0, 0, 8, AVS_COAP_CODE(0, 0), TEST_DATA("\x08\x00") },
    { 0, 1, 0, AVS_COAP_CODE(0, 0), TEST_DATA("\x10\x00") },

#    if SIZE_MAX > UINT32_MAX
    { 4295033098, 1, 8, AVS_COAP_CODE(0, 0),
      TEST_DATA("\xF8\xFF\xFF\xFF\xFF\x00") },
    { 4295033099, 0, 0, AVS_COAP_CODE(0, 0),
      TEST_DATA("\xF0\xFF\xFF\xFF\xFF\x00") }
#    endif
};
const size_t cases_count = AVS_ARRAY_SIZE(test_headers);

static avs_coap_tcp_header_t init_header(const header_test_data_t *test_data) {
    avs_coap_tcp_header_t header =
            _avs_coap_tcp_header_init(test_data->payload_length,
                                      test_data->options_length,
                                      test_data->token_length,
                                      test_data->code);
    return header;
}

AVS_UNIT_TEST(coap_tcp_header, serialize) {
    for (size_t i = 0; i < cases_count; i++) {
        void *buf = avs_calloc(1, _AVS_COAP_TCP_MAX_HEADER_LENGTH);
        ASSERT_NOT_NULL(buf);
        avs_coap_tcp_header_t header = init_header(&test_headers[i]);
        size_t bytes_written =
                _avs_coap_tcp_header_serialize(&header, buf,
                                               _AVS_COAP_TCP_MAX_HEADER_LENGTH);
        ASSERT_EQ(bytes_written, test_headers[i].data_size);
        ASSERT_EQ_BYTES_SIZED(buf, test_headers[i].data, bytes_written);
        avs_free(buf);
    }
}

static void validate_header(avs_coap_tcp_header_t *header,
                            const header_test_data_t *test_data) {
    ASSERT_EQ(header->code, test_data->code);
    ASSERT_EQ(header->opts_and_payload_len,
              test_data->payload_length + !!test_data->payload_length
                      + test_data->options_length);
    ASSERT_EQ(header->token_len, test_data->token_length);
}

AVS_UNIT_TEST(coap_tcp_header, parse) {
    for (size_t i = 0; i < cases_count; i++) {
        bytes_dispenser_t dispenser = {
            .read_ptr = test_headers[i].data,
            .bytes_left = test_headers[i].data_size
        };
        avs_coap_tcp_header_t header = { 0 };
        size_t missing;
        ASSERT_OK(_avs_coap_tcp_header_parse(&header, &dispenser, &missing));
        validate_header(&header, &test_headers[i]);
    }
}

#endif // defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_TCP)
