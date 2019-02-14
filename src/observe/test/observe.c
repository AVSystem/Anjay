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
#include <math.h>
#include <stdarg.h>

#include <avsystem/commons/unit/test.h>

#include <anjay_test/dm.h>
#include <anjay_test/mock_clock.h>

#include "../../anjay_core.h"
#include "../../sched_internal.h"

#include "../../servers/server_connections.h"
#include "../../servers/servers_internal.h"

#include "../../coap/test/utils.h"

static void assert_observe_size(anjay_t *anjay, size_t sz) {
    size_t result = 0;
    AVS_RBTREE_ELEM(anjay_observe_connection_entry_t) conn;
    AVS_RBTREE_FOREACH(conn, anjay->observe.connection_entries) {
        size_t local_size = AVS_RBTREE_SIZE(conn->entries);
        AVS_UNIT_ASSERT_NOT_EQUAL(local_size, 0);
        result += local_size;
    }
    AVS_UNIT_ASSERT_EQUAL(result, sz);
}

static void assert_msg_details_equal(const anjay_msg_details_t *a,
                                     const anjay_msg_details_t *b) {
    AVS_UNIT_ASSERT_EQUAL(a->msg_type, b->msg_type);
    AVS_UNIT_ASSERT_EQUAL(a->msg_code, b->msg_code);
    AVS_UNIT_ASSERT_EQUAL(a->format, b->format);
    AVS_UNIT_ASSERT_EQUAL(a->observe_serial, b->observe_serial);
    /* Yes, a pointer comparison */
    AVS_UNIT_ASSERT_TRUE(a->uri_path == b->uri_path);
    AVS_UNIT_ASSERT_TRUE(a->uri_query == b->uri_query);
    AVS_UNIT_ASSERT_TRUE(a->location_path == b->location_path);
}

static void assert_observe(anjay_t *anjay,
                           anjay_ssid_t ssid,
                           anjay_oid_t oid,
                           anjay_iid_t iid,
                           int32_t rid,
                           uint16_t format,
                           const anjay_msg_details_t *details,
                           const void *data,
                           size_t length) {
    anjay_observe_key_t key_query = {
        .connection = {
            .ssid = ssid,
            .type = ANJAY_CONNECTION_UDP
        },
        .oid = oid,
        .iid = iid,
        .rid = rid,
        .format = format
    };
    AVS_RBTREE_ELEM(anjay_observe_connection_entry_t) conn =
            AVS_RBTREE_FIND(anjay->observe.connection_entries,
                            connection_query(&key_query.connection));
    AVS_UNIT_ASSERT_NOT_NULL(conn);
    AVS_RBTREE_ELEM(anjay_observe_entry_t) entity =
            AVS_RBTREE_FIND(conn->entries,
                            _anjay_observe_entry_query(&key_query));
    AVS_UNIT_ASSERT_NOT_NULL(entity);
    AVS_UNIT_ASSERT_NULL(entity->last_unsent);
    AVS_UNIT_ASSERT_NOT_NULL(entity->last_sent);
    assert_msg_details_equal(&entity->last_sent->details, details);
    AVS_UNIT_ASSERT_EQUAL(entity->last_sent->value_length, length);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(entity->last_sent->value, data, length);
}

static void expect_server_res_read(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj,
                                   anjay_ssid_t ssid,
                                   anjay_rid_t rid,
                                   const anjay_mock_dm_data_t *data) {
    assert((*obj)->oid == ANJAY_DM_OID_SERVER);
    _anjay_mock_dm_expect_instance_it(anjay, obj, 0, 0, ssid);
    _anjay_mock_dm_expect_resource_present(anjay, obj, ssid,
                                           ANJAY_DM_RID_SERVER_SSID, 1);
    _anjay_mock_dm_expect_resource_read(anjay, obj, ssid,
                                        ANJAY_DM_RID_SERVER_SSID, 0,
                                        ANJAY_MOCK_DM_INT(0, ssid));
    _anjay_mock_dm_expect_resource_present(anjay, obj, ssid, rid, 1);
    _anjay_mock_dm_expect_resource_read(anjay, obj, ssid, rid, 0, data);
}

static void expect_read_notif_storing(anjay_t *anjay,
                                      const anjay_dm_object_def_t *const *obj,
                                      anjay_ssid_t ssid,
                                      bool value) {
    expect_server_res_read(anjay, obj, ssid,
                           ANJAY_DM_RID_SERVER_NOTIFICATION_STORING,
                           ANJAY_MOCK_DM_BOOL(0, value));
}

#define ASSERT_SUCCESS_TEST_RESULT(Ssid)                         \
    assert_observe(anjay, Ssid, 42, 69, 4, AVS_COAP_FORMAT_NONE, \
                   &(const anjay_msg_details_t) {                \
                       .msg_type = AVS_COAP_MSG_ACKNOWLEDGEMENT, \
                       .msg_code = AVS_COAP_CODE_CONTENT,        \
                       .format = ANJAY_COAP_FORMAT_PLAINTEXT,    \
                       .observe_serial = true                    \
                   },                                            \
                   "514", 3)

#define SUCCESS_TEST(...)                                                   \
    DM_TEST_INIT_WITH_SSIDS(__VA_ARGS__);                                   \
    do {                                                                    \
        for (size_t i = 0; i < AVS_ARRAY_SIZE(ssids); ++i) {                \
            DM_TEST_REQUEST(mocksocks[i], CON, GET, ID(0xFA3E), OBSERVE(0), \
                            PATH("42", "69", "4"));                         \
            _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);     \
            _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);  \
            _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,      \
                                                ANJAY_MOCK_DM_INT(0, 514)); \
            DM_TEST_EXPECT_READ_NULL_ATTRS(ssids[i], 69, 4);                \
            DM_TEST_EXPECT_RESPONSE(mocksocks[i], ACK, CONTENT, ID(0xFA3E), \
                                    OBSERVE(0xF40000),                      \
                                    CONTENT_FORMAT(PLAINTEXT),              \
                                    PAYLOAD("514"));                        \
            AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[i]));      \
            assert_observe_size(anjay, i + 1);                              \
            ASSERT_SUCCESS_TEST_RESULT(ssids[i]);                           \
        }                                                                   \
        AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));                    \
    } while (0)

