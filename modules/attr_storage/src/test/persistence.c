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

#include <string.h>

#include <avsystem/commons/stream.h>
#include <avsystem/commons/stream/stream_outbuf.h>
#include <avsystem/commons/stream/stream_inbuf.h>
#include <avsystem/commons/unit/test.h>

#include <anjay/attr_storage.h>

#include <anjay_test/dm.h>

#include "../attr_storage.h"
#include "attr_storage_test.h"
static anjay_t *const FAKE_ANJAY = (anjay_t *) ~(uintptr_t) NULL;

#define PERSIST_TEST_INIT(Size) \
        char buf[Size]; \
        avs_stream_outbuf_t outbuf = AVS_STREAM_OUTBUF_STATIC_INITIALIZER; \
        avs_stream_outbuf_set_buffer(&outbuf, buf, sizeof(buf)); \
        anjay_attr_storage_t *fas = anjay_attr_storage_new(FAKE_ANJAY)

#define PERSISTENCE_TEST_FINISH \
        do { \
            anjay_attr_storage_delete(fas); \
            _anjay_mock_dm_expect_clean(); \
        } while (0)

#define PERSIST_TEST_CHECK(Data) \
        do { \
            AVS_UNIT_ASSERT_EQUAL(sizeof(Data) - 1, \
                                  avs_stream_outbuf_offset(&outbuf)); \
            AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(Data, buf, sizeof(Data) - 1); \
            PERSISTENCE_TEST_FINISH; \
        } while (0)

#define MAGIC_HEADER "FAS\1"

AVS_UNIT_TEST(attr_storage_persistence, persist_empty) {
    PERSIST_TEST_INIT(256);
    AVS_UNIT_ASSERT_SUCCESS(anjay_attr_storage_persist(
            fas, (avs_stream_abstract_t *) &outbuf));
    PERSIST_TEST_CHECK(MAGIC_HEADER "\x00\x00\x00\x00");
}

