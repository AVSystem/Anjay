/*
 * Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
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

#include <anjay_init.h>

#include <math.h>
#include <stdarg.h>

#include <avsystem/commons/avs_unit_test.h>

#include "src/core/anjay_core.h"
#include "src/core/servers/anjay_server_connections.h"
#include "src/core/servers/anjay_servers_internal.h"
#include "tests/core/coap/utils.h"
#include "tests/utils/dm.h"
#include "tests/utils/mock_clock.h"
#include "tests/utils/utils.h"

#define MSG_ID_BASE 0x0000

static void assert_observe_consistency(anjay_t *anjay) {
    AVS_LIST(anjay_observe_connection_entry_t) conn;
    AVS_LIST_FOREACH(conn, anjay->observe.connection_entries) {
        size_t path_refs_in_observations = 0;
        AVS_RBTREE_ELEM(anjay_observation_t) observation;
        AVS_RBTREE_FOREACH(observation, conn->observations) {
            path_refs_in_observations += observation->paths_count;
        }

        size_t path_refs = 0;
        AVS_RBTREE_ELEM(anjay_observe_path_entry_t) path_entry;
        AVS_RBTREE_FOREACH(path_entry, conn->observed_paths) {
            AVS_LIST(AVS_RBTREE_ELEM(anjay_observation_t)) ref;
            AVS_LIST_FOREACH(ref, path_entry->refs) {
                ++path_refs;
                AVS_UNIT_ASSERT_NOT_NULL(ref);
                AVS_UNIT_ASSERT_NOT_NULL(*ref);
                AVS_UNIT_ASSERT_TRUE(AVS_RBTREE_FIND(conn->observations, *ref)
                                     == *ref);
                bool path_found = false;
                for (size_t i = 0; i < (*ref)->paths_count; ++i) {
                    if (_anjay_uri_path_equal(&(*ref)->paths[i],
                                              &path_entry->path)) {
                        path_found = true;
                        break;
                    }
                }
                AVS_UNIT_ASSERT_TRUE(path_found);
            }
        }
        AVS_UNIT_ASSERT_EQUAL(path_refs_in_observations, path_refs);
    }
}

static void assert_observe_size(anjay_t *anjay, size_t sz) {
    size_t result = 0;
    AVS_LIST(anjay_observe_connection_entry_t) conn;
    AVS_LIST_FOREACH(conn, anjay->observe.connection_entries) {
        size_t local_size = AVS_RBTREE_SIZE(conn->observations);
        AVS_UNIT_ASSERT_NOT_EQUAL(local_size, 0);
        result += local_size;
    }
    AVS_UNIT_ASSERT_EQUAL(result, sz);
}

static void assert_msg_details_equal(const anjay_msg_details_t *a,
                                     const anjay_msg_details_t *b) {
    AVS_UNIT_ASSERT_EQUAL(a->msg_code, b->msg_code);
    AVS_UNIT_ASSERT_EQUAL(a->format, b->format);
    /* Yes, a pointer comparison */
    AVS_UNIT_ASSERT_TRUE(a->uri_path == b->uri_path);
    AVS_UNIT_ASSERT_TRUE(a->uri_query == b->uri_query);
    AVS_UNIT_ASSERT_TRUE(a->location_path == b->location_path);
}

static void assert_observe(anjay_t *anjay,
                           anjay_ssid_t ssid,
                           const avs_coap_token_t *token,
                           const anjay_uri_path_t *uri,
                           const anjay_msg_details_t *details,
                           const void *data,
                           size_t length) {
    AVS_LIST(anjay_observe_connection_entry_t) *conn_ptr =
            find_connection_state((anjay_connection_ref_t) {
                .server = *_anjay_servers_find_ptr(anjay->servers, ssid),
                .conn_type = ANJAY_CONNECTION_PRIMARY
            });
    AVS_UNIT_ASSERT_NOT_NULL(conn_ptr);
    AVS_RBTREE_ELEM(anjay_observation_t) observation =
            AVS_RBTREE_FIND((*conn_ptr)->observations,
                            _anjay_observation_query(token));
    AVS_UNIT_ASSERT_NOT_NULL(observation);
    AVS_UNIT_ASSERT_EQUAL(observation->paths_count, 1);
    AVS_UNIT_ASSERT_TRUE(_anjay_uri_path_equal(&observation->paths[0], uri));
    AVS_UNIT_ASSERT_NULL(observation->last_unsent);
    AVS_UNIT_ASSERT_NOT_NULL(observation->last_sent);
    assert_msg_details_equal(&observation->last_sent->details, details);

    char buf[length];
    avs_stream_outbuf_t out_buf_stream = AVS_STREAM_OUTBUF_STATIC_INITIALIZER;
    avs_stream_outbuf_set_buffer(&out_buf_stream, buf, length);

    anjay_output_ctx_t *out_ctx = NULL;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_dynamic_construct(
            &out_ctx, (avs_stream_t *) &out_buf_stream, uri, details->format,
            ANJAY_ACTION_READ));
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_batch_data_output(anjay, observation->last_sent->values[0],
                                     ANJAY_SSID_BOOTSTRAP, out_ctx));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out_ctx));
    AVS_UNIT_ASSERT_EQUAL(avs_stream_outbuf_offset(&out_buf_stream), length);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(buf, data, length);
}

