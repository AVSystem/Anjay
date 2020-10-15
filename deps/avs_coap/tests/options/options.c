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

#    include <stdlib.h>

#    include <avsystem/coap/code.h>
#    include <avsystem/coap/option.h>

#    include "options/avs_coap_iterator.h"

#    define AVS_UNIT_ENABLE_SHORT_ASSERTS
#    include <avsystem/commons/avs_unit_test.h>

#    define MODULE_NAME test
#    include <avs_coap_x_log_config.h>

#    include "options/avs_coap_options.h"

AVS_UNIT_TEST(coap_options, erase_all_from_front) {
    uint8_t OPTS[] = "\x00" // delta = 0, empty
                     "\x23"
                     "foo"       // delta = 2, 3b payload
                     "\x10"      // delta = 1, empty
                     "\x11\xDD"; // delta = 1, 1b payload

    avs_coap_options_t opts = {
        .begin = OPTS,
        .size = sizeof(OPTS) - 1,
        .capacity = sizeof(OPTS) - 1
    };

    avs_coap_option_iterator_t optit = _avs_coap_optit_begin(&opts);

    ASSERT_FALSE(_avs_coap_optit_end(&optit));
    ASSERT_TRUE(&optit == _avs_coap_optit_erase(&optit));

    ASSERT_EQ(opts.size, 7);
    ASSERT_EQ_BYTES(OPTS,
                    "\x23"
                    "foo"
                    "\x10\x11\xDD");

    ASSERT_FALSE(_avs_coap_optit_end(&optit));
    ASSERT_TRUE(&optit == _avs_coap_optit_erase(&optit));

    ASSERT_EQ(opts.size, 3);
    ASSERT_EQ_BYTES(OPTS, "\x30\x11\xDD");

    ASSERT_FALSE(_avs_coap_optit_end(&optit));
    ASSERT_TRUE(&optit == _avs_coap_optit_erase(&optit));

    ASSERT_EQ(opts.size, 2);
    ASSERT_EQ_BYTES(OPTS, "\x41\xDD");

    ASSERT_FALSE(_avs_coap_optit_end(&optit));
    ASSERT_TRUE(&optit == _avs_coap_optit_erase(&optit));

    ASSERT_EQ(opts.size, 0);

    ASSERT_TRUE(_avs_coap_optit_end(&optit));
}

static void optit_advance(avs_coap_option_iterator_t *optit, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        ASSERT_FALSE(_avs_coap_optit_end(optit));
        ASSERT_TRUE(optit == _avs_coap_optit_next(optit));
    }
}

static void erase_nth_option(avs_coap_options_t *opts, size_t n) {
    avs_coap_option_iterator_t optit = _avs_coap_optit_begin(opts);
    optit_advance(&optit, n);
    ASSERT_FALSE(_avs_coap_optit_end(&optit));
    ASSERT_TRUE(&optit == _avs_coap_optit_erase(&optit));
}

AVS_UNIT_TEST(coap_options, erase_all_from_back) {
    uint8_t OPTS[] = "\x00" // delta = 0, empty
                     "\x23"
                     "foo"       // delta = 2, 3b payload
                     "\x10"      // delta = 1, empty
                     "\x11\xDD"; // delta = 1, 1b payload

    avs_coap_options_t opts = {
        .begin = OPTS,
        .size = sizeof(OPTS) - 1,
        .capacity = sizeof(OPTS) - 1
    };

    erase_nth_option(&opts, 3);
    ASSERT_EQ(opts.size, 6);
    ASSERT_EQ_BYTES(OPTS,
                    "\x00\x23"
                    "foo"
                    "\x10");

    erase_nth_option(&opts, 2);
    ASSERT_EQ(opts.size, 5);
    ASSERT_EQ_BYTES(OPTS,
                    "\x00\x23"
                    "foo");

    erase_nth_option(&opts, 1);
    ASSERT_EQ(opts.size, 1);
    ASSERT_EQ_BYTES(OPTS, "\x00");

    erase_nth_option(&opts, 0);
    ASSERT_EQ(opts.size, 0);
}

AVS_UNIT_TEST(coap_options, erase_with_header_expansion) {
    uint8_t OPTS[] = "\xC0"                  // delta = 12, empty
                     "\x14\xAA\xBB\xCC\xDD"; // delta = 1, "\xAA\xBB\xCC\xDD"

    avs_coap_options_t opts = {
        .begin = OPTS,
        .size = sizeof(OPTS) - 1,
        .capacity = sizeof(OPTS) - 1
    };

    avs_coap_option_iterator_t optit = _avs_coap_optit_begin(&opts);

    ASSERT_FALSE(_avs_coap_optit_end(&optit));
    ASSERT_TRUE(&optit == _avs_coap_optit_erase(&optit));

    ASSERT_EQ(opts.size, 6);
    ASSERT_EQ_BYTES_SIZED(OPTS, "\xD4\x00\xAA\xBB\xCC\xDD", 6);

    ASSERT_TRUE(&optit == _avs_coap_optit_next(&optit));
    ASSERT_TRUE(_avs_coap_optit_end(&optit));
}

AVS_UNIT_TEST(coap_options, insert_not_enough_space) {
    avs_coap_options_t opts = avs_coap_options_create_empty(NULL, 0);
    ASSERT_FAIL(avs_coap_options_add_empty(&opts, 0));

    uint8_t buffer[128] = ""; // buffer full of empty options 0
    opts = (avs_coap_options_t) {
        .begin = buffer,
        .size = sizeof(buffer),
        .capacity = sizeof(buffer)
    };
    ASSERT_FAIL(avs_coap_options_add_empty(&opts, 0));

    opts = (avs_coap_options_t) {
        .begin = buffer,
        .size = sizeof(buffer) - 1,
        .capacity = sizeof(buffer)
    };
    ASSERT_FAIL(avs_coap_options_add_opaque(&opts, 0, "A", 1));
}

static void deref_free(void **p) {
    free(*p);
}