AVS_UNIT_TEST(observe, simple) {
    SUCCESS_TEST(14);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(observe, read_failed) {
    DM_TEST_INIT_WITH_SSIDS(4);
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID(0xFA3E), OBSERVE(0),
                    PATH("42", "5", "7"));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 5, 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, NOT_FOUND, ID(0xFA3E),
                            NO_PAYLOAD);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    assert_observe_size(anjay, 0);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(observe, read_attrs_failed) {
    DM_TEST_INIT_WITH_SSIDS(4);
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID(0xFA3E), OBSERVE(0),
                    PATH("42", "69", "4"));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_INT(0, 514));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read_attrs(anjay, &OBJ, 69, 4, 4, -1, NULL);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT, ID(0xFA3E),
                            CONTENT_FORMAT(PLAINTEXT), PAYLOAD("514"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    assert_observe_size(anjay, 0);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(observe, overwrite) {
    SUCCESS_TEST(14);
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID(0xFA3E), OBSERVE(0),
                    PATH("42", "69", "4"));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(
            anjay, &OBJ, 69, 4, 0,
            ANJAY_MOCK_DM_ARRAY(
                    0,
                    ANJAY_MOCK_DM_ARRAY_ENTRY(4, ANJAY_MOCK_DM_INT(0, 777)),
                    ANJAY_MOCK_DM_ARRAY_ENTRY(7,
                                              ANJAY_MOCK_DM_STRING(0, "Hi!"))));
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
#define TLV_RESPONSE   \
    "\x88\x04\x09"     \
    "\x42\x04\x03\x09" \
    "\x43\x07"         \
    "Hi!"
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT, ID(0xFA3E),
                            OBSERVE(0xF40000), CONTENT_FORMAT(TLV),
                            PAYLOAD(TLV_RESPONSE));
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    assert_observe_size(anjay, 1);
    assert_observe(anjay, 14, 42, 69, 4, AVS_COAP_FORMAT_NONE,
                   &(const anjay_msg_details_t) {
                       .msg_type = AVS_COAP_MSG_ACKNOWLEDGEMENT,
                       .msg_code = AVS_COAP_CODE_CONTENT,
                       .format = ANJAY_COAP_FORMAT_TLV,
                       .observe_serial = true
                   },
                   TLV_RESPONSE, sizeof(TLV_RESPONSE) - 1);
#undef TLV_RESPONSE
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(observe, instance) {
    SUCCESS_TEST(14);
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID(0xFA3E), OBSERVE(0),
                    PATH("42", "69"));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 0, 0);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 1, 0);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 2, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 2, 0,
                                        ANJAY_MOCK_DM_STRING(0, "wow"));
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 3, 0);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_STRING(0, "such value"));
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 5, 0);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 6, 0);
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, -1);
#define TLV_RESPONSE \
    "\xc3\x02"       \
    "wow"            \
    "\xc8\x04\x0a"   \
    "such value"
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT, ID(0xFA3E),
                            OBSERVE(0xF40000), CONTENT_FORMAT(TLV),
                            PAYLOAD(TLV_RESPONSE));
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    assert_observe_size(anjay, 2);
    ASSERT_SUCCESS_TEST_RESULT(14);
    assert_observe(anjay, 14, 42, 69, -1, AVS_COAP_FORMAT_NONE,
                   &(const anjay_msg_details_t) {
                       .msg_type = AVS_COAP_MSG_ACKNOWLEDGEMENT,
                       .msg_code = AVS_COAP_CODE_CONTENT,
                       .format = ANJAY_COAP_FORMAT_TLV,
                       .observe_serial = true
                   },
                   TLV_RESPONSE, sizeof(TLV_RESPONSE) - 1);
#undef TLV_RESPONSE
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(observe, cancel_reset) {
    SUCCESS_TEST(14);
    const avs_coap_msg_t *request =
            COAP_MSG(RST, EMPTY, ID(0x3EFA), NO_PAYLOAD);
    avs_unit_mocksock_input(mocksocks[0], request->content, request->length);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    assert_observe_size(anjay, 1);
    const avs_coap_msg_t *request2 =
            COAP_MSG(RST, EMPTY, ID(0xFA3E), NO_PAYLOAD);
    avs_unit_mocksock_input(mocksocks[0], request2->content, request2->length);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    assert_observe_size(anjay, 0);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(observe, cancel_deregister) {
    SUCCESS_TEST(14);
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID(0xFA3E), OBSERVE(0x01),
                    PATH("42", "69", "6"));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 6, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 6, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Hello"));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT, ID(0xFA3E),
                            CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Hello"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    assert_observe_size(anjay, 1);
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID(0xFA3E), OBSERVE(0x01),
                    PATH("42", "69", "4"));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Good-bye"));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT, ID(0xFA3E),
                            CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Good-bye"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    assert_observe_size(anjay, 0);
    DM_TEST_FINISH;
}

static inline void remove_server(anjay_t *anjay,
                                 AVS_LIST(anjay_server_info_t) *server_ptr) {
    anjay_server_connection_t *connection =
            _anjay_get_server_connection((const anjay_connection_ref_t) {
                .server = *server_ptr,
                .conn_type = ANJAY_CONNECTION_UDP
            });
    AVS_UNIT_ASSERT_NOT_NULL(connection);
    _anjay_connection_internal_clean_socket(anjay, connection);
    AVS_LIST_DELETE(server_ptr);
}

AVS_UNIT_TEST(observe, gc) {
    SUCCESS_TEST(14, 69, 514, 666, 777);

    remove_server(anjay, &anjay->servers->servers);

    _anjay_observe_gc(anjay);
    assert_observe_size(anjay, 4);
    ASSERT_SUCCESS_TEST_RESULT(69);
    ASSERT_SUCCESS_TEST_RESULT(514);
    ASSERT_SUCCESS_TEST_RESULT(666);
    ASSERT_SUCCESS_TEST_RESULT(777);

    remove_server(anjay, AVS_LIST_NTH_PTR(&anjay->servers->servers, 3));

    _anjay_observe_gc(anjay);
    assert_observe_size(anjay, 3);
    ASSERT_SUCCESS_TEST_RESULT(69);
    ASSERT_SUCCESS_TEST_RESULT(514);
    ASSERT_SUCCESS_TEST_RESULT(666);

    remove_server(anjay, AVS_LIST_NTH_PTR(&anjay->servers->servers, 1));

    _anjay_observe_gc(anjay);
    assert_observe_size(anjay, 2);
    ASSERT_SUCCESS_TEST_RESULT(69);
    ASSERT_SUCCESS_TEST_RESULT(666);

    DM_TEST_FINISH;
}

static void expect_read_res_attrs(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr,
                                  anjay_ssid_t ssid,
                                  anjay_iid_t iid,
                                  anjay_rid_t rid,
                                  const anjay_dm_internal_res_attrs_t *attrs) {
    _anjay_mock_dm_expect_instance_present(anjay, obj_ptr, iid, 1);
    _anjay_mock_dm_expect_resource_present(anjay, obj_ptr, iid, rid, 1);
    _anjay_mock_dm_expect_resource_read_attrs(anjay, obj_ptr, iid, rid, ssid, 0,
                                              attrs);
    if (!_anjay_dm_attributes_full(
                _anjay_dm_get_internal_attrs_const(&attrs->standard.common))) {
        _anjay_mock_dm_expect_instance_read_default_attrs(
                anjay, obj_ptr, iid, ssid, 0, &ANJAY_DM_INTERNAL_ATTRS_EMPTY);
        _anjay_mock_dm_expect_object_read_default_attrs(
                anjay, obj_ptr, ssid, 0, &ANJAY_DM_INTERNAL_ATTRS_EMPTY);
    }
}

