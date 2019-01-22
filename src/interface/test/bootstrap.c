/*
 * Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
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

#include <anjay_config.h>

#include <errno.h>

#include <avsystem/commons/unit/test.h>

#include <anjay_test/dm.h>

#include "../../coap/test/utils.h"
#include "../../sched_internal.h"
#include "../../servers/servers_internal.h"

AVS_UNIT_TEST(bootstrap_write, resource) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42", "514", "4"),
                    CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Hello"));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 514, 1);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 514, 4,
                                         ANJAY_MOCK_DM_STRING(0, "Hello"), 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CHANGED, ID(0xFA3E), NO_PAYLOAD);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, resource_with_create) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42", "514", "4"),
                    CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Hello"));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 514, 0);
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 514,
                                          ANJAY_SSID_BOOTSTRAP, 0, 514);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 514, 4,
                                         ANJAY_MOCK_DM_STRING(0, "Hello"), 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CHANGED, ID(0xFA3E), NO_PAYLOAD);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, resource_with_present_error) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42", "514", "4"),
                    CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Hello"));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 514, -1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, INTERNAL_SERVER_ERROR,
                            ID(0xFA3E), NO_PAYLOAD);
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, resource_with_create_error) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42", "514", "4"),
                    CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Hello"));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 514, 0);
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 514,
                                          ANJAY_SSID_BOOTSTRAP, -1, 514);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, INTERNAL_SERVER_ERROR,
                            ID(0xFA3E), NO_PAYLOAD);
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, resource_with_create_invalid) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42", "514", "4"),
                    CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Hello"));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 514, 0);
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 514,
                                          ANJAY_SSID_BOOTSTRAP, 0, 42);
    // TODO: should expect transaction_rollback here
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, INTERNAL_SERVER_ERROR,
                            ID(0xFA3E), NO_PAYLOAD);
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, resource_error) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42", "514", "7"),
                    CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Hello"));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 514, 1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, NOT_FOUND, ID(0xFA3E),
                            NO_PAYLOAD);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, resource_with_mismatched_tlv_rid) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42", "514", "4"),
                    CONTENT_FORMAT(TLV),
                    PAYLOAD("\xc5\x05"
                            "Hello"));
    // mismatched resource id, RID Uri-Path was 4 but in the payload it is 5
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, BAD_REQUEST, ID(0xFA3E),
                            NO_PAYLOAD);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, resource_error_with_create) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42", "514", "7"),
                    CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Hello"));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 514, 0);
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 514,
                                          ANJAY_SSID_BOOTSTRAP, 0, 514);
    // TODO: should expect transaction_rollback here
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, NOT_FOUND, ID(0xFA3E),
                            NO_PAYLOAD);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, instance) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42", "69"),
                    CONTENT_FORMAT(TLV),
                    PAYLOAD("\xc1\x00\x0d\xc5\x06"
                            "Hello"));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 0,
                                         ANJAY_MOCK_DM_INT(0, 13), 0);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 6,
                                         ANJAY_MOCK_DM_STRING(0, "Hello"), 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CHANGED, ID(0xFA3E), NO_PAYLOAD);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, instance_with_redundant_tlv_header) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42", "69"),
                    CONTENT_FORMAT(TLV),
                    PAYLOAD("\x08\x45\x08\xc6\x0a"
                            "DDDDDD"));
    // Redundant \x08\x45
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, BAD_REQUEST, ID(0xFA3E),
                            NO_PAYLOAD);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write,
              instance_with_redundant_and_incorrect_tlv_header) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42", "69"),
                    CONTENT_FORMAT(TLV),
                    PAYLOAD("\x08\x01\x08\xc6\x0a"
                            "DDDDDD"));
    // IID is 69 but TLV payload contains IID 1
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, BAD_REQUEST, ID(0xFA3E),
                            NO_PAYLOAD);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, instance_wrong_type) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42", "69"),
                    CONTENT_FORMAT(TLV),
                    PAYLOAD("\xc1\x00\x0d"
                            "\x05\x06"
                            "Hello"));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 0,
                                         ANJAY_MOCK_DM_INT(0, 13), 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, BAD_REQUEST, ID(0xFA3E),
                            NO_PAYLOAD);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, instance_error) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42", "69"),
                    CONTENT_FORMAT(TLV),
                    PAYLOAD("\xc1\x00\x0d"
                            "\xc5\x06"
                            "Hello"));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 0,
                                         ANJAY_MOCK_DM_INT(0, 13), -1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, INTERNAL_SERVER_ERROR,
                            ID(0xFA3E), NO_PAYLOAD);
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, instance_some_unsupported) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42", "69"),
                    CONTENT_FORMAT(TLV),
                    PAYLOAD("\xc1\x00\x0d"
                            "\xc5\x07"
                            "Hello"));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 0,
                                         ANJAY_MOCK_DM_INT(0, 13), 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CHANGED, ID(0xFA3E), NO_PAYLOAD);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, object) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42"),
                    CONTENT_FORMAT(TLV),
                    PAYLOAD("\x08\x45\x03" // IID == 69
                            "\xc1\x00\x2a" // RID == 0
                            "\x08\x2a\x03" // IID == 42
                            "\xc1\x03\x45" /* RID == 3 */));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 0);
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 69, ANJAY_SSID_BOOTSTRAP,
                                          0, 69);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 0,
                                         ANJAY_MOCK_DM_INT(0, 42), 0);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 42, 1);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 42, 3,
                                         ANJAY_MOCK_DM_INT(0, 69), 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CHANGED, ID(0xFA3E), NO_PAYLOAD);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, object_error) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42"),
                    CONTENT_FORMAT(TLV),
                    PAYLOAD("\x08\x45\x03" // IID == 69
                            "\xc1\x00\x2a" // RID == 0
                            "\x08\x2a\x03" // IID == 42
                            "\xc1\x03\x45" /* RID == 3 */));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 0);
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 69, ANJAY_SSID_BOOTSTRAP,
                                          0, 69);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 0,
                                         ANJAY_MOCK_DM_INT(0, 42), 0);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 42, 1);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 42, 3,
                                         ANJAY_MOCK_DM_INT(0, 69), -1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, INTERNAL_SERVER_ERROR,
                            ID(0xFA3E), NO_PAYLOAD);
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, object_error_index_end) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42"),
                    CONTENT_FORMAT(TLV),
                    PAYLOAD("\x08\x45\x03" // IID == 69
                            "\xc1\x00\x2a" // RID == 0
                            "\x08\x2a\x03" // IID == 42
                            "\xc1\x03\x45" /* RID == 3 */));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 0);
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 69, ANJAY_SSID_BOOTSTRAP,
                                          0, 69);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 0,
                                         ANJAY_MOCK_DM_INT(0, 42), 0);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 42, 1);
    _anjay_mock_dm_expect_resource_write(
            anjay, &OBJ, 42, 3, ANJAY_MOCK_DM_INT(0, 69), ANJAY_GET_INDEX_END);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, INTERNAL_SERVER_ERROR,
                            ID(0xFA3E), NO_PAYLOAD);
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, object_wrong_type) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42"),
                    CONTENT_FORMAT(TLV),
                    PAYLOAD("\x08\x45\x03" // IID == 69
                            "\xc1\x00\x2a" // RID == 0
                            "\xc8\x2a\x03" // RID in place of IID == 42
                            "\xc1\x03\x45" /* RID == 3 */));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 0);
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 69, ANJAY_SSID_BOOTSTRAP,
                                          0, 69);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 0,
                                         ANJAY_MOCK_DM_INT(0, 42), 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, BAD_REQUEST, ID(0xFA3E),
                            NO_PAYLOAD);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, object_not_found) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("43"),
                    CONTENT_FORMAT(TLV),
                    PAYLOAD("\x08\x45\x03" // IID == 69
                            "\xc1\x00\x2a" // RID == 0
                            "\x08\x2a\x03" // IID == 42
                            "\xc1\x03\x45" /* RID == 3 */));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, NOT_FOUND, ID(0xFA3E),
                            NO_PAYLOAD);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, object_missing) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), CONTENT_FORMAT(TLV),
                    PAYLOAD("\x08\x45\x03" // IID == 69
                            "\xc1\x00\x2a" // RID == 0
                            "\x08\x2a\x03" // IID == 42
                            "\xc1\x03\x45" /* RID == 3 */));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, METHOD_NOT_ALLOWED, ID(0xFA3E),
                            NO_PAYLOAD);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, instance) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, DELETE, ID(0xFA3E), PATH("42", "34"));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 34, 1);
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 34, 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, DELETED, ID(0xfa3e), NO_PAYLOAD);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, instance_missing) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, DELETE, ID(0xFA3E), PATH("42", "34"));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 34, 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, DELETED, ID(0xfa3e), NO_PAYLOAD);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, instance_error) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, DELETE, ID(0xFA3E), PATH("42", "34"));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 34, 1);
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 34, -1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, INTERNAL_SERVER_ERROR,
                            ID(0xfa3e), NO_PAYLOAD);
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, instance_present_error) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, DELETE, ID(0xFA3E), PATH("42", "34"));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 34, -1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, INTERNAL_SERVER_ERROR,
                            ID(0xfa3e), NO_PAYLOAD);
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, object) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, DELETE, ID(0xFA3E), PATH("42"));
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 0, 0, 34);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 1, 0, 69);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 2, 0, 514);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 3, 0, ANJAY_IID_INVALID);
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 34, 0);
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 69, 0);
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 514, 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, DELETED, ID(0xfa3e), NO_PAYLOAD);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, object_it_error) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, DELETE, ID(0xFA3E), PATH("42"));
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 0, 0, 34);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 1, -1, 69);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, INTERNAL_SERVER_ERROR,
                            ID(0xfa3e), NO_PAYLOAD);
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, object_error) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, DELETE, ID(0xFA3E), PATH("42"));
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 0, 0, 34);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 1, 0, 69);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 2, 0, 514);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 3, 0, ANJAY_IID_INVALID);
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 34, 0);
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 69, -1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, INTERNAL_SERVER_ERROR,
                            ID(0xfa3e), NO_PAYLOAD);
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, object_missing) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, DELETE, ID(0xFA3E), PATH("77"));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, DELETED, ID(0xfa3e), NO_PAYLOAD);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, everything) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, DELETE, ID(0xFA3E), NO_PAYLOAD);
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SERVER, 0, 0, 2);
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SERVER, 1, 0, 3);
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SERVER, 2, 0,
                                      ANJAY_IID_INVALID);
    _anjay_mock_dm_expect_instance_remove(anjay, &FAKE_SERVER, 2, 0);
    _anjay_mock_dm_expect_instance_remove(anjay, &FAKE_SERVER, 3, 0);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ_WITH_RESET, 0, 0,
                                      ANJAY_IID_INVALID);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 0, 0, 34);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 1, 0, 69);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 2, 0, 514);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 3, 0, ANJAY_IID_INVALID);
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 34, 0);
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 69, 0);
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 514, 0);
    _anjay_mock_dm_expect_instance_it(
            anjay, (const anjay_dm_object_def_t *const *) &EXECUTE_OBJ, 0, 0,
            ANJAY_IID_INVALID);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ_WITH_RES_OPS, 0, 0,
                                      ANJAY_IID_INVALID);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, DELETED, ID(0xfa3e), NO_PAYLOAD);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, resource) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, DELETE, ID(0xFA3E),
                    PATH("42", "34", "7"));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, BAD_REQUEST, ID(0xfa3e),
                            NO_PAYLOAD);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, bs) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, DELETE, ID(0xFA3E), PATH("bs"));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, BAD_REQUEST, ID(0xfa3e),
                            NO_PAYLOAD);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

