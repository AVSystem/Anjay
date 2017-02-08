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

#include <anjay_modules/dm.h>

#include "../utils.h"
#include "../anjay.h"

VISIBILITY_SOURCE_BEGIN

#define dm_log(...) _anjay_log(anjay_dm, __VA_ARGS__)

#define CHECKED_HANDLER_CALL(ObjPtr, HandlerName, ...) \
    (((*(ObjPtr))->HandlerName) \
        ? (((*(ObjPtr))->HandlerName)(__VA_ARGS__)) \
        : (anjay_log(ERROR, #HandlerName " handler not set for object /%u", \
                     (*(ObjPtr))->oid), \
           ANJAY_ERR_METHOD_NOT_ALLOWED))

int _anjay_dm_object_read_default_attrs(anjay_t *anjay,
                                        const anjay_dm_object_def_t *const *obj_ptr,
                                        anjay_ssid_t ssid,
                                        anjay_dm_attributes_t *out) {
    dm_log(TRACE, "object_read_default_attrs /%u", (*obj_ptr)->oid);
    return CHECKED_HANDLER_CALL(obj_ptr, object_read_default_attrs,
                                anjay, obj_ptr, ssid, out);
}

int _anjay_dm_object_write_default_attrs(anjay_t *anjay,
                                         const anjay_dm_object_def_t *const *obj_ptr,
                                         anjay_ssid_t ssid,
                                         const anjay_dm_attributes_t *attrs) {
    dm_log(TRACE, "object_write_default_attrs /%u", (*obj_ptr)->oid);
    return CHECKED_HANDLER_CALL(obj_ptr, object_write_default_attrs,
                                anjay, obj_ptr, ssid, attrs);
}

int _anjay_dm_instance_it(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_iid_t *out,
                          void **cookie) {
    dm_log(TRACE, "instance_it /%u", (*obj_ptr)->oid);
    return CHECKED_HANDLER_CALL(obj_ptr, instance_it,
                                anjay, obj_ptr, out, cookie);
}

int _anjay_dm_instance_reset(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj_ptr,
                             anjay_iid_t iid) {
    dm_log(TRACE, "instance_reset /%u/%u", (*obj_ptr)->oid, iid);
    int result = _anjay_dm_transaction_include_object(anjay, obj_ptr);
    if (!result) {
        result = CHECKED_HANDLER_CALL(obj_ptr, instance_reset,
                                      anjay, obj_ptr, iid);
    }
    return result;
}

int _anjay_dm_instance_present(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid) {
    dm_log(TRACE, "instance_present /%u/%u", (*obj_ptr)->oid, iid);
    return CHECKED_HANDLER_CALL(obj_ptr, instance_present,
                                anjay, obj_ptr, iid);
}

int _anjay_dm_instance_create(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t *inout_iid,
                              anjay_ssid_t ssid) {
    dm_log(TRACE, "instance_create /%u/%u", (*obj_ptr)->oid, *inout_iid);
    int result = _anjay_dm_transaction_include_object(anjay, obj_ptr);
    if (!result) {
        result = CHECKED_HANDLER_CALL(obj_ptr, instance_create,
                                      anjay, obj_ptr, inout_iid, ssid);
    }
    return result;
}

int _anjay_dm_instance_remove(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t iid) {
    dm_log(TRACE, "instance_remove /%u/%u", (*obj_ptr)->oid, iid);
    int result = _anjay_dm_transaction_include_object(anjay, obj_ptr);
    if (!result) {
        result = CHECKED_HANDLER_CALL(obj_ptr, instance_remove,
                                      anjay, obj_ptr, iid);
    }
    return result;
}

