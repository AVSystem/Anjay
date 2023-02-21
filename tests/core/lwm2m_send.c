/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#include <anjay/lwm2m_send.h>

#include <avsystem/commons/avs_unit_mocksock.h>
#include <avsystem/commons/avs_unit_test.h>
#include <avsystem/commons/avs_utils.h>

#include <avsystem/coap/async.h>

#include <inttypes.h>

#include "src/core/anjay_core.h"
#include "src/core/coap/anjay_content_format.h"
#include "src/core/io/anjay_batch_builder.h"
#include "src/core/io/anjay_common.h"
#include "src/core/io/anjay_vtable.h"
#include "src/core/io/cbor/anjay_cbor_types.h"
#include "src/core/servers/anjay_servers_internal.h"
#include "src/modules/server/anjay_mod_server.h"
#include "tests/core/coap/utils.h"
#include "tests/utils/dm.h"

static const anjay_ssid_t SSID = 1;

static const uint16_t MSG_ID = 0x0000;

static const anjay_uri_path_t URI_PATH =
        RESOURCE_PATH_INITIALIZER(42, 0xDEAD, 0);

static const uint16_t VALUE = 0xFACE;

static anjay_send_batch_t *
get_new_batch_with_int_value(anjay_uri_path_t resource_path,
                             uint64_t resource_value) {
    assert(_anjay_uri_path_leaf_is(&resource_path, ANJAY_ID_RID));
    anjay_send_batch_builder_t *builder = anjay_send_batch_builder_new();
    AVS_UNIT_ASSERT_NOT_NULL(builder);
    AVS_UNIT_ASSERT_SUCCESS(anjay_send_batch_add_uint(
            builder, resource_path.ids[ANJAY_ID_OID],
            resource_path.ids[ANJAY_ID_IID], resource_path.ids[ANJAY_ID_RID],
            ANJAY_ID_INVALID, AVS_TIME_REAL_INVALID, resource_value));
    anjay_send_batch_t *batch = anjay_send_batch_builder_compile(&builder);
    AVS_UNIT_ASSERT_NULL(builder);
    AVS_UNIT_ASSERT_NOT_NULL(batch);
    return batch;
}

static anjay_send_batch_t *get_new_batch_with_int_value_from_dm(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_uri_path_t resource_path,
        int resource_value) {
    assert(_anjay_uri_path_leaf_is(&resource_path, ANJAY_ID_RID));
    anjay_send_batch_builder_t *builder = anjay_send_batch_builder_new();
    AVS_UNIT_ASSERT_NOT_NULL(builder);
    _anjay_mock_dm_expect_list_instances(
            anjay, obj_ptr, 0,
            (const anjay_iid_t[]) { resource_path.ids[ANJAY_ID_IID],
                                    ANJAY_ID_INVALID });
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
        if (resources[i].rid == resource_path.ids[ANJAY_ID_RID]) {
            resources[i].presence = ANJAY_DM_RES_PRESENT;
            break;
        }
    }
    AVS_UNIT_ASSERT_TRUE(i < AVS_ARRAY_SIZE(resources));
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, resource_path.ids[ANJAY_ID_IID], 0, resources);
    _anjay_mock_dm_expect_resource_read(anjay, obj_ptr,
                                        resource_path.ids[ANJAY_ID_IID],
                                        resource_path.ids[ANJAY_ID_RID],
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, resource_value));
    AVS_UNIT_ASSERT_SUCCESS(anjay_send_batch_data_add_current(
            builder, anjay, resource_path.ids[ANJAY_ID_OID],
            resource_path.ids[ANJAY_ID_IID], resource_path.ids[ANJAY_ID_RID]));
    anjay_send_batch_t *batch = anjay_send_batch_builder_compile(&builder);
    AVS_UNIT_ASSERT_NULL(builder);
    AVS_UNIT_ASSERT_NOT_NULL(batch);
    return batch;
}

typedef struct {
    anjay_send_finished_handler_t *real_handler;
    void *real_handler_data;
} test_finished_handler_arg_t;

static AVS_LIST(test_finished_handler_arg_t) HANDLER_WRAPPER_ARGS = NULL;

static void test_finished_handler_wrapper(anjay_t *anjay,
                                          anjay_ssid_t ssid,
                                          const anjay_send_batch_t *batch,
                                          int result,
                                          void *arg_) {
    test_finished_handler_arg_t *arg = (test_finished_handler_arg_t *) arg_;
    if (arg->real_handler) {
        arg->real_handler(anjay, ssid, batch, result, arg->real_handler_data);
    }
    AVS_LIST(test_finished_handler_arg_t) *arg_ptr =
            AVS_LIST_FIND_PTR(&HANDLER_WRAPPER_ARGS, arg);
    AVS_UNIT_ASSERT_NOT_NULL(arg_ptr);
    AVS_LIST_DELETE(arg_ptr);
}