static void expect_read_res(anjay_t *anjay,
                            const anjay_dm_object_def_t *const *obj_ptr,
                            anjay_iid_t iid,
                            anjay_rid_t rid,
                            const anjay_mock_dm_data_t *data) {
    _anjay_mock_dm_expect_instance_present(anjay, obj_ptr, iid, 1);
    _anjay_mock_dm_expect_resource_present(anjay, obj_ptr, iid, rid, 1);
    _anjay_mock_dm_expect_resource_read(anjay, obj_ptr, iid, rid, 0, data);
}

static const avs_coap_msg_identity_t NULL_IDENTITY = { 0 };

static void notify_max_period_test(const char *con_notify_ack,
                                   size_t con_notify_ack_size,
                                   size_t observe_size_after_ack) {
    static const anjay_dm_internal_res_attrs_t ATTRS = {
        .standard = {
            .common = {
                .min_period = 1,
                .max_period = 10
            },
            .greater_than = ANJAY_ATTRIB_VALUE_NONE,
            .less_than = ANJAY_ATTRIB_VALUE_NONE,
            .step = ANJAY_ATTRIB_VALUE_NONE
        }
    };

    ////// INITIALIZATION //////
    DM_TEST_INIT_WITH_SSIDS(14);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_observe_put_entry(
            anjay,
            &(const anjay_observe_key_t) { { 14, ANJAY_CONNECTION_UDP },
                                           42,
                                           69,
                                           4,
                                           AVS_COAP_FORMAT_NONE },
            &(const anjay_msg_details_t) {
                .msg_type = AVS_COAP_MSG_ACKNOWLEDGEMENT,
                .msg_code = AVS_COAP_CODE_CONTENT,
                .format = ANJAY_COAP_FORMAT_PLAINTEXT,
                .observe_serial = true
            },
            &NULL_IDENTITY, 514.0, "514", 3));
    assert_observe_size(anjay, 1);

    ////// EMPTY SCHEDULER RUN //////
    _anjay_mock_clock_advance(avs_time_duration_from_scalar(5, AVS_TIME_S));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);

    ////// PLAIN NOTIFICATION //////
    _anjay_mock_clock_advance(avs_time_duration_from_scalar(5, AVS_TIME_S));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_STRING(0, "Hello"));
    const avs_coap_msg_t *notify_response =
            COAP_MSG(NON, CONTENT, ID(0x69ED), OBSERVE(0xF90000),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Hello"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response->content,
                                    notify_response->length);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);

    assert_observe(anjay, 14, 42, 69, 4, AVS_COAP_FORMAT_NONE,
                   &(const anjay_msg_details_t) {
                       .msg_type = AVS_COAP_MSG_NON_CONFIRMABLE,
                       .msg_code = AVS_COAP_CODE_CONTENT,
                       .format = ANJAY_COAP_FORMAT_PLAINTEXT,
                       .observe_serial = true
                   },
                   "Hello", 5);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)
                                     ->entries)
                    ->notify_task);
    AVS_UNIT_ASSERT_EQUAL(
            AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)
                                     ->entries)
                    ->last_sent->timestamp.since_real_epoch.seconds,
            1010);
    AVS_UNIT_ASSERT_EQUAL(
            AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)
                                     ->entries)
                    ->last_confirmable.since_real_epoch.seconds,
            1000);

    ////// CONFIRMABLE NOTIFICATION //////
    _anjay_mock_clock_advance(avs_time_duration_diff(
            avs_time_duration_from_scalar(1, AVS_TIME_DAY),
            avs_time_duration_from_scalar(10, AVS_TIME_S)));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_STRING(0, "Hi!"));
    const avs_coap_msg_t *con_notify_response =
            COAP_MSG(CON, CONTENT, ID(0x69EE), OBSERVE(0xB40000),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Hi!"));
    avs_unit_mocksock_expect_output(mocksocks[0], con_notify_response->content,
                                    con_notify_response->length);
    avs_unit_mocksock_input(mocksocks[0], con_notify_ack, con_notify_ack_size);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, observe_size_after_ack);
    if (observe_size_after_ack) {
        AVS_UNIT_ASSERT_EQUAL(
                AVS_RBTREE_FIRST(
                        AVS_RBTREE_FIRST(anjay->observe.connection_entries)
                                ->entries)
                        ->last_confirmable.since_real_epoch.seconds,
                AVS_RBTREE_FIRST(
                        AVS_RBTREE_FIRST(anjay->observe.connection_entries)
                                ->entries)
                        ->last_sent->timestamp.since_real_epoch.seconds);
    }

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, max_period) {
    notify_max_period_test("\x60\x00\x69\xEE", 4, 1); // CON
    notify_max_period_test("\x70\x00\x69\xEE", 4, 0); // Reset
}

