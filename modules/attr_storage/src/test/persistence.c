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

#include <string.h>

#include <avsystem/commons/stream.h>
#include <avsystem/commons/stream/stream_inbuf.h>
#include <avsystem/commons/stream/stream_outbuf.h>
#include <avsystem/commons/unit/test.h>

#include <anjay/attr_storage.h>

#include <anjay_test/dm.h>

#include "../mod_attr_storage.h"
#include "attr_storage_test.h"

#define PERSIST_TEST_INIT(Size)                                        \
    char buf[Size];                                                    \
    avs_stream_outbuf_t outbuf = AVS_STREAM_OUTBUF_STATIC_INITIALIZER; \
    avs_stream_outbuf_set_buffer(&outbuf, buf, sizeof(buf));           \
    anjay_t *anjay = _anjay_test_dm_init(DM_TEST_CONFIGURATION());     \
    AVS_UNIT_ASSERT_NOT_NULL(anjay);                                   \
    AVS_UNIT_ASSERT_SUCCESS(anjay_attr_storage_install(anjay))

#define PERSISTENCE_TEST_FINISH        \
    do {                               \
        _anjay_mock_dm_expect_clean(); \
        _anjay_test_dm_finish(anjay);  \
    } while (0)

#define PERSIST_TEST_CHECK(Data)                                        \
    do {                                                                \
        AVS_UNIT_ASSERT_EQUAL(sizeof(Data) - 1,                         \
                              avs_stream_outbuf_offset(&outbuf));       \
        AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(Data, buf, sizeof(Data) - 1); \
        PERSISTENCE_TEST_FINISH;                                        \
    } while (0)

#define MAGIC_HEADER_V0 "FAS\0"
#define MAGIC_HEADER_V2 "FAS\2"

AVS_UNIT_TEST(attr_storage_persistence, persist_empty) {
    PERSIST_TEST_INIT(256);
    AVS_UNIT_ASSERT_SUCCESS(anjay_attr_storage_persist(
            anjay, (avs_stream_abstract_t *) &outbuf));
    PERSIST_TEST_CHECK(MAGIC_HEADER_V2 "\x00\x00\x00\x00");
}

#define INSTALL_FAKE_OBJECT(Oid, ...)                                   \
    const anjay_dm_object_def_t *const OBJ##Oid =                       \
            &(const anjay_dm_object_def_t) {                            \
                .oid = Oid,                                             \
                .supported_rids = ANJAY_DM_SUPPORTED_RIDS(__VA_ARGS__), \
                .handlers = { ANJAY_MOCK_DM_HANDLERS_NOATTRS }          \
            };                                                          \
    AVS_UNIT_ASSERT_SUCCESS(anjay_register_object(anjay, &OBJ##Oid))

static void write_obj_attrs(anjay_t *anjay,
                            anjay_oid_t oid,
                            anjay_ssid_t ssid,
                            const anjay_dm_internal_attrs_t *attrs) {
    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, oid);
    AVS_UNIT_ASSERT_NOT_NULL(obj);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_object_write_default_attrs(
            anjay, obj, ssid, attrs, NULL));
}

static void write_inst_attrs(anjay_t *anjay,
                             anjay_oid_t oid,
                             anjay_iid_t iid,
                             anjay_ssid_t ssid,
                             const anjay_dm_internal_attrs_t *attrs) {
    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, oid);
    AVS_UNIT_ASSERT_NOT_NULL(obj);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_instance_write_default_attrs(
            anjay, obj, iid, ssid, attrs, NULL));
}

static void write_res_attrs(anjay_t *anjay,
                            anjay_oid_t oid,
                            anjay_iid_t iid,
                            anjay_rid_t rid,
                            anjay_ssid_t ssid,
                            const anjay_dm_internal_res_attrs_t *attrs) {
    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, oid);
    AVS_UNIT_ASSERT_NOT_NULL(obj);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_resource_write_attrs(anjay, obj, iid, rid,
                                                           ssid, attrs, NULL));
}

