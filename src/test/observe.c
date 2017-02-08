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
#include <stdarg.h>

#include <avsystem/commons/unit/test.h>

#include <anjay_test/dm.h>
#include <anjay_test/mock_clock.h>

#include "../anjay.h"

// HACK to enable access to servers
#define ANJAY_SERVERS_INTERNALS
#include "../servers/connection_info.h"
#undef ANJAY_SERVERS_INTERNALS

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
    /* Yes, a pointer comparision */
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
                           const void *data, size_t length) {
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
            AVS_RBTREE_FIND(conn->entries, entry_query(&key_query));
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
    _anjay_mock_dm_expect_resource_supported(anjay, obj,
                                             ANJAY_DM_RID_SERVER_SSID, 1);
    _anjay_mock_dm_expect_resource_present(anjay, obj, ssid,
                                           ANJAY_DM_RID_SERVER_SSID, 1);
    _anjay_mock_dm_expect_resource_read(anjay, obj, ssid,
                                        ANJAY_DM_RID_SERVER_SSID, 0,
                                        ANJAY_MOCK_DM_INT(0, ssid));
    _anjay_mock_dm_expect_resource_supported(anjay, obj, rid, 1);
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

#define ASSERT_SUCCESS_TEST_RESULT(Ssid) \
    assert_observe(anjay, Ssid, 42, 69, 4, ANJAY_COAP_FORMAT_NONE, \
                   &(const anjay_msg_details_t) { \
                       .msg_type = ANJAY_COAP_MSG_ACKNOWLEDGEMENT, \
                       .msg_code = ANJAY_COAP_CODE_CONTENT, \
                       .format = ANJAY_COAP_FORMAT_PLAINTEXT, \
                       .observe_serial = true \
                   }, "514", 3)

#define SUCCESS_TEST(...) \
DM_TEST_INIT_WITH_SSIDS(__VA_ARGS__); \
do { \
    for (size_t i = 0; i < ANJAY_ARRAY_SIZE(ssids); ++i) { \
        static const char REQUEST[] = \
                "\x40\x01\xFA\x3E" /* CoAP header */ \
                "\x60" /* Observe */ \
                "\x52" "42" /* OID */ \
                "\x02" "69" /* IID */ \
                "\x01" "4"; /* RID */ \
        avs_unit_mocksock_input(mocksocks[i], REQUEST, sizeof(REQUEST) - 1); \
        _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1); \
        _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1); \
        _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1); \
        _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0, \
                                            ANJAY_MOCK_DM_INT(0, 514)); \
        DM_TEST_EXPECT_READ_NULL_ATTRS(ssids[i], 69, 4); \
        DM_TEST_EXPECT_RESPONSE(mocksocks[i], \
                "\x60\x45\xFA\x3E" /* CoAP header */ \
                "\x63\xF4\x00\x00" /* Observe option */ \
                "\x60" /* Content-Format */ \
                "\xFF" "514"); \
        AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[i])); \
        assert_observe_size(anjay, i + 1); \
        ASSERT_SUCCESS_TEST_RESULT(ssids[i]); \
    } \
    for (size_t i = 0; i < ANJAY_ARRAY_SIZE(ssids); ++i) { \
        DM_TEST_EXPECT_READ_NULL_ATTRS(ssids[i], 69, 4); \
    } \
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay)); \
} while (0)

AVS_UNIT_TEST(observe, simple) {
    SUCCESS_TEST(14);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(observe, read_failed) {
    DM_TEST_INIT_WITH_SSIDS(4);
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\x60" // Observe
            "\x52" "42" // OID
            "\x01" "5" // IID
            "\x01" "7"; // RID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 5, 0);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\x84\xFA\x3E");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    assert_observe_size(anjay, 0);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(observe, read_attrs_failed) {
    DM_TEST_INIT_WITH_SSIDS(4);
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\x60" // Observe
            "\x52" "42" // OID
            "\x02" "69" // IID
            "\x01" "4"; // RID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_INT(0, 514));
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read_attrs(anjay, &OBJ, 69, 4, 4, -1, NULL);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], "\x60\xA0\xFA\x3E");
    AVS_UNIT_ASSERT_FAILED(anjay_serve(anjay, mocksocks[0]));
    assert_observe_size(anjay, 0);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(observe, overwrite) {
    SUCCESS_TEST(14);
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\x60" // Observe
            "\x52" "42" // OID
            "\x02" "69" // IID
            "\x01" "4"; // RID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
            ANJAY_MOCK_DM_ARRAY(0,
                ANJAY_MOCK_DM_ARRAY_ENTRY(4, ANJAY_MOCK_DM_INT(0, 777)),
                ANJAY_MOCK_DM_ARRAY_ENTRY(7, ANJAY_MOCK_DM_STRING(0, "Hi!"))));
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
#define TLV_RESPONSE \
        "\x88\x04\x09" \
        "\x42\x04\x03\x09" \
        "\x43\x07" "Hi!"
    DM_TEST_EXPECT_RESPONSE(mocksocks[0],
            "\x60\x45\xFA\x3E" // CoAP header
            "\x63\xF4\x00\x00" // Observe option
            "\x62\x2d\x16" // Content-Format
            "\xFF" TLV_RESPONSE);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    assert_observe_size(anjay, 1);
    assert_observe(anjay, 14, 42, 69, 4, ANJAY_COAP_FORMAT_NONE,
                   &(const anjay_msg_details_t) {
                       .msg_type = ANJAY_COAP_MSG_ACKNOWLEDGEMENT,
                       .msg_code = ANJAY_COAP_CODE_CONTENT,
                       .format = ANJAY_COAP_FORMAT_TLV,
                       .observe_serial = true
                   }, TLV_RESPONSE, sizeof(TLV_RESPONSE) - 1);