int _anjay_dm_instance_read_default_attrs(anjay_t *anjay,
                                          const anjay_dm_object_def_t *const *obj_ptr,
                                          anjay_iid_t iid,
                                          anjay_ssid_t ssid,
                                          anjay_dm_attributes_t *out) {
    dm_log(TRACE, "instance_read_default_attrs /%u/%u", (*obj_ptr)->oid, iid);
    return CHECKED_HANDLER_CALL(obj_ptr, instance_read_default_attrs,
                                anjay, obj_ptr, iid, ssid, out);
}

int _anjay_dm_instance_write_default_attrs(anjay_t *anjay,
                                           const anjay_dm_object_def_t *const *obj_ptr,
                                           anjay_iid_t iid,
                                           anjay_ssid_t ssid,
                                           const anjay_dm_attributes_t *attrs) {
    dm_log(TRACE, "instance_write_default_attrs /%u/%u", (*obj_ptr)->oid, iid);
    return CHECKED_HANDLER_CALL(obj_ptr, instance_write_default_attrs,
                                anjay, obj_ptr, iid, ssid, attrs);
}

int
_anjay_dm_resource_supported_and_present(anjay_t *anjay,
                                         const anjay_dm_object_def_t *const *obj_ptr,
                                         anjay_iid_t iid,
                                         anjay_rid_t rid) {
    int retval = _anjay_dm_resource_supported(anjay, obj_ptr, rid);
    if (retval < 0 || retval > 1) {
        return retval;
    } else if (retval) {
        return _anjay_dm_resource_present(anjay, obj_ptr, iid, rid);
    }
    return 0;
}

int _anjay_dm_resource_present(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid,
                               anjay_rid_t rid) {
    anjay_log(TRACE, "resource_present /%u/%u/%u", (*obj_ptr)->oid, iid, rid);
    return CHECKED_HANDLER_CALL(obj_ptr, resource_present,
                                anjay, obj_ptr, iid, rid);
}

int _anjay_dm_resource_supported(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj_ptr,
                                 anjay_rid_t rid) {
    anjay_log(TRACE, "resource_supported /%u/*/%u", (*obj_ptr)->oid, rid);
    if (rid >= (*obj_ptr)->rid_bound) {
        return 0;
    }
    return CHECKED_HANDLER_CALL(obj_ptr, resource_supported,
                                anjay, obj_ptr, rid);
}

int _anjay_dm_resource_operations(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr,
                                  anjay_rid_t rid,
                                  anjay_dm_resource_op_mask_t *out) {
    anjay_log(TRACE, "resource_operations /%u/*/%u", (*obj_ptr)->oid, rid);
    if (!(*obj_ptr)->resource_operations) {
        anjay_log(TRACE, "resource_operations for /%u not implemented - "
                         "assumed all operations supported",
                  (*obj_ptr)->oid);
        *out = ANJAY_DM_RESOURCE_OP_BIT_R
                | ANJAY_DM_RESOURCE_OP_BIT_W
                | ANJAY_DM_RESOURCE_OP_BIT_E;
        return 0;
    }
    *out = ANJAY_DM_RESOURCE_OP_NONE;
    return CHECKED_HANDLER_CALL(obj_ptr, resource_operations, anjay, obj_ptr,
                                rid, out);
}

int _anjay_dm_resource_read(anjay_t *anjay,
                            const anjay_dm_object_def_t *const *obj_ptr,
                            anjay_iid_t iid,
                            anjay_rid_t rid,
                            anjay_output_ctx_t *ctx) {
    anjay_log(TRACE, "resource_read /%u/%u/%u", (*obj_ptr)->oid, iid, rid);
    return CHECKED_HANDLER_CALL(obj_ptr, resource_read,
                                anjay, obj_ptr, iid, rid, ctx);
}

int _anjay_dm_resource_write(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj_ptr,
                             anjay_iid_t iid,
                             anjay_rid_t rid,
                             anjay_input_ctx_t *ctx) {
    anjay_log(TRACE, "resource_write /%u/%u/%u", (*obj_ptr)->oid, iid, rid);
    int result = _anjay_dm_transaction_include_object(anjay, obj_ptr);
    if (!result) {
        result = CHECKED_HANDLER_CALL(obj_ptr, resource_write,
                                      anjay, obj_ptr, iid, rid, ctx);
    }
    return result;
}

