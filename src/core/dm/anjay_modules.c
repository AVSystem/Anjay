/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#include "../anjay_core.h"

VISIBILITY_SOURCE_BEGIN

AVS_LIST(anjay_dm_installed_module_t) *
_anjay_dm_module_find_ptr(anjay_unlocked_t *anjay,
                          anjay_dm_module_deleter_t *module_deleter) {
    if (!anjay) {
        return NULL;
    }
    AVS_LIST(anjay_dm_installed_module_t) *entry_ptr;
    AVS_LIST_FOREACH_PTR(entry_ptr, &anjay->dm.modules) {
        if ((*entry_ptr)->deleter == module_deleter) {
            return entry_ptr;
        }
    }
    return NULL;
}

int _anjay_dm_module_install(anjay_unlocked_t *anjay,
                             anjay_dm_module_deleter_t *module_deleter,
                             void *arg) {
    assert(module_deleter);
    if (_anjay_dm_module_find_ptr(anjay, module_deleter)) {
        anjay_log(ERROR, _("module ") "%p" _(" is already installed"),
                  (const void *) (uintptr_t) module_deleter);
        return -1;
    }
    AVS_LIST(anjay_dm_installed_module_t) new_entry =
            AVS_LIST_NEW_ELEMENT(anjay_dm_installed_module_t);
    if (!new_entry) {
        anjay_log(ERROR, _("out of memory"));
        return -1;
    }
    new_entry->deleter = module_deleter;
    new_entry->arg = arg;
    AVS_LIST_INSERT(&anjay->dm.modules, new_entry);
    return 0;
}

int _anjay_dm_module_uninstall(anjay_unlocked_t *anjay,
                               anjay_dm_module_deleter_t *module_deleter) {
    AVS_LIST(anjay_dm_installed_module_t) *module_ptr =
            _anjay_dm_module_find_ptr(anjay, module_deleter);
    if (!module_ptr) {
        anjay_log(ERROR, _("attempting to uninstall a non-installed module"));
        return -1;
    }
    module_deleter((*module_ptr)->arg);
    AVS_LIST_DELETE(module_ptr);
    return 0;
}

void *_anjay_dm_module_get_arg(anjay_unlocked_t *anjay,
                               anjay_dm_module_deleter_t *module_deleter) {
    AVS_LIST(anjay_dm_installed_module_t) *entry_ptr =
            _anjay_dm_module_find_ptr(anjay, module_deleter);
    return entry_ptr ? (*entry_ptr)->arg : NULL;
}
