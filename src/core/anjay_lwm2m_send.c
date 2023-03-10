/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#ifdef ANJAY_WITH_LWM2M11

#    include <inttypes.h>

#    include <avsystem/commons/avs_stream_membuf.h>
#    include <avsystem/commons/avs_utils.h>

#    include <avsystem/coap/async_client.h>
#    include <avsystem/coap/code.h>

#    include <anjay/lwm2m_send.h>

#    include "anjay_access_utils_private.h"
#    include "anjay_core.h"
#    include "anjay_io_core.h"
#    include "anjay_lwm2m_send.h"
#    include "anjay_servers_utils.h"
#    include "coap/anjay_content_format.h"
#    include "dm/anjay_query.h"
#    include "io/anjay_batch_builder.h"

#    define ANJAY_LWM2M_SEND_SOURCE

#    include "anjay_servers_reload.h"

VISIBILITY_SOURCE_BEGIN

#    define send_log(...) _anjay_log(anjay_send, __VA_ARGS__)

#    ifdef ANJAY_WITH_SEND

// Path for LwM2M Send requests defined by spec
#        define ANJAY_SEND_URI_PATH "dp"

static inline anjay_batch_builder_t *
cast_to_builder(anjay_send_batch_builder_t *builder) {
    return (anjay_batch_builder_t *) builder;
}

static inline anjay_send_batch_builder_t *
cast_to_send_builder(anjay_batch_builder_t *builder) {
    return (anjay_send_batch_builder_t *) builder;
}

static inline anjay_batch_t *cast_to_batch(anjay_send_batch_t *batch) {
    return (anjay_batch_t *) batch;
}

static inline anjay_send_batch_t *cast_to_send_batch(anjay_batch_t *batch) {
    return (anjay_send_batch_t *) batch;
}

static inline const anjay_batch_t *
cast_to_const_batch(const anjay_send_batch_t *batch) {
    return (const anjay_batch_t *) batch;
}

static inline const anjay_send_batch_t *
cast_to_const_send_batch(const anjay_batch_t *batch) {
    return (const anjay_send_batch_t *) batch;
}

typedef struct {
    avs_coap_exchange_id_t id;
    avs_stream_t *memstream;
    anjay_unlocked_output_ctx_t *out_ctx;
    size_t expected_offset;
    avs_time_real_t serialization_time;
    const anjay_batch_data_output_state_t *output_state;
} exchange_status_t;

struct anjay_send_entry {
    anjay_unlocked_t *anjay;
    anjay_send_finished_handler_t *finished_handler;
    void *finished_handler_data;
    anjay_ssid_t target_ssid;
    bool deferrable;
    anjay_batch_t *payload_batch;
    exchange_status_t exchange_status;
};

static void clear_exchange_status(exchange_status_t *status) {
    assert(!avs_coap_exchange_id_valid(status->id));
    _anjay_output_ctx_destroy(&status->out_ctx);
    avs_stream_cleanup(&status->memstream);
    status->output_state = NULL;
}

static void delete_send_entry(AVS_LIST(anjay_send_entry_t) *entry) {
    _anjay_batch_release(&(*entry)->payload_batch);
    clear_exchange_status(&(*entry)->exchange_status);
    AVS_LIST_DELETE(entry);
}

static avs_error_t setup_send_options(avs_coap_options_t *options,
                                      const anjay_url_t *server_uri,
                                      uint16_t content_format) {
    avs_error_t err;
    (void) (avs_is_err((err = _anjay_coap_add_string_options(
                                options, server_uri->uri_path,
                                AVS_COAP_OPTION_URI_PATH)))
            || avs_is_err((err = avs_coap_options_add_string(
                                   options, AVS_COAP_OPTION_URI_PATH,
                                   ANJAY_SEND_URI_PATH)))
            || avs_is_err((err = avs_coap_options_set_content_format(
                                   options, content_format)))
            || avs_is_err((err = _anjay_coap_add_string_options(
                                   options, server_uri->uri_query,
                                   AVS_COAP_OPTION_URI_QUERY))));
    return err;
}

