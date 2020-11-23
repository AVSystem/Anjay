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

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <anjay/core.h>
#include <avsystem/commons/avs_stream.h>
#include <avsystem/commons/avs_stream_membuf.h>
#include <avsystem/commons/avs_stream_v_table.h>
#include <avsystem/commons/avs_utils.h>

#include <anjay_modules/anjay_notify.h>

#include <avsystem/coap/code.h>

#include "coap/anjay_content_format.h"

#include "anjay_access_utils_private.h"
#include "anjay_core.h"
#include "anjay_dm_core.h"
#include "anjay_io_core.h"
#include "anjay_utils_private.h"
#include "dm/anjay_discover.h"
#include "dm/anjay_dm_create.h"
#include "dm/anjay_dm_execute.h"
#include "dm/anjay_dm_read.h"
#include "dm/anjay_dm_write.h"
#include "dm/anjay_dm_write_attrs.h"
#include "dm/anjay_query.h"
#include "io/anjay_vtable.h"
#include "observe/anjay_observe_core.h"

VISIBILITY_SOURCE_BEGIN

static int validate_version(const anjay_dm_object_def_t *obj_def) {
    if (!obj_def->version) {
        // missing version is equivalent to 1.0
        return 0;
    }

    unsigned major, minor;
    char dummy;
    if (sscanf(obj_def->version, "%u.%u%c", &major, &minor, &dummy) != 2) {
        dm_log(ERROR,
               _("invalid Object ") "/%u" _(
                       " version format (expected X.Y, where X and Y are "
                       "unsigned integers): ") "%s",
               (unsigned) obj_def->oid, obj_def->version);
        return -1;
    }

    return 0;
}

int anjay_register_object(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *def_ptr) {
    if (!def_ptr || !*def_ptr) {
        dm_log(ERROR, _("invalid object pointer"));
        return -1;
    }

    if ((*def_ptr)->oid == ANJAY_ID_INVALID) {
        anjay_log(ERROR,
                  _("Object ID ") "%u" _(
                          " is forbidden by the LwM2M 1.1 specification"),
                  ANJAY_ID_INVALID);
        return -1;
    }

    if (validate_version(*def_ptr)) {
        return -1;
    }

    AVS_LIST(const anjay_dm_object_def_t *const *) *obj_iter;

    AVS_LIST_FOREACH_PTR(obj_iter, &anjay->dm.objects) {
        assert(*obj_iter && **obj_iter);

        if ((***obj_iter)->oid >= (*def_ptr)->oid) {
            break;
        }
    }

    if (*obj_iter && (***obj_iter)->oid == (*def_ptr)->oid) {
        dm_log(ERROR, _("data model object ") "/%u" _(" already registered"),
               (*def_ptr)->oid);
        return -1;
    }

    AVS_LIST(const anjay_dm_object_def_t *const *) new_elem =
            AVS_LIST_NEW_ELEMENT(const anjay_dm_object_def_t *const *);
    if (!new_elem) {
        dm_log(ERROR, _("out of memory"));
        return -1;
    }

    *new_elem = def_ptr;
    AVS_LIST_INSERT(obj_iter, new_elem);

    dm_log(INFO, _("successfully registered object ") "/%u", (**new_elem)->oid);
    if (anjay_notify_instances_changed(anjay, (**new_elem)->oid)) {
        dm_log(WARNING, _("anjay_notify_instances_changed() failed on ") "/%u",
               (**new_elem)->oid);
    }
    if (anjay_schedule_registration_update(anjay, ANJAY_SSID_ANY)) {
        dm_log(WARNING, _("anjay_schedule_registration_update() failed"));
    }
    return 0;
}

static void remove_oid_from_notify_queue(anjay_notify_queue_t *out_queue,
                                         anjay_oid_t oid) {
    AVS_LIST(anjay_notify_queue_object_entry_t) *it;
    AVS_LIST_FOREACH_PTR(it, out_queue) {
        if ((*it)->oid >= oid) {
            break;
        }
    }
    if (*it && (*it)->oid == oid) {
        AVS_LIST(anjay_notify_queue_object_entry_t) entry = AVS_LIST_DETACH(it);
        _anjay_notify_clear_queue(&entry);
    }
}

