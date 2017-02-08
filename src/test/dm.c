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

#include <math.h>

#include <avsystem/commons/unit/mocksock.h>
#include <avsystem/commons/unit/test.h>

#include <anjay_test/dm.h>

#include "../anjay.h"
#include "../io/vtable.h"
#include "mock_coap_stream_impl.h"

AVS_UNIT_TEST(debug, debug_make_path_macro) {
    anjay_request_details_t details;
    details.oid = 0;
    details.iid = 1;
    details.rid = 2;
    details.has_iid = false;
    details.has_rid = false;

    AVS_UNIT_ASSERT_EQUAL_STRING(ANJAY_DEBUG_MAKE_PATH(&details), "/0");
    details.has_iid = true;
    AVS_UNIT_ASSERT_EQUAL_STRING(ANJAY_DEBUG_MAKE_PATH(&details), "/0/1");
    details.has_rid = true;
    AVS_UNIT_ASSERT_EQUAL_STRING(ANJAY_DEBUG_MAKE_PATH(&details), "/0/1/2");

    details.oid = 65535;
    details.iid = 65535;
    details.rid = 65535;
    AVS_UNIT_ASSERT_EQUAL_STRING(ANJAY_DEBUG_MAKE_PATH(&details),
                                 "/65535/65535/65535");
}

AVS_UNIT_TEST(dm_read, resource) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "69" // IID
            "\x01" "4"; // RID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_INT(0, 514));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0],
            "\x60\x45\xFA\x3E" // CoAP header
            "\xc0" // Content-Format
            "\xff" "514");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_read, resource_read_err_concrete) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "69" // IID
            "\x01" "4"; // RID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, ANJAY_ERR_UNAUTHORIZED,
                                        ANJAY_MOCK_DM_NONE);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x81\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_read, resource_read_err_generic) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "69" // IID
            "\x01" "4"; // RID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, -1,
                                        ANJAY_MOCK_DM_NONE);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\xA0\xFA\x3E");
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_read, resource_not_found_because_unsupported) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "69" // IID
            "\x01" "4"; // RID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x84\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_read, resource_not_found_because_not_present) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "69" // IID
            "\x01" "4"; // RID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x84\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_read, resource_out_of_bounds) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "69" // IID
            "\x03" "514"; // RID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x84\xFA\x3E"); // CoAP header
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_read, instance_empty) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "13"; // IID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 13, 1);
    for (anjay_rid_t i = 0; i <= 6; ++i) {
        _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, i, 1);
        _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 13, i, 0);
    }
    DM_TEST_EXPECT_RESPONSE(mocksocks[0],
            "\x60\x45\xFA\x3E" // CoAP header
            "\xc2\x2d\x16"); // Content-Format
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_read, instance_some) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "13"; // IID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 13, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 0, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 13, 0, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 13, 0, 0,
                                        ANJAY_MOCK_DM_INT(0, 69));
    for (anjay_rid_t i = 1; i <= 5; ++i) {
        _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, i, 1);
        _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 13, i, 0);
    }
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 6, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 13, 6, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 13, 6, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Hello"));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0],
            "\x60\x45\xFA\x3E" // CoAP header
            "\xc2\x2d\x16" // Content-Format
            "\xff"
            "\xc1\x00\x45"
            "\xc5\x06" "Hello");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_read, instance_resource_doesnt_support_read) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "13"; // IID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 13, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 0, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 13, 0, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 13, 0,
                                        ANJAY_ERR_METHOD_NOT_ALLOWED,
                                        ANJAY_MOCK_DM_NONE);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 1, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 13, 1, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 13, 1, 0,
                                        ANJAY_MOCK_DM_INT(0, 69));
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 2, 0);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 3, 0);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 0);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 5, 0);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 6, 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0],
            "\x60\x45\xFA\x3E" // CoAP header
            "\xc2\x2d\x16" // Content-Format
            "\xff"
            "\xc1\x01\x45");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_read, instance_not_found) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "13"; // IID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 13, 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x84\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_read, instance_err_concrete) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "13"; // IID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 13, ANJAY_ERR_UNAUTHORIZED);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x81\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_read, instance_err_generic) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "13"; // IID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 13, -1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\xA0\xFA\x3E");
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_read, object_empty) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42"; // OID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 0, 0, ANJAY_IID_INVALID);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0],
            "\x60\x45\xFA\x3E" // CoAP header
            "\xc2\x2d\x16"); // Content-Format
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_read, object_not_found) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB1" "3"; // OID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x84\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_read, object_some) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42"; // OID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 0, 0, 3);
    for (anjay_iid_t rid = 0; rid <= 6; ++rid) {
        _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, rid, 1);
        _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 3, rid, 0);
    }
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 1, 0, 7);
    for (anjay_rid_t rid = 0; rid <= 6; ++rid) {
        _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, rid, 1);
        _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 7, rid, 0);
    }
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 2, 0, ANJAY_IID_INVALID);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0],
            "\x60\x45\xFA\x3E" // CoAP header
            "\xc2\x2d\x16" // Content-Format
            "\xff\x00\x03\x00\x07");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_read, object_err_concrete) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42"; // OID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 0, ANJAY_ERR_UNAUTHORIZED, 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x81\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_read, object_err_generic) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42"; // OID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 0, -1, 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\xA0\xFA\x3E");
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_read, no_object) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E"; // CoAP header
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x80\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_read_accept, force_tlv) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "69" // IID
            "\x01" "4" // RID
            "\x62\x2d\x16"; // Accept
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_INT(0, 514));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0],
            "\x60\x45\xFA\x3E" // CoAP header
            "\xc2\x2d\x16" // Content-Format
            "\xff" "\xc2\x04\x02\x02");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_read_accept, force_text_ok) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "69" // IID
            "\x01" "4" // RID
            "\x60"; // Accept
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_INT(0, 514));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0],
            "\x60\x45\xFA\x3E" // CoAP header
            "\xc0" // Content-Format
            "\xff" "514");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_read_accept, force_text_on_bytes) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "69" // IID
            "\x01" "4" // RID
            "\x60"; // Accept
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_BYTES(0, "bytes"));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0],
            "\x60\x45\xFA\x3E" // CoAP header
            "\xc0" // Content-Format
            "\xff" "Ynl0ZXM=");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_read_accept, force_text_invalid) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "69" // IID, no RID
            "\x60"; // Accept
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x86\xFA\x3E"); // CoAP header
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_read_accept, force_opaque_ok) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "69" // IID
            "\x01" "4" // RID
            "\x61\x2a"; // Accept
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_BYTES(0, "bytes"));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0],
            "\x60\x45\xFA\x3E" // CoAP header
            "\xc1\x2a" // Content-Format
            "\xff" "bytes");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_read_accept, force_opaque_mismatch) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "69" // IID
            "\x01" "4" // RID
            "\x61\x2a"; // Accept
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, -1,
                                        ANJAY_MOCK_DM_INT(-1, 514));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x86\xFA\x3E"); // CoAP header
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_read_accept, force_opaque_invalid) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "69" // IID, no RID
            "\x61\x2a"; // Accept
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x86\xFA\x3E"); // CoAP header
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_read_accept, invalid_format) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "69" // IID
            "\x01" "4" // RID
            "\x62\x42\x42"; // Accept
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x86\xFA\x3E"); // CoAP header
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_write, resource) {
    DM_TEST_INIT;
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

AVS_UNIT_TEST(dm_write, resource_invalid_format) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x03" "514" // IID
            "\x01" "4" // RID
            "\x12\x42\x42" // Content-Format
            "\xFF"
            "Hello";
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x80\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_write, instance) {
    DM_TEST_INIT;
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

AVS_UNIT_TEST(dm_write, instance_invalid_format) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "69" // IID
            "\x12\x42\x42" // Content-Format
            "\xFF"
            "\xc1\x00\x0d"
            "\xc5\x06" "Hello";
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x80\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_write, instance_partial) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x02\xFA\x3E" // CoAP header
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
                                         ANJAY_MOCK_DM_STRING(0, "Hello"),
                                         0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x44\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_write, instance_full) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB2" "25" // OID
            "\x02" "69" // IID
            "\x12\x2d\x16"
            "\xFF"
            "\xc1\x00\x0d"
            "\xc5\x06" "Hello";
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ_WITH_RESET, 69, 1);
    _anjay_mock_dm_expect_instance_reset(anjay, &OBJ_WITH_RESET, 69, 0);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ_WITH_RESET, 0, 1);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ_WITH_RESET, 69, 0,
                                         ANJAY_MOCK_DM_INT(0, 13), 0);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ_WITH_RESET, 6, 1);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ_WITH_RESET, 69, 6,
                                         ANJAY_MOCK_DM_STRING(0, "Hello"),
                                         0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x44\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_write, instance_superfluous_instance) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "69" // IID
            "\x12\x2d\x16"
            "\xFF"
            "\x08\x45\x0a"
            "\xc1\x00\x0d"
            "\xc5\x06" "Hello";
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x80\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_write, instance_wrong_type) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "69" // IID
            "\x12\x2d\x16"
            "\xFF"
            "\x01\x00\x0d"
            "\xc5\x06" "Hello";
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x80\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_write, instance_nonexistent) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "69" // IID
            "\x12\x2d\x16"
            "\xFF"
            "\xc1\x00\x0d"
            "\xc5\x06" "Hello";
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x84\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_write, no_instance) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x12\x2d\x16"
            "\xFF"
            "\x08\x45\x0a"
            "\xc1\x00\x0d"
            "\xc5\x06" "Hello";
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x85\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_execute, success) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x02\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x03" "514" // IID
            "\x01" "4"; // RID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 514, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 514, 4, 1);
    _anjay_mock_dm_expect_resource_execute(anjay, &OBJ, 514, 4,
                                           ANJAY_MOCK_DM_NONE, 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x44\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_execute, data) {
    DM_TEST_INIT;
#define NYANCAT "Nyanyanyanyanyanyanya!"
    static const char REQUEST[] =
            "\x40\x02\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x03" "514" // IID
            "\x01" "4" // RID
            "\xFF" NYANCAT;
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 514, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 514, 4, 1);
    _anjay_mock_dm_expect_resource_execute(anjay, &OBJ, 514, 4,
                                           ANJAY_MOCK_DM_STRING(0, NYANCAT), 0);