static const char PERSIST_TEST_DATA[] =
        MAGIC_HEADER_V2 "\x00\x00\x00\x03" // 3 objects
                        "\x00\x04"         // OID 4
                        "\x00\x00\x00\x02" // 2 object-level default attrs
                        "\x00\x0E"         // SSID 14
                        "\xFF\xFF\xFF\xFF" // min period
                        "\x00\x00\x00\x03" // max period
                        "\xFF"             // confirmable
                        "\x00\x21"         // SSID 33
                        "\x00\x00\x00\x2A" // min period
                        "\xFF\xFF\xFF\xFF" // max period
                        "\x00"             // confirmable
                        "\x00\x00\x00\x00" // 0 instance entries
                        "\x00\x2A"         // OID 42
                        "\x00\x00\x00\x00" // 0 object-level default attrs
                        "\x00\x00\x00\x01" // 1 instance entry
                        "\x00\x01"         // IID 1
                        "\x00\x00\x00\x01" // 1 instance-level default attr
                        "\x00\x02"         // SSID 2
                        "\x00\x00\x00\x07" // min period
                        "\x00\x00\x00\x0D" // max period
                        "\xFF"             // confirmable
                        "\x00\x00\x00\x01" // 1 resource entry
                        "\x00\x03"         // RID 3
                        "\x00\x00\x00\x02" // 2 attr entries
                        "\x00\x02"         // SSID 2
                        "\xFF\xFF\xFF\xFF" // min period
                        "\xFF\xFF\xFF\xFF" // max period
                        /* greater than */ "\x3F\xF0\x00\x00\x00\x00\x00\x00"
                        /* less than */ "\xBF\xF0\x00\x00\x00\x00\x00\x00"
                        /* step */ "\x7F\xF8\x00\x00\x00\x00\x00\x00"
                        "\x01"             // confirmable
                        "\x00\x07"         // SSID 7
                        "\x00\x00\x00\x01" // min period
                        "\x00\x00\x00\x0E" // max period
                        /* greater than */ "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                        /* less than */ "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                        /* step */ "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                        "\xFF"             // confirmable
                        "\x02\x05"         // OID 517
                        "\x00\x00\x00\x00" // 0 object-level default attrs
                        "\x00\x00\x00\x01" // 1 instance entry
                        "\x02\x04"         // IID 516
                        "\x00\x00\x00\x00" // 0 instance-level default attrs
                        "\x00\x00\x00\x01" // 1 resource entry
                        "\x02\x03"         // RID 515
                        "\x00\x00\x00\x01" // 1 attr entry
                        "\x02\x02"         // SSID 514
                        "\x00\x00\x00\x21" // min period
                        "\xFF\xFF\xFF\xFF" // max period
                        /* greater than */ "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                        /* less than */ "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                        /* step */ "\x40\x45\x00\x00\x00\x00\x00\x00"
                        "\xFF"; // confirmable

#ifdef WITH_CUSTOM_ATTRIBUTES
static void persist_test_fill(anjay_t *anjay) {
    write_obj_attrs(anjay, 4, 33,
                    &(const anjay_dm_internal_attrs_t) {
                        .custom = {
                            .data = {
                                .con = ANJAY_DM_CON_ATTR_NON
                            }
                        },
                        .standard = {
                            .min_period = 42,
                            .max_period = ANJAY_ATTRIB_PERIOD_NONE
                        }
                    });
    write_obj_attrs(anjay, 4, 14,
                    &(const anjay_dm_internal_attrs_t) {
                            _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER.standard = {
                                .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                                .max_period = 3
                            } });
    write_inst_attrs(anjay, 42, 1, 2,
                     &(const anjay_dm_internal_attrs_t) {
                             _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER.standard = {
                                 .min_period = 7,
                                 .max_period = 13
                             } });
    write_res_attrs(anjay, 42, 1, 3, 2,
                    &(const anjay_dm_internal_res_attrs_t) {
                        .custom = {
                            .data = {
                                .con = ANJAY_DM_CON_ATTR_CON
                            }
                        },
                        .standard = {
                            .common = {
                                .min_period = ANJAY_ATTRIB_PERIOD_NONE,
                                .max_period = ANJAY_ATTRIB_PERIOD_NONE
                            },
                            .greater_than = 1.0,
                            .less_than = -1.0,
                            .step = ANJAY_ATTRIB_VALUE_NONE
                        }
                    });
    write_res_attrs(anjay, 42, 1, 3, 7,
                    &(const anjay_dm_internal_res_attrs_t) {
                            _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER.standard = {
                                .common = {
                                    .min_period = 1,
                                    .max_period = 14
                                },
                                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                                .step = ANJAY_ATTRIB_VALUE_NONE
                            } });
    write_res_attrs(anjay, 517, 516, 515, 514,
                    &(const anjay_dm_internal_res_attrs_t) {
                            _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER.standard = {
                                .common = {
                                    .min_period = 33,
                                    .max_period = ANJAY_ATTRIB_PERIOD_NONE
                                },
                                .greater_than = ANJAY_ATTRIB_VALUE_NONE,
                                .less_than = ANJAY_ATTRIB_VALUE_NONE,
                                .step = 42.0
                            } });
}

