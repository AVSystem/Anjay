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

#include <anjay_test/dm.h>

AVS_UNIT_TEST(bootstrap_write, resource) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    static const char REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x03" "514" // IID
            "\x01" "4" // RID
            "\x10" // Content-Format
            "\xFF"
            "Hello";
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 514, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 514, 4,
                                         ANJAY_MOCK_DM_STRING(0, "Hello"), 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x44\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, resource_with_create) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    static const char REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x03" "514" // IID
            "\x01" "4" // RID
            "\x10" // Content-Format
            "\xFF"
            "Hello";
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 514, 0);
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 514,
                                          ANJAY_SSID_BOOTSTRAP, 0, 514);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 514, 4,
                                         ANJAY_MOCK_DM_STRING(0, "Hello"), 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x44\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, resource_with_present_error) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    static const char REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x03" "514" // IID
            "\x01" "4" // RID
            "\x10" // Content-Format
            "\xFF"
            "Hello";
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 514, -1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\xA0\xFA\x3E");
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, resource_with_create_error) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    static const char REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x03" "514" // IID
            "\x01" "4" // RID
            "\x10" // Content-Format
            "\xFF"
            "Hello";
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 514, 0);
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 514,
                                          ANJAY_SSID_BOOTSTRAP, -1, 514);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\xA0\xFA\x3E");
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, resource_with_create_invalid) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    static const char REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x03" "514" // IID
            "\x01" "4" // RID
            "\x10" // Content-Format
            "\xFF"
            "Hello";
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 514, 0);
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 514,
                                          ANJAY_SSID_BOOTSTRAP, 0, 42);
    // TODO: should expect transaction_rollback here
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\xA0\xFA\x3E");
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, resource_error) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    static const char REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x03" "514" // IID
            "\x01" "4" // RID
            "\x10" // Content-Format
            "\xFF"
            "Hello";
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 514, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x84\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, resource_error_with_create) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    static const char REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x03" "514" // IID
            "\x01" "4" // RID
            "\x10" // Content-Format
            "\xFF"
            "Hello";
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 514, 0);
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 514,
                                          ANJAY_SSID_BOOTSTRAP, 0, 514);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 0);
    // TODO: should expect transaction_rollback here
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x84\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, instance) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    static const char REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "69" // IID
            "\x12\x2d\x16"
            "\xFF"
            "\xc1\x00\x0d"
            "\xc5\x06" "Hello";
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 0, 1);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 0,
                                         ANJAY_MOCK_DM_INT(0, 13), 0);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 6, 1);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 6,
                                         ANJAY_MOCK_DM_STRING(0, "Hello"), 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x44\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, instance_wrong_type) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    static const char REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "69" // IID
            "\x12\x2d\x16"
            "\xFF"
            "\xc1\x00\x0d"
            "\x05\x06" "Hello";
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 0, 1);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 0,
                                         ANJAY_MOCK_DM_INT(0, 13), 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x80\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, instance_error) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    static const char REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "69" // IID
            "\x12\x2d\x16"
            "\xFF"
            "\xc1\x00\x0d"
            "\xc5\x06" "Hello";
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 0, 1);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 0,
                                         ANJAY_MOCK_DM_INT(0, 13), -1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\xA0\xFA\x3E");
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, instance_some_unsupported) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    static const char REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "69" // IID
            "\x12\x2d\x16"
            "\xFF"
            "\xc1\x00\x0d"
            "\xc5\x06" "Hello";
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 0, 1);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 0,
                                         ANJAY_MOCK_DM_INT(0, 13), 0);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 6, 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x44\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, object) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    static const char REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x12\x2d\x16"
            "\xff"
            "\x08\x45\x03" // IID == 69
            "\xc1\x00\x2a" // RID == 0
            "\x08\x2a\x03" // IID == 42
            "\xc1\x03\x45"; // RID == 3
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 0);
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 69, ANJAY_SSID_BOOTSTRAP,
                                          0, 69);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 0, 1);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 0,
                                         ANJAY_MOCK_DM_INT(0, 42), 0);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 42, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 3, 1);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 42, 3,
                                         ANJAY_MOCK_DM_INT(0, 69), 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x44\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, object_error) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    static const char REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x12\x2d\x16"
            "\xff"
            "\x08\x45\x03" // IID == 69
            "\xc1\x00\x2a" // RID == 0
            "\x08\x2a\x03" // IID == 42
            "\xc1\x03\x45"; // RID == 3
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 0);
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 69, ANJAY_SSID_BOOTSTRAP,
                                          0, 69);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 0, 1);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 0,
                                         ANJAY_MOCK_DM_INT(0, 42), 0);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 42, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 3, 1);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 42, 3,
                                         ANJAY_MOCK_DM_INT(0, 69), -1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\xA0\xFA\x3E");
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, object_error_index_end) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    static const char REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x12\x2d\x16"
            "\xff"
            "\x08\x45\x03" // IID == 69
            "\xc1\x00\x2a" // RID == 0
            "\x08\x2a\x03" // IID == 42
            "\xc1\x03\x45"; // RID == 3
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 0);
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 69, ANJAY_SSID_BOOTSTRAP,
                                          0, 69);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 0, 1);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 0,
                                         ANJAY_MOCK_DM_INT(0, 42), 0);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 42, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 3, 1);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 42, 3,
                                         ANJAY_MOCK_DM_INT(0, 69),
                                         ANJAY_GET_INDEX_END);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\xA0\xFA\x3E");
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, object_wrong_type) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    static const char REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x12\x2d\x16"
            "\xff"
            "\x08\x45\x03" // IID == 69
            "\xc1\x00\x2a" // RID == 0
            "\xc8\x2a\x03" // RID in place of IID == 42
            "\xc1\x03\x45"; // RID == 3
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 0);
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 69, ANJAY_SSID_BOOTSTRAP,
                                          0, 69);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 0, 1);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 0,
                                         ANJAY_MOCK_DM_INT(0, 42), 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x80\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, object_not_found) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    static const char REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB2" "43" // OID
            "\x12\x2d\x16"
            "\xff"
            "\x08\x45\x03" // IID == 69
            "\xc1\x00\x2a" // RID == 0
            "\x08\x2a\x03" // IID == 42
            "\xc1\x03\x45"; // RID == 3
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x84\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, object_missing) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    static const char REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xC2\x2d\x16"
            "\xff"
            "\x08\x45\x03" // IID == 69
            "\xc1\x00\x2a" // RID == 0
            "\x08\x2a\x03" // IID == 42
            "\xc1\x03\x45"; // RID == 3
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x85\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, instance) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    static const char REQUEST[] =
            "\x40\x04\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "34"; // IID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 34, 1);
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 34, 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x42\xfa\x3e");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, instance_missing) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    static const char REQUEST[] =
            "\x40\x04\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "34"; // IID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 34, 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x42\xfa\x3e");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, instance_error) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    static const char REQUEST[] =
            "\x40\x04\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "34"; // IID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 34, 1);
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 34, -1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\xa0\xfa\x3e");
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, instance_present_error) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    static const char REQUEST[] =
            "\x40\x04\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "34"; // IID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 34, -1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\xa0\xfa\x3e");
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, object) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    static const char REQUEST[] =
            "\x40\x04\xFA\x3E" // CoAP header
            "\xB2" "42"; // OID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 0, 0, 34);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 1, 0, 69);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 2, 0, 514);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 3, 0, ANJAY_IID_INVALID);
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 34, 0);
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 69, 0);
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 514, 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x42\xfa\x3e");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, object_it_error) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    static const char REQUEST[] =
            "\x40\x04\xFA\x3E" // CoAP header
            "\xB2" "42"; // OID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 0, 0, 34);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 1, -1, 69);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\xa0\xfa\x3e");
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, object_error) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    static const char REQUEST[] =
            "\x40\x04\xFA\x3E" // CoAP header
            "\xB2" "42"; // OID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 0, 0, 34);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 1, 0, 69);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 2, 0, 514);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 3, 0, ANJAY_IID_INVALID);
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 34, 0);
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 69, -1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\xa0\xfa\x3e");
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, object_missing) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    static const char REQUEST[] =
            "\x40\x04\xFA\x3E" // CoAP header
            "\xB2" "77"; // OID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x42\xfa\x3e");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, everything) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    static const char REQUEST[] =
            "\x40\x04\xFA\x3E"; // CoAP header
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x80\xfa\x3e");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, resource) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    static const char REQUEST[] =
            "\x40\x04\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "34" // IID
            "\x01" "7"; // RID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x80\xfa\x3e");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, bs) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    static const char REQUEST[] =
            "\x40\x04\xFA\x3E" // CoAP header
            "\xB2" "bs";
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x80\xfa\x3e");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