#undef NYANCAT
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x44\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_execute, error) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x02\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x03" "514" // IID
            "\x01" "4"; // RID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 514, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 514, 4, 1);
    _anjay_mock_dm_expect_resource_execute(anjay, &OBJ, 514, 4,
                                           ANJAY_MOCK_DM_NONE,
                                           ANJAY_ERR_CONFLICT);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x89\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_execute, resource_out_of_bounds) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x02\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x03" "514" // IID
            "\x02" "17"; // RID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 514, 1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x84\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_execute, resource_inexistent) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x02\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x03" "514" // IID
            "\x01" "1"; // RID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 514, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 1, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 514, 1, 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x84\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_execute, instance_inexistent) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x02\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x03" "666" // IID
            "\x01" "1"; // RID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 666, 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x84\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

static int execute_get_arg_value_invalid_args(anjay_t *anjay,
                                              const anjay_dm_object_def_t *const *obj_ptr,
                                              anjay_iid_t iid,
                                              anjay_rid_t rid,
                                              anjay_execute_ctx_t *ctx) {
    (void)iid; (void)rid; (void)anjay; (void)obj_ptr;
    int ret;
    int arg;
    bool has_value;

    ret = anjay_execute_get_next_arg(ctx, &arg, &has_value);
    AVS_UNIT_ASSERT_EQUAL(ret, 0);
    AVS_UNIT_ASSERT_EQUAL(arg, 0);
    AVS_UNIT_ASSERT_EQUAL(has_value, true);

    char buf[32];
    // buf_size < 2
    ssize_t read_bytes = anjay_execute_get_arg_value(ctx, buf, 1);
    AVS_UNIT_ASSERT_EQUAL(read_bytes, -1);

    // buf == NULL
    read_bytes = anjay_execute_get_arg_value(ctx, NULL, 974);
    AVS_UNIT_ASSERT_EQUAL(read_bytes, -1);
    return 0;
}

AVS_UNIT_TEST(dm_execute, execute_get_arg_value_invalid_args) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x02\xFA\x3E" // CoAP header
            "\xB3" "128" // OID
            "\x03" "514" // IID
            "\x01" "1" // RID
            "\xFF" "0='foobarbaz'";

    EXECUTE_OBJ->resource_execute = execute_get_arg_value_invalid_args;
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay,
        (const anjay_dm_object_def_t *const *) &EXECUTE_OBJ, 514, 1);
    _anjay_mock_dm_expect_resource_supported(anjay,
        (const anjay_dm_object_def_t *const *) &EXECUTE_OBJ, 1, 1);
    _anjay_mock_dm_expect_resource_present(anjay,
        (const anjay_dm_object_def_t *const *) &EXECUTE_OBJ, 514, 1, 1);

    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x44\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

static int valid_args_execute(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t iid,
                              anjay_rid_t rid,
                              anjay_execute_ctx_t *ctx) {
    (void)iid; (void)rid; (void)anjay; (void)obj_ptr;
    int ret;
    int arg;
    bool has_value;

    ret = anjay_execute_get_next_arg(ctx, &arg, &has_value);
    AVS_UNIT_ASSERT_EQUAL(ret, 0);
    AVS_UNIT_ASSERT_EQUAL(arg, 0);
    AVS_UNIT_ASSERT_EQUAL(has_value, false);

    ret = anjay_execute_get_next_arg(ctx, &arg, &has_value);
    AVS_UNIT_ASSERT_EQUAL(ret, 0);
    AVS_UNIT_ASSERT_EQUAL(arg, 1);
    AVS_UNIT_ASSERT_EQUAL(has_value, false);

    ret = anjay_execute_get_next_arg(ctx, &arg, &has_value);
    AVS_UNIT_ASSERT_EQUAL(ret, 0);
    AVS_UNIT_ASSERT_EQUAL(arg, 2);
    AVS_UNIT_ASSERT_EQUAL(has_value, false);

    ret = anjay_execute_get_next_arg(ctx, &arg, &has_value);
    AVS_UNIT_ASSERT_EQUAL(ret, 1);
    AVS_UNIT_ASSERT_EQUAL(arg, -1);
    AVS_UNIT_ASSERT_EQUAL(has_value, false);
    return 0;
}

AVS_UNIT_TEST(dm_execute, valid_args) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x02\xFA\x3E" // CoAP header
            "\xB3" "128" // OID
            "\x03" "514" // IID
            "\x01" "1" // RID
            "\xFF" "0,1,2";

    EXECUTE_OBJ->resource_execute = valid_args_execute;
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay,
        (const anjay_dm_object_def_t *const *) &EXECUTE_OBJ, 514, 1);
    _anjay_mock_dm_expect_resource_supported(anjay,
        (const anjay_dm_object_def_t *const *) &EXECUTE_OBJ, 1, 1);
    _anjay_mock_dm_expect_resource_present(anjay,
        (const anjay_dm_object_def_t *const *) &EXECUTE_OBJ, 514, 1, 1);

    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x44\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}


static int valid_args_with_values_execute(anjay_t *anjay,
                                          const anjay_dm_object_def_t *const *obj_ptr,
                                          anjay_iid_t iid,
                                          anjay_rid_t rid,
                                          anjay_execute_ctx_t *ctx) {
    (void)iid; (void)rid; (void)anjay; (void)obj_ptr;
    int ret;
    int arg;
    bool has_value;

    ret = anjay_execute_get_next_arg(ctx, &arg, &has_value);
    AVS_UNIT_ASSERT_EQUAL(ret, 0);
    AVS_UNIT_ASSERT_EQUAL(arg, 0);
    AVS_UNIT_ASSERT_EQUAL(has_value, false);

    ret = anjay_execute_get_next_arg(ctx, &arg, &has_value);
    AVS_UNIT_ASSERT_EQUAL(ret, 0);
    AVS_UNIT_ASSERT_EQUAL(arg, 1);
    AVS_UNIT_ASSERT_EQUAL(has_value, true);

    char buf[32];
    ssize_t read_bytes = anjay_execute_get_arg_value(ctx, buf, 32);
    AVS_UNIT_ASSERT_EQUAL(read_bytes, strlen("value"));
    AVS_UNIT_ASSERT_EQUAL_STRING(buf, "value");
    /* Already read everything. */
    AVS_UNIT_ASSERT_EQUAL(anjay_execute_get_arg_value(ctx, buf, 32), 0);

    ret = anjay_execute_get_next_arg(ctx, &arg, &has_value);
    AVS_UNIT_ASSERT_EQUAL(ret, 0);
    AVS_UNIT_ASSERT_EQUAL(arg, 2);
    AVS_UNIT_ASSERT_EQUAL(has_value, false);

    ret = anjay_execute_get_next_arg(ctx, &arg, &has_value);
    AVS_UNIT_ASSERT_EQUAL(ret, 1);
    AVS_UNIT_ASSERT_EQUAL(arg, -1);
    AVS_UNIT_ASSERT_EQUAL(has_value, false);
    return 0;
}

