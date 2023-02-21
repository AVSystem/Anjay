/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#include <inttypes.h>

#include <avsystem/commons/avs_errno.h>
#include <avsystem/commons/avs_stream_membuf.h>

#include <avsystem/coap/async_client.h>
#include <avsystem/coap/code.h>

#include <anjay_modules/anjay_time_defs.h>

#define ANJAY_SERVERS_INTERNALS

#include "../anjay_core.h"
#include "../anjay_servers_inactive.h"
#include "../anjay_servers_private.h"
#include "../anjay_servers_reload.h"
#include "../anjay_servers_utils.h"
#include "../dm/anjay_query.h"

#include "anjay_activate.h"
#include "anjay_register.h"
#include "anjay_server_connections.h"
#include "anjay_servers_internal.h"

VISIBILITY_SOURCE_BEGIN

/** Update messages are sent to the server every
 * LIFETIME/ANJAY_UPDATE_INTERVAL_FACTOR seconds. */
#define ANJAY_UPDATE_INTERVAL_MARGIN_FACTOR 2

/** To avoid flooding the network in case of a very small lifetime, Update
 * messages are not sent more often than every ANJAY_MIN_UPDATE_INTERVAL_S
 * seconds. */
#define ANJAY_MIN_UPDATE_INTERVAL_S 1

static avs_time_real_t
calculate_time_of_next_update(anjay_server_info_t *server) {
    avs_time_real_t expire_time = _anjay_registration_expire_time(server);
    if (!avs_time_real_valid(expire_time)) {
        return AVS_TIME_REAL_INVALID;
    }
    avs_time_duration_t lifetime = avs_time_duration_from_scalar(
            server->registration_info.last_update_params.lifetime_s,
            AVS_TIME_S);
    avs_time_duration_t half_lifetime =
            avs_time_duration_div(lifetime,
                                  ANJAY_UPDATE_INTERVAL_MARGIN_FACTOR);
    anjay_server_connection_t *connection =
            _anjay_connection_get(&server->connections,
                                  ANJAY_CONNECTION_PRIMARY);
    avs_time_duration_t max_transmit_wait =
            _anjay_max_transmit_wait_for_transport(server->anjay,
                                                   connection->transport);
    avs_time_duration_t interval_margin =
            avs_time_duration_less(half_lifetime, max_transmit_wait)
                    ? half_lifetime
                    : max_transmit_wait;
    return avs_time_real_add(expire_time,
                             avs_time_duration_mul(interval_margin, -1));
}

static avs_time_real_t get_time_of_next_update(anjay_server_info_t *server) {
    if (server->next_action_handle
            && (server->next_action == ANJAY_SERVER_NEXT_ACTION_SEND_UPDATE
                || (server->next_action == ANJAY_SERVER_NEXT_ACTION_REFRESH
                    && server->registration_info.update_forced))) {
        // Update is scheduled - just return the time of that job
        avs_time_real_t real_now = avs_time_real_now();
        avs_time_monotonic_t monotonic_now = avs_time_monotonic_now();
        return avs_time_real_add(
                real_now, avs_time_monotonic_diff(
                                  avs_sched_time(&server->next_action_handle),
                                  monotonic_now));
    }
    // We don't have Update scheduled, so let's calculate it from scratch
    return calculate_time_of_next_update(server);
}

static int schedule_next_update(anjay_server_info_t *server) {
    if (!_anjay_server_active(server)) {
        // This may happen if the server is in the process of being disabled.
        // Skip scheduling Update in that case.
        return 0;
    }
    avs_time_real_t update_time = calculate_time_of_next_update(server);
    avs_time_duration_t min_margin =
            avs_time_duration_from_scalar(ANJAY_MIN_UPDATE_INTERVAL_S,
                                          AVS_TIME_S);
    avs_time_duration_t delay =
            avs_time_real_diff(update_time, avs_time_real_now());
    if (avs_time_duration_less(delay, min_margin)) {
        delay = min_margin;
    }

    anjay_log(DEBUG, _("scheduling update for SSID ") "%u" _(" after ") "%s",
              server->ssid, AVS_TIME_DURATION_AS_STRING(delay));

    return _anjay_server_reschedule_next_action(
            server, delay, ANJAY_SERVER_NEXT_ACTION_SEND_UPDATE);
}

bool _anjay_server_primary_connection_valid(anjay_server_info_t *server) {
    return _anjay_server_active(server)
           && _anjay_connection_get_online_socket((anjay_connection_ref_t) {
                  .server = server,
                  .conn_type = ANJAY_CONNECTION_PRIMARY
              }) != NULL;
}

int _anjay_server_reschedule_update_job(anjay_server_info_t *server) {
    if (schedule_next_update(server)) {
        anjay_log(ERROR, _("could not schedule next Update for server ") "%u",
                  server->ssid);
        return -1;
    }
    return 0;
}

static int reschedule_update_for_server(anjay_server_info_t *server) {
    int result = _anjay_schedule_refresh_server(server, AVS_TIME_DURATION_ZERO);
    if (!result) {
        // Make sure that Update is actually sent during the refresh.
        server->registration_info.update_forced = true;
    }
    return result;
}

static int reschedule_update_for_all_servers(anjay_unlocked_t *anjay) {
    int result = 0;

    AVS_LIST(anjay_server_info_t) it;
    AVS_LIST_FOREACH(it, anjay->servers) {
        if (_anjay_server_active(it)) {
            int partial = reschedule_update_for_server(it);
            if (!result) {
                result = partial;
            }
        }
    }

    return result;
}

int _anjay_schedule_registration_update_unlocked(anjay_unlocked_t *anjay,
                                                 anjay_ssid_t ssid) {
    int result = 0;

    if (ssid == ANJAY_SSID_ANY) {
        result = reschedule_update_for_all_servers(anjay);
    } else {
        anjay_server_info_t *server = _anjay_servers_find_active(anjay, ssid);
        if (!server) {
            anjay_log(WARNING, _("no active server with SSID = ") "%u", ssid);
            result = -1;
        } else {
            result = reschedule_update_for_server(server);
        }
    }

    return result;
}

