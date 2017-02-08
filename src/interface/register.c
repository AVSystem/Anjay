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

#include <inttypes.h>

#include <avsystem/commons/stream.h>

#include <anjay_modules/time.h>

#include "register.h"
#include "../dm.h"
#include "../dm/query.h"
#include "../utils.h"
#include "../coap/msg.h"
#include "../coap/stream.h"

VISIBILITY_SOURCE_BEGIN

static AVS_LIST(const anjay_string_t)
get_endpoint_path(avs_stream_abstract_t *stream) {
    anjay_coap_opt_iterator_t it = ANJAY_COAP_OPT_ITERATOR_EMPTY;
    AVS_LIST(const anjay_string_t) path = NULL;

    int result;
    char buffer[ANJAY_MAX_URI_SEGMENT_SIZE];
    size_t attr_size;

    while ((result = _anjay_coap_stream_get_option_string_it(
                    stream, ANJAY_COAP_OPT_LOCATION_PATH, &it,
                    &attr_size, buffer, sizeof(buffer) - 1)) == 0) {
        buffer[attr_size] = '\0';

        AVS_LIST(anjay_string_t) segment =
                (AVS_LIST(anjay_string_t))AVS_LIST_NEW_BUFFER(attr_size + 1);
        if (!segment) {
            anjay_log(ERROR, "out of memory");
            goto fail;
        }

        memcpy(segment, buffer, attr_size + 1);
        AVS_LIST_APPEND(&path, segment);
    }

    if (result == ANJAY_COAP_OPTION_MISSING) {
        return path;
    }

fail:
    AVS_LIST_CLEAR(&path);
    return NULL;
}

static const char *assemble_endpoint_path(char *buffer,
                                          size_t buffer_size,
                                          AVS_LIST(const anjay_string_t) path) {
    size_t off = 0;
    AVS_LIST(const anjay_string_t) segment;
    AVS_LIST_FOREACH(segment, path) {
        ssize_t result = _anjay_snprintf(buffer + off, buffer_size - off,
                                         "/%s", segment->c_str);
        if (result < 0) {
            return "<ERROR>";
        }
        off += (size_t)result;
    }

    return buffer;
}

static int send_objects_list(avs_stream_abstract_t *stream,
                             AVS_LIST(anjay_dm_cache_object_t) dm) {
    // TODO: (LWM2M 5.2.1) </>;rt="oma.lwm2m";ct=100 when JSON is implemented
    bool is_first_path = true;

    anjay_dm_cache_object_t *object;
    AVS_LIST_FOREACH(object, dm) {
        if (object->oid == ANJAY_DM_OID_SECURITY) {
            /* LWM2M spec, 2016-09-08 update says that Register/Update must not
             * include Security object instances */
            continue;
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
        } else {
            int result = avs_stream_write_f(stream, "%s</%u>",
                                            is_first_path ? "" : ",",
                                            object->oid);
            if (result) {
                return result;
            }
            is_first_path = false;
        }
    }
    return 0;
}

static int get_server_lifetime(anjay_t *anjay,
                               anjay_ssid_t ssid,
                               int64_t *out_lifetime) {
    anjay_iid_t server_iid;
    if (_anjay_find_server_iid(anjay, ssid, &server_iid)) {
        return -1;
    }

    const anjay_resource_path_t path = {
        ANJAY_DM_OID_SERVER, server_iid, ANJAY_DM_RID_SERVER_LIFETIME
    };

    int64_t lifetime;
    int read_ret = _anjay_dm_res_read_i64(anjay, &path, &lifetime);

    if (read_ret) {
        anjay_log(ERROR, "could not read lifetime for LWM2M server %u", ssid);
        return -1;
    } else if (lifetime <= 0) {
        anjay_log(ERROR, "lifetime returned by LWM2M server %u is <= 0", ssid);
        return -1;
    }
    *out_lifetime = lifetime;

    return 0;
}