AVS_UNIT_TEST(dm_execute, valid_args_with_values) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x02\xFA\x3E" // CoAP header
            "\xB3" "128" // OID
            "\x03" "514" // IID
            "\x01" "1" // RID
            "\xFF" "0,1='value',2";

    EXECUTE_OBJ->resource_execute = valid_args_with_values_execute;
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay,
        (const anjay_dm_object_def_t *const *) &EXECUTE_OBJ, 514, 1);
    _anjay_mock_dm_expect_resource_supported(anjay,
        (const anjay_dm_object_def_t *const *) &EXECUTE_OBJ, 1, 1);
    _anjay_mock_dm_expect_resource_present(anjay,
        (const anjay_dm_object_def_t *const *) &EXECUTE_OBJ, 514, 1, 1);

    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x44\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

static int valid_values_partial_read_execute(anjay_t *anjay,
                                             const anjay_dm_object_def_t *const *obj_ptr,
                                             anjay_iid_t iid,
                                             anjay_rid_t rid,
                                             anjay_execute_ctx_t *ctx) {
    (void)iid; (void)rid; (void)anjay; (void)obj_ptr;
    int ret;
    int arg;
    bool has_value;

    ret = anjay_execute_get_next_arg(ctx, &arg, &has_value);
    AVS_UNIT_ASSERT_EQUAL(ret, 0);
    AVS_UNIT_ASSERT_EQUAL(arg, 1);
    AVS_UNIT_ASSERT_EQUAL(has_value, true);

    char buf[32];
    /* Read in 2 parts. */
    ssize_t read_bytes = anjay_execute_get_arg_value(ctx, buf, 5);
    AVS_UNIT_ASSERT_EQUAL(read_bytes, strlen("very"));
    AVS_UNIT_ASSERT_EQUAL_STRING(buf, "very");
    read_bytes = anjay_execute_get_arg_value(ctx, buf, 32);
    AVS_UNIT_ASSERT_EQUAL(read_bytes, strlen("longvalue"));
    AVS_UNIT_ASSERT_EQUAL_STRING(buf, "longvalue");

    ret = anjay_execute_get_next_arg(ctx, &arg, &has_value);
    AVS_UNIT_ASSERT_EQUAL(ret, 1);
    AVS_UNIT_ASSERT_EQUAL(arg, -1);
    AVS_UNIT_ASSERT_EQUAL(has_value, false);
    return 0;
}

AVS_UNIT_TEST(dm_execute, valid_values_partial_read) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x02\xFA\x3E" // CoAP header
            "\xB3" "128" // OID
            "\x03" "514" // IID
            "\x01" "1" // RID
            "\xFF" "1='verylongvalue'";

    EXECUTE_OBJ->resource_execute = valid_values_partial_read_execute;
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay,
        (const anjay_dm_object_def_t *const *) &EXECUTE_OBJ, 514, 1);
    _anjay_mock_dm_expect_resource_supported(anjay,
        (const anjay_dm_object_def_t *const *) &EXECUTE_OBJ, 1, 1);
    _anjay_mock_dm_expect_resource_present(anjay,
        (const anjay_dm_object_def_t *const *) &EXECUTE_OBJ, 514, 1, 1);

    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x44\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

static int valid_values_skipping_execute(anjay_t *anjay,
                                         const anjay_dm_object_def_t *const *obj_ptr,
                                         anjay_iid_t iid,
                                         anjay_rid_t rid,
                                         anjay_execute_ctx_t *ctx) {
    (void)iid; (void)rid; (void)anjay; (void)obj_ptr;
    int ret;
    int arg;
    bool has_value;

    ret = anjay_execute_get_next_arg(ctx, &arg, &has_value);
    AVS_UNIT_ASSERT_EQUAL(ret, 0);
    AVS_UNIT_ASSERT_EQUAL(arg, 1);
    AVS_UNIT_ASSERT_EQUAL(has_value, true);

    char buf[2];
    ssize_t bytes_read = anjay_execute_get_arg_value(ctx, buf, 2);
    AVS_UNIT_ASSERT_EQUAL(bytes_read, 1);
    /* Don't care about the rest, ignore. */
    ret = anjay_execute_get_next_arg(ctx, &arg, &has_value);
    AVS_UNIT_ASSERT_EQUAL(ret, 0);
    AVS_UNIT_ASSERT_EQUAL(arg, 2);
    AVS_UNIT_ASSERT_EQUAL(has_value, false);

    ret = anjay_execute_get_next_arg(ctx, &arg, &has_value);
    AVS_UNIT_ASSERT_EQUAL(ret, 0);
    AVS_UNIT_ASSERT_EQUAL(arg, 3);
    AVS_UNIT_ASSERT_EQUAL(has_value, false);

    bytes_read = anjay_execute_get_arg_value(ctx, buf, 2);
    AVS_UNIT_ASSERT_EQUAL(bytes_read, 0);

    ret = anjay_execute_get_next_arg(ctx, &arg, &has_value);
    AVS_UNIT_ASSERT_EQUAL(ret, 1);
    AVS_UNIT_ASSERT_EQUAL(arg, -1);
    AVS_UNIT_ASSERT_EQUAL(has_value, false);

    return 0;
}

AVS_UNIT_TEST(dm_execute, valid_values_skipping) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x02\xFA\x3E" // CoAP header
            "\xB3" "128" // OID
            "\x03" "514" // IID
            "\x01" "1" // RID
            "\xFF" "1='verylongvalue',2,3";

    EXECUTE_OBJ->resource_execute = valid_values_skipping_execute;
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay,
        (const anjay_dm_object_def_t *const *) &EXECUTE_OBJ, 514, 1);
    _anjay_mock_dm_expect_resource_supported(anjay,
        (const anjay_dm_object_def_t *const *) &EXECUTE_OBJ, 1, 1);
    _anjay_mock_dm_expect_resource_present(anjay,
        (const anjay_dm_object_def_t *const *) &EXECUTE_OBJ, 514, 1, 1);

    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x44\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

static int invalid_input_execute(anjay_t *anjay,
                                       const anjay_dm_object_def_t *const *obj_ptr,
                                       anjay_iid_t iid,
                                       anjay_rid_t rid,
                                       anjay_execute_ctx_t *ctx) {
    (void)iid; (void)rid; (void)anjay; (void)obj_ptr;
    int ret;
    int arg;
    bool has_value;

    do {
        ret = anjay_execute_get_next_arg(ctx, &arg, &has_value);
        if (ret < 0) {
            /* This is a failure, but I don't understand any of these hexes, so
               I return success on every failure and vice versa. */
            return 0;
        }
    } while (!ret);

    /* Should not happen */
    return -1;
}

AVS_UNIT_TEST(dm_execute, invalid_input) {
    static const char request_pattern[] =
            "\x40\x02\xFA\x3E" // CoAP header
            "\xB3" "128" // OID
            "\x03" "514" // IID
            "\x01" "1"  // RID
            "\xFF";

    static const char* invalid_inputs[] = {
        "a",
        "0=",
        "0=1,2,3",
        "0='val,1",
        "0='val',1='val',3'',4",
        "=",
        "11",
        "0='val',11",
        "0='val"
    };

    EXECUTE_OBJ->resource_execute = invalid_input_execute;
    for (size_t i = 0; i < ANJAY_ARRAY_SIZE(invalid_inputs); i++) {
        DM_TEST_INIT;
        size_t input_len = strlen(invalid_inputs[i]);
        size_t pattern_len = sizeof(request_pattern) - 1;
        size_t request_len = pattern_len + input_len;
        char request[request_len];
        memcpy(request, request_pattern, pattern_len);
        memcpy(request + pattern_len, invalid_inputs[i], input_len);

        avs_unit_mocksock_input(mocksocks[0], request, request_len);
        _anjay_mock_dm_expect_instance_present(anjay,
                (const anjay_dm_object_def_t *const *)&EXECUTE_OBJ, 514, 1);
        _anjay_mock_dm_expect_resource_supported(anjay,
                (const anjay_dm_object_def_t *const *)&EXECUTE_OBJ, 1, 1);
         _anjay_mock_dm_expect_resource_present(anjay,
                (const anjay_dm_object_def_t *const *)&EXECUTE_OBJ, 514, 1, 1);
        DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x44\xFA\x3E");
        AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
        DM_TEST_FINISH;
    }
}

static int valid_input_execute(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid,
                               anjay_rid_t rid,
                               anjay_execute_ctx_t *ctx) {
    (void)iid; (void)rid; (void)anjay; (void)obj_ptr;
    int ret;
    int arg;
    bool has_value;

    do {
        ret = anjay_execute_get_next_arg(ctx, &arg, &has_value);
    } while (!ret);

    return ret < 0 ? -1 : 0;
}