int _anjay_dm_resource_execute(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid,
                               anjay_rid_t rid,
                               anjay_execute_ctx_t *execute_ctx) {
    anjay_log(TRACE, "resource_execute /%u/%u/%u", (*obj_ptr)->oid, iid, rid);
    return CHECKED_HANDLER_CALL(obj_ptr, resource_execute,
                                anjay, obj_ptr, iid, rid, execute_ctx);
}

int _anjay_dm_resource_dim(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *obj_ptr,
                           anjay_iid_t iid,
                           anjay_rid_t rid) {
    anjay_log(TRACE, "resource_dim /%u/%u/%u", (*obj_ptr)->oid, iid, rid);
    return CHECKED_HANDLER_CALL(obj_ptr, resource_dim,
                                anjay, obj_ptr, iid, rid);
}

int _anjay_dm_resource_read_attrs(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr,
                                  anjay_iid_t iid,
                                  anjay_rid_t rid,
                                  anjay_ssid_t ssid,
                                  anjay_dm_attributes_t *out) {
    anjay_log(TRACE, "resource_read_attrs /%u/%u/%u", (*obj_ptr)->oid, iid, rid);
    return CHECKED_HANDLER_CALL(obj_ptr, resource_read_attrs,
                                anjay, obj_ptr, iid, rid, ssid, out);
}

int _anjay_dm_resource_write_attrs(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_iid_t iid,
                                   anjay_rid_t rid,
                                   anjay_ssid_t ssid,
                                   const anjay_dm_attributes_t *attrs) {
    anjay_log(TRACE, "resource_write_attrs /%u/%u/%u", (*obj_ptr)->oid, iid, rid);
    return CHECKED_HANDLER_CALL(obj_ptr, resource_write_attrs,
                                anjay, obj_ptr, iid, rid, ssid, attrs);
}

#define MAX_SANE_TRANSACTION_DEPTH 64

void _anjay_dm_transaction_begin(anjay_t *anjay) {
    anjay_log(TRACE, "transaction_begin");
    ++anjay->transaction_state.depth;
    assert(anjay->transaction_state.depth < MAX_SANE_TRANSACTION_DEPTH);
}

int _anjay_dm_transaction_include_object(
        anjay_t *anjay, const anjay_dm_object_def_t *const *obj_ptr) {
    anjay_log(TRACE, "transaction_include_object /%u", (*obj_ptr)->oid);
    assert(anjay->transaction_state.depth > 0);
    AVS_LIST(const anjay_dm_object_def_t *const *) *it;
    AVS_LIST_FOREACH_PTR(it, &anjay->transaction_state.objs_in_transaction) {
        if (**it >= obj_ptr) {
            break;
        }
    }
    if (!*it || **it != obj_ptr) {
        AVS_LIST(const anjay_dm_object_def_t *const *) new_entry =
                AVS_LIST_NEW_ELEMENT(const anjay_dm_object_def_t *const *);
        if (!new_entry) {
            anjay_log(ERROR, "out of memory");
            return -1;
        }
        *new_entry = obj_ptr;
        AVS_LIST_INSERT(it, new_entry);
        anjay_log(TRACE, "begin_object_transaction /%u", (*obj_ptr)->oid);
        int result = CHECKED_HANDLER_CALL(obj_ptr, transaction_begin,
                                          anjay, obj_ptr);
        if (result) {
            // transaction_begin may have added new entries
            while (*it != new_entry) {
                it = AVS_LIST_NEXT_PTR(it);
            }
            AVS_LIST_DELETE(it);
            return result;
        }
    }
    return 0;
}