static int send_register(avs_stream_abstract_t *stream,
                         const char *endpoint_name,
                         const anjay_update_parameters_t *params) {
    anjay_msg_details_t details = {
        .msg_type = ANJAY_COAP_MSG_CONFIRMABLE,
        .msg_code = ANJAY_COAP_CODE_POST,
        .format = ANJAY_COAP_FORMAT_APPLICATION_LINK,
        .uri_path = _anjay_make_string_list("rd", NULL),
        .uri_query = _anjay_make_query_string_list(
                ANJAY_SUPPORTED_ENABLER_VERSION, endpoint_name,
                &params->lifetime_s,
                params->binding_mode == ANJAY_BINDING_U ? ANJAY_BINDING_NONE
                                                        : params->binding_mode)
    // TODO: support SMS number updates
    };

    int result = -1;
    if (!details.uri_path || !details.uri_query) {
        anjay_log(ERROR, "could not initialize request headers");
        goto cleanup;
    }

    if (_anjay_coap_stream_setup_request(stream, &details, NULL, 0)
            || send_objects_list(stream, params->dm)
            || avs_stream_finish_message(stream)) {
        anjay_log(ERROR, "could not send Register message");
    } else {
        anjay_log(INFO, "Register sent");
        result = 0;
    }

cleanup:
    AVS_LIST_CLEAR(&details.uri_path);
    AVS_LIST_CLEAR(&details.uri_query);
    return result;
}

