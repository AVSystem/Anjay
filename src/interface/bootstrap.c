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
    _anjay_sched_del(anjay->sched,
                     &anjay->bootstrap.client_initiated_bootstrap_handle);
}

static void start_bootstrap_if_not_already_started(anjay_t *anjay) {
    if (!anjay->bootstrap.in_progress) {
        AVS_LIST(anjay_active_server_info_t) server;
        AVS_LIST_FOREACH(server, anjay->servers.active) {
            if (server->ssid != ANJAY_SSID_BOOTSTRAP) {
                anjay_connection_ref_t ref = {
                    .server = server,
                    .conn_type = ANJAY_CONNECTION_WILDCARD
                };
                _anjay_connection_suspend(ref);
            }
        }
        _anjay_dm_transaction_begin(anjay);
        anjay->bootstrap.in_progress = true;
        _anjay_sched_del(anjay->sched,
                         &anjay->bootstrap.purge_bootstrap_handle);
    }
}

static int commit_bootstrap(anjay_t *anjay) {
    if (_anjay_dm_transaction_validate(anjay)) {
        return ANJAY_ERR_NOT_ACCEPTABLE;
    } else {
        anjay->bootstrap.in_progress = false;
        return _anjay_dm_transaction_finish_without_validation(anjay, 0);
    }
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
    if (!*obj_it) {
        return;
    }
    AVS_LIST(anjay_notify_queue_resource_entry_t) *res_it;
    AVS_LIST_FOREACH_PTR(res_it, &(*obj_it)->resources_changed) {
        if ((*res_it)->iid >= iid) {
            break;
        }
    }
    while (*res_it && (*res_it)->iid == iid) {
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
    if (!_anjay_dm_resource_supported(obj, rid)) {
        return ANJAY_ERR_NOT_FOUND;
    }
    int result = _anjay_dm_resource_write(anjay, obj, iid, rid, in_ctx, NULL);
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
    int ipresent = _anjay_dm_instance_present(anjay, obj, iid, NULL);
    anjay_iid_t new_iid = iid;
    if (ipresent < 0) {
        return ipresent;
    } else if (ipresent == 0) {
        result = _anjay_dm_instance_create(anjay, obj, &new_iid,
                                           ANJAY_SSID_BOOTSTRAP, NULL);
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
    anjay_log(DEBUG, "Bootstrap Write %s", ANJAY_DEBUG_MAKE_PATH(&args->uri));
    if (!args->uri.has_oid) {
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
    cancel_client_initiated_bootstrap(anjay);
    start_bootstrap_if_not_already_started(anjay);
    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, args->uri.oid);
    if (!obj || !*obj) {
        anjay_log(ERROR, "Object not found: %u", args->uri.oid);
        return ANJAY_ERR_NOT_FOUND;
    }

    int retval;
    if (args->uri.has_iid) {
        if (args->uri.has_rid) {
            retval =
                    with_instance_on_demand(anjay, obj, args->uri.iid, in_ctx,
                                            write_resource,
                                            (void *) (uintptr_t) args->uri.rid);
        } else {
            retval = write_instance(anjay, obj, args->uri.iid, in_ctx);
        }
    } else {
        retval = write_object(anjay, obj, in_ctx);
    }
    if (!retval && args->uri.oid == ANJAY_DM_OID_SECURITY) {
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
    int retval = _anjay_dm_instance_remove(anjay, obj, iid, NULL);
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
    anjay_log(DEBUG, "Bootstrap Delete %s", ANJAY_DEBUG_MAKE_PATH(&details->uri));
    cancel_client_initiated_bootstrap(anjay);
    start_bootstrap_if_not_already_started(anjay);

    if (details->is_bs_uri || details->uri.has_rid || !details->uri.has_oid) {
        return ANJAY_ERR_BAD_REQUEST;
    }

    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, details->uri.oid);
    if (!obj || !*obj) {
        anjay_log(WARNING, "Object not found: %u", details->uri.oid);
        return 0;
    }

    if (details->uri.has_iid) {
        int present = _anjay_dm_instance_present(anjay, obj, details->uri.iid,
                                                 NULL);
        if (present > 0) {
            return delete_instance(anjay, obj, details->uri.iid);
        } else {
            return present;
        }
    } else {
        return delete_object(anjay, obj);
    }
}

#ifdef WITH_DISCOVER
static int bootstrap_discover(anjay_t *anjay,
                              const anjay_request_details_t *details) {
    if (details->uri.has_iid || details->uri.has_rid) {
        return ANJAY_ERR_BAD_REQUEST;
    }

    if (details->uri.has_oid) {
        const anjay_dm_object_def_t *const *obj =
                _anjay_dm_find_object_by_oid(anjay, details->uri.oid);
        return _anjay_bootstrap_discover_object(anjay, obj);
    }
    return _anjay_bootstrap_discover(anjay);
}
#else // WITH_DISCOVER
#define bootstrap_discover(anjay, details, stream) \
        (anjay_log(ERROR, "Not supported: Bootstrap Discover %s", \
                   ANJAY_DEBUG_MAKE_PATH(&details->uri)), \
                   ANJAY_ERR_NOT_IMPLEMENTED)
#endif // WITH_DISCOVER

static int purge_bootstrap(anjay_t *anjay, void *dummy) {
    (void) dummy;
    anjay_iid_t iid;
    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_SECURITY);
    if (!obj || _anjay_find_security_iid(anjay, ANJAY_SSID_BOOTSTRAP, &iid)) {
        anjay_log(WARNING, "Could not find Bootstrap Server Account to purge");
        return 0;
    }
    int retval = -1;
    if (obj && *obj) {
        _anjay_dm_transaction_begin(anjay);
        anjay_notify_queue_t notification = NULL;
        (void) ((retval = _anjay_dm_instance_remove(anjay, obj, iid, NULL))
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

static int schedule_bootstrap_timeout(anjay_t *anjay) {
    anjay_iid_t iid;
    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_SECURITY);
    if (!obj || _anjay_find_security_iid(anjay, ANJAY_SSID_BOOTSTRAP, &iid)) {
        anjay_log(DEBUG, "Could not find Bootstrap Server Account to purge");
        return 0;
    }

    const anjay_uri_path_t res_path =
            MAKE_RESOURCE_PATH(ANJAY_DM_OID_SECURITY, iid,
                               ANJAY_DM_RID_SECURITY_BOOTSTRAP_TIMEOUT);

    int64_t timeout;
    if (!_anjay_dm_res_read_i64(anjay, &res_path, &timeout) && timeout > 0) {
        /* This is only ever called from _anjay_bootstrap_finish() if
         * in_progess was originally true. purge_bootstrap_handle is reset
         * when setting in_progress to true (see
         * start_bootstrap_if_not_already_started()) and never touched anywhere
         * else than there and here, so it's certainly NULL here and it's safe
         * to call _anjay_sched(). There's an assert for that inside it. */
        if (_anjay_sched(anjay->sched, &anjay->bootstrap.purge_bootstrap_handle,
                         (struct timespec){ (time_t) timeout, 0 },
                         purge_bootstrap, NULL)) {
            anjay_log(ERROR, "Could not schedule purge of "
                             "Bootstrap Server Account %" PRIu16, iid);
            return -1;
        }
    }
    return 0;
}

int _anjay_bootstrap_finish(anjay_t *anjay) {
    anjay_log(TRACE, "Bootstrap Sequence finished");

    if (!anjay->bootstrap.in_progress) {
        return 0;
    }

    cancel_client_initiated_bootstrap(anjay);
    int retval = commit_bootstrap(anjay);
    if (retval) {
        anjay_log(ERROR,
                  "Bootstrap configuration could not be committed, rejecting");
        return retval;
    }
    if ((retval = _anjay_notify_perform(anjay, ANJAY_SSID_BOOTSTRAP,
                                        anjay->bootstrap.notification_queue))) {
        anjay_log(ERROR, "Could not post-process data model after bootstrap");
    } else {
        _anjay_notify_clear_queue(&anjay->bootstrap.notification_queue);
        retval = schedule_bootstrap_timeout(anjay);
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
        .uri = {
            .has_oid = true,
            .oid = oid
        }
    };
    return bootstrap_write(anjay, &args, in_ctx);
}

static int invoke_action(anjay_t *anjay,
                         const anjay_request_details_t *details) {
    anjay_input_ctx_t *in_ctx = NULL;
    const uint16_t format =
            _anjay_translate_legacy_content_format(details->content_format);
    int result = -1;
    switch (details->action) {
    case ANJAY_ACTION_WRITE:
        if ((result = _anjay_input_dynamic_create(&in_ctx, &anjay->comm_stream,
                                                  false))) {
            anjay_log(ERROR, "could not create input context");
            return result;
        }

        if (format == ANJAY_COAP_FORMAT_TLV && details->uri.has_rid) {
            result = _anjay_dm_check_if_tlv_rid_matches_uri_rid(
                    in_ctx, details->uri.rid);
        }

        if (!result) {
            result = bootstrap_write(anjay, &DETAILS_TO_DM_WRITE_ARGS(details),
                                     in_ctx);
        }
        if (_anjay_input_ctx_destroy(&in_ctx)) {
            anjay_log(ERROR, "input ctx cleanup failed");
        }
        return result;
    case ANJAY_ACTION_DELETE:
        return bootstrap_delete(anjay, details);
    case ANJAY_ACTION_DISCOVER:
        return bootstrap_discover(anjay, details);
    case ANJAY_ACTION_BOOTSTRAP_FINISH:
        // _anjay_bootstrap_finish() is also called when Bootstrap Sequence is
        // finished via means other than Bootstrap Server communication.
        // It is a no-op if Client- or Server-Initiated Bootstrap is not
        // currently in progress. But we want proper purge semantics even if the
        // Bootstrap Server derps and sends Bootstrap Finish without doing
        // anything beforehand. That's why we're calling
        // start_bootstrap_if_not_already_started() first.
        start_bootstrap_if_not_already_started(anjay);
        return _anjay_bootstrap_finish(anjay);
    default:
        anjay_log(ERROR, "Invalid action for Bootstrap Interface");
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

int _anjay_bootstrap_perform_action(anjay_t *anjay,
                                    const anjay_request_details_t *details) {
    anjay_msg_details_t msg_details = {
        .msg_type = ANJAY_COAP_MSG_ACKNOWLEDGEMENT,
        .msg_code = make_success_response_code(details->action),
        .format = ANJAY_COAP_FORMAT_NONE
    };

    int result = _anjay_coap_stream_setup_response(anjay->comm_stream,
                                                   &msg_details);
    if (result) {
        return result;
    }

    return invoke_action(anjay, details);
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
        .uri_query = _anjay_make_query_string_list(NULL, endpoint_name, NULL,
                                                   ANJAY_BINDING_NONE, NULL)
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

static int request_bootstrap(anjay_t *anjay, void *dummy);

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
    start_bootstrap_if_not_already_started(anjay);
    return 0;
}

static int request_bootstrap(anjay_t *anjay, void *dummy) {
    if (_anjay_servers_is_connected_to_non_bootstrap(&anjay->servers)) {
        anjay_log(DEBUG,
                  "Client Initiated Bootstrap not applicable, not performing");
        return 0;
    }

    anjay_log(TRACE, "sending Client Initiated Bootstrap");

    (void) dummy;

    anjay_active_server_info_t *server =
            _anjay_servers_find_active(&anjay->servers, ANJAY_SSID_BOOTSTRAP);
    anjay_connection_ref_t connection = {
        .server = server,
        .conn_type = _anjay_get_default_connection_type(server)
    };
    if (!connection.server
            || _anjay_bind_server_stream(anjay, connection)) {
        anjay_log(ERROR, "could not get stream for bootstrap server");
        return -1;
    }

    int result = send_request_bootstrap(anjay->comm_stream,
                                        anjay->endpoint_name);
    if (result == ANJAY_COAP_SOCKET_ERR_NETWORK) {
        anjay_log(ERROR, "network communication error while "
                         "sending Request Bootstrap");
        _anjay_schedule_server_reconnect(anjay, server);
    } else if (result) {
        anjay_log(ERROR, "could not send Request Bootstrap");
    }

    avs_stream_reset(anjay->comm_stream);
    _anjay_release_server_stream(anjay, connection);
    return result;
}

int _anjay_bootstrap_account_prepare(anjay_t *anjay) {
    // schedule Client Initiated Bootstrap if not attempted already
    if (anjay->bootstrap.client_initiated_bootstrap_handle) {
        return 0;
    }

    anjay_iid_t security_iid;
    if (_anjay_find_security_iid(anjay, ANJAY_SSID_BOOTSTRAP, &security_iid)) {
        anjay_log(ERROR,
                  "could not find server Security IID of the Bootstrap Server");
        return -1;
    }

    const anjay_uri_path_t path =
            MAKE_RESOURCE_PATH(ANJAY_DM_OID_SECURITY, security_iid,
                               ANJAY_DM_RID_SECURITY_CLIENT_HOLD_OFF_TIME);
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
    _anjay_sched_del(anjay->sched, &anjay->bootstrap.purge_bootstrap_handle);
    _anjay_notify_clear_queue(&anjay->bootstrap.notification_queue);
}

#ifdef ANJAY_TEST
#include "test/bootstrap.c"
#endif // ANJAY_TEST