AVS_UNIT_TEST(dm_execute, valid_input) {
    static const char request_pattern[] =
            "\x40\x02\xFA\x3E" // CoAP header
            "\xB3" "128" // OID
            "\x03" "514" // IID
            "\x01" "1"; // RID

    static const char* valid_inputs[] = {
        "",
        "\xFF" "0='ala'",
        "\xFF" "2='10.3'",
        "\xFF" "7,0='https://www.oma.org'",
        "\xFF" "0,1,2,3,4"
    };

    EXECUTE_OBJ->resource_execute = valid_input_execute;
    for (size_t i = 0; i < ANJAY_ARRAY_SIZE(valid_inputs); i++) {
        DM_TEST_INIT;
        size_t input_len = strlen(valid_inputs[i]);
        size_t pattern_len = sizeof(request_pattern) - 1;
        size_t request_len = pattern_len + input_len;
        char request[request_len];
        memcpy(request, request_pattern, pattern_len);
        memcpy(request + pattern_len, valid_inputs[i], input_len);

        avs_unit_mocksock_input(mocksocks[0], request, request_len);
        _anjay_mock_dm_expect_instance_present(anjay,
                (const anjay_dm_object_def_t *const *)&EXECUTE_OBJ, 514, 1);
        _anjay_mock_dm_expect_resource_supported(anjay,
                (const anjay_dm_object_def_t *const *)&EXECUTE_OBJ, 1, 1);
        _anjay_mock_dm_expect_resource_present(anjay,
                (const anjay_dm_object_def_t *const *)&EXECUTE_OBJ, 514, 1, 1);
        DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x44\xFA\x3E");
        AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
        DM_TEST_FINISH;
    }
}