static int request_payload_writer(size_t payload_offset,
                                  void *payload_buf,
                                  size_t payload_buf_size,
                                  size_t *out_payload_chunk_size,
                                  void *entry_) {
    anjay_send_entry_t *entry = (anjay_send_entry_t *) entry_;
    if (payload_offset != entry->exchange_status.expected_offset) {
        send_log(DEBUG,
                 _("Server requested unexpected chunk of payload (expected "
                   "offset ") "%u" _(", got ") "%u" _(")"),
                 (unsigned) entry->exchange_status.expected_offset,
                 (unsigned) payload_offset);
        return -1;
    }

    char *write_ptr = (char *) payload_buf;
    const char *end_ptr = write_ptr + payload_buf_size;
    while (true) {
        size_t bytes_read;
        if (avs_is_err(avs_stream_read(entry->exchange_status.memstream,
                                       &bytes_read, NULL, write_ptr,
                                       (size_t) (end_ptr - write_ptr)))) {
            return -1;
        }
        write_ptr += bytes_read;

        // NOTE: (output_state == NULL && out_ctx != NULL) means start of
        // iteration; out_ctx is cleaned up at the end of iteration, so
        // (output_state == NULL && out_ctx == NULL) means end of iteration
        if (write_ptr >= end_ptr || !entry->exchange_status.out_ctx) {
            break;
        }
        int result = _anjay_batch_data_output_entry(
                entry->anjay, entry->payload_batch, entry->target_ssid,
                entry->exchange_status.serialization_time,
                &entry->exchange_status.output_state,
                entry->exchange_status.out_ctx);
        if (!result && !entry->exchange_status.output_state) {
            result = _anjay_output_ctx_destroy_and_process_result(
                    &entry->exchange_status.out_ctx, result);
        }
        if (result) {
            return result;
        }
    }
    *out_payload_chunk_size = (size_t) (write_ptr - (char *) payload_buf);
    entry->exchange_status.expected_offset += *out_payload_chunk_size;
    return 0;
}

static void call_finished_handler(anjay_send_entry_t *entry, int result) {
    if (!entry->finished_handler) {
        return;
    }
    anjay_send_finished_handler_t *handler = entry->finished_handler;
    void *handler_data = entry->finished_handler_data;
    anjay_unlocked_t *anjay = entry->anjay;
    anjay_ssid_t target_ssid = entry->target_ssid;
    const anjay_send_batch_t *batch =
            cast_to_const_send_batch(entry->payload_batch);
    // Prevent finished_handler from being called again
    entry->finished_handler = NULL;

    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    handler(anjay_locked, target_ssid, batch, result, handler_data);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
}

static void response_handler(avs_coap_ctx_t *ctx,
                             avs_coap_exchange_id_t exchange_id,
                             avs_coap_client_request_state_t state,
                             const avs_coap_client_async_response_t *response,
                             avs_error_t err,
                             void *entry_) {
    (void) ctx;
    (void) exchange_id;
    (void) err;
    anjay_send_entry_t *entry = (anjay_send_entry_t *) entry_;
    assert(entry);
    assert(avs_coap_exchange_id_equal(exchange_id, entry->exchange_status.id));
    if (entry->finished_handler) {
        static const int STATE_TO_RESULT[] = {
            [AVS_COAP_CLIENT_REQUEST_OK] = ANJAY_SEND_SUCCESS,
            [AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT] = ANJAY_SEND_SUCCESS,
            [AVS_COAP_CLIENT_REQUEST_FAIL] = ANJAY_SEND_TIMEOUT,
            [AVS_COAP_CLIENT_REQUEST_CANCEL] = ANJAY_SEND_ABORT
        };
        assert(state >= 0 && state < AVS_ARRAY_SIZE(STATE_TO_RESULT));
        int result = STATE_TO_RESULT[state];
        if (result == ANJAY_SEND_SUCCESS) {
            if (response->header.code != AVS_COAP_CODE_CHANGED) {
                result = -response->header.code;
            } else if (response->payload_size) {
                send_log(WARNING,
                         _("Unexpected payload received in response to Send"));
            }
        }
        call_finished_handler(entry, result);
    }
    if (state == AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT) {
        // We don't want/need to read the rest of the content, so we cancel the
        // exchange. Note that this will call this handler again with state set
        // to AVS_COAP_CLIENT_REQUEST_CANCEL.
        avs_coap_exchange_cancel(ctx, exchange_id);
    } else {
        entry->exchange_status.id = AVS_COAP_EXCHANGE_ID_INVALID;
        AVS_LIST(anjay_send_entry_t) *entry_ptr =
                (AVS_LIST(anjay_send_entry_t) *) AVS_LIST_FIND_PTR(
                        &entry->anjay->sender.entries, entry);
        assert(entry_ptr);
        assert(*entry_ptr == entry);
        delete_send_entry(entry_ptr);
    }
}

