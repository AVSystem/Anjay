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

int _sdm_begin_register_op(sdm_data_model_t *dm) {
    assert(dm);

    _sdm_reg_ctx_t *reg_ctx = &dm->op_ctx.reg_ctx;
    dm->op_count = 0;
    for (uint16_t idx = 0; idx < dm->objs_count; idx++) {
        sdm_obj_t *obj = dm->objs[idx];
        if (obj->oid != FLUF_OBJ_ID_SECURITY
                && obj->oid != FLUF_OBJ_ID_OSCORE) {
            obj->in_transaction = true;
            if (obj->obj_handlers && obj->obj_handlers->operation_begin) {
                dm->result =
                        obj->obj_handlers->operation_begin(obj, dm->operation);
                if (dm->result) {
                    return dm->result;
                }
            }
            dm->op_count = dm->op_count + 1 + obj->inst_count;
        }
    }
    reg_ctx->level = FLUF_ID_OID;
    reg_ctx->obj_idx = 0;
    reg_ctx->inst_idx = 0;
    return 0;
}

int sdm_get_register_record(sdm_data_model_t *dm,
                            fluf_uri_path_t *out_path,
                            const char **out_version) {
    assert(dm && out_path && out_version);

    _sdm_reg_ctx_t *reg_ctx = &dm->op_ctx.reg_ctx;
    assert(reg_ctx->obj_idx < dm->objs_count);

    if (dm->operation != FLUF_OP_REGISTER && dm->operation != FLUF_OP_UPDATE) {
        sdm_log(ERROR, "Incorrect operation");
        dm->result = SDM_ERR_LOGIC;
        return dm->result;
    }

    _SDM_ONGOING_OP_ERROR_CHECK(dm);
    _SDM_ONGOING_OP_COUNT_ERROR_CHECK(dm);

    if (reg_ctx->level == FLUF_ID_OID) {
        if (dm->objs[reg_ctx->obj_idx]->oid == FLUF_OBJ_ID_SECURITY
                || dm->objs[reg_ctx->obj_idx]->oid == FLUF_OBJ_ID_OSCORE) {
            reg_ctx->obj_idx++;
        }

        sdm_obj_t *obj = dm->objs[reg_ctx->obj_idx];
        *out_path = FLUF_MAKE_OBJECT_PATH(obj->oid);
        *out_version = obj->version;

        if (!obj->inst_count) {
            reg_ctx->obj_idx++;
        } else {
            reg_ctx->level = FLUF_ID_IID;
            reg_ctx->inst_idx = 0;
        }
    } else {
        sdm_obj_t *obj = dm->objs[reg_ctx->obj_idx];
        assert(reg_ctx->inst_idx < obj->inst_count);

        *out_path = FLUF_MAKE_INSTANCE_PATH(obj->oid,
                                            obj->insts[reg_ctx->inst_idx]->iid);
        *out_version = NULL;
        reg_ctx->inst_idx++;
        if (reg_ctx->inst_idx == obj->inst_count) {
            reg_ctx->level = FLUF_ID_OID;
            reg_ctx->obj_idx++;
        }
    }

    dm->op_count--;
    return dm->op_count > 0 ? 0 : SDM_LAST_RECORD;
}
