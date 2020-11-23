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

#include <avsystem/coap/code.h>

#include <anjay_modules/anjay_dm_utils.h>

#include "../anjay_core.h"
#include "../anjay_utils_private.h"

VISIBILITY_SOURCE_BEGIN

#define dm_log(...) _anjay_log(anjay_dm, __VA_ARGS__)

static bool has_handler(const anjay_dm_handlers_t *def, size_t handler_offset) {
    typedef void (*func_ptr_t)(void);
    return *(AVS_APPLY_OFFSET(func_ptr_t, def, handler_offset));
}

static const anjay_dm_handlers_t *
get_handler_from_list(AVS_LIST(anjay_dm_installed_module_t) module_list,
                      size_t handler_offset) {
    AVS_LIST_ITERATE(module_list) {
        if (has_handler(&module_list->def->overlay_handlers, handler_offset)) {
            return &module_list->def->overlay_handlers;
        }
    }
    return NULL;
}

static const anjay_dm_handlers_t *
get_next_handler_from_overlay(anjay_t *anjay,
                              const anjay_dm_module_t *current_module,
                              size_t handler_offset) {
    assert(current_module);

    AVS_LIST(anjay_dm_installed_module_t) *current_head =
            _anjay_dm_module_find_ptr(anjay, current_module);
    if (current_head) {
        return get_handler_from_list(AVS_LIST_NEXT(*current_head),
                                     handler_offset);
    }
    return NULL;
}

static const anjay_dm_handlers_t *
get_handler_from_overlay(anjay_t *anjay,
                         const anjay_dm_module_t *current_module,
                         size_t handler_offset) {
    if (current_module) {
        return get_next_handler_from_overlay(anjay, current_module,
                                             handler_offset);
    } else {
        return get_handler_from_list(anjay->dm.modules, handler_offset);
    }
}

static const anjay_dm_handlers_t *
get_handler(anjay_t *anjay,
            const anjay_dm_object_def_t *const *obj_ptr,
            const anjay_dm_module_t *current_module,
            size_t handler_offset) {
    const anjay_dm_handlers_t *result =
            get_handler_from_overlay(anjay, current_module, handler_offset);
    if (result) {
        return result;
    } else if (has_handler(&(*obj_ptr)->handlers, handler_offset)) {
        return &(*obj_ptr)->handlers;
    } else {
        return NULL;
    }
}

bool _anjay_dm_handler_implemented(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   const anjay_dm_module_t *current_module,
                                   size_t handler_offset) {
    return get_handler(anjay, obj_ptr, current_module, handler_offset) != NULL;
}

#define CHECKED_TAIL_CALL_HANDLER(Anjay, ObjPtr, Current, HandlerName, ...)   \
    do {                                                                      \
        const anjay_dm_handlers_t *handler =                                  \
                get_handler((Anjay), (ObjPtr), (Current),                     \
                            offsetof(anjay_dm_handlers_t, HandlerName));      \
        if (handler) {                                                        \
            int AVS_CONCAT(result, __LINE__) =                                \
                    handler->HandlerName(__VA_ARGS__);                        \
            if (AVS_CONCAT(result, __LINE__)) {                               \
                dm_log(DEBUG, #HandlerName _(" failed with code ") "%d (%s)", \
                       AVS_CONCAT(result, __LINE__),                          \
                       AVS_COAP_CODE_STRING(_anjay_make_error_response_code(  \
                               AVS_CONCAT(result, __LINE__))));               \
            }                                                                 \
            return AVS_CONCAT(result, __LINE__);                              \
        } else {                                                              \
            dm_log(DEBUG,                                                     \
                   #HandlerName _(" handler not set for object ") "/%u",      \
                   (*(ObjPtr))->oid);                                         \
            return ANJAY_ERR_METHOD_NOT_ALLOWED;                              \
        }                                                                     \
    } while (0)

int _anjay_dm_call_object_read_default_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_ssid_t ssid,
        anjay_dm_internal_oi_attrs_t *out,
        const anjay_dm_module_t *current_module) {
    dm_log(TRACE, _("object_read_default_attrs ") "/%u", (*obj_ptr)->oid);
    CHECKED_TAIL_CALL_HANDLER(anjay, obj_ptr, current_module,
                              object_read_default_attrs, anjay, obj_ptr, ssid,
                              &out->standard);
}

