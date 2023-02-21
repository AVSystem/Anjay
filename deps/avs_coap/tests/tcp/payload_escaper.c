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

#    define AVS_UNIT_ENABLE_SHORT_ASSERTS
#    include <avsystem/commons/avs_unit_test.h>

#    include <avsystem/commons/avs_utils.h>

#    include "tcp/avs_coap_tcp_utils.h"

#    include <stdio.h>

typedef struct {
    char *to_escape;
    size_t to_escape_size;
    char *escaped;
    size_t escaped_size;
} test_data_t;

#    define TEST_DATA(ToEscape, Escaped)              \
        {                                             \
            .to_escape = (ToEscape),                  \
            .to_escape_size = (sizeof(ToEscape) - 1), \
            .escaped = (Escaped),                     \
            .escaped_size = (sizeof(Escaped) - 1)     \
        }

AVS_UNIT_TEST(payload_escaper, escape_test) {
    // clang-format off
    test_data_t payloads[] = {
        TEST_DATA("a", "a"),
        TEST_DATA("\0", "\\x00"),
        TEST_DATA("\\", "\\\\"),
        TEST_DATA("%", "%"),
        TEST_DATA("\"", "\\\""),
        TEST_DATA("\'", "\\\'"),
        TEST_DATA("\\\\x00%c", "\\\\\\\\x00%c"),
        TEST_DATA("\r", "\\x0D"),
        TEST_DATA("\xFF", "\\xFF"),
        TEST_DATA(" ", " "),
        TEST_DATA("~", "~"),
        TEST_DATA("ABCDEFGH1234567", "ABCDEFGH1234567"),
        TEST_DATA("ABCDEFGH12345678", "ABCDEFGH1234567")
    };
    // clang-format on

    for (size_t i = 0; i < AVS_ARRAY_SIZE(payloads); i++) {
        char escaped_string[16];
        _avs_coap_tcp_escape_payload(payloads[i].to_escape,
                                     payloads[i].to_escape_size,
                                     escaped_string,
                                     sizeof(escaped_string));

        char log_message[16];
        avs_simple_snprintf(log_message, sizeof(log_message), "%s",
                            escaped_string);
        ASSERT_EQ(strlen(log_message), payloads[i].escaped_size);
        ASSERT_EQ_BYTES_SIZED(log_message, payloads[i].escaped,
                              payloads[i].escaped_size);
    }
}

AVS_UNIT_TEST(payload_escaper, convert_truncated) {
    const char *data = "abcdefgh12345678";

    char escaped_string[9];

    size_t bytes_escaped =
            _avs_coap_tcp_escape_payload(data, strlen(data), escaped_string,
                                         sizeof(escaped_string));
    ASSERT_EQ(bytes_escaped, sizeof(escaped_string) - 1);
    ASSERT_EQ_BYTES_SIZED(escaped_string, "abcdefgh", sizeof("abcdefgh"));

    bytes_escaped = _avs_coap_tcp_escape_payload(data + bytes_escaped,
                                                 strlen(data) - bytes_escaped,
                                                 escaped_string,
                                                 sizeof(escaped_string));
    ASSERT_EQ(bytes_escaped, sizeof(escaped_string) - 1);
    ASSERT_EQ_BYTES_SIZED(escaped_string, "12345678", sizeof("12345678"));
}

#endif // defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_TCP)
