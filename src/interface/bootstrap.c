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

#include <anjay_modules/notify.h>
#include <anjay_modules/interface/bootstrap.h>

#include "bootstrap.h"
#include "../anjay.h"
#include "../io.h"
#include "../dm/discover.h"
#include "../dm/query.h"
#include "anjay_modules/time.h"

#ifdef ANJAY_TEST
#include "test/bootstrap_mock.h"
#endif // ANJAY_TEST

VISIBILITY_SOURCE_BEGIN

static void cancel_client_initiated_bootstrap(anjay_t *anjay) {
    anjay->bootstrap.client_initiated_bootstrap_scheduled = true;
    _anjay_sched_del(anjay->sched,
                     &anjay->bootstrap.client_initiated_bootstrap_handle);
}

static void start_bootstrap_if_not_already_started(anjay_t *anjay) {
    if (!anjay->bootstrap.in_progress) {
        _anjay_dm_transaction_begin(anjay);
        anjay->bootstrap.in_progress = true;
    }
}

static int commit_bootstrap(anjay_t *anjay) {
    int result = 0;
    if (anjay->bootstrap.in_progress) {
        if (_anjay_dm_transaction_validate(anjay)) {
            result = ANJAY_ERR_NOT_ACCEPTABLE;
        } else {
            anjay->bootstrap.in_progress = false;
            result = _anjay_dm_transaction_finish_without_validation(anjay, 0);
        }
    }
    return result;
}

static void abort_bootstrap(anjay_t *anjay) {
    if (anjay->bootstrap.in_progress) {
        _anjay_dm_transaction_rollback(anjay);
        anjay->bootstrap.in_progress = false;
    }
}

static void bootstrap_remove_notify_changed(anjay_t *anjay,
                                            anjay_oid_t oid,
                                            anjay_iid_t iid) {
    AVS_LIST(anjay_notify_queue_object_entry_t) *obj_it;
    AVS_LIST_FOREACH_PTR(obj_it, &anjay->bootstrap.notification_queue) {
        if ((*obj_it)->oid > oid) {
            return;
        } else if ((*obj_it)->oid == oid) {
            break;
        }
    }
    if (!obj_it || !*obj_it) {
        return;
    }
    AVS_LIST(anjay_notify_queue_resource_entry_t) *res_it;
    AVS_LIST_FOREACH_PTR(res_it, &(*obj_it)->resources_changed) {
        if ((*res_it)->iid >= iid) {
            break;
        }
    }
    while (res_it && *res_it && (*res_it)->iid == iid) {
        AVS_LIST_DELETE(res_it);
    }
}

static uint8_t make_success_response_code(anjay_request_action_t action) {
    switch (action) {
    case ANJAY_ACTION_WRITE:            return ANJAY_COAP_CODE_CHANGED;
    case ANJAY_ACTION_DELETE:           return ANJAY_COAP_CODE_DELETED;
    case ANJAY_ACTION_DISCOVER:         return ANJAY_COAP_CODE_CONTENT;
    case ANJAY_ACTION_BOOTSTRAP_FINISH: return ANJAY_COAP_CODE_CHANGED;
    default:                            break;
    }
    return (uint8_t)(-ANJAY_ERR_INTERNAL);
}

typedef int with_instance_on_demand_cb_t(anjay_t *anjay,
                                         const anjay_dm_object_def_t *const *obj,
                                         anjay_iid_t iid,
                                         anjay_input_ctx_t *in_ctx,
                                         void *arg);

static int write_resource(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj,
                          anjay_iid_t iid,
                          anjay_input_ctx_t *in_ctx,
                          void *rid_) {
    anjay_rid_t rid = (anjay_rid_t) (uintptr_t) rid_;
    int result = _anjay_dm_map_present_result(
            _anjay_dm_resource_supported(anjay, obj, rid));
    if (!result) {
        result = _anjay_dm_resource_write(anjay, obj, iid, rid, in_ctx);
    }
    if (!result) {
        result = _anjay_notify_queue_resource_change(
                &anjay->bootstrap.notification_queue, (*obj)->oid, iid, rid);
    }
    return result;
}

