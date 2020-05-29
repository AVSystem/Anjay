/*
 * Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
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

#include <avs_coap_init.h>

#ifdef AVS_UNIT_TESTING

#    include "src/options/avs_coap_option.h"

#    define AVS_UNIT_ENABLE_SHORT_ASSERTS
#    include <avsystem/commons/avs_unit_test.h>

#    define MODULE_NAME test
#    include <avs_coap_x_log_config.h>

AVS_UNIT_TEST(coap_option, sizeof) {
    uint8_t buffer[512] = "";
    const avs_coap_option_t *opt = (const avs_coap_option_t *) buffer;

    buffer[0] = 0x00;
    // header byte + extended delta + extended length + value
    ASSERT_EQ(_avs_coap_option_sizeof(opt), 1 + 0 + 0 + 0);

    buffer[0] = 0xC0;
    ASSERT_EQ(_avs_coap_option_sizeof(opt), 1 + 0 + 0 + 0);

    buffer[0] = 0xD0;
    ASSERT_EQ(_avs_coap_option_sizeof(opt), 1 + 1 + 0 + 0);

    buffer[0] = 0xE0;
    ASSERT_EQ(_avs_coap_option_sizeof(opt), 1 + 2 + 0 + 0);

    buffer[0] = 0x01;
    ASSERT_EQ(_avs_coap_option_sizeof(opt), 1 + 0 + 0 + 1);

    buffer[0] = 0x0C;
    ASSERT_EQ(_avs_coap_option_sizeof(opt), 1 + 0 + 0 + 12);

    buffer[0] = 0x0D;
    ASSERT_EQ(_avs_coap_option_sizeof(opt), 1 + 0 + 1 + 13);

    buffer[0] = 0x0E;
    ASSERT_EQ(_avs_coap_option_sizeof(opt), 1 + 0 + 2 + 269);

    buffer[0] = 0x11;
    ASSERT_EQ(_avs_coap_option_sizeof(opt), 1 + 0 + 0 + 1);

    buffer[0] = 0xCC;
    ASSERT_EQ(_avs_coap_option_sizeof(opt), 1 + 0 + 0 + 12);

    buffer[0] = 0xDD;
    ASSERT_EQ(_avs_coap_option_sizeof(opt), 1 + 1 + 1 + 13);

    buffer[0] = 0xEE;
    ASSERT_EQ(_avs_coap_option_sizeof(opt), 1 + 2 + 2 + 269);
}

AVS_UNIT_TEST(coap_option_serialize, empty) {
    uint8_t buffer[512] = "";
    const size_t delta = 0;

    size_t written =
            _avs_coap_option_serialize(buffer, sizeof(buffer), delta, NULL, 0);
    //   1 - option header
    static const size_t SIZE = 1;
    ASSERT_EQ(written, SIZE);
    ASSERT_EQ_BYTES_SIZED("\x00", buffer, SIZE);
}

AVS_UNIT_TEST(coap_option_serialize, ext8_delta) {
    uint8_t buffer[512] = "";
    const size_t delta = _AVS_COAP_EXT_U8_BASE + 0x12;

    size_t written =
            _avs_coap_option_serialize(buffer, sizeof(buffer), delta, NULL, 0);
    //   1 - option header
    // + 1 - extended length
    static const size_t SIZE = 2;
    ASSERT_EQ(written, SIZE);
    ASSERT_EQ_BYTES_SIZED("\xd0\x12payload", buffer, SIZE);
}

AVS_UNIT_TEST(coap_option_serialize, ext16_delta) {
    uint8_t buffer[512] = "";
    const size_t delta = _AVS_COAP_EXT_U16_BASE + 0x1234;

    size_t written =
            _avs_coap_option_serialize(buffer, sizeof(buffer), delta, NULL, 0);
    //   1 - option header
    // + 2 - extended length
    static const size_t SIZE = 3;
    ASSERT_EQ(written, SIZE);
    ASSERT_EQ_BYTES_SIZED("\xe0\x12\x34payload", buffer, SIZE);
}

AVS_UNIT_TEST(coap_option_serialize, ext8_size) {
    uint8_t buffer[65536] = "";
    uint8_t data[_AVS_COAP_EXT_U8_BASE + 0x12];
    memset(data, 'A', sizeof(data));

    const size_t delta = 0;
    const size_t length = _AVS_COAP_EXT_U8_BASE + 0x12;
    size_t written = _avs_coap_option_serialize(buffer, sizeof(buffer), delta,
                                                data, length);
    //   1 - option header
    // + 1 - extended length
    static const size_t HDR_SIZE = 2;

    ASSERT_EQ(written, HDR_SIZE + length);
    ASSERT_EQ_BYTES_SIZED("\x0d\x12", buffer, HDR_SIZE);
    ASSERT_EQ_BYTES_SIZED(data, buffer + HDR_SIZE, length);
}

AVS_UNIT_TEST(coap_option_serialize, ext16_size) {
    uint8_t buffer[65536] = "";
    uint8_t data[_AVS_COAP_EXT_U16_BASE + 0x1234];
    memset(data, 'A', sizeof(data));

    const size_t delta = 0;
    const size_t length = _AVS_COAP_EXT_U16_BASE + 0x1234;
    size_t written = _avs_coap_option_serialize(buffer, sizeof(buffer), delta,
                                                data, length);
    //   1 - option header
    // + 2 - extended length
    static const size_t HDR_SIZE = 3;

    ASSERT_EQ(written, HDR_SIZE + length);
    ASSERT_EQ_BYTES_SIZED("\x0e\x12\x34", buffer, HDR_SIZE);
    ASSERT_EQ_BYTES_SIZED(data, buffer + HDR_SIZE, length);
}

#endif // AVS_UNIT_TESTING
