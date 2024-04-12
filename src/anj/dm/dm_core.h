/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_DM_DM_CORE_H
#define ANJAY_DM_DM_CORE_H

#include <limits.h>

#include "dm_utils.h"
#include "dm_utils_core.h"

typedef void dm_list_ctx_emit_t(dm_list_ctx_t *, uint16_t id);

static inline int _dm_map_present_result(int result) {
    if (!result) {
        return FLUF_COAP_CODE_NOT_FOUND;
    } else if (result > 0) {
        return 0;
    } else {
        return result;
    }
}

#endif // ANJAY_DM_DM_CORE_H