static int write_instance_inner(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj,
                                anjay_iid_t iid,
                                anjay_input_ctx_t *in_ctx,
                                void *dummy) {
    (void) dummy;
    anjay_id_type_t type;
    uint16_t id;
    int retval;
    while (!(retval = _anjay_input_get_id(in_ctx, &type, &id))) {
        if (type != ANJAY_ID_RID) {
            return ANJAY_ERR_BAD_REQUEST;
        }
        retval = write_resource(anjay, obj, iid, in_ctx,
                                (void *) (uintptr_t) id);
        if (!retval
                || retval == ANJAY_ERR_NOT_FOUND
                || retval == ANJAY_ERR_NOT_IMPLEMENTED) {
            // LwM2M spec, 5.2.7.1 BOOTSTRAP WRITE:
            // "When the 'Write' operation targets an Object or an Object
            // Instance, the LwM2M Client MUST ignore optional resources it does
            // not support in the payload." - so, continue on these errors.
            if (retval) {
                anjay_log(WARNING, "Ignoring error during BOOTSTRAP WRITE to "
                          "/%" PRIu16 "/%" PRIu16 "/%" PRIu16 ": %d",
                          (*obj)->oid, iid, id, retval);
            }
            retval = _anjay_input_next_entry(in_ctx);
        }
        if (retval) {
            return retval;
        }
    }
    return (retval == ANJAY_GET_INDEX_END) ? 0 : retval;
}

static int with_instance_on_demand(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj,
                                   anjay_iid_t iid,
                                   anjay_input_ctx_t *in_ctx,
                                   with_instance_on_demand_cb_t callback,
                                   void *arg) {
    int result = 0;
    int ipresent = _anjay_dm_instance_present(anjay, obj, iid);
    anjay_iid_t new_iid = iid;
    if (ipresent < 0) {
        return ipresent;
    } else if (ipresent == 0) {
        result = _anjay_dm_instance_create(anjay, obj, &new_iid,
                                           ANJAY_SSID_BOOTSTRAP);
        if (result) {
            anjay_log(DEBUG, "Instance Create handler for object %" PRIu16
                             " failed", (*obj)->oid);
            return result;
        } else if (iid != new_iid) {
            anjay_log(DEBUG, "Instance Create handler for object %" PRIu16
                             " returned Instance %" PRIu16 " while %" PRIu16
                             " was expected;",
                      (*obj)->oid, new_iid, iid);
            result = ANJAY_ERR_INTERNAL;
        }
    }

    if (!result) {
        result = callback(anjay, obj, iid, in_ctx, arg);
    }
    if (!result) {
        result = _anjay_notify_queue_instance_created(
                &anjay->bootstrap.notification_queue, (*obj)->oid, iid);
    }
    return result;
}

static int write_instance(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj,
                          anjay_iid_t iid,
                          anjay_input_ctx_t *in_ctx) {
    return with_instance_on_demand(anjay, obj, iid, in_ctx,
                                   write_instance_inner, NULL);
}

static int write_object(anjay_t *anjay,
                        const anjay_dm_object_def_t *const *obj,
                        anjay_input_ctx_t *in_ctx) {
    // should it remove existing instances?
    anjay_id_type_t type;
    uint16_t id;
    int retval;
    while (!(retval = _anjay_input_get_id(in_ctx, &type, &id))) {
        if (type != ANJAY_ID_IID) {
            return ANJAY_ERR_BAD_REQUEST;
        }
        anjay_input_ctx_t *nested_ctx = _anjay_input_nested_ctx(in_ctx);
        if (!nested_ctx) {
            return ANJAY_ERR_INTERNAL;
        }
        if ((retval = write_instance(anjay, obj, id, nested_ctx))
                || (retval = _anjay_input_next_entry(in_ctx))) {
            return retval;
        }
    }
    return (retval == ANJAY_GET_INDEX_END) ? 0 : retval;
}

static int security_object_valid_handler(anjay_t *anjay,
                                         const anjay_dm_object_def_t *const *obj,
                                         anjay_iid_t iid,
                                         void *bootstrap_instances) {
    (void) obj;
    if (!_anjay_is_bootstrap_security_instance(anjay, iid)) {
        return 0;
    }
    if (++*((uintptr_t *) bootstrap_instances) > 1) {
        return ANJAY_DM_FOREACH_BREAK;
    }
    return 0;
}