int _anjay_dm_call_object_write_default_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_ssid_t ssid,
        const anjay_dm_internal_oi_attrs_t *attrs,
        const anjay_dm_module_t *current_module) {
    dm_log(TRACE, _("object_write_default_attrs ") "/%u", (*obj_ptr)->oid);
    CHECKED_TAIL_CALL_HANDLER(anjay, obj_ptr, current_module,
                              object_write_default_attrs, anjay, obj_ptr, ssid,
                              &attrs->standard);
}

int _anjay_dm_call_list_instances(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr,
                                  anjay_dm_list_ctx_t *ctx,
                                  const anjay_dm_module_t *current_module) {
    dm_log(TRACE, _("list_instances ") "/%u", (*obj_ptr)->oid);
    CHECKED_TAIL_CALL_HANDLER(anjay, obj_ptr, current_module, list_instances,
                              anjay, obj_ptr, ctx);
}

int _anjay_dm_call_instance_reset(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr,
                                  anjay_iid_t iid,
                                  const anjay_dm_module_t *current_module) {
    dm_log(TRACE, _("instance_reset ") "/%u/%u", (*obj_ptr)->oid, iid);
    int result = _anjay_dm_transaction_include_object(anjay, obj_ptr);
    if (result) {
        return result;
    }
    CHECKED_TAIL_CALL_HANDLER(anjay, obj_ptr, current_module, instance_reset,
                              anjay, obj_ptr, iid);
}

int _anjay_dm_call_instance_create(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_iid_t iid,
                                   const anjay_dm_module_t *current_module) {
    dm_log(TRACE, _("instance_create ") "/%u/%u", (*obj_ptr)->oid, iid);
    int result = _anjay_dm_transaction_include_object(anjay, obj_ptr);
    if (result) {
        return result;
    }
    CHECKED_TAIL_CALL_HANDLER(anjay, obj_ptr, current_module, instance_create,
                              anjay, obj_ptr, iid);
}

int _anjay_dm_call_instance_remove(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_iid_t iid,
                                   const anjay_dm_module_t *current_module) {
    dm_log(TRACE, _("instance_remove ") "/%u/%u", (*obj_ptr)->oid, iid);
    int result = _anjay_dm_transaction_include_object(anjay, obj_ptr);
    if (result) {
        return result;
    }
    CHECKED_TAIL_CALL_HANDLER(anjay, obj_ptr, current_module, instance_remove,
                              anjay, obj_ptr, iid);
}

int _anjay_dm_call_instance_read_default_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_ssid_t ssid,
        anjay_dm_internal_oi_attrs_t *out,
        const anjay_dm_module_t *current_module) {
    dm_log(TRACE, _("instance_read_default_attrs ") "/%u/%u", (*obj_ptr)->oid,
           iid);
    CHECKED_TAIL_CALL_HANDLER(anjay, obj_ptr, current_module,
                              instance_read_default_attrs, anjay, obj_ptr, iid,
                              ssid, &out->standard);
}

int _anjay_dm_call_instance_write_default_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_ssid_t ssid,
        const anjay_dm_internal_oi_attrs_t *attrs,
        const anjay_dm_module_t *current_module) {
    dm_log(TRACE, _("instance_write_default_attrs ") "/%u/%u", (*obj_ptr)->oid,
           iid);
    CHECKED_TAIL_CALL_HANDLER(anjay, obj_ptr, current_module,
                              instance_write_default_attrs, anjay, obj_ptr, iid,
                              ssid, &attrs->standard);
}

int _anjay_dm_call_list_resources(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr,
                                  anjay_iid_t iid,
                                  anjay_dm_resource_list_ctx_t *ctx,
                                  const anjay_dm_module_t *current_module) {
    dm_log(TRACE, _("list_resources ") "/%u/%u", (*obj_ptr)->oid, iid);
    CHECKED_TAIL_CALL_HANDLER(anjay, obj_ptr, current_module, list_resources,
                              anjay, obj_ptr, iid, ctx);
}

int _anjay_dm_call_resource_read(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj_ptr,
                                 anjay_iid_t iid,
                                 anjay_rid_t rid,
                                 anjay_riid_t riid,
                                 anjay_output_ctx_t *ctx,
                                 const anjay_dm_module_t *current_module) {
    dm_log(LAZY_TRACE, _("resource_read ") "%s",
           ANJAY_DEBUG_MAKE_PATH(&MAKE_RESOURCE_INSTANCE_PATH((*obj_ptr)->oid,
                                                              iid, rid, riid)));
    CHECKED_TAIL_CALL_HANDLER(anjay, obj_ptr, current_module, resource_read,
                              anjay, obj_ptr, iid, rid, riid, ctx);
}