static AVS_LIST(anjay_send_entry_t) *
create_exchange(anjay_unlocked_t *anjay,
                anjay_ssid_t target_ssid,
                bool deferrable,
                anjay_send_finished_handler_t *finished_handler,
                void *finished_handler_data,
                const anjay_send_batch_t *batch) {
    anjay_batch_t *payload_batch =
            _anjay_batch_acquire(cast_to_const_batch(batch));
    if (!payload_batch) {
        send_log(ERROR, _("could not acquire batch"));
    }

    AVS_LIST(anjay_send_entry_t) entry =
            AVS_LIST_NEW_ELEMENT(anjay_send_entry_t);
    if (!entry) {
        send_log(ERROR, _("out of memory"));
        return NULL;
    }
    entry->anjay = anjay;
    entry->finished_handler = finished_handler;
    entry->finished_handler_data = finished_handler_data;
    entry->target_ssid = target_ssid;
    entry->deferrable = deferrable;
    entry->payload_batch = payload_batch;

    AVS_LIST(anjay_send_entry_t) *insert_ptr = &anjay->sender.entries;
    while (*insert_ptr && (*insert_ptr)->target_ssid < target_ssid) {
        AVS_LIST_ADVANCE_PTR(&insert_ptr);
    }
    AVS_LIST_INSERT(insert_ptr, entry);

    return insert_ptr;
}

static avs_error_t start_send_exchange(anjay_send_entry_t *entry,
                                       anjay_connection_ref_t connection) {
    assert(!avs_coap_exchange_id_valid(entry->exchange_status.id));
    assert(!entry->exchange_status.memstream);
    assert(!entry->exchange_status.out_ctx);
    assert(!entry->exchange_status.output_state);

    assert(connection.server);
    assert(_anjay_server_ssid(connection.server) == entry->target_ssid);

    if (!_anjay_connection_get_online_socket(connection)) {
        if (_anjay_schedule_refresh_server(connection.server,
                                           AVS_TIME_DURATION_ZERO)) {
            return avs_errno(AVS_ENOMEM);
        } else {
            // once the connection is up, _anjay_send_sched_retry_deferred()
            // will be called; we're done here
            return AVS_OK;
        }
    }

    avs_coap_ctx_t *coap = _anjay_connection_get_coap(connection);
    if (!coap) {
        return avs_errno(AVS_EBADF);
    }

    uint16_t content_format =
#        if defined(ANJAY_DEFAULT_SEND_FORMAT) \
                && ANJAY_DEFAULT_SEND_FORMAT != AVS_COAP_FORMAT_NONE
            ANJAY_DEFAULT_SEND_FORMAT
#        else  // defined(ANJAY_DEFAULT_SEND_FORMAT)
               // && ANJAY_DEFAULT_SEND_FORMAT != AVS_COAP_FORMAT_NONE
            _anjay_default_hierarchical_format(
                    _anjay_server_registration_info(connection.server)
                            ->lwm2m_version)
#        endif // defined(ANJAY_DEFAULT_SEND_FORMAT)
               // && ANJAY_DEFAULT_SEND_FORMAT != AVS_COAP_FORMAT_NONE
            ;

    const anjay_url_t *server_uri = _anjay_connection_uri(connection);
    assert(server_uri);

    avs_coap_request_header_t request = {
        .code = AVS_COAP_CODE_POST
    };

    anjay_uri_path_t base_path = MAKE_ROOT_PATH();
    _anjay_batch_update_common_path_prefix(&(const anjay_uri_path_t *) { NULL },
                                           &base_path, entry->payload_batch);

    avs_error_t err;
    if (avs_is_err((err = avs_coap_options_dynamic_init(&request.options)))
            || avs_is_err(
                       (err = setup_send_options(&request.options, server_uri,
                                                 content_format)))) {
        goto finish;
    }

    if (!(entry->exchange_status.memstream = avs_stream_membuf_create())
            || (_anjay_output_dynamic_send_construct(
                       &entry->exchange_status.out_ctx,
                       entry->exchange_status.memstream, &base_path,
                       content_format))) {
        send_log(ERROR, _("could not create output context"));
        err = avs_errno(AVS_ENOMEM);
        goto finish;
    }
    entry->exchange_status.expected_offset = 0;
    entry->exchange_status.serialization_time = avs_time_real_now();

    err = avs_coap_client_send_async_request(coap, &entry->exchange_status.id,
                                             &request, request_payload_writer,
                                             entry, response_handler, entry);
    _anjay_connection_schedule_queue_mode_close(connection);
finish:
    avs_coap_options_cleanup(&request.options);
    if (avs_is_err(err)) {
        clear_exchange_status(&entry->exchange_status);
#        ifdef ANJAY_WITH_COMMUNICATION_TIMESTAMP_API
    } else {
        _anjay_server_set_last_communication_time(connection.server);
#        endif // ANJAY_WITH_COMMUNICATION_TIMESTAMP_API
    }
    return err;
}