static int commit_object(anjay_t *anjay,
                         const anjay_dm_object_def_t *const *obj_ptr) {
    anjay_log(TRACE, "commit_object /%u/*/*", (*obj_ptr)->oid);
    int result = CHECKED_HANDLER_CALL(obj_ptr, transaction_commit,
                                      anjay, obj_ptr);
    return result;
}

static int rollback_object(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *obj_ptr) {
    anjay_log(TRACE, "rollback_object /%u/*/*", (*obj_ptr)->oid);
    int result = CHECKED_HANDLER_CALL(obj_ptr, transaction_rollback,
                                      anjay, obj_ptr);
    return result;
}

static int commit_or_rollback_object(anjay_t *anjay,
                                     const anjay_dm_object_def_t *const *obj,
                                     int predicate) {
    int result;
    if (predicate) {
        if ((result = rollback_object(anjay, obj)) != 0) {
            anjay_log(ERROR, "cannot rollback transaction on /%u, "
                             "object may be left in undefined state",
                      (*obj)->oid);
            return result;
        }
    } else if ((result = commit_object(anjay, obj)) != 0) {
        anjay_log(ERROR, "cannot commit transaction on /%u",
                  (*obj)->oid);
        predicate = result;
    }
    return predicate;
}

int _anjay_dm_transaction_validate(anjay_t *anjay) {
    anjay_log(TRACE, "transaction_validate");
    AVS_LIST(const anjay_dm_object_def_t *const *) obj;
    AVS_LIST_FOREACH(obj, anjay->transaction_state.objs_in_transaction) {
        anjay_log(TRACE, "validate_object /%u", (**obj)->oid);
        int result = CHECKED_HANDLER_CALL(*obj, transaction_validate,
                                          anjay, *obj);
        if (result) {
            anjay_log(ERROR, "Validation failed for /%u", (**obj)->oid);
            return result;
        }
    }
    return 0;
}

int
_anjay_dm_transaction_finish_without_validation(anjay_t *anjay, int result) {
    anjay_log(TRACE, "transaction_finish");
    assert(anjay->transaction_state.depth > 0);
    if (--anjay->transaction_state.depth != 0) {
        return result;
    }
    int final_result = result;
    AVS_LIST_CLEAR(&anjay->transaction_state.objs_in_transaction) {
        int commit_result = commit_or_rollback_object(
                anjay,
                *anjay->transaction_state.objs_in_transaction,
                result);
        if (!final_result && commit_result) {
            final_result = commit_result;
        }
    }
    return final_result;
}

int _anjay_dm_transaction_finish(anjay_t *anjay, int result) {
    if (!result && anjay->transaction_state.depth == 1) {
        result = _anjay_dm_transaction_validate(anjay);
    }
    return _anjay_dm_transaction_finish_without_validation(anjay, result);
}

int anjay_dm_instance_it_SINGLE(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr,
                                anjay_iid_t *out,
                                void **cookie) {
    (void) obj_ptr;
    if (!*cookie) {
        *cookie = anjay;
        *out = 0;
    } else {
        *out = ANJAY_IID_INVALID;
    }
    return 0;
}

int anjay_dm_instance_present_SINGLE(anjay_t *anjay,
                                     const anjay_dm_object_def_t *const *obj_ptr,
                                     anjay_iid_t iid) {
    (void) anjay; (void) obj_ptr;
    return (iid == 0);
}

int anjay_dm_resource_present_TRUE(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_iid_t iid,
                                   anjay_rid_t rid) {
    (void) anjay; (void) obj_ptr; (void) iid, (void) rid;
    return 1;
}

int anjay_dm_resource_supported_TRUE(anjay_t *anjay,
                                     const anjay_dm_object_def_t *const *obj_ptr,
                                     anjay_rid_t rid) {
    (void) anjay; (void) obj_ptr; (void) rid;
    return 1;
}

int anjay_dm_transaction_NOOP(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    (void) obj_ptr;
    return 0;
}