int anjay_schedule_registration_update(anjay_t *anjay_locked,
                                       anjay_ssid_t ssid) {
    int result = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    result = _anjay_schedule_registration_update_unlocked(anjay, ssid);
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

static int dm_payload_writer(size_t payload_offset,
                             void *payload_buf,
                             size_t payload_buf_size,
                             size_t *out_payload_chunk_size,
                             void *state_) {
    anjay_registration_async_exchange_state_t *state =
            (anjay_registration_async_exchange_state_t *) state_;
    size_t length = state->new_params.dm ? strlen(state->new_params.dm) : 0;
    assert(payload_offset <= length);
    if ((*out_payload_chunk_size =
                 AVS_MIN(length - payload_offset, payload_buf_size))) {
        memcpy(payload_buf, &state->new_params.dm[payload_offset],
               *out_payload_chunk_size);
    }
    return 0;
}

static int get_server_lifetime(anjay_unlocked_t *anjay,
                               anjay_ssid_t ssid,
                               int64_t *out_lifetime) {
    anjay_iid_t server_iid;
    if (_anjay_find_server_iid(anjay, ssid, &server_iid)) {
        return -1;
    }

    const anjay_uri_path_t path =
            MAKE_RESOURCE_PATH(ANJAY_DM_OID_SERVER, server_iid,
                               ANJAY_DM_RID_SERVER_LIFETIME);
    int64_t lifetime;
    int read_ret = _anjay_dm_read_resource_i64(anjay, &path, &lifetime);

    if (read_ret) {
        anjay_log(ERROR, _("could not read lifetime for LwM2M server ") "%u",
                  ssid);
        return -1;
    } else if (lifetime <= 0) {
        anjay_log(ERROR,
                  _("lifetime returned by LwM2M server ") "%u" _(" is <= 0"),
                  ssid);
        return -1;
    }
    *out_lifetime = lifetime;

    return 0;
}

typedef struct {
    bool first;
    avs_stream_t *stream;
    anjay_lwm2m_version_t version;
} query_dm_args_t;

static int query_dm_instance(anjay_unlocked_t *anjay,
                             const anjay_dm_installed_object_t *obj,
                             anjay_iid_t iid,
                             void *args_) {
    (void) anjay;
    query_dm_args_t *args = (query_dm_args_t *) args_;
    avs_error_t err =
            avs_stream_write_f(args->stream, "%s</%u/%u>",
                               args->first ? "" : ",",
                               _anjay_dm_installed_object_oid(obj), iid);
    args->first = false;
    return avs_is_ok(err) ? 0 : -1;
}

static int query_dm_object(anjay_unlocked_t *anjay,
                           const anjay_dm_installed_object_t *obj,
                           void *args_) {
    anjay_oid_t oid = _anjay_dm_installed_object_oid(obj);
    if (oid == ANJAY_DM_OID_SECURITY) {
        /* LwM2M TS 1.1, 6.2.1. Register says that "The Security Object ID:0,
         * and OSCORE Object ID:21, if present, MUST NOT be part of the
         * Registration Objects and Object Instances list." */
        return 0;
    }

    query_dm_args_t *args = (query_dm_args_t *) args_;
    if (args->first) {
        args->first = false;
    } else if (avs_is_err(avs_stream_write(args->stream, ",", 1))) {
        return -1;
    }
    bool obj_written = false;
    const char *version = _anjay_dm_installed_object_version(obj);
    if (version) {
        const char *format = "</%u>;ver=\"%s\"";
#ifdef ANJAY_WITH_LWM2M11
        if (args->version > ANJAY_LWM2M_VERSION_1_0) {
            format = "</%u>;ver=%s";
        }
#endif // ANJAY_WITH_LWM2M11

        if (avs_is_err(
                    avs_stream_write_f(args->stream, format, oid, version))) {
            return -1;
        }
        obj_written = true;
    }
    query_dm_args_t instance_args = {
        .first = !obj_written,
        .stream = args->stream,
        .version = args->version
    };
    int result = _anjay_dm_foreach_instance(anjay, obj, query_dm_instance,
                                            &instance_args);
    if (result) {
        return result;
    }
    if (!instance_args.first) {
        obj_written = true;
    }
    if (!obj_written
            && avs_is_err(avs_stream_write_f(args->stream, "</%u>", oid))) {
        return -1;
    }
    return 0;
}

static int
query_dm(anjay_unlocked_t *anjay, anjay_lwm2m_version_t version, char **out) {
    assert(out);
    assert(!*out);
    avs_stream_t *stream = avs_stream_membuf_create();
    if (!stream) {
        anjay_log(ERROR, _("out of memory"));
        return -1;
    }
    int retval;
    void *data = NULL;
    if ((retval = _anjay_dm_foreach_object(anjay, query_dm_object,
                                           &(query_dm_args_t) {
                                               .first = true,
                                               .stream = stream,
                                               .version = version
                                           }))
            || (retval =
                        (avs_is_ok(avs_stream_write(stream, "\0", 1)) ? 0 : -1))
            || (retval = (avs_is_ok(avs_stream_membuf_take_ownership(
                                  stream, &data, NULL))
                                  ? 0
                                  : -1))) {
        anjay_log(ERROR, _("could not enumerate objects"));
    }
    avs_stream_cleanup(&stream);
    *out = (char *) data;
    return retval;
}

static void update_parameters_cleanup(anjay_update_parameters_t *params) {
    avs_free(params->dm);
    params->dm = NULL;
}

static void
get_binding_mode_for_version(anjay_server_info_t *server,
                             anjay_lwm2m_version_t lwm2m_version,
                             anjay_binding_mode_t *out_binding_mode) {
    const anjay_binding_mode_t *server_binding_mode =
            _anjay_server_binding_mode(server);
    size_t out_ptr = 0;
    for (size_t in_ptr = 0; in_ptr < sizeof(*server_binding_mode) - 1
                            && server_binding_mode->data[in_ptr];
         ++in_ptr) {
#ifdef ANJAY_WITH_LWM2M11
        if (lwm2m_version >= ANJAY_LWM2M_VERSION_1_1
                && server_binding_mode->data[in_ptr] == 'Q') {
            continue;
        }
#else  // ANJAY_WITH_LWM2M11
        (void) lwm2m_version;
#endif // ANJAY_WITH_LWM2M11
        out_binding_mode->data[out_ptr++] = server_binding_mode->data[in_ptr];
    }
}

static int update_parameters_init(anjay_server_info_t *server,
                                  anjay_lwm2m_version_t lwm2m_version,
                                  anjay_update_parameters_t *out_params) {
    memset(out_params, 0, sizeof(*out_params));
    if (query_dm(server->anjay, lwm2m_version, &out_params->dm)) {
        goto error;
    }
    if (get_server_lifetime(server->anjay, _anjay_server_ssid(server),
                            &out_params->lifetime_s)) {
        goto error;
    }
    get_binding_mode_for_version(server, lwm2m_version,
                                 &out_params->binding_mode);
    return 0;
error:
    update_parameters_cleanup(out_params);
    return -1;
}

void _anjay_registration_info_cleanup(anjay_registration_info_t *info) {
    AVS_LIST_CLEAR(&info->endpoint_path);
    update_parameters_cleanup(&info->last_update_params);
}

void _anjay_registration_exchange_state_cleanup(
        anjay_registration_async_exchange_state_t *state) {
    avs_coap_ctx_t *coap = _anjay_connection_get_coap((anjay_connection_ref_t) {
        .server = AVS_CONTAINER_OF(state, anjay_server_info_t,
                                   registration_exchange_state),
        .conn_type = ANJAY_CONNECTION_PRIMARY
    });
    if (coap && avs_coap_exchange_id_valid(state->exchange_id)) {
        avs_coap_exchange_cancel(coap, state->exchange_id);
        assert(!avs_coap_exchange_id_valid(state->exchange_id));
    }
    update_parameters_cleanup(&state->new_params);
}

static bool should_use_queue_mode(anjay_server_info_t *server,
                                  anjay_lwm2m_version_t lwm2m_version) {
#ifdef ANJAY_WITH_LWM2M11
    switch (server->anjay->queue_mode_preference) {
    case ANJAY_FORCE_QUEUE_MODE:
        return true;
    case ANJAY_PREFER_QUEUE_MODE:
        if (lwm2m_version >= ANJAY_LWM2M_VERSION_1_1) {
            return true;
        }
        // fall-through
    case ANJAY_PREFER_ONLINE_MODE:
#else  // ANJAY_WITH_LWM2M11
    (void) server;
    (void) lwm2m_version;
#endif // ANJAY_WITH_LWM2M11
        return !!strchr(_anjay_server_binding_mode(server)->data, 'Q');
#ifdef ANJAY_WITH_LWM2M11
    case ANJAY_FORCE_ONLINE_MODE:
        return false;
    }

    AVS_UNREACHABLE("Invalid anjay_queue_mode_preference_t value");
    return false;
#endif // ANJAY_WITH_LWM2M11
}

#ifdef ANJAY_WITH_LWM2M11
static bool lwm2m11_queue_mode_changed(anjay_server_info_t *server) {
    if (server->registration_info.lwm2m_version >= ANJAY_LWM2M_VERSION_1_1
            && should_use_queue_mode(server,
                                     server->registration_info.lwm2m_version)
                           != server->registration_info.queue_mode) {
        anjay_log(DEBUG,
                  _("State of 1.1-style queue mode changed for SSID = ") "%u" _(
                          ", forcing re-register"),
                  server->ssid);
        return true;
    }
    return false;
}
#endif // ANJAY_WITH_LWM2M11

static int get_endpoint_path(AVS_LIST(const anjay_string_t) *out_path,
                             const avs_coap_options_t *opts) {
    assert(*out_path == NULL);

    int result;
    char buffer[ANJAY_MAX_URI_SEGMENT_SIZE];
    size_t attr_size;

    avs_coap_option_iterator_t it = AVS_COAP_OPTION_ITERATOR_EMPTY;
    while ((result = avs_coap_options_get_string_it(
                    opts, AVS_COAP_OPTION_LOCATION_PATH, &it, &attr_size,
                    buffer, sizeof(buffer) - 1))
           == 0) {
        buffer[attr_size] = '\0';

        AVS_LIST(anjay_string_t) segment =
                (AVS_LIST(anjay_string_t)) AVS_LIST_NEW_BUFFER(attr_size + 1);
        if (!segment) {
            anjay_log(ERROR, _("out of memory"));
            goto fail;
        }

        memcpy(segment, buffer, attr_size + 1);
        AVS_LIST_APPEND(out_path, segment);
    }

    if (result == AVS_COAP_OPTION_MISSING) {
        return 0;
    }

fail:
    AVS_LIST_CLEAR(out_path);
    return result;
}

static const char *assemble_endpoint_path(char *buffer,
                                          size_t buffer_size,
                                          AVS_LIST(const anjay_string_t) path) {
    size_t off = 0;
    AVS_LIST(const anjay_string_t) segment;
    AVS_LIST_FOREACH(segment, path) {
        int result = avs_simple_snprintf(buffer + off, buffer_size - off, "/%s",
                                         segment->c_str);
        if (result < 0) {
            return "<ERROR>";
        }
        off += (size_t) result;
    }

    return buffer;
}

static anjay_registration_result_t map_coap_error(avs_error_t coap_err) {
    assert(avs_is_err(coap_err));
    if (coap_err.category == AVS_COAP_ERR_CATEGORY
            && coap_err.code == AVS_COAP_ERR_TIMEOUT) {
        return ANJAY_REGISTRATION_ERROR_TIMEOUT;
    } else {
        anjay_log(DEBUG, _("mapping CoAP error (") "%s" _(") to network error"),
                  AVS_COAP_STRERROR(coap_err));
        return ANJAY_REGISTRATION_ERROR_NETWORK;
    }
}

static avs_error_t
setup_register_request_options(avs_coap_options_t *opts,
                               anjay_lwm2m_version_t lwm2m_version,
                               const char *endpoint_name,
                               const char *msisdn,
                               const anjay_url_t *uri,
                               bool lwm2m11_queue_mode,
                               int64_t lifetime_s,
                               const anjay_binding_mode_t *binding_mode) {
    assert(opts->size == 0);

    avs_error_t err;
    if (avs_is_err((err = avs_coap_options_set_content_format(
                            opts, AVS_COAP_FORMAT_LINK_FORMAT)))
            || avs_is_err(
                       (err = _anjay_coap_add_string_options(
                                opts, uri->uri_path, AVS_COAP_OPTION_URI_PATH)))
            || avs_is_err((err = avs_coap_options_add_string(
                                   opts, AVS_COAP_OPTION_URI_PATH, "rd")))
            || avs_is_err((err = _anjay_coap_add_string_options(
                                   opts, uri->uri_query,
                                   AVS_COAP_OPTION_URI_QUERY)))
            || avs_is_err((err = _anjay_coap_add_query_options(
                                   opts, &lwm2m_version, endpoint_name,
                                   &lifetime_s,
                                   strcmp(binding_mode->data, "U") == 0
                                           ? NULL
                                           : binding_mode->data,
                                   lwm2m11_queue_mode, msisdn)))) {
        anjay_log(ERROR, _("could not initialize request headers"));
    }
    return err;
}

static anjay_registration_result_t
check_register_response(const avs_coap_response_header_t *response,
                        AVS_LIST(const anjay_string_t) *out_endpoint_path) {
    if (response->code != AVS_COAP_CODE_CREATED) {
        anjay_log(WARNING,
                  _("server responded with ") "%s" _(" (expected ") "%s" _(")"),
                  AVS_COAP_CODE_STRING(response->code),
                  AVS_COAP_CODE_STRING(AVS_COAP_CODE_CREATED));
        assert(response->code != 0);
        return response->code == AVS_COAP_CODE_PRECONDITION_FAILED
                       ? ANJAY_REGISTRATION_ERROR_FALLBACK_REQUESTED
                       : ANJAY_REGISTRATION_ERROR_REJECTED;
    }

    AVS_LIST_CLEAR(out_endpoint_path);
    if (get_endpoint_path(out_endpoint_path, &response->options)) {
        anjay_log(ERROR, _("could not store Update location"));
        return ANJAY_REGISTRATION_ERROR_OTHER;
    }

    char location_buf[256] = "";
    anjay_log(INFO, _("registration successful, location = ") "%s",
              assemble_endpoint_path(location_buf, sizeof(location_buf),
                                     *out_endpoint_path));
    return ANJAY_REGISTRATION_SUCCESS;
}

static void register_with_version(anjay_server_info_t *server,
                                  anjay_lwm2m_version_t lwm2m_version,
                                  anjay_update_parameters_t *move_params);

static void
handle_register_response(anjay_server_info_t *server,
                         anjay_lwm2m_version_t attempted_version,
                         AVS_LIST(const anjay_string_t) *move_endpoint_path,
                         anjay_update_parameters_t *move_params,
                         anjay_registration_result_t result,
                         avs_error_t err) {
    if (result != ANJAY_REGISTRATION_SUCCESS) {
        anjay_log(WARNING, _("could not register to server ") "%u",
                  _anjay_server_ssid(server));
        AVS_LIST_CLEAR(move_endpoint_path);
    } else {
        _anjay_server_update_registration_info(
                server, move_endpoint_path, attempted_version,
                should_use_queue_mode(server, attempted_version), move_params);
        assert(!*move_endpoint_path);
    }

    if (result == ANJAY_REGISTRATION_ERROR_FALLBACK_REQUESTED) {
#ifdef ANJAY_WITH_LWM2M11
        if (attempted_version
                > server->anjay->lwm2m_version_config.minimum_version) {
            attempted_version = (anjay_lwm2m_version_t) (attempted_version - 1);
            anjay_log(WARNING,
                      _("attempting to fall back to LwM2M version ") "%s",
                      _anjay_lwm2m_version_as_string(attempted_version));
            // NOTE: update_parameters format may differ slightly between
            // LwM2M versions, so we need to rebuild them
            update_parameters_cleanup(move_params);
            if (update_parameters_init(server, attempted_version,
                                       move_params)) {
                result = ANJAY_REGISTRATION_ERROR_OTHER;
            } else {
                register_with_version(server, attempted_version, move_params);
            }
        } else
#endif // ANJAY_WITH_LWM2M11
        {
            result = ANJAY_REGISTRATION_ERROR_REJECTED;
        }
    }
    if (result != ANJAY_REGISTRATION_ERROR_FALLBACK_REQUESTED) {
        _anjay_server_on_updated_registration(server, result, err);
    }
}

static void
receive_register_response(avs_coap_ctx_t *coap,
                          avs_coap_exchange_id_t exchange_id,
                          avs_coap_client_request_state_t request_state,
                          const avs_coap_client_async_response_t *response,
                          avs_error_t err,
                          void *state_) {
    anjay_registration_async_exchange_state_t *state =
            (anjay_registration_async_exchange_state_t *) state_;
    anjay_registration_result_t result = ANJAY_REGISTRATION_ERROR_OTHER;
    AVS_LIST(const anjay_string_t) endpoint_path = NULL;
    if (request_state != AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT) {
        state->exchange_id = AVS_COAP_EXCHANGE_ID_INVALID;
    }

    switch (request_state) {
    case AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT:
        // Note: this will recursively call this function with
        // AVS_COAP_CLIENT_REQUEST_CANCEL.
        avs_coap_exchange_cancel(coap, exchange_id);
        // fall-through

    case AVS_COAP_CLIENT_REQUEST_OK:
        result = check_register_response(&response->header, &endpoint_path);
        break;

    case AVS_COAP_CLIENT_REQUEST_FAIL: {
        assert(avs_is_err(err));
        anjay_log(WARNING,
                  _("failure while receiving Register response: ") "%s",
                  AVS_COAP_STRERROR(err));
        result = map_coap_error(err);
        break;
    }

    case AVS_COAP_CLIENT_REQUEST_CANCEL:
        return;
    }

    handle_register_response(AVS_CONTAINER_OF(state, anjay_server_info_t,
                                              registration_exchange_state),
                             state->attempted_version, &endpoint_path,
                             &state->new_params, result, err);
    assert(!endpoint_path);
}

static void move_assign_update_params(anjay_update_parameters_t *out,
                                      anjay_update_parameters_t *move_in) {
    assert(out);
    assert(move_in);
    if (out != move_in) {
        if (move_in->dm) {
            avs_free(out->dm);
            out->dm = move_in->dm;
            move_in->dm = NULL;
        }

        out->lifetime_s = move_in->lifetime_s;
        memcpy(&out->binding_mode, &move_in->binding_mode,
               sizeof(out->binding_mode));
    }
}

static void send_register(anjay_server_info_t *server,
                          avs_coap_ctx_t *coap,
                          anjay_lwm2m_version_t lwm2m_version,
                          bool lwm2m11_queue_mode,
                          anjay_update_parameters_t *move_params) {
    const anjay_url_t *const connection_uri =
            _anjay_connection_uri((anjay_connection_ref_t) {
                .server = server,
                .conn_type = ANJAY_CONNECTION_PRIMARY
            });

    avs_coap_request_header_t request = {
        .code = AVS_COAP_CODE_POST
    };

    get_binding_mode_for_version(server, lwm2m_version,
                                 &move_params->binding_mode);

    avs_error_t err;
    if (avs_is_err((err = avs_coap_options_dynamic_init(&request.options)))
            || avs_is_err((err = setup_register_request_options(
                                   &request.options, lwm2m_version,
                                   server->anjay->endpoint_name, NULL,
                                   connection_uri, lwm2m11_queue_mode,
                                   move_params->lifetime_s,
                                   &move_params->binding_mode)))) {
        _anjay_server_on_updated_registration(
                server, ANJAY_REGISTRATION_ERROR_OTHER, err);
        goto cleanup;
    }
    ++server->registration_attempts;

    anjay_log(DEBUG, _("sending Register"));

    if (avs_coap_exchange_id_valid(
                server->registration_exchange_state.exchange_id)) {
        avs_coap_exchange_cancel(
                coap, server->registration_exchange_state.exchange_id);
    }
    assert(!avs_coap_exchange_id_valid(
            server->registration_exchange_state.exchange_id));
    server->registration_exchange_state.attempted_version = lwm2m_version;
    move_assign_update_params(&server->registration_exchange_state.new_params,
                              move_params);
    if (avs_is_err(
                (err = avs_coap_client_send_async_request(
                         coap, &server->registration_exchange_state.exchange_id,
                         &request, dm_payload_writer,
                         &server->registration_exchange_state,
                         receive_register_response,
                         &server->registration_exchange_state)))) {
        anjay_log(ERROR, _("could not send Register: ") "%s",
                  AVS_COAP_STRERROR(err));
        _anjay_server_on_updated_registration(server, map_coap_error(err), err);
    } else {
        anjay_log(INFO, _("Register sent"));
        server->registration_info.update_forced = false;
#ifdef ANJAY_WITH_COMMUNICATION_TIMESTAMP_API
        _anjay_server_set_last_communication_time(server);
#endif // ANJAY_WITH_COMMUNICATION_TIMESTAMP_API
    }
cleanup:
    avs_coap_options_cleanup(&request.options);
}

static void register_with_version(anjay_server_info_t *server,
                                  anjay_lwm2m_version_t lwm2m_version,
                                  anjay_update_parameters_t *move_params) {
    anjay_connection_ref_t connection = {
        .server = server,
        .conn_type = ANJAY_CONNECTION_PRIMARY
    };
    if (!_anjay_connection_get_online_socket(connection)) {
        anjay_log(ERROR, _("server connection is not online"));
        _anjay_server_on_updated_registration(
                server, ANJAY_REGISTRATION_ERROR_OTHER, avs_errno(AVS_EBADF));
    } else {
#ifdef ANJAY_WITH_LWM2M11
        bool queue_mode = should_use_queue_mode(server, lwm2m_version);
        bool lwm2m11_queue_mode =
                (queue_mode && lwm2m_version >= ANJAY_LWM2M_VERSION_1_1);
#endif // ANJAY_WITH_LWM2M11

        send_register(server, _anjay_connection_get_coap(connection),
                      lwm2m_version,
#ifdef ANJAY_WITH_LWM2M11
                      lwm2m11_queue_mode,
#else  // ANJAY_WITH_LWM2M11
                      false,
#endif // ANJAY_WITH_LWM2M11
                      move_params);
        _anjay_connection_schedule_queue_mode_close((anjay_connection_ref_t) {
            .server = server,
            .conn_type = ANJAY_CONNECTION_PRIMARY
        });
    }
}

static void do_register(anjay_server_info_t *server,
                        anjay_update_parameters_t *move_params) {
    anjay_lwm2m_version_t attempted_version =
#ifdef ANJAY_WITH_LWM2M11
            server->anjay->lwm2m_version_config.maximum_version;
#else  // ANJAY_WITH_LWM2M11
            ANJAY_LWM2M_VERSION_1_0;
#endif // ANJAY_WITH_LWM2M11
    anjay_log(INFO, _("Attempting to register with LwM2M version ") "%s",
              _anjay_lwm2m_version_as_string(attempted_version));
    register_with_version(server, attempted_version, move_params);
}

static inline bool dm_caches_equal(const char *left, const char *right) {
    return strcmp(left ? left : "", right ? right : "") == 0;
}

static avs_error_t
setup_update_request_options(anjay_unlocked_t *anjay,
                             avs_coap_options_t *opts,
                             AVS_LIST(const anjay_string_t) endpoint_path,
                             const anjay_update_parameters_t *old_params,
                             const anjay_update_parameters_t *new_params,
                             bool *out_dm_changed_since_last_update) {
    (void) anjay;
    assert(opts->size == 0);

    const int64_t *lifetime_s_ptr = NULL;
    assert(new_params->lifetime_s >= 0);
    if (new_params->lifetime_s != old_params->lifetime_s) {
        lifetime_s_ptr = &new_params->lifetime_s;
    }

    const char *binding_mode = (strcmp(old_params->binding_mode.data,
                                       new_params->binding_mode.data)
                                == 0)
                                       ? NULL
                                       : new_params->binding_mode.data;
    const char *sms_msisdn = NULL;
    *out_dm_changed_since_last_update =
            !dm_caches_equal(old_params->dm, new_params->dm);

    avs_error_t err;
    (void) ((*out_dm_changed_since_last_update
             && avs_is_err((err = avs_coap_options_set_content_format(
                                    opts, AVS_COAP_FORMAT_LINK_FORMAT))))
            || avs_is_err(
                       (err = _anjay_coap_add_string_options(
                                opts, endpoint_path, AVS_COAP_OPTION_URI_PATH)))
            || avs_is_err((err = _anjay_coap_add_query_options(
                                   /* opts = */ opts, /* version = */ NULL,
                                   /* endpoint_name = */ NULL,
                                   /* lifetime = */ lifetime_s_ptr,
                                   /* binding_mode = */ binding_mode,
                                   /* lwm2m11_queue_mode = */ false,
                                   /* sms_msisdn = */ sms_msisdn))));

    return err;
}

static anjay_registration_result_t
check_update_response(const avs_coap_response_header_t *response) {
    if (response->code == AVS_COAP_CODE_CHANGED) {
        anjay_log(INFO, _("registration successfully updated"));
        return ANJAY_REGISTRATION_SUCCESS;
    } else {
        /* 4.xx (client error) response means that a server received a
         * request it considers invalid, so retransmission of the same
         * message will most likely fail again. That may happen if:
         * - the registration already expired (4.04 Not Found response),
         * - the server is unable to parse our Update request or unwilling
         *   to process it,
         * - the server is broken.
         *
         * In the first case, the correct response is to Register again.
         * Otherwise, we might as well do the same, as server is required to
         * replace client registration information in such case.
         *
         * Any other response is either an 5.xx (server error), in which
         * case retransmission may succeed, or an unexpected non-error
         * response. However, as we don't do retransmissions, degenerating
         * to Register seems the best thing we can do. */
        anjay_log(DEBUG,
                  _("Update rejected: ") "%s" _(" (expected ") "%s" _(")"),
                  AVS_COAP_CODE_STRING(response->code),
                  AVS_COAP_CODE_STRING(AVS_COAP_CODE_CHANGED));
        assert(response->code != 0);
        return ANJAY_REGISTRATION_ERROR_REJECTED;
    }
}

static void
on_registration_update_result(anjay_server_info_t *server,
                              anjay_update_parameters_t *move_params,
                              anjay_registration_result_t result,
                              avs_error_t err) {
    switch (result) {
    case ANJAY_REGISTRATION_ERROR_TIMEOUT:
        anjay_log(WARNING,
                  _("timeout while updating registration for SSID==") "%" PRIu16
                          _("; trying to re-register"),
                  server->ssid);
        server->registration_info.expire_time = AVS_TIME_REAL_INVALID;
        // defined(ANJAY_WITH_CORE_PERSISTENCE)
        do_register(server, move_params);
        break;

    case ANJAY_REGISTRATION_ERROR_REJECTED:
        anjay_log(DEBUG,
                  _("update rejected for SSID = ") "%u" _(
                          "; needs re-registration"),
                  server->ssid);
        server->registration_info.expire_time = AVS_TIME_REAL_INVALID;
        // defined(ANJAY_WITH_CORE_PERSISTENCE)
        do_register(server, move_params);
        break;

    case ANJAY_REGISTRATION_SUCCESS: {
        const anjay_registration_info_t *old_info =
                _anjay_server_registration_info(server);
        _anjay_server_update_registration_info(
                server, NULL, old_info->lwm2m_version,
                should_use_queue_mode(server, old_info->lwm2m_version),
                move_params);
        update_parameters_cleanup(move_params);
        _anjay_server_on_updated_registration(server, result, err);
        break;
    }

    default:
        anjay_log(
                ERROR,
                _("could not send registration update for SSID==") "%" PRIu16 _(
                        ": ") "%d",
                server->ssid, (int) result);
        update_parameters_cleanup(move_params);
        _anjay_server_on_updated_registration(server, result, err);
    }
}

static void
receive_update_response(avs_coap_ctx_t *coap,
                        avs_coap_exchange_id_t exchange_id,
                        avs_coap_client_request_state_t request_state,
                        const avs_coap_client_async_response_t *response,
                        avs_error_t err,
                        void *state_) {
    anjay_registration_async_exchange_state_t *state =
            (anjay_registration_async_exchange_state_t *) state_;
    anjay_server_info_t *server = AVS_CONTAINER_OF(state, anjay_server_info_t,
                                                   registration_exchange_state);

    anjay_registration_result_t result = ANJAY_REGISTRATION_ERROR_OTHER;
    if (request_state != AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT) {
        state->exchange_id = AVS_COAP_EXCHANGE_ID_INVALID;
    }

    switch (request_state) {
    case AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT:
        // Note: this will recursively call this function with
        // AVS_COAP_CLIENT_REQUEST_CANCEL.
        avs_coap_exchange_cancel(coap, exchange_id);
        server->registration_info.update_forced = false;
        // fall-through

    case AVS_COAP_CLIENT_REQUEST_OK:
        result = check_update_response(&response->header);
        break;

    case AVS_COAP_CLIENT_REQUEST_FAIL: {
        assert(avs_is_err(err));
        anjay_log(WARNING, _("failure while receiving Update response: ") "%s",
                  AVS_COAP_STRERROR(err));
        result = map_coap_error(err);
        break;
    }

    case AVS_COAP_CLIENT_REQUEST_CANCEL:
        // Interrupted Update - make sure it is restarted after next refresh
        server->registration_info.update_forced = true;
        return;
    }

    on_registration_update_result(server, &state->new_params, result, err);
}

static void send_update(anjay_server_info_t *server,
                        avs_coap_ctx_t *coap,
                        anjay_update_parameters_t *move_params) {
    const anjay_registration_info_t *old_info =
            _anjay_server_registration_info(server);
    avs_coap_request_header_t request = {
        .code = AVS_COAP_CODE_POST
    };
    bool dm_changed_since_last_update;

    avs_error_t err;
    if (avs_is_err((err = avs_coap_options_dynamic_init(&request.options)))
            || avs_is_err((err = setup_update_request_options(
                                   server->anjay, &request.options,
                                   old_info->endpoint_path,
                                   &old_info->last_update_params, move_params,
                                   &dm_changed_since_last_update)))) {
        anjay_log(ERROR, _("could not setup update request"));
        on_registration_update_result(server, move_params,
                                      ANJAY_REGISTRATION_ERROR_OTHER, err);
        goto end;
    }

    anjay_log(DEBUG, _("sending Update"));

    if (avs_coap_exchange_id_valid(
                server->registration_exchange_state.exchange_id)) {
        avs_coap_exchange_cancel(
                coap, server->registration_exchange_state.exchange_id);
    }
    assert(!avs_coap_exchange_id_valid(
            server->registration_exchange_state.exchange_id));
    server->registration_exchange_state.attempted_version =
            old_info->lwm2m_version;
    move_assign_update_params(&server->registration_exchange_state.new_params,
                              move_params);
    if (avs_is_err((
                err = avs_coap_client_send_async_request(
                        coap, &server->registration_exchange_state.exchange_id,
                        &request,
                        dm_changed_since_last_update ? dm_payload_writer : NULL,
                        &server->registration_exchange_state,
                        receive_update_response,
                        &server->registration_exchange_state)))) {
        anjay_log(ERROR, _("could not send Update: ") "%s",
                  AVS_COAP_STRERROR(err));
        on_registration_update_result(server, move_params, map_coap_error(err),
                                      err);
    } else {
        anjay_log(INFO, _("Update sent"));
        server->registration_info.update_forced = false;
#ifdef ANJAY_WITH_COMMUNICATION_TIMESTAMP_API
        _anjay_server_set_last_communication_time(server);
#endif // ANJAY_WITH_COMMUNICATION_TIMESTAMP_API
    }
end:
    avs_coap_options_cleanup(&request.options);
}

static bool
needs_registration_update(anjay_server_info_t *server,
                          const anjay_update_parameters_t *new_params) {
    const anjay_registration_info_t *info =
            _anjay_server_registration_info(server);
    const anjay_update_parameters_t *old_params = &info->last_update_params;
    return info->update_forced
           || old_params->lifetime_s != new_params->lifetime_s
           || strcmp(old_params->binding_mode.data,
                     new_params->binding_mode.data)
           || !dm_caches_equal(old_params->dm, new_params->dm);
}

static void update_registration(anjay_server_info_t *server,
                                anjay_update_parameters_t *move_params) {
    anjay_connection_ref_t connection = {
        .server = server,
        .conn_type = ANJAY_CONNECTION_PRIMARY
    };
    if (!_anjay_connection_get_online_socket(connection)) {
        anjay_log(ERROR, _("server connection is not online"));
        on_registration_update_result(server, move_params,
                                      ANJAY_REGISTRATION_ERROR_OTHER,
                                      avs_errno(AVS_EBADF));
    } else {
        send_update(server, _anjay_connection_get_coap(connection),
                    move_params);
        _anjay_connection_schedule_queue_mode_close((anjay_connection_ref_t) {
            .server = server,
            .conn_type = ANJAY_CONNECTION_PRIMARY
        });
    }
}

void _anjay_server_ensure_valid_registration(anjay_server_info_t *server) {
    assert(server->ssid != ANJAY_SSID_BOOTSTRAP);
    if (!_anjay_server_active(server)) {
        // This may happen if the server is in the process of being disabled.
        // Skip Register/Update in that case.
        return;
    }

    anjay_update_parameters_t new_params;
    if (update_parameters_init(server, server->registration_info.lwm2m_version,
                               &new_params)) {
        on_registration_update_result(server, &new_params,
                                      ANJAY_REGISTRATION_ERROR_OTHER,
                                      avs_errno(AVS_UNKNOWN_ERROR));
    } else if (!_anjay_server_primary_connection_valid(server)) {
        anjay_log(ERROR,
                  _("No valid connection to Registration Interface for "
                    "SSID = ") "%u",
                  server->ssid);
        on_registration_update_result(server, &new_params,
                                      ANJAY_REGISTRATION_ERROR_OTHER,
                                      avs_errno(AVS_EBADF));
    } else {
        bool registration_or_update_in_progress = avs_coap_exchange_id_valid(
                server->registration_exchange_state.exchange_id);
        bool registration_expired = _anjay_server_registration_expired(server);
        bool needs_reregistration =
                !registration_or_update_in_progress && registration_expired;
#ifdef ANJAY_WITH_LWM2M11
        if (!needs_reregistration && lwm2m11_queue_mode_changed(server)) {
            needs_reregistration = true;
        }
#endif // ANJAY_WITH_LWM2M11
        bool needs_update = !needs_reregistration
                            && needs_registration_update(server, &new_params);
        if (needs_reregistration
                || (registration_or_update_in_progress && registration_expired
                    && needs_update)) {
            on_registration_update_result(server, &new_params,
                                          ANJAY_REGISTRATION_ERROR_REJECTED,
                                          avs_errno(AVS_UNKNOWN_ERROR));
        } else if (!needs_update) {
            update_parameters_cleanup(&new_params);
            if (!registration_or_update_in_progress) {
                _anjay_server_on_updated_registration(
                        server, ANJAY_REGISTRATION_SUCCESS, AVS_OK);
                anjay_connection_ref_t ref = {
                    .server = server,
                    .conn_type = ANJAY_CONNECTION_PRIMARY
                };
                anjay_server_connection_t *connection =
                        _anjay_get_server_connection(ref);
                if (!connection->queue_mode_close_socket_clb) {
                    _anjay_connection_schedule_queue_mode_close(ref);
                }
            }
        } else {
            update_registration(server, &new_params);
        }
    }
}

#ifndef ANJAY_WITHOUT_DEREGISTER
static avs_error_t
setup_deregister_request(avs_coap_request_header_t *out_request,
                         AVS_LIST(const anjay_string_t) endpoint_path) {
    *out_request = (avs_coap_request_header_t) {
        .code = AVS_COAP_CODE_DELETE
    };

    avs_error_t err;
    if (avs_is_err((err = avs_coap_options_dynamic_init(&out_request->options)))
            || avs_is_err((err = _anjay_coap_add_string_options(
                                   &out_request->options,
                                   endpoint_path,
                                   AVS_COAP_OPTION_URI_PATH)))) {
        anjay_log(ERROR, _("could not initialize request headers"));
    }
    return err;
}

static avs_error_t deregister(anjay_server_info_t *server) {
    // server is supposed to be bound at this point
    avs_coap_ctx_t *coap = _anjay_connection_get_coap((anjay_connection_ref_t) {
        .server = server,
        .conn_type = ANJAY_CONNECTION_PRIMARY
    });
    AVS_ASSERT(coap, "Register is not supposed to be called on a connection "
                     "that has no CoAP context");

    avs_coap_request_header_t request = { 0 };
    avs_coap_response_header_t response = { 0 };
    avs_error_t err =
            setup_deregister_request(&request,
                                     server->registration_info.endpoint_path);
    if (avs_is_err(err)) {
        goto end;
    }

    if (avs_is_err((err = avs_coap_streaming_send_request(
                            coap, &request, NULL, NULL, &response, NULL)))) {
        anjay_log(ERROR, _("Could not perform De-registration"));
        goto end;
    }

    if (response.code != AVS_COAP_CODE_DELETED) {
        anjay_log(WARNING,
                  _("server responded with ") "%s" _(" (expected ") "%s" _(")"),
                  AVS_COAP_CODE_STRING(response.code),
                  AVS_COAP_CODE_STRING(AVS_COAP_CODE_DELETED));
        err = avs_errno(AVS_EPROTO);
        goto end;
    }

    anjay_log(INFO, _("De-register sent"));
    err = AVS_OK;
#    ifdef ANJAY_WITH_COMMUNICATION_TIMESTAMP_API
    _anjay_server_set_last_communication_time(server);
#    endif // ANJAY_WITH_COMMUNICATION_TIMESTAMP_API

end:
    avs_coap_options_cleanup(&request.options);
    avs_coap_options_cleanup(&response.options);
    return err;
}

avs_error_t _anjay_server_deregister(anjay_server_info_t *server) {
    // make sure to cancel the reconnect/register/update job. there's no point
    // in doing that if we don't want to be registered to the server.
    avs_sched_del(&server->next_action_handle);

    assert(_anjay_server_active(server));
    anjay_connection_ref_t connection = {
        .server = server,
        .conn_type = ANJAY_CONNECTION_PRIMARY
    };
    if (!_anjay_connection_get_online_socket(connection)) {
        anjay_log(ERROR, _("server connection is not online, skipping"));
        return AVS_OK;
    }

    avs_error_t err = deregister(server);
    if (avs_is_err(err)) {
        anjay_log(ERROR, _("could not send De-Register request: ") "%s",
                  AVS_COAP_STRERROR(err));
    }
    return err;
}
#endif // ANJAY_WITHOUT_DEREGISTER

const anjay_registration_info_t *
_anjay_server_registration_info(anjay_server_info_t *server) {
    return &server->registration_info;
}

static avs_time_real_t get_registration_expire_time(int64_t lifetime_s) {
    return avs_time_real_add(avs_time_real_now(),
                             avs_time_duration_from_scalar(lifetime_s,
                                                           AVS_TIME_S));
}

void _anjay_server_update_registration_info(
        anjay_server_info_t *server,
        AVS_LIST(const anjay_string_t) *move_endpoint_path,
        anjay_lwm2m_version_t lwm2m_version,
        bool queue_mode,
        anjay_update_parameters_t *move_params) {
    anjay_registration_info_t *info = &server->registration_info;

    if (move_endpoint_path && move_endpoint_path != &info->endpoint_path) {
        AVS_LIST_CLEAR(&info->endpoint_path);
        info->endpoint_path = *move_endpoint_path;
        *move_endpoint_path = NULL;
    }

    if (move_params) {
        move_assign_update_params(&info->last_update_params, move_params);
    }

    info->lwm2m_version = lwm2m_version;
    info->queue_mode = queue_mode;
    info->expire_time =
            get_registration_expire_time(info->last_update_params.lifetime_s);
    info->update_forced = false;
    info->session_token = _anjay_server_primary_session_token(server);
    // defined(ANJAY_WITH_CORE_PERSISTENCE)
}

static int
server_object_instances_count_clb(anjay_unlocked_t *anjay,
                                  const anjay_dm_installed_object_t *obj,
                                  anjay_iid_t iid,
                                  void *count_ptr) {
    (void) anjay;
    (void) obj;
    (void) iid;
    ++*(size_t *) count_ptr;
    return 0;
}

static size_t server_object_instances_count(anjay_unlocked_t *anjay) {
    const anjay_dm_installed_object_t *server_obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_SERVER);
    if (!server_obj) {
        return 0;
    }
    size_t count = 0;
    _anjay_dm_foreach_instance(anjay, server_obj,
                               server_object_instances_count_clb, &count);
    return count;
}

