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
    fluf_double_to_simple_str_value(value, buff);
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
}