AVS_UNIT_TEST(attr_storage_persistence, persist_full) {
    PERSIST_TEST_INIT(512);
    INSTALL_FAKE_OBJECT(4, 3);
    INSTALL_FAKE_OBJECT(42, 3);
    INSTALL_FAKE_OBJECT(517, 3, 515);
    persist_test_fill(anjay);
    AVS_UNIT_ASSERT_SUCCESS(anjay_attr_storage_persist(
            anjay, (avs_stream_abstract_t *) &outbuf));
    PERSIST_TEST_CHECK(PERSIST_TEST_DATA);
}

AVS_UNIT_TEST(attr_storage_persistence, persist_not_enough_space) {
    PERSIST_TEST_INIT(128);
    INSTALL_FAKE_OBJECT(4, 3);
    INSTALL_FAKE_OBJECT(42, 3);
    INSTALL_FAKE_OBJECT(517, 3, 515);
    persist_test_fill(anjay);
    AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_persist(
            anjay, (avs_stream_abstract_t *) &outbuf));
    PERSISTENCE_TEST_FINISH;
}
#endif // WITH_CUSTOM_ATTRIBUTES

#define RESTORE_TEST_INIT(Data)                                     \
    avs_stream_inbuf_t inbuf = AVS_STREAM_INBUF_STATIC_INITIALIZER; \
    avs_stream_inbuf_set_buffer(&inbuf, (Data), sizeof(Data) - 1);  \
    anjay_t *anjay = _anjay_test_dm_init(DM_TEST_CONFIGURATION());  \
    AVS_UNIT_ASSERT_NOT_NULL(anjay);                                \
    AVS_UNIT_ASSERT_SUCCESS(anjay_attr_storage_install(anjay))