static bool server_state_stable(anjay_server_info_t *server) {
    if (server->ssid == ANJAY_SSID_BOOTSTRAP) {
        // Bootstrap server connection is considered stable if it's in the idle
        // state waiting for 1.0-style Server-Initiated Bootstrap. That state
        // does not expire.
        return !_anjay_bootstrap_scheduled(server->anjay);
    } else if (!_anjay_server_active(server)) {
        return false;
    } else {
        // Management servers connections are considered stable when they have
        // a valid, non-expired registration.
        return !_anjay_server_registration_expired(server);
    }
}

bool _anjay_ongoing_registration_exists_unlocked(anjay_unlocked_t *anjay) {
    if (_anjay_bootstrap_in_progress(anjay)) {
        return true;
    }

    size_t dm_servers_count = server_object_instances_count(anjay);
    if (dm_servers_count == 0) {
        return false;
    }

    size_t loaded_servers_count = 0;
    anjay_server_info_t *server;
    AVS_LIST_FOREACH(server, anjay->servers) {
        if (server->ssid != ANJAY_SSID_BOOTSTRAP) {
            loaded_servers_count++;
        }
    }

    if (dm_servers_count != loaded_servers_count) {
        return true;
    }

    AVS_LIST_FOREACH(server, anjay->servers) {
        if (!server->refresh_failed && !server_state_stable(server)) {
            return true;
        }
    }

    return false;
}