AVS_UNIT_TEST(coap_options, insert_last) {
    static const size_t buffer_size = 512;
    void *buffer __attribute__((__cleanup__(deref_free))) = malloc(buffer_size);
    avs_coap_options_t opts =
            avs_coap_options_create_empty(buffer, buffer_size);

#    ifdef WITH_AVS_COAP_BLOCK
    const avs_coap_option_block_t block = {
        .type = AVS_COAP_BLOCK1,
        .seq_num = 0x1234,
        .has_more = true,
        .size = 1024
    };
#    endif // WITH_AVS_COAP_BLOCK

    ASSERT_OK(avs_coap_options_add_opaque(&opts, 0, "0", 1));      // num  0
    ASSERT_OK(avs_coap_options_add_string(&opts, 1, "1"));         // num  1
    ASSERT_OK(avs_coap_options_add_empty(&opts, 2));               // num  2
    ASSERT_OK(avs_coap_options_add_u16(&opts, 3, 0x1234));         // num  3
    ASSERT_OK(avs_coap_options_add_u32(&opts, 4, 0x12345678));     // num  4
    ASSERT_OK(avs_coap_options_set_content_format(&opts, 0x4444)); // num 12
#    ifdef WITH_AVS_COAP_BLOCK
    ASSERT_OK(avs_coap_options_add_block(&opts, &block)); // num 27
#    endif                                                // WITH_AVS_COAP_BLOCK

    const uint8_t EXPECTED[] =
            "\x01\x30"             // num  0 (+0), "0"
            "\x11\x31"             // num  1 (+1), "1"
            "\x10"                 // num  2 (+1), empty
            "\x12\x12\x34"         // num  3 (+1), 0x1234
            "\x14\x12\x34\x56\x78" // num  4 (+1), 0x12345678
            "\x82\x44\x44"         // num 12 (+8), 0x4444
#    ifdef WITH_AVS_COAP_BLOCK
            "\xd3\x02\x01\x23\x4e" // num 27 (+15), ext size (0x02),
                                   // BLOCK1(0x1234, more=1 (0x08) | size=1024
                                   // (0x06))
#    endif                         // WITH_AVS_COAP_BLOCK
            ;

    ASSERT_EQ_BYTES_SIZED(buffer, EXPECTED, sizeof(EXPECTED) - 1);
}

AVS_UNIT_TEST(coap_options, insert_first) {
    static const size_t buffer_size = 512;
    void *buffer __attribute__((__cleanup__(deref_free))) = malloc(buffer_size);
    avs_coap_options_t opts =
            avs_coap_options_create_empty(buffer, buffer_size);

#    ifdef WITH_AVS_COAP_BLOCK
    const avs_coap_option_block_t block = {
        .type = AVS_COAP_BLOCK1,
        .seq_num = 0x1234,
        .has_more = true,
        .size = 1024
    };

    ASSERT_OK(avs_coap_options_add_block(&opts, &block)); // num 27
#    endif                                                // WITH_AVS_COAP_BLOCK
    ASSERT_OK(avs_coap_options_set_content_format(&opts, 0x4444)); // num 12
    ASSERT_OK(avs_coap_options_add_u32(&opts, 4, 0x12345678));     // num  4
    ASSERT_OK(avs_coap_options_add_u16(&opts, 3, 0x1234));         // num  3
    ASSERT_OK(avs_coap_options_add_empty(&opts, 2));               // num  2
    ASSERT_OK(avs_coap_options_add_string(&opts, 1, "1"));         // num  1
    ASSERT_OK(avs_coap_options_add_opaque(&opts, 0, "0", 1));      // num  0

    const uint8_t EXPECTED[] =
            "\x01\x30"             // num  0 (+0), "0"
            "\x11\x31"             // num  1 (+1), "1"
            "\x10"                 // num  2 (+1), empty
            "\x12\x12\x34"         // num  3 (+1), 0x1234
            "\x14\x12\x34\x56\x78" // num  4 (+1), 0x12345678
            "\x82\x44\x44"         // num 12 (+8), 0x4444
#    ifdef WITH_AVS_COAP_BLOCK
            "\xd3\x02\x01\x23\x4e" // num 27 (+15), ext size (0x02),
                                   // BLOCK1(0x1234, more=1 (0x08) | size=1024
                                   // (0x06))
#    endif                         // WITH_AVS_COAP_BLOCK
            ;

    ASSERT_EQ_BYTES_SIZED(buffer, EXPECTED, sizeof(EXPECTED) - 1);
}

AVS_UNIT_TEST(coap_options, insert_middle) {
    static const size_t buffer_size = 512;
    void *buffer __attribute__((__cleanup__(deref_free))) = malloc(buffer_size);
    avs_coap_options_t opts =
            avs_coap_options_create_empty(buffer, buffer_size);

#    ifdef WITH_AVS_COAP_BLOCK
    const avs_coap_option_block_t block = {
        .type = AVS_COAP_BLOCK1,
        .seq_num = 0x1234,
        .has_more = true,
        .size = 1024
    };
#    endif // WITH_AVS_COAP_BLOCK

    ASSERT_OK(avs_coap_options_add_opaque(&opts, 0, "0", 1)); // num  0
#    ifdef WITH_AVS_COAP_BLOCK
    ASSERT_OK(avs_coap_options_add_block(&opts, &block)); // num 27
#    endif                                                // WITH_AVS_COAP_BLOCK
    ASSERT_OK(avs_coap_options_add_string(&opts, 1, "1"));         // num  1
    ASSERT_OK(avs_coap_options_set_content_format(&opts, 0x4444)); // num 12
    ASSERT_OK(avs_coap_options_add_empty(&opts, 2));               // num  2
    ASSERT_OK(avs_coap_options_add_u32(&opts, 4, 0x12345678));     // num  4
    ASSERT_OK(avs_coap_options_add_u16(&opts, 3, 0x1234));         // num  3

    const uint8_t EXPECTED[] =
            "\x01\x30"             // num  0 (+0), "0"
            "\x11\x31"             // num  1 (+1), "1"
            "\x10"                 // num  2 (+1), empty
            "\x12\x12\x34"         // num  3 (+1), 0x1234
            "\x14\x12\x34\x56\x78" // num  4 (+1), 0x12345678
            "\x82\x44\x44"         // num 12 (+8), 0x4444
#    ifdef WITH_AVS_COAP_BLOCK
            "\xd3\x02\x01\x23\x4e" // num 27 (+15), ext size (0x02),
                                   // BLOCK1(0x1234, more=1 (0x08) | size=1024
                                   // (0x06))
#    endif                         // WITH_AVS_COAP_BLOCK
            ;

    ASSERT_EQ_BYTES_SIZED(buffer, EXPECTED, sizeof(EXPECTED) - 1);
}