#undef TLV_RESPONSE
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(observe, instance) {
    SUCCESS_TEST(14);
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\x60" // Observe
            "\x52" "42" // OID
            "\x02" "69"; // IID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 0, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 0, 0);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 1, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 1, 0);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 2, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 2, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 2, 0,
                                        ANJAY_MOCK_DM_STRING(0, "wow"));
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 3, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 3, 0);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_STRING(0, "such value"));
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 5, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 5, 0);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 6, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 6, 0);
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, -1);
#define TLV_RESPONSE \
        "\xc3\x02" "wow" \
        "\xc8\x04\x0a" "such value"
    DM_TEST_EXPECT_RESPONSE(mocksocks[0],
            "\x60\x45\xfa\x3e" // CoAP header
            "\x63\xF4\x00\x00" // Observe option
            "\x62\x2d\x16" // Content-Format
            "\xff" TLV_RESPONSE);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    assert_observe_size(anjay, 2);
    ASSERT_SUCCESS_TEST_RESULT(14);
    assert_observe(anjay, 14, 42, 69, -1, ANJAY_COAP_FORMAT_NONE,
                   &(const anjay_msg_details_t) {
                       .msg_type = ANJAY_COAP_MSG_ACKNOWLEDGEMENT,
                       .msg_code = ANJAY_COAP_CODE_CONTENT,
                       .format = ANJAY_COAP_FORMAT_TLV,
                       .observe_serial = true
                   }, TLV_RESPONSE, sizeof(TLV_RESPONSE) - 1);
#undef TLV_RESPONSE
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, -1);
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(observe, cancel_reset) {
    SUCCESS_TEST(14);
    static const char REQUEST[] = "\x70\x00\x3e\xfa";
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    assert_observe_size(anjay, 1);
    static const char REQUEST2[] = "\x70\x00\xfa\x3e";
    avs_unit_mocksock_input(mocksocks[0], REQUEST2, sizeof(REQUEST2) - 1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    assert_observe_size(anjay, 0);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(observe, cancel_deregister) {
    SUCCESS_TEST(14);
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\x61\x01" // Observe - Deregister
            "\x52" "42" // OID
            "\x02" "69" // IID
            "\x01" "6"; // RID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 6, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 6, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 6, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Hello"));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0],
            "\x60\x45\xFA\x3E" // CoAP header
            "\xC0" // Content-Format
            "\xFF" "Hello");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    assert_observe_size(anjay, 1);
    static const char REQUEST2[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\x61\x01" // Observe - Deregister
            "\x52" "42" // OID
            "\x02" "69" // IID
            "\x01" "4"; // RID
    avs_unit_mocksock_input(mocksocks[0], REQUEST2, sizeof(REQUEST2) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Good-bye"));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0],
            "\x60\x45\xFA\x3E" // CoAP header
            "\xC0" // Content-Format
            "\xFF" "Good-bye");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    assert_observe_size(anjay, 0);
    DM_TEST_FINISH;
}

static inline void
remove_server(AVS_LIST(anjay_active_server_info_t) *server_ptr) {
    _anjay_connection_internal_set_move_socket(&(*server_ptr)->udp_connection,
                                               NULL);
    AVS_LIST_DELETE(server_ptr);
}

AVS_UNIT_TEST(observe, gc) {
    SUCCESS_TEST(14, 69, 514, 666, 777);

    remove_server(&anjay->servers.active);

    _anjay_observe_gc(anjay);
    assert_observe_size(anjay, 4);
    ASSERT_SUCCESS_TEST_RESULT(69);
    ASSERT_SUCCESS_TEST_RESULT(514);
    ASSERT_SUCCESS_TEST_RESULT(666);
    ASSERT_SUCCESS_TEST_RESULT(777);

    remove_server(AVS_LIST_NTH_PTR(&anjay->servers.active, 3));

    _anjay_observe_gc(anjay);
    assert_observe_size(anjay, 3);
    ASSERT_SUCCESS_TEST_RESULT(69);
    ASSERT_SUCCESS_TEST_RESULT(514);
    ASSERT_SUCCESS_TEST_RESULT(666);

    remove_server(AVS_LIST_NTH_PTR(&anjay->servers.active, 1));

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
                                  const anjay_dm_attributes_t *attrs) {
    _anjay_mock_dm_expect_instance_present(anjay, obj_ptr, iid, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, obj_ptr, rid, 1);
    _anjay_mock_dm_expect_resource_present(anjay, obj_ptr, iid, rid, 1);
    _anjay_mock_dm_expect_resource_read_attrs(anjay, obj_ptr, iid, rid, ssid, 0,
                                              attrs);
    if (!_anjay_dm_attributes_full(attrs)) {
        _anjay_mock_dm_expect_instance_read_default_attrs(
                anjay, obj_ptr, iid, ssid, 0, &ANJAY_DM_ATTRIBS_EMPTY);
        _anjay_mock_dm_expect_object_read_default_attrs(
                anjay, obj_ptr, ssid, 0, &ANJAY_DM_ATTRIBS_EMPTY);
    }
}

static void expect_read_res(anjay_t *anjay,
                            const anjay_dm_object_def_t *const *obj_ptr,
                            anjay_iid_t iid,
                            anjay_rid_t rid,
                            const anjay_mock_dm_data_t *data) {
    _anjay_mock_dm_expect_instance_present(anjay, obj_ptr, iid, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, obj_ptr, rid, 1);
    _anjay_mock_dm_expect_resource_present(anjay, obj_ptr, iid, rid, 1);
    _anjay_mock_dm_expect_resource_read(anjay, obj_ptr, iid, rid, 0, data);
}

static void notify_max_period_test(const char *con_notify_ack,
                                   size_t con_notify_ack_size,
                                   size_t observe_size_after_ack) {
    static const anjay_dm_attributes_t ATTRS = {
        .min_period = 1,
        .max_period = 10,
        .greater_than = ANJAY_ATTRIB_VALUE_NONE,
        .less_than = ANJAY_ATTRIB_VALUE_NONE,
        .step = ANJAY_ATTRIB_VALUE_NONE
    };
    static const anjay_coap_msg_identity_t identity = {
        .msg_id = 0,
        .token_size = 0
    };

    ////// INITIALIZATION //////
    DM_TEST_INIT_WITH_SSIDS(14);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_observe_put_entry(
            anjay, &(const anjay_observe_key_t) {
                { 14, ANJAY_CONNECTION_UDP }, 42, 69, 4, ANJAY_COAP_FORMAT_NONE
            }, &(const anjay_msg_details_t) {
                .msg_type = ANJAY_COAP_MSG_ACKNOWLEDGEMENT,
                .msg_code = ANJAY_COAP_CODE_CONTENT,
                .format = ANJAY_COAP_FORMAT_PLAINTEXT,
                .observe_serial = true
            }, &identity, 514.0, "514", 3));
    assert_observe_size(anjay, 1);

    ////// EMPTY SCHEDULER RUN //////
    _anjay_mock_clock_advance(&(const struct timespec) { 5, 0 });
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);

    ////// PLAIN NOTIFICATION //////
    _anjay_mock_clock_advance(&(const struct timespec) { 5, 0 });
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_STRING(0, "Hello"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    static const char NOTIFY_RESPONSE[] =
            "\x50\x45\x69\xED" // CoAP header
            "\x63\xF9\x00\x00" // Observe option
            "\x60" // Content-Format
            "\xFF" "Hello";
    avs_unit_mocksock_expect_output(mocksocks[0], NOTIFY_RESPONSE,
                                    sizeof(NOTIFY_RESPONSE) - 1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);

    assert_observe(anjay, 14, 42, 69, 4, ANJAY_COAP_FORMAT_NONE,
                   &(const anjay_msg_details_t) {
                       .msg_type = ANJAY_COAP_MSG_NON_CONFIRMABLE,
                       .msg_code = ANJAY_COAP_CODE_CONTENT,
                       .format = ANJAY_COAP_FORMAT_PLAINTEXT,
                       .observe_serial = true
                   }, "Hello", 5);
    AVS_UNIT_ASSERT_NOT_NULL(AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)->entries)->notify_task);
    AVS_UNIT_ASSERT_EQUAL(AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)->entries)->last_sent->timestamp.tv_sec,
                          1010);
    AVS_UNIT_ASSERT_EQUAL(AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)->entries)->last_confirmable.tv_sec,
                          1000);

    ////// CONFIRMABLE NOTIFICATION //////
    _anjay_mock_clock_advance(&(const struct timespec) { 24*60*60 - 10, 0 });
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_STRING(0, "Hi!"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    static const char CON_NOTIFY_RESPONSE[] =
            "\x40\x45\x69\xEE" // CoAP header
            "\x63\xB4\x00\x00" // Observe option
            "\x60" // Content-Format
            "\xFF" "Hi!";
    avs_unit_mocksock_expect_output(mocksocks[0], CON_NOTIFY_RESPONSE,
                                    sizeof(CON_NOTIFY_RESPONSE) - 1);
    avs_unit_mocksock_input(mocksocks[0], con_notify_ack, con_notify_ack_size);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, observe_size_after_ack);
    if (observe_size_after_ack) {
        AVS_UNIT_ASSERT_EQUAL(AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)->entries)->last_confirmable.tv_sec,
                              AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)->entries)->last_sent->timestamp.tv_sec);

    }

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, max_period) {
    notify_max_period_test("\x60\x00\x69\xEE", 4, 1); // CON
    notify_max_period_test("\x70\x00\x69\xEE", 4, 0); // Reset
}

