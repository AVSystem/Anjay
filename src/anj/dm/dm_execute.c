/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anj/dm.h>

#include "../dm_core.h"
#include "../dm_utils/dm_utils_core.h"

int dm_execute(dm_t *dm, const fluf_uri_path_t *uri) {
    AVS_ASSERT(dm, "dm is NULL");
    AVS_ASSERT(uri, "uri is NULL");

    dm_log(DEBUG, _("Execute ") "%s", DM_DEBUG_MAKE_PATH(uri));
    if (!fluf_uri_path_is(uri, FLUF_ID_RID)) {
        dm_log(WARNING,
               _("Executable URI must point to resource. Actual: ") "%s",
               DM_DEBUG_MAKE_PATH(uri));
        return FLUF_COAP_CODE_METHOD_NOT_ALLOWED;
    }

    const dm_installed_object_t *obj;
    int result = _dm_find_object(dm, uri, &obj);
    if (result) {
        return result;
    }

    if ((result =
                 _dm_verify_instance_present(dm, obj, uri->ids[FLUF_ID_IID]))) {
        dm_log(WARNING, _("Instance is not present."));
        return result;
    }
    dm_resource_kind_t kind;
    if ((result = _dm_verify_resource_present(dm, obj, uri->ids[FLUF_ID_IID],
                                              uri->ids[FLUF_ID_RID], &kind))) {
        dm_log(WARNING, _("Resource is not present."));
        return result;
    }
    if (!_dm_res_kind_executable(kind)) {
        dm_log(DEBUG, "%s" _(" is not executable"), DM_DEBUG_MAKE_PATH(uri));
        return FLUF_COAP_CODE_METHOD_NOT_ALLOWED;
    }
    if ((result = _dm_call_resource_execute(dm, obj, uri->ids[FLUF_ID_IID],
                                            uri->ids[FLUF_ID_RID]))) {
        dm_log(WARNING, _("Resource execute handler failed: ") "%d", result);
    }
    return result;
}