#define INSTALL_FAKE_OBJECT(Oid, RidBound) \
        const anjay_dm_object_def_t *const OBJ##Oid = \
                &(const anjay_dm_object_def_t) { \
                    .oid = Oid, \
                    .rid_bound = RidBound, \
                    ANJAY_MOCK_DM_HANDLERS_NOATTRS \
                }; \
        AVS_UNIT_ASSERT_NOT_NULL(anjay_attr_storage_wrap_object( \
                fas, &OBJ##Oid ))

static void write_obj_attrs(anjay_attr_storage_t *fas,
                            anjay_oid_t oid,
                            anjay_ssid_t ssid,
                            const anjay_dm_attributes_t *attrs) {
    fas_object_t *obj = _anjay_attr_storage_find_object(fas, oid);
    AVS_UNIT_ASSERT_NOT_NULL(obj);
    AVS_UNIT_ASSERT_SUCCESS(obj->def.object_write_default_attrs(
            NULL, &obj->def_ptr, ssid, attrs));
}

static void write_inst_attrs(anjay_attr_storage_t *fas,
                             anjay_oid_t oid,
                             anjay_iid_t iid,
                             anjay_ssid_t ssid,
                             const anjay_dm_attributes_t *attrs) {
    fas_object_t *obj = _anjay_attr_storage_find_object(fas, oid);
    AVS_UNIT_ASSERT_NOT_NULL(obj);
    AVS_UNIT_ASSERT_SUCCESS(obj->def.instance_write_default_attrs(
            NULL, &obj->def_ptr, iid, ssid, attrs));
}

static void write_res_attrs(anjay_attr_storage_t *fas,
                            anjay_oid_t oid,
                            anjay_iid_t iid,
                            anjay_rid_t rid,
                            anjay_ssid_t ssid,
                            const anjay_dm_attributes_t *attrs) {
    fas_object_t *obj = _anjay_attr_storage_find_object(fas, oid);
    AVS_UNIT_ASSERT_NOT_NULL(obj);
    AVS_UNIT_ASSERT_SUCCESS(obj->def.resource_write_attrs(
            NULL, &obj->def_ptr, iid, rid, ssid, attrs));
}

static void persist_test_fill(anjay_attr_storage_t *fas) {
    INSTALL_FAKE_OBJECT(4, 4);
    INSTALL_FAKE_OBJECT(42, 4);
    INSTALL_FAKE_OBJECT(517, 522);
    write_obj_attrs(fas, 4, 33,
                    &(const anjay_dm_attributes_t) {
                        .min_period = 42,
                        .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                        .less_than = ANJAY_ATTRIB_VALUE_NONE,
                        .step = ANJAY_ATTRIB_VALUE_NONE
                    });
    write_obj_attrs(fas, 4, 14,
                    &(const anjay_dm_attributes_t) {
                        .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .max_period = 3,
                        .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                        .less_than = ANJAY_ATTRIB_VALUE_NONE,
                        .step = ANJAY_ATTRIB_VALUE_NONE
                    });
    write_inst_attrs(fas, 42, 1, 2,
                     &(const anjay_dm_attributes_t) {
                         .min_period = 7,
                         .max_period = 13,
                         .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                         .less_than = ANJAY_ATTRIB_VALUE_NONE,
                         .step = ANJAY_ATTRIB_VALUE_NONE
                     });
    write_res_attrs(fas, 42, 1, 3, 2,
                    &(const anjay_dm_attributes_t) {
                        .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .greater_than = 1.0,
                        .less_than = -1.0,
                        .step = ANJAY_ATTRIB_VALUE_NONE
                    });
    write_res_attrs(fas, 42, 1, 3, 7,
                    &(const anjay_dm_attributes_t) {
                        .min_period = 1,
                        .max_period = 14,
                        .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                        .less_than = ANJAY_ATTRIB_VALUE_NONE,
                        .step = ANJAY_ATTRIB_VALUE_NONE
                    });
    write_res_attrs(fas, 517, 516, 515, 514,
                    &(const anjay_dm_attributes_t) {
                        .min_period = 33,
                        .max_period = ANJAY_ATTRIB_PERIOD_NONE,
                        .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                        .less_than = ANJAY_ATTRIB_VALUE_NONE,
                        .step = 42.0
                    });
}

static const char PERSIST_TEST_DATA[] =
        MAGIC_HEADER
        "\x00\x00\x00\x03" // 3 objects
            "\x00\x04" // OID 4
                "\x00\x00\x00\x02" // 2 object-level default attrs
                    "\x00\x0E" // SSID 14
                        "\xFF\xFF\xFF\xFF" // min period
                        "\x00\x00\x00\x03" // max period
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // greater than
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // less than
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // step
                    "\x00\x21" // SSID 33
                        "\x00\x00\x00\x2A" // min period
                        "\xFF\xFF\xFF\xFF" // max period
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // greater than
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // less than
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // step
                "\x00\x00\x00\x00" // 0 instance entries
            "\x00\x2A" // OID 42
                "\x00\x00\x00\x00" // 0 object-level default attrs
                "\x00\x00\x00\x01" // 1 instance entry
                    "\x00\x01" // IID 1
                        "\x00\x00\x00\x01" // 1 instance-level default attr
                            "\x00\x02" // SSID 2
                                "\x00\x00\x00\x07" // min period
                                "\x00\x00\x00\x0D" // max period
                                "\x7f\xf8\x00\x00\x00\x00\x00\x00" // greater than
                                "\x7f\xf8\x00\x00\x00\x00\x00\x00" // less than
                                "\x7f\xf8\x00\x00\x00\x00\x00\x00" // step
                        "\x00\x00\x00\x01" // 1 resource entry
                            "\x00\x03" // RID 3
                                "\x00\x00\x00\x02" // 2 attr entries
                                    "\x00\x02" // SSID 2
                                        "\xFF\xFF\xFF\xFF" // min period
                                        "\xFF\xFF\xFF\xFF" // max period
                    /* greater than */  "\x3F\xF0\x00\x00\x00\x00\x00\x00"
                    /* less than */     "\xBF\xF0\x00\x00\x00\x00\x00\x00"

                    /* step */          "\x7F\xF8\x00\x00\x00\x00\x00\x00"
                                    "\x00\x07" // SSID 7
                                        "\x00\x00\x00\x01" // min period
                                        "\x00\x00\x00\x0E" // max period
                    /* greater than */  "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                    /* less than */     "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                    /* step */          "\x7f\xf8\x00\x00\x00\x00\x00\x00"
            "\x02\x05" // OID 517
                "\x00\x00\x00\x00" // 0 object-level default attrs
                "\x00\x00\x00\x01" // 1 instance entry
                    "\x02\x04" // IID 516
                        "\x00\x00\x00\x00" // 0 instance-level default attrs
                        "\x00\x00\x00\x01" // 1 resource entry
                            "\x02\x03" // RID 515
                                "\x00\x00\x00\x01" // 1 attr entry
                                    "\x02\x02" // SSID 514
                                        "\x00\x00\x00\x21" // min period
                                        "\xFF\xFF\xFF\xFF" // max period
                    /* greater than */  "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                    /* less than */     "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                    /* step */          "\x40\x45\x00\x00\x00\x00\x00\x00";

AVS_UNIT_TEST(attr_storage_persistence, persist_full) {
    PERSIST_TEST_INIT(512);
    persist_test_fill(fas);
    AVS_UNIT_ASSERT_SUCCESS(anjay_attr_storage_persist(
            fas, (avs_stream_abstract_t *) &outbuf));
    PERSIST_TEST_CHECK(PERSIST_TEST_DATA);
}

AVS_UNIT_TEST(attr_storage_persistence, persist_not_enough_space) {
    PERSIST_TEST_INIT(128);
    persist_test_fill(fas);
    AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_persist(
            fas, (avs_stream_abstract_t *) &outbuf));
    PERSISTENCE_TEST_FINISH;
}

#define RESTORE_TEST_INIT(Data) \
        avs_stream_inbuf_t inbuf = AVS_STREAM_INBUF_STATIC_INITIALIZER; \
        avs_stream_inbuf_set_buffer(&inbuf, (Data), sizeof(Data) - 1); \
        anjay_attr_storage_t *fas = anjay_attr_storage_new(FAKE_ANJAY)

AVS_UNIT_TEST(attr_storage_persistence, restore_empty) {
    RESTORE_TEST_INIT("");
    AVS_UNIT_ASSERT_SUCCESS(anjay_attr_storage_restore(
            fas, (avs_stream_abstract_t *) &inbuf));
    PERSISTENCE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage_persistence, restore_no_objects) {
    RESTORE_TEST_INIT(PERSIST_TEST_DATA);
    AVS_UNIT_ASSERT_SUCCESS(anjay_attr_storage_restore(
            fas, (avs_stream_abstract_t *) &inbuf));
    AVS_UNIT_ASSERT_NULL(fas->objects);
    PERSISTENCE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage_persistence, restore_one_object) {
    RESTORE_TEST_INIT(PERSIST_TEST_DATA);
    INSTALL_FAKE_OBJECT(42, 5);

    _anjay_mock_dm_expect_instance_it(FAKE_ANJAY, &OBJ42, 0, 0, 1);
    _anjay_mock_dm_expect_instance_it(FAKE_ANJAY, &OBJ42, 1, 0,
                                      ANJAY_IID_INVALID);
    _anjay_mock_dm_expect_resource_supported(FAKE_ANJAY, &OBJ42, 3, 1);
    _anjay_mock_dm_expect_resource_present(FAKE_ANJAY, &OBJ42, 1, 3, 1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_attr_storage_restore(
            fas, (avs_stream_abstract_t *) &inbuf));

    AVS_UNIT_ASSERT_NULL(
            _anjay_attr_storage_find_object(fas, 42)->default_attrs);
    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(_anjay_attr_storage_find_object(fas, 42)->instances),
            1);
    assert_instance_equal(
            _anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(
                    1,
                    test_default_attrlist(
                            test_default_attrs(2, 7, 13),
                            NULL),
                    test_resource_entry(
                            3,
                            test_resource_attrs(2,
                                                ANJAY_ATTRIB_PERIOD_NONE,
                                                ANJAY_ATTRIB_PERIOD_NONE,
                                                1.0,
                                                -1.0,
                                                ANJAY_ATTRIB_VALUE_NONE),
                            test_resource_attrs(7,
                                                1,
                                                14,
                                                ANJAY_ATTRIB_VALUE_NONE,
                                                ANJAY_ATTRIB_VALUE_NONE,
                                                ANJAY_ATTRIB_VALUE_NONE),
                            NULL),
                    NULL));
    PERSISTENCE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage_persistence, restore_all_objects) {
    RESTORE_TEST_INIT(PERSIST_TEST_DATA);
    INSTALL_FAKE_OBJECT(4, 5);
    INSTALL_FAKE_OBJECT(42, 5);
    INSTALL_FAKE_OBJECT(69, 10);
    INSTALL_FAKE_OBJECT(514, 522);
    INSTALL_FAKE_OBJECT(517, 522);

    // this will be cleared
    write_inst_attrs(fas, 69, 68, 67,
                     &(const anjay_dm_attributes_t) {
                         .min_period = 66,
                         .max_period = 65
                     });

    _anjay_mock_dm_expect_instance_it(FAKE_ANJAY, &OBJ4, 0, 0,
                                      ANJAY_IID_INVALID);
    _anjay_mock_dm_expect_instance_it(FAKE_ANJAY, &OBJ42, 0, 0, 1);
    _anjay_mock_dm_expect_instance_it(FAKE_ANJAY, &OBJ42, 1, 0,
                                      ANJAY_IID_INVALID);
    _anjay_mock_dm_expect_resource_supported(FAKE_ANJAY, &OBJ42, 3, 1);
    _anjay_mock_dm_expect_resource_present(FAKE_ANJAY, &OBJ42, 1, 3, 1);
    _anjay_mock_dm_expect_instance_it(FAKE_ANJAY, &OBJ517, 0, 0, 516);
    _anjay_mock_dm_expect_instance_it(FAKE_ANJAY, &OBJ517, 1, 0,
                                      ANJAY_IID_INVALID);
    _anjay_mock_dm_expect_resource_supported(FAKE_ANJAY, &OBJ517, 515, 1);
    _anjay_mock_dm_expect_resource_present(FAKE_ANJAY, &OBJ517, 516, 515, 1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_attr_storage_restore(
            fas, (avs_stream_abstract_t *) &inbuf));

    // object 4
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(
            _anjay_attr_storage_find_object(fas, 4)->default_attrs), 2);
    assert_attrs_equal(
            _anjay_attr_storage_find_object(fas, 4)->default_attrs,
            test_default_attrs(14, ANJAY_ATTRIB_PERIOD_NONE, 3));
    assert_attrs_equal(
            AVS_LIST_NEXT(
                    _anjay_attr_storage_find_object(fas, 4)->default_attrs),
            test_default_attrs(33, 42, ANJAY_ATTRIB_PERIOD_NONE));
    AVS_UNIT_ASSERT_NULL(_anjay_attr_storage_find_object(fas, 4)->instances);

    // object 42
    AVS_UNIT_ASSERT_NULL(
            _anjay_attr_storage_find_object(fas, 42)->default_attrs);
    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(_anjay_attr_storage_find_object(fas, 42)->instances),
            1);
    assert_instance_equal(
            _anjay_attr_storage_find_object(fas, 42)->instances,
            test_instance_entry(
                    1,
                    test_default_attrlist(
                            test_default_attrs(2, 7, 13),
                            NULL),
                    test_resource_entry(
                            3,
                            test_resource_attrs(2,
                                                ANJAY_ATTRIB_PERIOD_NONE,
                                                ANJAY_ATTRIB_PERIOD_NONE,
                                                1.0,
                                                -1.0,
                                                ANJAY_ATTRIB_VALUE_NONE),
                            test_resource_attrs(7,
                                                1,
                                                14,
                                                ANJAY_ATTRIB_VALUE_NONE,
                                                ANJAY_ATTRIB_VALUE_NONE,
                                                ANJAY_ATTRIB_VALUE_NONE),
                            NULL),
                    NULL));

    // object 69
    AVS_UNIT_ASSERT_NULL(
            _anjay_attr_storage_find_object(fas, 69)->default_attrs);
    AVS_UNIT_ASSERT_NULL(
            _anjay_attr_storage_find_object(fas, 69)->instances);

    // object 514
    AVS_UNIT_ASSERT_NULL(
            _anjay_attr_storage_find_object(fas, 514)->default_attrs);
    AVS_UNIT_ASSERT_NULL(
            _anjay_attr_storage_find_object(fas, 514)->instances);

    // object 517
    AVS_UNIT_ASSERT_NULL(
            _anjay_attr_storage_find_object(fas, 517)->default_attrs);
    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(_anjay_attr_storage_find_object(fas, 517)->instances),
            1);
    assert_instance_equal(
            _anjay_attr_storage_find_object(fas, 517)->instances,
            test_instance_entry(
                    516,
                    NULL,
                    test_resource_entry(
                            515,
                            test_resource_attrs(514,
                                                33,
                                                ANJAY_ATTRIB_PERIOD_NONE,
                                                ANJAY_ATTRIB_VALUE_NONE,
                                                ANJAY_ATTRIB_VALUE_NONE,
                                                42.0),
                            NULL),
                    NULL));

    PERSISTENCE_TEST_FINISH;
}