static bool is_deferrable_condition(anjay_send_result_t condition) {
    return condition == ANJAY_SEND_ERR_OFFLINE
           || condition == ANJAY_SEND_ERR_BOOTSTRAP;
}

static anjay_send_result_t
check_send_possibility(anjay_unlocked_t *anjay,
                       anjay_ssid_t ssid,
                       anjay_connection_ref_t *out_ref) {
    anjay_iid_t server_iid;
    if (_anjay_find_server_iid(anjay, ssid, &server_iid)) {
        return ANJAY_SEND_ERR_SSID;
    }

    bool is_lwm2m_send_muted;
    if (_anjay_dm_read_resource_bool(
                anjay,
                &MAKE_RESOURCE_PATH(ANJAY_DM_OID_SERVER,
                                    server_iid,
                                    ANJAY_DM_RID_SERVER_MUTE_SEND),
                &is_lwm2m_send_muted)
            || is_lwm2m_send_muted) {
        return ANJAY_SEND_ERR_MUTED;
    }

    if (_anjay_bootstrap_in_progress(anjay)) {
        send_log(DEBUG, _("Cannot perform LwM2M Send during bootstrap"));
        return ANJAY_SEND_ERR_BOOTSTRAP;
    }

    out_ref->conn_type = ANJAY_CONNECTION_PRIMARY;
    out_ref->server = _anjay_servers_find_active(anjay, ssid);
    if (!out_ref->server
            || !_anjay_connection_ready_for_outgoing_message(*out_ref)
            || !_anjay_socket_transport_is_online(
                       anjay, _anjay_connection_transport(*out_ref))) {
        send_log(DEBUG,
                 _("SSID ") "%u" _(" does not refer to a server connection "
                                   "that is currently online"),
                 ssid);
        return ANJAY_SEND_ERR_OFFLINE;
    } else if (_anjay_server_registration_info(out_ref->server)->lwm2m_version
               < ANJAY_LWM2M_VERSION_1_1) {
        send_log(
                DEBUG,
                _("Server SSID ") "%u" _(
                        " is registered with LwM2M version ") "%s" _(", which "
                                                                     "does not "
                                                                     "support "
                                                                     "Send"),
                ssid,
                _anjay_lwm2m_version_as_string(
                        _anjay_server_registration_info(out_ref->server)
                                ->lwm2m_version));
        return ANJAY_SEND_ERR_PROTOCOL;
    }

    return ANJAY_SEND_OK;
}