AVS_UNIT_TEST(notify, min_period) {
    static const anjay_dm_internal_res_attrs_t ATTRS = {
        .standard = {
            .common = {
                .min_period = 10,
                .max_period = 365 * 24 * 60 * 60 // a year
            },
            .greater_than = ANJAY_ATTRIB_VALUE_NONE,
            .less_than = ANJAY_ATTRIB_VALUE_NONE,
            .step = ANJAY_ATTRIB_VALUE_NONE
        }
    };

    ////// INITIALIZATION //////
    DM_TEST_INIT_WITH_SSIDS(14);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_observe_put_entry(
            anjay,
            &(const anjay_observe_key_t) { { 14, ANJAY_CONNECTION_UDP },
                                           42,
                                           69,
                                           4,
                                           AVS_COAP_FORMAT_NONE },
            &(const anjay_msg_details_t) {
                .msg_type = AVS_COAP_MSG_ACKNOWLEDGEMENT,
                .msg_code = AVS_COAP_CODE_CONTENT,
                .format = ANJAY_COAP_FORMAT_PLAINTEXT,
                .observe_serial = true
            },
            &NULL_IDENTITY, 514.0, "514", 3));
    _anjay_mock_dm_expect_clean();
    assert_observe_size(anjay, 1);

    ////// PMIN NOT REACHED //////
    _anjay_mock_clock_advance(avs_time_duration_from_scalar(5, AVS_TIME_S));
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    ////// PMIN REACHED //////
    _anjay_mock_clock_advance(avs_time_duration_from_scalar(5, AVS_TIME_S));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_STRING(0, "Hi!"));
    const avs_coap_msg_t *notify_response =
            COAP_MSG(NON, CONTENT, ID(0x69ED), OBSERVE(0xF90000),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Hi!"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response->content,
                                    notify_response->length);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);

    ////// AFTER PMIN, NO CHANGE //////
    _anjay_mock_clock_advance(avs_time_duration_from_scalar(10, AVS_TIME_S));
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_STRING(0, "Hi!"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)
                                     ->entries)
                    ->notify_task);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, confirmable) {
    ////// INITIALIZATION //////
    DM_TEST_INIT_GENERIC((DM_TEST_DEFAULT_OBJECTS), (14),
                         (.confirmable_notifications = true));
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_observe_put_entry(
            anjay,
            &(const anjay_observe_key_t) { { 14, ANJAY_CONNECTION_UDP },
                                           42,
                                           69,
                                           4,
                                           AVS_COAP_FORMAT_NONE },
            &(const anjay_msg_details_t) {
                .msg_type = AVS_COAP_MSG_ACKNOWLEDGEMENT,
                .msg_code = AVS_COAP_CODE_CONTENT,
                .format = ANJAY_COAP_FORMAT_PLAINTEXT,
                .observe_serial = true
            },
            &(avs_coap_msg_identity_t) { 0, AVS_COAP_TOKEN_EMPTY }, 514.0,
            "514", 3));
    assert_observe_size(anjay, 1);

    ////// EMPTY SCHEDULER RUN //////
    _anjay_mock_clock_advance(avs_time_duration_from_scalar(5, AVS_TIME_S));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);

    ////// CONFIRMABLE NOTIFICATION //////
    _anjay_mock_clock_advance(avs_time_duration_from_scalar(5, AVS_TIME_S));
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 42));
    const avs_coap_msg_t *notify_response =
            COAP_MSG(CON, CONTENT, ID(0x69ED), OBSERVE(0xF90000),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("42"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response->content,
                                    notify_response->length);
    const avs_coap_msg_t *notify_ack =
            COAP_MSG(ACK, EMPTY, ID(0x69ED), NO_PAYLOAD);
    avs_unit_mocksock_input(mocksocks[0], notify_ack->content,
                            notify_ack->length);
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, extremes) {
    static const anjay_dm_internal_res_attrs_t ATTRS = {
        .standard = {
            .common = {
                .min_period = 0,
                .max_period = 365 * 24 * 60 * 60 // a year
            },
            .greater_than = 777.0,
            .less_than = 69.0,
            .step = ANJAY_ATTRIB_VALUE_NONE
        }
    };

    ////// INITIALIZATION //////
    DM_TEST_INIT_WITH_SSIDS(14);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_observe_put_entry(
            anjay,
            &(const anjay_observe_key_t) { { 14, ANJAY_CONNECTION_UDP },
                                           42,
                                           69,
                                           4,
                                           AVS_COAP_FORMAT_NONE },
            &(const anjay_msg_details_t) {
                .msg_type = AVS_COAP_MSG_ACKNOWLEDGEMENT,
                .msg_code = AVS_COAP_CODE_CONTENT,
                .format = ANJAY_COAP_FORMAT_PLAINTEXT,
                .observe_serial = true
            },
            &NULL_IDENTITY, 514.0, "514", 3));
    _anjay_mock_dm_expect_clean();
    assert_observe_size(anjay, 1);

    ////// LESS //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_FLOAT(0, 42.43));
    const avs_coap_msg_t *notify_response =
            COAP_MSG(NON, CONTENT, ID(0x69ED), OBSERVE(0xF40000),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("42.43"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response->content,
                                    notify_response->length);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)
                                     ->entries)
                    ->notify_task);

    ////// EVEN LESS //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_FLOAT(0, 14.7));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)
                                     ->entries)
                    ->notify_task);

    ////// IN BETWEEN //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 695));
    const avs_coap_msg_t *notify_response2 =
            COAP_MSG(NON, CONTENT, ID(0x69EE), OBSERVE(0xF40000),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("695"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response2->content,
                                    notify_response2->length);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)
                                     ->entries)
                    ->notify_task);

    ////// EQUAL - STILL NOT CROSSING //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 69));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)
                                     ->entries)
                    ->notify_task);

    ////// GREATER //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 1024));
    const avs_coap_msg_t *notify_response3 =
            COAP_MSG(NON, CONTENT, ID(0x69EF), OBSERVE(0xF40000),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("1024"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response3->content,
                                    notify_response3->length);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)
                                     ->entries)
                    ->notify_task);

    ////// STILL GREATER //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 999));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)
                                     ->entries)
                    ->notify_task);

    ////// LESS AGAIN //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_FLOAT(0, -69.75));
    const avs_coap_msg_t *notify_response4 =
            COAP_MSG(NON, CONTENT, ID(0x69F0), OBSERVE(0xF40000),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("-69.75"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response4->content,
                                    notify_response4->length);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)
                                     ->entries)
                    ->notify_task);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, greater_only) {
    static const anjay_dm_internal_res_attrs_t ATTRS = {
        .standard = {
            .common = {
                .min_period = 0,
                .max_period = 365 * 24 * 60 * 60 // a year
            },
            .greater_than = 69.0,
            .less_than = ANJAY_ATTRIB_VALUE_NONE,
            .step = ANJAY_ATTRIB_VALUE_NONE
        }
    };

    ////// INITIALIZATION (GREATER) //////
    DM_TEST_INIT_WITH_SSIDS(14);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_observe_put_entry(
            anjay,
            &(const anjay_observe_key_t) { { 14, ANJAY_CONNECTION_UDP },
                                           42,
                                           69,
                                           4,
                                           AVS_COAP_FORMAT_NONE },
            &(const anjay_msg_details_t) {
                .msg_type = AVS_COAP_MSG_ACKNOWLEDGEMENT,
                .msg_code = AVS_COAP_CODE_CONTENT,
                .format = ANJAY_COAP_FORMAT_PLAINTEXT,
                .observe_serial = true
            },
            &NULL_IDENTITY, 514.0, "514", 3));
    _anjay_mock_dm_expect_clean();
    assert_observe_size(anjay, 1);

    ////// STILL GREATER //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 9001));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)
                                     ->entries)
                    ->notify_task);

    ////// LESS //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 42));
    const avs_coap_msg_t *notify_response =
            COAP_MSG(NON, CONTENT, ID(0x69ED), OBSERVE(0xF40000),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("42"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response->content,
                                    notify_response->length);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)
                                     ->entries)
                    ->notify_task);

    ////// GREATER AGAIN //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 77));
    const avs_coap_msg_t *notify_response2 =
            COAP_MSG(NON, CONTENT, ID(0x69EE), OBSERVE(0xF40000),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("77"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response2->content,
                                    notify_response2->length);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)
                                     ->entries)
                    ->notify_task);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, less_only) {
    static const anjay_dm_internal_res_attrs_t ATTRS = {
        .standard = {
            .common = {
                .min_period = 0,
                .max_period = 365 * 24 * 60 * 60 // a year
            },
            .greater_than = ANJAY_ATTRIB_VALUE_NONE,
            .less_than = 777.0,
            .step = ANJAY_ATTRIB_VALUE_NONE
        }
    };

    ////// INITIALIZATION (GREATER) //////
    DM_TEST_INIT_WITH_SSIDS(14);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_observe_put_entry(
            anjay,
            &(const anjay_observe_key_t) { { 14, ANJAY_CONNECTION_UDP },
                                           42,
                                           69,
                                           4,
                                           AVS_COAP_FORMAT_NONE },
            &(const anjay_msg_details_t) {
                .msg_type = AVS_COAP_MSG_ACKNOWLEDGEMENT,
                .msg_code = AVS_COAP_CODE_CONTENT,
                .format = ANJAY_COAP_FORMAT_PLAINTEXT,
                .observe_serial = true
            },
            &NULL_IDENTITY, 1337.0, "1337", 3));
    _anjay_mock_dm_expect_clean();
    assert_observe_size(anjay, 1);

    ////// LESS //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 42));
    const avs_coap_msg_t *notify_response =
            COAP_MSG(NON, CONTENT, ID(0x69ED), OBSERVE(0xF40000),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("42"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response->content,
                                    notify_response->length);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)
                                     ->entries)
                    ->notify_task);

    ////// STILL LESS //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 514));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)
                                     ->entries)
                    ->notify_task);

    ////// GREATER //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 9001));
    const avs_coap_msg_t *notify_response2 =
            COAP_MSG(NON, CONTENT, ID(0x69EE), OBSERVE(0xF40000),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("9001"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response2->content,
                                    notify_response2->length);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)
                                     ->entries)
                    ->notify_task);

    ////// LESS AGAIN //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 69));
    const avs_coap_msg_t *notify_response3 =
            COAP_MSG(NON, CONTENT, ID(0x69EF), OBSERVE(0xF40000),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("69"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response3->content,
                                    notify_response3->length);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)
                                     ->entries)
                    ->notify_task);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, step) {
    static const anjay_dm_internal_res_attrs_t ATTRS = {
        .standard = {
            .common = {
                .min_period = 0,
                .max_period = 365 * 24 * 60 * 60 // a year
            },
            .greater_than = ANJAY_ATTRIB_VALUE_NONE,
            .less_than = ANJAY_ATTRIB_VALUE_NONE,
            .step = 10.0
        }
    };

    ////// INITIALIZATION //////
    DM_TEST_INIT_WITH_SSIDS(14);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_observe_put_entry(
            anjay,
            &(const anjay_observe_key_t) { { 14, ANJAY_CONNECTION_UDP },
                                           42,
                                           69,
                                           4,
                                           AVS_COAP_FORMAT_NONE },
            &(const anjay_msg_details_t) {
                .msg_type = AVS_COAP_MSG_ACKNOWLEDGEMENT,
                .msg_code = AVS_COAP_CODE_CONTENT,
                .format = ANJAY_COAP_FORMAT_PLAINTEXT,
                .observe_serial = true
            },
            &NULL_IDENTITY, 514.0, "514", 3));
    _anjay_mock_dm_expect_clean();
    assert_observe_size(anjay, 1);

    ////// TOO LITTLE INCREASE //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_FLOAT(0, 523.5));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)
                                     ->entries)
                    ->notify_task);

    ////// INCREASE BY EXACTLY stp //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 524));
    const avs_coap_msg_t *notify_response0 =
            COAP_MSG(NON, CONTENT, ID(0x69ED), OBSERVE(0xF40000),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("524"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response0->content,
                                    notify_response0->length);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)
                                     ->entries)
                    ->notify_task);

    ////// INCREASE BY OVER stp //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_FLOAT(0, 540.048));
    const avs_coap_msg_t *notify_response1 =
            COAP_MSG(NON, CONTENT, ID(0x69EE), OBSERVE(0xF40000),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("540.048"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response1->content,
                                    notify_response1->length);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)
                                     ->entries)
                    ->notify_task);

    ////// NON-NUMERIC VALUE //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_STRING(0, "trololo"));
    const avs_coap_msg_t *notify_response2 =
            COAP_MSG(NON, CONTENT, ID(0x69EF), OBSERVE(0xF40000),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("trololo"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response2->content,
                                    notify_response2->length);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)
                                     ->entries)
                    ->notify_task);

    ////// BACK TO NUMBERS //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 42));
    const avs_coap_msg_t *notify_response3 =
            COAP_MSG(NON, CONTENT, ID(0x69F0), OBSERVE(0xF40000),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("42"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response3->content,
                                    notify_response3->length);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)
                                     ->entries)
                    ->notify_task);

    ////// TOO LITTLE DECREASE //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_FLOAT(0, 32.001));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)
                                     ->entries)
                    ->notify_task);

    ////// DECREASE BY EXACTLY stp //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 31));
    const avs_coap_msg_t *notify_response4 =
            COAP_MSG(NON, CONTENT, ID(0x69F1), OBSERVE(0xF40000),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("31"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response4->content,
                                    notify_response4->length);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)
                                     ->entries)
                    ->notify_task);

    ////// DECREASE BY MORE THAN stp //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 20));
    const avs_coap_msg_t *notify_response5 =
            COAP_MSG(NON, CONTENT, ID(0x69F2), OBSERVE(0xF40000),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("20"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response5->content,
                                    notify_response5->length);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)
                                     ->entries)
                    ->notify_task);

    ////// INCREASE BY EXACTLY stp //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 30));
    const avs_coap_msg_t *notify_response6 =
            COAP_MSG(NON, CONTENT, ID(0x69F3), OBSERVE(0xF40000),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("30"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response6->content,
                                    notify_response6->length);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)
                                     ->entries)
                    ->notify_task);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, multiple_formats) {
    static const anjay_dm_internal_res_attrs_t ATTRS = {
        .standard = {
            .common = {
                .min_period = 1,
                .max_period = 10
            },
            .greater_than = ANJAY_ATTRIB_VALUE_NONE,
            .less_than = ANJAY_ATTRIB_VALUE_NONE,
            .step = ANJAY_ATTRIB_VALUE_NONE
        }
    };
    static avs_coap_msg_identity_t identity = {
        .msg_id = 0,
        .token = {
            .size = 1
        }
    };

    ////// INITIALIZATION //////
    DM_TEST_INIT_WITH_SSIDS(14);
    identity.token.bytes[0] = 'N';
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_observe_put_entry(
            anjay,
            &(const anjay_observe_key_t) { { 14, ANJAY_CONNECTION_UDP },
                                           42,
                                           69,
                                           4,
                                           AVS_COAP_FORMAT_NONE },
            &(const anjay_msg_details_t) {
                .msg_type = AVS_COAP_MSG_ACKNOWLEDGEMENT,
                .msg_code = AVS_COAP_CODE_CONTENT,
                .format = ANJAY_COAP_FORMAT_PLAINTEXT,
                .observe_serial = true
            },
            &identity, 514.0, "514", 3));
    identity.token.bytes[0] = 'P';
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_observe_put_entry(
            anjay,
            &(const anjay_observe_key_t) { { 14, ANJAY_CONNECTION_UDP },
                                           42,
                                           69,
                                           4,
                                           ANJAY_COAP_FORMAT_PLAINTEXT },
            &(const anjay_msg_details_t) {
                .msg_type = AVS_COAP_MSG_ACKNOWLEDGEMENT,
                .msg_code = AVS_COAP_CODE_CONTENT,
                .format = ANJAY_COAP_FORMAT_PLAINTEXT,
                .observe_serial = true
            },
            &identity, 514.0, "514", 3));
    identity.token.bytes[0] = 'T';
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_observe_put_entry(
            anjay,
            &(const anjay_observe_key_t) { { 14, ANJAY_CONNECTION_UDP },
                                           42,
                                           69,
                                           4,
                                           ANJAY_COAP_FORMAT_TLV },
            &(const anjay_msg_details_t) {
                .msg_type = AVS_COAP_MSG_ACKNOWLEDGEMENT,
                .msg_code = AVS_COAP_CODE_CONTENT,
                .format = ANJAY_COAP_FORMAT_TLV,
                .observe_serial = true
            },
            &identity, 514.0, "\xc2\x04\x02\x02", 4));
    assert_observe_size(anjay, 3);

    ////// NOTIFICATION //////
    _anjay_mock_clock_advance(avs_time_duration_from_scalar(10, AVS_TIME_S));
    // no format preference
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_STRING(0, "Hello"));
    const avs_coap_msg_t *n_notify_response =
            COAP_MSG(NON, CONTENT, ID_TOKEN(0x69ED, "N"), OBSERVE(0xF90000),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Hello"));
    avs_unit_mocksock_expect_output(mocksocks[0], n_notify_response->content,
                                    n_notify_response->length);
    // plaintext
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_STRING(0, "Hello"));
    const avs_coap_msg_t *p_notify_response =
            COAP_MSG(NON, CONTENT, ID_TOKEN(0x69EE, "P"), OBSERVE(0xF90000),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Hello"));
    avs_unit_mocksock_expect_output(mocksocks[0], p_notify_response->content,
                                    p_notify_response->length);
    // TLV
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_STRING(0, "Hello"));
    const avs_coap_msg_t *t_notify_response =
            COAP_MSG(NON, CONTENT, ID_TOKEN(0x69EF, "T"), OBSERVE(0xF90000),
                     CONTENT_FORMAT(TLV),
                     PAYLOAD("\xc5\x04"
                             "Hello"));
    avs_unit_mocksock_expect_output(mocksocks[0], t_notify_response->content,
                                    t_notify_response->length);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 3);

    ////// NOTIFICATION - FORMAT CHANGE //////
    _anjay_mock_clock_advance(avs_time_duration_from_scalar(10, AVS_TIME_S));
    // no format preference
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4,
                    ANJAY_MOCK_DM_BYTES(0, "\x12\x34\x56\x78"));
    const avs_coap_msg_t *n_bytes_response =
            COAP_MSG(NON, CONTENT, ID_TOKEN(0x69F0, "N"), OBSERVE(0xFE0000),
                     CONTENT_FORMAT(OPAQUE), PAYLOAD("\x12\x34\x56\x78"));
    avs_unit_mocksock_expect_output(mocksocks[0], n_bytes_response->content,
                                    n_bytes_response->length);
    // plaintext - error
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4,
                    ANJAY_MOCK_DM_BYTES(0, "\x12\x34\x56\x78"));
    const avs_coap_msg_t *p_bytes_response =
            COAP_MSG(NON, CONTENT, ID_TOKEN(0x69F1, "P"), OBSERVE(0xFE0000),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("EjRWeA=="));
    avs_unit_mocksock_expect_output(mocksocks[0], p_bytes_response->content,
                                    p_bytes_response->length);
    // TLV
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4,
                    ANJAY_MOCK_DM_BYTES(0, "\x12\x34\x56\x78"));
    const avs_coap_msg_t *t_bytes_response =
            COAP_MSG(NON, CONTENT, ID_TOKEN(0x69F2, "T"), OBSERVE(0xFE0000),
                     CONTENT_FORMAT(TLV),
                     PAYLOAD("\xc4\x04"
                             "\x12\x34\x56\x78"));
    avs_unit_mocksock_expect_output(mocksocks[0], t_bytes_response->content,
                                    t_bytes_response->length);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 3);
    DM_TEST_FINISH;
}