static void expect_server_res_read(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj,
                                   anjay_ssid_t ssid,
                                   anjay_rid_t rid,
                                   const anjay_mock_dm_data_t *data) {
    assert((*obj)->oid == ANJAY_DM_OID_SERVER);
    _anjay_mock_dm_expect_list_instances(
            anjay, obj, 0, (const anjay_iid_t[]) { ssid, ANJAY_ID_INVALID });
    assert(rid > ANJAY_DM_RID_SERVER_SSID);
    anjay_mock_dm_res_entry_t resources[] = {
        { ANJAY_DM_RID_SERVER_SSID, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT },
        { ANJAY_DM_RID_SERVER_LIFETIME, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
        { ANJAY_DM_RID_SERVER_DEFAULT_PMIN, ANJAY_DM_RES_RW,
          ANJAY_DM_RES_ABSENT },
        { ANJAY_DM_RID_SERVER_DEFAULT_PMAX, ANJAY_DM_RES_RW,
          ANJAY_DM_RES_ABSENT },
        { ANJAY_DM_RID_SERVER_NOTIFICATION_STORING, ANJAY_DM_RES_RW,
          ANJAY_DM_RES_ABSENT },
        { ANJAY_DM_RID_SERVER_BINDING, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
        ANJAY_MOCK_DM_RES_END
    };
    size_t i;
    for (i = 0; i < AVS_ARRAY_SIZE(resources); ++i) {
        if (resources[i].rid == rid) {
            resources[i].presence = ANJAY_DM_RES_PRESENT;
            break;
        }
    }
    AVS_UNIT_ASSERT_TRUE(i < AVS_ARRAY_SIZE(resources));
    _anjay_mock_dm_expect_list_resources(anjay, obj, ssid, 0, resources);
    _anjay_mock_dm_expect_resource_read(anjay, obj, ssid,
                                        ANJAY_DM_RID_SERVER_SSID,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, ssid));
    _anjay_mock_dm_expect_list_resources(anjay, obj, ssid, 0, resources);
    _anjay_mock_dm_expect_resource_read(anjay, obj, ssid, rid, ANJAY_ID_INVALID,
                                        0, data);
}

static void expect_read_notif_storing(anjay_t *anjay,
                                      const anjay_dm_object_def_t *const *obj,
                                      anjay_ssid_t ssid,
                                      bool value) {
    expect_server_res_read(anjay, obj, ssid,
                           ANJAY_DM_RID_SERVER_NOTIFICATION_STORING,
                           ANJAY_MOCK_DM_BOOL(0, value));
}

#define ASSERT_SUCCESS_TEST_RESULT(Ssid)                    \
    assert_observe(anjay, Ssid,                             \
                   &(const avs_coap_token_t) {              \
                       .size = 8,                           \
                       .bytes = "SuccsTkn"                  \
                   },                                       \
                   &MAKE_RESOURCE_PATH(42, 69, 4),          \
                   &(const anjay_msg_details_t) {           \
                       .msg_code = AVS_COAP_CODE_CONTENT,   \
                       .format = AVS_COAP_FORMAT_PLAINTEXT, \
                   },                                       \
                   "514", 3)

#define SUCCESS_TEST(...)                                                     \
    DM_TEST_INIT_WITH_SSIDS(__VA_ARGS__);                                     \
    do {                                                                      \
        for (size_t i = 0; i < AVS_ARRAY_SIZE(ssids); ++i) {                  \
            DM_TEST_REQUEST(mocksocks[i], CON, GET,                           \
                            ID_TOKEN(0xFA3E, "SuccsTkn"), OBSERVE(0),         \
                            PATH("42", "69", "4"));                           \
            _anjay_mock_dm_expect_list_instances(                             \
                    anjay, &OBJ, 0,                                           \
                    (const anjay_iid_t[]) { 69, ANJAY_ID_INVALID });          \
            _anjay_mock_dm_expect_list_resources(                             \
                    anjay, &OBJ, 69, 0,                                       \
                    (const anjay_mock_dm_res_entry_t[]) {                     \
                            { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },      \
                            { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },      \
                            { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },      \
                            { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },      \
                            { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },     \
                            { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },      \
                            { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },      \
                            ANJAY_MOCK_DM_RES_END });                         \
            _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4,           \
                                                ANJAY_ID_INVALID, 0,          \
                                                ANJAY_MOCK_DM_INT(0, 514));   \
            DM_TEST_EXPECT_READ_NULL_ATTRS(ssids[i], 69, 4);                  \
            DM_TEST_EXPECT_RESPONSE(mocksocks[i], ACK, CONTENT,               \
                                    ID_TOKEN(0xFA3E, "SuccsTkn"), OBSERVE(0), \
                                    CONTENT_FORMAT(PLAINTEXT),                \
                                    PAYLOAD("514"));                          \
            AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[i]));        \
            assert_observe_size(anjay, i + 1);                                \
            ASSERT_SUCCESS_TEST_RESULT(ssids[i]);                             \
        }                                                                     \
        anjay_sched_run(anjay);                                               \
    } while (0)