AVS_UNIT_TEST(attr_storage_persistence, restore_empty) {
    RESTORE_TEST_INIT("");
    AVS_UNIT_ASSERT_SUCCESS(anjay_attr_storage_restore(
            anjay, (avs_stream_abstract_t *) &inbuf));
    PERSISTENCE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage_persistence, restore_no_objects) {
    RESTORE_TEST_INIT(PERSIST_TEST_DATA);
    AVS_UNIT_ASSERT_SUCCESS(anjay_attr_storage_restore(
            anjay, (avs_stream_abstract_t *) &inbuf));
    AVS_UNIT_ASSERT_NULL(_anjay_attr_storage_get(anjay)->objects);
    PERSISTENCE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage_persistence, restore_one_object) {
    RESTORE_TEST_INIT(PERSIST_TEST_DATA);
    INSTALL_FAKE_OBJECT(42, 3);

    _anjay_mock_dm_expect_instance_it(anjay, &OBJ42, 0, 0, 1);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ42, 1, 0, ANJAY_IID_INVALID);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ42, 1, 3, 1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_attr_storage_restore(
            anjay, (avs_stream_abstract_t *) &inbuf));

    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(_anjay_attr_storage_get(anjay)->objects), 1);
    assert_object_equal(
            _anjay_attr_storage_get(anjay)->objects,
            test_object_entry(
                    42, NULL,
                    test_instance_entry(
                            1,
                            test_default_attrlist(
                                    test_default_attrs(
                                            2, 7, 13,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    3,
                                    test_resource_attrs(
                                            2,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            1.0,
                                            -1.0,
                                            ANJAY_ATTRIB_VALUE_NONE,
                                            ANJAY_DM_CON_ATTR_CON),
                                    test_resource_attrs(
                                            7,
                                            1,
                                            14,
                                            ANJAY_ATTRIB_VALUE_NONE,
                                            ANJAY_ATTRIB_VALUE_NONE,
                                            ANJAY_ATTRIB_VALUE_NONE,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    NULL));
    PERSISTENCE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage_persistence, restore_all_objects) {
    RESTORE_TEST_INIT(PERSIST_TEST_DATA);
    INSTALL_FAKE_OBJECT(4, 3);
    INSTALL_FAKE_OBJECT(42, 3);
    INSTALL_FAKE_OBJECT(69, 3);
    INSTALL_FAKE_OBJECT(514, 515);
    INSTALL_FAKE_OBJECT(517, 515);

    // this will be cleared
    write_inst_attrs(anjay, 69, 68, 67,
                     &(const anjay_dm_internal_attrs_t) {
                             _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER.standard = {
                                 .min_period = 66,
                                 .max_period = 65
                             } });

    _anjay_mock_dm_expect_instance_it(anjay, &OBJ4, 0, 0, ANJAY_IID_INVALID);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ42, 0, 0, 1);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ42, 1, 0, ANJAY_IID_INVALID);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ42, 1, 3, 1);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ517, 0, 0, 516);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ517, 1, 0, ANJAY_IID_INVALID);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ517, 516, 515, 1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_attr_storage_restore(
            anjay, (avs_stream_abstract_t *) &inbuf));

    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(_anjay_attr_storage_get(anjay)->objects), 3);

    // object 4
    assert_object_equal(
            _anjay_attr_storage_get(anjay)->objects,
            test_object_entry(
                    4,
                    test_default_attrlist(
                            test_default_attrs(14, ANJAY_ATTRIB_PERIOD_NONE, 3,
                                               ANJAY_DM_CON_ATTR_DEFAULT),
                            test_default_attrs(33, 42, ANJAY_ATTRIB_PERIOD_NONE,
                                               ANJAY_DM_CON_ATTR_NON),
                            NULL),
                    NULL));

    // object 42
    assert_object_equal(
            AVS_LIST_NEXT(_anjay_attr_storage_get(anjay)->objects),
            test_object_entry(
                    42, NULL,
                    test_instance_entry(
                            1,
                            test_default_attrlist(
                                    test_default_attrs(
                                            2, 7, 13,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            test_resource_entry(
                                    3,
                                    test_resource_attrs(
                                            2,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            1.0,
                                            -1.0,
                                            ANJAY_ATTRIB_VALUE_NONE,
                                            ANJAY_DM_CON_ATTR_CON),
                                    test_resource_attrs(
                                            7,
                                            1,
                                            14,
                                            ANJAY_ATTRIB_VALUE_NONE,
                                            ANJAY_ATTRIB_VALUE_NONE,
                                            ANJAY_ATTRIB_VALUE_NONE,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    NULL));

    // object 517
    assert_object_equal(
            AVS_LIST_NTH(_anjay_attr_storage_get(anjay)->objects, 2),
            test_object_entry(
                    517, NULL,
                    test_instance_entry(
                            516,
                            NULL,
                            test_resource_entry(
                                    515,
                                    test_resource_attrs(
                                            514,
                                            33,
                                            ANJAY_ATTRIB_PERIOD_NONE,
                                            ANJAY_ATTRIB_VALUE_NONE,
                                            ANJAY_ATTRIB_VALUE_NONE,
                                            42.0,
                                            ANJAY_DM_CON_ATTR_DEFAULT),
                                    NULL),
                            NULL),
                    NULL));
    PERSISTENCE_TEST_FINISH;
}

static const char CLEARING_TEST_DATA[] =
        MAGIC_HEADER_V0 "\x00\x00\x00\x02" // 2 objects
                        "\x00\x2A"         // OID 42
                        "\x00\x00\x00\x00" // 0 object-level default attrs
                        "\x00\x00\x00\x01" // 1 instance entry
                        "\x00\x01"         // IID 1
                        "\x00\x00\x00\x00" // 0 instance-level default attr
                        "\x00\x00\x00\x01" // 1 resource entry
                        "\x00\x03"         // RID 3
                        "\x00\x00\x00\x02" // 2 attr entries
                        "\x00\x02"         // SSID 2
                        "\xFF\xFF\xFF\xFF" // min period
                        "\xFF\xFF\xFF\xFF" // max period
                        /* greater than */ "\x3F\xF0\x00\x00\x00\x00\x00\x00"
                        /* less than */ "\xBF\xF0\x00\x00\x00\x00\x00\x00"
                        /* step */ "\x7F\xF8\x00\x00\x00\x00\x00\x00"
                        "\x00\x07"         // SSID 7
                        "\x00\x00\x00\x01" // min period
                        "\x00\x00\x00\x0E" // max period
                        /* greater than */ "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                        /* less than */ "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                        /* step */ "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                        "\x02\x05"         // OID 517
                        "\x00\x00\x00\x00" // 0 object-level default attrs
                        "\x00\x00\x00\x01" // 1 instance entry
                        "\x02\x04"         // IID 516
                        "\x00\x00\x00\x00" // 0 instance-level default attrs
                        "\x00\x00\x00\x01" // 1 resource entry
                        "\x02\x03"         // RID 515
                        "\x00\x00\x00\x01" // 1 attr entry
                        "\x02\x02"         // SSID 514
                        "\x00\x00\x00\x21" // min period
                        "\xFF\xFF\xFF\xFF" // max period
                        /* greater than */ "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                        /* less than */ "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                        /* step */ "\x40\x45\x00\x00\x00\x00\x00\x00";

AVS_UNIT_TEST(attr_storage_persistence, restore_no_instances) {
    RESTORE_TEST_INIT(CLEARING_TEST_DATA);
    INSTALL_FAKE_OBJECT(42, 3);
    INSTALL_FAKE_OBJECT(517, 3, 515);

    _anjay_mock_dm_expect_instance_it(anjay, &OBJ42, 0, 0, ANJAY_IID_INVALID);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ517, 0, 0, ANJAY_IID_INVALID);
    AVS_UNIT_ASSERT_SUCCESS(anjay_attr_storage_restore(
            anjay, (avs_stream_abstract_t *) &inbuf));
    AVS_UNIT_ASSERT_NULL(_anjay_attr_storage_get(anjay)->objects);
    PERSISTENCE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage_persistence, restore_no_supported_resources) {
    RESTORE_TEST_INIT(CLEARING_TEST_DATA);
    INSTALL_FAKE_OBJECT(42, 0);
    INSTALL_FAKE_OBJECT(517, 0);

    _anjay_mock_dm_expect_instance_it(anjay, &OBJ42, 0, 0, 1);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ42, 1, 0, ANJAY_IID_INVALID);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ517, 0, 0, 516);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ517, 1, 0, ANJAY_IID_INVALID);
    AVS_UNIT_ASSERT_SUCCESS(anjay_attr_storage_restore(
            anjay, (avs_stream_abstract_t *) &inbuf));
    AVS_UNIT_ASSERT_NULL(_anjay_attr_storage_get(anjay)->objects);
    PERSISTENCE_TEST_FINISH;
}