AVS_UNIT_TEST(notify, min_period) {
    static const anjay_dm_attributes_t ATTRS = {
        .min_period = 10,
        .max_period = 365 * 24 * 60 * 60, // a year
        .greater_than = ANJAY_ATTRIB_VALUE_NONE,
        .less_than = ANJAY_ATTRIB_VALUE_NONE,
        .step = ANJAY_ATTRIB_VALUE_NONE
    };
    static const anjay_coap_msg_identity_t identity = {
        .msg_id = 0,
        .token_size = 0
    };

    ////// INITIALIZATION //////
    DM_TEST_INIT_WITH_SSIDS(14);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_observe_put_entry(
            anjay, &(const anjay_observe_key_t) {
                { 14, ANJAY_CONNECTION_UDP }, 42, 69, 4, ANJAY_COAP_FORMAT_NONE
            }, &(const anjay_msg_details_t) {
                .msg_type = ANJAY_COAP_MSG_ACKNOWLEDGEMENT,
                .msg_code = ANJAY_COAP_CODE_CONTENT,
                .format = ANJAY_COAP_FORMAT_PLAINTEXT,
                .observe_serial = true
            }, &identity, 514.0, "514", 3));
    _anjay_mock_dm_expect_clean();
    assert_observe_size(anjay, 1);

    ////// PMIN NOT REACHED //////
    _anjay_mock_clock_advance(&(const struct timespec) { 5, 0 });
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    ////// PMIN REACHED //////
    _anjay_mock_clock_advance(&(const struct timespec) { 5, 0 });
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_STRING(0, "Hi!"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    static const char NOTIFY_RESPONSE[] =
            "\x50\x45\x69\xED" // CoAP header
            "\x63\xF9\x00\x00" // Observe option
            "\x60" // Content-Format
            "\xFF" "Hi!";
    avs_unit_mocksock_expect_output(mocksocks[0], NOTIFY_RESPONSE,
                                    sizeof(NOTIFY_RESPONSE) - 1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);

    ////// AFTER PMIN, NO CHANGE //////
    _anjay_mock_clock_advance(&(const struct timespec) { 10, 0 });
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_STRING(0, "Hi!"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)->entries)->notify_task);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, range) {
    static const anjay_dm_attributes_t ATTRS = {
        .min_period = 0,
        .max_period = 365 * 24 * 60 * 60, // a year
        .greater_than = 69.0,
        .less_than = 777.0,
        .step = ANJAY_ATTRIB_VALUE_NONE
    };
    static const anjay_coap_msg_identity_t identity = {
        .msg_id = 0,
        .token_size = 0
    };

    ////// INITIALIZATION //////
    DM_TEST_INIT_WITH_SSIDS(14);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_observe_put_entry(
            anjay, &(const anjay_observe_key_t) {
                { 14, ANJAY_CONNECTION_UDP }, 42, 69, 4, ANJAY_COAP_FORMAT_NONE
            }, &(const anjay_msg_details_t) {
                .msg_type = ANJAY_COAP_MSG_ACKNOWLEDGEMENT,
                .msg_code = ANJAY_COAP_CODE_CONTENT,
                .format = ANJAY_COAP_FORMAT_PLAINTEXT,
                .observe_serial = true
            }, &identity, 514.0, "514", 3));
    _anjay_mock_dm_expect_clean();
    assert_observe_size(anjay, 1);

    ////// LESS //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_FLOAT(0, 42.42));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)->entries)->notify_task);

    ////// NON-NUMERIC VALUE //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_STRING(0, "Surprise!"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    static const char NOTIFY_RESPONSE[] =
            "\x50\x45\x69\xED" // CoAP header
            "\x63\xF4\x00\x00" // Observe option
            "\x60" // Content-Format
            "\xFF" "Surprise!";
    avs_unit_mocksock_expect_output(mocksocks[0], NOTIFY_RESPONSE,
                                    sizeof(NOTIFY_RESPONSE) - 1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)->entries)->notify_task);

    ////// GREATER //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 918));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)->entries)->notify_task);

    ////// IN RANGE //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 667));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    static const char NOTIFY_RESPONSE2[] =
            "\x50\x45\x69\xEE" // CoAP header
            "\x63\xF4\x00\x00" // Observe option
            "\x60" // Content-Format
            "\xFF" "667";
    avs_unit_mocksock_expect_output(mocksocks[0], NOTIFY_RESPONSE2,
                                    sizeof(NOTIFY_RESPONSE2) - 1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)->entries)->notify_task);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, extremes) {
    static const anjay_dm_attributes_t ATTRS = {
        .min_period = 0,
        .max_period = 365 * 24 * 60 * 60, // a year
        .greater_than = 777.0,
        .less_than = 69.0,
        .step = ANJAY_ATTRIB_VALUE_NONE
    };
    static const anjay_coap_msg_identity_t identity = {
        .msg_id = 0,
        .token_size = 0
    };

    ////// INITIALIZATION //////
    DM_TEST_INIT_WITH_SSIDS(14);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_observe_put_entry(
            anjay, &(const anjay_observe_key_t) {
                { 14, ANJAY_CONNECTION_UDP }, 42, 69, 4, ANJAY_COAP_FORMAT_NONE
            }, &(const anjay_msg_details_t) {
                .msg_type = ANJAY_COAP_MSG_ACKNOWLEDGEMENT,
                .msg_code = ANJAY_COAP_CODE_CONTENT,
                .format = ANJAY_COAP_FORMAT_PLAINTEXT,
                .observe_serial = true
            }, &identity, 514.0, "514", 3));
    _anjay_mock_dm_expect_clean();
    assert_observe_size(anjay, 1);

    ////// LESS //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_FLOAT(0, 42.43));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    static const char NOTIFY_RESPONSE[] =
            "\x50\x45\x69\xED" // CoAP header
            "\x63\xF4\x00\x00" // Observe option
            "\x60" // Content-Format
            "\xFF" "42.43";
    avs_unit_mocksock_expect_output(mocksocks[0], NOTIFY_RESPONSE,
                                    sizeof(NOTIFY_RESPONSE) - 1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)->entries)->notify_task);

    ////// IN RANGE //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 695));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)->entries)->notify_task);

    ////// GREATER //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 1024));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    static const char NOTIFY_RESPONSE2[] =
            "\x50\x45\x69\xEE" // CoAP header
            "\x63\xF4\x00\x00" // Observe option
            "\x60" // Content-Format
            "\xFF" "1024";
    avs_unit_mocksock_expect_output(mocksocks[0], NOTIFY_RESPONSE2,
                                    sizeof(NOTIFY_RESPONSE2) - 1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)->entries)->notify_task);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, greater_only) {
    static const anjay_dm_attributes_t ATTRS = {
        .min_period = 0,
        .max_period = 365 * 24 * 60 * 60, // a year
        .greater_than = 69.0,
        .less_than = ANJAY_ATTRIB_VALUE_NONE,
        .step = ANJAY_ATTRIB_VALUE_NONE
    };
    static const anjay_coap_msg_identity_t identity = {
        .msg_id = 0,
        .token_size = 0
    };

    ////// INITIALIZATION //////
    DM_TEST_INIT_WITH_SSIDS(14);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_observe_put_entry(
            anjay, &(const anjay_observe_key_t) {
                { 14, ANJAY_CONNECTION_UDP }, 42, 69, 4, ANJAY_COAP_FORMAT_NONE
            }, &(const anjay_msg_details_t) {
                .msg_type = ANJAY_COAP_MSG_ACKNOWLEDGEMENT,
                .msg_code = ANJAY_COAP_CODE_CONTENT,
                .format = ANJAY_COAP_FORMAT_PLAINTEXT,
                .observe_serial = true
            }, &identity, 514.0, "514", 3));
    _anjay_mock_dm_expect_clean();
    assert_observe_size(anjay, 1);

    ////// GREATER //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 9001));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    static const char NOTIFY_RESPONSE[] =
            "\x50\x45\x69\xED" // CoAP header
            "\x63\xF4\x00\x00" // Observe option
            "\x60" // Content-Format
            "\xFF" "9001";
    avs_unit_mocksock_expect_output(mocksocks[0], NOTIFY_RESPONSE,
                                    sizeof(NOTIFY_RESPONSE) - 1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)->entries)->notify_task);

    ////// LESS //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 42));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)->entries)->notify_task);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, less_only) {
    static const anjay_dm_attributes_t ATTRS = {
        .min_period = 0,
        .max_period = 365 * 24 * 60 * 60, // a year
        .greater_than = ANJAY_ATTRIB_VALUE_NONE,
        .less_than = 777.0,
        .step = ANJAY_ATTRIB_VALUE_NONE
    };
    static const anjay_coap_msg_identity_t identity = {
        .msg_id = 0,
        .token_size = 0
    };

    ////// INITIALIZATION //////
    DM_TEST_INIT_WITH_SSIDS(14);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_observe_put_entry(
            anjay, &(const anjay_observe_key_t) {
                { 14, ANJAY_CONNECTION_UDP }, 42, 69, 4, ANJAY_COAP_FORMAT_NONE
            }, &(const anjay_msg_details_t) {
                .msg_type = ANJAY_COAP_MSG_ACKNOWLEDGEMENT,
                .msg_code = ANJAY_COAP_CODE_CONTENT,
                .format = ANJAY_COAP_FORMAT_PLAINTEXT,
                .observe_serial = true
            }, &identity, 514.0, "514", 3));
    _anjay_mock_dm_expect_clean();
    assert_observe_size(anjay, 1);

    ////// LESS //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 42));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    static const char NOTIFY_RESPONSE[] =
            "\x50\x45\x69\xED" // CoAP header
            "\x63\xF4\x00\x00" // Observe option
            "\x60" // Content-Format
            "\xFF" "42";
    avs_unit_mocksock_expect_output(mocksocks[0], NOTIFY_RESPONSE,
                                    sizeof(NOTIFY_RESPONSE) - 1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)->entries)->notify_task);

    ////// LESS //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 9001));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)->entries)->notify_task);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, step) {
    static const anjay_dm_attributes_t ATTRS = {
        .min_period = 0,
        .max_period = 365 * 24 * 60 * 60, // a year
        .greater_than = ANJAY_ATTRIB_VALUE_NONE,
        .less_than = ANJAY_ATTRIB_VALUE_NONE,
        .step = 10.0
    };
    static const anjay_coap_msg_identity_t identity = {
        .msg_id = 0,
        .token_size = 0
    };

    ////// INITIALIZATION //////
    DM_TEST_INIT_WITH_SSIDS(14);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_observe_put_entry(
            anjay, &(const anjay_observe_key_t) {
                { 14, ANJAY_CONNECTION_UDP }, 42, 69, 4, ANJAY_COAP_FORMAT_NONE
            }, &(const anjay_msg_details_t) {
                .msg_type = ANJAY_COAP_MSG_ACKNOWLEDGEMENT,
                .msg_code = ANJAY_COAP_CODE_CONTENT,
                .format = ANJAY_COAP_FORMAT_PLAINTEXT,
                .observe_serial = true
            }, &identity, 514.0, "514", 3));
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
    AVS_UNIT_ASSERT_NOT_NULL(AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)->entries)->notify_task);

    ////// INCREASE BY EXACTLY stp //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 524));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    static const char NOTIFY_RESPONSE0[] =
            "\x50\x45\x69\xED" // CoAP header
            "\x63\xF4\x00\x00" // Observe option
            "\x60" // Content-Format
            "\xFF" "524";
    avs_unit_mocksock_expect_output(mocksocks[0], NOTIFY_RESPONSE0,
                                    sizeof(NOTIFY_RESPONSE0) - 1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)->entries)->notify_task);

    ////// INCREASE BY OVER stp //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_FLOAT(0, 540.048));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    static const char NOTIFY_RESPONSE1[] =
            "\x50\x45\x69\xEE" // CoAP header
            "\x63\xF4\x00\x00" // Observe option
            "\x60" // Content-Format
            "\xFF" "540.048";
    avs_unit_mocksock_expect_output(mocksocks[0], NOTIFY_RESPONSE1,
                                    sizeof(NOTIFY_RESPONSE1) - 1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)->entries)->notify_task);

    ////// NON-NUMERIC VALUE //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_STRING(0, "trololo"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    static const char NOTIFY_RESPONSE2[] =
            "\x50\x45\x69\xEF" // CoAP header
            "\x63\xF4\x00\x00" // Observe option
            "\x60" // Content-Format
            "\xFF" "trololo";
    avs_unit_mocksock_expect_output(mocksocks[0], NOTIFY_RESPONSE2,
                                    sizeof(NOTIFY_RESPONSE2) - 1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)->entries)->notify_task);

    ////// BACK TO NUMBERS //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 42));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    static const char NOTIFY_RESPONSE3[] =
            "\x50\x45\x69\xF0" // CoAP header
            "\x63\xF4\x00\x00" // Observe option
            "\x60" // Content-Format
            "\xFF" "42";
    avs_unit_mocksock_expect_output(mocksocks[0], NOTIFY_RESPONSE3,
                                    sizeof(NOTIFY_RESPONSE3) - 1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)->entries)->notify_task);

    ////// TOO LITTLE DECREASE //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_FLOAT(0, 32.001));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)->entries)->notify_task);

    ////// DECREASE BY EXACTLY stp //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 31));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    static const char NOTIFY_RESPONSE4[] =
            "\x50\x45\x69\xF1" // CoAP header
            "\x63\xF4\x00\x00" // Observe option
            "\x60" // Content-Format
            "\xFF" "31";
    avs_unit_mocksock_expect_output(mocksocks[0], NOTIFY_RESPONSE4,
                                    sizeof(NOTIFY_RESPONSE4) - 1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)->entries)->notify_task);

    ////// DECREASE BY MORE THAN stp //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 20));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    static const char NOTIFY_RESPONSE5[] =
            "\x50\x45\x69\xF2" // CoAP header
            "\x63\xF4\x00\x00" // Observe option
            "\x60" // Content-Format
            "\xFF" "20";
    avs_unit_mocksock_expect_output(mocksocks[0], NOTIFY_RESPONSE5,
                                    sizeof(NOTIFY_RESPONSE5) - 1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)->entries)->notify_task);

    ////// INCREASE BY EXACTLY stp //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 30));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    static const char NOTIFY_RESPONSE6[] =
            "\x50\x45\x69\xF3" // CoAP header
            "\x63\xF4\x00\x00" // Observe option
            "\x60" // Content-Format
            "\xFF" "30";
    avs_unit_mocksock_expect_output(mocksocks[0], NOTIFY_RESPONSE6,
                                    sizeof(NOTIFY_RESPONSE6) - 1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(AVS_RBTREE_FIRST(AVS_RBTREE_FIRST(anjay->observe.connection_entries)->entries)->notify_task);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, multiple_formats) {
    static const anjay_dm_attributes_t ATTRS = {
        .min_period = 1,
        .max_period = 10,
        .greater_than = ANJAY_ATTRIB_VALUE_NONE,
        .less_than = ANJAY_ATTRIB_VALUE_NONE,
        .step = ANJAY_ATTRIB_VALUE_NONE
    };
    static anjay_coap_msg_identity_t identity = {
        .msg_id = 0,
        .token_size = 1
    };

    ////// INITIALIZATION //////
    DM_TEST_INIT_WITH_SSIDS(14);
    identity.token.bytes[0] = 'N';
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_observe_put_entry(
            anjay, &(const anjay_observe_key_t) {
                { 14, ANJAY_CONNECTION_UDP }, 42, 69, 4, ANJAY_COAP_FORMAT_NONE
            }, &(const anjay_msg_details_t) {
                .msg_type = ANJAY_COAP_MSG_ACKNOWLEDGEMENT,
                .msg_code = ANJAY_COAP_CODE_CONTENT,
                .format = ANJAY_COAP_FORMAT_PLAINTEXT,
                .observe_serial = true
            }, &identity, 514.0, "514", 3));
    identity.token.bytes[0] = 'P';
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_observe_put_entry(
            anjay, &(const anjay_observe_key_t) {
                { 14, ANJAY_CONNECTION_UDP },
                42, 69, 4, ANJAY_COAP_FORMAT_PLAINTEXT
            }, &(const anjay_msg_details_t) {
                .msg_type = ANJAY_COAP_MSG_ACKNOWLEDGEMENT,
                .msg_code = ANJAY_COAP_CODE_CONTENT,
                .format = ANJAY_COAP_FORMAT_PLAINTEXT,
                .observe_serial = true
            }, &identity, 514.0, "514", 3));
    identity.token.bytes[0] = 'T';
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_observe_put_entry(
            anjay, &(const anjay_observe_key_t) {
                { 14, ANJAY_CONNECTION_UDP }, 42, 69, 4, ANJAY_COAP_FORMAT_TLV
            }, &(const anjay_msg_details_t) {
                .msg_type = ANJAY_COAP_MSG_ACKNOWLEDGEMENT,
                .msg_code = ANJAY_COAP_CODE_CONTENT,
                .format = ANJAY_COAP_FORMAT_TLV,
                .observe_serial = true
            }, &identity, 514.0, "\xc2\x04\x02\x02", 4));
    assert_observe_size(anjay, 3);

    ////// NOTIFICATION //////
    _anjay_mock_clock_advance(&(const struct timespec) { 10, 0 });
    // no format preference
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_STRING(0, "Hello"));
    // plaintext
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_STRING(0, "Hello"));
    // TLV
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_STRING(0, "Hello"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    // no format preference response
    static const char N_NOTIFY_RESPONSE[] =
            "\x51\x45\x69\xED" "N" // CoAP header
            "\x63\xF9\x00\x00" // Observe option
            "\x60" // Content-Format
            "\xFF" "Hello";
    avs_unit_mocksock_expect_output(mocksocks[0], N_NOTIFY_RESPONSE,
                                    sizeof(N_NOTIFY_RESPONSE) - 1);
    // plaintext response
    static const char P_NOTIFY_RESPONSE[] =
            "\x51\x45\x69\xEE" "P" // CoAP header
            "\x63\xF9\x00\x00" // Observe option
            "\x60" // Content-Format
            "\xFF" "Hello";
    avs_unit_mocksock_expect_output(mocksocks[0], P_NOTIFY_RESPONSE,
                                    sizeof(P_NOTIFY_RESPONSE) - 1);
    // TLV response
    static const char T_NOTIFY_RESPONSE[] =
            "\x51\x45\x69\xEF" "T" // CoAP header
            "\x63\xF9\x00\x00" // Observe option
            "\x62\x2d\x16" // Content-Format
            "\xFF" "\xc5\x04" "Hello";
    avs_unit_mocksock_expect_output(mocksocks[0], T_NOTIFY_RESPONSE,
                                    sizeof(T_NOTIFY_RESPONSE) - 1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    assert_observe_size(anjay, 3);

    ////// NOTIFICATION - FORMAT CHANGE //////
    _anjay_mock_clock_advance(&(const struct timespec) { 10, 0 });
    // no format preference
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4,
                    ANJAY_MOCK_DM_BYTES(0, "\x12\x34\x56\x78"));
    // plaintext - error
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4,
                    ANJAY_MOCK_DM_BYTES(0, "\x12\x34\x56\x78"));
    // TLV
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4,
                    ANJAY_MOCK_DM_BYTES(0, "\x12\x34\x56\x78"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    // no format preference - response
    static const char N_BYTES_RESPONSE[] =
            "\x51\x45\x69\xF0" "N" // CoAP header
            "\x63\xFE\x00\x00" // Observe option
            "\x61\x2A" // Content-Format
            "\xFF" "\x12\x34\x56\x78";
    avs_unit_mocksock_expect_output(mocksocks[0], N_BYTES_RESPONSE,
                                    sizeof(N_BYTES_RESPONSE) - 1);
    // plaintext - response
    static const char P_BYTES_RESPONSE[] =
            "\x51\x45\x69\xF1" "P" // CoAP header
            "\x63\xFE\x00\x00" // Observe option
            "\x60" // Content-Format
            "\xFF" "EjRWeA==";
    avs_unit_mocksock_expect_output(mocksocks[0], P_BYTES_RESPONSE,
                                    sizeof(P_BYTES_RESPONSE) - 1);
    // TLV - response
    static const char T_BYTES_RESPONSE[] =
            "\x51\x45\x69\xF2" "T" // CoAP header
            "\x63\xFE\x00\x00" // Observe option
            "\x62\x2d\x16" // Content-Format
            "\xFF" "\xc4\x04" "\x12\x34\x56\x78";
    avs_unit_mocksock_expect_output(mocksocks[0], T_BYTES_RESPONSE,
                                    sizeof(T_BYTES_RESPONSE) - 1);
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
                    anjay,
                    &(const anjay_observe_connection_key_t) {
                        ssid, conn_type
                    });
    AVS_UNIT_ASSERT_NOT_NULL(conn);

    AVS_RBTREE_ELEM(anjay_observe_entry_t) new_entry =
            AVS_RBTREE_ELEM_NEW(anjay_observe_entry_t);
    AVS_UNIT_ASSERT_NOT_NULL(new_entry);

    memcpy((void *) (intptr_t) (const void *) &new_entry->key,
            &(const anjay_observe_key_t) {
                { ssid, conn_type }, oid, iid, rid, ANJAY_COAP_FORMAT_NONE
            }, sizeof(anjay_observe_key_t));
    AVS_RBTREE_ELEM(anjay_observe_entry_t) entry =
            AVS_RBTREE_INSERT(conn->entries, new_entry);
    if (entry != new_entry) {
        AVS_RBTREE_ELEM_DELETE_DETACHED(&new_entry);
    }
}