bool anjay_ongoing_registration_exists(anjay_t *anjay_locked) {
    bool result = false;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    result = _anjay_ongoing_registration_exists_unlocked(anjay);
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

avs_time_real_t anjay_registration_expiration_time(anjay_t *anjay_locked,
                                                   anjay_ssid_t ssid) {
    avs_time_real_t result = AVS_TIME_REAL_INVALID;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    anjay_server_info_t *server = _anjay_servers_find_active(anjay, ssid);
    if (server && !_anjay_server_registration_expired(server)) {
        result = _anjay_server_registration_info(server)->expire_time;
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

static avs_time_real_t
next_planned_lifecycle_operation(anjay_server_info_t *server) {
    // If the server is inactive, but scheduled for reactivation - return the
    // time at which it is scheduled. The additional server->next_action_handle
    // check is intended to filter out the case when the transport is offline
    // (see _anjay_server_on_refreshed(), ANJAY_SERVER_CONNECTION_OFFLINE case).
    bool server_active = _anjay_server_active(server);
    if (!server_active && server->next_action_handle
            && avs_time_real_valid(server->reactivate_time)) {
        return server->reactivate_time;
    }

    assert(server->anjay);
    if (server->ssid == ANJAY_SSID_BOOTSTRAP) {
        avs_time_real_t result = AVS_TIME_REAL_INVALID;
#ifdef ANJAY_WITH_BOOTSTRAP
        avs_time_monotonic_t client_initiated_bootstrap_time_monotonic =
                avs_sched_time(&server->anjay->bootstrap
                                        .client_initiated_bootstrap_handle);
        result = avs_time_real_add(
                avs_time_real_now(),
                avs_time_monotonic_diff(
                        client_initiated_bootstrap_time_monotonic,
                        avs_time_monotonic_now()));
#endif // ANJAY_WITH_BOOTSTRAP
        return result;
    } else if (_anjay_bootstrap_in_progress(server->anjay)) {
        return AVS_TIME_REAL_INVALID;
    } else if (server_active && server->registration_info.update_forced) {
        return avs_time_real_now();
    } else {
        return get_time_of_next_update(server);
    }
}

avs_time_real_t anjay_next_planned_lifecycle_operation(anjay_t *anjay_locked,
                                                       anjay_ssid_t ssid) {
    avs_time_real_t result = AVS_TIME_REAL_INVALID;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    if (ssid == ANJAY_SSID_ANY) {
        AVS_LIST(anjay_server_info_t) it;
        AVS_LIST_FOREACH(it, anjay->servers) {
            avs_time_real_t server_result =
                    next_planned_lifecycle_operation(it);
            if (!avs_time_real_valid(result)
                    || avs_time_real_before(server_result, result)) {
                result = server_result;
            }
        }
    } else {
        anjay_server_info_t *server = _anjay_servers_find(anjay, ssid);
        if (!server) {
            anjay_log(WARNING, _("no server with SSID = ") "%u", ssid);
        } else {
            result = next_planned_lifecycle_operation(server);
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

avs_time_real_t anjay_transport_next_planned_lifecycle_operation(
        anjay_t *anjay_locked, anjay_transport_set_t transport_set) {
    avs_time_real_t result = AVS_TIME_REAL_INVALID;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    AVS_LIST(anjay_server_info_t) it;
    AVS_LIST_FOREACH(it, anjay->servers) {
        anjay_server_connection_t *conn =
                _anjay_connection_get(&it->connections,
                                      ANJAY_CONNECTION_PRIMARY);
        if (conn->transport != ANJAY_SOCKET_TRANSPORT_INVALID
                && _anjay_socket_transport_included(transport_set,
                                                    conn->transport)) {
            avs_time_real_t server_result =
                    next_planned_lifecycle_operation(it);
            if (!avs_time_real_valid(result)
                    || avs_time_real_before(server_result, result)) {
                result = server_result;
            }
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

#ifdef ANJAY_WITH_COMMUNICATION_TIMESTAMP_API
avs_error_t anjay_get_server_last_registration_time(anjay_t *anjay,
                                                    anjay_ssid_t ssid,
                                                    avs_time_real_t *out_time) {
    assert(anjay);
    assert(out_time);

    avs_error_t err = avs_errno(AVS_EBUSY);
    *out_time = AVS_TIME_REAL_INVALID;

    ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);
    if (ssid == ANJAY_SSID_ANY) {
        if (!anjay_unlocked->servers) {
            anjay_log(WARNING, _("no servers found"));
            err = avs_errno(AVS_EEXIST);
        } else {
            AVS_LIST(anjay_server_info_t) it;
            AVS_LIST_FOREACH(it, anjay_unlocked->servers) {
                avs_time_real_t server_result =
                        it->registration_info.last_registration_time;
                if (!avs_time_real_valid(*out_time)
                        || avs_time_real_before(*out_time, server_result)) {
                    *out_time = server_result;
                }
            }
            err = AVS_OK;
        }
    } else if (ssid == ANJAY_SSID_BOOTSTRAP) {
        err = avs_errno(AVS_EINVAL);
    } else {
        anjay_server_info_t *server = _anjay_servers_find(anjay_unlocked, ssid);
        if (!server) {
            anjay_log(WARNING, _("no server with SSID = ") "%u", ssid);
            err = avs_errno(AVS_EEXIST);
        } else {
            *out_time = server->registration_info.last_registration_time;
            err = AVS_OK;
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay);

    return err;
}

avs_error_t anjay_get_server_next_update_time(anjay_t *anjay,
                                              anjay_ssid_t ssid,
                                              avs_time_real_t *out_time) {
    assert(anjay);
    assert(out_time);

    avs_error_t err = avs_errno(AVS_EBUSY);
    *out_time = AVS_TIME_REAL_INVALID;

    ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);
    if (ssid == ANJAY_SSID_ANY) {
        if (!anjay_unlocked->servers) {
            anjay_log(WARNING, _("no servers found"));
            err = avs_errno(AVS_EEXIST);
        } else {
            AVS_LIST(anjay_server_info_t) it;
            AVS_LIST_FOREACH(it, anjay_unlocked->servers) {
                avs_time_real_t server_result = get_time_of_next_update(it);
                if (!avs_time_real_valid(*out_time)
                        || avs_time_real_before(server_result, *out_time)) {
                    *out_time = server_result;
                }
            }
            err = AVS_OK;
        }
    } else if (ssid == ANJAY_SSID_BOOTSTRAP) {
        err = avs_errno(AVS_EINVAL);
    } else {
        anjay_server_info_t *server = _anjay_servers_find(anjay_unlocked, ssid);
        if (!server) {
            anjay_log(WARNING, _("no server with SSID = ") "%u", ssid);
            err = avs_errno(AVS_EEXIST);
        } else {
            *out_time = get_time_of_next_update(server);
            err = AVS_OK;
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay);

    return err;
}

avs_error_t anjay_get_server_last_communication_time(
        anjay_t *anjay, anjay_ssid_t ssid, avs_time_real_t *out_time) {
    assert(anjay);
    assert(out_time);

    avs_error_t err = avs_errno(AVS_EBUSY);
    *out_time = AVS_TIME_REAL_INVALID;

    ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);
    if (ssid == ANJAY_SSID_ANY) {
        if (!anjay_unlocked->servers) {
            anjay_log(WARNING, _("no servers found"));
            err = avs_errno(AVS_EEXIST);
        } else {
            AVS_LIST(anjay_server_info_t) it;
            AVS_LIST_FOREACH(it, anjay_unlocked->servers) {
                avs_time_real_t server_result = it->last_communication_time;
                if (!avs_time_real_valid(*out_time)
                        || avs_time_real_before(*out_time, server_result)) {
                    *out_time = server_result;
                }
            }
            err = AVS_OK;
        }
    } else if (ssid == ANJAY_SSID_BOOTSTRAP) {
        err = avs_errno(AVS_EINVAL);
    } else {
        anjay_server_info_t *server = _anjay_servers_find(anjay_unlocked, ssid);
        if (!server) {
            anjay_log(WARNING, _("no server with SSID = ") "%u", ssid);
            err = avs_errno(AVS_EEXIST);
        } else {
            *out_time = server->last_communication_time;
            err = AVS_OK;
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay);

    return err;
}
#endif // ANJAY_WITH_COMMUNICATION_TIMESTAMP_API