static bool has_multiple_bootstrap_security_instances(anjay_t *anjay) {
    uintptr_t bootstrap_instances = 0;
    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_SECURITY);
    if (_anjay_dm_foreach_instance(anjay, obj, security_object_valid_handler,
                                   &bootstrap_instances)
            || bootstrap_instances > 1) {
        return true;
    }
    return false;
}

static int bootstrap_write(anjay_t *anjay,
                           const anjay_dm_write_args_t *args,
                           anjay_input_ctx_t *in_ctx) {
    anjay_log(DEBUG, "Bootstrap Write %s", ANJAY_DEBUG_MAKE_PATH(args));
    if (!args->has_oid) {
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
    cancel_client_initiated_bootstrap(anjay);
    start_bootstrap_if_not_already_started(anjay);
    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, args->oid);
    if (!obj || !*obj) {
        anjay_log(ERROR, "Object not found: %u", args->oid);
        return ANJAY_ERR_NOT_FOUND;
    }

    int retval;
    if (args->has_iid) {
        if (args->has_rid) {
            retval = with_instance_on_demand(anjay, obj, args->iid, in_ctx,
                                             write_resource,
                                             (void *) (uintptr_t) args->rid);
        } else {
            retval = write_instance(anjay, obj, args->iid, in_ctx);
        }
    } else {
        retval = write_object(anjay, obj, in_ctx);
    }
    if (!retval && args->oid == ANJAY_DM_OID_SECURITY) {
        if (has_multiple_bootstrap_security_instances(anjay)) {
            anjay_log(ERROR, "Bootstrap Server misused Security Object");
            retval = ANJAY_ERR_BAD_REQUEST;
        }
    }
    return retval;
}

static int append_iid(anjay_t *anjay,
                      const anjay_dm_object_def_t *const *obj,
                      anjay_iid_t iid,
                      void *iids_append_ptr_) {
    (void) anjay; (void) obj;
    AVS_LIST(anjay_iid_t) **iids_append_ptr =
            (AVS_LIST(anjay_iid_t) **) iids_append_ptr_;
    AVS_LIST(anjay_iid_t) new_element = AVS_LIST_NEW_ELEMENT(anjay_iid_t);
    if (!new_element) {
        anjay_log(ERROR, "Out of memory");
        return -1;
    }
    *new_element = iid;
    AVS_LIST_INSERT(*iids_append_ptr, new_element);
    *iids_append_ptr = AVS_LIST_NEXT_PTR(*iids_append_ptr);
    return 0;
}

static int delete_instance(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *obj,
                           anjay_iid_t iid) {
    int retval = _anjay_dm_instance_remove(anjay, obj, iid);
    if (retval) {
        anjay_log(ERROR, "delete_instance: cannot delete /%d/%d: %d",
                  (*obj)->oid, iid, retval);
    } else {
        bootstrap_remove_notify_changed(anjay, (*obj)->oid, iid);
        retval = _anjay_notify_queue_instance_removed(
                &anjay->bootstrap.notification_queue, (*obj)->oid, iid);
    }
    return retval;
}

static int delete_object(anjay_t *anjay,
                         const anjay_dm_object_def_t *const *obj) {
    // deleting from within _anjay_dm_foreach_instance()
    // would possibly invalidate cookies, so we use a temporary list
    AVS_LIST(anjay_iid_t) iids = NULL;
    AVS_LIST(anjay_iid_t) *iids_it = &iids;
    int retval = _anjay_dm_foreach_instance(anjay, obj, append_iid, &iids_it);
    if (!retval) {
        AVS_LIST(anjay_iid_t) iid;
        AVS_LIST_FOREACH(iid, iids) {
            if ((*obj)->oid == ANJAY_DM_OID_SECURITY
                    && _anjay_is_bootstrap_security_instance(anjay, *iid)) {
                continue; // don't remove self
            }
            if ((retval = delete_instance(anjay, obj, *iid))) {
                if (retval == ANJAY_ERR_METHOD_NOT_ALLOWED) {
                    // ignore 4.05 Method Not Allowed
                    // it most likely means that the Object is non-modifiable
                    // (transaction or Delete handlers not implemented)
                    // so we just leave it as it is
                    retval = 0;
                } else {
                    break;
                }
            }
        }
    }
    AVS_LIST_CLEAR(&iids);
    return retval;
}