static anjay_t *create_test_env(void) {
    anjay_t *anjay = (anjay_t *) calloc(1, sizeof(anjay_t));
    _anjay_observe_init(anjay);
    test_observe_entry(anjay, 1, ANJAY_CONNECTION_UDP, 2, 3, 1);
    test_observe_entry(anjay, 1, ANJAY_CONNECTION_UDP, 2, 3, 2);
    test_observe_entry(anjay, 1, ANJAY_CONNECTION_UDP, 2, 9, 4);
    test_observe_entry(anjay, 1, ANJAY_CONNECTION_UDP, 4, 1, 1);
    test_observe_entry(anjay, 3, ANJAY_CONNECTION_UDP, 2, 3, -1);
    test_observe_entry(anjay, 3, ANJAY_CONNECTION_UDP, 2, 3, 3);
    test_observe_entry(anjay, 3, ANJAY_CONNECTION_UDP, 2, 7, 3);
    test_observe_entry(anjay, 3, ANJAY_CONNECTION_UDP, 6, 0, 1);
    test_observe_entry(anjay, 3, ANJAY_CONNECTION_UDP, 6, 0, 2);
    test_observe_entry(anjay, 8, ANJAY_CONNECTION_UDP, 4, ANJAY_IID_INVALID, -1);
    test_observe_entry(anjay, 8, ANJAY_CONNECTION_UDP, 6, 0, 1);
    return anjay;
}