static const char RESTORE_BROKEN_DATA[] =
        MAGIC_HEADER
        "\x00\x00\x00\x03" // 3 objects
            "\x00\x04" // OID 4
                "\x00\x00\x00\x02" // 2 object-level default attrs
                    "\x00\x0E" // SSID 14
                        "\xFF\xFF\xFF\xFF" // min period
                        "\x00\x00\x00\x03" // max period
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // greater than
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // less than
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // step
                    "\x00\x21" // SSID 33
                        "\x00\x00\x00\x2A" // min period
                        "\xFF\xFF\xFF\xFF" // max period
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // greater than
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // less than
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // step
                "\x00\x00\x00\x00" // 0 instance entries
            "\x00\x2A" // OID 42
                "\x00\x00\x00\x00" // 0 object-level default attrs
                "\x00\x00\x00\x01" // 1 instance entry
                    "\x00\x01" // IID 1
                        "\x00\x00\x00\x01" // 1 instance-level default attr
                            "\x00\x02" // SSID 2
                                "\x00\x00\x00\x07" // min period
                                "\x00\x00\x00\x0D" // max period
                                "\x7f\xf8\x00\x00\x00\x00\x00\x00" // greater than
                                "\x7f\xf8\x00\x00\x00\x00\x00\x00" // less than
                                "\x7f\xf8\x00\x00\x00\x00\x00\x00" // step
                        "\x00\x00\x00\x01" // 1 resource entry
                            "\x00\x03" // RID 3
                                "\x00\x00\x00\x02" // 2 attr entries
                                    "\x00\x02" // SSID 2
                                        "\xFF\xFF\xFF\xFF" // min period
                                        "\xFF\xFF\xFF\xFF" // max period
                    /* greater than */  "\x3F\xF0\x00\x00\x00\x00\x00\x00"
                    /* less than */     "\xBF\xF0\x00\x00\x00\x00\x00\x00"
                                        "\x7f"; /* premature end of data */