static int assert_null_notify_perform(anjay_t *anjay,
                                      anjay_ssid_t origin_ssid,
                                      anjay_notify_queue_t queue) {
    (void) anjay; (void) origin_ssid;
    AVS_UNIT_ASSERT_NULL(queue);
    return 0;
}

AVS_UNIT_TEST(bootstrap_finish, success) {
    AVS_UNIT_MOCK(_anjay_notify_perform) = assert_null_notify_perform;
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
     static const char REQUEST[] =
            "\x40\x02\xFA\x3E" // CoAP header
            "\xB2" "bs"; // OID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x44\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    AVS_UNIT_ASSERT_EQUAL(
            AVS_UNIT_MOCK_INVOCATIONS(_anjay_notify_perform), 1);
    AVS_UNIT_ASSERT_EQUAL(
            AVS_UNIT_MOCK_INVOCATIONS(_anjay_dm_instance_remove), 0);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    AVS_UNIT_ASSERT_EQUAL(
            AVS_UNIT_MOCK_INVOCATIONS(_anjay_notify_perform), 1);
    AVS_UNIT_ASSERT_EQUAL(
            AVS_UNIT_MOCK_INVOCATIONS(_anjay_dm_instance_remove), 1);
    DM_TEST_FINISH;
}

static int fail_notify_perform(anjay_t *anjay,
                               anjay_ssid_t origin_ssid,
                               anjay_notify_queue_t queue) {
    (void) anjay; (void) origin_ssid; (void) queue;
    return -1;
}