int _anjay_dm_call_resource_write(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr,
                                  anjay_iid_t iid,
                                  anjay_rid_t rid,
                                  anjay_riid_t riid,
                                  anjay_input_ctx_t *ctx,
                                  const anjay_dm_module_t *current_module) {
    dm_log(LAZY_TRACE, _("resource_write ") "%s",
           ANJAY_DEBUG_MAKE_PATH(&MAKE_RESOURCE_INSTANCE_PATH((*obj_ptr)->oid,
                                                              iid, rid, riid)));
    int result = _anjay_dm_transaction_include_object(anjay, obj_ptr);
    if (result) {
        return result;
    }
    CHECKED_TAIL_CALL_HANDLER(anjay, obj_ptr, current_module, resource_write,
                              anjay, obj_ptr, iid, rid, riid, ctx);
}

int _anjay_dm_call_resource_execute(anjay_t *anjay,
                                    const anjay_dm_object_def_t *const *obj_ptr,
                                    anjay_iid_t iid,
                                    anjay_rid_t rid,
                                    anjay_execute_ctx_t *execute_ctx,
                                    const anjay_dm_module_t *current_module) {
    dm_log(TRACE, _("resource_execute ") "/%u/%u/%u", (*obj_ptr)->oid, iid,
           rid);
    CHECKED_TAIL_CALL_HANDLER(anjay, obj_ptr, current_module, resource_execute,
                              anjay, obj_ptr, iid, rid, execute_ctx);
}

int _anjay_dm_call_resource_reset(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr,
                                  anjay_iid_t iid,
                                  anjay_rid_t rid,
                                  const anjay_dm_module_t *current_module) {
    dm_log(TRACE, _("resource_reset ") "/%u/%u/%u", (*obj_ptr)->oid, iid, rid);
    int result = _anjay_dm_transaction_include_object(anjay, obj_ptr);
    if (result) {
        return result;
    }
    CHECKED_TAIL_CALL_HANDLER(anjay, obj_ptr, current_module, resource_reset,
                              anjay, obj_ptr, iid, rid);
}

int _anjay_dm_call_list_resource_instances(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_dm_list_ctx_t *ctx,
        const anjay_dm_module_t *current_module) {
    dm_log(TRACE, _("list_resource_instances ") "/%u/%u/%u", (*obj_ptr)->oid,
           iid, rid);
    if (!_anjay_dm_handler_implemented(anjay, obj_ptr, current_module,
                                       offsetof(anjay_dm_handlers_t,
                                                list_resource_instances))) {
        dm_log(TRACE,
               _("list_resource_instances handler not set for object ") "/%u",
               (*obj_ptr)->oid);
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
    CHECKED_TAIL_CALL_HANDLER(anjay, obj_ptr, current_module,
                              list_resource_instances, anjay, obj_ptr, iid, rid,
                              ctx);
}

int _anjay_dm_call_resource_read_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_ssid_t ssid,
        anjay_dm_internal_r_attrs_t *out,
        const anjay_dm_module_t *current_module) {
    dm_log(TRACE, _("resource_read_attrs ") "/%u/%u/%u", (*obj_ptr)->oid, iid,
           rid);
    CHECKED_TAIL_CALL_HANDLER(anjay, obj_ptr, current_module,
                              resource_read_attrs, anjay, obj_ptr, iid, rid,
                              ssid, &out->standard);
}

int _anjay_dm_call_resource_write_attrs(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_ssid_t ssid,
        const anjay_dm_internal_r_attrs_t *attrs,
        const anjay_dm_module_t *current_module) {
    dm_log(TRACE, _("resource_write_attrs ") "/%u/%u/%u", (*obj_ptr)->oid, iid,
           rid);
    CHECKED_TAIL_CALL_HANDLER(anjay, obj_ptr, current_module,
                              resource_write_attrs, anjay, obj_ptr, iid, rid,
                              ssid, &attrs->standard);
}

int _anjay_dm_call_transaction_begin(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        const anjay_dm_module_t *current_module) {
    dm_log(TRACE, _("begin_object_transaction ") "/%u", (*obj_ptr)->oid);
    CHECKED_TAIL_CALL_HANDLER(anjay, obj_ptr, current_module, transaction_begin,
                              anjay, obj_ptr);
}

int _anjay_dm_call_transaction_validate(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        const anjay_dm_module_t *current_module) {
    dm_log(TRACE, _("validate_object ") "/%u", (*obj_ptr)->oid);
    CHECKED_TAIL_CALL_HANDLER(anjay, obj_ptr, current_module,
                              transaction_validate, anjay, obj_ptr);
}