AVS_UNIT_TEST(attr_storage_persistence, restore_broken_stream) {
    RESTORE_TEST_INIT(RESTORE_BROKEN_DATA);
    INSTALL_FAKE_OBJECT(4, 5);
    INSTALL_FAKE_OBJECT(42, 5);
    INSTALL_FAKE_OBJECT(517, 522);

    // this will be cleared
    write_inst_attrs(fas, 517, 518, 519,
                     &(const anjay_dm_attributes_t) {
                         .min_period = 520,
                         .max_period = 521,
                     });

    _anjay_mock_dm_expect_instance_it(FAKE_ANJAY, &OBJ4, 0, 0,
                                      ANJAY_IID_INVALID);
    AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_restore(
            fas, (avs_stream_abstract_t *) &inbuf));

    AVS_UNIT_ASSERT_NULL(
            _anjay_attr_storage_find_object(fas, 4)->default_attrs);
    AVS_UNIT_ASSERT_NULL(
            _anjay_attr_storage_find_object(fas, 4)->instances);
    AVS_UNIT_ASSERT_NULL(
            _anjay_attr_storage_find_object(fas, 42)->default_attrs);
    AVS_UNIT_ASSERT_NULL(
            _anjay_attr_storage_find_object(fas, 42)->instances);
    AVS_UNIT_ASSERT_NULL(
            _anjay_attr_storage_find_object(fas, 517)->default_attrs);
    AVS_UNIT_ASSERT_NULL(
            _anjay_attr_storage_find_object(fas, 517)->instances);
    PERSISTENCE_TEST_FINISH;
}