AVS_UNIT_TEST(coap_options, insert_with_header_shortening) {
    uint8_t OPTS[] =
            "\xd4\x00\xAA\xBB\xCC\xDD" // delta = 13, payload "\xAA\xBB\xCC\xDD"
            "\x02\x11\x22";            // delta = 0, payload "\x11\x22"
    avs_coap_options_t opts = {
        .begin = OPTS,
        .size = sizeof(OPTS) - 1,
        .capacity = sizeof(OPTS) - 1
    };

    // make sure we only have two options
    avs_coap_option_iterator_t optit = _avs_coap_optit_begin(&opts);
    ASSERT_FALSE(_avs_coap_optit_end(&optit));
    ASSERT_EQ(_avs_coap_optit_number(&optit), 13);
    ASSERT_EQ(_avs_coap_option_delta(_avs_coap_optit_current(&optit)), 13);
    ASSERT_EQ(_avs_coap_option_content_length(_avs_coap_optit_current(&optit)),
              4);
    ASSERT_EQ_BYTES_SIZED(_avs_coap_option_value(
                                  _avs_coap_optit_current(&optit)),
                          "\xAA\xBB\xCC\xDD", 4);

    ASSERT_TRUE(_avs_coap_optit_next(&optit) == &optit);
    ASSERT_FALSE(_avs_coap_optit_end(&optit));
    ASSERT_EQ(_avs_coap_optit_number(&optit), 13);
    ASSERT_EQ(_avs_coap_option_delta(_avs_coap_optit_current(&optit)), 0);
    ASSERT_EQ(_avs_coap_option_content_length(_avs_coap_optit_current(&optit)),
              2);
    ASSERT_EQ_BYTES_SIZED(_avs_coap_option_value(
                                  _avs_coap_optit_current(&optit)),
                          "\x11\x22", 2);

    ASSERT_TRUE(_avs_coap_optit_next(&optit) == &optit);
    ASSERT_TRUE(_avs_coap_optit_end(&optit));

    // at this point, the buffer is full, but inserting an option with number
    // in [1; 12] range and no payload will shorten the header of existing
    // option to make enough room for insertion

    ASSERT_FAIL(avs_coap_options_add_empty(&opts, 0));
    ASSERT_FAIL(avs_coap_options_add_empty(&opts, 13));

    ASSERT_OK(avs_coap_options_add_empty(&opts, 1));

    // make sure the option was successfully inserted
    optit = _avs_coap_optit_begin(&opts);
    ASSERT_FALSE(_avs_coap_optit_end(&optit));

    ASSERT_EQ(_avs_coap_optit_number(&optit), 1);
    ASSERT_EQ(_avs_coap_option_delta(_avs_coap_optit_current(&optit)), 1);
    ASSERT_EQ(_avs_coap_option_content_length(_avs_coap_optit_current(&optit)),
              0);

    ASSERT_TRUE(_avs_coap_optit_next(&optit) == &optit);
    ASSERT_FALSE(_avs_coap_optit_end(&optit));

    ASSERT_EQ(_avs_coap_optit_number(&optit), 13);
    ASSERT_EQ(_avs_coap_option_delta(optit.curr_opt), 12);
    ASSERT_EQ(_avs_coap_option_content_length(optit.curr_opt), 4);
    ASSERT_EQ_BYTES_SIZED(_avs_coap_option_value(optit.curr_opt),
                          "\xAA\xBB\xCC\xDD", 4);

    ASSERT_TRUE(_avs_coap_optit_next(&optit) == &optit);
    ASSERT_FALSE(_avs_coap_optit_end(&optit));

    ASSERT_EQ(_avs_coap_optit_number(&optit), 13);
    ASSERT_EQ(_avs_coap_option_delta(optit.curr_opt), 0);
    ASSERT_EQ(_avs_coap_option_content_length(optit.curr_opt), 2);
    ASSERT_EQ_BYTES_SIZED(_avs_coap_option_value(optit.curr_opt), "\x11\x22",
                          2);

    ASSERT_TRUE(_avs_coap_optit_next(&optit) == &optit);
    ASSERT_TRUE(_avs_coap_optit_end(&optit));
}

AVS_UNIT_TEST(coap_options, set_content_format) {
    static const size_t buffer_size = 512;
    void *buffer __attribute__((__cleanup__(deref_free))) = malloc(buffer_size);
    avs_coap_options_t opts =
            avs_coap_options_create_empty(buffer, buffer_size);

    ASSERT_OK(avs_coap_options_set_content_format(&opts, 0));
    ASSERT_EQ(opts.size, 1);
    ASSERT_EQ_BYTES(opts.begin, "\xC0");

    // overwrite with longer
    ASSERT_OK(avs_coap_options_set_content_format(&opts, 10));
    ASSERT_EQ(opts.size, 2);
    ASSERT_EQ_BYTES(opts.begin, "\xC1\x0A");

    // overwrite with same length
    ASSERT_OK(avs_coap_options_set_content_format(&opts, 0xDD));
    ASSERT_EQ(opts.size, 2);
    ASSERT_EQ_BYTES(opts.begin, "\xC1\xDD");

    // remove option
    ASSERT_OK(avs_coap_options_set_content_format(&opts, AVS_COAP_FORMAT_NONE));
    ASSERT_EQ(opts.size, 0);

    // set to long value
    ASSERT_OK(avs_coap_options_set_content_format(&opts, 0xC000));
    ASSERT_EQ(opts.size, 3);
    ASSERT_EQ_BYTES(opts.begin, "\xC2\xC0\x00");

    // overwrite with shorter
    ASSERT_OK(avs_coap_options_set_content_format(&opts, 3));
    ASSERT_EQ(opts.size, 2);
    ASSERT_EQ_BYTES(opts.begin, "\xC1\x03");
}

