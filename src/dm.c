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

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

#include <anjay/core.h>
#include <avsystem/commons/stream_v_table.h>
#include <avsystem/commons/stream.h>
#include <avsystem/commons/stream/stream_membuf.h>

#include <anjay_modules/notify.h>

#include "coap/msg.h"
#include "dm.h"
#include "dm/discover.h"
#include "dm/execute.h"
#include "dm/query.h"
#include "io.h"
#include "observe.h"
#include "utils.h"
#include "anjay.h"
#include "access_control.h"

VISIBILITY_SOURCE_BEGIN

#ifdef ANJAY_TEST
#include "test/mock_coap_stream.h"
#endif

static int validate_supported_rids(const anjay_dm_object_def_t *obj_def) {
    if (obj_def->supported_rids.count != 0 && !obj_def->supported_rids.rids) {
        anjay_log(ERROR, "/%u: supported_rids.count is nonzero, but "
                         "supported_rids.rids in is NULL", obj_def->oid);
        return -1;
    }

    for (size_t i = 1; i < obj_def->supported_rids.count; ++i) {
        if (obj_def->supported_rids.rids[i]
                <= obj_def->supported_rids.rids[i - 1]) {
            anjay_log(ERROR, "supported_rids in /%u is not strictly ascending",
                             obj_def->oid);
            return -1;
        }
    }

    return 0;
}