int anjay_unregister_object(anjay_t *anjay,
                            const anjay_dm_object_def_t *const *def_ptr) {
    assert(!anjay->current_connection.server);

    if (!def_ptr || !*def_ptr) {
        dm_log(ERROR, _("invalid object pointer"));
        return -1;
    }

    AVS_LIST(const anjay_dm_object_def_t *const *) *obj_iter;
    AVS_LIST_FOREACH_PTR(obj_iter, &anjay->dm.objects) {
        assert(*obj_iter && **obj_iter);
        if ((***obj_iter)->oid >= (*def_ptr)->oid) {
            break;
        }
    }

    if (!*obj_iter || (***obj_iter)->oid != (*def_ptr)->oid) {
        dm_log(ERROR, _("object ") "%" PRIu16 _(" is not currently registered"),
               (*def_ptr)->oid);
        return -1;
    }
    if (**obj_iter != def_ptr) {
        dm_log(ERROR,
               _("object ") "%" PRIu16 _(" that is registered is not the same "
                                         "as the object passed for unregister"),
               (*def_ptr)->oid);
        return -1;
    }

    AVS_LIST(const anjay_dm_object_def_t *const *) detached =
            AVS_LIST_DETACH(obj_iter);

    AVS_LIST_FOREACH_PTR(obj_iter,
                         &anjay->transaction_state.objs_in_transaction) {
        if (**obj_iter >= def_ptr) {
            if (**obj_iter == def_ptr) {
                assert(anjay->transaction_state.depth);
                if (_anjay_dm_call_transaction_rollback(anjay, **obj_iter,
                                                        NULL)) {
                    dm_log(ERROR,
                           _("cannot rollback transaction on ") "/%u" _(
                                   ", object may be left in undefined state"),
                           (*def_ptr)->oid);
                }
                AVS_LIST_DELETE(obj_iter);
            }
            break;
        }
    }

    anjay_notify_queue_t notify = NULL;
    if (_anjay_notify_queue_instance_set_unknown_change(&notify,
                                                        (*def_ptr)->oid)
            || _anjay_notify_flush(anjay, &notify)) {
        dm_log(WARNING,
               _("could not perform notifications about removed object ") "%" PRIu16,
               (*def_ptr)->oid);
    }

    remove_oid_from_notify_queue(&anjay->scheduled_notify.queue,
                                 (*def_ptr)->oid);
#ifdef ANJAY_WITH_BOOTSTRAP
    remove_oid_from_notify_queue(&anjay->bootstrap.notification_queue,
                                 (*def_ptr)->oid);
#endif // ANJAY_WITH_BOOTSTRAP
    dm_log(INFO, _("successfully unregistered object ") "/%u", (*def_ptr)->oid);
    AVS_LIST_DELETE(&detached);
    if (anjay_schedule_registration_update(anjay, ANJAY_SSID_ANY)) {
        dm_log(WARNING, _("anjay_schedule_registration_update() failed"));
    }
    return 0;
}

void _anjay_dm_cleanup(anjay_t *anjay) {
    AVS_LIST_CLEAR(&anjay->dm.modules) {
        if (anjay->dm.modules->def->deleter) {
            anjay->dm.modules->def->deleter(anjay->dm.modules->arg);
        }
    }

    AVS_LIST_CLEAR(&anjay->dm.objects);
}

const anjay_dm_object_def_t *const *
_anjay_dm_find_object_by_oid(anjay_t *anjay, anjay_oid_t oid) {
    AVS_LIST(const anjay_dm_object_def_t *const *) obj;
    AVS_LIST_FOREACH(obj, anjay->dm.objects) {
        assert(*obj && **obj);
        if ((**obj)->oid == oid) {
            return *obj;
        }
    }

    return NULL;
}

uint8_t _anjay_dm_make_success_response_code(anjay_request_action_t action) {
    switch (action) {
    case ANJAY_ACTION_READ:
    case ANJAY_ACTION_DISCOVER:
        return AVS_COAP_CODE_CONTENT;
    case ANJAY_ACTION_WRITE:
    case ANJAY_ACTION_WRITE_UPDATE:
    case ANJAY_ACTION_WRITE_ATTRIBUTES:
    case ANJAY_ACTION_EXECUTE:
        return AVS_COAP_CODE_CHANGED;
    case ANJAY_ACTION_CREATE:
        return AVS_COAP_CODE_CREATED;
    case ANJAY_ACTION_DELETE:
        return AVS_COAP_CODE_DELETED;
    default:
        break;
    }
    return (uint8_t) (-ANJAY_ERR_INTERNAL);
}

static int prepare_input_context(avs_stream_t *stream,
                                 const anjay_request_t *request,
                                 anjay_input_ctx_t **out_in_ctx) {
    *out_in_ctx = NULL;

    int result = _anjay_input_dynamic_construct(out_in_ctx, stream, request);
    if (result) {
        dm_log(ERROR, _("could not create input context"));
    }

    return result;
}

const char *_anjay_debug_make_path__(char *buffer,
                                     size_t buffer_size,
                                     const anjay_uri_path_t *uri) {
    assert(uri);
    int result = 0;
    char *ptr = buffer;
    char *buffer_end = buffer + buffer_size;
    size_t length = _anjay_uri_path_length(uri);
    if (!length) {
        result = avs_simple_snprintf(buffer, buffer_size, "/");
    } else {
        for (size_t i = 0; result >= 0 && i < length; ++i) {
            result = avs_simple_snprintf(ptr, (size_t) (buffer_end - ptr),
                                         "/%u", (unsigned) uri->ids[i]);
            ptr += result;
        }
    }
    if (result < 0) {
        AVS_UNREACHABLE("should never happen");
        return "<error>";
    }
    return buffer;
}