static void test_observe_entry(anjay_t *anjay,
                               anjay_ssid_t ssid,
                               anjay_connection_type_t conn_type,
                               anjay_oid_t oid,
                               anjay_iid_t iid,
                               int32_t rid) {
    AVS_RBTREE_ELEM(anjay_observe_connection_entry_t) conn =
            find_or_create_connection_state(
                    anjay, &(const anjay_connection_key_t) { ssid, conn_type });
    AVS_UNIT_ASSERT_NOT_NULL(conn);

    AVS_RBTREE_ELEM(anjay_observe_entry_t) new_entry =
            AVS_RBTREE_ELEM_NEW(anjay_observe_entry_t);
    AVS_UNIT_ASSERT_NOT_NULL(new_entry);

    memcpy((void *) (intptr_t) (const void *) &new_entry->key,
           (&(const anjay_observe_key_t) {
                   { ssid, conn_type }, oid, iid, rid, AVS_COAP_FORMAT_NONE }),
           sizeof(anjay_observe_key_t));
    AVS_RBTREE_ELEM(anjay_observe_entry_t) entry =
            AVS_RBTREE_INSERT(conn->entries, new_entry);
    if (entry != new_entry) {
        AVS_RBTREE_ELEM_DELETE_DETACHED(&new_entry);
    }
}

