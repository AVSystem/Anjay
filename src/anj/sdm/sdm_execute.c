/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <fluf/fluf_defs.h>
#include <fluf/fluf_io.h>
#include <fluf/fluf_utils.h>

#include <anj/sdm.h>
#include <anj/sdm_io.h>

#include "sdm_core.h"

int _sdm_begin_execute_op(sdm_data_model_t *dm,
                          const fluf_uri_path_t *base_path) {
    assert(dm && base_path && fluf_uri_path_is(base_path, FLUF_ID_RID));
    sdm_obj_t *obj;
    dm->result = _sdm_get_obj_ptr_call_operation_begin(
            dm, base_path->ids[FLUF_ID_OID], &obj);
    if (dm->result) {
        return dm->result;
    }
    dm->result = _sdm_get_obj_ptrs(obj, base_path, &dm->entity_ptrs);
    if (dm->result) {
        return dm->result;
    }
    if (dm->entity_ptrs.res->res_spec->operation != SDM_RES_E) {
        sdm_log(ERROR, "Resource is not executable");
        dm->result = SDM_ERR_METHOD_NOT_ALLOWED;
        return dm->result;
    }
    return 0;
}

int sdm_execute(sdm_data_model_t *dm,
                const char *execute_arg,
                size_t execute_arg_len) {
    assert(dm && dm->entity_ptrs.res && dm->entity_ptrs.res->res_handlers
           && dm->entity_ptrs.res->res_handlers->res_execute);
    assert(dm->op_in_progress && !dm->result);
    dm->result =
            dm->entity_ptrs.res->res_handlers->res_execute(dm->entity_ptrs.obj,
                                                           dm->entity_ptrs.inst,
                                                           dm->entity_ptrs.res,
                                                           execute_arg,
                                                           execute_arg_len);
    if (dm->result) {
        sdm_log(ERROR, "res_execute handler failed.");
        return dm->result;
    }
    return 0;
}
