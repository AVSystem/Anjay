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

#include <inttypes.h>

#include <avsystem/commons/avs_errno.h>
#include <avsystem/commons/avs_stream_inbuf.h>

#include <avsystem/coap/async_client.h>
#include <avsystem/coap/code.h>
#include <avsystem/coap/streaming.h>

#include <anjay_modules/anjay_bootstrap.h>
#include <anjay_modules/anjay_notify.h>
#include <anjay_modules/anjay_time_defs.h>

#include "anjay_bootstrap_core.h"
#include "anjay_core.h"
#include "anjay_io_core.h"
#include "anjay_servers_utils.h"
#include "coap/anjay_content_format.h"
#include "dm/anjay_discover.h"
#include "dm/anjay_dm_read.h"
#include "dm/anjay_query.h"

#ifdef ANJAY_TEST
#    include "tests/core/bootstrap_mock.h"
#endif // ANJAY_TEST

VISIBILITY_SOURCE_BEGIN

#ifdef ANJAY_WITH_BOOTSTRAP

static void cancel_client_initiated_bootstrap(anjay_t *anjay) {
    avs_sched_del(&anjay->bootstrap.client_initiated_bootstrap_handle);
}

static void cancel_est_sren(anjay_t *anjay) {
    (void) anjay;
}

static int suspend_nonbootstrap_server(anjay_t *anjay,
                                       anjay_server_info_t *server,
                                       void *data) {
    (void) anjay;
    (void) data;
    if (_anjay_server_ssid(server) != ANJAY_SSID_BOOTSTRAP) {
        anjay_connection_type_t conn_type;
        ANJAY_CONNECTION_TYPE_FOREACH(conn_type) {
            _anjay_connection_suspend((anjay_connection_ref_t) {
                .server = server,
                .conn_type = conn_type
            });
        }
    }
    return 0;
}

static void start_bootstrap_if_not_already_started(
        anjay_t *anjay, anjay_connection_ref_t bootstrap_connection) {
    if (bootstrap_connection.server) {
        anjay->bootstrap.bootstrap_session_token =
                _anjay_server_primary_session_token(
                        bootstrap_connection.server);
        if (avs_coap_exchange_id_valid(
                    anjay->bootstrap.outgoing_request_exchange_id)) {
            avs_coap_exchange_cancel(
                    _anjay_connection_get_coap(bootstrap_connection),
                    anjay->bootstrap.outgoing_request_exchange_id);
        }
    }
    if (!anjay->bootstrap.in_progress) {
        // clear inactive servers so that they won't attempt to retry; they will
        // be recreated during _anjay_schedule_reload_servers() after bootstrap
        // procedure is finished
        _anjay_servers_cleanup_inactive(anjay);
        // suspend active connections
        _anjay_servers_foreach_active(anjay, suspend_nonbootstrap_server, NULL);

        _anjay_dm_transaction_begin(anjay);
        avs_sched_del(&anjay->bootstrap.purge_bootstrap_handle);
    }
    anjay->bootstrap.in_progress = true;
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
    case ANJAY_ACTION_READ:
        return AVS_COAP_CODE_CONTENT;
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
                             anjay_input_ctx_t *in_ctx);

static int write_resource(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj,
                          anjay_iid_t iid,
                          anjay_input_ctx_t *in_ctx) {
    (void) iid;
    return _anjay_dm_write_resource(anjay, obj, in_ctx,
                                    &anjay->bootstrap.notification_queue);
}

static int write_instance_inner(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj,
                                anjay_iid_t iid,
                                anjay_input_ctx_t *in_ctx) {
    int retval;
    anjay_uri_path_t path;
    while (!(retval = _anjay_input_get_path(in_ctx, &path, NULL))) {
        if (path.ids[ANJAY_ID_IID] != iid) {
            /* another instance */
            break;
        }
        if (_anjay_uri_path_has(&path, ANJAY_ID_RID)) {
            /* non-empty instance */
            retval = write_resource(anjay, obj, iid, in_ctx);
        } else {
            retval = _anjay_input_next_entry(in_ctx);
        }
        if (retval == ANJAY_ERR_NOT_FOUND
                || retval == ANJAY_ERR_NOT_IMPLEMENTED) {
            // LwM2M spec, 5.2.7.1 BOOTSTRAP WRITE:
            // "When the 'Write' operation targets an Object or an Object
            // Instance, the LwM2M Client MUST ignore optional resources it does
            // not support in the payload." - so, continue on these errors.
            anjay_log(WARNING,
                      _("Ignoring error during BOOTSTRAP WRITE to ") "%s" _(
                              ": ") "%d",
                      ANJAY_DEBUG_MAKE_PATH(&path), retval);
        } else if (retval) {
            return retval;
        }
    }
    return (retval == ANJAY_GET_PATH_END) ? 0 : retval;
}