static int fail_notify_perform(anjay_t *anjay, anjay_notify_queue_t queue) {
    (void) anjay;
    (void) queue;
    return -1;
}

AVS_UNIT_TEST(bootstrap_finish, error) {
    AVS_UNIT_MOCK(_anjay_notify_perform) = fail_notify_perform;
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);

    // do some Write first to call notifications
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42", "514", "4"),
                    CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Hello"));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 514, 0);
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 514,
                                          ANJAY_SSID_BOOTSTRAP, 0, 514);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 514, 4,
                                         ANJAY_MOCK_DM_STRING(0, "Hello"), 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CHANGED, ID(0xFA3E), NO_PAYLOAD);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));

    // Bootstrap Finish
    DM_TEST_REQUEST(mocksocks[0], CON, POST, ID(0xFA3E), PATH("bs"));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, INTERNAL_SERVER_ERROR,
                            ID(0xFA3E), NO_PAYLOAD);
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    AVS_UNIT_ASSERT_EQUAL(AVS_UNIT_MOCK_INVOCATIONS(_anjay_dm_instance_remove),
                          0);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    AVS_UNIT_ASSERT_EQUAL(AVS_UNIT_MOCK_INVOCATIONS(_anjay_dm_instance_remove),
                          0);
    _anjay_mock_clock_advance(avs_time_duration_from_scalar(1, AVS_TIME_S));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    // still not removing
    AVS_UNIT_ASSERT_EQUAL(AVS_UNIT_MOCK_INVOCATIONS(_anjay_dm_instance_remove),
                          0);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_invalid, invalid) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, POST, ID(0xFA3E), PATH("42"),
                    NO_PAYLOAD);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, METHOD_NOT_ALLOWED, ID(0xFA3E),
                            NO_PAYLOAD);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}