static int
check_register_response(avs_stream_abstract_t *stream,
                        AVS_LIST(const anjay_string_t) *out_endpoint_path) {
    uint8_t response_code;
    if (_anjay_coap_stream_get_code(stream, &response_code)) {
        anjay_log(ERROR, "could not get response code");
        return -1;
    }

    if (response_code != ANJAY_COAP_CODE_CREATED) {
        anjay_log(ERROR, "server responded with %s (expected %s)",
                  ANJAY_COAP_CODE_STRING(response_code),
                  ANJAY_COAP_CODE_STRING(ANJAY_COAP_CODE_CREATED));
        return -1;
    }

    AVS_LIST(const anjay_string_t) endpoint_path = get_endpoint_path(stream);
    if (!endpoint_path) {
        anjay_log(ERROR, "server did not specify a location");
        return -1;
    }
    if (strcmp(endpoint_path->c_str, "rd")) {
        anjay_log(ERROR, "returned location does not start with 'rd' segment "
                  "(got '%s')", endpoint_path->c_str);
        AVS_LIST_CLEAR(&endpoint_path);
        return -1;
    }

    if (*out_endpoint_path) {
        AVS_LIST_CLEAR(out_endpoint_path);
    }
    *out_endpoint_path = endpoint_path;

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
    (void) anjay; (void) obj;
    AVS_LIST(anjay_iid_t) **cache_instance_insert_ptr =
            (AVS_LIST(anjay_iid_t) **) cache_instance_insert_ptr_;

    AVS_LIST(anjay_iid_t) new_instance = AVS_LIST_NEW_ELEMENT(anjay_iid_t);
    if (!new_instance) {
        anjay_log(ERROR, "out of memory");
        return -1;
    }
    AVS_LIST_INSERT(*cache_instance_insert_ptr, new_instance);
    *cache_instance_insert_ptr = AVS_LIST_NEXT_PTR(*cache_instance_insert_ptr);

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
    AVS_LIST(anjay_dm_cache_object_t) **cache_object_insert_ptr =
            (AVS_LIST(anjay_dm_cache_object_t) **) cache_object_insert_ptr_;

    AVS_LIST(anjay_dm_cache_object_t) new_object =
            AVS_LIST_NEW_ELEMENT(anjay_dm_cache_object_t);
    if (!new_object) {
        anjay_log(ERROR, "out of memory");
        return -1;
    }
    AVS_LIST_INSERT(*cache_object_insert_ptr, new_object);
    *cache_object_insert_ptr = AVS_LIST_NEXT_PTR(*cache_object_insert_ptr);

    new_object->oid = (*obj)->oid;
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

static struct timespec get_registration_expire_time(int64_t lifetime_s) {
    struct timespec expire_time;
    clock_gettime(CLOCK_MONOTONIC, &expire_time);
    expire_time.tv_sec += (time_t) lifetime_s;
    return expire_time;
}

static void cleanup_update_parameters(anjay_update_parameters_t *params) {
    clear_dm_cache(&params->dm);
}

static int init_update_parameters(anjay_t *anjay,
                                  const anjay_active_server_info_t *server,
                                  anjay_update_parameters_t *out_params) {
    if (query_dm(anjay, &out_params->dm)) {
        goto error;
    }
    if (get_server_lifetime(anjay, server->ssid, &out_params->lifetime_s)) {
        goto error;
    }
    out_params->binding_mode = _anjay_server_cached_binding_mode(server);
    if (out_params->binding_mode == ANJAY_BINDING_NONE) {
        goto error;
    }
    return 0;
error:
    cleanup_update_parameters(out_params);
    return -1;
}

static void
update_registration_info(anjay_registration_info_t *info,
                         anjay_update_parameters_t *move_params) {
    clear_dm_cache(&info->last_update_params.dm);
    info->last_update_params.dm = move_params->dm;
    move_params->dm = NULL;

    assert(move_params->lifetime_s >= 0);
    info->last_update_params.lifetime_s = move_params->lifetime_s;
    info->last_update_params.binding_mode = move_params->binding_mode;

    info->expire_time =
            get_registration_expire_time(info->last_update_params.lifetime_s);
}

static void
registration_info_init(anjay_registration_info_t *info,
                       AVS_LIST(const anjay_string_t) *move_endpoint_path,
                       anjay_update_parameters_t *move_params) {
    update_registration_info(info, move_params);

    info->endpoint_path = *move_endpoint_path;
    *move_endpoint_path = NULL;
}

void _anjay_registration_info_cleanup(anjay_registration_info_t *info) {
    AVS_LIST_CLEAR(&info->endpoint_path);
    cleanup_update_parameters(&info->last_update_params);
}

int _anjay_register(anjay_t *anjay,
                    avs_stream_abstract_t *stream,
                    anjay_active_server_info_t *server,
                    const char *endpoint_name) {
    anjay_update_parameters_t new_params;
    if (init_update_parameters(anjay, server, &new_params)) {
        return -1;
    }

    AVS_LIST(const anjay_string_t) endpoint_path = NULL;
    int result = -1;

    if (send_register(stream, endpoint_name, &new_params)
            || check_register_response(stream, &endpoint_path)) {
        anjay_log(ERROR, "could not register to server %u", server->ssid);
        goto fail;
    }

    _anjay_registration_info_cleanup(&server->registration_info);
    registration_info_init(&server->registration_info,
                           &endpoint_path, &new_params);
    result = 0;

fail:
    cleanup_update_parameters(&new_params);
    AVS_LIST_CLEAR(&endpoint_path);
    return result;
}

static bool iid_lists_equal(AVS_LIST(anjay_iid_t) left,
                            AVS_LIST(anjay_iid_t) right) {
    while (left && right) {
        if (*left != *right) {
            return false;
        }
        left = AVS_LIST_NEXT(left);
        right = AVS_LIST_NEXT(right);
    }
    return !(left || right);
}

static bool dm_caches_equal(AVS_LIST(anjay_dm_cache_object_t) left,
                            AVS_LIST(anjay_dm_cache_object_t) right) {
    while (left && right) {
        if (left->oid != right->oid
                || !iid_lists_equal(left->instances, right->instances)) {
            return false;
        }
        left = AVS_LIST_NEXT(left);
        right = AVS_LIST_NEXT(right);
    }
    return !(left || right);
}

static int send_update(avs_stream_abstract_t *stream,
                       AVS_LIST(const anjay_string_t) endpoint_path,
                       const anjay_update_parameters_t *old_params,
                       const anjay_update_parameters_t *new_params) {
    const int64_t *lifetime_s_ptr = NULL;
    assert(new_params->lifetime_s >= 0);
    if (new_params->lifetime_s != old_params->lifetime_s) {
        lifetime_s_ptr = &new_params->lifetime_s;
    }

    anjay_binding_mode_t binding_mode =
            (old_params->binding_mode == new_params->binding_mode)
                    ? ANJAY_BINDING_NONE : new_params->binding_mode;

    bool dm_changed_since_last_update =
            !dm_caches_equal(old_params->dm, new_params->dm);
    anjay_msg_details_t details = {
        .msg_type = ANJAY_COAP_MSG_CONFIRMABLE,
        .msg_code = ANJAY_COAP_CODE_POST,
        .format = dm_changed_since_last_update
                    ? ANJAY_COAP_FORMAT_APPLICATION_LINK
                    : ANJAY_COAP_FORMAT_NONE,
        .uri_path = endpoint_path,
        .uri_query = _anjay_make_query_string_list(NULL, NULL, lifetime_s_ptr,
                                                   binding_mode)
        // TODO: support SMS binding modes and number updates
    };

    int result = -1;
    if (_anjay_coap_stream_setup_request(stream, &details, NULL, 0)
            || (dm_changed_since_last_update
                && send_objects_list(stream, new_params->dm))
            || avs_stream_finish_message(stream)) {
        anjay_log(ERROR, "could not send Update message");
    } else {
        anjay_log(INFO, "Update sent");
        result = 0;
    }

    // request_uri must not be cleared here
    AVS_LIST_CLEAR(&details.uri_query);
    return result;
}

static int check_update_response(avs_stream_abstract_t *stream) {
    uint8_t response_code;
    if (_anjay_coap_stream_get_code(stream, &response_code)) {
        anjay_log(ERROR, "could not get response code");
        return -1;
    }

    if (response_code == ANJAY_COAP_CODE_CHANGED) {
        anjay_log(INFO, "registration successfully updated");
        return 0;
    } else if (_anjay_coap_msg_code_is_client_error(response_code)) {
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
         * replace client registration information in such case. */
        anjay_log(DEBUG, "Update rejected: %s",
                  ANJAY_COAP_CODE_STRING(response_code));
        return ANJAY_REGISTRATION_UPDATE_REJECTED;
    } else {
        /* Any other response is either an 5.xx (server error), in which case
         * retransmission may succeed, or an unexpected non-error response.
         * In the latter case the server is broken and deserves to be flooded
         * with retransmitted Updates until the server implementer notices
         * something is wrong. */
        anjay_log(ERROR, "server responded with %s (expected %s)",
                  ANJAY_COAP_CODE_STRING(response_code),
                  ANJAY_COAP_CODE_STRING(ANJAY_COAP_CODE_CHANGED));
        return -1;
    }
}

int _anjay_update_registration(anjay_t *anjay,
                               avs_stream_abstract_t *stream,
                               anjay_active_server_info_t *server) {
    anjay_update_parameters_t new_params;
    if (init_update_parameters(anjay, server, &new_params)) {
        return -1;
    }

    int retval = -1;
    if ((retval = send_update(stream, server->registration_info.endpoint_path,
                              &server->registration_info.last_update_params,
                              &new_params))
            || (retval = check_update_response(stream))) {
        anjay_log(ERROR, "could not update registration");
        goto finish;
    }

    update_registration_info(&server->registration_info, &new_params);
    retval = 0;

finish:
    cleanup_update_parameters(&new_params);
    return retval;
}

static int check_deregister_response(avs_stream_abstract_t *stream) {
    uint8_t response_code;
    if (_anjay_coap_stream_get_code(stream, &response_code)) {
        anjay_log(ERROR, "could not get response code");
        return -1;
    }

    if (response_code != ANJAY_COAP_CODE_DELETED) {
        anjay_log(ERROR, "server responded with %s (expected %s)",
                  ANJAY_COAP_CODE_STRING(response_code),
                  ANJAY_COAP_CODE_STRING(ANJAY_COAP_CODE_DELETED));
        return -1;
    }

    return 0;
}

int _anjay_deregister(avs_stream_abstract_t *stream,
                      const anjay_registration_info_t *registration_info) {
    anjay_msg_details_t details = {
        .msg_type = ANJAY_COAP_MSG_CONFIRMABLE,
        .msg_code = ANJAY_COAP_CODE_DELETE,
        .format = ANJAY_COAP_FORMAT_NONE,
        .uri_path = registration_info->endpoint_path
    };

    int result;
    if ((result = _anjay_coap_stream_setup_request(stream, &details, NULL, 0))
            || (result = avs_stream_finish_message(stream))
            || (result = check_deregister_response(stream))) {
        anjay_log(ERROR, "Could not perform De-registration");
    } else {
        anjay_log(INFO, "De-register sent");
    }
    return result;
}

struct timespec
_anjay_register_time_remaining(const anjay_registration_info_t *info) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    struct timespec diff;
    _anjay_time_diff(&diff, &info->expire_time, &now);

    return diff;
}