static anjay_send_result_t
send_impl(anjay_unlocked_t *anjay,
          anjay_ssid_t ssid,
          bool deferrable,
          const anjay_send_batch_t *data,
          anjay_send_finished_handler_t *finished_handler,
          void *finished_handler_data) {
    anjay_connection_ref_t ref = {
        .server = NULL
    };
    anjay_send_result_t result = check_send_possibility(anjay, ssid, &ref);
    bool should_defer = (deferrable && is_deferrable_condition(result));
    if (result != ANJAY_SEND_OK && !should_defer) {
        return result;
    }

    AVS_LIST(anjay_send_entry_t) *entry_ptr =
            create_exchange(anjay, ssid, deferrable, finished_handler,
                            finished_handler_data, data);
    if (!entry_ptr || !*entry_ptr) {
        return ANJAY_SEND_ERR_INTERNAL;
    }

    if (!should_defer) {
        assert(ref.server);
        if (avs_is_err(start_send_exchange(*entry_ptr, ref))) {
            delete_send_entry(entry_ptr);
            return ANJAY_SEND_ERR_INTERNAL;
        }
    }
    return ANJAY_SEND_OK;
}

anjay_send_result_t
_anjay_send_deferrable_unlocked(anjay_unlocked_t *anjay,
                                anjay_ssid_t ssid,
                                const anjay_send_batch_t *data,
                                anjay_send_finished_handler_t *finished_handler,
                                void *finished_handler_data) {
    return send_impl(anjay, ssid, true, data, finished_handler,
                     finished_handler_data);
}