static anjay_t *create_test_env(void) {
    anjay_t *anjay = (anjay_t *) avs_calloc(1, sizeof(anjay_t));
    _anjay_observe_init(&anjay->observe, false, 0);
    test_observe_entry(anjay, 1, ANJAY_CONNECTION_UDP, 2, 3, 1);
    test_observe_entry(anjay, 1, ANJAY_CONNECTION_UDP, 2, 3, 2);
    test_observe_entry(anjay, 1, ANJAY_CONNECTION_UDP, 2, 9, 4);
    test_observe_entry(anjay, 1, ANJAY_CONNECTION_UDP, 4, 1, 1);
    test_observe_entry(anjay, 3, ANJAY_CONNECTION_UDP, 2, 3, -1);
    test_observe_entry(anjay, 3, ANJAY_CONNECTION_UDP, 2, 3, 3);
    test_observe_entry(anjay, 3, ANJAY_CONNECTION_UDP, 2, 7, 3);
    test_observe_entry(anjay, 3, ANJAY_CONNECTION_UDP, 6, 0, 1);
    test_observe_entry(anjay, 3, ANJAY_CONNECTION_UDP, 6, 0, 2);
    test_observe_entry(anjay, 8, ANJAY_CONNECTION_UDP, 4, ANJAY_IID_INVALID,
                       -1);
    test_observe_entry(anjay, 8, ANJAY_CONNECTION_UDP, 6, 0, 1);
    return anjay;
}

