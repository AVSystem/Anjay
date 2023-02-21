/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#define AVS_UNIT_ENABLE_SHORT_ASSERTS
#include <avsystem/commons/avs_unit_test.h>

#include "src/core/servers/anjay_servers_internal.h"
#include "tests/core/coap/utils.h"
#include "tests/utils/dm.h"

#ifdef ANJAY_WITH_LWM2M11
AVS_UNIT_TEST(bootstrap_read, root_path) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID(0xFA3E), NO_PAYLOAD);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, METHOD_NOT_ALLOWED, ID(0xFA3E),
                            NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_read, resource_path) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID(0xFA3E), PATH("1", "0", "0"),
                    NO_PAYLOAD);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, METHOD_NOT_ALLOWED, ID(0xFA3E),
                            NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_read, resource_instance_path) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID(0xFA3E),
                    PATH("1", "0", "0", "0"), NO_PAYLOAD);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, METHOD_NOT_ALLOWED, ID(0xFA3E),
                            NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_read, invalid_oid) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID(0xFA3E), PATH("3"), NO_PAYLOAD);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, METHOD_NOT_ALLOWED, ID(0xFA3E),
                            NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_read, server_object_instance) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID(0xFA3E), PATH("1", "12"),
                    ACCEPT(AVS_COAP_FORMAT_OMA_LWM2M_TLV), NO_PAYLOAD);
    const anjay_iid_t iid = 12;
    const anjay_rid_t rid = 34;
    _anjay_mock_dm_expect_list_instances(
            anjay, &FAKE_SERVER, 0,
            (const anjay_iid_t[]) { iid, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &FAKE_SERVER, iid, 0,
            (const anjay_mock_dm_res_entry_t[]) { { rid, ANJAY_DM_RES_R,
                                                    ANJAY_DM_RES_PRESENT },
                                                  ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, iid, rid,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_STRING(0, "whatever"));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT, ID(0xFA3E),
                            CONTENT_FORMAT(OMA_LWM2M_TLV),
                            // [ 0xC8 ][ 0x22 ]
                            // 11.............. Resource with Value
                            // ..0............. Identifier field is 8 bits long
                            // ...01............Length field is 8-bits and Bits
                            //                  2-0 are ignored
                            // ........00100010 ID = 34
                            PAYLOAD("\xC8\x22\x08"
                                    "whatever"));
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_read, server_object) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID(0xFA3E), PATH("1"),
                    ACCEPT(AVS_COAP_FORMAT_SENML_JSON), NO_PAYLOAD);
    const anjay_iid_t iid1 = 10;
    const anjay_iid_t iid2 = 20;
    const anjay_rid_t rid1 = 30;
    const anjay_rid_t rid2 = 40;
    _anjay_mock_dm_expect_list_instances(
            anjay, &FAKE_SERVER, 0,
            (const anjay_iid_t[]) { iid1, iid2, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &FAKE_SERVER, iid1, 0,
            (const anjay_mock_dm_res_entry_t[]) { { rid1, ANJAY_DM_RES_R,
                                                    ANJAY_DM_RES_PRESENT },
                                                  ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, iid1, rid1,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_STRING(0, "RES1"));
    _anjay_mock_dm_expect_list_resources(
            anjay, &FAKE_SERVER, iid2, 0,
            (const anjay_mock_dm_res_entry_t[]) { { rid2, ANJAY_DM_RES_R,
                                                    ANJAY_DM_RES_PRESENT },
                                                  ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, iid2, rid2,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_STRING(0, "RES2"));

    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT, ID(0xFA3E),
                            CONTENT_FORMAT(SENML_JSON),
                            PAYLOAD("[{\"bn\":\"/1\","
                                    "\"n\":\"/10/30\",\"vs\":\"RES1\"},"
                                    "{\"n\":\"/20/40\",\"vs\":\"RES2\"}]"));
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_read, non_readable_resources) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID(0xFA3E), PATH("1"),
                    ACCEPT(AVS_COAP_FORMAT_SENML_JSON), NO_PAYLOAD);
    const anjay_iid_t iid1 = 10;
    const anjay_iid_t iid2 = 20;
    const anjay_rid_t rid1 = 30;
    const anjay_rid_t rid2 = 40;
    _anjay_mock_dm_expect_list_instances(
            anjay, &FAKE_SERVER, 0,
            (const anjay_iid_t[]) { iid1, iid2, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &FAKE_SERVER, iid1, 0,
            (const anjay_mock_dm_res_entry_t[]) { { rid1, ANJAY_DM_RES_BS_RW,
                                                    ANJAY_DM_RES_PRESENT },
                                                  ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, iid1, rid1,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_STRING(0, "RES1"));
    _anjay_mock_dm_expect_list_resources(
            anjay, &FAKE_SERVER, iid2, 0,
            (const anjay_mock_dm_res_entry_t[]) { { rid2, ANJAY_DM_RES_W,
                                                    ANJAY_DM_RES_PRESENT },
                                                  ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, iid2, rid2,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_STRING(0, "RES2"));

    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT, ID(0xFA3E),
                            CONTENT_FORMAT(SENML_JSON),
                            PAYLOAD("[{\"bn\":\"/1\","
                                    "\"n\":\"/10/30\",\"vs\":\"RES1\"},"
                                    "{\"n\":\"/20/40\",\"vs\":\"RES2\"}]"));
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}
#endif // ANJAY_WITH_LWM2M11

AVS_UNIT_TEST(bootstrap_write, resource) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42", "514", "4"),
                    CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Hello"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0,
            (const anjay_iid_t[]) { 14, 42, 69, 514, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 514, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 514, 4, ANJAY_ID_INVALID,
                                         ANJAY_MOCK_DM_STRING(0, "Hello"), 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CHANGED, ID(0xFA3E), NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, resource_with_create) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42", "514", "4"),
                    CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Hello"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0,
            (const anjay_iid_t[]) { 14, 42, 69, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 514, 0);
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 514, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 514, 4, ANJAY_ID_INVALID,
                                         ANJAY_MOCK_DM_STRING(0, "Hello"), 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CHANGED, ID(0xFA3E), NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, resource_with_present_error) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42", "514", "4"),
                    CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Hello"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, -1,
            (const anjay_iid_t[]) { 14, 42, ANJAY_ID_INVALID });
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, INTERNAL_SERVER_ERROR,
                            ID(0xFA3E), NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, resource_with_create_error) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42", "514", "4"),
                    CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Hello"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0,
            (const anjay_iid_t[]) { 14, 42, 69, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 514, -1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, INTERNAL_SERVER_ERROR,
                            ID(0xFA3E), NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, resource_error) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42", "514", "7"),
                    CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Hello"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0,
            (const anjay_iid_t[]) { 14, 42, 69, 514, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 514, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, NOT_FOUND, ID(0xFA3E),
                            NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, resource_with_mismatched_tlv_rid) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42", "514", "4"),
                    CONTENT_FORMAT(OMA_LWM2M_TLV),
                    PAYLOAD("\xc5\x05"
                            "Hello"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0,
            (const anjay_iid_t[]) { 14, 42, 69, 514, ANJAY_ID_INVALID });
    // mismatched resource id, RID Uri-Path was 4 but in the payload it is 5
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, BAD_REQUEST, ID(0xFA3E),
                            NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, resource_error_with_create) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42", "514", "7"),
                    CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Hello"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0,
            (const anjay_iid_t[]) { 14, 42, 69, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 514, 0);
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 514, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    // TODO: should expect transaction_rollback here
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, NOT_FOUND, ID(0xFA3E),
                            NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, instance) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42", "69"),
                    CONTENT_FORMAT(OMA_LWM2M_TLV),
                    PAYLOAD("\xc1\x00\x0d\xc5\x06"
                            "Hello"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0,
            (const anjay_iid_t[]) { 14, 42, 69, 514, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 0, ANJAY_ID_INVALID,
                                         ANJAY_MOCK_DM_INT(0, 13), 0);
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 6, ANJAY_ID_INVALID,
                                         ANJAY_MOCK_DM_STRING(0, "Hello"), 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CHANGED, ID(0xFA3E), NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, instance_with_redundant_tlv_header) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42", "69"),
                    CONTENT_FORMAT(OMA_LWM2M_TLV),
                    PAYLOAD("\x08\x45\x08\xc6\x06"
                            "DDDDDD"));
    // Redundant (but consistent with the Uri-Path) \x08\x45
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0,
            (const anjay_iid_t[]) { 14, 42, 69, 514, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 6, ANJAY_ID_INVALID,
                                         ANJAY_MOCK_DM_STRING(0, "DDDDDD"), 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CHANGED, ID(0xFA3E), NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write,
              instance_with_redundant_and_incorrect_tlv_header) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42", "69"),
                    CONTENT_FORMAT(OMA_LWM2M_TLV),
                    PAYLOAD("\x08\x01\x08\xc6\x0a"
                            "DDDDDD"));
    // IID is 69 but TLV payload contains IID 1
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0,
            (const anjay_iid_t[]) { 14, 42, 69, 514, ANJAY_ID_INVALID });
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, BAD_REQUEST, ID(0xFA3E),
                            NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, instance_wrong_type) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42", "69"),
                    CONTENT_FORMAT(OMA_LWM2M_TLV),
                    PAYLOAD("\xc1\x00\x0d"
                            "\x05\x06"
                            "Hello"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0,
            (const anjay_iid_t[]) { 14, 42, 69, 514, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 0, ANJAY_ID_INVALID,
                                         ANJAY_MOCK_DM_INT(0, 13), 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, BAD_REQUEST, ID(0xFA3E),
                            NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, instance_error) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42", "69"),
                    CONTENT_FORMAT(OMA_LWM2M_TLV),
                    PAYLOAD("\xc1\x00\x0d"
                            "\xc5\x06"
                            "Hello"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0,
            (const anjay_iid_t[]) { 14, 42, 69, 514, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 0, ANJAY_ID_INVALID,
                                         ANJAY_MOCK_DM_INT(0, 13), -1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, INTERNAL_SERVER_ERROR,
                            ID(0xFA3E), NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, instance_some_unsupported) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42", "69"),
                    CONTENT_FORMAT(OMA_LWM2M_TLV),
                    PAYLOAD("\xc1\x00\x0d"
                            "\xc5\x07"
                            "Hello"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0,
            (const anjay_iid_t[]) { 14, 42, 69, 514, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 0, ANJAY_ID_INVALID,
                                         ANJAY_MOCK_DM_INT(0, 13), 0);
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CHANGED, ID(0xFA3E), NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, object) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42"),
                    CONTENT_FORMAT(OMA_LWM2M_TLV),
                    PAYLOAD("\x08\x45\x03" // IID == 69
                            "\xc1\x00\x2a" // RID == 0
                            "\x08\x2a\x03" // IID == 42
                            "\xc1\x03\x45" /* RID == 3 */));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 14, 42, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 69, 0);
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 0, ANJAY_ID_INVALID,
                                         ANJAY_MOCK_DM_INT(0, 42), 0);
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 14, 42, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 42, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 42, 3, ANJAY_ID_INVALID,
                                         ANJAY_MOCK_DM_INT(0, 69), 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CHANGED, ID(0xFA3E), NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, object_error) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42"),
                    CONTENT_FORMAT(OMA_LWM2M_TLV),
                    PAYLOAD("\x08\x45\x03" // IID == 69
                            "\xc1\x00\x2a" // RID == 0
                            "\x08\x2a\x03" // IID == 42
                            "\xc1\x03\x45" /* RID == 3 */));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 14, 42, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 69, 0);
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 0, ANJAY_ID_INVALID,
                                         ANJAY_MOCK_DM_INT(0, 42), 0);
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 14, 42, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 42, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 42, 3, ANJAY_ID_INVALID,
                                         ANJAY_MOCK_DM_INT(0, 69), -1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, INTERNAL_SERVER_ERROR,
                            ID(0xFA3E), NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, object_error_index_end) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42"),
                    CONTENT_FORMAT(OMA_LWM2M_TLV),
                    PAYLOAD("\x08\x45\x03" // IID == 69
                            "\xc1\x00\x2a" // RID == 0
                            "\x08\x2a\x03" // IID == 42
                            "\xc1\x03\x45" /* RID == 3 */));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 14, 42, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 69, 0);
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 0, ANJAY_ID_INVALID,
                                         ANJAY_MOCK_DM_INT(0, 42), 0);
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 14, 42, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 42, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 42, 3, ANJAY_ID_INVALID,
                                         ANJAY_MOCK_DM_INT(0, 69),
                                         ANJAY_GET_PATH_END);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, INTERNAL_SERVER_ERROR,
                            ID(0xFA3E), NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, object_wrong_type) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42"),
                    CONTENT_FORMAT(OMA_LWM2M_TLV),
                    PAYLOAD("\x08\x45\x03" // IID == 69
                            "\xc1\x00\x2a" // RID == 0
                            "\xc8\x2a\x03" // RID in place of IID == 42
                            "\xc1\x03\x45" /* RID == 3 */));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 14, 42, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 69, 0);
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 0, ANJAY_ID_INVALID,
                                         ANJAY_MOCK_DM_INT(0, 42), 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, BAD_REQUEST, ID(0xFA3E),
                            NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, object_not_found) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("43"),
                    CONTENT_FORMAT(OMA_LWM2M_TLV),
                    PAYLOAD("\x08\x45\x03" // IID == 69
                            "\xc1\x00\x2a" // RID == 0
                            "\x08\x2a\x03" // IID == 42
                            "\xc1\x03\x45" /* RID == 3 */));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, NOT_FOUND, ID(0xFA3E),
                            NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, object_missing) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E),
                    CONTENT_FORMAT(OMA_LWM2M_TLV),
                    PAYLOAD("\x08\x45\x03" // IID == 69
                            "\xc1\x00\x2a" // RID == 0
                            "\x08\x2a\x03" // IID == 42
                            "\xc1\x03\x45" /* RID == 3 */));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, METHOD_NOT_ALLOWED, ID(0xFA3E),
                            NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_write, multiple_resource_followed_by_single_resoure) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42", "69"),
                    CONTENT_FORMAT(OMA_LWM2M_TLV),
                    PAYLOAD("\x88\x15\x0e"
                            "\x45\x03"
                            "Hello"
                            "\x45\x07"
                            "world"
                            "\xe4\x01\xa4"
                            "test"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 69, ANJAY_ID_INVALID });
    // Write /42/69/21
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 21, ANJAY_DM_RES_RWM, ANJAY_DM_RES_PRESENT },
                    { 37, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    { 420, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_reset(anjay, &OBJ, 69, 21, 0);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 21, 3,
                                         ANJAY_MOCK_DM_STRING(0, "Hello"), 0);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 21, 7,
                                         ANJAY_MOCK_DM_STRING(0, "world"), 0);
    // Write /42/69/420
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 21, ANJAY_DM_RES_RWM, ANJAY_DM_RES_PRESENT },
                    { 37, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    { 420, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 420, ANJAY_ID_INVALID,
                                         ANJAY_MOCK_DM_STRING(0, "test"), 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CHANGED, ID(0xFA3E), NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, instance) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, DELETE, ID(0xFA3E), PATH("42", "34"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 34, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 34, 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, DELETED, ID(0xfa3e), NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, instance_missing) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, DELETE, ID(0xFA3E), PATH("42", "34"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { ANJAY_ID_INVALID });
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, DELETED, ID(0xfa3e), NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, instance_error) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, DELETE, ID(0xFA3E), PATH("42", "34"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 34, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 34, -1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, INTERNAL_SERVER_ERROR,
                            ID(0xfa3e), NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, instance_present_error) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, DELETE, ID(0xFA3E), PATH("42", "34"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, -1, (const anjay_iid_t[]) { 34, ANJAY_ID_INVALID });
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, INTERNAL_SERVER_ERROR,
                            ID(0xfa3e), NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, object) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, DELETE, ID(0xFA3E), PATH("42"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0,
            (const anjay_iid_t[]) { 34, 69, 514, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 34, 0);
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 69, 0);
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 514, 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, DELETED, ID(0xfa3e), NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, object_it_error) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, DELETE, ID(0xFA3E), PATH("42"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, -1,
            (const anjay_iid_t[]) { 34, 69, ANJAY_ID_INVALID });
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, INTERNAL_SERVER_ERROR,
                            ID(0xfa3e), NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, object_error) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, DELETE, ID(0xFA3E), PATH("42"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0,
            (const anjay_iid_t[]) { 34, 69, 514, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 34, 0);
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 69, -1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, INTERNAL_SERVER_ERROR,
                            ID(0xfa3e), NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, object_missing) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, DELETE, ID(0xFA3E), PATH("77"));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, DELETED, ID(0xfa3e), NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, everything) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, DELETE, ID(0xFA3E), NO_PAYLOAD);
    _anjay_mock_dm_expect_list_instances(
            anjay, &FAKE_SERVER, 0,
            (const anjay_iid_t[]) { 2, 3, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_instance_remove(anjay, &FAKE_SERVER, 2, 0);
    _anjay_mock_dm_expect_instance_remove(anjay, &FAKE_SERVER, 3, 0);
    _anjay_mock_dm_expect_list_instances(anjay, &OBJ_WITH_RESET, 0,
                                         (const anjay_iid_t[]) {
                                                 ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0,
            (const anjay_iid_t[]) { 34, 69, 514, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 34, 0);
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 69, 0);
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 514, 0);
    _anjay_mock_dm_expect_list_instances(
            anjay, (const anjay_dm_object_def_t *const *) &EXECUTE_OBJ, 0,
            (const anjay_iid_t[]) { ANJAY_ID_INVALID });
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, DELETED, ID(0xfa3e), NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, resource) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, DELETE, ID(0xFA3E),
                    PATH("42", "34", "7"));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, BAD_REQUEST, ID(0xfa3e),
                            NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_delete, bs) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, DELETE, ID(0xFA3E), PATH("bs"));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, BAD_REQUEST, ID(0xfa3e),
                            NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