AVS_UNIT_TEST(coap_options, iterate) {
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wpedantic"
    const uint8_t CONTENT[] = {
        // clang-format off
        [0]  = 0x00,                                                  // empty option
        [1]  = 0x10,                                                  // delta = 1
        [2]  = 0xD0, [3]         = 0x00,                              // extended delta (1b)
        [4]  = 0xE0, [5 ... 6]   = 0x00,                              // extended delta (2b)
        [7]  = 0x01, [8]         = 0x00,                              // length = 1
        [9]  = 0x0D, [10]        = 0x00, [11 ... 11+13-1]     = 0x00, // extended length (1b)
        [24] = 0x0E, [25 ... 26] = 0x00, [27 ... 27+13+256-1] = 0x00  // extended length (2b)
        // clang-format on
    };
#    pragma GCC diagnostic pop

    // TODO: ugly const_cast
    avs_coap_options_t opts = {
        .begin = (void *) (intptr_t) CONTENT,
        .size = sizeof(CONTENT),
        .capacity = sizeof(CONTENT)
    };

    avs_coap_option_iterator_t it = _avs_coap_optit_begin(&opts);
    size_t expected_opt_number = 0;
    const uint8_t *expected_opt_ptr = CONTENT;

    ASSERT_FALSE(_avs_coap_optit_end(&it));
    ASSERT_EQ(_avs_coap_optit_number(&it), expected_opt_number);
    ASSERT_TRUE((const uint8_t *) _avs_coap_optit_current(&it)
                == expected_opt_ptr);
    expected_opt_ptr += 1;

    expected_opt_number += 1;
    ASSERT_TRUE(_avs_coap_optit_next(&it) == &it);
    ASSERT_FALSE(_avs_coap_optit_end(&it));
    ASSERT_EQ(_avs_coap_optit_number(&it), expected_opt_number);
    ASSERT_TRUE((const uint8_t *) _avs_coap_optit_current(&it)
                == expected_opt_ptr);
    expected_opt_ptr += 1;

    expected_opt_number += 13;
    ASSERT_TRUE(_avs_coap_optit_next(&it) == &it);
    ASSERT_FALSE(_avs_coap_optit_end(&it));
    ASSERT_EQ(_avs_coap_optit_number(&it), expected_opt_number);
    ASSERT_TRUE((const uint8_t *) _avs_coap_optit_current(&it)
                == expected_opt_ptr);
    expected_opt_ptr += 2;

    expected_opt_number += 13 + 256;
    ASSERT_TRUE(_avs_coap_optit_next(&it) == &it);
    ASSERT_FALSE(_avs_coap_optit_end(&it));
    ASSERT_EQ(_avs_coap_optit_number(&it), expected_opt_number);
    ASSERT_TRUE((const uint8_t *) _avs_coap_optit_current(&it)
                == expected_opt_ptr);
    expected_opt_ptr += 3;

    ASSERT_TRUE(_avs_coap_optit_next(&it) == &it);
    ASSERT_FALSE(_avs_coap_optit_end(&it));
    ASSERT_EQ(_avs_coap_optit_number(&it), expected_opt_number);
    ASSERT_TRUE((const uint8_t *) _avs_coap_optit_current(&it)
                == expected_opt_ptr);
    expected_opt_ptr += 1 + 1;

    ASSERT_TRUE(_avs_coap_optit_next(&it) == &it);
    ASSERT_FALSE(_avs_coap_optit_end(&it));
    ASSERT_EQ(_avs_coap_optit_number(&it), expected_opt_number);
    ASSERT_TRUE((const uint8_t *) _avs_coap_optit_current(&it)
                == expected_opt_ptr);
    expected_opt_ptr += 1 + 1 + 13;

    ASSERT_TRUE(_avs_coap_optit_next(&it) == &it);
    ASSERT_FALSE(_avs_coap_optit_end(&it));
    ASSERT_EQ(_avs_coap_optit_number(&it), expected_opt_number);
    ASSERT_TRUE((const uint8_t *) _avs_coap_optit_current(&it)
                == expected_opt_ptr);
    expected_opt_ptr += 1 + 2 + 13 + 256;

    ASSERT_TRUE(_avs_coap_optit_next(&it) == &it);
    ASSERT_TRUE(_avs_coap_optit_end(&it));
}

AVS_UNIT_TEST(coap_options, block_too_long) {
    const uint8_t CONTENT[] = "\xd4\x0a"          // num: 23 (13 + 10), size: 4
                              "\x00\x00\x00\x00"; // BLOCK2 option

    avs_coap_options_t opts = {
        .begin = (void *) (intptr_t) CONTENT,
        .size = sizeof(CONTENT) - 1,
        .capacity = sizeof(CONTENT) - 1
    };

    ASSERT_FALSE(_avs_coap_options_valid(&opts));
}

AVS_UNIT_TEST(coap_options, fuzz_heap_overflow) {
    const uint8_t CONTENT[] = "\x74\xff\xff\x7f\xff"
                              "\x31\x32"
                              "\x60"
                              "\x45\x00\x05\x0b\x00\x00"
                              "\x32\x00\x19"
                              "\x31\x5c";

    static const size_t buffer_size = sizeof(CONTENT) + 12;
    void *buffer __attribute__((__cleanup__(deref_free))) =
            calloc(1, buffer_size);
    memcpy(buffer, CONTENT, sizeof(CONTENT) - 1);

    avs_coap_options_t opts = {
        .begin = buffer,
        .size = sizeof(CONTENT) - 1,
        .capacity = buffer_size
    };

    avs_coap_options_remove_by_number(&opts, AVS_COAP_OPTION_BLOCK2);
}

#    ifdef WITH_AVS_COAP_BLOCK
static avs_coap_options_t init_options(void *buf, size_t buf_size, ...) {
    va_list list;
    va_start(list, buf_size);

    avs_coap_options_t opts = avs_coap_options_create_empty(buf, buf_size);
    while (true) {
        // uint16_t is promoted to int when passed to ...
        uint16_t opt_num = (uint16_t) va_arg(list, int);

        // reserved option number; this function uses it as end marker
        if (opt_num == 0) {
            break;
        }

        switch (opt_num) {
        case AVS_COAP_OPTION_URI_HOST:
        case AVS_COAP_OPTION_LOCATION_PATH:
        case AVS_COAP_OPTION_URI_PATH:
        case AVS_COAP_OPTION_URI_QUERY:
        case AVS_COAP_OPTION_LOCATION_QUERY:
        case AVS_COAP_OPTION_PROXY_URI:
        case AVS_COAP_OPTION_PROXY_SCHEME: {
            const char *value = va_arg(list, const char *);
            ASSERT_OK(avs_coap_options_add_string(&opts, opt_num, value));
            break;
        }

        case AVS_COAP_OPTION_IF_MATCH:
        case AVS_COAP_OPTION_ETAG:
        case AVS_COAP_OPTION_IF_NONE_MATCH: {
            const void *opt_buf = va_arg(list, const void *);
            size_t opt_buf_size = va_arg(list, size_t);
            // sanity check in case we read garbage because int was passed
            // instead of size_t
            ASSERT_TRUE(opt_buf_size < 8);
            ASSERT_OK(avs_coap_options_add_opaque(&opts, opt_num, opt_buf,
                                                  (uint16_t) opt_buf_size));
            break;
        }

#        ifdef WITH_AVS_COAP_OBSERVE
        case AVS_COAP_OPTION_OBSERVE: {
            // shorter values promote to int when passed to ...
            int value = va_arg(list, int);
            ASSERT_OK(avs_coap_options_add_observe(&opts, (uint32_t) value));
            break;
        }
#        endif // WITH_AVS_COAP_OBSERVE

        case AVS_COAP_OPTION_URI_PORT:
        case AVS_COAP_OPTION_CONTENT_FORMAT:
        case AVS_COAP_OPTION_MAX_AGE:
        case AVS_COAP_OPTION_ACCEPT:
        case AVS_COAP_OPTION_SIZE1: {
            // shorter values promote to int when passed to ...
            int value = va_arg(list, int);
            ASSERT_OK(
                    avs_coap_options_add_u32(&opts, opt_num, (uint32_t) value));
            break;
        }

        case AVS_COAP_OPTION_BLOCK2:
        case AVS_COAP_OPTION_BLOCK1: {
            const avs_coap_option_block_t *block =
                    va_arg(list, const avs_coap_option_block_t *);
            ASSERT_OK(avs_coap_options_add_block(&opts, block));
            break;
        }

        default:
            ASSERT_TRUE(!"unexpected option number");
            break;
        }
    }

    va_end(list);

    return opts;
}