static void assert_there_is_not_any_server(anjay_t *anjay) {
    _anjay_mock_dm_expect_list_instances(
            anjay, &FAKE_SERVER, 0, (const anjay_iid_t[]) { ANJAY_ID_INVALID });
}

static const anjay_mock_dm_res_entry_t SERVER_RESOURCES[] = {
    { SERV_RES_SSID, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT },
    { SERV_RES_MUTE_SEND, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
    ANJAY_MOCK_DM_RES_END
};

static void assert_there_is_server_with_ssid(anjay_ssid_t ssid,
                                             anjay_t *anjay) {
    _anjay_mock_dm_expect_list_instances(
            anjay, &FAKE_SERVER, 0,
            (const anjay_iid_t[]) { ssid, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(anjay, &FAKE_SERVER, ssid, 0,
                                         SERVER_RESOURCES);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, ssid,
                                        SERV_RES_SSID, ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, ssid));
}

static void assert_mute_send_resource_read_failure(anjay_t *anjay,
                                                   anjay_ssid_t ssid) {
    _anjay_mock_dm_expect_list_resources(anjay, &FAKE_SERVER, ssid, 0,
                                         SERVER_RESOURCES);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, ssid,
                                        SERV_RES_MUTE_SEND, ANJAY_ID_INVALID,
                                        ANJAY_ERR_INTERNAL, ANJAY_MOCK_DM_NONE);
}

static void assert_mute_send_resource_equals(bool value,
                                             anjay_t *anjay,
                                             anjay_ssid_t ssid) {
    _anjay_mock_dm_expect_list_resources(anjay, &FAKE_SERVER, ssid, 0,
                                         SERVER_RESOURCES);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, ssid,
                                        SERV_RES_MUTE_SEND, ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_BOOL(0, value));
}

static void
test_call_anjay_send(anjay_t *anjay,
                     anjay_ssid_t ssid,
                     const anjay_send_batch_t *data,
                     anjay_send_finished_handler_t *finished_handler,
                     void *finished_handler_data) {
    assert_there_is_server_with_ssid(ssid, anjay);
    assert_mute_send_resource_equals(false, anjay, ssid);

    AVS_LIST(test_finished_handler_arg_t) wrapper_arg =
            AVS_LIST_NEW_ELEMENT(test_finished_handler_arg_t);
    AVS_UNIT_ASSERT_NOT_NULL(wrapper_arg);
    wrapper_arg->real_handler = finished_handler;
    wrapper_arg->real_handler_data = finished_handler_data;
    AVS_LIST_INSERT(&HANDLER_WRAPPER_ARGS, wrapper_arg);

    ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);
    anjay_unlocked->servers->registration_info.lwm2m_version =
            ANJAY_LWM2M_VERSION_1_1;
    ANJAY_MUTEX_UNLOCK(anjay);
    AVS_UNIT_ASSERT_SUCCESS(anjay_send(
            anjay, ssid, data, test_finished_handler_wrapper, wrapper_arg));
    anjay_sched_run(anjay);
}

typedef struct {
    char payload[2048];
    size_t payload_size;
} expected_payload_t;