AVS_UNIT_TEST(observe, read_failed) {
    DM_TEST_INIT_WITH_SSIDS(4);
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID_TOKEN(0xFA3E, "Res7"),
                    OBSERVE(0), PATH("42", "5", "7"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 14, 42, ANJAY_ID_INVALID });
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, NOT_FOUND,
                            ID_TOKEN(0xFA3E, "Res7"), NO_PAYLOAD);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 0);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(observe, simple) {
    SUCCESS_TEST(14);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(observe, read_attrs_failed) {
    DM_TEST_INIT_WITH_SSIDS(4);
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID_TOKEN(0xFA3E, "Res4"),
                    OBSERVE(0), PATH("42", "69", "4"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0,
            (const anjay_iid_t[]) { 14, 42, 69, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, 514));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0,
            (const anjay_iid_t[]) { 14, 42, 69, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read_attrs(anjay, &OBJ, 69, 4, 4, -1, NULL);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT,
                            ID_TOKEN(0xFA3E, "Res4"), CONTENT_FORMAT(PLAINTEXT),
                            PAYLOAD("514"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 0);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(observe, multiple_equivalent_observations) {
    SUCCESS_TEST(14);
    // "Res4" observation is equivalent to "SuccsTst" created by SUCCESS_TEST()
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID_TOKEN(0xFA3E, "Res4"),
                    OBSERVE(0), PATH("42", "69", "4"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0,
            (const anjay_iid_t[]) { 14, 42, 69, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, 42));
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT,
                            ID_TOKEN(0xFA3E, "Res4"), OBSERVE(0),
                            CONTENT_FORMAT(PLAINTEXT), PAYLOAD("42"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 2);
    ASSERT_SUCCESS_TEST_RESULT(14);
    assert_observe(anjay, 14,
                   &(const avs_coap_token_t) {
                       .size = 4,
                       .bytes = "Res4"
                   },
                   &MAKE_RESOURCE_PATH(42, 69, 4),
                   &(const anjay_msg_details_t) {
                       .msg_code = AVS_COAP_CODE_CONTENT,
                       .format = AVS_COAP_FORMAT_PLAINTEXT
                   },
                   "42", 2);
#undef TLV_RESPONSE
    anjay_sched_run(anjay);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(observe, overwrite) {
    SUCCESS_TEST(14);
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID_TOKEN(0xFA3E, "SuccsTkn"),
                    OBSERVE(0), PATH("42", "69", "5"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0,
            (const anjay_iid_t[]) { 14, 42, 69, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RWM, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RWM, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RWM, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RWM, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RWM, ANJAY_DM_RES_ABSENT },
                    { 5, ANJAY_DM_RES_RWM, ANJAY_DM_RES_PRESENT },
                    { 6, ANJAY_DM_RES_RWM, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_list_resource_instances(
            anjay, &OBJ, 69, 5, 0,
            (const anjay_riid_t[]) { 4, 7, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 5, 4, 0,
                                        ANJAY_MOCK_DM_INT(0, 777));
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 5, 7, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Hi!"));
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 5);
#define TLV_RESPONSE   \
    "\x88\x05\x09"     \
    "\x42\x04\x03\x09" \
    "\x43\x07"         \
    "Hi!"
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT,
                            ID_TOKEN(0xFA3E, "SuccsTkn"), OBSERVE(0),
                            CONTENT_FORMAT(OMA_LWM2M_TLV),
                            PAYLOAD(TLV_RESPONSE));
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 1);
    assert_observe(anjay, 14,
                   &(const avs_coap_token_t) {
                       .size = 8,
                       .bytes = "SuccsTkn"
                   },
                   &MAKE_RESOURCE_PATH(42, 69, 5),
                   &(const anjay_msg_details_t) {
                       .msg_code = AVS_COAP_CODE_CONTENT,
                       .format = AVS_COAP_FORMAT_OMA_LWM2M_TLV
                   },
                   TLV_RESPONSE, sizeof(TLV_RESPONSE) - 1);
#undef TLV_RESPONSE
    anjay_sched_run(anjay);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(observe, instance_overwrite) {
    SUCCESS_TEST(14);
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID_TOKEN(0xFA3E, "ObjToken"),
                    OBSERVE(0), PATH("42", "69"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0,
            (const anjay_iid_t[]) { 14, 42, 69, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 2, ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_STRING(0, "wow"));
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_STRING(0, "such value"));
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, -1);
#define TLV_RESPONSE \
    "\xc3\x02"       \
    "wow"            \
    "\xc8\x04\x0a"   \
    "such value"
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT,
                            ID_TOKEN(0xFA3E, "ObjToken"), OBSERVE(0),
                            CONTENT_FORMAT(OMA_LWM2M_TLV),
                            PAYLOAD(TLV_RESPONSE));
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 2);
    ASSERT_SUCCESS_TEST_RESULT(14);
    assert_observe(anjay, 14,
                   &(const avs_coap_token_t) {
                       .size = 8,
                       .bytes = "ObjToken"
                   },
                   &MAKE_INSTANCE_PATH(42, 69),
                   &(const anjay_msg_details_t) {
                       .msg_code = AVS_COAP_CODE_CONTENT,
                       .format = AVS_COAP_FORMAT_OMA_LWM2M_TLV
                   },
                   TLV_RESPONSE, sizeof(TLV_RESPONSE) - 1);
#undef TLV_RESPONSE
    anjay_sched_run(anjay);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(observe, cancel_deregister) {
    SUCCESS_TEST(14);
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID_TOKEN(0xFA3E, "Res6"),
                    OBSERVE(0x01), PATH("42", "69", "6"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0,
            (const anjay_iid_t[]) { 14, 42, 69, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    ANJAY_MOCK_DM_RES_END });

    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 6, ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Hello"));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT,
                            ID_TOKEN(0xFA3E, "Res6"), CONTENT_FORMAT(PLAINTEXT),
                            PAYLOAD("Hello"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 1);
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID_TOKEN(0xFA3E, "SuccsTkn"),
                    OBSERVE(0x01), PATH("42", "69", "4"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0,
            (const anjay_iid_t[]) { 14, 42, 69, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Good-bye"));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT,
                            ID_TOKEN(0xFA3E, "SuccsTkn"),
                            CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Good-bye"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 0);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(observe, cancel_deregister_keying) {
    SUCCESS_TEST(14);
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID_TOKEN(0xFA3E, "Res5"),
                    OBSERVE(0), PATH("42", "69", "5"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0,
            (const anjay_iid_t[]) { 14, 42, 69, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 5, ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, 42));
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 5);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT,
                            ID_TOKEN(0xFA3E, "Res5"), OBSERVE(0),
                            CONTENT_FORMAT(PLAINTEXT), PAYLOAD("42"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 2);

    // CANCEL USING Res5 TOKEN BUT /42/69/4 PATH
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID_TOKEN(0xFA3E, "Res5"),
                    OBSERVE(0x01), PATH("42", "69", "4"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0,
            (const anjay_iid_t[]) { 14, 42, 69, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Good-bye"));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT,
                            ID_TOKEN(0xFA3E, "Res5"), CONTENT_FORMAT(PLAINTEXT),
                            PAYLOAD("Good-bye"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 1);
    ASSERT_SUCCESS_TEST_RESULT(14);

    // CANCEL USING SuccsTkn TOKEN BUT /42/69/5 PATH
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID_TOKEN(0xFA3F, "SuccsTkn"),
                    OBSERVE(0x01), PATH("42", "69", "5"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0,
            (const anjay_iid_t[]) { 14, 42, 69, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 5, ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Sayonara"));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT,
                            ID_TOKEN(0xFA3F, "SuccsTkn"),
                            CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Sayonara"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 0);
    DM_TEST_FINISH;
}

static inline void remove_server(anjay_t *anjay,
                                 AVS_LIST(anjay_server_info_t) *server_ptr) {
    anjay_server_connection_t *connection =
            _anjay_get_server_connection((const anjay_connection_ref_t) {
                .server = *server_ptr,
                .conn_type = ANJAY_CONNECTION_PRIMARY
            });
    AVS_UNIT_ASSERT_NOT_NULL(connection);
    _anjay_mocksock_expect_stats_zero(connection->conn_socket_);
    _anjay_connection_internal_clean_socket(anjay, connection);
    AVS_LIST_DELETE(server_ptr);
}

AVS_UNIT_TEST(observe, gc) {
    SUCCESS_TEST(14, 69, 514, 666, 777);

    remove_server(anjay, &anjay->servers->servers);

    _anjay_observe_gc(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 4);
    ASSERT_SUCCESS_TEST_RESULT(69);
    ASSERT_SUCCESS_TEST_RESULT(514);
    ASSERT_SUCCESS_TEST_RESULT(666);
    ASSERT_SUCCESS_TEST_RESULT(777);

    remove_server(anjay, AVS_LIST_NTH_PTR(&anjay->servers->servers, 3));

    _anjay_observe_gc(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 3);
    ASSERT_SUCCESS_TEST_RESULT(69);
    ASSERT_SUCCESS_TEST_RESULT(514);
    ASSERT_SUCCESS_TEST_RESULT(666);

    remove_server(anjay, AVS_LIST_NTH_PTR(&anjay->servers->servers, 1));

    _anjay_observe_gc(anjay);
    assert_observe_consistency(anjay);
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
                                  const anjay_dm_internal_r_attrs_t *attrs) {
    _anjay_mock_dm_expect_list_instances(
            anjay, obj_ptr, 0, (const anjay_iid_t[]) { iid, ANJAY_ID_INVALID });
    anjay_mock_dm_res_entry_t resources[] = {
        { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
        { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
        { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
        { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
        { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
        { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
        { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
        ANJAY_MOCK_DM_RES_END
    };
    size_t i;
    for (i = 0; i < AVS_ARRAY_SIZE(resources); ++i) {
        if (resources[i].rid == rid) {
            resources[i].presence = ANJAY_DM_RES_PRESENT;
            break;
        }
    }
    AVS_UNIT_ASSERT_TRUE(i < AVS_ARRAY_SIZE(resources));
    _anjay_mock_dm_expect_list_resources(anjay, obj_ptr, iid, 0, resources);
    _anjay_mock_dm_expect_resource_read_attrs(anjay, obj_ptr, iid, rid, ssid, 0,
                                              attrs);
    if (!_anjay_dm_attributes_full(_anjay_dm_get_internal_oi_attrs_const(
                &attrs->standard.common))) {
        _anjay_mock_dm_expect_instance_read_default_attrs(
                anjay, obj_ptr, iid, ssid, 0,
                &ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY);
        _anjay_mock_dm_expect_object_read_default_attrs(
                anjay, obj_ptr, ssid, 0, &ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY);
    }
}

static void expect_read_res(anjay_t *anjay,
                            const anjay_dm_object_def_t *const *obj_ptr,
                            anjay_iid_t iid,
                            anjay_rid_t rid,
                            const anjay_mock_dm_data_t *data) {
    _anjay_mock_dm_expect_list_instances(
            anjay, obj_ptr, 0, (const anjay_iid_t[]) { iid, ANJAY_ID_INVALID });
    anjay_mock_dm_res_entry_t resources[] = {
        { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
        { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
        { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
        { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
        { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
        { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
        { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
        ANJAY_MOCK_DM_RES_END
    };
    size_t i;
    for (i = 0; i < AVS_ARRAY_SIZE(resources); ++i) {
        if (resources[i].rid == rid) {
            resources[i].presence = ANJAY_DM_RES_PRESENT;
            break;
        }
    }
    AVS_UNIT_ASSERT_TRUE(i < AVS_ARRAY_SIZE(resources));
    _anjay_mock_dm_expect_list_resources(anjay, obj_ptr, iid, 0, resources);
    _anjay_mock_dm_expect_resource_read(anjay, obj_ptr, iid, rid,
                                        ANJAY_ID_INVALID, 0, data);
}

static const avs_coap_observe_id_t RES4_IDENTITY = {
    .token = {
        .size = 4,
        .bytes = "Res4"
    }
};

static void notify_max_period_test(const char *con_notify_ack,
                                   size_t con_notify_ack_size,
                                   size_t observe_size_after_ack) {
    static const anjay_dm_internal_r_attrs_t ATTRS = {
        .standard = {
            .common = {
                .min_period = 1,
                .max_period = 10,
                .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
            },
            .greater_than = ANJAY_ATTRIB_VALUE_NONE,
            .less_than = ANJAY_ATTRIB_VALUE_NONE,
            .step = ANJAY_ATTRIB_VALUE_NONE
        }
    };

    ////// INITIALIZATION //////
    DM_TEST_INIT_WITH_SSIDS(14);
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID_TOKEN(0x69ED, "Res4"),
                    OBSERVE(0), PATH("42", "69", "4"));
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_FLOAT(0, 514.0));
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT,
                            ID_TOKEN(0x69ED, "Res4"), CONTENT_FORMAT(PLAINTEXT),
                            OBSERVE(0), PAYLOAD("514"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));

    assert_observe_size(anjay, 1);

    ////// EMPTY SCHEDULER RUN //////
    _anjay_mock_clock_advance(avs_time_duration_from_scalar(5, AVS_TIME_S));
    anjay_sched_run(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 1);

    ////// PLAIN NOTIFICATION //////
    _anjay_mock_clock_advance(avs_time_duration_from_scalar(5, AVS_TIME_S));
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_STRING(0, "Hello"));
    const coap_test_msg_t *notify_response =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE, "Res4"), OBSERVE(1),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Hello"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response->content,
                                    notify_response->length);
    anjay_sched_run(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 1);

    assert_observe(anjay, 14,
                   &(const avs_coap_token_t) {
                       .size = 4,
                       .bytes = "Res4"
                   },
                   &MAKE_RESOURCE_PATH(42, 69, 4),
                   &(const anjay_msg_details_t) {
                       .msg_code = AVS_COAP_CODE_CONTENT,
                       .format = AVS_COAP_FORMAT_PLAINTEXT
                   },
                   "Hello", 5);

    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(anjay->observe.connection_entries->observations)
                    ->notify_task);
    AVS_UNIT_ASSERT_EQUAL(
            AVS_RBTREE_FIRST(anjay->observe.connection_entries->observations)
                    ->last_sent->timestamp.since_real_epoch.seconds,
            1010);
    AVS_UNIT_ASSERT_EQUAL(
            AVS_RBTREE_FIRST(anjay->observe.connection_entries->observations)
                    ->last_confirmable.since_real_epoch.seconds,
            1000);

    ////// CONFIRMABLE NOTIFICATION //////
    _anjay_mock_clock_advance(avs_time_duration_diff(
            avs_time_duration_from_scalar(1, AVS_TIME_DAY),
            avs_time_duration_from_scalar(10, AVS_TIME_S)));
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_STRING(0, "Hi!"));
    const coap_test_msg_t *con_notify_response =
            COAP_MSG(CON, CONTENT, ID_TOKEN(MSG_ID_BASE + 1, "Res4"),
                     OBSERVE(2), CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Hi!"));
    avs_unit_mocksock_expect_output(mocksocks[0], con_notify_response->content,
                                    con_notify_response->length);
    anjay_sched_run(anjay);
    avs_unit_mocksock_input(mocksocks[0], con_notify_ack, con_notify_ack_size);
    anjay_serve(anjay, mocksocks[0]);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, observe_size_after_ack);
    if (observe_size_after_ack) {
        AVS_UNIT_ASSERT_EQUAL(
                AVS_RBTREE_FIRST(
                        anjay->observe.connection_entries->observations)
                        ->last_confirmable.since_real_epoch.seconds,
                AVS_RBTREE_FIRST(
                        anjay->observe.connection_entries->observations)
                        ->last_sent->timestamp.since_real_epoch.seconds);

        ////// ANOTHER PLAIN NOTIFICATION //////
        _anjay_mock_clock_advance(
                avs_time_duration_from_scalar(10, AVS_TIME_S));
        expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
        expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_STRING(0, "Howdy!"));
        const coap_test_msg_t *non_notify_response =
                COAP_MSG(NON, CONTENT, ID_TOKEN(0x0002, "Res4"), OBSERVE(3),
                         CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Howdy!"));
        avs_unit_mocksock_expect_output(mocksocks[0],
                                        non_notify_response->content,
                                        non_notify_response->length);
        anjay_sched_run(anjay);
        assert_observe_consistency(anjay);
        assert_observe_size(anjay, 1);

        assert_observe(anjay, 14,
                       &(const avs_coap_token_t) {
                           .size = 4,
                           .bytes = "Res4"
                       },
                       &MAKE_RESOURCE_PATH(42, 69, 4),
                       &(const anjay_msg_details_t) {
                           .msg_code = AVS_COAP_CODE_CONTENT,
                           .format = AVS_COAP_FORMAT_PLAINTEXT
                       },
                       "Howdy!", 6);
    }

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, max_period) {
    notify_max_period_test("\x60\x00\x00\x01", 4, 1); // CON
    notify_max_period_test("\x70\x00\x00\x01", 4, 0); // Reset
}

AVS_UNIT_TEST(notify, min_period) {
    static const anjay_dm_internal_r_attrs_t ATTRS = {
        .standard = {
            .common = {
                .min_period = 10,
                .max_period = 365 * 24 * 60 * 60 /* a year */,
                .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
            },
            .greater_than = ANJAY_ATTRIB_VALUE_NONE,
            .less_than = ANJAY_ATTRIB_VALUE_NONE,
            .step = ANJAY_ATTRIB_VALUE_NONE
        }
    };

    ////// INITIALIZATION //////
    DM_TEST_INIT_WITH_SSIDS(14);
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID_TOKEN(0x69ED, "Res4"),
                    OBSERVE(0), PATH("42", "69", "4"));
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_FLOAT(0, 514.0));
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT,
                            ID_TOKEN(0x69ED, "Res4"), CONTENT_FORMAT(PLAINTEXT),
                            OBSERVE(0), PAYLOAD("514"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));

    assert_observe_size(anjay, 1);

    ////// PMIN NOT REACHED //////
    _anjay_mock_clock_advance(avs_time_duration_from_scalar(5, AVS_TIME_S));
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);
    anjay_sched_run(anjay);

    ////// PMIN REACHED //////
    _anjay_mock_clock_advance(avs_time_duration_from_scalar(5, AVS_TIME_S));
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_STRING(0, "Hi!"));
    const coap_test_msg_t *notify_response =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE, "Res4"), OBSERVE(1),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Hi!"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response->content,
                                    notify_response->length);
    anjay_sched_run(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 1);

    ////// AFTER PMIN, NO CHANGE //////
    _anjay_mock_clock_advance(avs_time_duration_from_scalar(10, AVS_TIME_S));
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_STRING(0, "Hi!"));
    anjay_sched_run(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(anjay->observe.connection_entries->observations)
                    ->notify_task);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, epmin_greater_than_pmax) {
    static const anjay_dm_internal_r_attrs_t ATTRS = {
        .standard = {
            .common = {
                .min_period = 0,
                .max_period = 5,
                .min_eval_period = 8,
                .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
            },
            .greater_than = ANJAY_ATTRIB_VALUE_NONE,
            .less_than = ANJAY_ATTRIB_VALUE_NONE,
            .step = ANJAY_ATTRIB_VALUE_NONE
        }
    };

    ////// INITIALIZATION //////
    DM_TEST_INIT_WITH_SSIDS(14);
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID_TOKEN(0x69ED, "I love C"),
                    OBSERVE(0), PATH("42", "69", "4"));
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 314159));
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT,
                            ID_TOKEN(0x69ED, "I love C"),
                            CONTENT_FORMAT(PLAINTEXT), OBSERVE(0),
                            PAYLOAD("314159"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));

    ////// NOTIFICATION BEFORE EPMIN EXPIRATION //////
    _anjay_mock_clock_advance(avs_time_duration_from_scalar(6, AVS_TIME_S));
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    // pmax has expired but epmin has not expired yet so we don't expect calling
    // read_handler
    const coap_test_msg_t *notify_response1 =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE, "I love C"),
                     OBSERVE(1), CONTENT_FORMAT(PLAINTEXT), PAYLOAD("314159"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response1->content,
                                    notify_response1->length);
    anjay_sched_run(anjay);

    ////// NOTIFICATION AFTER EPMIN EXPIRATION //////
    _anjay_mock_clock_advance(avs_time_duration_from_scalar(6, AVS_TIME_S));
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    // epmin has finally expired so read_handler can be called
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 271828));
    const coap_test_msg_t *notify_response2 =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE + 1, "I love C"),
                     OBSERVE(2), CONTENT_FORMAT(PLAINTEXT), PAYLOAD("271828"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response2->content,
                                    notify_response2->length);
    anjay_sched_run(anjay);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, epmin_less_than_pmax) {
    static const anjay_dm_internal_r_attrs_t ATTRS = {
        .standard = {
            .common = {
                .min_period = 0,
                .max_period = 365 * 24 * 60 * 60 /* a year */,
                .min_eval_period = 15,
                .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
            },
            .greater_than = ANJAY_ATTRIB_VALUE_NONE,
            .less_than = ANJAY_ATTRIB_VALUE_NONE,
            .step = ANJAY_ATTRIB_VALUE_NONE
        }
    };

    ////// INITIALIZATION //////
    DM_TEST_INIT_WITH_SSIDS(14);
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID_TOKEN(0x69ED, "I love C"),
                    OBSERVE(0), PATH("42", "69", "4"));
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 314159));
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT,
                            ID_TOKEN(0x69ED, "I love C"),
                            CONTENT_FORMAT(PLAINTEXT), OBSERVE(0),
                            PAYLOAD("314159"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));

    ////// NOTIFY ABOUT RESOURCE CHANGE //////
    _anjay_mock_clock_advance(avs_time_duration_from_scalar(10, AVS_TIME_S));
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);
    // We don't send notification yet because epmin didn't expire

    ////// EPMIN EXPIRED BUT RESOURCE VALUE REMAINS THE SAME //////
    _anjay_mock_clock_advance(avs_time_duration_from_scalar(10, AVS_TIME_S));
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 314159));
    anjay_sched_run(anjay);
    // We don't send notification yet because resource value remains the same

    ////// NOTIFY ABOUT RESOURCE CHANGE //////
    _anjay_mock_clock_advance(avs_time_duration_from_scalar(10, AVS_TIME_S));
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);

    ////// EPMIN EXPIRED AND RESOURCE VALUE CHANGED //////
    _anjay_mock_clock_advance(avs_time_duration_from_scalar(10, AVS_TIME_S));
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 271828));
    const coap_test_msg_t *notify_response =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE, "I love C"),
                     OBSERVE(1), CONTENT_FORMAT(PLAINTEXT), PAYLOAD("271828"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response->content,
                                    notify_response->length);
    anjay_sched_run(anjay);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, confirmable) {
    ////// INITIALIZATION //////
    const anjay_dm_object_def_t *const *obj_defs[] = {
        DM_TEST_DEFAULT_OBJECTS
    };
    anjay_ssid_t ssids[] = { 14 };
    DM_TEST_INIT_GENERIC(obj_defs, ssids,
                         DM_TEST_CONFIGURATION(.confirmable_notifications =
                                                       true));
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID_TOKEN(0x69ED, "Res4"),
                    OBSERVE(0), PATH("42", "69", "4"));
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_FLOAT(0, 514.0));
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT,
                            ID_TOKEN(0x69ED, "Res4"), CONTENT_FORMAT(PLAINTEXT),
                            OBSERVE(0), PAYLOAD("514"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));

    assert_observe_size(anjay, 1);

    ////// EMPTY SCHEDULER RUN //////
    _anjay_mock_clock_advance(avs_time_duration_from_scalar(5, AVS_TIME_S));
    anjay_sched_run(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 1);

    ////// CONFIRMABLE NOTIFICATION //////
    _anjay_mock_clock_advance(avs_time_duration_from_scalar(5, AVS_TIME_S));
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 42));
    const coap_test_msg_t *notify_response =
            COAP_MSG(CON, CONTENT, ID_TOKEN(MSG_ID_BASE, "Res4"), OBSERVE(1),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("42"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response->content,
                                    notify_response->length);
    anjay_sched_run(anjay);

    const coap_test_msg_t *notify_ack =
            COAP_MSG(ACK, EMPTY, ID(MSG_ID_BASE), NO_PAYLOAD);
    avs_unit_mocksock_input(mocksocks[0], notify_ack->content,
                            notify_ack->length);
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    anjay_serve(anjay, mocksocks[0]);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, extremes) {
    static const anjay_dm_internal_r_attrs_t ATTRS = {
        .standard = {
            .common = {
                .min_period = 0,
                .max_period = 365 * 24 * 60 * 60 /* a year */,
                .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
            },
            .greater_than = 777.0,
            .less_than = 69.0,
            .step = ANJAY_ATTRIB_VALUE_NONE
        }
    };

    ////// INITIALIZATION //////
    DM_TEST_INIT_WITH_SSIDS(14);
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID_TOKEN(0x69ED, "Res4"),
                    OBSERVE(0), PATH("42", "69", "4"));
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_FLOAT(0, 514.0));
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT,
                            ID_TOKEN(0x69ED, "Res4"), CONTENT_FORMAT(PLAINTEXT),
                            OBSERVE(0), PAYLOAD("514"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));

    assert_observe_size(anjay, 1);

    ////// LESS //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_FLOAT(0, 42.43));
    const coap_test_msg_t *notify_response =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE, "Res4"), OBSERVE(1),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("42.43"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response->content,
                                    notify_response->length);
    anjay_sched_run(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(anjay->observe.connection_entries->observations)
                    ->notify_task);

    ////// EVEN LESS //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_FLOAT(0, 14.7));
    anjay_sched_run(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(anjay->observe.connection_entries->observations)
                    ->notify_task);

    ////// IN BETWEEN //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 695));
    const coap_test_msg_t *notify_response2 =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE + 1, "Res4"),
                     OBSERVE(2), CONTENT_FORMAT(PLAINTEXT), PAYLOAD("695"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response2->content,
                                    notify_response2->length);
    anjay_sched_run(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(anjay->observe.connection_entries->observations)
                    ->notify_task);

    ////// EQUAL - STILL NOT CROSSING //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 69));
    anjay_sched_run(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(anjay->observe.connection_entries->observations)
                    ->notify_task);

    ////// GREATER //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 1024));
    const coap_test_msg_t *notify_response3 =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE + 2, "Res4"),
                     OBSERVE(3), CONTENT_FORMAT(PLAINTEXT), PAYLOAD("1024"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response3->content,
                                    notify_response3->length);
    anjay_sched_run(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(anjay->observe.connection_entries->observations)
                    ->notify_task);

    ////// STILL GREATER //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 999));
    anjay_sched_run(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(anjay->observe.connection_entries->observations)
                    ->notify_task);

    ////// LESS AGAIN //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_FLOAT(0, -69.75));
    const coap_test_msg_t *notify_response4 =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE + 3, "Res4"),
                     OBSERVE(4), CONTENT_FORMAT(PLAINTEXT), PAYLOAD("-69.75"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response4->content,
                                    notify_response4->length);
    anjay_sched_run(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(anjay->observe.connection_entries->observations)
                    ->notify_task);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, greater_only) {
    static const anjay_dm_internal_r_attrs_t ATTRS = {
        .standard = {
            .common = {
                .min_period = 0,
                .max_period = 365 * 24 * 60 * 60 /* a year */,
                .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
            },
            .greater_than = 69.0,
            .less_than = ANJAY_ATTRIB_VALUE_NONE,
            .step = ANJAY_ATTRIB_VALUE_NONE
        }
    };

    ////// INITIALIZATION (GREATER) //////
    DM_TEST_INIT_WITH_SSIDS(14);
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID_TOKEN(0x69ED, "Res4"),
                    OBSERVE(0), PATH("42", "69", "4"));
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_FLOAT(0, 514.0));
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT,
                            ID_TOKEN(0x69ED, "Res4"), CONTENT_FORMAT(PLAINTEXT),
                            OBSERVE(0), PAYLOAD("514"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));

    assert_observe_size(anjay, 1);

    ////// STILL GREATER //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 9001));
    anjay_sched_run(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(anjay->observe.connection_entries->observations)
                    ->notify_task);

    ////// LESS //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 42));
    const coap_test_msg_t *notify_response =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE, "Res4"), OBSERVE(1),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("42"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response->content,
                                    notify_response->length);
    anjay_sched_run(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(anjay->observe.connection_entries->observations)
                    ->notify_task);

    ////// GREATER AGAIN //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 77));
    const coap_test_msg_t *notify_response2 =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE + 1, "Res4"),
                     OBSERVE(2), CONTENT_FORMAT(PLAINTEXT), PAYLOAD("77"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response2->content,
                                    notify_response2->length);
    anjay_sched_run(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(anjay->observe.connection_entries->observations)
                    ->notify_task);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, less_only) {
    static const anjay_dm_internal_r_attrs_t ATTRS = {
        .standard = {
            .common = {
                .min_period = 0,
                .max_period = 365 * 24 * 60 * 60 /* a year */,
                .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
            },
            .greater_than = ANJAY_ATTRIB_VALUE_NONE,
            .less_than = 777.0,
            .step = ANJAY_ATTRIB_VALUE_NONE
        }
    };

    ////// INITIALIZATION (GREATER) //////
    DM_TEST_INIT_WITH_SSIDS(14);
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID_TOKEN(0x69ED, "Res4"),
                    OBSERVE(0), PATH("42", "69", "4"));
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_FLOAT(0, 1337.0));
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT,
                            ID_TOKEN(0x69ED, "Res4"), CONTENT_FORMAT(PLAINTEXT),
                            OBSERVE(0), PAYLOAD("1337"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));

    assert_observe_size(anjay, 1);

    ////// LESS //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 42));
    const coap_test_msg_t *notify_response =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE, "Res4"), OBSERVE(1),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("42"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response->content,
                                    notify_response->length);
    anjay_sched_run(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(anjay->observe.connection_entries->observations)
                    ->notify_task);

    ////// STILL LESS //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 514));
    anjay_sched_run(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(anjay->observe.connection_entries->observations)
                    ->notify_task);

    ////// GREATER //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 9001));
    const coap_test_msg_t *notify_response2 =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE + 1, "Res4"),
                     OBSERVE(2), CONTENT_FORMAT(PLAINTEXT), PAYLOAD("9001"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response2->content,
                                    notify_response2->length);
    anjay_sched_run(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(anjay->observe.connection_entries->observations)
                    ->notify_task);

    ////// LESS AGAIN //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 69));
    const coap_test_msg_t *notify_response3 =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE + 2, "Res4"),
                     OBSERVE(3), CONTENT_FORMAT(PLAINTEXT), PAYLOAD("69"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response3->content,
                                    notify_response3->length);
    anjay_sched_run(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(anjay->observe.connection_entries->observations)
                    ->notify_task);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, step) {
    static const anjay_dm_internal_r_attrs_t ATTRS = {
        .standard = {
            .common = {
                .min_period = 0,
                .max_period = 365 * 24 * 60 * 60 /* a year */,
                .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
            },
            .greater_than = ANJAY_ATTRIB_VALUE_NONE,
            .less_than = ANJAY_ATTRIB_VALUE_NONE,
            .step = 10.0
        }
    };

    ////// INITIALIZATION //////
    DM_TEST_INIT_WITH_SSIDS(14);
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID_TOKEN(0x69ED, "Res4"),
                    OBSERVE(0), PATH("42", "69", "4"));
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_FLOAT(0, 514.0));
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT,
                            ID_TOKEN(0x69ED, "Res4"), CONTENT_FORMAT(PLAINTEXT),
                            OBSERVE(0), PAYLOAD("514"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    assert_observe_size(anjay, 1);

    ////// TOO LITTLE INCREASE //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_FLOAT(0, 523.5));
    anjay_sched_run(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(anjay->observe.connection_entries->observations)
                    ->notify_task);

    ////// INCREASE BY EXACTLY stp //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 524));
    const coap_test_msg_t *notify_response0 =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE, "Res4"), OBSERVE(1),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("524"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response0->content,
                                    notify_response0->length);
    anjay_sched_run(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(anjay->observe.connection_entries->observations)
                    ->notify_task);

    ////// INCREASE BY OVER stp //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_FLOAT(0, 540.048));
    const coap_test_msg_t *notify_response1 =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE + 1, "Res4"),
                     OBSERVE(2), CONTENT_FORMAT(PLAINTEXT), PAYLOAD("540.048"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response1->content,
                                    notify_response1->length);
    anjay_sched_run(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(anjay->observe.connection_entries->observations)
                    ->notify_task);

    ////// NON-NUMERIC VALUE //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_STRING(0, "trololo"));
    const coap_test_msg_t *notify_response2 =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE + 2, "Res4"),
                     OBSERVE(3), CONTENT_FORMAT(PLAINTEXT), PAYLOAD("trololo"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response2->content,
                                    notify_response2->length);
    anjay_sched_run(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(anjay->observe.connection_entries->observations)
                    ->notify_task);

    ////// BACK TO NUMBERS //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 42));
    const coap_test_msg_t *notify_response3 =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE + 3, "Res4"),
                     OBSERVE(4), CONTENT_FORMAT(PLAINTEXT), PAYLOAD("42"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response3->content,
                                    notify_response3->length);
    anjay_sched_run(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(anjay->observe.connection_entries->observations)
                    ->notify_task);

    ////// TOO LITTLE DECREASE //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_FLOAT(0, 32.001));
    anjay_sched_run(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(anjay->observe.connection_entries->observations)
                    ->notify_task);

    ////// DECREASE BY EXACTLY stp //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 31));
    const coap_test_msg_t *notify_response4 =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE + 4, "Res4"),
                     OBSERVE(5), CONTENT_FORMAT(PLAINTEXT), PAYLOAD("31"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response4->content,
                                    notify_response4->length);
    anjay_sched_run(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(anjay->observe.connection_entries->observations)
                    ->notify_task);

    ////// DECREASE BY MORE THAN stp //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 20));
    const coap_test_msg_t *notify_response5 =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE + 5, "Res4"),
                     OBSERVE(6), CONTENT_FORMAT(PLAINTEXT), PAYLOAD("20"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response5->content,
                                    notify_response5->length);
    anjay_sched_run(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(anjay->observe.connection_entries->observations)
                    ->notify_task);

    ////// INCREASE BY EXACTLY stp //////
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_INT(0, 30));
    const coap_test_msg_t *notify_response6 =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE + 6, "Res4"),
                     OBSERVE(7), CONTENT_FORMAT(PLAINTEXT), PAYLOAD("30"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response6->content,
                                    notify_response6->length);
    anjay_sched_run(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 1);
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_RBTREE_FIRST(anjay->observe.connection_entries->observations)
                    ->notify_task);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, multiple_formats) {
    static const anjay_dm_internal_r_attrs_t ATTRS = {
        .standard = {
            .common = {
                .min_period = 1,
                .max_period = 10,
                .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE,
                .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
            },
            .greater_than = ANJAY_ATTRIB_VALUE_NONE,
            .less_than = ANJAY_ATTRIB_VALUE_NONE,
            .step = ANJAY_ATTRIB_VALUE_NONE
        }
    };

    ////// INITIALIZATION //////
    DM_TEST_INIT_WITH_SSIDS(14);
    // Token: N
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID_TOKEN(0x69ED, "N"), OBSERVE(0),
                    PATH("42", "69", "4"));
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_FLOAT(0, 514.0));
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT, ID_TOKEN(0x69ED, "N"),
                            CONTENT_FORMAT(PLAINTEXT), OBSERVE(0),
                            PAYLOAD("514"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    // Token: P
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID_TOKEN(0x69ED, "P"), OBSERVE(0),
                    ACCEPT(AVS_COAP_FORMAT_PLAINTEXT), PATH("42", "69", "4"));
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_FLOAT(0, 514.0));
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT, ID_TOKEN(0x69ED, "P"),
                            CONTENT_FORMAT(PLAINTEXT), OBSERVE(0),
                            PAYLOAD("514"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    // Token: T
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID_TOKEN(0x69ED, "T"),
                    ACCEPT(AVS_COAP_FORMAT_OMA_LWM2M_TLV), OBSERVE(0),
                    PATH("42", "69", "4"));
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_FLOAT(0, 514.0));
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT, ID_TOKEN(0x69ED, "T"),
                            CONTENT_FORMAT(OMA_LWM2M_TLV), OBSERVE(0),
                            PAYLOAD("\xC4\x04\x44\x00\x80\x00"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));

    assert_observe_size(anjay, 3);

    ////// NOTIFICATION //////
    _anjay_mock_clock_advance(avs_time_duration_from_scalar(10, AVS_TIME_S));
    // no format preference
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_STRING(0, "Hello"));
    const coap_test_msg_t *n_notify_response =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE, "N"), OBSERVE(1),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Hello"));
    avs_unit_mocksock_expect_output(mocksocks[0], n_notify_response->content,
                                    n_notify_response->length);
    // plaintext
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_STRING(0, "Hello"));
    const coap_test_msg_t *p_notify_response =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE + 1, "P"), OBSERVE(1),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Hello"));
    avs_unit_mocksock_expect_output(mocksocks[0], p_notify_response->content,
                                    p_notify_response->length);
    // TLV
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4, ANJAY_MOCK_DM_STRING(0, "Hello"));
    const coap_test_msg_t *t_notify_response =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE + 2, "T"), OBSERVE(1),
                     CONTENT_FORMAT(OMA_LWM2M_TLV),
                     PAYLOAD("\xc5\x04"
                             "Hello"));
    avs_unit_mocksock_expect_output(mocksocks[0], t_notify_response->content,
                                    t_notify_response->length);
    anjay_sched_run(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 3);

    ////// NOTIFICATION - FORMAT CHANGE //////
    _anjay_mock_clock_advance(avs_time_duration_from_scalar(10, AVS_TIME_S));
    // no format preference - uses previous format (plaintext)
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4,
                    ANJAY_MOCK_DM_BYTES(0, "\x12\x34\x56\x78"));
    const coap_test_msg_t *n_bytes_response =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE + 3, "N"), OBSERVE(2),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("EjRWeA=="));
    avs_unit_mocksock_expect_output(mocksocks[0], n_bytes_response->content,
                                    n_bytes_response->length);
    // plaintext
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4,
                    ANJAY_MOCK_DM_BYTES(0, "\x12\x34\x56\x78"));
    const coap_test_msg_t *p_bytes_response =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE + 4, "P"), OBSERVE(2),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("EjRWeA=="));
    avs_unit_mocksock_expect_output(mocksocks[0], p_bytes_response->content,
                                    p_bytes_response->length);
    // TLV
    expect_read_res_attrs(anjay, &OBJ, 14, 69, 4, &ATTRS);
    expect_read_res(anjay, &OBJ, 69, 4,
                    ANJAY_MOCK_DM_BYTES(0, "\x12\x34\x56\x78"));
    const coap_test_msg_t *t_bytes_response =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE + 5, "T"), OBSERVE(2),
                     CONTENT_FORMAT(OMA_LWM2M_TLV),
                     PAYLOAD("\xc4\x04"
                             "\x12\x34\x56\x78"));
    avs_unit_mocksock_expect_output(mocksocks[0], t_bytes_response->content,
                                    t_bytes_response->length);
    anjay_sched_run(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 3);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, storing_when_inactive) {
    SUCCESS_TEST(14, 34);
    anjay_server_connection_t *connection =
            _anjay_get_server_connection((const anjay_connection_ref_t) {
                .server = anjay->servers->servers,
                .conn_type = ANJAY_CONNECTION_PRIMARY
            });
    AVS_UNIT_ASSERT_NOT_NULL(connection);

    // deactivate the first server
    avs_net_socket_t *socket14 = connection->conn_socket_;
    connection->conn_socket_ = NULL;
    _anjay_observe_gc(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 2);

    // first notification
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);

    _anjay_mock_clock_advance(avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 69, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Rin"));

    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 69, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Len"));
    const coap_test_msg_t *notify_response =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE, "SuccsTkn"),
                     OBSERVE(1), CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Len"));
    avs_unit_mocksock_expect_output(mocksocks[1], notify_response->content,

                                    notify_response->length);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    anjay_sched_run(anjay);

    // second notification
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);

    _anjay_mock_clock_advance(avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 69, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Miku"));

    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 69, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Luka"));
    const coap_test_msg_t *notify_response2 =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE + 1, "SuccsTkn"),
                     OBSERVE(2), CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Luka"));
    avs_unit_mocksock_expect_output(mocksocks[1], notify_response2->content,
                                    notify_response2->length);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    anjay_sched_run(anjay);

    // reactivate the server
    connection->conn_socket_ = socket14;
    _anjay_observe_gc(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 2);
    anjay->current_connection.server = anjay->servers->servers;
    anjay->current_connection.conn_type = ANJAY_CONNECTION_PRIMARY;
    _anjay_observe_sched_flush(anjay->current_connection);
    memset(&anjay->current_connection, 0, sizeof(anjay->current_connection));

    const coap_test_msg_t *notify_response3 =
            COAP_MSG(NON, CONTENT, ID_TOKEN(0x0000, "SuccsTkn"), OBSERVE(1),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Rin"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response3->content,
                                    notify_response3->length);
    anjay_sched_run(anjay);

    const coap_test_msg_t *notify_response4 =
            COAP_MSG(NON, CONTENT, ID_TOKEN(0x0001, "SuccsTkn"), OBSERVE(2),
                     CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Miku"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response4->content,
                                    notify_response4->length);
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    anjay_sched_run(anjay);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, no_storing_when_disabled) {
    SUCCESS_TEST(14, 34);
    anjay_server_connection_t *connection =
            _anjay_get_server_connection((const anjay_connection_ref_t) {
                .server = anjay->servers->servers,
                .conn_type = ANJAY_CONNECTION_PRIMARY
            });
    AVS_UNIT_ASSERT_NOT_NULL(connection);

    // deactivate the first server
    avs_net_socket_t *socket14 = connection->conn_socket_;
    connection->conn_socket_ = NULL;
    _anjay_observe_gc(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 2);

    // first notification
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);

    _anjay_mock_clock_advance(avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, false);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 69, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Ia"));
    const coap_test_msg_t *notify_response =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE, "SuccsTkn"),
                     OBSERVE(1), CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Ia"));
    avs_unit_mocksock_expect_output(mocksocks[1], notify_response->content,
                                    notify_response->length);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    anjay_sched_run(anjay);

    // second notification
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);

    _anjay_mock_clock_advance(avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, false);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 69, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Gumi"));
    const coap_test_msg_t *notify_response2 =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE + 1, "SuccsTkn"),
                     OBSERVE(2), CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Gumi"));
    avs_unit_mocksock_expect_output(mocksocks[1], notify_response2->content,
                                    notify_response2->length);
    DM_TEST_EXPECT_READ_NULL_ATTRS(34, 69, 4);
    anjay_sched_run(anjay);

    // reactivate the server
    connection->conn_socket_ = socket14;
    _anjay_observe_gc(anjay);
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 2);
    anjay->current_connection.server = anjay->servers->servers;
    anjay->current_connection.conn_type = ANJAY_CONNECTION_PRIMARY;
    _anjay_observe_sched_flush(anjay->current_connection);
    memset(&anjay->current_connection, 0, sizeof(anjay->current_connection));

    anjay_sched_run(anjay);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, storing_on_send_error) {
    SUCCESS_TEST(14);

    // first notification
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);

    _anjay_mock_clock_advance(avs_time_duration_from_scalar(1, AVS_TIME_S));

    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 69, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Meiko"));
    // this is a hack: all other errno values trigger reconnection
    avs_unit_mocksock_output_fail(mocksocks[0], avs_errno(AVS_EMSGSIZE));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    anjay_sched_run(anjay);

    // second notification
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);

    _anjay_mock_clock_advance(avs_time_duration_from_scalar(1, AVS_TIME_S));

    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 69, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Kaito"));
    avs_unit_mocksock_output_fail(mocksocks[0], avs_errno(AVS_EMSGSIZE));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    anjay_sched_run(anjay);

    // anjay_serve() will reschedule notification sending
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID(0xFB3E), PATH("42", "69", "3"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 69, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 3, ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Mayu"));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT, ID(0xFB3E),
                            CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Mayu"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));

    // now the notifications shall arrive
    const coap_test_msg_t *notify_response =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE + 2, "SuccsTkn"),
                     OBSERVE(3), CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Meiko"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response->content,
                                    notify_response->length);
    anjay_sched_run(anjay);

    const coap_test_msg_t *notify_response2 =
            COAP_MSG(NON, CONTENT, ID_TOKEN(MSG_ID_BASE + 3, "SuccsTkn"),
                     OBSERVE(4), CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Kaito"));
    avs_unit_mocksock_expect_output(mocksocks[0], notify_response2->content,
                                    notify_response2->length);
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    anjay_sched_run(anjay);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, no_storing_on_send_error) {
    SUCCESS_TEST(14);

    // first notification
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);

    _anjay_mock_clock_advance(avs_time_duration_from_scalar(1, AVS_TIME_S));

    // let's leave storing on for a moment
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 69, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Meiko"));
    avs_unit_mocksock_output_fail(mocksocks[0], avs_errno(AVS_EMSGSIZE));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, true);
    anjay_sched_run(anjay);

    // second notification
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);

    _anjay_mock_clock_advance(avs_time_duration_from_scalar(1, AVS_TIME_S));

    // and now we have it disabled
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 69, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Kaito"));
    avs_unit_mocksock_output_fail(mocksocks[0], avs_errno(AVS_EMSGSIZE));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, false);
    anjay_sched_run(anjay);

    // anjay_serve() will reschedule notification sending...
    DM_TEST_REQUEST(mocksocks[0], CON, GET, ID(0xFB3E), PATH("42", "69", "3"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 69, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 3, ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Mayu"));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CONTENT, ID(0xFB3E),
                            CONTENT_FORMAT(PLAINTEXT), PAYLOAD("Mayu"));
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));

    // ...but nothing should come
    anjay_sched_run(anjay);

    DM_TEST_FINISH;
}