static int with_instance_on_demand(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj,
                                   anjay_iid_t iid,
                                   anjay_input_ctx_t *in_ctx,
                                   with_instance_on_demand_cb_t callback) {
    int result = 0;
    int ipresent = _anjay_dm_instance_present(anjay, obj, iid);
    if (ipresent < 0) {
        return ipresent;
    } else if (ipresent == 0
               && (result = _anjay_dm_call_instance_create(anjay, obj, iid,
                                                           NULL))) {
        anjay_log(DEBUG,
                  _("Instance Create handler for object ") "%" PRIu16 _(
                          " failed"),
                  (*obj)->oid);
        return result;
    }

    if (!result) {
        result = callback(anjay, obj, iid, in_ctx);
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
                                   write_instance_inner);
}

static int write_object(anjay_t *anjay,
                        const anjay_dm_object_def_t *const *obj,
                        anjay_input_ctx_t *in_ctx) {
    // should it remove existing instances?
    int retval;
    do {
        anjay_uri_path_t path;
        if ((retval = _anjay_input_get_path(in_ctx, &path, NULL))) {
            if (retval == ANJAY_GET_PATH_END) {
                retval = 0;
            }
            break;
        }
        if (path.ids[ANJAY_ID_IID] == ANJAY_ID_INVALID) {
            retval = ANJAY_ERR_BAD_REQUEST;
            break;
        }
        retval = write_instance(anjay, obj, path.ids[ANJAY_ID_IID], in_ctx);
    } while (!retval);
    return retval;
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

static int bootstrap_write_impl(anjay_t *anjay,
                                const anjay_uri_path_t *uri,
                                anjay_input_ctx_t *in_ctx) {
    anjay_log(LAZY_DEBUG, _("Bootstrap Write ") "%s",
              ANJAY_DEBUG_MAKE_PATH(uri));
    if (!_anjay_uri_path_has(uri, ANJAY_ID_OID)
            || _anjay_uri_path_has(uri, ANJAY_ID_RIID)) {
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
    cancel_client_initiated_bootstrap(anjay);
    cancel_est_sren(anjay);
    start_bootstrap_if_not_already_started(anjay, anjay->current_connection);
    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, uri->ids[ANJAY_ID_OID]);
    if (!obj || !*obj) {
        anjay_log(DEBUG, _("Object not found: ") "%u", uri->ids[ANJAY_ID_OID]);
        return ANJAY_ERR_NOT_FOUND;
    }

    int retval = -1;
    if (_anjay_uri_path_leaf_is(uri, ANJAY_ID_OID)) {
        retval = write_object(anjay, obj, in_ctx);
    } else if (_anjay_uri_path_leaf_is(uri, ANJAY_ID_IID)) {
        retval = write_instance(anjay, obj, uri->ids[ANJAY_ID_IID], in_ctx);
    } else if (_anjay_uri_path_leaf_is(uri, ANJAY_ID_RID)) {
        retval = with_instance_on_demand(anjay, obj, uri->ids[ANJAY_ID_IID],
                                         in_ctx, write_resource);
    }
    if (!retval && uri->ids[ANJAY_ID_OID] == ANJAY_DM_OID_SECURITY) {
        if (has_multiple_bootstrap_security_instances(anjay)) {
            anjay_log(DEBUG, _("Multiple Security Object instances configured "
                               "for the Bootstrap Server Account"));
            retval = ANJAY_ERR_BAD_REQUEST;
        }
    }

    return retval;
}

static int delete_instance(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *obj,
                           anjay_iid_t iid) {
    int retval = _anjay_dm_call_instance_remove(anjay, obj, iid, NULL);
    if (retval) {
        anjay_log(WARNING,
                  _("delete_instance: cannot delete ") "/%d/%d" _(": ") "%d",
                  (*obj)->oid, iid, retval);
    } else {
        bootstrap_remove_notify_changed(&anjay->bootstrap, (*obj)->oid, iid);
        retval = _anjay_notify_queue_instance_removed(
                &anjay->bootstrap.notification_queue, (*obj)->oid, iid);
    }
    return retval;
}

typedef struct {
    bool skip_bootstrap;
    int retval;
} delete_object_arg_t;

static int delete_object(anjay_t *anjay,
                         const anjay_dm_object_def_t *const *obj,
                         void *arg_) {
    // the contract forbids deleting instances from within
    // _anjay_dm_list_instances(), so we use a temporary list
    AVS_LIST(anjay_iid_t) iids = NULL;
    delete_object_arg_t *arg = (delete_object_arg_t *) arg_;
    int retval = _anjay_dm_get_sorted_instance_list(anjay, obj, &iids);
    if (!retval) {
        AVS_LIST(anjay_iid_t) iid;
        AVS_LIST_FOREACH(iid, iids) {
            if (arg->skip_bootstrap && (*obj)->oid == ANJAY_DM_OID_SECURITY
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
    if (!arg->retval) {
        arg->retval = retval;
    }
    return 0;
}

static int bootstrap_delete(anjay_t *anjay, const anjay_request_t *request) {
    anjay_log(LAZY_DEBUG, _("Bootstrap Delete ") "%s",
              ANJAY_DEBUG_MAKE_PATH(&request->uri));
    cancel_client_initiated_bootstrap(anjay);
    cancel_est_sren(anjay);
    start_bootstrap_if_not_already_started(anjay, anjay->current_connection);

    if (request->is_bs_uri
            || _anjay_uri_path_has(&request->uri, ANJAY_ID_RID)) {
        return ANJAY_ERR_BAD_REQUEST;
    }

    delete_object_arg_t delete_arg = {
        .skip_bootstrap = true,
        .retval = 0
    };
    int retval = 0;
    if (_anjay_uri_path_has(&request->uri, ANJAY_ID_OID)) {
        const anjay_dm_object_def_t *const *obj =
                _anjay_dm_find_object_by_oid(anjay,
                                             request->uri.ids[ANJAY_ID_OID]);
        if (!obj || !*obj) {
            anjay_log(WARNING, _("Object not found: ") "%u",
                      request->uri.ids[ANJAY_ID_OID]);
            return 0;
        }

        if (_anjay_uri_path_leaf_is(&request->uri, ANJAY_ID_IID)) {
            int present =
                    _anjay_dm_instance_present(anjay, obj,
                                               request->uri.ids[ANJAY_ID_IID]);
            if (present > 0) {
                return delete_instance(anjay, obj,
                                       request->uri.ids[ANJAY_ID_IID]);
            } else {
                return present;
            }
        } else {
            retval = delete_object(anjay, obj, &delete_arg);
        }
    } else {
        retval = _anjay_dm_foreach_object(anjay, delete_object, &delete_arg);
    }
    if (delete_arg.retval) {
        return delete_arg.retval;
    } else {
        return retval;
    }
}

#    ifdef ANJAY_WITH_DISCOVER
static int bootstrap_discover(anjay_t *anjay, const anjay_request_t *request) {
    if (_anjay_uri_path_has(&request->uri, ANJAY_ID_IID)) {
        return ANJAY_ERR_BAD_REQUEST;
    }

    anjay_msg_details_t msg_details = {
        .msg_code = make_success_response_code(request->action),
        .format = AVS_COAP_FORMAT_LINK_FORMAT
    };
    avs_stream_t *response_stream =
            _anjay_coap_setup_response_stream(request->ctx, &msg_details);
    if (!response_stream) {
        return -1;
    }

    return _anjay_bootstrap_discover(anjay, response_stream,
                                     request->uri.ids[ANJAY_ID_OID]);
}
#    else // ANJAY_WITH_DISCOVER
#        define bootstrap_discover(anjay, details)                          \
            (anjay_log(ERROR, _("Not supported: Bootstrap Discover ") "%s", \
                       ANJAY_DEBUG_MAKE_PATH(&details->uri)),               \
             ANJAY_ERR_NOT_IMPLEMENTED)
#    endif // ANJAY_WITH_DISCOVER

static void purge_bootstrap(avs_sched_t *sched, const void *dummy) {
    (void) dummy;
    anjay_t *anjay = _anjay_get_from_sched(sched);
    anjay_iid_t iid;
    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_SECURITY);
    if (!obj
            || (iid = _anjay_find_bootstrap_security_iid(anjay))
                           == ANJAY_ID_INVALID) {
        anjay_log(WARNING,
                  _("Could not find Bootstrap Server Account to purge"));
        return;
    }
    int retval = -1;
    if (obj && *obj) {
        _anjay_dm_transaction_begin(anjay);
        anjay_notify_queue_t notification = NULL;
        (void) ((retval = _anjay_dm_call_instance_remove(anjay, obj, iid, NULL))
                || (retval = _anjay_notify_queue_instance_removed(
                            &notification, (*obj)->oid, iid))
                || (retval = _anjay_notify_flush(anjay, &notification)));
        retval = _anjay_dm_transaction_finish(anjay, retval);
    }
    if (retval) {
        anjay_log(WARNING,
                  _("Could not purge Bootstrap Server Account ") "%" PRIu16,
                  iid);
    }
}

static int schedule_bootstrap_timeout(anjay_t *anjay) {
    anjay_iid_t iid;
    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_SECURITY);
    if (!obj
            || (iid = _anjay_find_bootstrap_security_iid(anjay))
                           == ANJAY_ID_INVALID) {
        anjay_log(DEBUG, _("Could not find Bootstrap Server Account to purge"));
        return 0;
    }

    const anjay_uri_path_t res_path =
            MAKE_RESOURCE_PATH(ANJAY_DM_OID_SECURITY, iid,
                               ANJAY_DM_RID_SECURITY_BOOTSTRAP_TIMEOUT);

    int64_t timeout;
    if (!_anjay_dm_read_resource_i64(anjay, &res_path, &timeout)
            && timeout > 0) {
        /* This function is called on each Bootstrap Finish -- i.e. we might
         * have already scheduled a purge. For this reason, we need to release
         * the purge job handle first. */
        if (AVS_SCHED_DELAYED(
                    anjay->sched, &anjay->bootstrap.purge_bootstrap_handle,
                    avs_time_duration_from_scalar(timeout, AVS_TIME_S),
                    purge_bootstrap, NULL, 0)) {
            anjay_log(ERROR,
                      _("Could not schedule purge of Bootstrap Server "
                        "Account ") "%" PRIu16,
                      iid);
            return -1;
        }
    }
    return 0;
}

static int validate_bootstrap_configuration(anjay_t *anjay) {
    cancel_client_initiated_bootstrap(anjay);
    start_bootstrap_if_not_already_started(anjay, anjay->current_connection);
    if (_anjay_dm_transaction_validate(anjay)) {
        anjay_log(WARNING, _("Bootstrap configuration is invalid, rejecting"));
        return ANJAY_ERR_NOT_ACCEPTABLE;
    }
    return 0;
}

#    define BOOTSTRAP_FINISH_PERFORM_TIMEOUT (1 << 0)
#    define BOOTSTRAP_FINISH_DISABLE_SERVER (1 << 1)

static int bootstrap_finish_impl(anjay_t *anjay, int flags) {
    anjay_log(TRACE, _("Bootstrap Sequence finished"));
    anjay->bootstrap.in_progress = false;
    _anjay_conn_session_token_reset(&anjay->bootstrap.bootstrap_session_token);
    int retval = _anjay_dm_transaction_finish_without_validation(anjay, 0);
    if (retval) {
        anjay_log(
                WARNING,
                _("Bootstrap configuration could not be committed, rejecting"));
        return retval;
    }
    if ((retval = _anjay_notify_perform_without_servers(
                 anjay, anjay->bootstrap.notification_queue))) {
        anjay_log(WARNING,
                  _("Could not post-process data model after bootstrap"));
    } else {
        _anjay_notify_clear_queue(&anjay->bootstrap.notification_queue);
        if (flags & BOOTSTRAP_FINISH_PERFORM_TIMEOUT) {
            retval = schedule_bootstrap_timeout(anjay);
        }
    }
    if ((flags & BOOTSTRAP_FINISH_DISABLE_SERVER) && !retval) {
        if (!anjay->bootstrap.allow_legacy_server_initiated_bootstrap) {
            retval = anjay_disable_server_with_timeout(
                    anjay, ANJAY_SSID_BOOTSTRAP, AVS_TIME_DURATION_INVALID);
        }
    }
    if (retval) {
        anjay_log(WARNING,
                  _("Bootstrap Finish failed, re-entering bootstrap phase"));
        start_bootstrap_if_not_already_started(anjay,
                                               anjay->current_connection);
    } else {
        _anjay_schedule_reload_servers(anjay);
    }
    return retval;
}

static int bootstrap_finish(anjay_t *anjay) {
    int result = validate_bootstrap_configuration(anjay);
    if (result) {
        return result;
    }
    return bootstrap_finish_impl(anjay,
                                 BOOTSTRAP_FINISH_PERFORM_TIMEOUT
                                         | BOOTSTRAP_FINISH_DISABLE_SERVER);
}

static void
reset_client_initiated_bootstrap_backoff(anjay_bootstrap_t *bootstrap) {
    bootstrap->client_initiated_bootstrap_last_attempt =
            AVS_TIME_MONOTONIC_INVALID;
    bootstrap->client_initiated_bootstrap_holdoff = AVS_TIME_DURATION_INVALID;
}

int _anjay_bootstrap_notify_regular_connection_available(anjay_t *anjay) {
    if (avs_coap_exchange_id_valid(
                anjay->bootstrap.outgoing_request_exchange_id)) {
        // Let the bootstrap request finish. When a response comes, bootstrap
        // procedure will be started, which will suspend all non-bootstrap
        // connections, including the one whose readiness is being notified
        // with this function.
        return 0;
    }
    int result = 0;
    if (anjay->bootstrap.in_progress) {
        (void) ((result = validate_bootstrap_configuration(anjay))
                || (result = bootstrap_finish_impl(
                            anjay, BOOTSTRAP_FINISH_DISABLE_SERVER)));
    } else {
        cancel_client_initiated_bootstrap(anjay);
    }
    if (!result) {
        reset_client_initiated_bootstrap_backoff(&anjay->bootstrap);
    }
    return result;
}

bool _anjay_bootstrap_legacy_server_initiated_allowed(anjay_t *anjay) {
    return anjay->bootstrap.allow_legacy_server_initiated_bootstrap;
}

bool _anjay_bootstrap_in_progress(anjay_t *anjay) {
    return anjay->bootstrap.in_progress;
}

static int bootstrap_write(anjay_t *anjay, const anjay_request_t *request) {
    anjay_input_ctx_t *in_ctx;
    int result;
    if ((result = _anjay_input_dynamic_construct(
                 &in_ctx, request->payload_stream, request))) {
        anjay_log(ERROR, _("could not create input context"));
        return result;
    }

    if (!result) {
        result = bootstrap_write_impl(anjay, &request->uri, in_ctx);
    }
    if (_anjay_input_ctx_destroy(&in_ctx)) {
        anjay_log(ERROR, _("input ctx cleanup failed"));
    }
    return result;
}

static void timeout_bootstrap_finish(avs_sched_t *sched, const void *dummy) {
    (void) dummy;
    anjay_t *anjay = _anjay_get_from_sched(sched);
    anjay_log(WARNING, _("Bootstrap Finish not received in time - aborting"));
    // Abort client-initiated-bootstrap entirely. After that,
    // anjay_all_connections_failed() starts returning true (if the
    // bootstrap was the only server), which gives the user an
    // opportunity to react accordingly.
    anjay_server_info_t *server =
            _anjay_servers_find_active(anjay, ANJAY_SSID_BOOTSTRAP);
    if (server) {
        _anjay_server_on_failure(server, "not reachable");
    }
}

static int schedule_finish_timeout(anjay_t *anjay,
                                   anjay_connection_ref_t connection) {
    if (AVS_SCHED_DELAYED(anjay->sched, &anjay->bootstrap.finish_timeout_handle,
                          _anjay_exchange_lifetime_for_transport(
                                  anjay,
                                  _anjay_connection_transport(connection)),
                          timeout_bootstrap_finish, NULL, 0)) {
        anjay_log(ERROR, _("could not schedule finish timeout"));
        return -1;
    }
    return 0;
}

static int invoke_action(anjay_t *anjay, const anjay_request_t *request) {
    // Cancel the job explicitly, because it may happen that Bootstrap Finish
    // succeeds, but schedule_finish_timeout() fails, leaving the job on the
    // scheduler.
    avs_sched_del(&anjay->bootstrap.finish_timeout_handle);

    int result;
    switch (request->action) {
    case ANJAY_ACTION_WRITE:
        result = bootstrap_write(anjay, request);
        break;
    case ANJAY_ACTION_DELETE:
        result = bootstrap_delete(anjay, request);
        break;
    case ANJAY_ACTION_DISCOVER:
        result = bootstrap_discover(anjay, request);
        break;
    case ANJAY_ACTION_BOOTSTRAP_FINISH:
        result = bootstrap_finish(anjay);
        break;
    default:
        anjay_log(DEBUG, _("Invalid action for Bootstrap Interface"));
        result = ANJAY_ERR_METHOD_NOT_ALLOWED;
        break;
    }
    if ((request->action != ANJAY_ACTION_BOOTSTRAP_FINISH || result)
            && schedule_finish_timeout(anjay, anjay->current_connection)) {
        result = -1;
    }
    return result;
}

int _anjay_bootstrap_perform_action(anjay_t *anjay,
                                    const anjay_request_t *request) {
    anjay_msg_details_t msg_details = {
        .msg_code = make_success_response_code(request->action),
        .format = AVS_COAP_FORMAT_NONE
    };

    if (!_anjay_coap_setup_response_stream(request->ctx, &msg_details)) {
        return -1;
    }

    return invoke_action(anjay, request);
}

static void send_request_bootstrap(anjay_t *anjay,
                                   anjay_connection_ref_t connection);

static void bootstrap_request_response_handler(
        avs_coap_ctx_t *ctx,
        avs_coap_exchange_id_t exchange_id,
        avs_coap_client_request_state_t result,
        const avs_coap_client_async_response_t *response,
        avs_error_t err,
        void *anjay_) {
    anjay_t *anjay = (anjay_t *) anjay_;
    if (result != AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT) {
        anjay->bootstrap.outgoing_request_exchange_id =
                AVS_COAP_EXCHANGE_ID_INVALID;
    }
    if (result != AVS_COAP_CLIENT_REQUEST_CANCEL) {
        anjay->bootstrap.bootstrap_trigger = false;
    }

    const anjay_connection_ref_t connection =
            _anjay_servers_find_active_primary_connection(anjay,
                                                          ANJAY_SSID_BOOTSTRAP);
    assert(connection.server || result == AVS_COAP_CLIENT_REQUEST_CANCEL);
    switch (result) {
    case AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT:
        // Note: this will recursively call this function with
        // AVS_COAP_CLIENT_REQUEST_CANCEL.
        avs_coap_exchange_cancel(ctx, exchange_id);
        // fall-through

    case AVS_COAP_CLIENT_REQUEST_OK:
        assert(connection.conn_type != ANJAY_CONNECTION_UNSET);
        if (response->header.code != AVS_COAP_CODE_CHANGED) {
            anjay_log(WARNING,
                      _("server responded with ") "%s" _(" (expected ") "%s" _(
                              ")"),
                      AVS_COAP_CODE_STRING(response->header.code),
                      AVS_COAP_CODE_STRING(AVS_COAP_CODE_CHANGED));
            _anjay_server_on_server_communication_error(connection.server,
                                                        avs_errno(AVS_EPROTO));
        } else {
            anjay_log(INFO,
                      _("Client-initiated Bootstrap successfully started"));
            start_bootstrap_if_not_already_started(anjay, connection);
            if (schedule_finish_timeout(anjay, connection)) {
                _anjay_server_on_server_communication_error(
                        connection.server, avs_errno(AVS_ENOMEM));
            }
        }
        break;

    case AVS_COAP_CLIENT_REQUEST_FAIL: {
        if (avs_is_err(err)) {
            if (err.category == AVS_COAP_ERR_CATEGORY
                    && err.code == AVS_COAP_ERR_TIMEOUT) {
                anjay_log(WARNING, _("could not request bootstrap: timeout"));
                _anjay_server_on_server_communication_timeout(
                        connection.server);
            } else {
                anjay_log(WARNING, _("could not send Request Bootstrap: ") "%s",
                          AVS_COAP_STRERROR(err));
                _anjay_server_on_server_communication_error(connection.server,
                                                            err);
            }
        }
        break;
    }

    case AVS_COAP_CLIENT_REQUEST_CANCEL:
        break;
    }
}

static void send_request_bootstrap(anjay_t *anjay,
                                   anjay_connection_ref_t connection) {
    const anjay_url_t *const connection_uri = _anjay_connection_uri(connection);
    avs_coap_request_header_t request = {
        .code = AVS_COAP_CODE_POST
    };

    avs_coap_ctx_t *coap = _anjay_connection_get_coap(connection);
    assert(coap);

    avs_error_t err;
    if (avs_is_err((err = avs_coap_options_dynamic_init(&request.options)))
            || avs_is_err((err = _anjay_coap_add_string_options(
                                   &request.options,
                                   connection_uri->uri_path,
                                   AVS_COAP_OPTION_URI_PATH)))
            || avs_is_err((err = avs_coap_options_add_string(
                                   &request.options, AVS_COAP_OPTION_URI_PATH,
                                   "bs")))
            || avs_is_err((err = _anjay_coap_add_string_options(
                                   &request.options,
                                   connection_uri->uri_query,
                                   AVS_COAP_OPTION_URI_QUERY)))
            || avs_is_err((err = _anjay_coap_add_query_options(
                                   &request.options, NULL, anjay->endpoint_name,
                                   NULL, NULL, false, NULL)))) {
        anjay_log(ERROR, _("could not initialize request headers"));
        anjay->bootstrap.bootstrap_trigger = false;
        _anjay_server_on_server_communication_error(connection.server, err);
    } else {
        assert(!avs_coap_exchange_id_valid(
                anjay->bootstrap.outgoing_request_exchange_id));
        if (avs_is_err((err = avs_coap_client_send_async_request(
                                coap,
                                &anjay->bootstrap.outgoing_request_exchange_id,
                                &request, NULL, NULL,
                                bootstrap_request_response_handler, anjay)))) {
            anjay_log(WARNING, _("could not send Request Bootstrap: ") "%s",
                      AVS_COAP_STRERROR(err));
            anjay->bootstrap.bootstrap_trigger = false;
            _anjay_server_on_server_communication_error(connection.server, err);
        }
    }
    avs_coap_options_cleanup(&request.options);
}

static void request_bootstrap_job(avs_sched_t *sched, const void *dummy);

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
    anjay_log(DEBUG, _("Scheduling bootstrap in ") "%s" _(" seconds"),
              AVS_TIME_DURATION_AS_STRING(
                      anjay->bootstrap.client_initiated_bootstrap_holdoff));
    if (AVS_SCHED_DELAYED(anjay->sched,
                          &anjay->bootstrap.client_initiated_bootstrap_handle,
                          avs_time_monotonic_diff(attempt_instant, now),
                          request_bootstrap_job, NULL, 0)) {
        anjay_log(WARNING, _("Could not schedule Client Initiated Bootstrap"));
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

static void request_bootstrap_job(avs_sched_t *sched, const void *dummy) {
    anjay_t *anjay = _anjay_get_from_sched(sched);
    anjay_log(TRACE, _("sending Client Initiated Bootstrap"));

    (void) dummy;

    const anjay_connection_ref_t connection =
            _anjay_servers_find_active_primary_connection(anjay,
                                                          ANJAY_SSID_BOOTSTRAP);
    if (!connection.server) {
        anjay_log(DEBUG, _("Bootstrap server connection not available to send "
                           "Request Bootstrap through"));
        anjay->bootstrap.bootstrap_trigger = false;
        return;
    }
    if (connection.conn_type == ANJAY_CONNECTION_UNSET) {
        goto error;
    }
    if (_anjay_conn_session_tokens_equal(
                anjay->bootstrap.bootstrap_session_token,
                _anjay_server_primary_session_token(connection.server))) {
        anjay_log(DEBUG, _("Bootstrap already started on the same connection"));
        goto error;
    }
    if (!_anjay_connection_get_online_socket(connection)) {
        anjay_log(DEBUG, _("bootstrap server connection is not online"));
        goto error;
    }
    // Bootstrap Server has no concept of "registration", but we're reusing the
    // registration_info field in the server structure to store which LwM2M
    // version was used for Request Bootstrap. This is used to determine whether
    // Preferred Content Type is sent in the Request Bootstrap message.
    _anjay_server_update_registration_info(
            connection.server, NULL, ANJAY_LWM2M_VERSION_1_0, false, NULL);
    send_request_bootstrap(anjay, connection);
    return;
error:
    anjay->bootstrap.bootstrap_trigger = false;
    _anjay_server_on_server_communication_error(connection.server,
                                                avs_errno(AVS_EPROTO));
}

static int64_t client_hold_off_time_s(anjay_t *anjay) {
    anjay_iid_t security_iid = _anjay_find_bootstrap_security_iid(anjay);
    if (security_iid == ANJAY_ID_INVALID) {
        anjay_log(WARNING,
                  _("could not find server Security IID of the Bootstrap "
                    "Server"));
        return -1;
    }

    const anjay_uri_path_t path =
            MAKE_RESOURCE_PATH(ANJAY_DM_OID_SECURITY, security_iid,
                               ANJAY_DM_RID_SECURITY_CLIENT_HOLD_OFF_TIME);
    int64_t holdoff_s;
    if (_anjay_dm_read_resource_i64(anjay, &path, &holdoff_s)
            || holdoff_s < 0) {
        return -1;
    }
    return holdoff_s;
}

int _anjay_perform_bootstrap_action_if_appropriate(
        anjay_t *anjay,
        anjay_server_info_t *bootstrap_server,
        anjay_bootstrap_action_t action) {
    if (!bootstrap_server && action != ANJAY_BOOTSTRAP_ACTION_NONE) {
        return anjay_enable_server(anjay, ANJAY_SSID_BOOTSTRAP);
    }

    switch (action) {
    case ANJAY_BOOTSTRAP_ACTION_NONE:
        return 0;
    case ANJAY_BOOTSTRAP_ACTION_REQUEST: {
        // schedule Client Initiated Bootstrap if not attempted already;
        // if bootstrap is already in progress, schedule_request_bootstrap()
        // will check if the endpoint changed and re-request if so
        if (!avs_time_monotonic_valid(
                    anjay->bootstrap.client_initiated_bootstrap_last_attempt)) {
            int64_t holdoff_s = client_hold_off_time_s(anjay);
            if (holdoff_s < 0) {
                anjay_log(INFO,
                          _("Client Hold Off Time not set or invalid, not "
                            "scheduling Client Initiated Bootstrap"));
                return 0;
            }
            anjay_log(DEBUG, _("scheduling Client Initiated Bootstrap"));
            anjay->bootstrap.client_initiated_bootstrap_holdoff =
                    avs_time_duration_from_scalar(holdoff_s, AVS_TIME_S);
        }
        int result = schedule_request_bootstrap(anjay);
        if (!result) {
            cancel_est_sren(anjay);
        }
        return result;
    }
    }
    AVS_UNREACHABLE("invalid action");
    return -1;
}

void _anjay_bootstrap_init(anjay_bootstrap_t *bootstrap,
                           bool allow_legacy_server_initiated_bootstrap) {
    bootstrap->allow_legacy_server_initiated_bootstrap =
            allow_legacy_server_initiated_bootstrap;
    _anjay_conn_session_token_reset(&bootstrap->bootstrap_session_token);
    reset_client_initiated_bootstrap_backoff(bootstrap);
}

void _anjay_bootstrap_cleanup(anjay_t *anjay) {
    assert(!avs_coap_exchange_id_valid(
            anjay->bootstrap.outgoing_request_exchange_id));
    cancel_client_initiated_bootstrap(anjay);
    cancel_est_sren(anjay);
    reset_client_initiated_bootstrap_backoff(&anjay->bootstrap);
    abort_bootstrap(anjay);
    avs_sched_del(&anjay->bootstrap.purge_bootstrap_handle);
    avs_sched_del(&anjay->bootstrap.finish_timeout_handle);
    _anjay_notify_clear_queue(&anjay->bootstrap.notification_queue);
}

#    ifdef ANJAY_TEST
#        include "tests/core/bootstrap.c"
#    endif // ANJAY_TEST

#else // ANJAY_WITH_BOOTSTRAP

int anjay_schedule_bootstrap_request(anjay_t *anjay) {
    (void) anjay;
    anjay_log(ERROR,
              _("Anjay is compiled without Bootstrap support, cannot "
                "schedule Bootstrap Request"));
    return -1;
}

#endif // ANJAY_WITH_BOOTSTRAP