static expected_payload_t
get_expected_payload_for_batch_with_int_value(anjay_uri_path_t resource_path,
                                              uint16_t resource_value,
                                              double timestamp) {
    AVS_ASSERT(resource_value > UINT8_MAX,
               "this function encodes properly only values greater than 0xFF");
    uint16_t converted_value = avs_convert_be16(resource_value);

    assert(_anjay_uri_path_leaf_is(&resource_path, ANJAY_ID_RID));
    const char *path_str = ANJAY_DEBUG_MAKE_PATH(&resource_path);
    size_t path_str_len = strlen(path_str);
    AVS_UNIT_ASSERT_TRUE(
            path_str_len
            <= 33); // otherwise the CBOR encoding would be different
    char cbor_path_header = (char) (0x60 + path_str_len);

    expected_payload_t result;
    int payload_size;
    int value_offset;
    if (isnan(timestamp)) {
        AVS_UNIT_ASSERT_TRUE(
                (payload_size = avs_simple_snprintf(
                         result.payload, sizeof(result.payload),
                         "\x81\xA2\x21%c%s%c%c%n__", cbor_path_header, path_str,
                         SENML_LABEL_VALUE, CBOR_EXT_LENGTH_2BYTE,
                         &value_offset))
                >= 0);
        memcpy(result.payload + value_offset, &converted_value,
               sizeof(converted_value));
    } else {
        // This payload is valid only for timestamp that must be 8 bytes double
        // and cannot be converted to 4-byte float
        AVS_UNIT_ASSERT_FALSE(((float) timestamp) == timestamp);
        int timestamp_offset;
        uint64_t converted_timestamp = avs_htond(timestamp);
        AVS_UNIT_ASSERT_TRUE(
                (payload_size = avs_simple_snprintf(
                         result.payload, sizeof(result.payload),
                         "\x81\xA3\x21%c%s\x22\xFB%n________%c%c%n__",
                         cbor_path_header, path_str, &timestamp_offset,
                         SENML_LABEL_VALUE, CBOR_EXT_LENGTH_2BYTE,
                         &value_offset))
                >= 0);
        memcpy(result.payload + timestamp_offset, &converted_timestamp,
               sizeof(converted_timestamp));
        memcpy(result.payload + value_offset, &converted_value,
               sizeof(converted_value));
    }
    result.payload_size = (size_t) payload_size;
    return result;
}

static void
test_expect_scheduled_lwm2m_send_request(avs_net_socket_t *mocksock,
                                         uint16_t msg_id,
                                         avs_coap_token_t token,
                                         expected_payload_t expected_payload) {
    DM_TEST_REQUEST_FROM_CLIENT(
            mocksock, CON, POST, ID_TOKEN_RAW(msg_id, token), PATH("dp"),
            CONTENT_FORMAT(SENML_CBOR),
            PAYLOAD_EXTERNAL(expected_payload.payload,
                             expected_payload.payload_size));
}

static void test_handle_lwm2m_send_response(anjay_t *anjay,
                                            avs_net_socket_t *mocksock,
                                            const coap_test_msg_t *msg) {
    size_t old_queue_size = AVS_LIST_SIZE(HANDLER_WRAPPER_ARGS);

    avs_unit_mocksock_input(mocksock, msg->content, msg->length);
    expect_has_buffered_data_check(mocksock, false);
    ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);
    avs_coap_async_handle_incoming_packet(
            _anjay_connection_get(&anjay_unlocked->servers->connections,
                                  ANJAY_CONNECTION_PRIMARY)
                    ->coap_ctx,
            NULL, NULL);
    ANJAY_MUTEX_UNLOCK(anjay);

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(HANDLER_WRAPPER_ARGS),
                          old_queue_size - 1);
}

static void
send_finished_handler_result_validator(anjay_t *anjay,
                                       anjay_ssid_t ssid,
                                       const anjay_send_batch_t *batch,
                                       int result,
                                       void *expected_result) {
    (void) anjay;
    AVS_UNIT_ASSERT_EQUAL(ssid, SSID);
    AVS_UNIT_ASSERT_NOT_NULL(batch);
    AVS_UNIT_ASSERT_EQUAL(result, (int) (intptr_t) expected_result);
}