anjay_send_result_t
anjay_send_deferrable(anjay_t *anjay_locked,
                      anjay_ssid_t ssid,
                      const anjay_send_batch_t *data,
                      anjay_send_finished_handler_t *finished_handler,
                      void *finished_handler_data) {
    anjay_send_result_t result = ANJAY_SEND_ERR_INTERNAL;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    result =
            _anjay_send_deferrable_unlocked(anjay, ssid, data, finished_handler,
                                            finished_handler_data);
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

anjay_send_result_t anjay_send(anjay_t *anjay_locked,
                               anjay_ssid_t ssid,
                               const anjay_send_batch_t *data,
                               anjay_send_finished_handler_t *finished_handler,
                               void *finished_handler_data) {
    anjay_send_result_t result = ANJAY_SEND_ERR_INTERNAL;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    result = send_impl(anjay, ssid, false, data, finished_handler,
                       finished_handler_data);
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

anjay_send_batch_builder_t *anjay_send_batch_builder_new(void) {
    return cast_to_send_builder(_anjay_batch_builder_new());
}

void anjay_send_batch_builder_cleanup(
        anjay_send_batch_builder_t **builder_ptr) {
    anjay_batch_builder_t *builder = cast_to_builder(*builder_ptr);
    _anjay_batch_builder_cleanup(&builder);
    assert(!builder);
    *builder_ptr = NULL;
}

int anjay_send_batch_add_int(anjay_send_batch_builder_t *builder,
                             anjay_oid_t oid,
                             anjay_iid_t iid,
                             anjay_rid_t rid,
                             anjay_riid_t riid,
                             avs_time_real_t timestamp,
                             int64_t value) {
    return _anjay_batch_add_int(cast_to_builder(builder),
                                &MAKE_RESOURCE_INSTANCE_PATH(oid, iid, rid,
                                                             riid),
                                timestamp, value);
}

int anjay_send_batch_add_uint(anjay_send_batch_builder_t *builder,
                              anjay_oid_t oid,
                              anjay_iid_t iid,
                              anjay_rid_t rid,
                              anjay_riid_t riid,
                              avs_time_real_t timestamp,
                              uint64_t value) {
    return _anjay_batch_add_uint(cast_to_builder(builder),
                                 &MAKE_RESOURCE_INSTANCE_PATH(oid, iid, rid,
                                                              riid),
                                 timestamp, value);
}

int anjay_send_batch_add_double(anjay_send_batch_builder_t *builder,
                                anjay_oid_t oid,
                                anjay_iid_t iid,
                                anjay_rid_t rid,
                                anjay_riid_t riid,
                                avs_time_real_t timestamp,
                                double value) {
    return _anjay_batch_add_double(cast_to_builder(builder),
                                   &MAKE_RESOURCE_INSTANCE_PATH(oid, iid, rid,
                                                                riid),
                                   timestamp, value);
}

int anjay_send_batch_add_bool(anjay_send_batch_builder_t *builder,
                              anjay_oid_t oid,
                              anjay_iid_t iid,
                              anjay_rid_t rid,
                              anjay_riid_t riid,
                              avs_time_real_t timestamp,
                              bool value) {
    return _anjay_batch_add_bool(cast_to_builder(builder),
                                 &MAKE_RESOURCE_INSTANCE_PATH(oid, iid, rid,
                                                              riid),
                                 timestamp, value);
}

int anjay_send_batch_add_string(anjay_send_batch_builder_t *builder,
                                anjay_oid_t oid,
                                anjay_iid_t iid,
                                anjay_rid_t rid,
                                anjay_riid_t riid,
                                avs_time_real_t timestamp,
                                const char *str) {
    return _anjay_batch_add_string(
            cast_to_builder(builder),
            &MAKE_RESOURCE_INSTANCE_PATH(oid, iid, rid, riid), timestamp, str);
}

int anjay_send_batch_add_bytes(anjay_send_batch_builder_t *builder,
                               anjay_oid_t oid,
                               anjay_iid_t iid,
                               anjay_rid_t rid,
                               anjay_riid_t riid,
                               avs_time_real_t timestamp,
                               const void *data,
                               size_t length) {
    return _anjay_batch_add_bytes(cast_to_builder(builder),
                                  &MAKE_RESOURCE_INSTANCE_PATH(oid, iid, rid,
                                                               riid),
                                  timestamp, data, length);
}

int anjay_send_batch_add_objlnk(anjay_send_batch_builder_t *builder,
                                anjay_oid_t oid,
                                anjay_iid_t iid,
                                anjay_rid_t rid,
                                anjay_riid_t riid,
                                avs_time_real_t timestamp,
                                anjay_oid_t objlnk_oid,
                                anjay_iid_t objlnk_iid) {
    return _anjay_batch_add_objlnk(cast_to_builder(builder),
                                   &MAKE_RESOURCE_INSTANCE_PATH(oid, iid, rid,
                                                                riid),
                                   timestamp, objlnk_oid, objlnk_iid);
}

static int
batch_data_add_current_impl(anjay_send_batch_builder_t *builder,
                            anjay_unlocked_t *anjay,
                            anjay_oid_t oid,
                            anjay_iid_t iid,
                            anjay_rid_t rid,
                            const avs_time_real_t *forced_timestamp) {
    assert(builder);
    assert(anjay);

    if (iid == ANJAY_ID_INVALID || rid == ANJAY_ID_INVALID) {
        return -1;
    }

    const anjay_dm_installed_object_t *obj =
            _anjay_dm_find_object_by_oid(anjay, oid);
    if (!obj) {
        send_log(ERROR, _("unregistered Object ID: ") "%u", oid);
        return ANJAY_ERR_NOT_FOUND;
    }
    anjay_dm_path_info_t path_info;
    int result =
            _anjay_dm_path_info(anjay, obj, &MAKE_RESOURCE_PATH(oid, iid, rid),
                                &path_info);
    if (result) {
        return result;
    }
    return _anjay_dm_read_into_batch(cast_to_builder(builder), anjay, obj,
                                     &path_info, ANJAY_SSID_BOOTSTRAP,
                                     forced_timestamp);
}

int _anjay_send_batch_data_add_current_unlocked(
        anjay_send_batch_builder_t *builder,
        anjay_unlocked_t *anjay,
        anjay_oid_t oid,
        anjay_iid_t iid,
        anjay_rid_t rid) {
    return batch_data_add_current_impl(builder, anjay, oid, iid, rid, NULL);
}

int anjay_send_batch_data_add_current(anjay_send_batch_builder_t *builder,
                                      anjay_t *anjay_locked,
                                      anjay_oid_t oid,
                                      anjay_iid_t iid,
                                      anjay_rid_t rid) {
    int result = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    result = _anjay_send_batch_data_add_current_unlocked(builder, anjay, oid,
                                                         iid, rid);
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

int _anjay_send_batch_data_add_current_multiple_unlocked(
        anjay_send_batch_builder_t *builder,
        anjay_unlocked_t *anjay,
        const anjay_send_resource_path_t *paths,
        size_t paths_length,
        bool ignore_not_found) {
    assert(builder);
    assert(anjay);

    anjay_batch_builder_t *batch_builder = cast_to_builder(builder);
    AVS_LIST(anjay_batch_entry_t) *append_ptr = batch_builder->append_ptr;
    avs_time_real_t timestamp = avs_time_real_now();

    for (size_t i = 0; i < paths_length; i++) {
        int result = batch_data_add_current_impl(builder, anjay, paths[i].oid,
                                                 paths[i].iid, paths[i].rid,
                                                 &timestamp);
        if (result == ANJAY_ERR_NOT_FOUND && ignore_not_found) {
            send_log(WARNING,
                     _("resource ") "/%u/%u/%u" _(" not found, ignoring"),
                     paths[i].oid, paths[i].iid, paths[i].rid);
        } else if (result) {
            batch_builder->append_ptr = append_ptr;
            _anjay_batch_entry_list_cleanup(batch_builder->append_ptr);
            return result;
        }
    }
    return 0;
}

int anjay_send_batch_data_add_current_multiple(
        anjay_send_batch_builder_t *builder,
        anjay_t *anjay_locked,
        const anjay_send_resource_path_t *paths,
        size_t paths_length) {
    int result = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    result = _anjay_send_batch_data_add_current_multiple_unlocked(
            builder, anjay, paths, paths_length, false);
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

int anjay_send_batch_data_add_current_multiple_ignore_not_found(
        anjay_send_batch_builder_t *builder,
        anjay_t *anjay_locked,
        const anjay_send_resource_path_t *paths,
        size_t paths_length) {
    int result = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    result = _anjay_send_batch_data_add_current_multiple_unlocked(
            builder, anjay, paths, paths_length, true);
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

anjay_send_batch_t *
anjay_send_batch_builder_compile(anjay_send_batch_builder_t **builder_ptr) {
    anjay_batch_builder_t *builder = cast_to_builder(*builder_ptr);
    anjay_send_batch_t *result =
            cast_to_send_batch(_anjay_batch_builder_compile(&builder));
    *builder_ptr = cast_to_send_builder(builder);
    return result;
}

anjay_send_batch_t *anjay_send_batch_acquire(const anjay_send_batch_t *batch) {
    return cast_to_send_batch(_anjay_batch_acquire(cast_to_const_batch(batch)));
}

void anjay_send_batch_release(anjay_send_batch_t **batch_ptr) {
    anjay_batch_t *batch = cast_to_batch(*batch_ptr);
    _anjay_batch_release(&batch);
    assert(!batch);
    *batch_ptr = NULL;
}

static void cancel_send_entry(AVS_LIST(anjay_send_entry_t) *entry_ptr,
                              int result) {
    call_finished_handler(*entry_ptr, result);
    delete_send_entry(entry_ptr);
}

bool _anjay_send_in_progress(anjay_connection_ref_t ref) {
    assert(ref.server);
    avs_coap_ctx_t *coap = NULL;
    if (ref.conn_type == ANJAY_CONNECTION_PRIMARY) {
        coap = _anjay_connection_get_coap(ref);
    }
    if (!coap) {
        return false;
    }
    AVS_LIST(anjay_send_entry_t) entry;
    AVS_LIST_FOREACH(entry, _anjay_from_server(ref.server)->sender.entries) {
        if (entry->target_ssid == _anjay_server_ssid(ref.server)
                && avs_coap_exchange_id_valid(entry->exchange_status.id)) {
            return true;
        } else if (entry->target_ssid > _anjay_server_ssid(ref.server)) {
            break;
        }
    }
    return false;
}

void _anjay_send_interrupt(anjay_connection_ref_t ref) {
    assert(ref.server);
    avs_coap_ctx_t *coap = NULL;
    if (ref.conn_type == ANJAY_CONNECTION_PRIMARY) {
        coap = _anjay_connection_get_coap(ref);
    }
    if (!coap) {
        return;
    }
    AVS_LIST(anjay_send_entry_t) *entry_ptr;
    AVS_LIST(anjay_send_entry_t) helper;
    AVS_LIST_DELETABLE_FOREACH_PTR(
            entry_ptr, helper,
            &_anjay_from_server(ref.server)->sender.entries) {
        if ((*entry_ptr)->target_ssid == _anjay_server_ssid(ref.server)
                && avs_coap_exchange_id_valid(
                           (*entry_ptr)->exchange_status.id)) {
            avs_coap_exchange_cancel(coap, (*entry_ptr)->exchange_status.id);
        } else if ((*entry_ptr)->target_ssid > _anjay_server_ssid(ref.server)) {
            break;
        }
    }
}

void _anjay_send_cleanup(anjay_sender_t *sender) {
    while (sender->entries) {
        cancel_send_entry(&sender->entries, ANJAY_SEND_ABORT);
    }
}

static void retry_deferred_job(avs_sched_t *sched, const void *ssid_) {
    anjay_t *anjay_locked = _anjay_get_from_sched(sched);
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    anjay_ssid_t ssid_or_any = *(const anjay_ssid_t *) ssid_;

    anjay_ssid_t send_condition_ssid = ANJAY_SSID_ANY;
    anjay_send_result_t send_condition = ANJAY_SEND_ERR_INTERNAL;
    anjay_connection_ref_t connection = {
        .server = NULL
    };

    AVS_LIST(anjay_send_entry_t) *entry_ptr;
    AVS_LIST(anjay_send_entry_t) helper;
    AVS_LIST_DELETABLE_FOREACH_PTR(entry_ptr, helper, &anjay->sender.entries) {
        if ((*entry_ptr)->exchange_status.memstream) {
            // Entry is not deferred
            continue;
        }

        if (ssid_or_any != ANJAY_SSID_ANY) {
            if ((*entry_ptr)->target_ssid < ssid_or_any) {
                continue;
            } else if ((*entry_ptr)->target_ssid > ssid_or_any) {
                break;
            }
        }

        assert((*entry_ptr)->target_ssid != ANJAY_SSID_ANY);
        if (send_condition_ssid != (*entry_ptr)->target_ssid) {
            send_condition_ssid = (*entry_ptr)->target_ssid;
            send_condition = check_send_possibility(anjay, send_condition_ssid,
                                                    &connection);
        }

        if ((send_condition != ANJAY_SEND_OK
             && (!(*entry_ptr)->deferrable
                 || !is_deferrable_condition(send_condition)))
                || (send_condition == ANJAY_SEND_OK
                    && avs_is_err(
                               start_send_exchange(*entry_ptr, connection)))) {
            cancel_send_entry(entry_ptr, ANJAY_SEND_DEFERRED_ERROR);
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
}

bool _anjay_send_has_deferred(anjay_unlocked_t *anjay, anjay_ssid_t ssid) {
    assert(ssid != ANJAY_SSID_ANY);
    AVS_LIST(anjay_send_entry_t) it;
    AVS_LIST_FOREACH(it, anjay->sender.entries) {
        if (it->target_ssid > ssid) {
            break;
        } else if (it->exchange_status.memstream) {
            // Entry is not deferred
            continue;
        } else if (it->target_ssid == ssid) {
            return true;
        }
    }
    return false;
}

int _anjay_send_sched_retry_deferred(anjay_unlocked_t *anjay,
                                     anjay_ssid_t ssid) {
    int result = AVS_SCHED_NOW(anjay->sched, NULL, retry_deferred_job, &ssid,
                               sizeof(ssid));
    if (result) {
        send_log(WARNING,
                 _("Could not schedule deferred retry for Send requests for "
                   "SSID = ") "%" PRIu16,
                 ssid);
    }
    return result;
}

#    endif // ANJAY_WITH_SEND

#endif // ANJAY_WITH_LWM2M11