#        define INIT_OPTIONS(...) \
            init_options(&(char[256]){ 0 }[0], 256, __VA_ARGS__)
#        define BLOCK1(SeqNum, Size, HasMore) \
            &(avs_coap_option_block_t) {      \
                .type = AVS_COAP_BLOCK1,      \
                .seq_num = (SeqNum),          \
                .size = (Size),               \
                .has_more = (HasMore),        \
            }
#        define BLOCK2(SeqNum, Size, HasMore) \
            &(avs_coap_option_block_t) {      \
                .type = AVS_COAP_BLOCK2,      \
                .seq_num = (SeqNum),          \
                .size = (Size),               \
                .has_more = (HasMore),        \
            }

AVS_UNIT_TEST(coap_options_is_sequential_block_request, block1_simple) {
    avs_coap_options_t prev_req =
            INIT_OPTIONS(AVS_COAP_OPTION_BLOCK1, BLOCK1(1, 1024, true), 0);
    avs_coap_options_t prev_res = prev_req;
    avs_coap_options_t curr_req =
            INIT_OPTIONS(AVS_COAP_OPTION_BLOCK1, BLOCK1(2, 1024, true), 0);

    ASSERT_TRUE(_avs_coap_options_is_sequential_block_request(
            &prev_res, &prev_req, &curr_req, 2048));
}

AVS_UNIT_TEST(coap_options_is_sequential_block_request, block2_simple) {
    avs_coap_options_t prev_req =
            INIT_OPTIONS(AVS_COAP_OPTION_BLOCK2, BLOCK2(1, 1024, true), 0);
    avs_coap_options_t prev_res = prev_req;
    avs_coap_options_t curr_req =
            INIT_OPTIONS(AVS_COAP_OPTION_BLOCK2, BLOCK2(2, 1024, true), 0);

    ASSERT_TRUE(_avs_coap_options_is_sequential_block_request(
            &prev_res, &prev_req, &curr_req, 0));
}

AVS_UNIT_TEST(coap_options_is_sequential_block_request, block1_size_change) {
    avs_coap_options_t prev_req =
            INIT_OPTIONS(AVS_COAP_OPTION_BLOCK1, BLOCK1(1, 1024, true), 0);
    avs_coap_options_t prev_res = prev_req;
    avs_coap_options_t curr_req =
            INIT_OPTIONS(AVS_COAP_OPTION_BLOCK1, BLOCK1(4, 512, true), 0);

    ASSERT_TRUE(_avs_coap_options_is_sequential_block_request(
            &prev_res, &prev_req, &curr_req, 2048));
}

AVS_UNIT_TEST(coap_options_is_sequential_block_request, block2_size_change) {
    avs_coap_options_t prev_req =
            INIT_OPTIONS(AVS_COAP_OPTION_BLOCK2, BLOCK2(1, 1024, true), 0);
    avs_coap_options_t prev_res = prev_req;
    avs_coap_options_t curr_req =
            INIT_OPTIONS(AVS_COAP_OPTION_BLOCK2, BLOCK2(4, 512, true), 0);

    ASSERT_TRUE(_avs_coap_options_is_sequential_block_request(
            &prev_res, &prev_req, &curr_req, 2048));
}

AVS_UNIT_TEST(coap_options_is_sequential_block_request,
              block1_elective_mismatch) {
    avs_coap_options_t prev_req =
            INIT_OPTIONS(AVS_COAP_OPTION_BLOCK1, BLOCK1(1, 1024, true),
                         AVS_COAP_OPTION_URI_PATH, "chcialem",
                         AVS_COAP_OPTION_LOCATION_QUERY, "byc=marynarzem", 0);
    avs_coap_options_t prev_res = prev_req;
    avs_coap_options_t curr_req =
            INIT_OPTIONS(AVS_COAP_OPTION_BLOCK1, BLOCK1(2, 1024, true),
                         AVS_COAP_OPTION_URI_PATH, "chcialem",
                         AVS_COAP_OPTION_LOCATION_QUERY,
                         "byc="
                         "operatorem dzwigu budowlanego ktory podnosi pionowo "
                         "zelbetowy strop o masie m=1500kg z przyspieszeniem "
                         "a=2m/s^2",
                         0);

    ASSERT_TRUE(_avs_coap_options_is_sequential_block_request(
            &prev_res, &prev_req, &curr_req, 2048));
}

AVS_UNIT_TEST(coap_options_is_sequential_block_request, block1_critical_match) {
    avs_coap_options_t prev_req =
            INIT_OPTIONS(AVS_COAP_OPTION_BLOCK1, BLOCK1(1, 1024, true),
                         AVS_COAP_OPTION_URI_PATH, "chcialem",
                         AVS_COAP_OPTION_URI_QUERY, "miec=tatuaze", 0);
    avs_coap_options_t prev_res = prev_req;
    avs_coap_options_t curr_req =
            INIT_OPTIONS(AVS_COAP_OPTION_BLOCK1, BLOCK1(2, 1024, true),
                         AVS_COAP_OPTION_URI_PATH, "chcialem",
                         AVS_COAP_OPTION_URI_QUERY, "miec=tatuaze", 0);

    ASSERT_TRUE(_avs_coap_options_is_sequential_block_request(
            &prev_res, &prev_req, &curr_req, 2048));
}

