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

#include <inttypes.h>

#include <avsystem/commons/coap/msg.h>
#include <avsystem/commons/coap/msg_opt.h>
#include <avsystem/commons/stream.h>
#include <avsystem/commons/utils.h>

#include <anjay_modules/time_defs.h>

#include "../dm/query.h"
#include "../dm_core.h"
#include "../servers_utils.h"
#include "../utils_core.h"
#include "register.h"

#include "../coap/coap_stream.h"
#include "../coap/content_format.h"

VISIBILITY_SOURCE_BEGIN

static int get_endpoint_path(AVS_LIST(const anjay_string_t) *out_path,
                             const avs_coap_msg_t *msg) {
    assert(*out_path == NULL);

    int result;
    char buffer[ANJAY_MAX_URI_SEGMENT_SIZE];
    size_t attr_size;

    avs_coap_opt_iterator_t it = AVS_COAP_OPT_ITERATOR_EMPTY;
    while ((result = avs_coap_msg_get_option_string_it(
                    msg, AVS_COAP_OPT_LOCATION_PATH, &it, &attr_size, buffer,
                    sizeof(buffer) - 1))
           == 0) {
        buffer[attr_size] = '\0';

        AVS_LIST(anjay_string_t) segment =
                (AVS_LIST(anjay_string_t)) AVS_LIST_NEW_BUFFER(attr_size + 1);
        if (!segment) {
            anjay_log(ERROR, "out of memory");
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
        ssize_t result = avs_simple_snprintf(buffer + off, buffer_size - off,
                                             "/%s", segment->c_str);
        if (result < 0) {
            return "<ERROR>";
        }
        off += (size_t) result;
    }

    return buffer;
}

static int send_objects_list(avs_stream_abstract_t *stream,
                             AVS_LIST(anjay_dm_cache_object_t) dm) {
    // TODO: (LwM2M 5.2.1) </>;rt="oma.lwm2m";ct=100 when JSON is implemented
    bool is_first_path = true;

    anjay_dm_cache_object_t *object;
    AVS_LIST_FOREACH(object, dm) {
        if (*object->version || !object->instances) {
            int result =
                    avs_stream_write_f(stream, "%s</%u>",
                                       is_first_path ? "" : ",", object->oid);
            if (result) {
                return result;
            }
            if (*object->version) {
                result = avs_stream_write_f(stream, ";ver=\"%s\"",
                                            object->version);
                if (result) {
                    return result;
                }
            }

            is_first_path = false;
        }

        if (object->instances) {
            anjay_iid_t *iid;
            AVS_LIST_FOREACH(iid, object->instances) {
                int result = avs_stream_write_f(stream, "%s</%u/%u>",
                                                is_first_path ? "" : ",",
                                                object->oid, *iid);
                if (result) {
                    return result;
                }
                is_first_path = false;
            }
        }
    }
    return 0;
}

static int
get_server_lifetime(anjay_t *anjay, anjay_ssid_t ssid, int64_t *out_lifetime) {
    anjay_iid_t server_iid;
    if (_anjay_find_server_iid(anjay, ssid, &server_iid)) {
        return -1;
    }

    const anjay_uri_path_t path =
            MAKE_RESOURCE_PATH(ANJAY_DM_OID_SERVER, server_iid,
                               ANJAY_DM_RID_SERVER_LIFETIME);
    int64_t lifetime;
    int read_ret = _anjay_dm_res_read_i64(anjay, &path, &lifetime);

    if (read_ret) {
        anjay_log(ERROR, "could not read lifetime for LwM2M server %u", ssid);
        return -1;
    } else if (lifetime <= 0) {
        anjay_log(ERROR, "lifetime returned by LwM2M server %u is <= 0", ssid);
        return -1;
    }
    *out_lifetime = lifetime;

    return 0;
}

static int send_register(anjay_t *anjay,
                         const anjay_update_parameters_t *params) {
    const anjay_url_t *const connection_uri =
            _anjay_connection_uri(anjay->current_connection);
    anjay_msg_details_t details = {
        .msg_type = AVS_COAP_MSG_CONFIRMABLE,
        .msg_code = AVS_COAP_CODE_POST,
        .format = ANJAY_COAP_FORMAT_APPLICATION_LINK
    };

    int result = -1;
    if (_anjay_copy_string_list(&details.uri_path, connection_uri->uri_path)
            || _anjay_copy_string_list(&details.uri_query,
                                       connection_uri->uri_query)
            || !AVS_LIST_APPEND(&details.uri_path, ANJAY_MAKE_STRING_LIST("rd"))
            || !AVS_LIST_APPEND(&details.uri_query,
                                _anjay_make_query_string_list(
                                        ANJAY_SUPPORTED_ENABLER_VERSION,
                                        anjay->endpoint_name,
                                        &params->lifetime_s,
                                        strcmp(params->binding_mode, "U") == 0
                                                ? NULL
                                                : params->binding_mode,
                                        _anjay_local_msisdn(anjay)))) {
        anjay_log(ERROR, "could not initialize request headers");
        goto cleanup;
    }

    if ((result = _anjay_coap_stream_setup_request(anjay->comm_stream, &details,
                                                   NULL))
            || (result = send_objects_list(anjay->comm_stream, params->dm))
            || (result = avs_stream_finish_message(anjay->comm_stream))) {
        anjay_log(ERROR, "could not send Register message");
    } else {
        anjay_log(INFO, "Register sent");
    }

cleanup:
    AVS_LIST_CLEAR(&details.uri_path);
    AVS_LIST_CLEAR(&details.uri_query);
    return result;
}

static int
check_register_response(avs_stream_abstract_t *stream,
                        AVS_LIST(const anjay_string_t) *out_endpoint_path) {
    const avs_coap_msg_t *response;
    if (_anjay_coap_stream_get_incoming_msg(stream, &response)) {
        anjay_log(ERROR, "could not get response");
        return -1;
    }

    if (avs_coap_msg_get_code(response) != AVS_COAP_CODE_CREATED) {
        anjay_log(ERROR, "server responded with %s (expected %s)",
                  AVS_COAP_CODE_STRING(avs_coap_msg_get_code(response)),
                  AVS_COAP_CODE_STRING(AVS_COAP_CODE_CREATED));
        return -avs_coap_msg_get_code(response);
    }

    AVS_LIST_CLEAR(out_endpoint_path);
    if (get_endpoint_path(out_endpoint_path, response)) {
        anjay_log(ERROR, "could not store Update location");
        return -1;
    }

    char location_buf[256];
    anjay_log(INFO, "registration successful, location = %s",
              assemble_endpoint_path(location_buf, sizeof(location_buf),
                                     *out_endpoint_path));

    return 0;
}

static void clear_dm_cache(AVS_LIST(anjay_dm_cache_object_t) *cache_ptr) {
    AVS_LIST_CLEAR(cache_ptr) {
        AVS_LIST_CLEAR(&(*cache_ptr)->instances);
    }
}

static int query_dm_instance(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj,
                             anjay_iid_t iid,
                             void *cache_instance_insert_ptr_) {
    (void) anjay;
    (void) obj;
    AVS_LIST(anjay_iid_t) **cache_instance_insert_ptr =
            (AVS_LIST(anjay_iid_t) **) cache_instance_insert_ptr_;

    AVS_LIST(anjay_iid_t) new_instance = AVS_LIST_NEW_ELEMENT(anjay_iid_t);
    if (!new_instance) {
        anjay_log(ERROR, "out of memory");
        return -1;
    }
    AVS_LIST_INSERT(*cache_instance_insert_ptr, new_instance);
    AVS_LIST_ADVANCE_PTR(cache_instance_insert_ptr);

    *new_instance = iid;
    return 0;
}

static int compare_iids(const void *left_, const void *right_, size_t size) {
    (void) size;
    anjay_iid_t left = *(const anjay_iid_t *) left_;
    anjay_iid_t right = *(const anjay_iid_t *) right_;
    if (left < right) {
        return -1;
    } else if (left == right) {
        return 0;
    } else {
        return 1;
    }
}

static int query_dm_object(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *obj,
                           void *cache_object_insert_ptr_) {
    if ((*obj)->oid == ANJAY_DM_OID_SECURITY) {
        /* LwM2M spec, 2016-09-08 update says that Register/Update must not
         * include Security object instances */
        return 0;
    }

    AVS_LIST(anjay_dm_cache_object_t) **cache_object_insert_ptr =
            (AVS_LIST(anjay_dm_cache_object_t) **) cache_object_insert_ptr_;

    AVS_LIST(anjay_dm_cache_object_t) new_object =
            AVS_LIST_NEW_ELEMENT(anjay_dm_cache_object_t);
    if (!new_object) {
        anjay_log(ERROR, "out of memory");
        return -1;
    }
    AVS_LIST_INSERT(*cache_object_insert_ptr, new_object);
    AVS_LIST_ADVANCE_PTR(cache_object_insert_ptr);

    new_object->oid = (*obj)->oid;
    if ((*obj)->version
            && avs_simple_snprintf(new_object->version,
                                   sizeof(new_object->version), "%s",
                                   (*obj)->version)
                           < 0) {
        return -1;
    }
    AVS_LIST(anjay_iid_t) *instance_insert_ptr = &new_object->instances;
    int retval = _anjay_dm_foreach_instance(anjay, obj, query_dm_instance,
                                            &instance_insert_ptr);
    if (!retval) {
        AVS_LIST_SORT(&new_object->instances, compare_iids);
    }
    return retval;
}

static int query_dm(anjay_t *anjay, AVS_LIST(anjay_dm_cache_object_t) *out) {
    *out = NULL;
    AVS_LIST(anjay_dm_cache_object_t) *insert_ptr = out;
    int retval = _anjay_dm_foreach_object(anjay, query_dm_object, &insert_ptr);
    if (retval) {
        anjay_log(ERROR, "could not enumerate objects");
        clear_dm_cache(out);
    }
    // objects in Anjay DM are kept sorted, there's no need to sort here
    return retval;
}

void _anjay_update_parameters_cleanup(anjay_update_parameters_t *params) {
    clear_dm_cache(&params->dm);
}

static int init_update_parameters(anjay_t *anjay,
                                  anjay_server_info_t *server,
                                  anjay_update_parameters_t *out_params) {
    if (query_dm(anjay, &out_params->dm)) {
        goto error;
    }
    if (get_server_lifetime(anjay, _anjay_server_ssid(server),
                            &out_params->lifetime_s)) {
        goto error;
    }
    _anjay_server_actual_binding_mode(&out_params->binding_mode, server);
    return 0;
error:
    _anjay_update_parameters_cleanup(out_params);
    return -1;
}

void _anjay_registration_info_cleanup(anjay_registration_info_t *info) {
    AVS_LIST_CLEAR(&info->endpoint_path);
    _anjay_update_parameters_cleanup(&info->last_update_params);
}

int _anjay_registration_update_ctx_init(
        anjay_t *anjay,
        anjay_registration_update_ctx_t *out_ctx,
        anjay_server_info_t *server) {
    assert(!anjay->current_connection.server);
    memset(out_ctx, 0, sizeof(*out_ctx));
    out_ctx->anjay = anjay;
    out_ctx->server = server;
    return init_update_parameters(anjay, server, &out_ctx->new_params);
}

static int bind_server_stream(anjay_registration_update_ctx_t *ctx) {
    assert(ctx->server);
    anjay_connection_ref_t connection = {
        .server = ctx->server,
        .conn_type = _anjay_server_primary_conn_type(ctx->server)
    };
    if (connection.conn_type == ANJAY_CONNECTION_UNSET) {
        anjay_log(ERROR, "no valid registration connection for server %u",
                  _anjay_server_ssid(ctx->server));
        return -1;
    }
    if (_anjay_bind_server_stream(ctx->anjay, connection)) {
        anjay_log(ERROR, "could not get stream for server %u",
                  _anjay_server_ssid(ctx->server));
        return -1;
    }
    return 0;
}

int _anjay_register(anjay_registration_update_ctx_t *ctx) {
    AVS_LIST(const anjay_string_t) endpoint_path = NULL;
    if (bind_server_stream(ctx)) {
        return -1;
    }

    int result;
    if ((result = send_register(ctx->anjay, &ctx->new_params))
            || (result = check_register_response(ctx->anjay->comm_stream,
                                                 &endpoint_path))) {
        anjay_log(ERROR, "could not register to server %u",
                  _anjay_dm_current_ssid(ctx->anjay));
        AVS_LIST_CLEAR(&endpoint_path);
        goto finish;
    }

    _anjay_server_update_registration_info(
            ctx->anjay->current_connection.server, &endpoint_path,
            &ctx->new_params);
    assert(!endpoint_path);

finish:
    _anjay_release_server_stream(ctx->anjay);
    return result;
}

static bool iid_lists_equal(AVS_LIST(anjay_iid_t) left,
                            AVS_LIST(anjay_iid_t) right) {
    while (left && right) {
        if (*left != *right) {
            return false;
        }
        AVS_LIST_ADVANCE(&left);
        AVS_LIST_ADVANCE(&right);
    }
    return !(left || right);
}

static bool dm_caches_equal(AVS_LIST(anjay_dm_cache_object_t) left,
                            AVS_LIST(anjay_dm_cache_object_t) right) {
    while (left && right) {
        if (left->oid != right->oid
                || strcmp(left->version, right->version) != 0
                || !iid_lists_equal(left->instances, right->instances)) {
            return false;
        }
        AVS_LIST_ADVANCE(&left);
        AVS_LIST_ADVANCE(&right);
    }
    return !(left || right);
}

static int send_update(anjay_t *anjay,
                       AVS_LIST(const anjay_string_t) endpoint_path,
                       const anjay_update_parameters_t *old_params,
                       const anjay_update_parameters_t *new_params) {
    const int64_t *lifetime_s_ptr = NULL;
    assert(new_params->lifetime_s >= 0);
    if (new_params->lifetime_s != old_params->lifetime_s) {
        lifetime_s_ptr = &new_params->lifetime_s;
    }

    const char *binding_mode =
            (strcmp(old_params->binding_mode, new_params->binding_mode) == 0)
                    ? NULL
                    : new_params->binding_mode;

    bool dm_changed_since_last_update =
            !dm_caches_equal(old_params->dm, new_params->dm);
    anjay_msg_details_t details = {
        .msg_type = AVS_COAP_MSG_CONFIRMABLE,
        .msg_code = AVS_COAP_CODE_POST,
        .format = dm_changed_since_last_update
                          ? ANJAY_COAP_FORMAT_APPLICATION_LINK
                          : AVS_COAP_FORMAT_NONE,
        .uri_path = endpoint_path,
        .uri_query = _anjay_make_query_string_list(NULL, NULL, lifetime_s_ptr,
                                                   binding_mode, NULL)
    };

    int result = -1;
    if ((result = _anjay_coap_stream_setup_request(anjay->comm_stream, &details,
                                                   NULL))
            || (dm_changed_since_last_update
                && (result = send_objects_list(anjay->comm_stream,
                                               new_params->dm)))
            || (result = avs_stream_finish_message(anjay->comm_stream))) {
        anjay_log(ERROR, "could not send Update message");
    } else {
        anjay_log(INFO, "Update sent");
    }

    // request_uri must not be cleared here
    AVS_LIST_CLEAR(&details.uri_query);
    return result;
}

static int check_update_response(avs_stream_abstract_t *stream) {
    const avs_coap_msg_t *response;
    if (_anjay_coap_stream_get_incoming_msg(stream, &response)) {
        anjay_log(ERROR, "could not get response");
        return -1;
    }

    const uint8_t code = avs_coap_msg_get_code(response);
    if (code == AVS_COAP_CODE_CHANGED) {
        anjay_log(INFO, "registration successfully updated");
        return 0;
    } else {
        /* 4.xx (client error) response means that a server received a request
         * it considers invalid, so retransmission of the same message will
         * most likely fail again. That may happen if:
         * - the registration already expired (4.04 Not Found response),
         * - the server is unable to parse our Update request or unwilling
         *   to process it,
         * - the server is broken.
         *
         * In the first case, the correct response is to Register again.
         * Otherwise, we might as well do the same, as server is required to
         * replace client registration information in such case.
         *
         * Any other response is either an 5.xx (server error), in which case
         * retransmission may succeed, or an unexpected non-error response.
         * However, as we don't do retransmissions, degenerating to Register
         * seems the best thing we can do. */
        anjay_log(DEBUG, "Update rejected: %s (expected %s)",
                  AVS_COAP_CODE_STRING(code),
                  AVS_COAP_CODE_STRING(AVS_COAP_CODE_CHANGED));
        return ANJAY_REGISTRATION_UPDATE_REJECTED;
    }
}

bool _anjay_needs_registration_update(anjay_registration_update_ctx_t *ctx) {
    const anjay_registration_info_t *info =
            _anjay_server_registration_info(ctx->server);
    const anjay_update_parameters_t *old_params = &info->last_update_params;
    return info->update_forced
           || old_params->lifetime_s != ctx->new_params.lifetime_s
           || strcmp(old_params->binding_mode, ctx->new_params.binding_mode)
           || !dm_caches_equal(old_params->dm, ctx->new_params.dm);
}

int _anjay_update_registration(anjay_registration_update_ctx_t *ctx) {
    if (bind_server_stream(ctx)) {
        return -1;
    }
    const anjay_registration_info_t *old_info = _anjay_server_registration_info(
            ctx->anjay->current_connection.server);
    int retval = -1;
    if ((retval = send_update(ctx->anjay, old_info->endpoint_path,
                              &old_info->last_update_params, &ctx->new_params))
            || (retval = check_update_response(ctx->anjay->comm_stream))) {
        anjay_log(ERROR, "could not update registration");
        goto finish;
    }

    _anjay_server_update_registration_info(
            ctx->anjay->current_connection.server, NULL, &ctx->new_params);

finish:
    _anjay_release_server_stream(ctx->anjay);
    return retval;
}

void _anjay_registration_update_ctx_release(
        anjay_registration_update_ctx_t *ctx) {
    _anjay_update_parameters_cleanup(&ctx->new_params);
}

static int check_deregister_response(avs_stream_abstract_t *stream) {
    const avs_coap_msg_t *response;
    if (_anjay_coap_stream_get_incoming_msg(stream, &response)) {
        anjay_log(ERROR, "could not get response");
        return -1;
    }

    const uint8_t code = avs_coap_msg_get_code(response);
    if (code != AVS_COAP_CODE_DELETED) {
        anjay_log(ERROR, "server responded with %s (expected %s)",
                  AVS_COAP_CODE_STRING(code),
                  AVS_COAP_CODE_STRING(AVS_COAP_CODE_DELETED));
        return -1;
    }

    return 0;
}

int _anjay_deregister(anjay_t *anjay,
                      AVS_LIST(const anjay_string_t) endpoint_path) {
    anjay_msg_details_t details = {
        .msg_type = AVS_COAP_MSG_CONFIRMABLE,
        .msg_code = AVS_COAP_CODE_DELETE,
        .format = AVS_COAP_FORMAT_NONE,
        .uri_path = endpoint_path
    };

    int result;
    if ((result = _anjay_coap_stream_setup_request(anjay->comm_stream, &details,
                                                   NULL))
            || (result = avs_stream_finish_message(anjay->comm_stream))
            || (result = check_deregister_response(anjay->comm_stream))) {
        anjay_log(ERROR, "Could not perform De-registration");
    } else {
        anjay_log(INFO, "De-register sent");
    }
    return result;
}

avs_time_duration_t
_anjay_register_time_remaining(const anjay_registration_info_t *info) {
    return avs_time_real_diff(info->expire_time, avs_time_real_now());
}