static const char INSANE_TEST_DATA[] =
        MAGIC_HEADER
        "\x00\x00\x00\x03" // 3 objects
            "\x00\x04" // OID 4
                "\x00\x00\x00\x02" // 2 object-level default attrs
                    "\x00\x0E" // SSID 14
                        "\xFF\xFF\xFF\xFF" // min period
                        "\x00\x00\x00\x03" // max period
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // greater than
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // less than
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // step
                    "\x00\x21" // SSID 33
                        "\x00\x00\x00\x2A" // min period
                        "\xFF\xFF\xFF\xFF" // max period
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // greater than
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // less than
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // step
                "\x00\x00\x00\x00" // 0 instance entries
            "\x00\x2A" // OID 42
                "\x00\x00\x00\x00" // 0 object-level default attrs
                "\x00\x00\x00\x01" // 1 instance entry
                    "\x00\x01" // IID 1
                        "\x00\x00\x00\x01" // 1 instance-level default attr
                            "\x00\x02" // SSID 2
                                "\x00\x00\x00\x07" // min period
                                "\x00\x00\x00\x0D" // max period
                                "\x7f\xf8\x00\x00\x00\x00\x00\x00" // greater than
                                "\x7f\xf8\x00\x00\x00\x00\x00\x00" // less than
                                "\x7f\xf8\x00\x00\x00\x00\x00\x00" // step
                        "\x00\x00\x00\x01" // 1 resource entry
                            "\x00\x03" // RID 3
                                "\x00\x00\x00\x02" // 2 attr entries
                            /*********** INVALID SSID ORDER FOLLOW ***********/
                                    "\x00\x07" // SSID 7
                                        "\x00\x00\x00\x01" // min period
                                        "\x00\x00\x00\x0E" // max period
                    /* greater than */  "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                    /* less than */     "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                    /* step */          "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                                    "\x00\x02" // SSID 2
                                        "\xFF\xFF\xFF\xFF" // min period
                                        "\xFF\xFF\xFF\xFF" // max period
                    /* greater than */  "\x3F\xF0\x00\x00\x00\x00\x00\x00"
                    /* less than */     "\xBF\xF0\x00\x00\x00\x00\x00\x00"
                    /* step */          "\x7F\xF8\x00\x00\x00\x00\x00\x00"
                            /************ INVALID SSID ORDER END ************/
            "\x02\x05" // OID 517
                "\x00\x00\x00\x00" // 0 object-level default attrs
                "\x00\x00\x00\x01" // 1 instance entry
                    "\x02\x04" // IID 516
                        "\x00\x00\x00\x00" // 0 instance-level default attrs
                        "\x00\x00\x00\x01" // 1 resource entry
                            "\x02\x03" // RID 515
                                "\x00\x00\x00\x01" // 1 attr entry
                                    "\x02\x02" // SSID 514
                                        "\x00\x00\x00\x21" // min period
                                        "\xFF\xFF\xFF\xFF" // max period
                    /* greater than */  "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                    /* less than */     "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                    /* step */          "\x40\x45\x00\x00\x00\x00\x00\x00";