AVS_UNIT_TEST(coap_options_is_sequential_block_request,
              block1_elective_dropped) {
    avs_coap_options_t prev_req =
            INIT_OPTIONS(AVS_COAP_OPTION_BLOCK1, BLOCK1(1, 1024, true),
                         AVS_COAP_OPTION_LOCATION_QUERY, "now look at this net",
                         0);
    avs_coap_options_t prev_res = prev_req;
    avs_coap_options_t curr_req =
            INIT_OPTIONS(AVS_COAP_OPTION_BLOCK1, BLOCK1(2, 1024, true), 0);

    ASSERT_TRUE(_avs_coap_options_is_sequential_block_request(
            &prev_res, &prev_req, &curr_req, 2048));
}

AVS_UNIT_TEST(coap_options_is_sequential_block_request,
              block1_elective_inserted) {
    avs_coap_options_t prev_req =
            INIT_OPTIONS(AVS_COAP_OPTION_BLOCK1, BLOCK1(1, 1024, true), 0);
    avs_coap_options_t prev_res = prev_req;
    avs_coap_options_t curr_req =
            INIT_OPTIONS(AVS_COAP_OPTION_BLOCK1, BLOCK1(2, 1024, true),
                         AVS_COAP_OPTION_LOCATION_QUERY, "that i just found",
                         0);

    ASSERT_TRUE(_avs_coap_options_is_sequential_block_request(
            &prev_res, &prev_req, &curr_req, 2048));
}

AVS_UNIT_TEST(coap_options_is_sequential_block_request,
              block1_offset_mismatch) {
    avs_coap_options_t prev_req =
            INIT_OPTIONS(AVS_COAP_OPTION_BLOCK1, BLOCK1(1, 1024, true), 0);
    avs_coap_options_t prev_res = prev_req;
    avs_coap_options_t curr_req =
            INIT_OPTIONS(AVS_COAP_OPTION_BLOCK1, BLOCK1(3, 1024, true), 0);

    ASSERT_FALSE(_avs_coap_options_is_sequential_block_request(
            &prev_res, &prev_req, &curr_req, 2048));
}

AVS_UNIT_TEST(coap_options_is_sequential_block_request,
              block2_offset_mismatch) {
    avs_coap_options_t prev_req =
            INIT_OPTIONS(AVS_COAP_OPTION_BLOCK2, BLOCK2(1, 1024, true), 0);
    avs_coap_options_t prev_res = prev_req;
    avs_coap_options_t curr_req =
            INIT_OPTIONS(AVS_COAP_OPTION_BLOCK2, BLOCK2(3, 1024, true), 0);

    ASSERT_FALSE(_avs_coap_options_is_sequential_block_request(
            &prev_res, &prev_req, &curr_req, 0));
}

AVS_UNIT_TEST(coap_options_is_sequential_block_request, critical_mismatch) {
    avs_coap_options_t prev_req =
            INIT_OPTIONS(AVS_COAP_OPTION_BLOCK1, BLOCK1(1, 1024, true),
                         AVS_COAP_OPTION_URI_PATH, "when",
                         AVS_COAP_OPTION_URI_QUERY, "i say=go", 0);
    avs_coap_options_t prev_res = prev_req;
    avs_coap_options_t curr_req =
            INIT_OPTIONS(AVS_COAP_OPTION_BLOCK1, BLOCK1(2, 1024, true),
                         AVS_COAP_OPTION_URI_PATH, "get ready",
                         AVS_COAP_OPTION_URI_QUERY, "to=throw", 0);

    ASSERT_FALSE(_avs_coap_options_is_sequential_block_request(
            &prev_res, &prev_req, &curr_req, 1024));
}

AVS_UNIT_TEST(coap_options_is_sequential_block_request,
              content_format_mismatch) {
    avs_coap_options_t prev_req =
            INIT_OPTIONS(AVS_COAP_OPTION_BLOCK1, BLOCK1(1, 1024, true),
                         AVS_COAP_OPTION_CONTENT_FORMAT, 42, 0);
    avs_coap_options_t prev_res = prev_req;
    avs_coap_options_t curr_req =
            INIT_OPTIONS(AVS_COAP_OPTION_BLOCK1, BLOCK1(2, 1024, true),
                         AVS_COAP_OPTION_CONTENT_FORMAT, 1042, 0);

    ASSERT_FALSE(_avs_coap_options_is_sequential_block_request(
            &prev_res, &prev_req, &curr_req, 1024));
}
#    endif // WITH_AVS_COAP_BLOCK

AVS_UNIT_TEST(coap_options_dynamic, grow) {
    avs_coap_options_t opts;
    ASSERT_OK(avs_coap_options_dynamic_init_with_size(&opts, 0));

    static const uint16_t NUMS[] = {
        0,
        _AVS_COAP_EXT_U8_BASE,
        _AVS_COAP_EXT_U16_BASE,
    };
    static const uint8_t ZEROS[_AVS_COAP_EXT_U16_BASE] = "";

    for (uint16_t size = 0; size < AVS_ARRAY_SIZE(NUMS); ++size) {
        for (size_t num = 0; num < AVS_ARRAY_SIZE(NUMS); ++num) {
            uint16_t opt_num = NUMS[num];
            uint16_t opt_size = NUMS[size];

            ASSERT_OK(avs_coap_options_add_opaque(&opts, opt_num, ZEROS,
                                                  opt_size));
        }
    }

    avs_coap_options_cleanup(&opts);
}

AVS_UNIT_TEST(coap_options_dynamic, double_cleanup) {
    avs_coap_options_t opts;
    ASSERT_OK(avs_coap_options_dynamic_init(&opts));
    ASSERT_OK(avs_coap_options_add_empty(&opts, 100));

    avs_coap_options_cleanup(&opts);
    avs_coap_options_cleanup(&opts);
}

AVS_UNIT_TEST(coap_options, cleanup_is_safe_on_static_options) {
    uint8_t buf[128];
    avs_coap_options_t opts = avs_coap_options_create_empty(buf, sizeof(buf));
    ASSERT_OK(avs_coap_options_add_empty(&opts, 100));

    avs_coap_options_cleanup(&opts);
    avs_coap_options_cleanup(&opts);
}

AVS_UNIT_TEST(coap_options, repeated_non_repeatable_elective_options) {
    /**
     * If a message includes an option with more occurrences than the option
     * is defined for, each supernumerary option occurrence that appears
     * subsequently in the message MUST be treated like an unrecognized
     * option (see Section 5.4.1).
     * (...)
     * Upon reception, unrecognized options of class "elective" MUST be silently
     * ignored.
     */
    uint8_t buf[128];
    avs_coap_options_t opts = avs_coap_options_create_empty(buf, sizeof(buf));
    avs_coap_options_add_u16(&opts, AVS_COAP_OPTION_CONTENT_FORMAT, 19);
    avs_coap_options_add_u16(&opts, AVS_COAP_OPTION_CONTENT_FORMAT, 69);

    ASSERT_TRUE(_avs_coap_options_valid(&opts));

    uint16_t value = 0;
    ASSERT_OK(avs_coap_options_get_u16(&opts, AVS_COAP_OPTION_CONTENT_FORMAT,
                                       &value));
    ASSERT_EQ(value, 19);
}