AVS_UNIT_TEST(dm_write_attributes, resource) {
    DM_TEST_INIT_WITH_SSIDS(77);
    static const char REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x03" "514" // IID
            "\x01" "4" // RID
            "\x47" "pmin=42"
            "\x06" "st=0.7";
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 514, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 514, 4, 1);
    _anjay_mock_dm_expect_resource_read_attrs(anjay, &OBJ, 514, 4, 77, 0,
                                              &ANJAY_DM_ATTRIBS_EMPTY);
    _anjay_mock_dm_expect_resource_write_attrs(anjay, &OBJ, 514, 4, 77,
            &(const anjay_dm_attributes_t) {
                .min_period = 42,
                .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = 0.7
            }, 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x44\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_write_attributes, instance) {
    DM_TEST_INIT_WITH_SSIDS(42);
    static const char REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "77" // IID
            "\x47" "pmin=69";
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 77, 1);
    _anjay_mock_dm_expect_instance_read_default_attrs(anjay, &OBJ, 77, 42, 0,
                                                      &ANJAY_DM_ATTRIBS_EMPTY);
    _anjay_mock_dm_expect_instance_write_default_attrs(anjay, &OBJ, 77, 42,
            &(const anjay_dm_attributes_t) {
                .min_period = 69,
                .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            }, 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x44\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_write_attributes, object) {
    DM_TEST_INIT_WITH_SSIDS(666);
    static const char REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x48" "pmax=514";
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_object_read_default_attrs(anjay, &OBJ, 666, 0,
                                                    &ANJAY_DM_ATTRIBS_EMPTY);
    _anjay_mock_dm_expect_object_write_default_attrs(anjay, &OBJ, 666,
            &(const anjay_dm_attributes_t) {
                .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                .max_period = 514,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            }, 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x44\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_write_attributes, no_resource) {
    DM_TEST_INIT_WITH_SSIDS(1);
    static const char REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x01" "2" // IID
            "\x01" "3" // RID
            "\x47" "pmin=42";
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 2, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 3, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 2, 3, 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x84\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_write_attributes, no_instance) {
    DM_TEST_INIT_WITH_SSIDS(4);
    static const char REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x01" "5" // IID
            "\x01" "6" // RID
            "\x47" "pmin=42";
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 5, 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x84\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_discover, resource) {
    DM_TEST_INIT_WITH_SSIDS(7);
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "69" // IID
            "\x01" "4" // RID
            "\x61\x28"; // Accept: application/link-format
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_dim(anjay, &OBJ, 69, 4,
                                       ANJAY_DM_DIM_INVALID);
    _anjay_mock_dm_expect_resource_read_attrs(anjay, &OBJ, 69, 4, 7, 0,
            &(const anjay_dm_attributes_t) {
                .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                .max_period = 514,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = 6.46,
                .step = ANJAY_ATTRIB_VALUE_NONE
            });

    _anjay_mock_dm_expect_instance_read_default_attrs(anjay, &OBJ, 69, 7, 0,
            &(const anjay_dm_attributes_t) {
                .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            });

    _anjay_mock_dm_expect_object_read_default_attrs(anjay, &OBJ, 7, 0,
            &(const anjay_dm_attributes_t) {
                .min_period = 10,
                .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            });

    DM_TEST_EXPECT_RESPONSE(mocksocks[0],
            "\x60\x45\xfa\x3e" // CoAP header
            "\xc1\x28" // Content-Format: application/link-format
            "\xff" "</42/69/4>;pmin=10;pmax=514;lt=6.46");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_discover, resource_multiple_servers) {
    DM_TEST_INIT_WITH_SSIDS(34, 45);
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "69" // IID
            "\x01" "4" // RID
            "\x61\x28"; // Accept: application/link-format
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_dim(anjay, &OBJ, 69, 4, 54);
    _anjay_mock_dm_expect_resource_read_attrs(anjay, &OBJ, 69, 4, 34, 0,
            &(const anjay_dm_attributes_t) {
                .min_period = 10,
                .max_period = 514,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = 6.46,
                .step = ANJAY_ATTRIB_VALUE_NONE
            });
    _anjay_mock_dm_expect_instance_read_default_attrs(anjay, &OBJ, 69, 34, 0,
                                                      &ANJAY_DM_ATTRIBS_EMPTY);
    _anjay_mock_dm_expect_object_read_default_attrs(anjay, &OBJ, 34, 0,
                                                    &ANJAY_DM_ATTRIBS_EMPTY);

    DM_TEST_EXPECT_RESPONSE(mocksocks[0],
            "\x60\x45\xfa\x3e" // CoAP header
            "\xc1\x28" // Content-Format: application/link-format
            "\xff"
            "</42/69/4>;dim=54;pmin=10;pmax=514;lt=6.46");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_discover, instance) {
    DM_TEST_INIT_WITH_SSIDS(69);
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x03" "514" // IID
            "\x61\x28"; // Accept: application/link-format
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 514, 1);

    _anjay_mock_dm_expect_instance_read_default_attrs(anjay, &OBJ, 514, 69, 0,
            &(const anjay_dm_attributes_t) {
                .min_period = 666,
                .max_period = 777,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            });

    for (anjay_rid_t rid = 0; rid < OBJ->rid_bound; rid++) {
        if (rid > 1) {
            _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, rid, 1);
            _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 514, rid, 0);
        } else {
            _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, rid, 1);
            _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 514, rid, 1);
            anjay_dm_attributes_t attrs;
            memset(&attrs, 0, sizeof(anjay_dm_attributes_t));
            attrs.max_period = ANJAY_ATTRIB_PERIOD_NONE;
            attrs.min_period = ANJAY_ATTRIB_PERIOD_NONE;
            attrs.greater_than = (double)rid;
            attrs.less_than = ANJAY_ATTRIB_VALUE_NONE;
            attrs.step = ANJAY_ATTRIB_VALUE_NONE;
            _anjay_mock_dm_expect_resource_dim(anjay, &OBJ, 514, rid, ANJAY_DM_DIM_INVALID);
            _anjay_mock_dm_expect_resource_read_attrs(anjay, &OBJ, 514, rid, 69,
                                                      0, &attrs);
        }
    }

    DM_TEST_EXPECT_RESPONSE(mocksocks[0],
            "\x60\x45\xfa\x3e" // CoAP header
            "\xc1\x28" // Content-Format: application/link-format
            "\xff" "</42/514>;pmin=666;pmax=777,</42/514/0>;gt=0,</42/514/1>;gt=1");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_discover, instance_multiple_servers) {
    DM_TEST_INIT_WITH_SSIDS(69, 96);
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x03" "514" // IID
            "\x61\x28"; // Accept: application/link-format
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 514, 1);

    _anjay_mock_dm_expect_instance_read_default_attrs(anjay, &OBJ, 514, 69, 0,
            &(const anjay_dm_attributes_t) {
                .min_period = 666,
                .max_period = 777,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            });

    for (anjay_rid_t rid = 0; rid < OBJ->rid_bound; rid++) {
        if (rid > 1) {
            _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, rid, 1);
            _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 514, rid, 0);
        } else {
            _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, rid, 1);
            _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 514, rid, 1);
            anjay_dm_attributes_t attrs;
            memset(&attrs, 0, sizeof(anjay_dm_attributes_t));
            attrs.max_period = ANJAY_ATTRIB_PERIOD_NONE;
            attrs.min_period = ANJAY_ATTRIB_PERIOD_NONE;
            attrs.greater_than = (double)rid;
            attrs.less_than = ANJAY_ATTRIB_VALUE_NONE;
            attrs.step = ANJAY_ATTRIB_VALUE_NONE;
            _anjay_mock_dm_expect_resource_dim(anjay, &OBJ, 514, rid, ANJAY_DM_DIM_INVALID);
            _anjay_mock_dm_expect_resource_read_attrs(anjay, &OBJ, 514, rid, 69,
                                                      0, &attrs);
        }
    }

    DM_TEST_EXPECT_RESPONSE(mocksocks[0],
            "\x60\x45\xfa\x3e" // CoAP header
            "\xc1\x28" // Content-Format: application/link-format
            "\xff" "</42/514>;pmin=666;pmax=777,"
                   "</42/514/0>;gt=0,"
                   "</42/514/1>;gt=1");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_discover, object) {
    DM_TEST_INIT_WITH_SSIDS(2);
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x61\x28"; // Accept: application/link-format
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_object_read_default_attrs(anjay, &OBJ, 2, 0,
            &(const anjay_dm_attributes_t) {
                .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                .max_period = 514,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            });

    int presence[][7] = {
        { 1, 0, 0, 1, 1, 0, 1 },
        { 0, 0, 0, 0, 1, 1, 1 }
    };
    const size_t ITERATIONS = sizeof(presence) / sizeof(presence[0]);
    for (anjay_iid_t iid = 0; iid < ITERATIONS; ++iid) {
        _anjay_mock_dm_expect_instance_it(anjay, &OBJ, iid, 0, iid);

        for (anjay_rid_t rid = 0; rid < 7; ++rid) {
            _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, rid, 1);
            _anjay_mock_dm_expect_resource_present(anjay, &OBJ, iid, rid, presence[iid][rid]);
        }
    }
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, ITERATIONS, 0,
                                      ANJAY_IID_INVALID);

    DM_TEST_EXPECT_RESPONSE(mocksocks[0],
            "\x60\x45\xfa\x3e" // CoAP header
            "\xc1\x28" // Content-Format: application/link-format
            "\xff" "</42>;pmax=514,</42/0>,</42/0/0>,</42/0/3>,</42/0/4>,</42/0/6>,"
                   "</42/1>,</42/1/4>,</42/1/5>,</42/1/6>");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_discover, object_multiple_servers) {
    DM_TEST_INIT_WITH_SSIDS(2, 3);
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x61\x28"; // Accept: application/link-format
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_object_read_default_attrs(anjay, &OBJ, 2, 0,
            &(const anjay_dm_attributes_t) {
                .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                .max_period = 514,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            });

    int presence[][7] = {
        { 1, 0, 0, 1, 1, 0, 1 },
        { 0, 0, 0, 0, 1, 1, 1 }
    };
    const size_t ITERATIONS = sizeof(presence) / sizeof(presence[0]);
    for (anjay_iid_t iid = 0; iid < ITERATIONS; ++iid) {
        _anjay_mock_dm_expect_instance_it(anjay, &OBJ, iid, 0, iid);

        for (anjay_rid_t rid = 0; rid < 7; ++rid) {
            _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, rid, 1);
            _anjay_mock_dm_expect_resource_present(anjay, &OBJ, iid, rid, presence[iid][rid]);
        }
    }
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, ITERATIONS, 0,
                                      ANJAY_IID_INVALID);

    DM_TEST_EXPECT_RESPONSE(mocksocks[0],
            "\x60\x45\xfa\x3e" // CoAP header
            "\xc1\x28" // Content-Format: application/link-format
            "\xff" "</42>;pmax=514,"
                   "</42/0>,</42/0/0>,</42/0/3>,</42/0/4>,</42/0/6>,"
                   "</42/1>,</42/1/4>,</42/1/5>,</42/1/6>");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_discover, error) {
    DM_TEST_INIT_WITH_SSIDS(7);
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "69" // IID
            "\x01" "4" // RID
            "\x61\x28"; // Accept: application/link-format
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_dim(anjay, &OBJ, 69, 4,
                                       ANJAY_DM_DIM_INVALID);
    _anjay_mock_dm_expect_resource_read_attrs(anjay, &OBJ, 69, 4, 7,
                                              ANJAY_ERR_CONFLICT,
                                              &ANJAY_DM_ATTRIBS_EMPTY);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x89\xfa\x3e");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_discover, multiple_servers_empty) {
    DM_TEST_INIT_WITH_SSIDS(34, 45);
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "69" // IID
            "\x01" "4" // RID
            "\x61\x28"; // Accept: application/link-format
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_dim(anjay, &OBJ, 69, 4, ANJAY_DM_DIM_INVALID);
    _anjay_mock_dm_expect_resource_read_attrs(anjay, &OBJ, 69, 4, 34, 0,
                                              &ANJAY_DM_ATTRIBS_EMPTY);
    _anjay_mock_dm_expect_instance_read_default_attrs(
            anjay, &OBJ, 69, 34, 0, &ANJAY_DM_ATTRIBS_EMPTY);
    _anjay_mock_dm_expect_object_read_default_attrs(anjay, &OBJ, 34, 0,
            &ANJAY_DM_ATTRIBS_EMPTY);

    DM_TEST_EXPECT_RESPONSE(mocksocks[0],
            "\x60\x45\xfa\x3e" // CoAP header
            "\xc1\x28" // Content-Format: application/link-format
            "\xff"
            "</42/69/4>");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_create, only_iid) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x02\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x12" "\x2d\x16" // Content-format: TLV
            "\xFF" "\x20" "\x02\x02"; // IID == 514
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 514, 0);
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 514, 1, 0, 514);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0],
            "\x60\x41\xfa\x3e"
            "\x82" "42"
            "\x03" "514");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_create, failure) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x02\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x12" "\x2d\x16" // Content-Format: TLV
            "\xFF" "\x20" "\x02\x02"; // IID == 514
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 514, 0);
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 514, 1, -1, 514);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\xa0\xfa\x3e");
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_create, already_exists) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x02\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x12" "\x2d\x16" // Content-Format: TLV
            "\xFF" "\x00" "\x45"; // IID == 69
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x80\xfa\x3e");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_create, wrong_iid) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x02\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x12" "\x2d\x16" // Content-Format: TLV
            "\xFF" "\x20" "\x02\x02"; // IID == 514
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 514, 0);
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 514, 1, 0, 7);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\xa0\xfa\x3e");
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_create, no_iid) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x02\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x12" "\x2d\x16"; // Content-Format: TLV
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, ANJAY_IID_INVALID, 1, 0,
                                          69);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0],
            "\x60\x41\xfa\x3e"
            "\x82" "42"
            "\x02" "69");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_create, with_data) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x02\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x12\x2d\x16"
            "\xff"
            "\xc1\x00\x0d"
            "\xc5\x06" "Hello";
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, ANJAY_IID_INVALID, 1, 0,
                                          69);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 0, 1);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 0,
                                         ANJAY_MOCK_DM_INT(0, 13), 0);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 6, 1);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 6,
                                         ANJAY_MOCK_DM_STRING(0, "Hello"), 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0],
            "\x60\x41\xfa\x3e"
            "\x82" "42"
            "\x02" "69");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_create, with_iid_and_data) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x02\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x12\x2d\x16"
            "\xff"
            "\x08\x45\x0a" // IID == 69
            "\xc1\x00\x0d" // RID == 0
            "\xc5\x06" "Hello"; // RID == 6
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 0);
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 69, 1, 0, 69);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 0, 1);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 0,
                                         ANJAY_MOCK_DM_INT(0, 13), 0);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 6, 1);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 6,
                                         ANJAY_MOCK_DM_STRING(0, "Hello"), 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0],
            "\x60\x41\xfa\x3e"
            "\x82" "42"
            "\x02" "69");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_create, multiple_iids) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x02\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x12\x2d\x16"
            "\xff"
            "\x08\x45\x03" // IID == 69
            "\xc1\x00\x2a" // RID == 0
            "\x08\x2a\x03" // IID == 42
            "\xc1\x03\x45"; // RID == 3
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 0);
    _anjay_mock_dm_expect_instance_create(anjay, &OBJ, 69, 1, 0, 69);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 0, 1);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ, 69, 0,
                                         ANJAY_MOCK_DM_INT(0, 42), 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x80\xfa\x3e");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_delete, success) {
    DM_TEST_INIT;
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