static int bootstrap_delete(anjay_t *anjay,
                            const anjay_request_details_t *details) {
    anjay_log(DEBUG, "Bootstrap Delete %s", ANJAY_DEBUG_MAKE_PATH(details));
    cancel_client_initiated_bootstrap(anjay);
    start_bootstrap_if_not_already_started(anjay);

    if (details->is_bs_uri || details->has_rid || !details->has_oid) {
        return ANJAY_ERR_BAD_REQUEST;
    }

    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, details->oid);
    if (!obj || !*obj) {
        anjay_log(WARNING, "Object not found: %u", details->oid);
        return 0;
    }

    if (details->has_iid) {
        int present = _anjay_dm_instance_present(anjay, obj, details->iid);
        if (present > 0) {
            return delete_instance(anjay, obj, details->iid);
        } else {
            return present;
        }
    } else {
        return delete_object(anjay, obj);
    }
}

#ifdef WITH_DISCOVER
static int bootstrap_discover(anjay_t *anjay,
                              const anjay_request_details_t *details,
                              avs_stream_abstract_t *stream) {
    if (details->has_iid || details->has_rid) {
        return ANJAY_ERR_BAD_REQUEST;
    }

    if (details->has_oid) {
        const anjay_dm_object_def_t *const *obj =
                _anjay_dm_find_object_by_oid(anjay, details->oid);
        return _anjay_bootstrap_discover_object(anjay, obj, stream);
    }
    return _anjay_bootstrap_discover(anjay, stream);
}
#else // WITH_DISCOVER
#define bootstrap_discover(anjay, details, stream) \
        (anjay_log(ERROR, "Not supported: Bootstrap Discover %s", \
                   ANJAY_DEBUG_MAKE_PATH(details)), ANJAY_ERR_NOT_IMPLEMENTED)
#endif // WITH_DISCOVER

static int purge_bootstrap(anjay_t *anjay, void *iid_) {
    anjay_iid_t iid = (anjay_iid_t) (uintptr_t) iid_;
    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_SECURITY);
    int retval = -1;
    if (obj && *obj) {
        _anjay_dm_transaction_begin(anjay);
        anjay_notify_queue_t notification = NULL;
        (void) ((retval = _anjay_dm_instance_remove(anjay, obj, iid))
                || (retval = _anjay_notify_queue_instance_removed(
                        &notification, (*obj)->oid, iid))
                || (retval = _anjay_notify_flush(anjay, ANJAY_SSID_BOOTSTRAP,
                                                 &notification)));
        retval = _anjay_dm_transaction_finish(anjay, retval);
    }
    if (retval) {
        anjay_log(ERROR, "Could not purge Bootstrap Server Account %" PRIu16,
                  iid);
    }
    return retval;
}

static int schedule_bootstrap_timeout(anjay_t *anjay,
                                      const anjay_dm_object_def_t *const *obj,
                                      anjay_iid_t iid,
                                      void *dummy) {
    assert((*obj)->oid == ANJAY_DM_OID_SECURITY);
    (void) dummy;

    if (!_anjay_is_bootstrap_security_instance(anjay, iid)) {
        return 0;
    }

    const anjay_resource_path_t res_path = {
        ANJAY_DM_OID_SECURITY,
        iid,
        ANJAY_DM_RID_SECURITY_BOOTSTRAP_TIMEOUT
    };

    int64_t timeout;
    bool timeout_is_set = !_anjay_dm_res_read_i64(anjay, &res_path, &timeout);
    if (!timeout_is_set) {
        timeout = 0;
    }

    if (!timeout_is_set || timeout > 0) {
        if (_anjay_sched(anjay->sched, NULL,
                         (struct timespec){ (time_t) timeout, 0 },
                         purge_bootstrap, (void *) (uintptr_t) iid)) {
            anjay_log(ERROR, "Could not schedule purge of "
                             "Bootstrap Server Account %" PRIu16, iid);
        }
    }
    return 0;
}