int anjay_register_object(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *def_ptr) {
    assert(!anjay->transaction_state.depth);
    assert(!anjay->transaction_state.objs_in_transaction);

    if (!def_ptr || !*def_ptr) {
        anjay_log(ERROR, "invalid object pointer");
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
        anjay_log(ERROR, "data model object /%u already registered",
                  (*def_ptr)->oid);
        return -1;
    }

    if (validate_supported_rids(*def_ptr)) {
        return -1;
    }

    AVS_LIST(const anjay_dm_object_def_t *const *) new_elem =
            AVS_LIST_NEW_ELEMENT(const anjay_dm_object_def_t *const *);
    if (!new_elem) {
        anjay_log(ERROR, "out of memory");
        return -1;
    }

    *new_elem = def_ptr;
    AVS_LIST_INSERT(obj_iter, new_elem);

    anjay_log(INFO, "successfully registered object /%u", (**new_elem)->oid);
    if (anjay_notify_instances_changed(anjay, (**new_elem)->oid)) {
        anjay_log(WARNING, "anjay_notify_instances_changed() failed on /%u",
                  (**new_elem)->oid);
    }
    if (anjay_schedule_registration_update(anjay, ANJAY_SSID_ANY)) {
        anjay_log(WARNING, "anjay_schedule_registration_update() failed");
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
    assert(!anjay->transaction_state.depth);
    assert(!anjay->transaction_state.objs_in_transaction);

    if (!def_ptr || !*def_ptr) {
        anjay_log(ERROR, "invalid object pointer");
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
        anjay_log(ERROR, "object %" PRIu16 " is not currently registered",
                  (*def_ptr)->oid);
        return -1;
    }
    if (**obj_iter != def_ptr) {
        anjay_log(ERROR, "object %" PRIu16 " that is registered is not "
                         "the same as the object passed for unregister",
                  (*def_ptr)->oid);
        return -1;
    }

    AVS_LIST(const anjay_dm_object_def_t *const *) detached =
            AVS_LIST_DETACH(obj_iter);

    anjay_notify_queue_t notify = NULL;
    if (_anjay_notify_queue_instance_set_unknown_change(&notify,
                                                        (*def_ptr)->oid)
            || _anjay_notify_flush(anjay, ANJAY_SSID_BOOTSTRAP, &notify)) {
        anjay_log(WARNING, "could not perform notifications about "
                           "removed object %" PRIu16, (*def_ptr)->oid);
    }

    remove_oid_from_notify_queue(&anjay->scheduled_notify.queue,
                                 (*def_ptr)->oid);
#ifdef WITH_BOOTSTRAP
    remove_oid_from_notify_queue(&anjay->bootstrap.notification_queue,
                                 (*def_ptr)->oid);
#endif // WITH_BOOTSTRAP
    anjay_log(INFO, "successfully unregistered object /%u", (*def_ptr)->oid);
    AVS_LIST_DELETE(&detached);
    if (anjay_schedule_registration_update(anjay, ANJAY_SSID_ANY)) {
        anjay_log(WARNING, "anjay_schedule_registration_update() failed");
    }
    return 0;
}

void _anjay_dm_cleanup(anjay_t *anjay) {
    AVS_LIST_CLEAR(&anjay->dm.modules) {
        if (anjay->dm.modules->def->deleter) {
            anjay->dm.modules->def->deleter(anjay, anjay->dm.modules->arg);
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
    anjay_log(TRACE, "could not found object: /%u not registered", oid);

    return NULL;
}

static anjay_input_ctx_constructor_t *
input_ctx_for_action(anjay_request_action_t action) {
    switch (action) {
    case ANJAY_ACTION_WRITE:
    case ANJAY_ACTION_WRITE_UPDATE:
    case ANJAY_ACTION_CREATE:
        return _anjay_input_dynamic_create;
    case ANJAY_ACTION_EXECUTE:
        return _anjay_input_text_create;
    default:
        return NULL;
    }
}

static uint8_t make_success_response_code(anjay_request_action_t action) {
    switch (action) {
    case ANJAY_ACTION_READ:             return ANJAY_COAP_CODE_CONTENT;
    case ANJAY_ACTION_DISCOVER:         return ANJAY_COAP_CODE_CONTENT;
    case ANJAY_ACTION_WRITE:            return ANJAY_COAP_CODE_CHANGED;
    case ANJAY_ACTION_WRITE_UPDATE:     return ANJAY_COAP_CODE_CHANGED;
    case ANJAY_ACTION_WRITE_ATTRIBUTES: return ANJAY_COAP_CODE_CHANGED;
    case ANJAY_ACTION_EXECUTE:          return ANJAY_COAP_CODE_CHANGED;
    case ANJAY_ACTION_CREATE:           return ANJAY_COAP_CODE_CREATED;
    case ANJAY_ACTION_DELETE:           return ANJAY_COAP_CODE_DELETED;
    default:                            break;
    }
    return (uint8_t)(-ANJAY_ERR_INTERNAL);
}

static int prepare_input_context(avs_stream_abstract_t *stream,
                                 anjay_request_action_t action,
                                 anjay_input_ctx_t **out_in_ctx) {
    *out_in_ctx = NULL;

    anjay_input_ctx_constructor_t *constructor = input_ctx_for_action(action);
    if (constructor) {
        int result = constructor(out_in_ctx, &stream, false);
        if (result) {
            anjay_log(ERROR, "could not create input context");
            return result;
        }
    }

    return 0;
}

const char *_anjay_debug_make_path__(char *buffer,
                                     size_t buffer_size,
                                     const anjay_uri_path_t *uri) {
    assert(uri);
    assert(uri->has_iid || !uri->has_rid);

    const uint16_t *ids[] = {
        uri->has_oid ? &uri->oid : NULL,
        uri->has_iid ? &uri->iid : NULL,
        uri->has_rid ? &uri->rid : NULL,
        NULL
    };
    const uint16_t **id_ptr = ids;

    size_t offset = 0;
    do {
        ssize_t result;
        if (*id_ptr) {
            result = _anjay_snprintf(buffer + offset, buffer_size - offset,
                                     "/%u", **id_ptr);
        } else {
            result = _anjay_snprintf(buffer, buffer_size, "%s", "/");
        }
        if (result < 0) {
            assert(0 && "should never happen");
            return "<error>";
        }

        offset += (size_t) result;
        ++id_ptr;
    } while (*id_ptr);

    return buffer;
}

static int ensure_instance_present(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_iid_t iid) {
    return _anjay_dm_map_present_result(
            _anjay_dm_instance_present(anjay, obj_ptr, iid, NULL));
}

static int
ensure_resource_supported_and_present(anjay_t *anjay,
                                      const anjay_dm_object_def_t *const *obj,
                                      anjay_iid_t iid,
                                      anjay_rid_t rid) {
    return _anjay_dm_map_present_result(
            _anjay_dm_resource_supported_and_present(anjay, obj, iid, rid,
                                                     NULL));
}

static int ensure_resource_present(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj,
                                   anjay_iid_t iid,
                                   anjay_rid_t rid) {
    return _anjay_dm_map_present_result(
            _anjay_dm_resource_present(anjay, obj, iid, rid, NULL));
}

static bool
has_resource_operation_bit(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *obj_ptr,
                           anjay_rid_t rid,
                           anjay_dm_resource_op_bit_t bit) {
    anjay_dm_resource_op_mask_t mask = ANJAY_DM_RESOURCE_OP_NONE;
    if (_anjay_dm_resource_operations(anjay, obj_ptr, rid, &mask, NULL)) {
        anjay_log(ERROR, "resource_operations /%u/*/%u failed", (*obj_ptr)->oid,
                  rid);
        return false;
    }
    return !!(mask & bit);
}

static int read_resource_internal(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj,
                                  anjay_iid_t iid,
                                  anjay_rid_t rid,
                                  anjay_output_ctx_t *out_ctx) {
    int result = _anjay_output_set_id(out_ctx, ANJAY_ID_RID, rid);
    if (!result) {
        result = _anjay_dm_resource_read(anjay, obj, iid, rid, out_ctx, NULL);
    }
    return result;
}

static int read_present_resource(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj,
                                 anjay_iid_t iid,
                                 anjay_rid_t rid,
                                 anjay_output_ctx_t *out_ctx) {
    if (!has_resource_operation_bit(anjay, obj, rid,
                                    ANJAY_DM_RESOURCE_OP_BIT_R)) {
        anjay_log(ERROR, "Read /%u/*/%u is not supported", (*obj)->oid, rid);
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
    return read_resource_internal(anjay, obj, iid, rid, out_ctx);
}

static int read_resource(anjay_t *anjay,
                         const anjay_dm_object_def_t *const *obj,
                         anjay_iid_t iid,
                         anjay_rid_t rid,
                         anjay_output_ctx_t *out_ctx) {
    int result = ensure_resource_supported_and_present(anjay, obj, iid, rid);
    if (result) {
        return result;
    }
    return read_present_resource(anjay, obj, iid, rid, out_ctx);
}

static int read_instance(anjay_t *anjay,
                         const anjay_dm_object_def_t *const *obj,
                         anjay_iid_t iid,
                         anjay_output_ctx_t *out_ctx) {
    for (size_t i = 0; i < (*obj)->supported_rids.count; ++i) {
        int result = ensure_resource_present(anjay, obj, iid,
                                             (*obj)->supported_rids.rids[i]);
        if (!result) {
            result = read_present_resource(anjay, obj, iid,
                                           (*obj)->supported_rids.rids[i],
                                           out_ctx);
        }
        if (result
                && result != ANJAY_ERR_METHOD_NOT_ALLOWED
                && result != ANJAY_ERR_NOT_FOUND) {
            return result;
        }
    }
    return 0;
}

static int read_instance_wrapped(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj,
                                 anjay_iid_t iid,
                                 anjay_output_ctx_t *out_ctx) {
    int result = _anjay_output_set_id(out_ctx, ANJAY_ID_IID, iid);
    if (result) {
        return result;
    }
    anjay_output_ctx_t *instance_ctx = _anjay_output_object_start(out_ctx);
    if (!instance_ctx) {
        return ANJAY_ERR_INTERNAL;
    }
    result = read_instance(anjay, obj, iid, instance_ctx);
    int finish_result = _anjay_output_object_finish(instance_ctx);
    return result ? result : finish_result;
}

static int read_object(anjay_t *anjay,
                       const anjay_dm_object_def_t *const *obj,
                       const anjay_dm_read_args_t *details,
                       anjay_output_ctx_t *out_ctx) {
    assert(details->uri.has_oid);
    int result = 0;
    anjay_iid_t iid;
    void *cookie = NULL;

    anjay_action_info_t info = {
        .oid = details->uri.oid,
        .ssid = details->ssid,
        .action = ANJAY_ACTION_READ
    };

    while (!result
            && !(result = _anjay_dm_instance_it(anjay, obj, &iid, &cookie,
                                                NULL))
            && iid != ANJAY_IID_INVALID) {
        info.iid = iid;
        if (!_anjay_access_control_action_allowed(anjay, &info)) {
            continue;
        }
        result = read_instance_wrapped(anjay, obj, iid, out_ctx);
    }
    return result;
}

static anjay_output_ctx_t *
dm_read_spawn_ctx(avs_stream_abstract_t *stream,
                  int *errno_ptr,
                  const anjay_dm_read_args_t *details) {
    uint16_t requested_format = details->requested_format;
    if (!details->uri.has_rid) {
        int ret = _anjay_handle_requested_format(&requested_format,
                                                 ANJAY_COAP_FORMAT_TLV);
#ifdef WITH_JSON
        if (ret) {
            ret = _anjay_handle_requested_format(&requested_format,
                                                 ANJAY_COAP_FORMAT_JSON);
        }
#endif
        if (ret) {
            *errno_ptr = ret;
            anjay_log(ERROR,
                      "Got option: Accept: %" PRIu16 ", but reads on "
                      "non-resource paths only support TLV and JSON formats",
                      details->requested_format);
            return NULL;
        }
    }

    anjay_msg_details_t msg_details = {
        .msg_type = ANJAY_COAP_MSG_ACKNOWLEDGEMENT,
        .format = requested_format,
        .msg_code = make_success_response_code(ANJAY_ACTION_READ),
        .observe_serial = details->observe_serial
    };

    return _anjay_output_dynamic_create(stream, errno_ptr, &msg_details,
                                        &details->uri);
}

static int dm_read(anjay_t *anjay,
                   const anjay_dm_object_def_t *const *obj,
                   const anjay_dm_read_args_t *details,
                   anjay_output_ctx_t *out_ctx) {
    anjay_log(DEBUG, "Read %s", ANJAY_DEBUG_MAKE_PATH(&details->uri));
    assert(details->uri.has_oid);
    int result = 0;
    if (details->uri.has_iid) {
        const anjay_action_info_t info = {
            .iid = details->uri.iid,
            .oid = details->uri.oid,
            .ssid = details->ssid,
            .action = ANJAY_ACTION_READ
        };

        if (!(result = ensure_instance_present(anjay, obj, details->uri.iid))) {
            if (!_anjay_access_control_action_allowed(anjay, &info)) {
                result = ANJAY_ERR_UNAUTHORIZED;
            } else if (details->uri.has_rid) {
                result = read_resource(anjay, obj, details->uri.iid, details->uri.rid,
                                       out_ctx);
            } else {
                result = read_instance(anjay, obj, details->uri.iid, out_ctx);
            }
        }
    } else {
        result = read_object(anjay, obj, details, out_ctx);
    }

    int finish_result = _anjay_output_ctx_destroy(&out_ctx);

    if (result) {
        return result;
    } else if (finish_result == ANJAY_OUTCTXERR_ANJAY_RET_NOT_CALLED) {
        anjay_log(ERROR, "unable to determine resource type: anjay_ret_* not "
                  "called during successful resource_read handler call for %s",
                  ANJAY_DEBUG_MAKE_PATH(&details->uri));
        return ANJAY_ERR_INTERNAL;
    } else {
        return finish_result;
    }
}

#ifdef WITH_OBSERVE
static void build_observe_key(anjay_observe_key_t *result,
                              const anjay_request_details_t *details) {
    result->connection.ssid = details->ssid;
    result->connection.type = details->conn_type;
    result->oid = details->uri.oid;
    result->iid = (details->uri.has_iid ? details->uri.iid : ANJAY_IID_INVALID);
    result->rid = (details->uri.has_rid ? details->uri.rid : ANJAY_RID_EMPTY);
    result->format = details->requested_format;
}

static anjay_output_ctx_t *
dm_observe_spawn_ctx(avs_stream_abstract_t *stream,
                     int *errno_ptr,
                     const anjay_dm_read_args_t *details,
                     double *out_numeric) {
    anjay_output_ctx_t *raw = dm_read_spawn_ctx(stream, errno_ptr, details);
    if (raw) {
        anjay_output_ctx_t *out = _anjay_observe_decorate_ctx(raw, out_numeric);
        if (!out) {
            _anjay_output_ctx_destroy(&raw);
        }
        return out;
    }
    return NULL;
}

ssize_t _anjay_dm_read_for_observe(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj,
                                   const anjay_dm_read_args_t *details,
                                   anjay_msg_details_t *out_details,
                                   double *out_numeric,
                                   char *buffer,
                                   size_t size) {
    anjay_observe_stream_t out = _anjay_new_observe_stream(out_details);
    avs_stream_outbuf_set_buffer(&out.outbuf, buffer, size);
    int out_ctx_errno = 0;
    anjay_output_ctx_t *out_ctx =
            dm_observe_spawn_ctx((avs_stream_abstract_t *) &out,
                                 &out_ctx_errno, details, out_numeric);
    if (!out_ctx) {
        return out_ctx_errno ? out_ctx_errno : ANJAY_ERR_INTERNAL;
    }
    int result = dm_read(anjay, obj, details, out_ctx);
    if (out_ctx_errno < 0) {
        return (ssize_t) out_ctx_errno;
    } else if (result < 0) {
        return (ssize_t) result;
    }
    return (ssize_t) avs_stream_outbuf_offset(&out.outbuf);
}

static int dm_observe(anjay_t *anjay,
                      const anjay_dm_object_def_t *const *obj,
                      const anjay_request_details_t *details) {
    anjay_log(DEBUG, "Observe %s", ANJAY_DEBUG_MAKE_PATH(&details->uri));
    assert(details->uri.has_oid);
    char buf[ANJAY_MAX_OBSERVABLE_RESOURCE_SIZE];
    double numeric = NAN;
    anjay_msg_details_t observe_details;
    ssize_t size = _anjay_dm_read_for_observe(anjay, obj,
                                              &DETAILS_TO_DM_READ_ARGS(details),
                                              &observe_details,
                                              &numeric,
                                              buf, sizeof(buf));
    if (size < 0) {
        return (int) size;
    }
    anjay_observe_key_t key;
    build_observe_key(&key, details);
    int result;
    if ((result = _anjay_observe_put_entry(anjay, &key, &observe_details,
                                           &details->request_identity,
                                           numeric, buf, (size_t) size))
            || (result = _anjay_coap_stream_setup_response(anjay->comm_stream,
                                                           &observe_details))
            || (result = avs_stream_write(anjay->comm_stream,
                                          buf, (size_t) size))) {
        _anjay_observe_remove_entry(anjay, &key);
    }
    return result;
}
#else // WITH_OBSERVE
#define dm_observe(anjay, obj, details) \
        (anjay_log(ERROR, "Not supported: Observe %s", \
                   ANJAY_DEBUG_MAKE_PATH(&details->uri)), \
                   ANJAY_ERR_NOT_IMPLEMENTED)
#endif // WITH_OBSERVE

static int dm_read_or_observe(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj,
                              const anjay_request_details_t *details) {
    if (details->observe == ANJAY_COAP_OBSERVE_REGISTER) {
        return dm_observe(anjay, obj, details);
    } else {
#ifdef WITH_OBSERVE
        if (details->observe == ANJAY_COAP_OBSERVE_DEREGISTER) {
            anjay_observe_key_t key;
            build_observe_key(&key, details);
            _anjay_observe_remove_entry(anjay, &key);
        }
#endif // WITH_OBSERVE
        const anjay_dm_read_args_t read_args = DETAILS_TO_DM_READ_ARGS(details);
        int out_ctx_errno = 0;
        anjay_output_ctx_t *out_ctx = dm_read_spawn_ctx(
                anjay->comm_stream, &out_ctx_errno, &read_args);
        if (!out_ctx) {
            return out_ctx_errno ? out_ctx_errno : ANJAY_ERR_INTERNAL;
        }
        int result = dm_read(anjay, obj, &read_args, out_ctx);
        if (out_ctx_errno) {
            return out_ctx_errno;
        } else {
            return result;
        }
    }
}

static inline bool resource_specific_request_attrs_empty(
        const anjay_request_attributes_t *attrs) {
    return !attrs->has_greater_than
            && !attrs->has_less_than
            && !attrs->has_step;
}

static inline bool
request_attrs_empty(const anjay_request_attributes_t *attrs) {
    return !attrs->has_min_period
            && !attrs->has_max_period
#ifdef WITH_CON_ATTR
            && !attrs->custom.has_con
#endif
            && resource_specific_request_attrs_empty(attrs);
}

#ifdef WITH_DISCOVER
static int dm_discover(anjay_t *anjay,
                       const anjay_dm_object_def_t *const *obj,
                       const anjay_request_details_t *details) {
    anjay_log(DEBUG, "Discover %s", ANJAY_DEBUG_MAKE_PATH(&details->uri));
    /* Access Control check is omitted here, because dm_discover is always
     * allowed. */
    int result = _anjay_coap_stream_setup_response(anjay->comm_stream,
            &(anjay_msg_details_t) {
                .msg_type = ANJAY_COAP_MSG_ACKNOWLEDGEMENT,
                .msg_code = make_success_response_code(ANJAY_ACTION_DISCOVER),
                .format = ANJAY_COAP_FORMAT_APPLICATION_LINK
            });

    if (result) {
        anjay_log(ERROR, "could not setup message");
        return result;
    }

    if (details->uri.has_iid) {
        if (!(result = ensure_instance_present(anjay, obj, details->uri.iid))) {
            if (details->uri.has_rid) {
                if (!(result = ensure_resource_supported_and_present(
                        anjay, obj, details->uri.iid, details->uri.rid))) {
                    result = _anjay_discover_resource(
                            anjay, obj,
                            details->uri.iid, details->uri.rid, details->ssid);
                }
            } else {
                result = _anjay_discover_instance(anjay, obj, details->uri.iid,
                                                  details->ssid);
            }
        }
    } else {
        result = _anjay_discover_object(anjay, obj, details->ssid);
    }

    if (result) {
        anjay_log(ERROR, "Discover %s failed!",
                  ANJAY_DEBUG_MAKE_PATH(&details->uri));
    }
    return result;
}
#else // WITH_DISCOVER
#define dm_discover(anjay, obj, details) \
        (anjay_log(ERROR, "Not supported: Discover %s", \
                   ANJAY_DEBUG_MAKE_PATH(&details->uri)), \
                   ANJAY_ERR_NOT_IMPLEMENTED)
#endif // WITH_DISCOVER

static int
write_present_resource(anjay_t *anjay,
                       const anjay_dm_object_def_t *const *obj,
                       anjay_iid_t iid,
                       anjay_rid_t rid,
                       anjay_input_ctx_t *in_ctx,
                       anjay_notify_queue_t *notify_queue) {
    if (!has_resource_operation_bit(anjay, obj, rid,
                                    ANJAY_DM_RESOURCE_OP_BIT_W)) {
        anjay_log(ERROR, "Write /%u/*/%u is not supported", (*obj)->oid, rid);
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
    int result = _anjay_dm_resource_write(anjay, obj, iid, rid, in_ctx, NULL);
    if (!result && notify_queue) {
        result = _anjay_notify_queue_resource_change(notify_queue,
                                                     (*obj)->oid, iid, rid);
    }
    return result;
}

static int write_resource(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj,
                          anjay_iid_t iid,
                          anjay_rid_t rid,
                          anjay_input_ctx_t *in_ctx,
                          anjay_notify_queue_t *notify_queue) {
    if (!_anjay_dm_resource_supported(obj, rid)) {
        return ANJAY_ERR_NOT_FOUND;
    }
    return write_present_resource(anjay, obj, iid, rid, in_ctx, notify_queue);
}

typedef enum {
    WRITE_INSTANCE_FAIL_ON_UNSUPPORTED,
    WRITE_INSTANCE_IGNORE_UNSUPPORTED
} write_instance_hint_t;

static int write_instance_impl(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj,
                               anjay_iid_t iid,
                               anjay_input_ctx_t *in_ctx,
                               anjay_notify_queue_t *notify,
                               write_instance_hint_t hint) {
    anjay_id_type_t type;
    uint16_t id;
    int retval;
    while (!(retval = _anjay_input_get_id(in_ctx, &type, &id))) {
        if (type != ANJAY_ID_RID) {
            return ANJAY_ERR_BAD_REQUEST;
        }
        bool supported = _anjay_dm_resource_supported(obj, id);
        if (!supported && hint == WRITE_INSTANCE_FAIL_ON_UNSUPPORTED) {
            return ANJAY_ERR_NOT_FOUND;
        }
        if ((supported && (retval = write_present_resource(anjay, obj, iid, id,
                                                           in_ctx, notify)))
                || (retval = _anjay_input_next_entry(in_ctx))) {
            return retval;
        }
    }
    return (retval == ANJAY_GET_INDEX_END) ? 0 : retval;
}

static int write_instance(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj,
                          anjay_iid_t iid,
                          anjay_input_ctx_t *in_ctx,
                          anjay_notify_queue_t *notify,
                          write_instance_hint_t hint) {
    anjay_id_type_t type;
    uint16_t id;
    int retval = _anjay_input_get_id(in_ctx, &type, &id);
    if (retval) {
        return (retval == ANJAY_GET_INDEX_END) ? 0 : retval;
    }
    if (type == ANJAY_ID_IID) {
        if (id != iid) {
            anjay_log(WARNING, "Attempted Write on /%" PRIu16 " with "
                               "IID==%" PRIu16 " in CoAP Options but "
                               "IID==%" PRIu16 " in content header",
                      (*obj)->oid, iid, id);
            return ANJAY_ERR_BAD_REQUEST;
        }
        anjay_input_ctx_t *nested_ctx = _anjay_input_nested_ctx(in_ctx);
        if (!nested_ctx) {
            return ANJAY_ERR_INTERNAL;
        }
        if ((retval = write_instance_impl(anjay, obj, iid, nested_ctx, notify,
                                          hint))
                || (retval = _anjay_input_next_entry(in_ctx))
                || (retval = _anjay_input_get_id(in_ctx, &type, &id))
                        != ANJAY_GET_INDEX_END) {
            return retval;
        }
        return 0;
    } else {
        return write_instance_impl(anjay, obj, iid, in_ctx, notify, hint);
    }
}

static int dm_write(anjay_t *anjay,
                    const anjay_dm_object_def_t *const *obj,
                    const anjay_dm_write_args_t *args,
                    anjay_input_ctx_t *in_ctx,
                    anjay_request_action_t action,
                    uint16_t content_format) {
    anjay_log(DEBUG, "Write %s", ANJAY_DEBUG_MAKE_PATH(&args->uri));
    if (!args->uri.has_iid) {
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }

    anjay_notify_queue_t notify_queue = NULL;
    int retval = ensure_instance_present(anjay, obj, args->uri.iid);
    if (!retval) {
        const anjay_action_info_t action_info = {
            .oid = args->uri.oid,
            .iid = args->uri.iid,
            .ssid = args->ssid,
            .action = action
        };
        if (!_anjay_access_control_action_allowed(anjay, &action_info)) {
            return ANJAY_ERR_UNAUTHORIZED;
        }

        if (args->uri.has_rid) {
            const uint16_t format =
                    _anjay_translate_legacy_content_format(content_format);
            if (format == ANJAY_COAP_FORMAT_TLV) {
                retval = _anjay_dm_check_if_tlv_rid_matches_uri_rid(in_ctx,
                                                                    args->uri.rid);
            }

            if (!retval) {
                retval = write_resource(anjay, obj, args->uri.iid, args->uri.rid,
                                        in_ctx, &notify_queue);
            }
        } else {
            if (action != ANJAY_ACTION_WRITE_UPDATE) {
                retval = _anjay_dm_instance_reset(anjay, obj, args->uri.iid, NULL);
            }
            if (!retval) {
                retval = write_instance(anjay, obj, args->uri.iid,
                                        in_ctx, &notify_queue,
                                        WRITE_INSTANCE_FAIL_ON_UNSUPPORTED);
            }
        }
    }
    if (!retval) {
        retval = _anjay_notify_perform(anjay, args->ssid, notify_queue);
    }
    _anjay_notify_clear_queue(&notify_queue);
    return retval;
}

static void update_attrs(anjay_dm_internal_res_attrs_t *attrs_ptr,
                         const anjay_request_attributes_t *request_attrs) {
    if (request_attrs->has_min_period) {
        attrs_ptr->standard.common.min_period =
                request_attrs->values.standard.common.min_period;
    }
    if (request_attrs->has_max_period) {
        attrs_ptr->standard.common.max_period =
                request_attrs->values.standard.common.max_period;
    }
    if (request_attrs->has_greater_than) {
        attrs_ptr->standard.greater_than =
                request_attrs->values.standard.greater_than;
    }
    if (request_attrs->has_less_than) {
        attrs_ptr->standard.less_than =
                request_attrs->values.standard.less_than;
    }
    if (request_attrs->has_step) {
        attrs_ptr->standard.step = request_attrs->values.standard.step;
    }
#ifdef WITH_CON_ATTR
    if (request_attrs->custom.has_con) {
        attrs_ptr->custom.data.con = request_attrs->values.custom.data.con;
    }
#endif
}

static bool resource_attrs_valid(const anjay_dm_internal_res_attrs_t *attrs) {
    double step = 0.0;
    if (!isnan(attrs->standard.step)) {
        if (attrs->standard.step < 0.0) {
            anjay_log(DEBUG, "Attempted to set negative step attribute");
            return false;
        }
        step = attrs->standard.step;
    }
    if (!isnan(attrs->standard.less_than)
            && !isnan(attrs->standard.greater_than)
            && attrs->standard.less_than + 2 * step
                    >= attrs->standard.greater_than) {
        anjay_log(DEBUG, "Attempted to set attributes that fail the "
                         "'lt + 2*st < gt' precondition");
        return false;
    }
    return true;
}

static int dm_write_resource_attrs(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj,
                                   anjay_iid_t iid,
                                   anjay_rid_t rid,
                                   anjay_ssid_t ssid,
                                   const anjay_request_attributes_t *attributes) {
    anjay_dm_internal_res_attrs_t attrs = ANJAY_DM_INTERNAL_RES_ATTRS_EMPTY;
    int result = ensure_resource_supported_and_present(anjay, obj, iid, rid);

    if (!result) {
        result = _anjay_dm_resource_read_attrs(anjay, obj, iid, rid, ssid,
                                               &attrs, NULL);
    }
    if (!result) {
        update_attrs(&attrs, attributes);
        if (!resource_attrs_valid(&attrs)) {
            result = ANJAY_ERR_BAD_REQUEST;
        } else {
            result = _anjay_dm_resource_write_attrs(anjay, obj, iid, rid, ssid,
                                                    &attrs, NULL);
        }
    }
    return result;
}

static int dm_write_instance_attrs(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj,
                                   anjay_iid_t iid,
                                   anjay_ssid_t ssid,
                                   const anjay_request_attributes_t *attributes) {
    anjay_dm_internal_res_attrs_t attrs = ANJAY_DM_INTERNAL_RES_ATTRS_EMPTY;
    int result = _anjay_dm_read_combined_instance_attrs(
            anjay, obj, iid, ssid,
            _anjay_dm_get_internal_attrs(&attrs.standard.common));
    if (!result) {
        update_attrs(&attrs, attributes);
        result = _anjay_dm_instance_write_default_attrs(
                anjay, obj, iid, ssid,
                _anjay_dm_get_internal_attrs(&attrs.standard.common), NULL);
    }
    return result;
}

static int dm_write_object_attrs(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj,
                                 anjay_ssid_t ssid,
                                 const anjay_request_attributes_t *attributes) {
    anjay_dm_internal_res_attrs_t attrs = ANJAY_DM_INTERNAL_RES_ATTRS_EMPTY;
    int result = _anjay_dm_read_combined_object_attrs(
            anjay, obj, ssid,
            _anjay_dm_get_internal_attrs(&attrs.standard.common));
    if (!result) {
        update_attrs(&attrs, attributes);
        result = _anjay_dm_object_write_default_attrs(
                anjay, obj, ssid,
                _anjay_dm_get_internal_attrs(&attrs.standard.common), NULL);
    }
    return result;
}

static int dm_write_attributes(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj,
                               const anjay_request_details_t *details) {
    anjay_log(DEBUG, "Write Attributes %s",
              ANJAY_DEBUG_MAKE_PATH(&details->uri));
    assert(details->uri.has_oid);
    if (request_attrs_empty(&details->attributes)) {
        return 0;
    }
    if (!details->uri.has_rid
            && !resource_specific_request_attrs_empty(&details->attributes)) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    int result;
    if (details->uri.has_iid) {
        if (!(result = ensure_instance_present(anjay, obj, details->uri.iid))) {
            if (details->uri.has_rid) {
                result = dm_write_resource_attrs(anjay, obj, details->uri.iid,
                                                 details->uri.rid, details->ssid,
                                                 &details->attributes);
            } else {
                result = dm_write_instance_attrs(anjay, obj, details->uri.iid,
                                                 details->ssid,
                                                 &details->attributes);
            }
        }
    } else {
        result = dm_write_object_attrs(anjay, obj, details->ssid,
                                       &details->attributes);
    }
#ifdef WITH_OBSERVE
    if (!result) {
        // ensure that new attributes are "seen" by the observe code
        anjay_observe_key_t key;
        build_observe_key(&key, details);
        key.format = ANJAY_COAP_FORMAT_NONE;
        result = _anjay_observe_notify(anjay, &key, false);
    }
#endif // WITH_OBSERVE
    return result;
}

static int dm_execute(anjay_t *anjay,
                      const anjay_dm_object_def_t *const *obj,
                      const anjay_request_details_t *details,
                      anjay_input_ctx_t *in_ctx) {
    anjay_log(DEBUG, "Execute %s", ANJAY_DEBUG_MAKE_PATH(&details->uri));
    assert(details->uri.has_oid);
    if (!details->uri.has_iid || !details->uri.has_rid) {
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }

    int retval = ensure_instance_present(anjay, obj, details->uri.iid);
    if (!retval) {
        retval = ensure_resource_supported_and_present(anjay, obj,
                                         details->uri.iid, details->uri.rid);
    }
    if (!retval) {
        if (!_anjay_access_control_action_allowed(anjay,
                &DETAILS_TO_ACTION_INFO(details))) {
            return ANJAY_ERR_UNAUTHORIZED;
        }

        if (!has_resource_operation_bit(anjay, obj, details->uri.rid,
                                        ANJAY_DM_RESOURCE_OP_BIT_E)) {
            anjay_log(ERROR, "Execute %s is not supported",
                      ANJAY_DEBUG_MAKE_PATH(&details->uri));
            return ANJAY_ERR_METHOD_NOT_ALLOWED;
        }

        anjay_execute_ctx_t* execute_ctx = _anjay_execute_ctx_create(in_ctx);
        retval = _anjay_dm_resource_execute(anjay, obj, details->uri.iid,
                                            details->uri.rid, execute_ctx, NULL);
        _anjay_execute_ctx_destroy(&execute_ctx);
    }
    return retval;
}

static int set_create_response_location(anjay_oid_t oid,
                                        anjay_iid_t iid,
                                        avs_stream_abstract_t *stream) {
    AVS_STATIC_ASSERT(((anjay_oid_t) -1) == 65535, oid_is_u16);
    AVS_STATIC_ASSERT(((anjay_iid_t) -1) == 65535, iid_is_u16);
    char oid_str[6], iid_str[6];
    int result = (_anjay_snprintf(oid_str, sizeof(oid_str),
                                  "%" PRIu16, oid) < 0 ? -1 : 0);
    if (!result) {
        result = (_anjay_snprintf(iid_str, sizeof(iid_str),
                                  "%" PRIu16, iid) < 0 ? -1 : 0);
    }
    if (!result) {
        anjay_msg_details_t msg_details = {
            .msg_type = ANJAY_COAP_MSG_ACKNOWLEDGEMENT,
            .msg_code = make_success_response_code(ANJAY_ACTION_CREATE),
            .format = ANJAY_COAP_FORMAT_NONE,
            .location_path = _anjay_make_string_list(oid_str, iid_str, NULL)
        };
        if (!msg_details.location_path) {
            result = -1;
        } else {
            result = _anjay_coap_stream_setup_response(stream, &msg_details);
        }
        AVS_LIST_CLEAR(&msg_details.location_path);
    }
    return result;
}

static int dm_create_inner(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *obj,
                           anjay_iid_t *new_iid_ptr,
                           anjay_ssid_t ssid,
                           anjay_input_ctx_t *in_ctx) {
    anjay_iid_t proposed_iid = *new_iid_ptr;
    int result = _anjay_dm_instance_create(anjay, obj, new_iid_ptr, ssid, NULL);
    if (result || *new_iid_ptr == ANJAY_IID_INVALID) {
        anjay_log(DEBUG, "Instance Create handler for object %" PRIu16
                         " failed", (*obj)->oid);
        return result ? result : ANJAY_ERR_INTERNAL;
    } else if (proposed_iid != ANJAY_IID_INVALID
            && *new_iid_ptr != proposed_iid) {
        anjay_log(DEBUG, "Instance Create handler for object %" PRIu16
                         " returned Instance %" PRIu16 " while %" PRIu16
                         " was expected; removing",
                  (*obj)->oid, *new_iid_ptr, proposed_iid);
        result = ANJAY_ERR_INTERNAL;
    } else if ((result = write_instance_impl(
            anjay, obj, *new_iid_ptr, in_ctx, NULL,
            WRITE_INSTANCE_IGNORE_UNSUPPORTED))) {
        anjay_log(DEBUG, "Writing Resources for newly created "
                         "/%" PRIu16 "/%" PRIu16 "; removing",
                  (*obj)->oid, *new_iid_ptr);
    }
    return result;
}

static int dm_create_with_explicit_iid(anjay_t *anjay,
                                       const anjay_dm_object_def_t *const *obj,
                                       anjay_iid_t *new_iid_ptr,
                                       anjay_ssid_t ssid,
                                       anjay_input_ctx_t *in_ctx) {
    if (*new_iid_ptr == ANJAY_IID_INVALID) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    int result = _anjay_dm_instance_present(anjay, obj, *new_iid_ptr, NULL);
    if (result > 0) {
        anjay_log(DEBUG, "Instance /%" PRIu16 "/%" PRIu16 " already exists",
                  (*obj)->oid, *new_iid_ptr);
        return ANJAY_ERR_BAD_REQUEST;
    } else if (result) {
        anjay_log(DEBUG, "Instance Present handler for /%" PRIu16 "/%" PRIu16
                         " failed", (*obj)->oid, *new_iid_ptr);
        return result;
    }
    anjay_input_ctx_t *nested_ctx = _anjay_input_nested_ctx(in_ctx);
    result = dm_create_inner(anjay, obj, new_iid_ptr, ssid, nested_ctx);
    if (!result) {
        anjay_id_type_t id_type;
        uint16_t id;
        (void) ((result = _anjay_input_next_entry(in_ctx))
                || (result = _anjay_input_get_id(in_ctx, &id_type, &id)));
        if (result == ANJAY_GET_INDEX_END) {
            return 0;
        } else {
            anjay_log(DEBUG, "More than one Object Instance or broken input "
                             "stream while processing Object Create");
            return result ? result : ANJAY_ERR_BAD_REQUEST;
        }
    }
    return result;
}

static int dm_create(anjay_t *anjay,
                     const anjay_dm_object_def_t *const *obj,
                     const anjay_request_details_t *details,
                     anjay_input_ctx_t *in_ctx) {
    anjay_log(DEBUG, "Create %s", ANJAY_DEBUG_MAKE_PATH(&details->uri));
    if (details->uri.has_rid) {
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }

    if (!_anjay_access_control_action_allowed(anjay,
            &DETAILS_TO_ACTION_INFO(details))) {
        return ANJAY_ERR_UNAUTHORIZED;
    }

    anjay_iid_t new_iid = ANJAY_IID_INVALID;
    anjay_id_type_t stream_first_id_type;
    uint16_t stream_first_id;
    int result = _anjay_input_get_id(in_ctx,
                                     &stream_first_id_type, &stream_first_id);
    if (!result && stream_first_id_type == ANJAY_ID_IID) {
        new_iid = stream_first_id;
        result = dm_create_with_explicit_iid(anjay, obj, &new_iid, details->ssid,
                                             in_ctx);
    } else if (!result || result == ANJAY_GET_INDEX_END) {
        result = dm_create_inner(anjay, obj, &new_iid, details->ssid, in_ctx);
    }
    if (!result) {
        anjay_log(DEBUG, "created: /%u/%u", (*obj)->oid, new_iid);
        if ((result = set_create_response_location((*obj)->oid, new_iid,
                                                   anjay->comm_stream))) {
            anjay_log(DEBUG, "Could not prepare response message.");
        }
    }
    if (!result) {
        anjay_notify_queue_t notify_queue = NULL;
        (void) ((result = _anjay_notify_queue_instance_created(&notify_queue,
                                                               details->uri.oid,
                                                               new_iid))
                || (result = _anjay_notify_flush(anjay, details->ssid,
                                                 &notify_queue)));
    }
    return result;
}

static int dm_delete(anjay_t *anjay,
                     const anjay_dm_object_def_t *const *obj,
                     const anjay_request_details_t *details) {
    anjay_log(DEBUG, "Delete %s", ANJAY_DEBUG_MAKE_PATH(&details->uri));
    if (!details->uri.has_iid || details->uri.has_rid) {
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }

    int retval = ensure_instance_present(anjay, obj, details->uri.iid);
    if (!retval) {
        if (!_anjay_access_control_action_allowed(anjay,
                &DETAILS_TO_ACTION_INFO(details))) {
            return ANJAY_ERR_UNAUTHORIZED;
        }
        retval = _anjay_dm_instance_remove(anjay, obj, details->uri.iid, NULL);
    }
    if (!retval) {
        anjay_notify_queue_t notify_queue = NULL;
        (void) ((retval = _anjay_notify_queue_instance_removed(&notify_queue,
                                                               details->uri.oid,
                                                               details->uri.iid))
                || (retval = _anjay_notify_flush(anjay, details->ssid,
                                                 &notify_queue)));
    }
    return retval;
}

static int dm_cancel_observe(anjay_t *anjay,
                             const anjay_request_details_t *details) {
    (void) anjay;
    anjay_log(DEBUG, "Cancel Observe %04" PRIX16,
              details->request_identity.msg_id);
#ifdef WITH_OBSERVE
    _anjay_observe_remove_by_msg_id(anjay, details->request_identity.msg_id);
#endif // WITH_OBSERVE
    return 0;
}

int _anjay_dm_check_if_tlv_rid_matches_uri_rid(anjay_input_ctx_t *in_ctx,
                                               anjay_rid_t uri_rid) {
    anjay_id_type_t type;
    uint16_t id;
    int retval = _anjay_input_get_id(in_ctx, &type, &id);

    if (!retval && type == ANJAY_ID_RID && uri_rid == id) {
        return 0;
    }
    return retval ? retval : ANJAY_ERR_BAD_REQUEST;
}

static int invoke_transactional_action(anjay_t *anjay,
                                       const anjay_dm_object_def_t *const *obj,
                                       const anjay_request_details_t *details,
                                       anjay_input_ctx_t *in_ctx) {
    _anjay_dm_transaction_begin(anjay);
    int retval = 0;
    switch (details->action) {
    case ANJAY_ACTION_WRITE:
    case ANJAY_ACTION_WRITE_UPDATE:
        assert(in_ctx);
        retval = dm_write(anjay, obj, &DETAILS_TO_DM_WRITE_ARGS(details),
                          in_ctx, details->action, details->content_format);
        break;
    case ANJAY_ACTION_CREATE:
        assert(in_ctx);
        retval = dm_create(anjay, obj, details, in_ctx);
        break;
    case ANJAY_ACTION_DELETE:
        retval = dm_delete(anjay, obj, details);
        break;
    default:
        anjay_log(ERROR, "invalid transactional action");
        retval = ANJAY_ERR_METHOD_NOT_ALLOWED;
    }

    return _anjay_dm_transaction_finish(anjay, retval);
}

static int invoke_action(anjay_t *anjay,
                         const anjay_dm_object_def_t *const *obj,
                         const anjay_request_details_t *details,
                         anjay_input_ctx_t *in_ctx) {
    switch (details->action) {
    case ANJAY_ACTION_READ:
        return dm_read_or_observe(anjay, obj, details);
    case ANJAY_ACTION_DISCOVER:
        return dm_discover(anjay, obj, details);
    case ANJAY_ACTION_WRITE:
    case ANJAY_ACTION_WRITE_UPDATE:
    case ANJAY_ACTION_CREATE:
    case ANJAY_ACTION_DELETE:
        return invoke_transactional_action(anjay, obj, details, in_ctx);
    case ANJAY_ACTION_WRITE_ATTRIBUTES:
        return dm_write_attributes(anjay, obj, details);
    case ANJAY_ACTION_EXECUTE:
        assert(in_ctx);
        return dm_execute(anjay, obj, details, in_ctx);
    case ANJAY_ACTION_CANCEL_OBSERVE:
        return dm_cancel_observe(anjay, details);
    default:
        anjay_log(ERROR, "Invalid action for Management Interface");
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

int _anjay_dm_perform_action(anjay_t *anjay,
                             const anjay_request_details_t *details) {
    const anjay_dm_object_def_t *const *obj = NULL;
    if (details->uri.has_oid) {
        if (!(obj = _anjay_dm_find_object_by_oid(anjay, details->uri.oid))
                || !*obj) {
            anjay_log(ERROR, "Object not found: %u", details->uri.oid);
            return ANJAY_ERR_NOT_FOUND;
        }
    } else if (details->action != ANJAY_ACTION_CANCEL_OBSERVE) {
        anjay_log(ERROR, "at least Object ID must be present in Uri-Path");
        return ANJAY_ERR_BAD_REQUEST;
    }

    anjay_msg_details_t msg_details = {
        .msg_type = ANJAY_COAP_MSG_ACKNOWLEDGEMENT,
        .msg_code = make_success_response_code(details->action),
        .format = ANJAY_COAP_FORMAT_NONE
    };

    anjay_input_ctx_t *in_ctx = NULL;
    int result;
    if ((result = prepare_input_context(anjay->comm_stream, details->action,
                                        &in_ctx))
            || (result = _anjay_coap_stream_setup_response(anjay->comm_stream,
                                                           &msg_details))) {
        return result;
    }

    result = invoke_action(anjay, obj, details, in_ctx);
    if (_anjay_input_ctx_destroy(&in_ctx)) {
        anjay_log(ERROR, "input ctx cleanup failed");
    }
    return result;
}

int _anjay_dm_foreach_object(anjay_t *anjay,
                             anjay_dm_foreach_object_handler_t *handler,
                             void *data) {
    AVS_LIST(const anjay_dm_object_def_t *const *) obj;
    AVS_LIST_FOREACH(obj, anjay->dm.objects) {
        assert(*obj && **obj);

        int result = handler(anjay, *obj, data);
        if (result == ANJAY_DM_FOREACH_BREAK) {
            anjay_log(DEBUG, "foreach_object: break on /%u", (**obj)->oid);
            return 0;
        } else if (result) {
            anjay_log(ERROR, "foreach_object_handler failed for /%u (%d)",
                      (**obj)->oid, result);
            return result;
        }
    }

    return 0;
}

int _anjay_dm_foreach_instance(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj,
                               anjay_dm_foreach_instance_handler_t *handler,
                               void *data) {
    if (!obj) {
        anjay_log(ERROR, "attempt to iterate through NULL Object");
        return -1;
    }
    void *cookie = NULL;
    int result;
    anjay_iid_t iid = 0;

    while (!(result = _anjay_dm_instance_it(anjay, obj, &iid, &cookie, NULL))
            && iid != ANJAY_IID_INVALID) {
        result = handler(anjay, obj, iid, data);
        if (result == ANJAY_DM_FOREACH_BREAK) {
            anjay_log(DEBUG, "foreach_instance: break on /%u/%u", (*obj)->oid,
                      iid);
            return 0;
        } else if (result) {
            anjay_log(ERROR, "foreach_instance_handler failed for /%u/%u (%d)",
                      (*obj)->oid, iid, result);
            return result;
        }
    }

    if (result < 0) {
        anjay_log(ERROR, "instance_it handler for /%u failed (%d)", (*obj)->oid,
                  result);
    }

    return result;
}

int _anjay_dm_res_read(anjay_t *anjay,
                       const anjay_uri_path_t *path,
                       char *buffer,
                       size_t buffer_size,
                       size_t *out_bytes_read) {
    ASSERT_RESOURCE_PATH(*path);
    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, path->oid);
    if (!obj) {
        anjay_log(ERROR, "unregistered Object ID: %u", path->oid);
        return -1;
    }

    avs_stream_outbuf_t stream =
            AVS_STREAM_OUTBUF_STATIC_INITIALIZER;
    avs_stream_outbuf_set_buffer(&stream, buffer, buffer_size);

    anjay_output_buf_ctx_t ctx = _anjay_output_buf_ctx_init(&stream);

    int result = ensure_resource_supported_and_present(
            anjay, obj, path->iid, path->rid);
    if (result) {
        return result;
    }
    result = read_resource_internal(anjay, obj, path->iid, path->rid,
                                    (anjay_output_ctx_t *) &ctx);
    if (out_bytes_read) {
        *out_bytes_read = avs_stream_outbuf_offset(&stream);
    }
    return result;
}

struct anjay_dm_multires_read_ctx_struct {
    anjay_input_ctx_t *input_ctx;
};

static avs_stream_abstract_t *
read_tlv_to_membuf(anjay_t *anjay, const anjay_uri_path_t *path) {
    ASSERT_RESOURCE_PATH(*path);
    const anjay_dm_object_def_t * const *obj =
            _anjay_dm_find_object_by_oid(anjay, path->oid);
    if (!obj) {
        anjay_log(ERROR, "unregistered Object ID: %u", path->oid);
        return NULL;
    }
    avs_stream_abstract_t *membuf = avs_stream_membuf_create();
    if (!membuf) {
        return NULL;
    }
    anjay_output_ctx_t *out = _anjay_output_raw_tlv_create(membuf);
    if (!out || read_resource(anjay, obj, path->iid, path->rid, out)) {
        avs_stream_cleanup(&membuf);
    }
    _anjay_output_ctx_destroy(&out);
    return membuf;
}

anjay_input_ctx_t *
_anjay_dm_read_as_input_ctx(anjay_t *anjay, const anjay_uri_path_t *path) {
    ASSERT_RESOURCE_PATH(*path);
    avs_stream_abstract_t *membuf = read_tlv_to_membuf(anjay, path);
    if (!membuf) {
        return NULL;
    }
    anjay_input_ctx_t *out = NULL;
    if (_anjay_input_tlv_create(&out, &membuf, true)) {
        anjay_log(ERROR, "could not create the input context");
        avs_stream_cleanup(&membuf);
        return NULL;
    }
    assert(!membuf);
    return out;
}

#ifdef ANJAY_TEST
#include "test/dm.c"
#endif // ANJAY_TEST