static void destroy_test_env(anjay_t *anjay) {
    _anjay_observe_cleanup(anjay);
    free(anjay);
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
                             const anjay_dm_object_def_t *const *obj,
                             anjay_observe_entry_t *entity) {
    (void) anjay;
    (void) obj;
    AVS_UNIT_ASSERT_NOT_NULL(MOCK_NOTIFY);
    mock_notify_entry_value_t *entry = AVS_LIST_DETACH(&MOCK_NOTIFY);
    AVS_UNIT_ASSERT_EQUAL(entry_key_cmp(&entity->key, &entry->key), 0);
    int retval = entry->retval;
    AVS_LIST_DELETE(&entry);
    return retval;
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
    AVS_UNIT_MOCK(notify_entry) = mock_notify_entry;

    expect_notify_entry(8, 4, ANJAY_IID_INVALID, -1, ANJAY_COAP_FORMAT_NONE, 0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_observe_notify(anjay,
            &(const anjay_observe_key_t) {
                { 1, ANJAY_CONNECTION_WILDCARD },
                4, 1, 1, ANJAY_COAP_FORMAT_NONE
            }, true));
    expect_notify_clear();

    AVS_UNIT_ASSERT_SUCCESS(_anjay_observe_notify(anjay,
            &(const anjay_observe_key_t) {
                { 3, ANJAY_CONNECTION_WILDCARD },
                2, 7, -1, ANJAY_COAP_FORMAT_NONE
            }, true));
    expect_notify_clear();

    expect_notify_entry(3, 6, 0, 1, ANJAY_COAP_FORMAT_NONE, 0);
    expect_notify_entry(8, 6, 0, 1, ANJAY_COAP_FORMAT_NONE, 0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_observe_notify(anjay,
            &(const anjay_observe_key_t) {
                { 1, ANJAY_CONNECTION_WILDCARD },
                6, 0, 1, ANJAY_COAP_FORMAT_NONE
            }, true));
    expect_notify_clear();

    expect_notify_entry(3, 6, 0, 2, ANJAY_COAP_FORMAT_NONE, 0);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_observe_notify(anjay,
            &(const anjay_observe_key_t) {
                { 1, ANJAY_CONNECTION_WILDCARD },
                6, 0, 2, ANJAY_COAP_FORMAT_NONE
            }, true));
    expect_notify_clear();

    expect_notify_entry(1, 2, 3, 1, ANJAY_COAP_FORMAT_NONE, 0);
    expect_notify_entry(1, 2, 3, 2, ANJAY_COAP_FORMAT_NONE, -42);
    expect_notify_entry(1, 2, 9, 4, ANJAY_COAP_FORMAT_NONE, 0);
    expect_notify_entry(3, 2, 3, -1, ANJAY_COAP_FORMAT_NONE, -514);
    expect_notify_entry(3, 2, 3, 3, ANJAY_COAP_FORMAT_NONE, 0);
    expect_notify_entry(3, 2, 7, 3, ANJAY_COAP_FORMAT_NONE, 0);
    AVS_UNIT_ASSERT_EQUAL(_anjay_observe_notify(anjay,
            &(const anjay_observe_key_t) {
                { ANJAY_IID_INVALID, ANJAY_CONNECTION_WILDCARD },
                2, ANJAY_IID_INVALID, -1, ANJAY_COAP_FORMAT_NONE
            }, true), -42);
    expect_notify_clear();

    destroy_test_env(anjay);
}