AVS_UNIT_TEST(anjay_send, success) {
    DM_TEST_INIT;

    anjay_send_batch_t *batch = get_new_batch_with_int_value(URI_PATH, VALUE);
    test_expect_scheduled_lwm2m_send_request(
            mocksocks[0], MSG_ID, nth_token(0),
            get_expected_payload_for_batch_with_int_value(URI_PATH, VALUE,
                                                          NAN));
    test_call_anjay_send(anjay, SSID, batch,
                         send_finished_handler_result_validator,
                         (void *) (intptr_t) ANJAY_SEND_SUCCESS);
    anjay_send_batch_release(&batch);
    test_handle_lwm2m_send_response(anjay, mocksocks[0],
                                    COAP_MSG(ACK, CHANGED,
                                             ID_TOKEN_RAW(MSG_ID, nth_token(0)),
                                             NO_PAYLOAD));

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(anjay_send, empty) {
    DM_TEST_INIT;

    anjay_send_batch_builder_t *builder = anjay_send_batch_builder_new();
    AVS_UNIT_ASSERT_NOT_NULL(builder);
    anjay_send_batch_t *batch = anjay_send_batch_builder_compile(&builder);
    AVS_UNIT_ASSERT_NULL(builder);
    AVS_UNIT_ASSERT_NOT_NULL(batch);
    test_expect_scheduled_lwm2m_send_request(
            mocksocks[0], MSG_ID, nth_token(0),
            (expected_payload_t) { { (char) 0x80 }, 1 });
    test_call_anjay_send(anjay, SSID, batch,
                         send_finished_handler_result_validator,
                         (void *) (intptr_t) ANJAY_SEND_SUCCESS);
    anjay_send_batch_release(&batch);
    test_handle_lwm2m_send_response(anjay, mocksocks[0],
                                    COAP_MSG(ACK, CHANGED,
                                             ID_TOKEN_RAW(MSG_ID, nth_token(0)),
                                             NO_PAYLOAD));

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(anjay_send, error_responses) {
    DM_TEST_INIT;
    anjay_send_batch_t *batch = get_new_batch_with_int_value(URI_PATH, VALUE);

    test_expect_scheduled_lwm2m_send_request(
            mocksocks[0], MSG_ID, nth_token(0),
            get_expected_payload_for_batch_with_int_value(URI_PATH, VALUE,
                                                          NAN));
    test_call_anjay_send(anjay, SSID, batch,
                         send_finished_handler_result_validator,
                         (void *) (intptr_t) ANJAY_ERR_SERVICE_UNAVAILABLE);
    test_handle_lwm2m_send_response(anjay, mocksocks[0],
                                    COAP_MSG(ACK, SERVICE_UNAVAILABLE,
                                             ID_TOKEN_RAW(MSG_ID, nth_token(0)),
                                             NO_PAYLOAD));

    test_expect_scheduled_lwm2m_send_request(
            mocksocks[0], (uint16_t) (MSG_ID + 1), nth_token(1),
            get_expected_payload_for_batch_with_int_value(URI_PATH, VALUE,
                                                          NAN));
    test_call_anjay_send(anjay, SSID, batch,
                         send_finished_handler_result_validator,
                         (void *) (intptr_t) ANJAY_ERR_FORBIDDEN);
    test_handle_lwm2m_send_response(
            anjay, mocksocks[0],
            COAP_MSG(ACK, FORBIDDEN,
                     ID_TOKEN_RAW((uint16_t) (MSG_ID + 1), nth_token(1)),
                     NO_PAYLOAD));

    anjay_send_batch_release(&batch);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(anjay_send, partial_success) {
    DM_TEST_INIT;

    anjay_send_batch_t *batch = get_new_batch_with_int_value(URI_PATH, VALUE);
    test_expect_scheduled_lwm2m_send_request(
            mocksocks[0], MSG_ID, nth_token(0),
            get_expected_payload_for_batch_with_int_value(URI_PATH, VALUE,
                                                          NAN));
    test_call_anjay_send(anjay, SSID, batch,
                         send_finished_handler_result_validator,
                         (void *) (intptr_t) ANJAY_SEND_SUCCESS);
    anjay_send_batch_release(&batch);
    test_handle_lwm2m_send_response(
            anjay, mocksocks[0],
            COAP_MSG(ACK, CHANGED, ID_TOKEN_RAW(MSG_ID, nth_token(0)),
                     BLOCK2(0, 16, "12345678901234567890")));

    DM_TEST_FINISH;
}

static void test_expect_scheduled_lwm2m_send_retransmissions(
        anjay_t *anjay,
        avs_net_socket_t *mocksock,
        uint16_t msg_id,
        avs_coap_token_t token,
        expected_payload_t expected_payload,
        unsigned expected_retransmissions_count) {
    for (unsigned i = 0; i < expected_retransmissions_count; i++) {
        avs_time_duration_t delay;
        AVS_UNIT_ASSERT_SUCCESS(anjay_sched_time_to_next(anjay, &delay));
        _anjay_mock_clock_advance(delay);
        DM_TEST_REQUEST_FROM_CLIENT(
                mocksock, CON, POST, ID_TOKEN_RAW(msg_id, token), PATH("dp"),
                CONTENT_FORMAT(SENML_CBOR),
                PAYLOAD_EXTERNAL(expected_payload.payload,
                                 expected_payload.payload_size));
        anjay_sched_run(anjay);
    }
}

static void test_expect_lwm2m_send_retransmissions_timeout(anjay_t *anjay) {
    size_t old_queue_size = AVS_LIST_SIZE(HANDLER_WRAPPER_ARGS);
    avs_time_duration_t delay;
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_time_to_next(anjay, &delay));
    _anjay_mock_clock_advance(delay);
    anjay_sched_run(anjay);
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(HANDLER_WRAPPER_ARGS),
                          old_queue_size - 1);
}

AVS_UNIT_TEST(anjay_send, implicit_abort) {
    DM_TEST_INIT;

    anjay_send_batch_t *batch = get_new_batch_with_int_value(URI_PATH, VALUE);
    test_expect_scheduled_lwm2m_send_request(
            mocksocks[0], MSG_ID, nth_token(0),
            get_expected_payload_for_batch_with_int_value(URI_PATH, VALUE,
                                                          NAN));
    test_call_anjay_send(anjay, SSID, batch, NULL, NULL);
    anjay_send_batch_release(&batch);
    test_expect_scheduled_lwm2m_send_retransmissions(
            anjay, mocksocks[0], MSG_ID, nth_token(0),
            get_expected_payload_for_batch_with_int_value(URI_PATH, VALUE, NAN),
            AVS_COAP_DEFAULT_UDP_TX_PARAMS.max_retransmit);
    test_expect_lwm2m_send_retransmissions_timeout(anjay);

    DM_TEST_FINISH;
}

static void send_timeout_finished_handler(anjay_t *anjay,
                                          anjay_ssid_t ssid,
                                          const anjay_send_batch_t *batch,
                                          int result,
                                          void *data) {
    (void) anjay;
    AVS_UNIT_ASSERT_EQUAL(ssid, SSID);
    AVS_UNIT_ASSERT_NOT_NULL(batch);
    AVS_UNIT_ASSERT_EQUAL(result, ANJAY_SEND_TIMEOUT);
    AVS_UNIT_ASSERT_NULL(data);
}

AVS_UNIT_TEST(anjay_send, explicit_abort) {
    DM_TEST_INIT;

    anjay_send_batch_t *batch = get_new_batch_with_int_value(URI_PATH, VALUE);
    test_expect_scheduled_lwm2m_send_request(
            mocksocks[0], MSG_ID, nth_token(0),
            get_expected_payload_for_batch_with_int_value(URI_PATH, VALUE,
                                                          NAN));
    test_call_anjay_send(anjay, SSID, batch, send_timeout_finished_handler,
                         NULL);
    anjay_send_batch_release(&batch);
    test_expect_scheduled_lwm2m_send_retransmissions(
            anjay, mocksocks[0], MSG_ID, nth_token(0),
            get_expected_payload_for_batch_with_int_value(URI_PATH, VALUE, NAN),
            AVS_COAP_DEFAULT_UDP_TX_PARAMS.max_retransmit);
    test_expect_lwm2m_send_retransmissions_timeout(anjay);

    DM_TEST_FINISH;
}

static void
send_continue_twice_finished_handler(anjay_t *anjay,
                                     anjay_ssid_t ssid,
                                     const anjay_send_batch_t *batch,
                                     int result,
                                     void *data) {
    unsigned *failure_counter = (unsigned *) data;
    AVS_UNIT_ASSERT_EQUAL(result, ANJAY_SEND_TIMEOUT);
    if (++(*failure_counter) <= 2) {
        test_call_anjay_send(anjay, ssid, batch,
                             send_continue_twice_finished_handler, data);
    }
}

AVS_UNIT_TEST(anjay_send, continue) {
    DM_TEST_INIT;

    const unsigned messages_count_per_attempt =
            1 + AVS_COAP_DEFAULT_UDP_TX_PARAMS.max_retransmit;
    unsigned failure_counter = 0;

    anjay_send_batch_t *batch = get_new_batch_with_int_value(URI_PATH, VALUE);
    expected_payload_t expected_payload =
            get_expected_payload_for_batch_with_int_value(URI_PATH, VALUE, NAN);
    test_expect_scheduled_lwm2m_send_request(mocksocks[0], MSG_ID, nth_token(0),
                                             expected_payload);
    test_call_anjay_send(anjay, SSID, batch,
                         send_continue_twice_finished_handler,
                         &failure_counter);
    anjay_send_batch_release(&batch);

    test_expect_scheduled_lwm2m_send_retransmissions(
            anjay, mocksocks[0], MSG_ID, nth_token(0), expected_payload,
            messages_count_per_attempt - 1);
    test_expect_scheduled_lwm2m_send_retransmissions(
            anjay, mocksocks[0], (uint16_t) (MSG_ID + 1), nth_token(1),
            expected_payload, messages_count_per_attempt);
    test_expect_scheduled_lwm2m_send_retransmissions(
            anjay, mocksocks[0], (uint16_t) (MSG_ID + 2), nth_token(2),
            expected_payload, messages_count_per_attempt);
    test_expect_lwm2m_send_retransmissions_timeout(anjay);

    AVS_UNIT_ASSERT_EQUAL(failure_counter, 3);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(anjay_send, resource_from_dm) {
    DM_TEST_INIT;

    const double absolute_time = SENML_TIME_SECONDS_THRESHOLD + 12345.6789e-9;
    _anjay_mock_clock_reset(
            avs_time_monotonic_from_fscalar(absolute_time, AVS_TIME_S));

    anjay_send_batch_t *batch =
            get_new_batch_with_int_value_from_dm(anjay, &OBJ, URI_PATH, VALUE);
    test_expect_scheduled_lwm2m_send_request(
            mocksocks[0], MSG_ID, nth_token(0),
            get_expected_payload_for_batch_with_int_value(URI_PATH, VALUE,
                                                          absolute_time));
    test_call_anjay_send(anjay, SSID, batch,
                         send_finished_handler_result_validator, NULL);
    anjay_send_batch_release(&batch);
    test_handle_lwm2m_send_response(anjay, mocksocks[0],
                                    COAP_MSG(ACK, CHANGED,
                                             ID_TOKEN_RAW(MSG_ID, nth_token(0)),
                                             NO_PAYLOAD));

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(anjay_send, unreachable_server) {
    DM_TEST_INIT_WITHOUT_SERVER;

    anjay_send_batch_t *batch = get_new_batch_with_int_value(URI_PATH, VALUE);
    assert_there_is_server_with_ssid(SSID, anjay);
    assert_mute_send_resource_equals(false, anjay, SSID);
    const anjay_send_result_t result =
            anjay_send(anjay, SSID, batch, NULL, NULL);
    anjay_send_batch_release(&batch);

    AVS_UNIT_ASSERT_FAILED(result);
    AVS_UNIT_ASSERT_EQUAL(result, ANJAY_SEND_ERR_OFFLINE);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(anjay_send, offline_mode) {
    DM_TEST_INIT;
    ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);
    anjay_unlocked->servers->registration_info.lwm2m_version =
            ANJAY_LWM2M_VERSION_1_1;

    avs_unit_mocksock_expect_shutdown(mocksocks[0]);
    avs_net_socket_shutdown(mocksocks[0]);
    avs_net_socket_close(mocksocks[0]);
    // Mark UDP transport as offline - otherwise the server entry would be
    // considered suspended for queue mode and the Send would be deferred.
    anjay_unlocked->online_transports.udp = false;
    ANJAY_MUTEX_UNLOCK(anjay);

    anjay_send_batch_t *batch = get_new_batch_with_int_value(URI_PATH, VALUE);
    assert_there_is_server_with_ssid(SSID, anjay);
    assert_mute_send_resource_equals(false, anjay, SSID);
    const anjay_send_result_t result =
            anjay_send(anjay, SSID, batch, NULL, NULL);
    anjay_send_batch_release(&batch);

    AVS_UNIT_ASSERT_FAILED(result);
    AVS_UNIT_ASSERT_EQUAL(result, ANJAY_SEND_ERR_OFFLINE);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(anjay_send, ssid_any) {
    DM_TEST_INIT;
    ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);
    anjay_unlocked->servers->registration_info.lwm2m_version =
            ANJAY_LWM2M_VERSION_1_1;
    ANJAY_MUTEX_UNLOCK(anjay);

    anjay_send_batch_t *batch = get_new_batch_with_int_value(URI_PATH, VALUE);
    const anjay_send_result_t result =
            anjay_send(anjay, ANJAY_SSID_ANY, batch, NULL, NULL);
    anjay_send_batch_release(&batch);

    AVS_UNIT_ASSERT_FAILED(result);
    AVS_UNIT_ASSERT_EQUAL(result, ANJAY_SEND_ERR_SSID);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(anjay_send, ssid_bootstrap) {
    DM_TEST_INIT;
    ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);
    anjay_unlocked->servers->registration_info.lwm2m_version =
            ANJAY_LWM2M_VERSION_1_1;
    ANJAY_MUTEX_UNLOCK(anjay);

    anjay_send_batch_t *batch = get_new_batch_with_int_value(URI_PATH, VALUE);
    const anjay_send_result_t result =
            anjay_send(anjay, ANJAY_SSID_BOOTSTRAP, batch, NULL, NULL);
    anjay_send_batch_release(&batch);

    AVS_UNIT_ASSERT_FAILED(result);
    AVS_UNIT_ASSERT_EQUAL(result, ANJAY_SEND_ERR_SSID);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(anjay_send, not_existing_ssid) {
    DM_TEST_INIT;
    ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);
    anjay_unlocked->servers->registration_info.lwm2m_version =
            ANJAY_LWM2M_VERSION_1_1;
    ANJAY_MUTEX_UNLOCK(anjay);

    anjay_send_batch_t *batch = get_new_batch_with_int_value(URI_PATH, VALUE);
    assert_there_is_not_any_server(anjay);
    const anjay_send_result_t result =
            anjay_send(anjay, 1234, batch, NULL, NULL);
    anjay_send_batch_release(&batch);

    AVS_UNIT_ASSERT_FAILED(result);
    AVS_UNIT_ASSERT_EQUAL(result, ANJAY_SEND_ERR_SSID);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(anjay_send, muted) {
    DM_TEST_INIT;
    ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);
    anjay_unlocked->servers->registration_info.lwm2m_version =
            ANJAY_LWM2M_VERSION_1_1;
    ANJAY_MUTEX_UNLOCK(anjay);

    anjay_send_batch_t *batch = get_new_batch_with_int_value(URI_PATH, VALUE);
    assert_there_is_server_with_ssid(SSID, anjay);
    assert_mute_send_resource_equals(true, anjay, SSID);
    const anjay_send_result_t result =
            anjay_send(anjay, SSID, batch, NULL, NULL);
    anjay_send_batch_release(&batch);

    AVS_UNIT_ASSERT_FAILED(result);
    AVS_UNIT_ASSERT_EQUAL(result, ANJAY_SEND_ERR_MUTED);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(anjay_send, assert_mute_send_resource_read_failure) {
    DM_TEST_INIT;
    ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);
    anjay_unlocked->servers->registration_info.lwm2m_version =
            ANJAY_LWM2M_VERSION_1_1;
    ANJAY_MUTEX_UNLOCK(anjay);

    anjay_send_batch_t *batch = get_new_batch_with_int_value(URI_PATH, VALUE);
    assert_there_is_server_with_ssid(SSID, anjay);
    assert_mute_send_resource_read_failure(anjay, SSID);
    const anjay_send_result_t result =
            anjay_send(anjay, SSID, batch, NULL, NULL);
    anjay_send_batch_release(&batch);

    AVS_UNIT_ASSERT_FAILED(result);
    AVS_UNIT_ASSERT_EQUAL(result, ANJAY_SEND_ERR_MUTED);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(anjay_send, add_multiple_successful) {
    DM_TEST_INIT;
    anjay_send_batch_builder_t *builder = anjay_send_batch_builder_new();
    AVS_UNIT_ASSERT_NOT_NULL(builder);

    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 1, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 1, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 1, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT },
                    { 2, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 1, 1, ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, 23));

    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 1, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 1, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 1, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT },
                    { 2, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 1, 2, ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, 45));

    anjay_send_resource_path_t paths[] = { { 42, 1, 1 }, { 42, 1, 2 } };

    AVS_UNIT_ASSERT_SUCCESS(anjay_send_batch_data_add_current_multiple(
            builder, anjay, paths, AVS_ARRAY_SIZE(paths)));
    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(((anjay_batch_builder_t *) builder)->list), 2);
    anjay_send_batch_builder_cleanup(&builder);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(anjay_send, add_multiple_single_resource_fail) {
    DM_TEST_INIT;
    anjay_send_batch_builder_t *builder = anjay_send_batch_builder_new();
    AVS_UNIT_ASSERT_NOT_NULL(builder);

    AVS_LIST(anjay_batch_entry_t) *initial_append_ptr =
            ((anjay_batch_builder_t *) builder)->append_ptr;

    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 1, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 1, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 1, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT },
                    { 2, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 1, 1, ANJAY_ID_INVALID, -1,
                                        ANJAY_MOCK_DM_INT(0, 23));

    anjay_send_resource_path_t paths[] = { { 42, 1, 1 } };

    AVS_UNIT_ASSERT_FAILED(anjay_send_batch_data_add_current_multiple(
            builder, anjay, paths, AVS_ARRAY_SIZE(paths)));
    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(((anjay_batch_builder_t *) builder)->list), 0);
    AVS_UNIT_ASSERT_TRUE(initial_append_ptr
                         == ((anjay_batch_builder_t *) builder)->append_ptr);
    AVS_UNIT_ASSERT_NULL(*((anjay_batch_builder_t *) builder)->append_ptr);
    anjay_send_batch_builder_cleanup(&builder);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(anjay_send, add_multiple_twice_with_fail) {
    DM_TEST_INIT;
    anjay_send_batch_builder_t *builder = anjay_send_batch_builder_new();
    AVS_UNIT_ASSERT_NOT_NULL(builder);

    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 1, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 1, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 1, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT },
                    { 2, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 1, 1, ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, 23));

    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 1, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 1, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 1, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT },
                    { 2, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 1, 2, ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, 45));

    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 1, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 1, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 1, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT },
                    { 2, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 1, 1, ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, 23));

    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 1, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 1, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 1, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT },
                    { 2, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 1, 2, ANJAY_ID_INVALID, -1,
                                        ANJAY_MOCK_DM_INT(0, 45));

    anjay_send_resource_path_t paths[] = { { 42, 1, 1 }, { 42, 1, 2 } };

    AVS_UNIT_ASSERT_SUCCESS(anjay_send_batch_data_add_current_multiple(
            builder, anjay, paths, AVS_ARRAY_SIZE(paths)));

    AVS_LIST(anjay_batch_entry_t) *pre_fail_append_ptr =
            ((anjay_batch_builder_t *) builder)->append_ptr;

    AVS_UNIT_ASSERT_FAILED(anjay_send_batch_data_add_current_multiple(
            builder, anjay, paths, AVS_ARRAY_SIZE(paths)));
    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(((anjay_batch_builder_t *) builder)->list), 2);
    AVS_UNIT_ASSERT_TRUE(pre_fail_append_ptr
                         == ((anjay_batch_builder_t *) builder)->append_ptr);
    AVS_UNIT_ASSERT_NULL(*((anjay_batch_builder_t *) builder)->append_ptr);
    anjay_send_batch_builder_cleanup(&builder);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(anjay_send, add_multiple_ignore_not_found) {
    DM_TEST_INIT;
    anjay_send_batch_builder_t *builder = anjay_send_batch_builder_new();
    AVS_UNIT_ASSERT_NOT_NULL(builder);

    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 1, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 1, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 1, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT },
                    { 2, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 1, 1, ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, 23));

    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 1, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 1, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 1, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT },
                    { 2, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 1, 2, ANJAY_ID_INVALID,
                                        ANJAY_ERR_NOT_FOUND,
                                        ANJAY_MOCK_DM_INT(0, 45));

    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 1, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 1, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { 1, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT },
                    { 2, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT },
                    ANJAY_MOCK_DM_RES_END });

    anjay_send_resource_path_t paths[] = { { 42, 1, 1 },
                                           { 42, 1, 2 },
                                           { 42, 1, 3 } };

    AVS_UNIT_ASSERT_SUCCESS(
            anjay_send_batch_data_add_current_multiple_ignore_not_found(
                    builder, anjay, paths, AVS_ARRAY_SIZE(paths)));
    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(((anjay_batch_builder_t *) builder)->list), 1);

    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 1, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 1, 0,
            (const anjay_mock_dm_res_entry_t[]) { { 1, ANJAY_DM_RES_R,
                                                    ANJAY_DM_RES_PRESENT },
                                                  ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 1, 1, ANJAY_ID_INVALID, -1,
                                        ANJAY_MOCK_DM_INT(0, 45));

    AVS_UNIT_ASSERT_FAILED(
            anjay_send_batch_data_add_current_multiple_ignore_not_found(
                    builder, anjay, paths, 1));
    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(((anjay_batch_builder_t *) builder)->list), 1);

    // This should not be ignored.
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 1, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 1, 0,
            (const anjay_mock_dm_res_entry_t[]) { { 1, ANJAY_DM_RES_R,
                                                    ANJAY_DM_RES_PRESENT },
                                                  ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 1, 1, ANJAY_ID_INVALID, -1,
                                        ANJAY_MOCK_DM_INT(0, 45));
    AVS_UNIT_ASSERT_FAILED(
            anjay_send_batch_data_add_current_multiple_ignore_not_found(
                    builder, anjay, paths, 1));
    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(((anjay_batch_builder_t *) builder)->list), 1);

    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 1, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &OBJ, 1, 0,
            (const anjay_mock_dm_res_entry_t[]) { { 1, ANJAY_DM_RES_R,
                                                    ANJAY_DM_RES_PRESENT },
                                                  ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 1, 1, ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, 45));
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_send_batch_data_add_current(builder, anjay, 42, 1, 1));
    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(((anjay_batch_builder_t *) builder)->list), 2);

    anjay_send_batch_builder_cleanup(&builder);
    DM_TEST_FINISH;
}
