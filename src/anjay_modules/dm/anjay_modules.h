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

#ifndef ANJAY_INCLUDE_ANJAY_MODULES_DM_MODULES_H
#define ANJAY_INCLUDE_ANJAY_MODULES_DM_MODULES_H

#include <anjay/dm.h>

#include <anjay_modules/anjay_notify.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef void anjay_dm_module_deleter_t(void *arg);

typedef struct {
    /**
     * Global overlay of handlers that may replace handlers natively declared
     * for all LwM2M Objects.
     *
     * When modules are installed, upon calling any of the <c>_anjay_dm_*</c>
     * callback wrapper functions with the <c>current_module</c> argument set to
     * <c>NULL</c>, the following happens:
     *
     * - The installed modules are searched in such order so that most recently
     *   installed module is first.
     * - The first module in which the appropriate handler field (e.g.
     *   <c>overlay_handlers.resource_read</c>) is non-<c>NULL</c>, is selected.
     * - If no such overlay is installed, the function declared in the LwM2M
     *   Object is selected, if non-<c>NULL</c>.
     * - If a function could be selected, it is called.
     * - If no appropriate non-<c>NULL</c> handler was found,
     *   @ref ANJAY_ERR_METHOD_NOT_ALLOWED is returned.
     *
     * Functions declared in an overlay handler may call the appropriate
     * <c>_anjay_dm_*</c> function (e.g. @ref _anjay_dm_resource_read) with the
     * <c>current_module</c> argument set to its own corresponding module
     * definition pointer (the same value that was passed as <c>module</c> to
     * <c>_anjay_dm_module_install</c>). This will cause the "underlying"
     * handler to be called - the above procedure will restart, but skipping all
     * the modules from the beginning to <c>current_module</c>, inclusive - so
     * the underlying original implementation (or another overlay, if multiple
     * are installed) will be called.
     */
    anjay_dm_handlers_t overlay_handlers;

    /**
     * A function to be called every time when Anjay is notified of some data
     * model change - this also includes changes made through LwM2M protocol
     * itself.
     */
    anjay_notify_callback_t *notify_callback;

    /**
     * A function to be called when the module is uninstalled, that will clean
     * up any resources used by it.
     */
    anjay_dm_module_deleter_t *deleter;
} anjay_dm_module_t;

/**
 * Installs an Anjay module. See the definition of fields in
 * @ref anjay_dm_module_t for a detailed explanation of what modules can do.
 *
 * @param anjay  Anjay object to operate on
 *
 * @param module Pointer to a module definition structure; this pointer is also
 *               used as a module identifier, so it needs to have at least the
 *               same lifetime as the module itself, and normally it is a
 *               singleton with static lifetime
 *
 * @param arg    Opaque pointer that can be later retrieved using
 *               @ref _anjay_dm_module_get_arg and will also be passed to the
 *               <c>notify_callback</c> and <c>deleter</c> functions.
 *
 * @returns 0 for success, or a negative value in case of error.
 */
int _anjay_dm_module_install(anjay_t *anjay,
                             const anjay_dm_module_t *module,
                             void *arg);

int _anjay_dm_module_uninstall(anjay_t *anjay, const anjay_dm_module_t *module);

/**
 * Returns the <c>arg</c> previously passed to @ref _anjay_dm_module_install
 */
void *_anjay_dm_module_get_arg(anjay_t *anjay, const anjay_dm_module_t *module);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_INCLUDE_ANJAY_MODULES_DM_MODULES_H */
