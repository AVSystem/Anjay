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

#include <anjay_modules/interface/bootstrap.h>
#include <anjay_modules/notify.h>
#include <anjay_modules/time_defs.h>

#include "../anjay_core.h"
#include "../coap/content_format.h"
#include "../dm/discover.h"
#include "../dm/query.h"
#include "../io_core.h"
#include "../servers_utils.h"
#include "bootstrap_core.h"

#ifdef ANJAY_TEST
#    include "test/bootstrap_mock.h"
#endif // ANJAY_TEST

VISIBILITY_SOURCE_BEGIN

static void cancel_client_initiated_bootstrap(anjay_t *anjay) {
    _anjay_sched_del(anjay->sched,
                     &anjay->bootstrap.client_initiated_bootstrap_handle);
}

static int suspend_nonbootstrap_server(anjay_t *anjay,
                                       anjay_server_info_t *server,
                                       void *data) {
    (void) anjay;
    (void) data;
    if (_anjay_server_ssid(server) != ANJAY_SSID_BOOTSTRAP) {
        anjay_connection_ref_t ref = {
            .server = server,
            .conn_type = ANJAY_CONNECTION_UNSET
        };
        _anjay_connection_suspend(ref);
    }
    return 0;
}

static void start_bootstrap_if_not_already_started(anjay_t *anjay) {
    if (!anjay->bootstrap.in_progress) {
        // clear inactive servers so that they won't attempt to retry; they will
        // be recreated during _anjay_schedule_reload_servers() after bootstrap
        // procedure is finished
        _anjay_servers_cleanup_inactive(anjay);
        // suspend active connections
        _anjay_servers_foreach_active(anjay, suspend_nonbootstrap_server, NULL);

        _anjay_dm_transaction_begin(anjay);
        _anjay_sched_del(anjay->sched,
                         &anjay->bootstrap.purge_bootstrap_handle);
    }
    anjay->bootstrap.bootstrap_session_token =
            _anjay_server_primary_session_token(
                    anjay->current_connection.server);
    anjay->bootstrap.in_progress = true;
}

static int commit_bootstrap(anjay_t *anjay) {
    if (anjay->bootstrap.in_progress) {
        if (_anjay_dm_transaction_validate(anjay)) {
            return ANJAY_ERR_NOT_ACCEPTABLE;
        } else {
            anjay->bootstrap.in_progress = false;
            _anjay_conn_session_token_reset(
                    &anjay->bootstrap.bootstrap_session_token);
            return _anjay_dm_transaction_finish_without_validation(anjay, 0);
        }
    }
    return 0;
}

static void abort_bootstrap(anjay_t *anjay) {
    if (anjay->bootstrap.in_progress) {
        _anjay_dm_transaction_rollback(anjay);
        anjay->bootstrap.in_progress = false;
        _anjay_conn_session_token_reset(
                &anjay->bootstrap.bootstrap_session_token);
        _anjay_schedule_reload_servers(anjay);
    }
}

static void bootstrap_remove_notify_changed(anjay_bootstrap_t *bootstrap,
                                            anjay_oid_t oid,
                                            anjay_iid_t iid) {
    AVS_LIST(anjay_notify_queue_object_entry_t) *obj_it;
    AVS_LIST_FOREACH_PTR(obj_it, &bootstrap->notification_queue) {
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
    case ANJAY_ACTION_WRITE:
        return AVS_COAP_CODE_CHANGED;
    case ANJAY_ACTION_DELETE:
        return AVS_COAP_CODE_DELETED;
    case ANJAY_ACTION_DISCOVER:
        return AVS_COAP_CODE_CONTENT;
    case ANJAY_ACTION_BOOTSTRAP_FINISH:
        return AVS_COAP_CODE_CHANGED;
    default:
        break;
    }
    return (uint8_t) (-ANJAY_ERR_INTERNAL);
}