AVS_UNIT_TEST(attr_storage_persistence, restore_no_present_resources) {
    RESTORE_TEST_INIT(CLEARING_TEST_DATA);
    INSTALL_FAKE_OBJECT(42, 3);
    INSTALL_FAKE_OBJECT(517, 515);

    _anjay_mock_dm_expect_instance_it(anjay, &OBJ42, 0, 0, 1);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ42, 1, 0, ANJAY_IID_INVALID);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ42, 1, 3, 0);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ517, 0, 0, 516);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ517, 1, 0, ANJAY_IID_INVALID);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ517, 516, 515, 0);
    AVS_UNIT_ASSERT_SUCCESS(anjay_attr_storage_restore(
            anjay, (avs_stream_abstract_t *) &inbuf));
    AVS_UNIT_ASSERT_NULL(_anjay_attr_storage_get(anjay)->objects);
    PERSISTENCE_TEST_FINISH;
}

static const char RESTORE_BROKEN_DATA[] =
        MAGIC_HEADER_V0 "\x00\x00\x00\x03" // 3 objects
                        "\x00\x04"         // OID 4
                        "\x00\x00\x00\x02" // 2 object-level default attrs
                        "\x00\x0E"         // SSID 14
                        "\xFF\xFF\xFF\xFF" // min period
                        "\x00\x00\x00\x03" // max period
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // greater than
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // less than
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // step
                        "\x00\x21"                         // SSID 33
                        "\x00\x00\x00\x2A"                 // min period
                        "\xFF\xFF\xFF\xFF"                 // max period
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // greater than
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // less than
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // step
                        "\x00\x00\x00\x00"                 // 0 instance entries
                        "\x00\x2A"                         // OID 42
                        "\x00\x00\x00\x00" // 0 object-level default attrs
                        "\x00\x00\x00\x01" // 1 instance entry
                        "\x00\x01"         // IID 1
                        "\x00\x00\x00\x01" // 1 instance-level default attr
                        "\x00\x02"         // SSID 2
                        "\x00\x00\x00\x07" // min period
                        "\x00\x00\x00\x0D" // max period
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // greater than
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // less than
                        "\x7f\xf8\x00\x00\x00\x00\x00\x00" // step
                        "\x00\x00\x00\x01"                 // 1 resource entry
                        "\x00\x03"                         // RID 3
                        "\x00\x00\x00\x02"                 // 2 attr entries
                        "\x00\x02"                         // SSID 2
                        "\xFF\xFF\xFF\xFF"                 // min period
                        "\xFF\xFF\xFF\xFF"                 // max period
                        /* greater than */ "\x3F\xF0\x00\x00\x00\x00\x00\x00"
                        /* less than */ "\xBF\xF0\x00\x00\x00\x00\x00\x00"
                        "\x7f"; /* premature end of data */