AVS_UNIT_TEST(dm_delete, no_iid) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x04\xFA\x3E" // CoAP header
            "\xB2" "42"; // OID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x85\xfa\x3e");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_delete, superfluous_rid) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x04\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x03" "514" // IID
            "\x01" "2"; // RID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x85\xfa\x3e");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_delete, not_exists) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x04\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "69"; // IID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x84\xfa\x3e");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_delete, failure) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x04\xFA\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "84"; // IID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 84, 1);
    _anjay_mock_dm_expect_instance_remove(anjay, &OBJ, 84, ANJAY_ERR_CONFLICT);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x89\xfa\x3e");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

static int succeed(void) {
    return 0;
}

static int mock_get_id(anjay_input_ctx_t *in_ctx,
                       anjay_id_type_t *out_id_type, uint16_t *out_id) {
    (void) in_ctx;
    *out_id_type = ANJAY_ID_RID;
    *out_id = 0;
    return 0;
}

AVS_UNIT_TEST(dm_operations, unimplemented) {
    anjay_t anjay;
    memset(&anjay, 0, sizeof(anjay));

    DECLARE_COAP_STREAM_MOCK(mock);
    avs_stream_v_table_t vtable = *mock.vtable;
    anjay_coap_stream_ext_t ext =
            *(const anjay_coap_stream_ext_t*)vtable.extension_list[0].data;

    ext.setup_response = (anjay_coap_stream_setup_response_t*)succeed;
    vtable.extension_list = &(const avs_stream_v_table_extension_t[]) {
        { ANJAY_COAP_STREAM_EXTENSION, &ext }
    }[0];
    mock.vtable = &vtable;

    const anjay_dm_object_def_t OBJ_DEF = {
        .oid = 1337,
        .rid_bound = 31337
    };
    const anjay_dm_object_def_t *const def_ptr = &OBJ_DEF;

    anjay_input_ctx_vtable_t in_ctx_vtable = {
        .some_bytes = (anjay_input_ctx_bytes_t) fail,
        .string = (anjay_input_ctx_string_t) fail,
        .i32 = (anjay_input_ctx_i32_t) fail,
        .i64 = (anjay_input_ctx_i64_t) fail,
        .f32 = (anjay_input_ctx_f32_t) fail,
        .f64 = (anjay_input_ctx_f64_t) fail,
        .boolean = (anjay_input_ctx_boolean_t) fail,
        .objlnk = (anjay_input_ctx_objlnk_t) fail,
        .attach_child = (anjay_input_ctx_attach_child_t) fail,
        .get_id = (anjay_input_ctx_get_id_t) fail,
        .close = (anjay_input_ctx_close_t) fail
    };
    struct {
        const anjay_input_ctx_vtable_t *vtable;
    } in_ctx = {
        .vtable = &in_ctx_vtable
    };

#define ASSERT_ACTION_FAILS(...) \
    { \
        anjay_request_details_t details = { \
            .requested_format = ANJAY_COAP_FORMAT_NONE, \
            .action = __VA_ARGS__ \
        }; \
        AVS_UNIT_ASSERT_FAILED(invoke_action(&anjay, &def_ptr, &details, \
                                             (anjay_input_ctx_t *) &in_ctx, \
                                             (avs_stream_abstract_t *)&mock)); \
    }

    ASSERT_ACTION_FAILS(ANJAY_ACTION_READ)
    ASSERT_ACTION_FAILS(ANJAY_ACTION_DISCOVER)
    ASSERT_ACTION_FAILS(ANJAY_ACTION_WRITE)
    ASSERT_ACTION_FAILS(ANJAY_ACTION_WRITE_UPDATE)
    ASSERT_ACTION_FAILS(ANJAY_ACTION_WRITE_ATTRIBUTES,
                        .attributes = {
                            .has_min_period = true
                        })
    ASSERT_ACTION_FAILS(ANJAY_ACTION_EXECUTE)
    ASSERT_ACTION_FAILS(ANJAY_ACTION_DELETE)
    in_ctx_vtable.get_id = mock_get_id;
    ASSERT_ACTION_FAILS(ANJAY_ACTION_CREATE)

    // Cancel Observe does not call any handlers, so it does not fail
}

static const anjay_dm_attrs_query_details_t DM_EFFECTIVE_ATTRS_STANDARD_QUERY =
    (anjay_dm_attrs_query_details_t) {
        .obj = &OBJ,
        .iid = 69,
        .rid = 4,
        .ssid = 1,
        .with_server_level_attrs = true
    };