int _anjay_dm_verify_instance_present(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid) {
    return _anjay_dm_map_present_result(
            _anjay_dm_instance_present(anjay, obj_ptr, iid));
}

int _anjay_dm_verify_resource_present(anjay_t *anjay,
                                      const anjay_dm_object_def_t *const *obj,
                                      anjay_iid_t iid,
                                      anjay_rid_t rid,
                                      anjay_dm_resource_kind_t *out_kind) {
    anjay_dm_resource_presence_t presence;
    int retval = _anjay_dm_resource_kind_and_presence(anjay, obj, iid, rid,
                                                      out_kind, &presence);
    if (retval) {
        return retval;
    }
    if (presence == ANJAY_DM_RES_ABSENT) {
        return ANJAY_ERR_NOT_FOUND;
    }
    return 0;
}

typedef struct {
    anjay_riid_t riid_to_find;
    bool found;
} resource_instance_present_args_t;

static int
dm_resource_instance_present_clb(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj,
                                 anjay_iid_t iid,
                                 anjay_rid_t rid,
                                 anjay_riid_t riid,
                                 void *args_) {
    (void) anjay;
    (void) obj;
    (void) iid;
    (void) rid;
    resource_instance_present_args_t *args =
            (resource_instance_present_args_t *) args_;
    if (riid == args->riid_to_find) {
        args->found = true;
        return ANJAY_FOREACH_BREAK;
    }
    return ANJAY_FOREACH_CONTINUE;
}

static int dm_resource_instance_present(anjay_t *anjay,
                                        const anjay_dm_object_def_t *const *obj,
                                        anjay_iid_t iid,
                                        anjay_rid_t rid,
                                        anjay_riid_t riid) {
    resource_instance_present_args_t args = {
        .riid_to_find = riid,
        .found = false
    };
    int result = _anjay_dm_foreach_resource_instance(
            anjay, obj, iid, rid, dm_resource_instance_present_clb, &args);
    if (result < 0) {
        return result;
    }
    return args.found ? 1 : 0;
}

int _anjay_dm_verify_resource_instance_present(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_riid_t riid) {
    return _anjay_dm_map_present_result(
            dm_resource_instance_present(anjay, obj, iid, rid, riid));
}