AVS_UNIT_TEST(attr_storage_persistence, restore_broken_stream) {
    RESTORE_TEST_INIT(RESTORE_BROKEN_DATA);
    INSTALL_FAKE_OBJECT(4, 3);
    INSTALL_FAKE_OBJECT(42, 3);
    INSTALL_FAKE_OBJECT(517, 3, 515);

    // this will be cleared
    write_inst_attrs(anjay, 517, 518, 519,
                     &(const anjay_dm_internal_attrs_t) {
                             _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER.standard = {
                                 .min_period = 520,
                                 .max_period = 521,
                             } });

    AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_restore(
            anjay, (avs_stream_abstract_t *) &inbuf));

    AVS_UNIT_ASSERT_NULL(_anjay_attr_storage_get(anjay)->objects);
    PERSISTENCE_TEST_FINISH;
}

static const char INSANE_TEST_DATA[] =
        MAGIC_HEADER_V0 "\x00\x00\x00\x03" // 3 objects
                        "\x00\x04"         // OID 4
                        "\x00\x00\x00\x02" // 2 object-level default attrs
                        "\x00\x0E"         // SSID 14
                        "\xFF\xFF\xFF\xFF" // min period
                        "\x00\x00\x00\x03" // max period
                        "\x00\x21"         // SSID 33
                        "\x00\x00\x00\x2A" // min period
                        "\xFF\xFF\xFF\xFF" // max period
                        "\x00\x00\x00\x00" // 0 instance entries
                        "\x00\x2A"         // OID 42
                        "\x00\x00\x00\x00" // 0 object-level default attrs
                        "\x00\x00\x00\x01" // 1 instance entry
                        "\x00\x01"         // IID 1
                        "\x00\x00\x00\x01" // 1 instance-level default attr
                        "\x00\x02"         // SSID 2
                        "\x00\x00\x00\x07" // min period
                        "\x00\x00\x00\x0D" // max period
                        "\x00\x00\x00\x01" // 1 resource entry
                        "\x00\x03"         // RID 3
                        "\x00\x00\x00\x02" // 2 attr entries
                        /*********** INVALID SSID ORDER FOLLOW ***********/
                        "\x00\x07"         // SSID 7
                        "\x00\x00\x00\x01" // min period
                        "\x00\x00\x00\x0E" // max period
                        /* greater than */ "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                        /* less than */ "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                        /* step */ "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                        "\x00\x02"         // SSID 2
                        "\xFF\xFF\xFF\xFF" // min period
                        "\xFF\xFF\xFF\xFF" // max period
                        /* greater than */ "\x3F\xF0\x00\x00\x00\x00\x00\x00"
                        /* less than */ "\xBF\xF0\x00\x00\x00\x00\x00\x00"
                        /* step */ "\x7F\xF8\x00\x00\x00\x00\x00\x00"
                        /************ INVALID SSID ORDER END ************/
                        "\x02\x05"         // OID 517
                        "\x00\x00\x00\x00" // 0 object-level default attrs
                        "\x00\x00\x00\x01" // 1 instance entry
                        "\x02\x04"         // IID 516
                        "\x00\x00\x00\x00" // 0 instance-level default attrs
                        "\x00\x00\x00\x01" // 1 resource entry
                        "\x02\x03"         // RID 515
                        "\x00\x00\x00\x01" // 1 attr entry
                        "\x02\x02"         // SSID 514
                        "\x00\x00\x00\x21" // min period
                        "\xFF\xFF\xFF\xFF" // max period
                        /* greater than */ "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                        /* less than */ "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                        /* step */ "\x40\x45\x00\x00\x00\x00\x00\x00";