static int fail_notify_perform(anjay_unlocked_t *anjay,
                               anjay_ssid_t origin_ssid,
                               anjay_notify_queue_t *queue_ptr) {
    (void) anjay;
    (void) origin_ssid;
    (void) queue_ptr;
    return -1;
}

AVS_UNIT_TEST(bootstrap_finish, error) {
    AVS_UNIT_MOCK(_anjay_notify_perform_without_servers) = fail_notify_perform;
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);

    // do some Write first to call notifications
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("42", "514", "4"),
                    CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Hello"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0,
            (const anjay_iid_t[]) { 14, 42, 69, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 514, 0);
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 514, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 514, 4, ANJAY_ID_INVALID,
                                         ANJAY_MOCK_DM_STRING(0, "Hello"), 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CHANGED, ID(0xFA3E), NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));

    // Bootstrap Finish
    DM_TEST_REQUEST(mocksocks[0], CON, POST, ID(0xFA3E), PATH("bs"));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, INTERNAL_SERVER_ERROR,
                            ID(0xFA3E), NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    AVS_UNIT_ASSERT_EQUAL(
            AVS_UNIT_MOCK_INVOCATIONS(_anjay_dm_call_instance_remove), 0);
    anjay_sched_run(anjay);
    AVS_UNIT_ASSERT_EQUAL(
            AVS_UNIT_MOCK_INVOCATIONS(_anjay_dm_call_instance_remove), 0);
    _anjay_mock_clock_advance(avs_time_duration_from_scalar(1, AVS_TIME_S));
    anjay_sched_run(anjay);
    // still not removing
    AVS_UNIT_ASSERT_EQUAL(
            AVS_UNIT_MOCK_INVOCATIONS(_anjay_dm_call_instance_remove), 0);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(bootstrap_invalid, invalid) {
    DM_TEST_INIT_WITH_SSIDS(ANJAY_SSID_BOOTSTRAP);
    DM_TEST_REQUEST(mocksocks[0], CON, POST, ID(0xFA3E), PATH("42"),
                    NO_PAYLOAD);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, METHOD_NOT_ALLOWED, ID(0xFA3E),
                            NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}