AVS_UNIT_TEST(notify, storing_when_inactive) {
    SUCCESS_TEST(14, 34);

    // deactivate the first server
    anjay_active_server_info_t *inactive14 =
            AVS_LIST_DETACH(&anjay->servers.active);
    AVS_UNIT_ASSERT_NOT_NULL(AVS_LIST_INSERT_NEW(anjay_inactive_server_info_t,
                                                 &anjay->servers.inactive));
    anjay->servers.inactive->ssid = 14;
    _anjay_observe_gc(anjay);
    assert_observe_size(anjay, 2);

    // first notification
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    _anjay_mock_clock_advance(&(const struct timespec) { 1, 0 });

    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Rin"));

    expect_read_notif_storing(anjay, &FAKE_SERVER, 34, true);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Len"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    expect_read_notif_storing(anjay, &FAKE_SERVER, 34, true);
    static const char NOTIFY_RESPONSE[] =
            "\x50\x45\x69\xED" // CoAP header
            "\x63\xF4\x80\x00" // Observe option
            "\x60" // Content-Format
            "\xFF" "Len";
    avs_unit_mocksock_expect_output(mocksocks[1], NOTIFY_RESPONSE,
                                    sizeof(NOTIFY_RESPONSE) - 1);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    // second notification
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    _anjay_mock_clock_advance(&(const struct timespec) { 1, 0 });

    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Miku"));

    expect_read_notif_storing(anjay, &FAKE_SERVER, 34, true);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Luka"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    expect_read_notif_storing(anjay, &FAKE_SERVER, 34, true);
    static const char NOTIFY_RESPONSE2[] =
            "\x50\x45\x69\xEE" // CoAP header
            "\x63\xF5\x00\x00" // Observe option
            "\x60" // Content-Format
            "\xFF" "Luka";
    avs_unit_mocksock_expect_output(mocksocks[1], NOTIFY_RESPONSE2,
                                    sizeof(NOTIFY_RESPONSE2) - 1);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    // reactivate the server
    AVS_LIST_DELETE(&anjay->servers.inactive);
    AVS_UNIT_ASSERT_NULL(anjay->servers.inactive);
    AVS_LIST_INSERT(&anjay->servers.active, inactive14);
    _anjay_observe_gc(anjay);
    assert_observe_size(anjay, 2);
    _anjay_observe_sched_flush(anjay, 14, ANJAY_CONNECTION_UDP);

    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    static const char NOTIFY_RESPONSE3[] =
            "\x50\x45\x69\xEF" // CoAP header
            "\x63\xF5\x00\x00" // Observe option
            "\x60" // Content-Format
            "\xFF" "Rin";
    avs_unit_mocksock_expect_output(mocksocks[0], NOTIFY_RESPONSE3,
                                    sizeof(NOTIFY_RESPONSE3) - 1);
    static const char NOTIFY_RESPONSE4[] =
            "\x50\x45\x69\xF0" // CoAP header
            "\x63\xF5\x00\x00" // Observe option
            "\x60" // Content-Format
            "\xFF" "Miku";
    avs_unit_mocksock_expect_output(mocksocks[0], NOTIFY_RESPONSE4,
                                    sizeof(NOTIFY_RESPONSE4) - 1);
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, no_storing_when_disabled) {
    SUCCESS_TEST(14, 34);

    // deactivate the first server
    anjay_active_server_info_t *inactive14 =
            AVS_LIST_DETACH(&anjay->servers.active);
    AVS_UNIT_ASSERT_NOT_NULL(AVS_LIST_INSERT_NEW(anjay_inactive_server_info_t,
                                                 &anjay->servers.inactive));
    anjay->servers.inactive->ssid = 14;
    _anjay_observe_gc(anjay);
    assert_observe_size(anjay, 2);

    // first notification
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    _anjay_mock_clock_advance(&(const struct timespec) { 1, 0 });

    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, false);
    expect_read_notif_storing(anjay, &FAKE_SERVER, 34, false);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Ia"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    expect_read_notif_storing(anjay, &FAKE_SERVER, 34, false);
    static const char NOTIFY_RESPONSE[] =
            "\x50\x45\x69\xED" // CoAP header
            "\x63\xF4\x80\x00" // Observe option
            "\x60" // Content-Format
            "\xFF" "Ia";
    avs_unit_mocksock_expect_output(mocksocks[1], NOTIFY_RESPONSE,
                                    sizeof(NOTIFY_RESPONSE) - 1);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    // second notification
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    _anjay_mock_clock_advance(&(const struct timespec) { 1, 0 });

    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, false);
    expect_read_notif_storing(anjay, &FAKE_SERVER, 34, false);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Gumi"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    expect_read_notif_storing(anjay, &FAKE_SERVER, 34, true);
    static const char NOTIFY_RESPONSE2[] =
            "\x50\x45\x69\xEE" // CoAP header
            "\x63\xF5\x00\x00" // Observe option
            "\x60" // Content-Format
            "\xFF" "Gumi";
    avs_unit_mocksock_expect_output(mocksocks[1], NOTIFY_RESPONSE2,
                                    sizeof(NOTIFY_RESPONSE2) - 1);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    // reactivate the server
    AVS_LIST_DELETE(&anjay->servers.inactive);
    AVS_UNIT_ASSERT_NULL(anjay->servers.inactive);
    AVS_LIST_INSERT(&anjay->servers.active, inactive14);
    _anjay_observe_gc(anjay);
    assert_observe_size(anjay, 2);
    _anjay_observe_sched_flush(anjay, 14, ANJAY_CONNECTION_UDP);

    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, storing_on_send_error) {
    SUCCESS_TEST(14);

    // first notification
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    _anjay_mock_clock_advance(&(const struct timespec) { 1, 0 });

    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Meiko"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    avs_unit_mocksock_output_fail(mocksocks[0], -1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    // second notification
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    _anjay_mock_clock_advance(&(const struct timespec) { 1, 0 });

    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Kaito"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    avs_unit_mocksock_output_fail(mocksocks[0], -1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    // anjay_serve() with reschedule notification sending
    static const char REQUEST[] =
            "\x40\x01\xFB\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "69" // IID
            "\x01" "3"; // RID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 3, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 3, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 3, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Mayu"));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0],
            "\x60\x45\xFB\x3E" // CoAP header
            "\xC0" // Content-Format
            "\xFF" "Mayu");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));

    // now the notifications shall arrive
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    static const char NOTIFY_RESPONSE[] =
            "\x50\x45\x69\xEF" // CoAP header
            "\x63\xF5\x00\x00" // Observe option
            "\x60" // Content-Format
            "\xFF" "Meiko";
    avs_unit_mocksock_expect_output(mocksocks[0], NOTIFY_RESPONSE,
                                    sizeof(NOTIFY_RESPONSE) - 1);
    static const char NOTIFY_RESPONSE2[] =
            "\x50\x45\x69\xF0" // CoAP header
            "\x63\xF5\x00\x00" // Observe option
            "\x60" // Content-Format
            "\xFF" "Kaito";
    avs_unit_mocksock_expect_output(mocksocks[0], NOTIFY_RESPONSE2,
                                    sizeof(NOTIFY_RESPONSE2) - 1);
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

    _anjay_mock_clock_advance(&(const struct timespec) { 1, 0 });

    // let's leave storing on for a moment
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Meiko"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    avs_unit_mocksock_output_fail(mocksocks[0], -1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    // second notification
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    _anjay_mock_clock_advance(&(const struct timespec) { 1, 0 });

    // and now we have it disabled
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, false);
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Kaito"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, false);
    avs_unit_mocksock_output_fail(mocksocks[0], -1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    // anjay_serve() with reschedule notification sending...
    static const char REQUEST[] =
            "\x40\x01\xFB\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x02" "69" // IID
            "\x01" "3"; // RID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 3, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 3, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 3, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Mayu"));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0],
            "\x60\x45\xFB\x3E" // CoAP header
            "\xC0" // Content-Format
            "\xFF" "Mayu");
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));

    // ...but nothing should come
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, storing_of_errors) {
    SUCCESS_TEST(14);

    // first notification
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    _anjay_mock_clock_advance(&(const struct timespec) { 1, 0 });

    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    // error during attribute reading
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, -1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    avs_unit_mocksock_output_fail(mocksocks[0], -1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    // second notification - should not actually do anything
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    _anjay_mock_clock_advance(&(const struct timespec) { 1, 0 });
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    // sending is now scheduled, should receive the previous error
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    static const char NOTIFY_RESPONSE[] =
            "\x50\xA0\x69\xEE" // CoAP header
            "\x63\xF5\x00\x00"; // Observe option
    avs_unit_mocksock_expect_output(mocksocks[0], NOTIFY_RESPONSE,
                                    sizeof(NOTIFY_RESPONSE) - 1);
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

    _anjay_mock_clock_advance(&(const struct timespec) { 1, 0 });

    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, false);
    // error during attribute reading
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, -1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, false);
    avs_unit_mocksock_output_fail(mocksocks[0], -1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    // now the notification shall be gone
    assert_observe_size(anjay, 0);

    DM_TEST_FINISH;
}