AVS_UNIT_TEST(attr_storage_persistence, restore_insane_data) {
    RESTORE_TEST_INIT(INSANE_TEST_DATA);
    INSTALL_FAKE_OBJECT(4, 3);
    INSTALL_FAKE_OBJECT(42, 3);
    INSTALL_FAKE_OBJECT(517, 3, 515);

    // this will be cleared
    write_inst_attrs(anjay, 517, 518, 519,
                     &(const anjay_dm_internal_attrs_t) {
                             _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER.standard = {
                                 .min_period = 520,
                                 .max_period = 521
                             } });

    AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_restore(
            anjay, (avs_stream_abstract_t *) &inbuf));

    AVS_UNIT_ASSERT_NULL(_anjay_attr_storage_get(anjay)->objects);
    PERSISTENCE_TEST_FINISH;
}

static const char TEST_DATA_WITH_EMPTY_OID_ATTRS[] =
        MAGIC_HEADER_V0 "\x00\x00\x00\x01" // 3 objects
                        "\x00\x04"         // OID 4
                        "\x00\x00\x00\x02" // 2 object-level default attrs
                        "\x00\x0E"         // SSID 14
                        "\xFF\xFF\xFF\xFF" // min period
                        "\x00\x00\x00\x03" // max period
                        "\x00\x21"         // SSID 33
                        /********* EMPTY ATTRIBUTES FOLLOW *********/
                        "\xFF\xFF\xFF\xFF" // min period
                        "\xFF\xFF\xFF\xFF" // max period
                        /*********** EMPTY ATTRIBUTES END ***********/
                        "\x00\x00\x00\x00"; // 0 instance entries

static const char TEST_DATA_WITH_EMPTY_IID_ATTRS[] =
        MAGIC_HEADER_V0 "\x00\x00\x00\x01" // 3 objects
                        "\x00\x2A"         // OID 42
                        "\x00\x00\x00\x00" // 0 object-level default attrs
                        "\x00\x00\x00\x01" // 1 instance entry
                        "\x00\x01"         // IID 1
                        "\x00\x00\x00\x01" // 1 instance-level default attr
                        "\x00\x02"         // SSID 2
                        /********* EMPTY ATTRIBUTES FOLLOW *********/
                        "\xFF\xFF\xFF\xFF" // min period
                        "\xFF\xFF\xFF\xFF" // max period
                        /*********** EMPTY ATTRIBUTES END ***********/
                        "\x00\x00\x00\x01" // 1 resource entry
                        "\x00\x03"         // RID 3
                        "\x00\x00\x00\x01" // 2 attr entries
                        "\x00\x02"         // SSID 2
                        "\x00\x00\x00\x01" // min period
                        "\x00\x00\x00\x0E" // max period
                        /* greater than */ "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                        /* less than */ "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                        /* step */ "\x7f\xf8\x00\x00\x00\x00\x00\x00";