static void storing_of_errors_test_impl(bool storing_resource_value) {
    SUCCESS_TEST(14);

    // first notification
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);

    _anjay_mock_clock_advance(avs_time_duration_from_scalar(1, AVS_TIME_S));

    // error during reading
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, -1, (const anjay_iid_t[]) { 69, ANJAY_ID_INVALID });
    avs_unit_mocksock_output_fail(mocksocks[0], avs_errno(AVS_EMSGSIZE));
    expect_read_notif_storing(anjay, &FAKE_SERVER, 14, storing_resource_value);
    anjay_sched_run(anjay);

    // second notification - should not actually do anything
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);

    _anjay_mock_clock_advance(avs_time_duration_from_scalar(1, AVS_TIME_S));

    // sending is now scheduled, should receive the previous error
    const coap_test_msg_t *con_notify_response =
            COAP_MSG(CON, INTERNAL_SERVER_ERROR,
                     ID_TOKEN(MSG_ID_BASE + 1, "SuccsTkn"), NO_PAYLOAD);
    avs_unit_mocksock_expect_output(mocksocks[0], con_notify_response->content,
                                    con_notify_response->length);
    anjay_sched_run(anjay);

    const coap_test_msg_t *con_ack =
            COAP_MSG(ACK, EMPTY, ID(MSG_ID_BASE + 1), NO_PAYLOAD);
    avs_unit_mocksock_input(mocksocks[0], con_ack->content, con_ack->length);
    anjay_serve(anjay, mocksocks[0]);

    // now the notification shall be gone
    assert_observe_consistency(anjay);
    assert_observe_size(anjay, 0);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(notify, storing_of_errors) {
    storing_of_errors_test_impl(true);
}

AVS_UNIT_TEST(notify, no_storing_of_errors) {
    // As a special exception, notification storing is always enabled for
    // errors, regardless of the actual setting.
    storing_of_errors_test_impl(false);
}

AVS_UNIT_TEST(notify, send_error) {
    SUCCESS_TEST(14);

    // first notification
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    anjay_sched_run(anjay);

    _anjay_mock_clock_advance(avs_time_duration_from_scalar(1, AVS_TIME_S));

    // let's leave storing on for a moment
    DM_TEST_EXPECT_READ_NULL_ATTRS(14, 69, 4);
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 69, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 69, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 2, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 3, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 4, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
                    { 5, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    { 6, ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Meiko"));
    avs_unit_mocksock_output_fail(mocksocks[0], avs_errno(AVS_ECONNRESET));
    anjay_sched_run(anjay);

    _anjay_mocksock_expect_stats_zero(mocksocks[0]);
    anjay_sched_run(anjay);

    DM_TEST_FINISH;
}
