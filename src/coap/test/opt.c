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

AVS_UNIT_TEST(coap_opt, sizeof) {
    uint8_t buffer[512] = "";
    const anjay_coap_opt_t *opt = (const anjay_coap_opt_t*)buffer;

    buffer[0] = 0x00;
    // header byte + extended delta + extended length + value
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_sizeof(opt), 1 + 0 + 0 + 0);

    buffer[0] = 0xC0;
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_sizeof(opt), 1 + 0 + 0 + 0);

    buffer[0] = 0xD0;
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_sizeof(opt), 1 + 1 + 0 + 0);

    buffer[0] = 0xE0;
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_sizeof(opt), 1 + 2 + 0 + 0);

    buffer[0] = 0x01;
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_sizeof(opt), 1 + 0 + 0 + 1);

    buffer[0] = 0x0C;
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_sizeof(opt), 1 + 0 + 0 + 12);

    buffer[0] = 0x0D;
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_sizeof(opt), 1 + 0 + 1 + 13);

    buffer[0] = 0x0E;
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_sizeof(opt), 1 + 0 + 2 + 269);

    buffer[0] = 0x11;
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_sizeof(opt), 1 + 0 + 0 + 1);

    buffer[0] = 0xCC;
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_sizeof(opt), 1 + 0 + 0 + 12);

    buffer[0] = 0xDD;
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_sizeof(opt), 1 + 1 + 1 + 13);

    buffer[0] = 0xEE;
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_sizeof(opt), 1 + 2 + 2 + 269);
}