AVS_UNIT_TEST(attr_storage_persistence, restore_insane_data) {
    RESTORE_TEST_INIT(INSANE_TEST_DATA);
    INSTALL_FAKE_OBJECT(4, 5);
    INSTALL_FAKE_OBJECT(42, 5);
    INSTALL_FAKE_OBJECT(517, 522);

    // this will be cleared
    write_inst_attrs(fas, 517, 518, 519,
                     &(const anjay_dm_attributes_t) {
                         .min_period = 520,
                         .max_period = 521
                     });

    _anjay_mock_dm_expect_instance_it(FAKE_ANJAY, &OBJ4, 0, 0,
                                      ANJAY_IID_INVALID);
    AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_restore(
            fas, (avs_stream_abstract_t *) &inbuf));

    AVS_UNIT_ASSERT_NULL(
            _anjay_attr_storage_find_object(fas, 4)->default_attrs);
    AVS_UNIT_ASSERT_NULL(
            _anjay_attr_storage_find_object(fas, 4)->instances);
    AVS_UNIT_ASSERT_NULL(
            _anjay_attr_storage_find_object(fas, 42)->default_attrs);
    AVS_UNIT_ASSERT_NULL(
            _anjay_attr_storage_find_object(fas, 42)->instances);
    AVS_UNIT_ASSERT_NULL(
            _anjay_attr_storage_find_object(fas, 517)->default_attrs);
    AVS_UNIT_ASSERT_NULL(
            _anjay_attr_storage_find_object(fas, 517)->instances);

    PERSISTENCE_TEST_FINISH;
}