int _anjay_dm_call_transaction_commit(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        const anjay_dm_module_t *current_module) {
    dm_log(TRACE, _("commit_object ") "/%u", (*obj_ptr)->oid);
    CHECKED_TAIL_CALL_HANDLER(anjay, obj_ptr, current_module,
                              transaction_commit, anjay, obj_ptr);
}

int _anjay_dm_call_transaction_rollback(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        const anjay_dm_module_t *current_module) {
    dm_log(TRACE, _("rollback_object ") "/%u", (*obj_ptr)->oid);
    CHECKED_TAIL_CALL_HANDLER(anjay, obj_ptr, current_module,
                              transaction_rollback, anjay, obj_ptr);
}

#define MAX_SANE_TRANSACTION_DEPTH 64

void _anjay_dm_transaction_begin(anjay_t *anjay) {
    dm_log(TRACE, _("transaction_begin"));
    ++anjay->transaction_state.depth;
    assert(anjay->transaction_state.depth < MAX_SANE_TRANSACTION_DEPTH);
}

int _anjay_dm_transaction_include_object(
        anjay_t *anjay, const anjay_dm_object_def_t *const *obj_ptr) {
    dm_log(TRACE, _("transaction_include_object ") "/%u", (*obj_ptr)->oid);
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
            dm_log(ERROR, _("out of memory"));
            return -1;
        }
        *new_entry = obj_ptr;
        AVS_LIST_INSERT(it, new_entry);
        int result = _anjay_dm_call_transaction_begin(anjay, obj_ptr, NULL);
        if (result) {
            // transaction_begin may have added new entries
            while (*it != new_entry) {
                AVS_LIST_ADVANCE_PTR(&it);
            }
            AVS_LIST_DELETE(it);
            return result;
        }
    }
    return 0;
}

static int commit_or_rollback_object(anjay_t *anjay,
                                     const anjay_dm_object_def_t *const *obj,
                                     int predicate) {
    int result;
    if (predicate) {
        if ((result = _anjay_dm_call_transaction_rollback(anjay, obj, NULL))) {
            dm_log(ERROR,
                   _("cannot rollback transaction on ") "/%u" _(
                           ", object may be left in undefined state"),
                   (*obj)->oid);
            return result;
        }
    } else if ((result = _anjay_dm_call_transaction_commit(anjay, obj, NULL))) {
        dm_log(ERROR, _("cannot commit transaction on ") "/%u", (*obj)->oid);
        predicate = result;
    }
    return predicate;
}

int _anjay_dm_transaction_validate(anjay_t *anjay) {
    dm_log(TRACE, _("transaction_validate"));
    AVS_LIST(const anjay_dm_object_def_t *const *) obj;
    AVS_LIST_FOREACH(obj, anjay->transaction_state.objs_in_transaction) {
        dm_log(TRACE, _("validate_object ") "/%u", (**obj)->oid);
        int result = _anjay_dm_call_transaction_validate(anjay, *obj, NULL);
        if (result) {
            dm_log(ERROR, _("Validation failed for ") "/%u", (**obj)->oid);
            return result;
        }
    }
    return 0;
}

int _anjay_dm_transaction_finish_without_validation(anjay_t *anjay,
                                                    int result) {
    dm_log(TRACE, _("transaction_finish"));
    assert(anjay->transaction_state.depth > 0);
    if (--anjay->transaction_state.depth != 0) {
        return result;
    }
    int final_result = result;
    AVS_LIST_CLEAR(&anjay->transaction_state.objs_in_transaction) {
        int commit_result = commit_or_rollback_object(
                anjay, *anjay->transaction_state.objs_in_transaction, result);
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

bool _anjay_dm_transaction_object_included(
        anjay_t *anjay, const anjay_dm_object_def_t *const *obj_ptr) {
    if (anjay->transaction_state.depth > 0) {
        AVS_LIST(const anjay_dm_object_def_t *const *) *it;
        AVS_LIST_FOREACH_PTR(it,
                             &anjay->transaction_state.objs_in_transaction) {
            if (**it == obj_ptr) {
                return true;
            } else if (**it > obj_ptr) {
                break;
            }
        }
    }
    return false;
}

int anjay_dm_list_instances_SINGLE(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_dm_list_ctx_t *ctx) {
    (void) anjay;
    (void) obj_ptr;
    anjay_dm_emit(ctx, 0);
    return 0;
}

int anjay_dm_transaction_NOOP(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    (void) obj_ptr;
    return 0;
}