AVS_UNIT_TEST(coap_options, add_string_f) {
    uint8_t buf[128];
    avs_coap_options_t opts = avs_coap_options_create_empty(buf, sizeof(buf));
    ASSERT_OK(avs_coap_options_add_string_f(&opts, AVS_COAP_OPTION_URI_PATH,
                                            "jogurty w %s tylko %d.%02d zl",
                                            "realu", 1, 29));
    ASSERT_OK(avs_coap_options_add_string_f(&opts, AVS_COAP_OPTION_URI_QUERY,
                                            "nowe, nieuzywane %s do %s",
                                            "kierunkowskazy", "prodiza"));

    char str[64];
    avs_coap_option_iterator_t it = AVS_COAP_OPTION_ITERATOR_EMPTY;
    size_t option_size;

    ASSERT_OK(avs_coap_options_get_string_it(&opts, AVS_COAP_OPTION_URI_PATH,
                                             &it, &option_size, str,
                                             sizeof(str)));
#    define EXPECTED_VALUE "jogurty w realu tylko 1.29 zl"
    ASSERT_EQ(option_size, sizeof(EXPECTED_VALUE));
    ASSERT_EQ_STR(str, EXPECTED_VALUE);
#    undef EXPECTED_VALUE

    it = AVS_COAP_OPTION_ITERATOR_EMPTY;
    ASSERT_OK(avs_coap_options_get_string_it(&opts, AVS_COAP_OPTION_URI_QUERY,
                                             &it, &option_size, str,
                                             sizeof(str)));
#    define EXPECTED_VALUE "nowe, nieuzywane kierunkowskazy do prodiza"
    ASSERT_EQ(option_size, sizeof(EXPECTED_VALUE));
    ASSERT_EQ_STR(str, EXPECTED_VALUE);
#    undef EXPECTED_VALUE
}

AVS_UNIT_TEST(coap_options, add_string_f_nullbyte) {
    uint8_t buf[32];
    avs_coap_options_t opts = avs_coap_options_create_empty(buf, sizeof(buf));
    ASSERT_OK(avs_coap_options_add_string_f(&opts, AVS_COAP_OPTION_URI_PATH,
                                            "lol %c nullbyte", '\0'));

    uint8_t bytes[32];
    avs_coap_option_iterator_t it = AVS_COAP_OPTION_ITERATOR_EMPTY;
    size_t option_size;

    ASSERT_OK(avs_coap_options_get_bytes_it(&opts, AVS_COAP_OPTION_URI_PATH,
                                            &it, &option_size, bytes,
                                            sizeof(bytes)));
#    define EXPECTED_VALUE "lol \0 nullbyte"
    ASSERT_EQ(option_size, sizeof(EXPECTED_VALUE) - 1);
    ASSERT_EQ_BYTES(bytes, EXPECTED_VALUE);
#    undef EXPECTED_VALUE
}

#    define ETAG_FROM_STRING(Data)   \
        (avs_coap_etag_t) {          \
            .bytes = (Data),         \
            .size = sizeof(Data) - 1 \
        }

AVS_UNIT_TEST(coap_options, two_etags) {
    uint8_t buf[32];
    avs_coap_options_t opts = avs_coap_options_create_empty(buf, sizeof(buf));
    const avs_coap_etag_t etag1 = ETAG_FROM_STRING("tag");
    const avs_coap_etag_t etag2 = ETAG_FROM_STRING("napraw");

    ASSERT_OK(avs_coap_options_add_etag(&opts, &etag1));
    ASSERT_OK(avs_coap_options_add_etag(&opts, &etag2));

    avs_coap_option_iterator_t it = AVS_COAP_OPTION_ITERATOR_EMPTY;
    avs_coap_etag_t out_etag;

    ASSERT_OK(avs_coap_options_get_etag_it(&opts, &it, &out_etag));
    ASSERT_TRUE(avs_coap_etag_equal(&etag1, &out_etag));

    ASSERT_OK(avs_coap_options_get_etag_it(&opts, &it, &out_etag));
    ASSERT_TRUE(avs_coap_etag_equal(&etag2, &out_etag));
}

AVS_UNIT_TEST(coap_options, get_string) {
#    define OPTION1 "opt1"
#    define OPTION2 "opt2"
    uint8_t buf[32];
    avs_coap_options_t opts = avs_coap_options_create_empty(buf, sizeof(buf));
    ASSERT_OK(avs_coap_options_add_string(&opts, AVS_COAP_OPTION_URI_PATH,
                                          OPTION1));
    ASSERT_OK(avs_coap_options_add_string(&opts, AVS_COAP_OPTION_URI_PATH,
                                          OPTION2));

    char bytes[32];
    size_t option_size;

    ASSERT_OK(avs_coap_options_get_string(&opts, AVS_COAP_OPTION_URI_PATH,
                                          &option_size, bytes, sizeof(bytes)));
    ASSERT_EQ(option_size, sizeof(OPTION1));
    ASSERT_EQ_BYTES(bytes, OPTION1);
#    undef OPTION1
#    undef OPTION2
}

AVS_UNIT_TEST(coap_options, reread_bytes_to_bigger_buffer) {
#    define OPTION1 "opcja 1"
#    define OPTION2 "opcja 2"
    uint8_t buf[32];
    avs_coap_options_t opts = avs_coap_options_create_empty(buf, sizeof(buf));
    ASSERT_OK(avs_coap_options_add_string(&opts, AVS_COAP_OPTION_URI_PATH,
                                          OPTION1));
    ASSERT_OK(avs_coap_options_add_string(&opts, AVS_COAP_OPTION_URI_PATH,
                                          OPTION2));

    // too short buffer
    char buffer_short[sizeof(OPTION1) - 1];

    char buffer_long[sizeof(OPTION1)];
    size_t option_size;

    avs_coap_option_iterator_t it = AVS_COAP_OPTION_ITERATOR_EMPTY;
    ASSERT_FAIL(avs_coap_options_get_string_it(&opts, AVS_COAP_OPTION_URI_PATH,
                                               &it, &option_size, buffer_short,
                                               sizeof(buffer_short)));
    ASSERT_EQ(option_size, sizeof(OPTION1));

    ASSERT_OK(avs_coap_options_get_string_it(&opts, AVS_COAP_OPTION_URI_PATH,
                                             &it, &option_size, buffer_long,
                                             sizeof(buffer_long)));

    ASSERT_EQ(option_size, sizeof(OPTION1));
    ASSERT_EQ_BYTES(buffer_long, OPTION1);

#    undef OPTION1
#    undef OPTION2
}