static const char TEST_DATA_WITH_EMPTY_OID_ATTRS[] =
        MAGIC_HEADER
        "\x00\x00\x00\x01" // 3 objects
            "\x00\x04" // OID 4
                "\x00\x00\x00\x02" // 2 object-level default attrs
                    "\x00\x0E" // SSID 14
                        "\xFF\xFF\xFF\xFF" // min period
                        "\x00\x00\x00\x03" // max period
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // greater than
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // less than
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // step
                    "\x00\x21" // SSID 33
                /********* EMPTY ATTRIBUTES FOLLOW *********/
                        "\xFF\xFF\xFF\xFF" // min period
                        "\xFF\xFF\xFF\xFF" // max period
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // greater than
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // less than
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // step
                /*********** EMPTY ATTRIBUTES END ***********/
                "\x00\x00\x00\x00"; // 0 instance entries


static const char TEST_DATA_WITH_EMPTY_IID_ATTRS[] =
        MAGIC_HEADER
        "\x00\x00\x00\x01" // 3 objects
            "\x00\x2A" // OID 42
                "\x00\x00\x00\x00" // 0 object-level default attrs
                "\x00\x00\x00\x01" // 1 instance entry
                    "\x00\x01" // IID 1
                        "\x00\x00\x00\x01" // 1 instance-level default attr
                            "\x00\x02" // SSID 2
                        /********* EMPTY ATTRIBUTES FOLLOW *********/
                                "\xFF\xFF\xFF\xFF" // min period
                                "\xFF\xFF\xFF\xFF" // max period
                                "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                                "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                                "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                        /*********** EMPTY ATTRIBUTES END ***********/
                        "\x00\x00\x00\x01" // 1 resource entry
                            "\x00\x03" // RID 3
                                "\x00\x00\x00\x01" // 2 attr entries
                                    "\x00\x02" // SSID 2
                                        "\x00\x00\x00\x01" // min period
                                        "\x00\x00\x00\x0E" // max period
                    /* greater than */  "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                    /* less than */     "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                    /* step */          "\x7f\xf8\x00\x00\x00\x00\x00\x00";

static const char TEST_DATA_WITH_EMPTY_RID_ATTRS[] =
        MAGIC_HEADER
        "\x00\x00\x00\x01" // 3 objects
            "\x02\x05" // OID 517
                "\x00\x00\x00\x00" // 0 object-level default attrs
                "\x00\x00\x00\x01" // 1 instance entry
                    "\x02\x04" // IID 516
                        "\x00\x00\x00\x00" // 0 instance-level default attrs
                        "\x00\x00\x00\x01" // 1 resource entry
                            "\x02\x03" // RID 515
                                "\x00\x00\x00\x01" // 1 attr entry
                                    "\x02\x02" // SSID 514
                                /********* EMPTY ATTRIBUTES FOLLOW *********/
                                        "\xFF\xFF\xFF\xFF" // min period
                                        "\xFF\xFF\xFF\xFF" // max period
                    /* greater than */  "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                    /* less than */     "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                    /* step */          "\x7f\xf8\x00\x00\x00\x00\x00\x00";
                                /*********** EMPTY ATTRIBUTES END ***********/

#define DEFINE_UNIT_TEST_RESTORE_DATA_WITH_EMPTY(Suffix) \
AVS_UNIT_TEST(attr_storage_persistence, restore_data_with_empty_##Suffix) { \
    RESTORE_TEST_INIT(TEST_DATA_WITH_EMPTY_##Suffix); \
    INSTALL_FAKE_OBJECT(4, 5); \
    INSTALL_FAKE_OBJECT(42, 5); \
    INSTALL_FAKE_OBJECT(517, 522); \
    \
    AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_restore( \
            fas, (avs_stream_abstract_t *) &inbuf)); \
    \
    AVS_UNIT_ASSERT_NULL( \
            _anjay_attr_storage_find_object(fas, 4)->default_attrs); \
    AVS_UNIT_ASSERT_NULL( \
            _anjay_attr_storage_find_object(fas, 4)->instances); \
    AVS_UNIT_ASSERT_NULL( \
            _anjay_attr_storage_find_object(fas, 42)->default_attrs); \
    AVS_UNIT_ASSERT_NULL( \
            _anjay_attr_storage_find_object(fas, 42)->instances); \
    AVS_UNIT_ASSERT_NULL( \
            _anjay_attr_storage_find_object(fas, 517)->default_attrs); \
    AVS_UNIT_ASSERT_NULL( \
            _anjay_attr_storage_find_object(fas, 517)->instances); \
    \
    PERSISTENCE_TEST_FINISH; \
}

