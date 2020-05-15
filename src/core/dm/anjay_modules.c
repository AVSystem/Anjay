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

#include "../anjay_core.h"

VISIBILITY_SOURCE_BEGIN

AVS_LIST(anjay_dm_installed_module_t) *
_anjay_dm_module_find_ptr(anjay_t *anjay, const anjay_dm_module_t *module) {
    if (!anjay) {
        return NULL;
    }
    AVS_LIST(anjay_dm_installed_module_t) *entry_ptr;
    AVS_LIST_FOREACH_PTR(entry_ptr, &anjay->dm.modules) {
        if ((*entry_ptr)->def == module) {
            return entry_ptr;
        }
    }
    return NULL;
}

int _anjay_dm_module_install(anjay_t *anjay,
                             const anjay_dm_module_t *module,
                             void *arg) {
    if (_anjay_dm_module_find_ptr(anjay, module)) {
        anjay_log(ERROR, _("module ") "%p" _(" is already installed"),
                  (const void *) module);
        return -1;
    }
    AVS_LIST(anjay_dm_installed_module_t) new_entry =
            AVS_LIST_NEW_ELEMENT(anjay_dm_installed_module_t);
    if (!new_entry) {
        anjay_log(ERROR, _("out of memory"));
        return -1;
    }
    new_entry->def = module;
    new_entry->arg = arg;
    AVS_LIST_INSERT(&anjay->dm.modules, new_entry);
    return 0;
}

int _anjay_dm_module_uninstall(anjay_t *anjay,
                               const anjay_dm_module_t *module) {
    AVS_LIST(anjay_dm_installed_module_t) *module_ptr =
            _anjay_dm_module_find_ptr(anjay, module);
    if (!module_ptr) {
        anjay_log(ERROR, _("attempting to uninstall a non-installed module"));
        return -1;
    }
    if ((*module_ptr)->def->deleter) {
        (*module_ptr)->def->deleter((*module_ptr)->arg);
    }
    AVS_LIST_DELETE(module_ptr);
    return 0;
}

void *_anjay_dm_module_get_arg(anjay_t *anjay,
                               const anjay_dm_module_t *module) {
    AVS_LIST(anjay_dm_installed_module_t) *entry_ptr =
            _anjay_dm_module_find_ptr(anjay, module);
    return entry_ptr ? (*entry_ptr)->arg : NULL;
}