static void destroy_test_env(anjay_t *anjay) {
    _anjay_observe_cleanup(&anjay->observe, anjay->sched);
    avs_free(anjay);
}

static const anjay_dm_object_def_t *const *fake_object(anjay_t *anjay,
                                                       anjay_oid_t oid) {
    (void) anjay;
    (void) oid;
    static anjay_dm_object_def_t obj;
    static const anjay_dm_object_def_t *const obj_ptr = &obj;
    obj.oid = oid;
    return &obj_ptr;
}

typedef struct {
    anjay_observe_key_t key;
    int retval;
} mock_notify_entry_value_t;

static AVS_LIST(mock_notify_entry_value_t) MOCK_NOTIFY = NULL;

static int mock_notify_entry(anjay_t *anjay,
                             anjay_observe_entry_t *entity,
                             void *result_ptr) {
    (void) anjay;
    AVS_UNIT_ASSERT_NOT_NULL(MOCK_NOTIFY);
    mock_notify_entry_value_t *entry = AVS_LIST_DETACH(&MOCK_NOTIFY);
    AVS_UNIT_ASSERT_EQUAL(_anjay_observe_key_cmp(&entity->key, &entry->key), 0);
    _anjay_update_ret((int *) result_ptr, entry->retval);
    AVS_LIST_DELETE(&entry);
    return 0;
}

static void expect_notify_entry(anjay_ssid_t ssid,
                                anjay_oid_t oid,
                                anjay_iid_t iid,
                                int32_t rid,
                                uint16_t format,
                                int retval) {
    mock_notify_entry_value_t *entry =
            AVS_LIST_NEW_ELEMENT(mock_notify_entry_value_t);
    AVS_UNIT_ASSERT_NOT_NULL(entry);
    entry->key.connection.ssid = ssid;
    entry->key.connection.type = ANJAY_CONNECTION_UDP;
    entry->key.oid = oid;
    entry->key.iid = iid;
    entry->key.rid = rid;
    entry->key.format = format;
    entry->retval = retval;
    AVS_LIST_APPEND(&MOCK_NOTIFY, entry);
}

static inline void expect_notify_clear(void) {
    AVS_UNIT_ASSERT_NULL(MOCK_NOTIFY);
}

AVS_UNIT_TEST(notify, notify_changed) {
    anjay_t *anjay = create_test_env();

    AVS_UNIT_MOCK(_anjay_dm_find_object_by_oid) = fake_object;

    expect_notify_entry(8, 4, ANJAY_IID_INVALID, -1, AVS_COAP_FORMAT_NONE, 0);
    AVS_UNIT_ASSERT_SUCCESS(observe_notify_impl(
            anjay,
            &(const anjay_observe_key_t) { { 1, ANJAY_CONNECTION_UNSET },
                                           4,
                                           1,
                                           1,
                                           AVS_COAP_FORMAT_NONE },
            true, mock_notify_entry));
    expect_notify_clear();

    AVS_UNIT_ASSERT_SUCCESS(observe_notify_impl(
            anjay,
            &(const anjay_observe_key_t) { { 3, ANJAY_CONNECTION_UNSET },
                                           2,
                                           7,
                                           -1,
                                           AVS_COAP_FORMAT_NONE },
            true, mock_notify_entry));
    expect_notify_clear();

    expect_notify_entry(3, 6, 0, 1, AVS_COAP_FORMAT_NONE, 0);
    expect_notify_entry(8, 6, 0, 1, AVS_COAP_FORMAT_NONE, 0);
    AVS_UNIT_ASSERT_SUCCESS(observe_notify_impl(
            anjay,
            &(const anjay_observe_key_t) { { 1, ANJAY_CONNECTION_UNSET },
                                           6,
                                           0,
                                           1,
                                           AVS_COAP_FORMAT_NONE },
            true, mock_notify_entry));
    expect_notify_clear();

    expect_notify_entry(3, 6, 0, 2, AVS_COAP_FORMAT_NONE, 0);
    AVS_UNIT_ASSERT_SUCCESS(observe_notify_impl(
            anjay,
            &(const anjay_observe_key_t) { { 1, ANJAY_CONNECTION_UNSET },
                                           6,
                                           0,
                                           2,
                                           AVS_COAP_FORMAT_NONE },
            true, mock_notify_entry));
    expect_notify_clear();

    expect_notify_entry(1, 2, 3, 1, AVS_COAP_FORMAT_NONE, 0);
    expect_notify_entry(1, 2, 3, 2, AVS_COAP_FORMAT_NONE, -42);
    expect_notify_entry(1, 2, 9, 4, AVS_COAP_FORMAT_NONE, 0);
    expect_notify_entry(3, 2, 3, -1, AVS_COAP_FORMAT_NONE, -514);
    expect_notify_entry(3, 2, 3, 3, AVS_COAP_FORMAT_NONE, 0);
    expect_notify_entry(3, 2, 7, 3, AVS_COAP_FORMAT_NONE, 0);
    AVS_UNIT_ASSERT_EQUAL(
            observe_notify_impl(
                    anjay,
                    &(const anjay_observe_key_t) { { ANJAY_IID_INVALID,
                                                     ANJAY_CONNECTION_UNSET },
                                                   2,
                                                   ANJAY_IID_INVALID,
                                                   -1,
                                                   AVS_COAP_FORMAT_NONE },
                    true, mock_notify_entry),
            -42);
    expect_notify_clear();

    destroy_test_env(anjay);
}