typedef int
with_instance_on_demand_cb_t(anjay_t *anjay,
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
        if (!retval || retval == ANJAY_ERR_NOT_FOUND
                || retval == ANJAY_ERR_NOT_IMPLEMENTED) {
            // LwM2M spec, 5.2.7.1 BOOTSTRAP WRITE:
            // "When the 'Write' operation targets an Object or an Object
            // Instance, the LwM2M Client MUST ignore optional resources it does
            // not support in the payload." - so, continue on these errors.
            if (retval) {
                anjay_log(WARNING,
                          "Ignoring error during BOOTSTRAP WRITE to "
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
            anjay_log(DEBUG,
                      "Instance Create handler for object %" PRIu16 " failed",
                      (*obj)->oid);
            return result;
        } else if (iid != new_iid) {
            anjay_log(DEBUG,
                      "Instance Create handler for object %" PRIu16
                      " returned Instance %" PRIu16 " while %" PRIu16
                      " was expected;",
                      (*obj)->oid, new_iid, iid);
            result = ANJAY_ERR_INTERNAL;
        }
    }

    if (!result) {
        result = callback(anjay, obj, iid, in_ctx, arg);
    }
    if (ipresent == 0 && !result) {
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

static int
security_object_valid_handler(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj,
                              anjay_iid_t iid,
                              void *bootstrap_instances) {
    (void) obj;
    if (!_anjay_is_bootstrap_security_instance(anjay, iid)) {
        return 0;
    }
    if (++*((uintptr_t *) bootstrap_instances) > 1) {
        return ANJAY_FOREACH_BREAK;
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
                           const anjay_uri_path_t *uri,
                           anjay_input_ctx_t *in_ctx) {
    anjay_log(DEBUG, "Bootstrap Write %s", ANJAY_DEBUG_MAKE_PATH(uri));
    if (!_anjay_uri_path_has_oid(uri)) {
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
    cancel_client_initiated_bootstrap(anjay);
    start_bootstrap_if_not_already_started(anjay);
    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, uri->oid);
    if (!obj || !*obj) {
        anjay_log(ERROR, "Object not found: %u", uri->oid);
        return ANJAY_ERR_NOT_FOUND;
    }

    int retval = -1;
    switch (uri->type) {
    case ANJAY_PATH_RESOURCE:
        retval = with_instance_on_demand(anjay, obj, uri->iid, in_ctx,
                                         write_resource,
                                         (void *) (uintptr_t) uri->rid);
        break;
    case ANJAY_PATH_INSTANCE:
        retval = write_instance(anjay, obj, uri->iid, in_ctx);
        break;
    case ANJAY_PATH_OBJECT:
        retval = write_object(anjay, obj, in_ctx);
        break;
    case ANJAY_PATH_ROOT:
        AVS_UNREACHABLE("According to specification Bootstrap Write is required"
                        " to have parameter Object ID");
    }
    if (!retval && uri->oid == ANJAY_DM_OID_SECURITY) {
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
    (void) anjay;
    (void) obj;
    AVS_LIST(anjay_iid_t) **iids_append_ptr =
            (AVS_LIST(anjay_iid_t) **) iids_append_ptr_;
    AVS_LIST(anjay_iid_t) new_element = AVS_LIST_NEW_ELEMENT(anjay_iid_t);
    if (!new_element) {
        anjay_log(ERROR, "Out of memory");
        return -1;
    }
    *new_element = iid;
    AVS_LIST_INSERT(*iids_append_ptr, new_element);
    AVS_LIST_ADVANCE_PTR(iids_append_ptr);
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
        bootstrap_remove_notify_changed(&anjay->bootstrap, (*obj)->oid, iid);
        retval = _anjay_notify_queue_instance_removed(
                &anjay->bootstrap.notification_queue, (*obj)->oid, iid);
    }
    return retval;
}

static int delete_object(anjay_t *anjay,
                         const anjay_dm_object_def_t *const *obj,
                         void *retval_ptr_) {
    // deleting from within _anjay_dm_foreach_instance()
    // would possibly invalidate cookies, so we use a temporary list
    AVS_LIST(anjay_iid_t) iids = NULL;
    AVS_LIST(anjay_iid_t) *iids_it = &iids;
    int *retval_ptr = (int *) retval_ptr_;
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
    if (!*retval_ptr) {
        *retval_ptr = retval;
    }
    return 0;
}

static int bootstrap_delete(anjay_t *anjay, const anjay_request_t *request) {
    anjay_log(DEBUG, "Bootstrap Delete %s",
              ANJAY_DEBUG_MAKE_PATH(&request->uri));
    cancel_client_initiated_bootstrap(anjay);
    start_bootstrap_if_not_already_started(anjay);

    if (request->is_bs_uri || _anjay_uri_path_has_rid(&request->uri)) {
        return ANJAY_ERR_BAD_REQUEST;
    }

    int delete_retval = 0;
    int retval = 0;
    if (_anjay_uri_path_has_oid(&request->uri)) {
        const anjay_dm_object_def_t *const *obj =
                _anjay_dm_find_object_by_oid(anjay, request->uri.oid);
        if (!obj || !*obj) {
            anjay_log(WARNING, "Object not found: %u", request->uri.oid);
            return 0;
        }

        if (_anjay_uri_path_has_iid(&request->uri)) {
            int present = _anjay_dm_instance_present(anjay, obj,
                                                     request->uri.iid, NULL);
            if (present > 0) {
                return delete_instance(anjay, obj, request->uri.iid);
            } else {
                return present;
            }
        } else {
            retval = delete_object(anjay, obj, &delete_retval);
        }
    } else {
        retval = _anjay_dm_foreach_object(anjay, delete_object, &delete_retval);
    }
    if (delete_retval) {
        return delete_retval;
    } else {
        return retval;
    }
}

#ifdef WITH_DISCOVER
static int bootstrap_discover(anjay_t *anjay, const anjay_request_t *request) {
    if (_anjay_uri_path_has_iid(&request->uri)) {
        return ANJAY_ERR_BAD_REQUEST;
    }

    if (_anjay_uri_path_has_oid(&request->uri)) {
        const anjay_dm_object_def_t *const *obj =
                _anjay_dm_find_object_by_oid(anjay, request->uri.oid);
        if (!obj) {
            return ANJAY_ERR_NOT_FOUND;
        }
        return _anjay_bootstrap_discover_object(anjay, obj);
    }
    return _anjay_bootstrap_discover(anjay);
}
#else // WITH_DISCOVER
#    define bootstrap_discover(anjay, details)                    \
        (anjay_log(ERROR, "Not supported: Bootstrap Discover %s", \
                   ANJAY_DEBUG_MAKE_PATH(&details->uri)),         \
         ANJAY_ERR_NOT_IMPLEMENTED)
#endif // WITH_DISCOVER

static void purge_bootstrap(anjay_t *anjay, const void *dummy) {
    (void) dummy;
    anjay_iid_t iid = ANJAY_IID_INVALID;
    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_SECURITY);
    if (!obj || _anjay_find_security_iid(anjay, ANJAY_SSID_BOOTSTRAP, &iid)) {
        anjay_log(WARNING, "Could not find Bootstrap Server Account to purge");
        return;
    }
    int retval = -1;
    if (obj && *obj) {
        _anjay_dm_transaction_begin(anjay);
        anjay_notify_queue_t notification = NULL;
        (void) ((retval = _anjay_dm_instance_remove(anjay, obj, iid, NULL))
                || (retval = _anjay_notify_queue_instance_removed(
                            &notification, (*obj)->oid, iid))
                || (retval = _anjay_notify_flush(anjay, &notification)));
        retval = _anjay_dm_transaction_finish(anjay, retval);
    }
    if (retval) {
        anjay_log(ERROR, "Could not purge Bootstrap Server Account %" PRIu16,
                  iid);
    }
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
        /* This function is called on each Bootstrap Finish -- i.e. we might
         * have already scheduled a purge. For this reason, we need to release
         * the purge job handle first. */
        _anjay_sched_del(anjay->sched,
                         &anjay->bootstrap.purge_bootstrap_handle);
        if (_anjay_sched(anjay->sched, &anjay->bootstrap.purge_bootstrap_handle,
                         avs_time_duration_from_scalar(timeout, AVS_TIME_S),
                         purge_bootstrap, NULL, 0)) {
            anjay_log(ERROR,
                      "Could not schedule purge of "
                      "Bootstrap Server Account %" PRIu16,
                      iid);
            return -1;
        }
    }
    return 0;
}

static int bootstrap_finish_impl(anjay_t *anjay, bool perform_timeout) {
    anjay_log(TRACE, "Bootstrap Sequence finished");

    cancel_client_initiated_bootstrap(anjay);
    start_bootstrap_if_not_already_started(anjay);
    int retval = commit_bootstrap(anjay);
    if (retval) {
        anjay_log(ERROR,
                  "Bootstrap configuration could not be committed, rejecting");
        return retval;
    }
    if ((retval = _anjay_notify_perform(anjay,
                                        anjay->bootstrap.notification_queue))) {
        anjay_log(ERROR, "Could not post-process data model after bootstrap");
    } else {
        _anjay_notify_clear_queue(&anjay->bootstrap.notification_queue);
        if (perform_timeout) {
            retval = schedule_bootstrap_timeout(anjay);
        }
    }
    if (!retval && !anjay->bootstrap.allow_server_initiated_bootstrap) {
        retval = anjay_disable_server_with_timeout(anjay, ANJAY_SSID_BOOTSTRAP,
                                                   AVS_TIME_DURATION_INVALID);
    }
    if (retval) {
        anjay_log(ERROR,
                  "Bootstrap Finish failed, re-entering bootstrap phase");
        start_bootstrap_if_not_already_started(anjay);
    } else {
        _anjay_schedule_reload_servers(anjay);
    }
    return retval;
}

static int bootstrap_finish(anjay_t *anjay) {
    return bootstrap_finish_impl(anjay, true);
}

static void
reset_client_initiated_bootstrap_backoff(anjay_bootstrap_t *bootstrap) {
    bootstrap->client_initiated_bootstrap_last_attempt =
            AVS_TIME_MONOTONIC_INVALID;
    bootstrap->client_initiated_bootstrap_holdoff = AVS_TIME_DURATION_INVALID;
}

int _anjay_bootstrap_notify_regular_connection_available(anjay_t *anjay) {
    int result = 0;
    if (anjay->bootstrap.in_progress) {
        result = bootstrap_finish_impl(anjay, false);
    } else {
        cancel_client_initiated_bootstrap(anjay);
    }
    if (!result) {
        reset_client_initiated_bootstrap_backoff(&anjay->bootstrap);
    }
    return result;
}

bool _anjay_bootstrap_server_initiated_allowed(anjay_t *anjay) {
    return anjay->bootstrap.allow_server_initiated_bootstrap;
}

int _anjay_bootstrap_object_write(anjay_t *anjay,
                                  anjay_oid_t oid,
                                  anjay_input_ctx_t *in_ctx) {
    const anjay_uri_path_t uri = {
        .type = ANJAY_PATH_OBJECT,
        .oid = oid
    };
    return bootstrap_write(anjay, &uri, in_ctx);
}

static int invoke_action(anjay_t *anjay, const anjay_request_t *request) {
    anjay_input_ctx_t *in_ctx = NULL;
    const uint16_t format =
            _anjay_translate_legacy_content_format(request->content_format);
    int result = -1;
    switch (request->action) {
    case ANJAY_ACTION_WRITE:
        if ((result = _anjay_input_dynamic_create(&in_ctx, &anjay->comm_stream,
                                                  false))) {
            anjay_log(ERROR, "could not create input context");
            return result;
        }

        if (format == ANJAY_COAP_FORMAT_TLV
                && _anjay_uri_path_has_rid(&request->uri)) {
            result = _anjay_dm_check_if_tlv_rid_matches_uri_rid(
                    in_ctx, request->uri.rid);
        }

        if (!result) {
            result = bootstrap_write(anjay, &request->uri, in_ctx);
        }
        if (_anjay_input_ctx_destroy(&in_ctx)) {
            anjay_log(ERROR, "input ctx cleanup failed");
        }
        return result;
    case ANJAY_ACTION_DELETE:
        return bootstrap_delete(anjay, request);
    case ANJAY_ACTION_DISCOVER:
        return bootstrap_discover(anjay, request);
    case ANJAY_ACTION_BOOTSTRAP_FINISH:
        return bootstrap_finish(anjay);
    default:
        anjay_log(ERROR, "Invalid action for Bootstrap Interface");
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

int _anjay_bootstrap_perform_action(anjay_t *anjay,
                                    const anjay_request_t *request) {
    anjay_msg_details_t msg_details = {
        .msg_type = AVS_COAP_MSG_ACKNOWLEDGEMENT,
        .msg_code = make_success_response_code(request->action),
        .format = request->action == ANJAY_ACTION_DISCOVER
                          ? ANJAY_COAP_FORMAT_APPLICATION_LINK
                          : AVS_COAP_FORMAT_NONE
    };

    int result =
            _anjay_coap_stream_setup_response(anjay->comm_stream, &msg_details);
    if (result) {
        return result;
    }

    return invoke_action(anjay, request);
}

static int check_request_bootstrap_response(avs_stream_abstract_t *stream) {
    const avs_coap_msg_t *response;
    if (_anjay_coap_stream_get_incoming_msg(stream, &response)) {
        anjay_log(ERROR, "could not get response");
        return -1;
    }

    const uint8_t code = avs_coap_msg_get_code(response);
    if (code != AVS_COAP_CODE_CHANGED) {
        anjay_log(ERROR, "server responded with %s (expected %s)",
                  AVS_COAP_CODE_STRING(code),
                  AVS_COAP_CODE_STRING(AVS_COAP_CODE_CHANGED));
        return -1;
    }

    return 0;
}

static int send_request_bootstrap(anjay_t *anjay) {
    const anjay_url_t *const connection_uri =
            _anjay_connection_uri(anjay->current_connection);
    anjay_msg_details_t details = {
        .msg_type = AVS_COAP_MSG_CONFIRMABLE,
        .msg_code = AVS_COAP_CODE_POST,
        .format = AVS_COAP_FORMAT_NONE
    };

    int result = -1;
    if (_anjay_copy_string_list(&details.uri_path, connection_uri->uri_path)
            || _anjay_copy_string_list(&details.uri_query,
                                       connection_uri->uri_query)
            || !AVS_LIST_APPEND(&details.uri_path, ANJAY_MAKE_STRING_LIST("bs"))
            || !AVS_LIST_APPEND(
                       &details.uri_query,
                       _anjay_make_query_string_list(NULL, anjay->endpoint_name,
                                                     NULL, NULL, NULL))) {
        anjay_log(ERROR, "could not initialize request headers");
        goto cleanup;
    }

    if ((result = _anjay_coap_stream_setup_request(anjay->comm_stream, &details,
                                                   NULL))
            || (result = avs_stream_finish_message(anjay->comm_stream))
            || (result =
                        check_request_bootstrap_response(anjay->comm_stream))) {
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

static void request_bootstrap_job(anjay_t *anjay, const void *dummy);

static int schedule_request_bootstrap(anjay_t *anjay) {
    avs_time_monotonic_t now = avs_time_monotonic_now();
    if (!avs_time_monotonic_valid(
                anjay->bootstrap.client_initiated_bootstrap_last_attempt)) {
        anjay->bootstrap.client_initiated_bootstrap_last_attempt = now;
    }
    if (!avs_time_duration_valid(
                anjay->bootstrap.client_initiated_bootstrap_holdoff)) {
        anjay->bootstrap.client_initiated_bootstrap_holdoff =
                AVS_TIME_DURATION_ZERO;
    }

    avs_time_monotonic_t attempt_instant = avs_time_monotonic_add(
            anjay->bootstrap.client_initiated_bootstrap_last_attempt,
            anjay->bootstrap.client_initiated_bootstrap_holdoff);
    if (_anjay_sched(anjay->sched,
                     &anjay->bootstrap.client_initiated_bootstrap_handle,
                     avs_time_monotonic_diff(attempt_instant, now),
                     request_bootstrap_job, NULL, 0)) {
        anjay_log(ERROR, "Could not schedule Client Initiated Bootstrap");
        return -1;
    }

    const avs_time_duration_t MIN_HOLDOFF =
            avs_time_duration_from_scalar(3, AVS_TIME_S);
    const avs_time_duration_t MAX_HOLDOFF =
            avs_time_duration_from_scalar(120, AVS_TIME_S);

    anjay->bootstrap.client_initiated_bootstrap_last_attempt = attempt_instant;
    anjay->bootstrap.client_initiated_bootstrap_holdoff = avs_time_duration_mul(
            anjay->bootstrap.client_initiated_bootstrap_holdoff, 2);
    if (avs_time_duration_less(
                anjay->bootstrap.client_initiated_bootstrap_holdoff,
                MIN_HOLDOFF)) {
        anjay->bootstrap.client_initiated_bootstrap_holdoff = MIN_HOLDOFF;
    } else if (avs_time_duration_less(
                       MAX_HOLDOFF,
                       anjay->bootstrap.client_initiated_bootstrap_holdoff)) {
        anjay->bootstrap.client_initiated_bootstrap_holdoff = MAX_HOLDOFF;
    }
    return 0;
}

static void request_bootstrap_job(anjay_t *anjay, const void *dummy) {
    anjay_log(TRACE, "sending Client Initiated Bootstrap");

    (void) dummy;

    int result = -1;
    anjay_server_info_t *server =
            _anjay_servers_find_active(anjay, ANJAY_SSID_BOOTSTRAP);
    anjay_connection_ref_t connection;
    if (!server) {
        goto finish;
    }
    connection.server = server;
    connection.conn_type = _anjay_server_primary_conn_type(server);
    if (connection.conn_type == ANJAY_CONNECTION_UNSET) {
        goto finish;
    }
    if (_anjay_conn_session_tokens_equal(
                anjay->bootstrap.bootstrap_session_token,
                _anjay_server_primary_session_token(server))) {
        anjay_log(DEBUG, "Bootstrap already started on the same connection");
        result = 0;
        goto finish;
    }
    if (_anjay_bind_server_stream(anjay, connection)) {
        anjay_log(ERROR, "could not get stream for bootstrap server");
        goto finish;
    }

    result = send_request_bootstrap(anjay);
    if (!result) {
        start_bootstrap_if_not_already_started(anjay);
    }

    _anjay_release_server_stream(anjay);

    if (result) {
        anjay_log(ERROR, "could not send Request Bootstrap");
    }
finish:
    if (server) {
        if (result == AVS_COAP_CTX_ERR_TIMEOUT) {
            _anjay_server_on_registration_timeout(anjay, server);
        } else if (result) {
            _anjay_server_on_server_communication_error(anjay, server);
        }
    }
}

static int64_t client_hold_off_time_s(anjay_t *anjay) {
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
        return -1;
    }
    return holdoff_s;
}

int _anjay_bootstrap_request_if_appropriate(anjay_t *anjay) {
    if (anjay->bootstrap.client_initiated_bootstrap_handle) {
        return 0;
    }
    // schedule Client Initiated Bootstrap if not attempted already;
    // if bootstrap is already in progress, schedule_request_bootstrap()
    // will check if the endpoint changed and re-request if so
    if (!avs_time_monotonic_valid(
                anjay->bootstrap.client_initiated_bootstrap_last_attempt)) {
        int64_t holdoff_s = client_hold_off_time_s(anjay);
        if (holdoff_s < 0) {
            anjay_log(INFO, "Client Hold Off Time not set or invalid, "
                            "not scheduling Client Initiated Bootstrap");
            return 0;
        }
        anjay_log(DEBUG, "scheduling Client Initiated Bootstrap");
        anjay->bootstrap.client_initiated_bootstrap_holdoff =
                avs_time_duration_from_scalar(holdoff_s, AVS_TIME_S);
    }
    return schedule_request_bootstrap(anjay);
}

void _anjay_bootstrap_init(anjay_bootstrap_t *bootstrap,
                           bool allow_server_initiated_bootstrap) {
    bootstrap->allow_server_initiated_bootstrap =
            allow_server_initiated_bootstrap;
    _anjay_conn_session_token_reset(&bootstrap->bootstrap_session_token);
    reset_client_initiated_bootstrap_backoff(bootstrap);
}

void _anjay_bootstrap_cleanup(anjay_t *anjay) {
    cancel_client_initiated_bootstrap(anjay);
    reset_client_initiated_bootstrap_backoff(&anjay->bootstrap);
    abort_bootstrap(anjay);
    _anjay_sched_del(anjay->sched, &anjay->bootstrap.purge_bootstrap_handle);
    _anjay_notify_clear_queue(&anjay->bootstrap.notification_queue);
}

#ifdef ANJAY_TEST
#    include "test/bootstrap.c"
#endif // ANJAY_TEST
