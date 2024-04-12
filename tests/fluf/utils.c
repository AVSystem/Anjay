/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <math.h>
#include <string.h>

#include <avsystem/commons/avs_unit_test.h>

#include <fluf/fluf_utils.h>

static void test_double_to_string(double value, const char *result) {
    char buff[100] = { 0 };
    fluf_double_to_simple_str_value(buff, value);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(buff, result, strlen(result));
};

AVS_UNIT_TEST(utils, double_to_str_custom) {
    test_double_to_string(0, "0");
    test_double_to_string((double) UINT16_MAX, "65535");
    test_double_to_string((double) UINT32_MAX - 0.02, "4294967294.98");
    test_double_to_string((double) UINT32_MAX, "4294967295");
    test_double_to_string((double) UINT32_MAX + 1.0, "4294967296");
    test_double_to_string(0.0005999999999999999, "0.0005999999999999999");
    test_double_to_string(0.00000122, "0.00000122");
    test_double_to_string(0.000000002, "0.000000002");
    test_double_to_string(777.000760, "777.00076");
    test_double_to_string(10.022, "10.022");
    test_double_to_string(100.022, "100.022");
    test_double_to_string(1000.033, "1000.033");
    test_double_to_string(99999.03, "99999.03");
    test_double_to_string(999999999.4440002, "999999999.4440002");
    test_double_to_string(1234e15, "1234000000000000000");
    test_double_to_string(1e16, "10000000000000000");
    test_double_to_string(1000000000000001, "1000000000000001");
    test_double_to_string(2111e18, "2.111e21");
    test_double_to_string(0.0, "0");
    test_double_to_string(NAN, "nan");
    test_double_to_string(INFINITY, "inf");
    test_double_to_string(-INFINITY, "-inf");
    test_double_to_string(-(double) UINT32_MAX, "-4294967295");
    test_double_to_string(-10.022, "-10.022");
    test_double_to_string(-100.022, "-100.022");
    test_double_to_string(-1234e15, "-1234000000000000000");
    test_double_to_string(-2111e18, "-2.111e21");
    test_double_to_string(-124e-15, "-1.24e-13");
    test_double_to_string(-4568e-22, "-4.568e-19");
    test_double_to_string(1.0, "1");
    test_double_to_string(78e120, "7.8e121");
    test_double_to_string(1e20, "1e20");
}

static void
test_string_to_uint64(const char *buff, uint64_t expected, bool failed) {
    uint64_t value;
    if (!failed) {
        AVS_UNIT_ASSERT_SUCCESS(
                fluf_string_to_uint64_value(&value, buff, strlen(buff)));
        AVS_UNIT_ASSERT_EQUAL(value, expected);
    } else {
        AVS_UNIT_ASSERT_FAILED(
                fluf_string_to_uint64_value(&value, buff, strlen(buff)));
    }
}

AVS_UNIT_TEST(utils, string_to_uint64) {
    test_string_to_uint64("", 0, true);
    test_string_to_uint64("0", 0, false);
    test_string_to_uint64("1", 1, false);
    test_string_to_uint64("2", 2, false);
    test_string_to_uint64("255", 255, false);
    test_string_to_uint64("256", 256, false);
    test_string_to_uint64("65535", 65535, false);
    test_string_to_uint64("65536", 65536, false);
    test_string_to_uint64("4294967295", 4294967295, false);
    test_string_to_uint64("4294967296", 4294967296, false);
    test_string_to_uint64("18446744073709551615", UINT64_MAX, false);
    test_string_to_uint64("18446744073709551616", 0, true);
    test_string_to_uint64("99999999999999999999", 0, true);
    test_string_to_uint64("184467440737095516160", 0, true);
    test_string_to_uint64("b", 0, true);
    test_string_to_uint64("-1", 0, true);
    test_string_to_uint64("255b", 0, true);
    test_string_to_uint64("123b5", 0, true);
}

