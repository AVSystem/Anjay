/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_ATTR_STORAGE_H
#define ANJAY_ATTR_STORAGE_H

#include <anjay/attr_storage.h>
#include <anjay/core.h>

#include <anjay_modules/anjay_utils_core.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct as_object_entry as_object_entry_t;

typedef struct {
    avs_stream_t *persist_data;
    bool modified_since_persist;
} as_saved_state_t;

typedef struct {
    AVS_LIST(as_object_entry_t) objects;
    bool modified_since_persist;
    as_saved_state_t saved_state;
} anjay_attr_storage_t;

static inline bool _anjay_dm_implements_any_object_default_attrs_handlers(
        const anjay_dm_installed_object_t *obj_ptr) {
    return _anjay_dm_handler_implemented(
                   obj_ptr, ANJAY_DM_HANDLER_object_read_default_attrs)
           || _anjay_dm_handler_implemented(
                      obj_ptr, ANJAY_DM_HANDLER_object_write_default_attrs);
}

static inline bool _anjay_dm_implements_any_instance_default_attrs_handlers(
        const anjay_dm_installed_object_t *obj_ptr) {
    return _anjay_dm_handler_implemented(
                   obj_ptr, ANJAY_DM_HANDLER_instance_read_default_attrs)
           || _anjay_dm_handler_implemented(
                      obj_ptr, ANJAY_DM_HANDLER_instance_write_default_attrs);
}

static inline bool _anjay_dm_implements_any_resource_attrs_handlers(
        const anjay_dm_installed_object_t *obj_ptr) {
    return _anjay_dm_handler_implemented(obj_ptr,
                                         ANJAY_DM_HANDLER_resource_read_attrs)
           || _anjay_dm_handler_implemented(
                      obj_ptr, ANJAY_DM_HANDLER_resource_write_attrs);
}

#ifdef ANJAY_WITH_LWM2M11
static inline bool _anjay_dm_implements_any_resource_instance_attrs_handlers(
        const anjay_dm_installed_object_t *obj_ptr) {
    return _anjay_dm_handler_implemented(
                   obj_ptr, ANJAY_DM_HANDLER_resource_instance_read_attrs)
           || _anjay_dm_handler_implemented(
                      obj_ptr, ANJAY_DM_HANDLER_resource_instance_write_attrs);
}
#endif // ANJAY_WITH_LWM2M11

int _anjay_attr_storage_init(anjay_unlocked_t *anjay);
void _anjay_attr_storage_cleanup(anjay_attr_storage_t *as);

avs_error_t _anjay_attr_storage_transaction_begin(anjay_unlocked_t *anjay);
void _anjay_attr_storage_transaction_commit(anjay_unlocked_t *anjay);
avs_error_t _anjay_attr_storage_transaction_rollback(anjay_unlocked_t *anjay);

int _anjay_attr_storage_notify(anjay_unlocked_t *anjay,
                               anjay_notify_queue_t queue);

extern const anjay_unlocked_dm_handlers_t _ANJAY_ATTR_STORAGE_HANDLERS;

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_ATTR_STORAGE_H */