#ifdef ANJAY_WITH_DISCOVER
static int dm_discover(anjay_t *anjay,
                       const anjay_dm_object_def_t *const *obj,
                       const anjay_request_t *request) {
    dm_log(LAZY_DEBUG, _("Discover ") "%s",
           ANJAY_DEBUG_MAKE_PATH(&request->uri));
    if (_anjay_uri_path_has(&request->uri, ANJAY_ID_RIID)) {
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
    avs_stream_t *response_stream = _anjay_coap_setup_response_stream(
            request->ctx,
            &(anjay_msg_details_t) {
                .msg_code = _anjay_dm_make_success_response_code(
                        ANJAY_ACTION_DISCOVER),
                .format = AVS_COAP_FORMAT_LINK_FORMAT
            });

    if (!response_stream) {
        dm_log(ERROR, _("could not setup message"));
        return -1;
    }

    int result = _anjay_discover(anjay, response_stream, obj,
                                 request->uri.ids[ANJAY_ID_IID],
                                 request->uri.ids[ANJAY_ID_RID]);
    if (result) {
        dm_log(WARNING, _("Discover ") "%s" _(" failed!"),
               ANJAY_DEBUG_MAKE_PATH(&request->uri));
    }
    return result;
}
#else // ANJAY_WITH_DISCOVER
#    define dm_discover(anjay, obj, details)               \
        (dm_log(ERROR, _("Not supported: Discover ") "%s", \
                ANJAY_DEBUG_MAKE_PATH(&details->uri)),     \
         ANJAY_ERR_NOT_IMPLEMENTED)
#endif // ANJAY_WITH_DISCOVER

static int dm_execute(anjay_t *anjay,
                      const anjay_dm_object_def_t *const *obj,
                      const anjay_request_t *request) {
    // Treat not specified format as implicit Plain Text
    if (request->content_format != AVS_COAP_FORMAT_PLAINTEXT
            && request->content_format != AVS_COAP_FORMAT_NONE) {
        return AVS_COAP_CODE_UNSUPPORTED_CONTENT_FORMAT;
    }
    dm_log(LAZY_DEBUG, _("Execute ") "%s",
           ANJAY_DEBUG_MAKE_PATH(&request->uri));
    if (!_anjay_uri_path_leaf_is(&request->uri, ANJAY_ID_RID)) {
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }

    int retval =
            _anjay_dm_verify_instance_present(anjay, obj,
                                              request->uri.ids[ANJAY_ID_IID]);
    if (!retval) {
        if (!_anjay_instance_action_allowed(
                    anjay, &REQUEST_TO_ACTION_INFO(anjay, request))) {
            return ANJAY_ERR_UNAUTHORIZED;
        }
        anjay_dm_resource_kind_t kind;
        if (!(retval = _anjay_dm_verify_resource_present(
                      anjay, obj, request->uri.ids[ANJAY_ID_IID],
                      request->uri.ids[ANJAY_ID_RID], &kind))
                && !_anjay_dm_res_kind_executable(kind)) {
            dm_log(LAZY_DEBUG, "%s" _(" is not executable"),
                   ANJAY_DEBUG_MAKE_PATH(&request->uri));
            return ANJAY_ERR_METHOD_NOT_ALLOWED;
        }
    }
    if (!retval) {
        anjay_execute_ctx_t *execute_ctx =
                _anjay_execute_ctx_create(request->payload_stream);
        retval = _anjay_dm_call_resource_execute(anjay, obj,
                                                 request->uri.ids[ANJAY_ID_IID],
                                                 request->uri.ids[ANJAY_ID_RID],
                                                 execute_ctx, NULL);
        _anjay_execute_ctx_destroy(&execute_ctx);
    }
    return retval;
}

static int dm_delete(anjay_t *anjay,
                     const anjay_dm_object_def_t *const *obj,
                     const anjay_request_t *request) {
    dm_log(LAZY_DEBUG, _("Delete ") "%s", ANJAY_DEBUG_MAKE_PATH(&request->uri));
    if (!_anjay_uri_path_leaf_is(&request->uri, ANJAY_ID_IID)) {
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }

    int retval =
            _anjay_dm_verify_instance_present(anjay, obj,
                                              request->uri.ids[ANJAY_ID_IID]);
    if (!retval) {
        if (!_anjay_instance_action_allowed(
                    anjay, &REQUEST_TO_ACTION_INFO(anjay, request))) {
            return ANJAY_ERR_UNAUTHORIZED;
        }

        retval = _anjay_dm_call_instance_remove(
                anjay, obj, request->uri.ids[ANJAY_ID_IID], NULL);
    }
    if (!retval) {
        anjay_notify_queue_t notify_queue = NULL;
        (void) ((retval = _anjay_notify_queue_instance_removed(
                         &notify_queue, request->uri.ids[ANJAY_ID_OID],
                         request->uri.ids[ANJAY_ID_IID]))
                || (retval = _anjay_notify_flush(anjay, &notify_queue)));
    }
    return retval;
}

static int invoke_transactional_action(anjay_t *anjay,
                                       const anjay_dm_object_def_t *const *obj,
                                       const anjay_request_t *request,
                                       anjay_input_ctx_t *in_ctx) {
    _anjay_dm_transaction_begin(anjay);
    int retval = 0;
    switch (request->action) {
    case ANJAY_ACTION_WRITE:
    case ANJAY_ACTION_WRITE_UPDATE:
        assert(in_ctx);
        retval = _anjay_dm_write(anjay, obj, request, in_ctx);
        break;
    case ANJAY_ACTION_CREATE:
        assert(in_ctx);
        retval = _anjay_dm_create(anjay, obj, request, in_ctx);
        break;
    case ANJAY_ACTION_DELETE:
        retval = dm_delete(anjay, obj, request);
        break;
    default:
        dm_log(ERROR, _("invalid transactional action"));
        retval = ANJAY_ERR_METHOD_NOT_ALLOWED;
    }

    return _anjay_dm_transaction_finish(anjay, retval);
}

static int invoke_action(anjay_t *anjay,
                         const anjay_dm_object_def_t *const *obj,
                         const anjay_request_t *request,
                         anjay_input_ctx_t *in_ctx) {
    switch (request->action) {
    case ANJAY_ACTION_READ:
        return _anjay_dm_read_or_observe(anjay, obj, request);
    case ANJAY_ACTION_DISCOVER:
        return dm_discover(anjay, obj, request);
    case ANJAY_ACTION_WRITE:
    case ANJAY_ACTION_WRITE_UPDATE:
    case ANJAY_ACTION_CREATE:
    case ANJAY_ACTION_DELETE:
        return invoke_transactional_action(anjay, obj, request, in_ctx);
    case ANJAY_ACTION_WRITE_ATTRIBUTES:
        return _anjay_dm_write_attributes(anjay, obj, request);
    case ANJAY_ACTION_EXECUTE:
        AVS_ASSERT(!in_ctx, "in_ctx should be NULL for Execute");
        return dm_execute(anjay, obj, request);
    default:
        dm_log(ERROR, _("Invalid action for Management Interface"));
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

int _anjay_dm_perform_action(anjay_t *anjay, const anjay_request_t *request) {
    const anjay_dm_object_def_t *const *obj = NULL;
    if (_anjay_uri_path_has(&request->uri, ANJAY_ID_OID)) {
        if (!(obj = _anjay_dm_find_object_by_oid(
                      anjay, request->uri.ids[ANJAY_ID_OID]))
                || !*obj) {
            dm_log(DEBUG, _("Object not found: ") "%u",
                   request->uri.ids[ANJAY_ID_OID]);
            return ANJAY_ERR_NOT_FOUND;
        }
    } else {
        dm_log(DEBUG, _("at least Object ID must be present in Uri-Path"));
        return ANJAY_ERR_BAD_REQUEST;
    }

    /**
     * NOTE: Some operations do not require payload in response, and a simple
     * empty response initialized just below will be sufficient. Other
     * operations may setup response once again themselves if necessary.
     */
    anjay_msg_details_t msg_details = {
        .msg_code = _anjay_dm_make_success_response_code(request->action),
        .format = AVS_COAP_FORMAT_NONE
    };
    if (!_anjay_coap_setup_response_stream(request->ctx, &msg_details)) {
        return ANJAY_ERR_INTERNAL;
    }

    if (_anjay_uri_path_has(&request->uri, ANJAY_ID_OID)
            && (request->uri.ids[ANJAY_ID_OID] == ANJAY_DM_OID_SECURITY)) {
        /**
         * According to the LwM2M 1.1 specification:
         * > The LwM2M Client MUST reject with an "4.01 Unauthorized" response
         * > code any LwM2M Server operation on the Security Object (ID: 0).
         * >
         * > The LwM2M Client MUST reject with an "4.01 Unauthorized" response
         * > code any LwM2M Server operation on an OSCORE Object (ID: 21).
         *
         * Note that other, per-instance security checks are performed via
         * _anjay_instance_action_allowed().
         */
        return ANJAY_ERR_UNAUTHORIZED;
    }

    anjay_input_ctx_t *in_ctx = NULL;
    int result =
            prepare_input_context(request->payload_stream, request, &in_ctx);
    if (result) {
        return result;
    }

    result = invoke_action(anjay, obj, request, in_ctx);

    int destroy_result = _anjay_input_ctx_destroy(&in_ctx);
    return result ? result : destroy_result;
}

int _anjay_dm_foreach_object(anjay_t *anjay,
                             anjay_dm_foreach_object_handler_t *handler,
                             void *data) {
    AVS_LIST(const anjay_dm_object_def_t *const *) obj;
    AVS_LIST_FOREACH(obj, anjay->dm.objects) {
        assert(*obj && **obj);

        int result = handler(anjay, *obj, data);
        if (result == ANJAY_FOREACH_BREAK) {
            dm_log(TRACE, _("foreach_object: break on ") "/%u", (**obj)->oid);
            return 0;
        } else if (result) {
            dm_log(DEBUG,
                   _("foreach_object_handler failed for ") "/%u" _(" (") "%d" _(
                           ")"),
                   (**obj)->oid, result);
            return result;
        }
    }

    return 0;
}

typedef struct {
    const anjay_dm_list_ctx_vtable_t *vtable;
    anjay_t *anjay;
    const anjay_dm_object_def_t *const *obj;
    int32_t last_iid;
    anjay_dm_foreach_instance_handler_t *handler;
    void *handler_data;
    int result;
} anjay_dm_foreach_instance_ctx_t;

static void foreach_instance_emit(anjay_dm_list_ctx_t *ctx_, uint16_t iid) {
    anjay_dm_foreach_instance_ctx_t *ctx =
            (anjay_dm_foreach_instance_ctx_t *) ctx_;
    if (!ctx->result) {
        if (iid == ANJAY_ID_INVALID) {
            dm_log(ERROR, "%" PRIu16 _(" is not a valid Instance ID"), iid);
            ctx->result = ANJAY_ERR_INTERNAL;
            return;
        }
        if (iid <= ctx->last_iid) {
            dm_log(ERROR,
                   _("list_instances MUST return Instance IDs in strictly "
                     "ascending order; ") "%" PRIu16
                           _(" returned after ") "%" PRId32,
                   iid, ctx->last_iid);
            ctx->result = ANJAY_ERR_INTERNAL;
            return;
        }
        ctx->last_iid = iid;
        ctx->result =
                ctx->handler(ctx->anjay, ctx->obj, iid, ctx->handler_data);
        if (ctx->result == ANJAY_FOREACH_BREAK) {
            dm_log(TRACE, _("foreach_instance: break on ") "/%u/%u",
                   (*ctx->obj)->oid, iid);
        } else if (ctx->result) {
            dm_log(DEBUG,
                   _("foreach_instance_handler failed for ") "/%u/%u" _(
                           " (") "%d" _(")"),
                   (*ctx->obj)->oid, iid, ctx->result);
        }
    }
}

int _anjay_dm_foreach_instance(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj,
                               anjay_dm_foreach_instance_handler_t *handler,
                               void *data) {
    if (!obj) {
        dm_log(ERROR, _("attempt to iterate through NULL Object"));
        return -1;
    }

    static const anjay_dm_list_ctx_vtable_t VTABLE = {
        .emit = foreach_instance_emit
    };
    anjay_dm_foreach_instance_ctx_t ctx = {
        .vtable = &VTABLE,
        .anjay = anjay,
        .obj = obj,
        .last_iid = -1,
        .handler = handler,
        .handler_data = data,
        .result = 0
    };
    int result =
            _anjay_dm_call_list_instances(anjay, obj,
                                          (anjay_dm_list_ctx_t *) &ctx, NULL);
    if (result < 0) {
        dm_log(WARNING,
               _("list_instances handler for ") "/%u" _(" failed (") "%d" _(
                       ")"),
               (*obj)->oid, result);
        return result;
    }
    return ctx.result == ANJAY_FOREACH_BREAK ? 0 : ctx.result;
}

typedef struct {
    anjay_iid_t iid_to_find;
    bool found;
} instance_present_args_t;

static int query_dm_instance(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj,
                             anjay_iid_t iid,
                             void *instance_insert_ptr_) {
    (void) anjay;
    (void) obj;
    AVS_LIST(anjay_iid_t) **instance_insert_ptr =
            (AVS_LIST(anjay_iid_t) **) instance_insert_ptr_;

    AVS_LIST(anjay_iid_t) new_instance = AVS_LIST_NEW_ELEMENT(anjay_iid_t);
    if (!new_instance) {
        dm_log(ERROR, _("out of memory"));
        return -1;
    }
    AVS_LIST_INSERT(*instance_insert_ptr, new_instance);
    AVS_LIST_ADVANCE_PTR(instance_insert_ptr);

    *new_instance = iid;
    return 0;
}

int _anjay_dm_get_sorted_instance_list(anjay_t *anjay,
                                       const anjay_dm_object_def_t *const *obj,
                                       AVS_LIST(anjay_iid_t) *out) {
    assert(!*out);
    AVS_LIST(anjay_iid_t) *instance_insert_ptr = out;
    int retval = _anjay_dm_foreach_instance(anjay, obj, query_dm_instance,
                                            &instance_insert_ptr);
    if (retval) {
        AVS_LIST_CLEAR(out);
    }
    return retval;
}

static int instance_present_clb(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj,
                                anjay_iid_t iid,
                                void *args_) {
    (void) anjay;
    (void) obj;
    instance_present_args_t *args = (instance_present_args_t *) args_;
    if (iid >= args->iid_to_find) {
        args->found = (iid == args->iid_to_find);
        return ANJAY_FOREACH_BREAK;
    }
    return ANJAY_FOREACH_CONTINUE;
}

int _anjay_dm_instance_present(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid) {
    instance_present_args_t args = {
        .iid_to_find = iid,
        .found = false
    };
    int retval = _anjay_dm_foreach_instance(anjay, obj_ptr,
                                            instance_present_clb, &args);
    if (retval < 0) {
        return retval;
    }
    return args.found ? 1 : 0;
}

struct anjay_dm_resource_list_ctx_struct {
    anjay_t *anjay;
    const anjay_dm_object_def_t *const *obj;
    anjay_iid_t iid;
    int32_t last_rid;
    anjay_dm_foreach_resource_handler_t *handler;
    void *handler_data;
    int result;
};

static bool presence_valid(anjay_dm_resource_presence_t presence) {
    return presence == ANJAY_DM_RES_ABSENT || presence == ANJAY_DM_RES_PRESENT;
}

void anjay_dm_emit_res(anjay_dm_resource_list_ctx_t *ctx,
                       anjay_rid_t rid,
                       anjay_dm_resource_kind_t kind,
                       anjay_dm_resource_presence_t presence) {
    if (!ctx->result) {
        if (rid == ANJAY_ID_INVALID) {
            dm_log(ERROR, "%" PRIu16 _(" is not a valid Resource ID"), rid);
            ctx->result = ANJAY_ERR_INTERNAL;
            return;
        }
        if (rid <= ctx->last_rid) {
            dm_log(ERROR,
                   _("list_resources MUST return Resource IDs in strictly "
                     "ascending order; ") "%" PRIu16
                           _(" returned after ") "%" PRId32,
                   rid, ctx->last_rid);
            ctx->result = ANJAY_ERR_INTERNAL;
            return;
        }
        ctx->last_rid = rid;
        if (!_anjay_dm_res_kind_valid(kind)) {
            dm_log(ERROR, "%d" _(" is not valid anjay_dm_resource_kind_t"),
                   (int) kind);
            ctx->result = ANJAY_ERR_INTERNAL;
            return;
        }
        if (!presence_valid(presence)) {
            dm_log(ERROR, "%d" _(" is not valid anjay_dm_resource_presence_t"),
                   (int) presence);
            ctx->result = ANJAY_ERR_INTERNAL;
            return;
        }
        ctx->result = ctx->handler(ctx->anjay, ctx->obj, ctx->iid, rid, kind,
                                   presence, ctx->handler_data);
        if (ctx->result == ANJAY_FOREACH_BREAK) {
            dm_log(TRACE, _("foreach_resource: break on ") "/%u/%u/%u",
                   (*ctx->obj)->oid, ctx->iid, rid);
        } else if (ctx->result) {
            dm_log(DEBUG,
                   _("foreach_resource_handler failed for ") "/%u/%u/%u" _(
                           " (") "%d" _(")"),
                   (*ctx->obj)->oid, ctx->iid, rid, ctx->result);
        }
    }
}

int _anjay_dm_foreach_resource(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj,
                               anjay_iid_t iid,
                               anjay_dm_foreach_resource_handler_t *handler,
                               void *data) {
    if (!obj) {
        dm_log(ERROR, _("attempt to iterate through NULL Object"));
        return -1;
    }

    anjay_dm_resource_list_ctx_t ctx = {
        .anjay = anjay,
        .obj = obj,
        .iid = iid,
        .last_rid = -1,
        .handler = handler,
        .handler_data = data,
        .result = 0
    };
    int result = _anjay_dm_call_list_resources(anjay, obj, iid, &ctx, NULL);
    if (result < 0) {
        dm_log(ERROR,
               _("list_resources handler for ") "/%u/%u" _(" failed (") "%d" _(
                       ")"),
               (*obj)->oid, iid, result);
        return result;
    }
    return ctx.result == ANJAY_FOREACH_BREAK ? 0 : ctx.result;
}

typedef struct {
    anjay_rid_t rid_to_find;
    anjay_dm_resource_kind_t kind;
    anjay_dm_resource_presence_t presence;
} resource_present_args_t;

static int kind_and_presence_clb(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj,
                                 anjay_iid_t iid,
                                 anjay_rid_t rid,
                                 anjay_dm_resource_kind_t kind,
                                 anjay_dm_resource_presence_t presence,
                                 void *args_) {
    (void) anjay;
    (void) obj;
    (void) iid;
    resource_present_args_t *args = (resource_present_args_t *) args_;
    if (rid >= args->rid_to_find) {
        if (rid == args->rid_to_find) {
            args->kind = kind;
            args->presence = presence;
        }
        return ANJAY_FOREACH_BREAK;
    }
    return ANJAY_FOREACH_CONTINUE;
}

int _anjay_dm_resource_kind_and_presence(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_dm_resource_kind_t *out_kind,
        anjay_dm_resource_presence_t *out_presence) {
    resource_present_args_t args = {
        .rid_to_find = rid,
        .kind = (anjay_dm_resource_kind_t) -1,
        .presence = ANJAY_DM_RES_ABSENT
    };
    assert(!_anjay_dm_res_kind_valid(args.kind));
    int retval = _anjay_dm_foreach_resource(anjay, obj_ptr, iid,
                                            kind_and_presence_clb, &args);
    if (retval) {
        return retval;
    }
    if (!_anjay_dm_res_kind_valid(args.kind)) {
        return ANJAY_ERR_NOT_FOUND;
    }
    if (out_kind) {
        *out_kind = args.kind;
    }
    if (out_presence) {
        *out_presence = args.presence;
    }
    return 0;
}

int _anjay_dm_path_info(anjay_t *anjay,
                        const anjay_dm_object_def_t *const *obj,
                        const anjay_uri_path_t *path,
                        anjay_dm_path_info_t *out_info) {
    memset(out_info, 0, sizeof(*out_info));
    out_info->uri = *path;
    out_info->is_present = true;
    out_info->is_hierarchical = true;

    if (_anjay_uri_path_length(path) == 0) {
        return 0;
    }
    if (!obj) {
        out_info->is_present = false;
        return 0;
    }

    int result = 0;
    if (_anjay_uri_path_has(path, ANJAY_ID_IID)) {
        result = _anjay_dm_verify_instance_present(anjay, obj,
                                                   path->ids[ANJAY_ID_IID]);
    }
    if (!result && _anjay_uri_path_has(path, ANJAY_ID_RID)) {
        anjay_dm_resource_kind_t kind;
        anjay_dm_resource_presence_t presence;
        result = _anjay_dm_resource_kind_and_presence(anjay, obj,
                                                      path->ids[ANJAY_ID_IID],
                                                      path->ids[ANJAY_ID_RID],
                                                      &kind, &presence);
        if (!result) {
            out_info->is_present = (presence == ANJAY_DM_RES_PRESENT);
            if (out_info->is_present) {
                out_info->has_resource = true;
                out_info->kind = kind;
                out_info->is_hierarchical = _anjay_dm_res_kind_multiple(kind);
            }
        }
    }
    if (!result && _anjay_uri_path_has(path, ANJAY_ID_RIID)) {
        int is_present = (out_info->is_present && out_info->is_hierarchical);
        if (is_present) {
            is_present = dm_resource_instance_present(anjay, obj,
                                                      path->ids[ANJAY_ID_IID],
                                                      path->ids[ANJAY_ID_RID],
                                                      path->ids[ANJAY_ID_RIID]);
        }
        result = _anjay_dm_map_present_result(is_present);
        out_info->is_hierarchical = false;
    }
    if (result == ANJAY_ERR_NOT_FOUND) {
        out_info->is_present = false;
        return 0;
    } else {
        return result;
    }
}

typedef struct {
    const anjay_dm_list_ctx_vtable_t *vtable;
    anjay_t *anjay;
    const anjay_dm_object_def_t *const *obj;
    anjay_iid_t iid;
    anjay_rid_t rid;
    int32_t last_riid;
    anjay_dm_foreach_resource_instance_handler_t *handler;
    void *handler_data;
    int result;
} anjay_dm_foreach_resource_instance_ctx_t;

static void foreach_resource_instance_emit(anjay_dm_list_ctx_t *ctx_,
                                           uint16_t riid) {
    anjay_dm_foreach_resource_instance_ctx_t *ctx =
            (anjay_dm_foreach_resource_instance_ctx_t *) ctx_;
    if (!ctx->result) {
        if (riid == ANJAY_ID_INVALID) {
            dm_log(ERROR, "%" PRIu16 _(" is not a valid Resource Instance ID"),
                   riid);
            ctx->result = ANJAY_ERR_INTERNAL;
            return;
        }
        if (riid <= ctx->last_riid) {
            dm_log(ERROR,
                   _("list_resource_instances MUST return Resource Instance "
                     "IDs in strictly ascending order; ") "%" PRIu16
                           _(" returned after ") "%" PRId32,
                   riid, ctx->last_riid);
            ctx->result = ANJAY_ERR_INTERNAL;
            return;
        }
        ctx->last_riid = riid;
        ctx->result = ctx->handler(ctx->anjay, ctx->obj, ctx->iid, ctx->rid,
                                   riid, ctx->handler_data);
        if (ctx->result == ANJAY_FOREACH_BREAK) {
            dm_log(TRACE,
                   _("foreach_resource_instance: break on ") "/%u/%u/%u/%u",
                   (*ctx->obj)->oid, ctx->iid, ctx->rid, riid);
        } else if (ctx->result) {
            dm_log(DEBUG,
                   _("foreach_resource_handler failed for ") "/%u/%u/%u/%u" _(
                           " (") "%d" _(")"),
                   (*ctx->obj)->oid, ctx->iid, ctx->rid, riid, ctx->result);
        }
    }
}

int _anjay_dm_foreach_resource_instance(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_dm_foreach_resource_instance_handler_t *handler,
        void *data) {
    if (!obj) {
        dm_log(ERROR, _("attempt to iterate through NULL Object"));
        return -1;
    }

    static const anjay_dm_list_ctx_vtable_t VTABLE = {
        .emit = foreach_resource_instance_emit
    };
    anjay_dm_foreach_resource_instance_ctx_t ctx = {
        .vtable = &VTABLE,
        .anjay = anjay,
        .obj = obj,
        .iid = iid,
        .rid = rid,
        .last_riid = -1,
        .handler = handler,
        .handler_data = data,
        .result = 0
    };
    int result = _anjay_dm_call_list_resource_instances(
            anjay, obj, iid, rid, (anjay_dm_list_ctx_t *) &ctx, NULL);
    if (result < 0) {
        dm_log(ERROR,
               _("list_resource_instances handler for ") "/%u/%u/%u" _(
                       " failed (") "%d" _(")"),
               (*obj)->oid, iid, rid, result);
        return result;
    }
    return ctx.result == ANJAY_FOREACH_BREAK ? 0 : ctx.result;
}

anjay_ssid_t _anjay_dm_current_ssid(anjay_t *anjay) {
    return (anjay_ssid_t) (anjay->current_connection.server
                                   ? _anjay_server_ssid(
                                             anjay->current_connection.server)
                                   : ANJAY_SSID_BOOTSTRAP);
}

#ifdef ANJAY_TEST
#    include "tests/core/dm.c"
#endif // ANJAY_TEST