AVS_UNIT_TEST(dm_effective_attrs, resource_full) {
    DM_TEST_INIT;
    (void) mocksocks;
    static const anjay_dm_attributes_t RES_ATTRS = {
        .min_period = 14,
        .max_period = 42,
        .greater_than = 77.2,
        .less_than = ANJAY_ATTRIB_VALUE_NONE,
        .step = ANJAY_ATTRIB_VALUE_NONE
    };
    _anjay_mock_dm_expect_resource_read_attrs(anjay, &OBJ, 69, 4, 1, 0,
                                              &RES_ATTRS);
    _anjay_mock_dm_expect_instance_read_default_attrs(anjay, &OBJ, 69, 1, 0,
                                                      &ANJAY_DM_ATTRIBS_EMPTY);
    _anjay_mock_dm_expect_object_read_default_attrs(anjay, &OBJ, 1, 0,
                                                    &ANJAY_DM_ATTRIBS_EMPTY);

    anjay_dm_attributes_t attrs;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_effective_attrs(
            anjay, &DM_EFFECTIVE_ATTRS_STANDARD_QUERY, &attrs));
    _anjay_mock_dm_assert_attributes_equal(&attrs, &RES_ATTRS);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_effective_attrs, fallback_to_instance_and_then_to_object) {
    DM_TEST_INIT;
    (void) mocksocks;
    _anjay_mock_dm_expect_resource_read_attrs(anjay, &OBJ, 69, 4, 1, 0,
            &(const anjay_dm_attributes_t) {
                .min_period = 14,
                .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            });
    _anjay_mock_dm_expect_instance_read_default_attrs(anjay, &OBJ, 69, 1, 0,
            &(const anjay_dm_attributes_t) {
                .min_period = 514,
                .max_period = 42,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            });
    _anjay_mock_dm_expect_object_read_default_attrs(anjay, &OBJ, 34, 0,
            &(const anjay_dm_attributes_t) {
                .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = 10.0
            });

    anjay_dm_attributes_t attrs;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_effective_attrs(
            anjay, &DM_EFFECTIVE_ATTRS_STANDARD_QUERY, &attrs));
    _anjay_mock_dm_assert_attributes_equal(&attrs,
            &(const anjay_dm_attributes_t) {
                .min_period = 14,
                .max_period = 42,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = 10.0
            });
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_effective_attrs, fallback_to_object) {
    DM_TEST_INIT;
    (void) mocksocks;
    _anjay_mock_dm_expect_resource_read_attrs(anjay, &OBJ, 69, 4, 1, 0,
            &(const anjay_dm_attributes_t) {
                .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                .greater_than = 43.7,
                .less_than = 17.3,
                .step = 6.9
            });
    _anjay_mock_dm_expect_instance_read_default_attrs(anjay, &OBJ, 69, 1, 0,
            &(const anjay_dm_attributes_t) {
                .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                .max_period = 777,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            });
    _anjay_mock_dm_expect_object_read_default_attrs(anjay, &OBJ, 1, 0,
            &(const anjay_dm_attributes_t) {
                .min_period = 514,
                .max_period = 69,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            });
    anjay_dm_attributes_t attrs;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_effective_attrs(
            anjay, &DM_EFFECTIVE_ATTRS_STANDARD_QUERY, &attrs));
    _anjay_mock_dm_assert_attributes_equal(&attrs,
            &(const anjay_dm_attributes_t) {
                .min_period = 514,
                .max_period = 777,
                .greater_than = 43.7,
                .less_than = 17.3,
                .step = 6.9
            });
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_effective_attrs, fallback_to_server) {
    DM_TEST_INIT;
    (void) mocksocks;
    _anjay_mock_dm_expect_resource_read_attrs(anjay, &OBJ, 69, 4, 1, 0,
                                              &ANJAY_DM_ATTRIBS_EMPTY);
    _anjay_mock_dm_expect_instance_read_default_attrs(anjay, &OBJ, 69, 1, 0,
                                                      &ANJAY_DM_ATTRIBS_EMPTY);
    _anjay_mock_dm_expect_object_read_default_attrs(anjay, &OBJ, 1, 0,
            &(const anjay_dm_attributes_t) {
                .min_period = 4,
                .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            });
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SERVER, 0, 0, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SERVER,
                                             ANJAY_DM_RID_SERVER_SSID, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SERVER, 1,
                                           ANJAY_DM_RID_SERVER_SSID, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 1,
                                        ANJAY_DM_RID_SERVER_SSID, 0,
                                        ANJAY_MOCK_DM_INT(0, 1));
    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SERVER,
                                             ANJAY_DM_RID_SERVER_DEFAULT_PMAX, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SERVER, 1,
                                           ANJAY_DM_RID_SERVER_DEFAULT_PMAX, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 1,
                                        ANJAY_DM_RID_SERVER_DEFAULT_PMAX, 0,
                                        ANJAY_MOCK_DM_INT(0, 42));
    anjay_dm_attributes_t attrs;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_effective_attrs(
            anjay, &DM_EFFECTIVE_ATTRS_STANDARD_QUERY, &attrs));
    _anjay_mock_dm_assert_attributes_equal(&attrs,
            &(const anjay_dm_attributes_t) {
                .min_period = 4,
                .max_period = 42,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            });
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_effective_attrs, resource_fail) {
    DM_TEST_INIT;
    (void) mocksocks;
    _anjay_mock_dm_expect_resource_read_attrs(anjay, &OBJ, 69, 4, 1, -1, NULL);
    anjay_dm_attributes_t attrs = ANJAY_DM_ATTRIBS_EMPTY;
    AVS_UNIT_ASSERT_FAILED(_anjay_dm_effective_attrs(
            anjay, &DM_EFFECTIVE_ATTRS_STANDARD_QUERY, &attrs));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_effective_attrs, for_instance) {
    DM_TEST_INIT;
    (void) mocksocks;
    _anjay_mock_dm_expect_instance_read_default_attrs(anjay, &OBJ, 69, 1, 0,
            &(const anjay_dm_attributes_t) {
                .min_period = 9,
                .max_period = 77,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            });
    _anjay_mock_dm_expect_object_read_default_attrs(anjay, &OBJ, 1, 0,
                                                    &ANJAY_DM_ATTRIBS_EMPTY);
    anjay_dm_attributes_t attrs;
    anjay_dm_attrs_query_details_t details = DM_EFFECTIVE_ATTRS_STANDARD_QUERY;
    details.rid = -1;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_effective_attrs(anjay, &details, &attrs));
    _anjay_mock_dm_assert_attributes_equal(&attrs,
            &(const anjay_dm_attributes_t) {
                .min_period = 9,
                .max_period = 77,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            });
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_effective_attrs, instance_fail) {
    DM_TEST_INIT;
    (void) mocksocks;
    _anjay_mock_dm_expect_instance_read_default_attrs(anjay, &OBJ, 69, 1, -1,
                                                      NULL);
    anjay_dm_attributes_t attrs;
    anjay_dm_attrs_query_details_t details = DM_EFFECTIVE_ATTRS_STANDARD_QUERY;
    details.rid = -1;
    AVS_UNIT_ASSERT_FAILED(_anjay_dm_effective_attrs(anjay, &details, &attrs));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_effective_attrs, for_object) {
    DM_TEST_INIT;
    (void) mocksocks;
    _anjay_mock_dm_expect_object_read_default_attrs(anjay, &OBJ, 1, 0,
            &(const anjay_dm_attributes_t) {
                .min_period = 6,
                .max_period = 54,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            });
    anjay_dm_attributes_t attrs;
    anjay_dm_attrs_query_details_t details = DM_EFFECTIVE_ATTRS_STANDARD_QUERY;
    details.rid = -1;
    details.iid = ANJAY_IID_INVALID;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_effective_attrs(anjay, &details, &attrs));
    _anjay_mock_dm_assert_attributes_equal(&attrs,
            &(const anjay_dm_attributes_t) {
                .min_period = 6,
                .max_period = 54,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            });
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_effective_attrs, object_fail) {
    DM_TEST_INIT;
    (void) mocksocks;
    _anjay_mock_dm_expect_object_read_default_attrs(anjay, &OBJ, 1, -1, NULL);
    anjay_dm_attributes_t attrs;
    anjay_dm_attrs_query_details_t details = DM_EFFECTIVE_ATTRS_STANDARD_QUERY;
    details.rid = -1;
    details.iid = ANJAY_IID_INVALID;
    AVS_UNIT_ASSERT_FAILED(_anjay_dm_effective_attrs(anjay, &details, &attrs));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_effective_attrs, server_default) {
    DM_TEST_INIT;
    (void) mocksocks;
    _anjay_mock_dm_expect_object_read_default_attrs(anjay, &OBJ, 1, 0,
                                                    &ANJAY_DM_ATTRIBS_EMPTY);
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SERVER, 0, 0, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SERVER,
                                             ANJAY_DM_RID_SERVER_SSID, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SERVER, 1,
                                           ANJAY_DM_RID_SERVER_SSID, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 1,
                                        ANJAY_DM_RID_SERVER_SSID, 0,
                                        ANJAY_MOCK_DM_INT(0, 1));
    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SERVER,
                                             ANJAY_DM_RID_SERVER_DEFAULT_PMIN, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SERVER, 1,
                                           ANJAY_DM_RID_SERVER_DEFAULT_PMIN, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 1,
                                        ANJAY_DM_RID_SERVER_DEFAULT_PMIN, 0,
                                        ANJAY_MOCK_DM_INT(0, 0));
    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SERVER,
                                             ANJAY_DM_RID_SERVER_DEFAULT_PMAX, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SERVER, 1,
                                           ANJAY_DM_RID_SERVER_DEFAULT_PMAX, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 1,
                                        ANJAY_DM_RID_SERVER_DEFAULT_PMAX, 0,
                                        ANJAY_MOCK_DM_INT(0, 404));
    anjay_dm_attributes_t attrs;
    anjay_dm_attrs_query_details_t details = DM_EFFECTIVE_ATTRS_STANDARD_QUERY;
    details.rid = -1;
    details.iid = ANJAY_IID_INVALID;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_effective_attrs(anjay, &details, &attrs));
   _anjay_mock_dm_assert_attributes_equal(&attrs,
            &(const anjay_dm_attributes_t) {
                .min_period = 0,
                .max_period = 404,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            });
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_effective_attrs, no_server) {
    DM_TEST_INIT;
    (void) mocksocks;
    _anjay_mock_dm_expect_object_read_default_attrs(anjay, &OBJ, 1, 0,
                                                    &ANJAY_DM_ATTRIBS_EMPTY);
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SERVER, 0, 0,
                                      ANJAY_IID_INVALID);
    anjay_dm_attributes_t attrs;
    anjay_dm_attrs_query_details_t details = DM_EFFECTIVE_ATTRS_STANDARD_QUERY;
    details.rid = -1;
    details.iid = ANJAY_IID_INVALID;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_effective_attrs(anjay, &details, &attrs));
   _anjay_mock_dm_assert_attributes_equal(&attrs,
            &(const anjay_dm_attributes_t) {
                .min_period = ANJAY_DM_DEFAULT_PMIN_VALUE,
                .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            });
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_effective_attrs, no_resources) {
    DM_TEST_INIT;
    (void) mocksocks;
    _anjay_mock_dm_expect_object_read_default_attrs(anjay, &OBJ, 1, 0,
                                                    &ANJAY_DM_ATTRIBS_EMPTY);
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SERVER, 0, 0, 1);

    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SERVER,
                                             ANJAY_DM_RID_SERVER_SSID, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SERVER, 1,
                                           ANJAY_DM_RID_SERVER_SSID, 1);

    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 1,
                                        ANJAY_DM_RID_SERVER_SSID, 0,
                                        ANJAY_MOCK_DM_INT(0, 1));
    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SERVER,
                                             ANJAY_DM_RID_SERVER_DEFAULT_PMIN, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SERVER, 1,
                                           ANJAY_DM_RID_SERVER_DEFAULT_PMIN, 0);
    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SERVER,
                                             ANJAY_DM_RID_SERVER_DEFAULT_PMAX, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SERVER, 1,
                                           ANJAY_DM_RID_SERVER_DEFAULT_PMAX, 0);
    anjay_dm_attributes_t attrs;
    anjay_dm_attrs_query_details_t details = DM_EFFECTIVE_ATTRS_STANDARD_QUERY;
    details.rid = -1;
    details.iid = ANJAY_IID_INVALID;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_effective_attrs(anjay, &details, &attrs));
   _anjay_mock_dm_assert_attributes_equal(&attrs,
            &(const anjay_dm_attributes_t) {
                .min_period = ANJAY_DM_DEFAULT_PMIN_VALUE,
                .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                .step = ANJAY_ATTRIB_VALUE_NONE
            });
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_effective_attrs, read_error) {
    DM_TEST_INIT;
    (void) mocksocks;
    _anjay_mock_dm_expect_object_read_default_attrs(anjay, &OBJ, 1, 0,
                                                    &ANJAY_DM_ATTRIBS_EMPTY);
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SERVER, 0, 0, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SERVER,
                                             ANJAY_DM_RID_SERVER_SSID, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SERVER, 1,
                                           ANJAY_DM_RID_SERVER_SSID, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 1,
                                        ANJAY_DM_RID_SERVER_SSID, 0,
                                        ANJAY_MOCK_DM_INT(0, 1));
    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SERVER,
                                             ANJAY_DM_RID_SERVER_DEFAULT_PMIN, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SERVER, 1,
                                           ANJAY_DM_RID_SERVER_DEFAULT_PMIN, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 1,
                                        ANJAY_DM_RID_SERVER_DEFAULT_PMIN, 0,
                                        ANJAY_MOCK_DM_INT(0, 7));
    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SERVER,
                                             ANJAY_DM_RID_SERVER_DEFAULT_PMAX, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SERVER, 1,
                                           ANJAY_DM_RID_SERVER_DEFAULT_PMAX, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 1,
                                        ANJAY_DM_RID_SERVER_DEFAULT_PMAX, -1,
                                        ANJAY_MOCK_DM_NONE);
    anjay_dm_attributes_t attrs;
    anjay_dm_attrs_query_details_t details = DM_EFFECTIVE_ATTRS_STANDARD_QUERY;
    details.rid = -1;
    details.iid = ANJAY_IID_INVALID;
    AVS_UNIT_ASSERT_FAILED(_anjay_dm_effective_attrs(anjay, &details, &attrs));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_effective_attrs, read_invalid) {
    DM_TEST_INIT;
    (void) mocksocks;
    _anjay_mock_dm_expect_object_read_default_attrs(anjay, &OBJ, 1, 0,
                                                    &ANJAY_DM_ATTRIBS_EMPTY);
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SERVER, 0, 0, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SERVER,
                                             ANJAY_DM_RID_SERVER_SSID, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SERVER, 1,
                                           ANJAY_DM_RID_SERVER_SSID, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 1,
                                        ANJAY_DM_RID_SERVER_SSID, 0,
                                        ANJAY_MOCK_DM_INT(0, 1));
    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SERVER,
                                             ANJAY_DM_RID_SERVER_DEFAULT_PMIN, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SERVER, 1,
                                           ANJAY_DM_RID_SERVER_DEFAULT_PMIN, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 1,
                                        ANJAY_DM_RID_SERVER_DEFAULT_PMIN, 0,
                                        ANJAY_MOCK_DM_INT(0, 7));
    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SERVER,
                                             ANJAY_DM_RID_SERVER_DEFAULT_PMAX, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SERVER, 1,
                                           ANJAY_DM_RID_SERVER_DEFAULT_PMAX, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 1,
                                        ANJAY_DM_RID_SERVER_DEFAULT_PMAX, 0,
                                        ANJAY_MOCK_DM_INT(0, -1));
    anjay_dm_attributes_t attrs;
    anjay_dm_attrs_query_details_t details = DM_EFFECTIVE_ATTRS_STANDARD_QUERY;
    details.rid = -1;
    details.iid = ANJAY_IID_INVALID;
    AVS_UNIT_ASSERT_FAILED(_anjay_dm_effective_attrs(anjay, &details, &attrs));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_resource_operations, nonreadable_resource) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB3" "667" // OID
            "\x02" "69" // IID
            "\x01" "4"; // RID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ_WITH_RES_OPS, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ_WITH_RES_OPS, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ_WITH_RES_OPS, 69, 4, 1);
    _anjay_mock_dm_expect_resource_operations(anjay, &OBJ_WITH_RES_OPS, 4,
                                              ANJAY_DM_RESOURCE_OP_BIT_E, 0);
    // 4.05 Method Not Allowed
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x85\xfa\x3e");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_resource_operations, nonexecutable_resource) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x02\xFA\x3E" // CoAP header
            "\xB3" "667" // OID
            "\x02" "69" // IID
            "\x01" "4"; // RID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ_WITH_RES_OPS, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ_WITH_RES_OPS, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ_WITH_RES_OPS, 69, 4, 1);
    _anjay_mock_dm_expect_resource_operations(anjay, &OBJ_WITH_RES_OPS, 4,
                                              ANJAY_DM_RESOURCE_OP_BIT_W, 0);
    // 4.05 Method Not Allowed
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x85\xfa\x3e");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_resource_operations, nonwritable_resource) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB3" "667" // OID
            "\x02" "69" // IID
            "\x01" "4" // RID
            "\x10" // Content-Format
            "\xFF"
            "content";
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ_WITH_RES_OPS, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ_WITH_RES_OPS, 4, 1);
    _anjay_mock_dm_expect_resource_operations(anjay, &OBJ_WITH_RES_OPS, 4,
                                              ANJAY_DM_RESOURCE_OP_BIT_R, 0);
    // 4.05 Method Not Allowed
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x85\xfa\x3e");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_resource_operations, readable_resource) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\xB3" "667" // OID
            "\x02" "69" // IID
            "\x01" "4"; // RID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ_WITH_RES_OPS, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ_WITH_RES_OPS, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ_WITH_RES_OPS, 69, 4, 1);
    _anjay_mock_dm_expect_resource_operations(anjay, &OBJ_WITH_RES_OPS, 4,
                                              ANJAY_DM_RESOURCE_OP_BIT_R, 0);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ_WITH_RES_OPS, 69, 4, 0,
                                        ANJAY_MOCK_DM_INT(0, 514));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0],
            "\x60\x45\xFA\x3E" // CoAP header
            "\xc0" // Content-Format
            "\xff" "514");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_resource_operations, executable_resource) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x02\xFA\x3E" // CoAP header
            "\xB3" "667" // OID
            "\x03" "514" // IID
            "\x01" "4"; // RID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ_WITH_RES_OPS, 514, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ_WITH_RES_OPS, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ_WITH_RES_OPS, 514, 4, 1);
    _anjay_mock_dm_expect_resource_operations(anjay, &OBJ_WITH_RES_OPS, 4,
                                              ANJAY_DM_RESOURCE_OP_BIT_E, 0);
    _anjay_mock_dm_expect_resource_execute(anjay, &OBJ_WITH_RES_OPS, 514, 4,
                                           ANJAY_MOCK_DM_NONE, 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x44\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_resource_operations, writable_resource) {
    DM_TEST_INIT;
    static const char REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB3" "667" // OID
            "\x03" "514" // IID
            "\x01" "4" // RID
            "\x10" // Content-Format
            "\xFF"
            "Hello";
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ_WITH_RES_OPS, 514, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ_WITH_RES_OPS, 4, 1);
    _anjay_mock_dm_expect_resource_operations(anjay, &OBJ_WITH_RES_OPS, 4,
                                              ANJAY_DM_RESOURCE_OP_BIT_W, 0);
    _anjay_mock_dm_expect_resource_write(anjay, &OBJ_WITH_RES_OPS, 514, 4,
                                         ANJAY_MOCK_DM_STRING(0, "Hello"), 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x44\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(dm_res_read, no_space) {
    DM_TEST_INIT;

    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 3, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 42, 3, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 42, 3, 0,
                                        ANJAY_MOCK_DM_STRING(0, ""));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_res_read(
            anjay, &(anjay_resource_path_t) { OBJ->oid, 42, 3 },
            NULL, 0, NULL));

    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 514, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 514, 4, -1,
                                        ANJAY_MOCK_DM_STRING(-1, "Hello"));
    AVS_UNIT_ASSERT_FAILED(_anjay_dm_res_read(
            anjay, &(anjay_resource_path_t) { OBJ->oid, 514, 4 },
            NULL, 0, NULL));

    char fake_string = 42;
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 5, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 5, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 5, 0,
                                        ANJAY_MOCK_DM_STRING(0, ""));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_res_read_string(
            anjay, &(anjay_resource_path_t) { OBJ->oid, 69, 5 },
            &fake_string, 1));
    AVS_UNIT_ASSERT_EQUAL(fake_string, 0);

    fake_string = 69;
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 6, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 32, 6, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 32, 6, -1,
                                        ANJAY_MOCK_DM_STRING(-1, "Goodbye"));
    AVS_UNIT_ASSERT_FAILED(_anjay_dm_res_read_string(
            anjay, &(anjay_resource_path_t) { OBJ->oid, 32, 6 },
            &fake_string, 1));
    AVS_UNIT_ASSERT_EQUAL(fake_string, 69);

    DM_TEST_FINISH;
}