static void
test_string_to_uint32(const char *buff, uint64_t expected, bool failed) {
    uint32_t value;
    if (!failed) {
        AVS_UNIT_ASSERT_SUCCESS(
                fluf_string_to_uint32_value(&value, buff, strlen(buff)));
        AVS_UNIT_ASSERT_EQUAL(value, expected);
    } else {
        AVS_UNIT_ASSERT_FAILED(
                fluf_string_to_uint32_value(&value, buff, strlen(buff)));
    }
}

AVS_UNIT_TEST(utils, string_to_uint32) {
    test_string_to_uint64("", 0, true);
    test_string_to_uint32("0", 0, false);
    test_string_to_uint32("1", 1, false);
    test_string_to_uint32("2", 2, false);
    test_string_to_uint32("255", 255, false);
    test_string_to_uint32("256", 256, false);
    test_string_to_uint32("65535", 65535, false);
    test_string_to_uint32("65536", 65536, false);
    test_string_to_uint32("4294967295", 4294967295, false);
    test_string_to_uint32("4294967296", 4294967296, true);
    test_string_to_uint32("42949672951", 4294967296, true);
    test_string_to_uint64("b", 0, true);
    test_string_to_uint64("-1", 0, true);
    test_string_to_uint64("255b", 0, true);
    test_string_to_uint64("123b5", 0, true);
}

static void
test_string_to_int64(const char *buff, int64_t expected, bool failed) {
    int64_t value;
    if (!failed) {
        AVS_UNIT_ASSERT_SUCCESS(
                fluf_string_to_int64_value(&value, buff, strlen(buff)));
        AVS_UNIT_ASSERT_EQUAL(value, expected);
    } else {
        AVS_UNIT_ASSERT_FAILED(
                fluf_string_to_int64_value(&value, buff, strlen(buff)));
    }
}

AVS_UNIT_TEST(utils, string_to_int64) {
    test_string_to_uint64("", 0, true);
    test_string_to_int64("0", 0, false);
    test_string_to_int64("1", 1, false);
    test_string_to_int64("+1", 1, false);
    test_string_to_int64("-1", -1, false);
    test_string_to_int64("2", 2, false);
    test_string_to_int64("+2", 2, false);
    test_string_to_int64("-2", -2, false);
    test_string_to_int64("255", 255, false);
    test_string_to_int64("+255", 255, false);
    test_string_to_int64("-255", -255, false);
    test_string_to_int64("256", 256, false);
    test_string_to_int64("+256", 256, false);
    test_string_to_int64("-256", -256, false);
    test_string_to_int64("65535", 65535, false);
    test_string_to_int64("+65535", 65535, false);
    test_string_to_int64("-65535", -65535, false);
    test_string_to_int64("65536", 65536, false);
    test_string_to_int64("+65536", 65536, false);
    test_string_to_int64("-65536", -65536, false);
    test_string_to_int64("4294967295", 4294967295, false);
    test_string_to_int64("+4294967295", 4294967295, false);
    test_string_to_int64("-4294967295", -4294967295, false);
    test_string_to_int64("4294967296", 4294967296, false);
    test_string_to_int64("+4294967296", 4294967296, false);
    test_string_to_int64("-4294967296", -4294967296, false);
    test_string_to_int64("9223372036854775807", INT64_MAX, false);
    test_string_to_int64("+9223372036854775807", INT64_MAX, false);
    test_string_to_int64("-9223372036854775808", INT64_MIN, false);
    test_string_to_int64("9223372036854775808", 0, true);
    test_string_to_int64("9999999999999999999", 0, true);
    test_string_to_int64("92233720368547758070", 0, true);
    test_string_to_int64("18446744073709551615", 0, true);
    test_string_to_uint64("b", 0, true);
    test_string_to_uint64("255b", 0, true);
    test_string_to_uint64("123b5", 0, true);
    test_string_to_uint64("-b", 0, true);
    test_string_to_uint64("-255b", 0, true);
    test_string_to_uint64("-123b5", 0, true);
}