AVS_UNIT_TEST(notify, storing_when_inactive) {
    SUCCESS_TEST(14, 34);
    anjay_server_connection_t *connection =
            _anjay_get_server_connection((const anjay_connection_ref_t) {
                .server = anjay->servers->servers,
                .conn_type = ANJAY_CONNECTION_UDP
            });
    AVS_UNIT_ASSERT_NOT_NULL(connection);

    // deactivate the first server
    avs_net_abstract_socket_t *socket14 = connection->conn_socket_;
    connection->conn_socket_ = NULL;
    _anjay_observe_gc(anjay);
    assert_observe_size(anjay, 2);

    // first notification
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    _anjay_mock_clock_advance(avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Rin"));

    expect_read_notif_storing(anjay, &FAKE_SERVER, 34, true);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Len"));
    const avs_coap_msg_t *notify_response =
            COAP_MSG(NON, CONTENT, ID(0x69ED), OBSERVE(0xF48000),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Len"));
    avs_unit_mocksock_expect_output(mocksocks[1], notify_response->content,

                                    notify_response->length);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    // second notification
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    _anjay_mock_clock_advance(avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Miku"));

    expect_read_notif_storing(anjay, &FAKE_SERVER, 34, true);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Luka"));
    const avs_coap_msg_t *notify_response2 =
            COAP_MSG(NON, CONTENT, ID(0x69EE), OBSERVE(0xF50000),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Luka"));
    avs_unit_mocksock_expect_output(mocksocks[1], notify_response2->content,
                                    notify_response2->length);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    // reactivate the server
    connection->conn_socket_ = socket14;
    _anjay_observe_gc(anjay);
    assert_observe_size(anjay, 2);
    anjay->current_connection.server = anjay->servers->servers;
    anjay->current_connection.conn_type = ANJAY_CONNECTION_UDP;
    _anjay_observe_sched_flush_current_connection(anjay);
    memset(&anjay->current_connection, 0, sizeof(anjay->current_connection));

    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    const avs_coap_msg_t *notify_response3 =
            COAP_MSG(NON, CONTENT, ID(0x69EF), OBSERVE(0xF50000),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Rin"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response3->content,
                                    notify_response3->length);
    const avs_coap_msg_t *notify_response4 =
            COAP_MSG(NON, CONTENT, ID(0x69F0), OBSERVE(0xF50000),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Miku"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response4->content,
                                    notify_response4->length);
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, no_storing_when_disabled) {
    SUCCESS_TEST(14, 34);
    anjay_server_connection_t *connection =
            _anjay_get_server_connection((const anjay_connection_ref_t) {
                .server = anjay->servers->servers,
                .conn_type = ANJAY_CONNECTION_UDP
            });
    AVS_UNIT_ASSERT_NOT_NULL(connection);

    // deactivate the first server
    avs_net_abstract_socket_t *socket14 = connection->conn_socket_;
    connection->conn_socket_ = NULL;
    _anjay_observe_gc(anjay);
    assert_observe_size(anjay, 2);

    // first notification
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    _anjay_mock_clock_advance(avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, false);
    expect_read_notif_storing(anjay, &FAKE_SERVER, 34, false);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Ia"));
    const avs_coap_msg_t *notify_response =
            COAP_MSG(NON, CONTENT, ID(0x69ED), OBSERVE(0xF48000),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Ia"));
    avs_unit_mocksock_expect_output(mocksocks[1], notify_response->content,
                                    notify_response->length);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    // second notification
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    _anjay_mock_clock_advance(avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, false);
    expect_read_notif_storing(anjay, &FAKE_SERVER, 34, false);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Gumi"));
    const avs_coap_msg_t *notify_response2 =
            COAP_MSG(NON, CONTENT, ID(0x69EE), OBSERVE(0xF50000),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Gumi"));
    avs_unit_mocksock_expect_output(mocksocks[1], notify_response2->content,
                                    notify_response2->length);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    // reactivate the server
    connection->conn_socket_ = socket14;
    _anjay_observe_gc(anjay);
    assert_observe_size(anjay, 2);
    anjay->current_connection.server = anjay->servers->servers;
    anjay->current_connection.conn_type = ANJAY_CONNECTION_UDP;
    _anjay_observe_sched_flush_current_connection(anjay);
    memset(&anjay->current_connection, 0, sizeof(anjay->current_connection));

    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, storing_on_send_error) {
    SUCCESS_TEST(14);

    // first notification
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    _anjay_mock_clock_advance(avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Meiko"));
    avs_unit_mocksock_output_fail(mocksocks[0], -1);
    // this is a hack: all other errno values trigger reconnection
    avs_unit_mocksock_expect_errno(mocksocks[0], EMSGSIZE);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    // second notification
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    _anjay_mock_clock_advance(avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Kaito"));
    avs_unit_mocksock_output_fail(mocksocks[0], -1);
    avs_unit_mocksock_expect_errno(mocksocks[0], EMSGSIZE);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    // anjay_serve() with reschedule notification sending
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID(0XFB3E), PATH("42", "69", "3"));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 3, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 3, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Mayu"));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT, ID(0xFB3E),
                            CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Mayu"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));

    // now the notifications shall arrive
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    const avs_coap_msg_t *notify_response =
            COAP_MSG(NON, CONTENT, ID(0x69EF), OBSERVE(0xF50000),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Meiko"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response->content,
                                    notify_response->length);
    const avs_coap_msg_t *notify_response2 =
            COAP_MSG(NON, CONTENT, ID(0x69F0), OBSERVE(0xF50000),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Kaito"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response2->content,
                                    notify_response2->length);
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, no_storing_on_send_error) {
    SUCCESS_TEST(14);

    // first notification
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    _anjay_mock_clock_advance(avs_time_duration_from_scalar(1, AVS_TIME_S));

    // let's leave storing on for a moment
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Meiko"));
    avs_unit_mocksock_output_fail(mocksocks[0], -1);
    avs_unit_mocksock_expect_errno(mocksocks[0], EMSGSIZE);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    // second notification
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    _anjay_mock_clock_advance(avs_time_duration_from_scalar(1, AVS_TIME_S));

    // and now we have it disabled
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, false);
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Kaito"));
    avs_unit_mocksock_output_fail(mocksocks[0], -1);
    avs_unit_mocksock_expect_errno(mocksocks[0], EMSGSIZE);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    // anjay_serve() with reschedule notification sending...
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID(0XFB3E), PATH("42", "69", "3"));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 3, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 3, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Mayu"));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT, ID(0xFB3E),
                            CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Mayu"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));

    // ...but nothing should come
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, storing_of_errors) {
    SUCCESS_TEST(14);

    // first notification
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    _anjay_mock_clock_advance(avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    // error during attribute reading
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, -1);
    _anjay_mock_dm_expect_object_read_default_attrs(
            anjay, &OBJ, 14, -1, &ANJAY_DM_INTERNAL_ATTRS_EMPTY);
    avs_unit_mocksock_output_fail(mocksocks[0], -1);
    avs_unit_mocksock_expect_errno(mocksocks[0], EMSGSIZE);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    // second notification - should not actually do anything
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    _anjay_mock_clock_advance(avs_time_duration_from_scalar(1, AVS_TIME_S));

    // sending is now scheduled, should receive the previous error
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    const avs_coap_msg_t *con_notify_response =
            COAP_MSG(CON, INTERNAL_SERVER_ERROR, ID(0x69EE), NO_PAYLOAD);
    avs_unit_mocksock_expect_output(mocksocks[0], con_notify_response->content,
                                    con_notify_response->length);
    const avs_coap_msg_t *con_ack =
            COAP_MSG(ACK, EMPTY, ID(0x69EE), NO_PAYLOAD);
    avs_unit_mocksock_input(mocksocks[0], con_ack->content, con_ack->length);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    // now the notification shall be gone
    assert_observe_size(anjay, 0);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, no_storing_of_errors) {
    SUCCESS_TEST(14);

    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    _anjay_mock_clock_advance(avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, false);
    // error during attribute reading
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, -1);
    _anjay_mock_dm_expect_object_read_default_attrs(
            anjay, &OBJ, 14, -1, &ANJAY_DM_INTERNAL_ATTRS_EMPTY);
    avs_unit_mocksock_output_fail(mocksocks[0], -1);
    avs_unit_mocksock_expect_errno(mocksocks[0], EMSGSIZE);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    // now the notification shall be gone
    assert_observe_size(anjay, 0);

    DM_TEST_FINISH;
}