AVS_UNIT_TEST(bootstrap_finish, error) {
    AVS_UNIT_MOCK(_anjay_notify_perform) = fail_notify_perform;
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);

    // do some Write first to call notifications
    static const char REQUEST1[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x03" "514" // IID
            "\x01" "4" // RID
            "\x10" // Content-Format
            "\xFF"
            "Hello";
    avs_unit_mocksock_input(mocksocks[0], REQUEST1, sizeof(REQUEST1) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 514, 0);
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 514,
                                          ANJAY_SSID_BOOTSTRAP, 0, 514);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 514, 4,
                                         ANJAY_MOCK_DM_STRING(0, "Hello"), 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x44\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));

    // Bootstrap Finish
    static const char REQUEST2[] =
            "\x40\x02\xFA\x3E" // CoAP header
            "\xB2" "bs"; // OID
    avs_unit_mocksock_input(mocksocks[0], REQUEST2, sizeof(REQUEST2) - 1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\xA0\xFA\x3E");
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    AVS_UNIT_ASSERT_EQUAL(
            AVS_UNIT_MOCK_INVOCATIONS(_anjay_dm_instance_remove), 0);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    AVS_UNIT_ASSERT_EQUAL(
            AVS_UNIT_MOCK_INVOCATIONS(_anjay_dm_instance_remove), 1);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_invalid, invalid) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
     static const char REQUEST[] =
            "\x40\x02\xFA\x3E" // CoAP header
            "\xB2" "42"; // OID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x85\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

static long long time_to_ns(struct timespec ts) {
    return ts.tv_sec * (long long) NS_IN_S + ts.tv_nsec;
}

AVS_UNIT_TEST(bootstrap_backoff, backoff) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    AVS_UNIT_ASSERT_SUCCESS(schedule_request_bootstrap(anjay, 0));

    // after initial failure, Request Bootstrap requests are re-sent with
    // exponential backoff with a factor of 2, starting with 3s, capped at 120s
    struct timespec sched_job_delay;
    struct timespec backoff = { 3, 0 };
    const struct timespec max_backoff = { 120, 0 };

    while (backoff.tv_sec <= max_backoff.tv_sec) {
        avs_unit_mocksock_output_fail(mocksocks[0], -1);
        AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

        AVS_UNIT_ASSERT_SUCCESS(anjay_sched_time_to_next(anjay, &sched_job_delay));
        AVS_UNIT_ASSERT_TRUE(
                llabs(time_to_ns(sched_job_delay) - time_to_ns(backoff)) < 10);

        _anjay_mock_clock_advance(&sched_job_delay);
        backoff.tv_sec *= 2;
    }

    // ensure the delay is capped at max_backoff
    avs_unit_mocksock_output_fail(mocksocks[0], -1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_time_to_next(anjay, &sched_job_delay));
    AVS_UNIT_ASSERT_TRUE(
            llabs(time_to_ns(sched_job_delay) - time_to_ns(max_backoff)) < 10);

    DM_TEST_FINISH;
}