int _anjay_bootstrap_finish(anjay_t *anjay) {
    anjay_log(TRACE, "Bootstrap Sequence finished");

    cancel_client_initiated_bootstrap(anjay);
    int retval = commit_bootstrap(anjay);
    if (retval) {
        anjay_log(ERROR,
                  "Bootstrap configuration could not be committed, rejecting");
        return retval;
    }
    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_SECURITY);
    if ((retval = _anjay_dm_foreach_instance(
                 anjay, obj, schedule_bootstrap_timeout, NULL))) {
        anjay_log(ERROR,
                  "Could not iterate over LWM2M Security object instances");
    }
    int retval2 = _anjay_notify_perform(anjay, ANJAY_SSID_BOOTSTRAP,
                                        anjay->bootstrap.notification_queue);
    if (retval2) {
        anjay_log(ERROR, "Could not post-process data model after bootstrap");
        retval = retval2;
    } else {
        _anjay_notify_clear_queue(&anjay->bootstrap.notification_queue);
    }
    if (retval) {
        anjay_log(ERROR,
                  "Bootstrap Finish failed, re-entering bootstrap phase");
        start_bootstrap_if_not_already_started(anjay);
    }
    return retval;
}

int _anjay_bootstrap_object_write(anjay_t *anjay,
                                  anjay_oid_t oid,
                                  anjay_input_ctx_t *in_ctx) {
    const anjay_dm_write_args_t args = {
        .ssid = ANJAY_SSID_BOOTSTRAP,
        .has_oid = true,
        .oid = oid
    };
    return bootstrap_write(anjay, &args, in_ctx);
}