DEFINE_UNIT_TEST_RESTORE_DATA_WITH_EMPTY(OID_ATTRS)
DEFINE_UNIT_TEST_RESTORE_DATA_WITH_EMPTY(IID_ATTRS)
DEFINE_UNIT_TEST_RESTORE_DATA_WITH_EMPTY(RID_ATTRS)

AVS_UNIT_TEST(attr_storage_persistence, restore_data_with_bad_magic) {
    static const char DATA[] = "FBS0\x00\x00\x00\x00";

    RESTORE_TEST_INIT(DATA);
    INSTALL_FAKE_OBJECT(4, 5);
    INSTALL_FAKE_OBJECT(42, 5);
    INSTALL_FAKE_OBJECT(517, 522);

    AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_restore(
            fas, (avs_stream_abstract_t *) &inbuf));

    AVS_UNIT_ASSERT_NULL(
            _anjay_attr_storage_find_object(fas, 4)->default_attrs);
    AVS_UNIT_ASSERT_NULL(
            _anjay_attr_storage_find_object(fas, 4)->instances);
    AVS_UNIT_ASSERT_NULL(
            _anjay_attr_storage_find_object(fas, 42)->default_attrs);
    AVS_UNIT_ASSERT_NULL(
            _anjay_attr_storage_find_object(fas, 42)->instances);
    AVS_UNIT_ASSERT_NULL(
            _anjay_attr_storage_find_object(fas, 517)->default_attrs);
    AVS_UNIT_ASSERT_NULL(
            _anjay_attr_storage_find_object(fas, 517)->instances);

    PERSISTENCE_TEST_FINISH;
}

static const char TEST_DATA_DUPLICATE_OID[] =
        MAGIC_HEADER
        "\x00\x00\x00\x02" // 2 objects
            "\x00\x04" // OID 4
                "\x00\x00\x00\x01" // 1 object-level default attr
                    "\x00\x0E" // SSID 14
                        "\xFF\xFF\xFF\xFF" // min period
                        "\x00\x00\x00\x03" // max period
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // greater than
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // less than
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // step
                "\x00\x00\x00\x00" // 0 instance entries
            "\x00\x04" // OID 4
                "\x00\x00\x00\x01" // 1 object-level default attr
                    "\x00\x07" // SSID 7
                        "\xFF\xFF\xFF\xFF" // min period
                        "\x00\x00\x00\x03" // max period
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // greater than
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // less than
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // step
                "\x00\x00\x00\x00"; // 0 instance entries

AVS_UNIT_TEST(attr_storage_persistence, restore_duplicate_oid) {
    RESTORE_TEST_INIT(TEST_DATA_DUPLICATE_OID);
    INSTALL_FAKE_OBJECT(4, 5);

    // this will be cleared
    write_inst_attrs(fas, 4, 5, 6,
                     &(const anjay_dm_attributes_t) {
                         .min_period = 7,
                         .max_period = 8
                     });

    _anjay_mock_dm_expect_instance_it(FAKE_ANJAY, &OBJ4, 0, 0,
                                      ANJAY_IID_INVALID);
    AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_restore(
            fas, (avs_stream_abstract_t *) &inbuf));

    AVS_UNIT_ASSERT_NULL(
            _anjay_attr_storage_find_object(fas, 4)->default_attrs);
    AVS_UNIT_ASSERT_NULL(
            _anjay_attr_storage_find_object(fas, 4)->instances);

    PERSISTENCE_TEST_FINISH;
}

// TODO: Actually test removing nonexistent IIDs and RIDs