AVS_UNIT_TEST(coap_options, skip_option) {
#    define OPTION1 "opcja 1"
#    define OPTION2 "opcja 2"
    uint8_t buf[32];
    avs_coap_options_t opts = avs_coap_options_create_empty(buf, sizeof(buf));
    ASSERT_OK(avs_coap_options_add_string(&opts, AVS_COAP_OPTION_URI_PATH,
                                          OPTION1));
    ASSERT_OK(avs_coap_options_add_string(&opts, AVS_COAP_OPTION_URI_PATH,
                                          OPTION2));

    // too short buffer
    char buffer_short[sizeof(OPTION1) - 1];

    char buffer_long[sizeof(OPTION2)];
    size_t option_size;

    avs_coap_option_iterator_t it = AVS_COAP_OPTION_ITERATOR_EMPTY;
    ASSERT_FAIL(avs_coap_options_get_string_it(&opts, AVS_COAP_OPTION_URI_PATH,
                                               &it, &option_size, buffer_short,
                                               sizeof(buffer_short)));
    ASSERT_OK(avs_coap_options_skip_it(&it));

    ASSERT_OK(avs_coap_options_get_string_it(&opts, AVS_COAP_OPTION_URI_PATH,
                                             &it, &option_size, buffer_long,
                                             sizeof(buffer_long)));
    ASSERT_FAIL(avs_coap_options_skip_it(&it));

    ASSERT_EQ(option_size, sizeof(OPTION2));
    ASSERT_EQ_BYTES(buffer_long, OPTION2);

#    undef OPTION1
#    undef OPTION2
}

#    ifdef WITH_AVS_COAP_OBSERVE
AVS_UNIT_TEST(coap_options, observe) {
    uint8_t buf[32];
    avs_coap_options_t opts = avs_coap_options_create_empty(buf, sizeof(buf));
    uint32_t value;

    ASSERT_OK(avs_coap_options_add_observe(&opts, 0x1000000));
    ASSERT_OK(avs_coap_options_get_observe(&opts, &value));
    ASSERT_EQ(value, 0);
    avs_coap_options_remove_by_number(&opts, AVS_COAP_OPTION_OBSERVE);

    ASSERT_OK(avs_coap_options_add_observe(&opts, 0xFFFFFF));
    ASSERT_OK(avs_coap_options_get_observe(&opts, &value));
    ASSERT_EQ(value, 0xFFFFFF);
}

#        ifdef WITH_AVS_COAP_BLOCK
static avs_coap_request_header_t request_header_init(uint8_t coap_code) {
    static uint8_t buf[64];
    memset(buf, 0, sizeof(buf));
    return (avs_coap_request_header_t) {
        .code = coap_code,
        .options = avs_coap_options_create_empty(buf, sizeof(buf))
    };
}

static bool critical_option_validator(uint8_t msg_code, uint32_t optnum) {
    switch (msg_code) {
    case AVS_COAP_CODE_GET:
        return optnum == AVS_COAP_OPTION_URI_PATH
               || optnum == AVS_COAP_OPTION_ACCEPT;
    case AVS_COAP_CODE_PUT:
    case AVS_COAP_CODE_POST:
        return optnum == AVS_COAP_OPTION_URI_PATH
               || optnum == AVS_COAP_OPTION_URI_QUERY
               || optnum == AVS_COAP_OPTION_ACCEPT;
    case AVS_COAP_CODE_DELETE:
        return optnum == AVS_COAP_OPTION_URI_PATH;
    case AVS_COAP_CODE_FETCH:
        return optnum == AVS_COAP_OPTION_ACCEPT;
    default:
        return false;
    }
}

AVS_UNIT_TEST(coap_options, critical_option_validator) {
    avs_coap_request_header_t req_header;

    // AVS_COAP_CODE_GET

    req_header = request_header_init(AVS_COAP_CODE_GET);
    ASSERT_OK(avs_coap_options_add_string(
            &req_header.options, AVS_COAP_OPTION_URI_PATH, "der_Kran"));
    ASSERT_OK(avs_coap_options_add_u16(
            &req_header.options, AVS_COAP_OPTION_ACCEPT, AVS_COAP_FORMAT_JSON));
    ASSERT_OK(avs_coap_options_validate_critical(&req_header,
                                                 critical_option_validator));
    // Observe is not critical
    ASSERT_OK(avs_coap_options_add_observe(&req_header.options, 1));
    ASSERT_OK(avs_coap_options_validate_critical(&req_header,
                                                 critical_option_validator));
    ASSERT_OK(avs_coap_options_add_block(&req_header.options,
                                         &(avs_coap_option_block_t) {
                                             .type = AVS_COAP_BLOCK2,
                                             .seq_num = 0,
                                             .has_more = false,
                                             .size = 256,
                                             .is_bert = false
                                         }));
    ASSERT_OK(avs_coap_options_validate_critical(&req_header,
                                                 critical_option_validator));
    // BLOCK1 cannot be present if code == GET
    ASSERT_OK(avs_coap_options_add_block(&req_header.options,
                                         &(avs_coap_option_block_t) {
                                             .type = AVS_COAP_BLOCK1,
                                             .seq_num = 0,
                                             .has_more = false,
                                             .size = 256,
                                             .is_bert = false
                                         }));
    ASSERT_FAIL(avs_coap_options_validate_critical(&req_header,
                                                   critical_option_validator));

    // AVS_COAP_CODE_PUT
    req_header = request_header_init(AVS_COAP_CODE_PUT);
    ASSERT_OK(avs_coap_options_add_string(
            &req_header.options, AVS_COAP_OPTION_URI_QUERY, "omae_mou=dzwig"));
    ASSERT_OK(avs_coap_options_validate_critical(&req_header,
                                                 critical_option_validator));
    ASSERT_OK(avs_coap_options_add_string(&req_header.options,
                                          AVS_COAP_OPTION_PROXY_URI,
                                          "bijcie masterczulki"));
    ASSERT_FAIL(avs_coap_options_validate_critical(&req_header,
                                                   critical_option_validator));
}
#        endif // WITH_AVS_COAP_BLOCK

#    endif // WITH_AVS_COAP_OBSERVE

#endif // AVS_UNIT_TESTING