static int invoke_action(anjay_t *anjay,
                         const anjay_request_details_t *details,
                         avs_stream_abstract_t *stream) {
    anjay_input_ctx_t *in_ctx = NULL;
    int result = -1;

    switch (details->action) {
    case ANJAY_ACTION_WRITE:
        if ((result = _anjay_input_dynamic_create(&in_ctx, &stream, false))) {
            anjay_log(ERROR, "could not create input context");
            return result;
        }
        result = bootstrap_write(anjay, &DETAILS_TO_DM_WRITE_ARGS(details),
                                 in_ctx);
        if (_anjay_input_ctx_destroy(&in_ctx)) {
            anjay_log(ERROR, "input ctx cleanup failed");
        }
        return result;
    case ANJAY_ACTION_DELETE:
        return bootstrap_delete(anjay, details);
    case ANJAY_ACTION_DISCOVER:
        return bootstrap_discover(anjay, details, stream);
    case ANJAY_ACTION_BOOTSTRAP_FINISH:
        return _anjay_bootstrap_finish(anjay);
    default:
        anjay_log(ERROR, "Invalid action for Bootstrap Interface");
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

int _anjay_bootstrap_perform_action(anjay_t *anjay,
                                    avs_stream_abstract_t *stream,
                                    const anjay_request_details_t *details) {
    anjay_msg_details_t msg_details = {
        .msg_type = ANJAY_COAP_MSG_ACKNOWLEDGEMENT,
        .msg_code = make_success_response_code(details->action),
        .format = ANJAY_COAP_FORMAT_NONE
    };

    int result = _anjay_coap_stream_setup_response(stream, &msg_details);
    if (result) {
        return result;
    }

    return invoke_action(anjay, details, stream);
}

static int check_request_bootstrap_response(avs_stream_abstract_t *stream) {
    uint8_t response_code;
    if (_anjay_coap_stream_get_code(stream, &response_code)) {
        anjay_log(ERROR, "could not get response code");
        return -1;
    }

    if (response_code != ANJAY_COAP_CODE_CHANGED) {
        anjay_log(ERROR, "server responded with %s (expected %s)",
                  ANJAY_COAP_CODE_STRING(response_code),
                  ANJAY_COAP_CODE_STRING(ANJAY_COAP_CODE_CHANGED));
        return -1;
    }

    return 0;
}

static int send_request_bootstrap(avs_stream_abstract_t *stream,
                                  const char *endpoint_name) {
    anjay_msg_details_t details = {
        .msg_type = ANJAY_COAP_MSG_CONFIRMABLE,
        .msg_code = ANJAY_COAP_CODE_POST,
        .format = ANJAY_COAP_FORMAT_NONE,
        .uri_path = _anjay_make_string_list("bs", NULL),
        .uri_query = _anjay_make_query_string_list(NULL, endpoint_name,
                                                   NULL, ANJAY_BINDING_NONE)
    };

    int result = -1;
    if (!details.uri_path || !details.uri_query) {
        anjay_log(ERROR, "could not initialize request headers");
        goto cleanup;
    }

    if ((result = _anjay_coap_stream_setup_request(stream, &details, NULL, 0))
            || (result = avs_stream_finish_message(stream))
            || (result = check_request_bootstrap_response(stream))) {
        anjay_log(ERROR, "could not request bootstrap");
    } else {
        anjay_log(INFO, "Request Bootstrap sent");
        result = 0;
    }

cleanup:
    AVS_LIST_CLEAR(&details.uri_path);
    AVS_LIST_CLEAR(&details.uri_query);
    return result;
}

static int request_bootstrap(anjay_t *anjay, void *dummy) {
    assert(!_anjay_servers_is_connected_to_non_bootstrap(&anjay->servers));

    anjay_log(TRACE, "sending Client Initiated Bootstrap");

    (void) dummy;

    anjay_active_server_info_t *server =
            _anjay_servers_find_active(&anjay->servers, ANJAY_SSID_BOOTSTRAP);
    anjay_connection_ref_t connection = {
        .server = server,
        .conn_type = _anjay_get_default_connection_type(server)
    };
    avs_stream_abstract_t *stream =
            connection.server ? _anjay_get_server_stream(anjay, connection)
                              : NULL;
    if (!stream) {
        anjay_log(ERROR, "could not get stream for bootstrap server");
        return -1;
    }

    int result = send_request_bootstrap(stream, anjay->endpoint_name);
    if (result) {
        anjay_log(ERROR, "could not send Request Bootstrap");
    }

    avs_stream_reset(stream);
    _anjay_release_server_stream(anjay, connection);

    if (result) {
        anjay_log(ERROR, "could not send Request Bootstrap");
    }
    return result;
}

static int schedule_request_bootstrap(anjay_t *anjay, time_t holdoff_s) {
    struct timespec delay = { holdoff_s, 0 };
    anjay_sched_retryable_backoff_t backoff = {
        .delay = { 3, 0 },
        .max_delay = { 120, 0 }
    };

    if (_anjay_sched_retryable(
                anjay->sched,
                &anjay->bootstrap.client_initiated_bootstrap_handle, delay,
                backoff, request_bootstrap, NULL)) {
        anjay_log(ERROR, "Could not schedule Client Initiated Bootstrap");
        return -1;
    }
    anjay->bootstrap.client_initiated_bootstrap_scheduled = true;
    start_bootstrap_if_not_already_started(anjay);
    return 0;
}

int _anjay_bootstrap_account_prepare(anjay_t *anjay) {
    // schedule Client Initiated Bootstrap if not attempted already
    if (anjay->bootstrap.client_initiated_bootstrap_scheduled
            || anjay->bootstrap.client_initiated_bootstrap_handle) {
        // Client Initiated Bootstrap is never scheduled more than once
        return 0;
    }

    anjay_iid_t security_iid;
    if (_anjay_find_security_iid(anjay, ANJAY_SSID_BOOTSTRAP, &security_iid)) {
        anjay_log(ERROR,
                  "could not find server Security IID of the Bootstrap Server");
        return -1;
    }

    const anjay_resource_path_t path = {
        ANJAY_DM_OID_SECURITY,
        security_iid,
        ANJAY_DM_RID_SECURITY_CLIENT_HOLD_OFF_TIME
    };
    int64_t holdoff_s;
    if (_anjay_dm_res_read_i64(anjay, &path, &holdoff_s) || holdoff_s < 0) {
        anjay_log(INFO, "Client Hold Off Time not set or invalid, "
                        "not scheduling Client Initiated Bootstrap");
        return 0;
    }
    return schedule_request_bootstrap(anjay, (time_t) holdoff_s);
}

int _anjay_bootstrap_update_reconnected(anjay_t *anjay) {
    if (anjay->bootstrap.in_progress
            && !anjay->bootstrap.client_initiated_bootstrap_handle) {
        // if it's already scheduled then it'll happen on the new socket,
        // so no need to reschedule
        return schedule_request_bootstrap(anjay, 0);
    }
    return 0;
}

void _anjay_bootstrap_cleanup(anjay_t *anjay) {
    cancel_client_initiated_bootstrap(anjay);
    abort_bootstrap(anjay);
    _anjay_notify_clear_queue(&anjay->bootstrap.notification_queue);
}

#ifdef ANJAY_TEST
#include "test/bootstrap.c"
#endif // ANJAY_TEST