static const char TEST_DATA_WITH_EMPTY_RID_ATTRS[] =
        MAGIC_HEADER_V0 "\x00\x00\x00\x01" // 3 objects
                        "\x02\x05"         // OID 517
                        "\x00\x00\x00\x00" // 0 object-level default attrs
                        "\x00\x00\x00\x01" // 1 instance entry
                        "\x02\x04"         // IID 516
                        "\x00\x00\x00\x00" // 0 instance-level default attrs
                        "\x00\x00\x00\x01" // 1 resource entry
                        "\x02\x03"         // RID 515
                        "\x00\x00\x00\x01" // 1 attr entry
                        "\x02\x02"         // SSID 514
                        /********* EMPTY ATTRIBUTES FOLLOW *********/
                        "\xFF\xFF\xFF\xFF" // min period
                        "\xFF\xFF\xFF\xFF" // max period
                        /* greater than */ "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                        /* less than */ "\x7f\xf8\x00\x00\x00\x00\x00\x00"
                        /* step */ "\x7f\xf8\x00\x00\x00\x00\x00\x00";
/*********** EMPTY ATTRIBUTES END ***********/

#define DEFINE_UNIT_TEST_RESTORE_DATA_WITH_EMPTY(Suffix)               \
    AVS_UNIT_TEST(attr_storage_persistence,                            \
                  restore_data_with_empty_##Suffix) {                  \
        RESTORE_TEST_INIT(TEST_DATA_WITH_EMPTY_##Suffix);              \
        INSTALL_FAKE_OBJECT(4, 3);                                     \
        INSTALL_FAKE_OBJECT(42, 3);                                    \
        INSTALL_FAKE_OBJECT(517, 3, 515);                              \
                                                                       \
        AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_restore(             \
                anjay, (avs_stream_abstract_t *) &inbuf));             \
                                                                       \
        AVS_UNIT_ASSERT_NULL(_anjay_attr_storage_get(anjay)->objects); \
        PERSISTENCE_TEST_FINISH;                                       \
    }

DEFINE_UNIT_TEST_RESTORE_DATA_WITH_EMPTY(OID_ATTRS)
DEFINE_UNIT_TEST_RESTORE_DATA_WITH_EMPTY(IID_ATTRS)
DEFINE_UNIT_TEST_RESTORE_DATA_WITH_EMPTY(RID_ATTRS)

AVS_UNIT_TEST(attr_storage_persistence, restore_data_with_bad_magic) {
    static const char DATA[] = "FBS0\x00\x00\x00\x00";

    RESTORE_TEST_INIT(DATA);
    INSTALL_FAKE_OBJECT(4, 3);
    INSTALL_FAKE_OBJECT(42, 3);
    INSTALL_FAKE_OBJECT(517, 3, 515);

    AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_restore(
            anjay, (avs_stream_abstract_t *) &inbuf));

    AVS_UNIT_ASSERT_NULL(_anjay_attr_storage_get(anjay)->objects);
    PERSISTENCE_TEST_FINISH;
}

static const char TEST_DATA_DUPLICATE_OID[] =
        MAGIC_HEADER_V0 "\x00\x00\x00\x02"  // 2 objects
                        "\x00\x04"          // OID 4
                        "\x00\x00\x00\x01"  // 1 object-level default attr
                        "\x00\x0E"          // SSID 14
                        "\xFF\xFF\xFF\xFF"  // min period
                        "\x00\x00\x00\x03"  // max period
                        "\x00\x00\x00\x00"  // 0 instance entries
                        "\x00\x04"          // OID 4
                        "\x00\x00\x00\x01"  // 1 object-level default attr
                        "\x00\x07"          // SSID 7
                        "\xFF\xFF\xFF\xFF"  // min period
                        "\x00\x00\x00\x03"  // max period
                        "\x00\x00\x00\x00"; // 0 instance entries

AVS_UNIT_TEST(attr_storage_persistence, restore_duplicate_oid) {
    RESTORE_TEST_INIT(TEST_DATA_DUPLICATE_OID);
    INSTALL_FAKE_OBJECT(4, 3);

    // this will be cleared
    write_inst_attrs(anjay, 4, 5, 6,
                     &(const anjay_dm_internal_attrs_t) {
                             _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER.standard = {
                                 .min_period = 7,
                                 .max_period = 8
                             } });

    AVS_UNIT_ASSERT_FAILED(anjay_attr_storage_restore(
            anjay, (avs_stream_abstract_t *) &inbuf));

    AVS_UNIT_ASSERT_NULL(_anjay_attr_storage_get(anjay)->objects);
    PERSISTENCE_TEST_FINISH;
}

// TODO: Actually test removing nonexistent IIDs and RIDs
