/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef SDM_CORE_H
#define SDM_CORE_H

#include "sdm_core.h"

#include <anj/anj_config.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ANJ_WITH_SDM_LOGS
#    ifndef AVS_COMMONS_WITH_AVS_LOG
#        error "ANJ_WITH_SDM_LOGS requires avs_log to be enabled"
#    endif
#    include <avsystem/commons/avs_log.h>
#    define sdm_log(...) avs_log(sdm, __VA_ARGS__)
#else // ANJ_WITH_SDM_LOGS
#    define sdm_log(...) \
        do {             \
        } while (0)
#endif // ANJ_WITH_SDM_LOGS

#define _SDM_OBJ_SERVER_SSID_RID 0
#define _SDM_OBJ_SECURITY_SERVER_URI_RID 0
#define _SDM_OBJ_SECURITY_BOOTSTRAP_SERVER_RID 1
#define _SDM_OBJ_SECURITY_SSID_RID 10
#define _SDM_OBJ_SECURITY_OSCORE_RID 17

int _sdm_begin_register_op(sdm_data_model_t *dm);

int _sdm_begin_bootstrap_discover_op(sdm_data_model_t *dm,
                                     const fluf_uri_path_t *base_path);

int _sdm_begin_discover_op(sdm_data_model_t *dm,
                           const fluf_uri_path_t *base_path);

int _sdm_begin_execute_op(sdm_data_model_t *dm,
                          const fluf_uri_path_t *base_path);

int _sdm_begin_read_op(sdm_data_model_t *dm, const fluf_uri_path_t *base_path);

int _sdm_get_resource_value(sdm_data_model_t *dm,
                            const fluf_uri_path_t *path,
                            fluf_res_value_t *out_value,
                            fluf_data_type_t *out_type);

int _sdm_begin_write_op(sdm_data_model_t *dm, const fluf_uri_path_t *base_path);

int _sdm_begin_create_op(sdm_data_model_t *dm,
                         const fluf_uri_path_t *base_path);

int _sdm_process_delete_op(sdm_data_model_t *dm,
                           const fluf_uri_path_t *base_path);

int _sdm_delete_res_instance(sdm_data_model_t *dm);

int _sdm_call_operation_begin(sdm_obj_t *obj, fluf_op_t operation);

int _sdm_get_obj_ptr_call_operation_begin(sdm_data_model_t *dm,
                                          fluf_oid_t oid,
                                          sdm_obj_t **out_obj);

int _sdm_get_obj_ptrs(sdm_obj_t *obj,
                      const fluf_uri_path_t *path,
                      _sdm_entity_ptrs_t *out_ptrs);

int _sdm_get_entity_ptrs(sdm_data_model_t *dm,
                         const fluf_uri_path_t *path,
                         _sdm_entity_ptrs_t *out_ptrs);

#ifndef NDEBUG
int _sdm_check_obj_instance(sdm_obj_inst_t *inst);

int _sdm_check_obj(sdm_obj_t *obj);
#endif // NDEBUG

static inline bool _sdm_is_multi_instance_resource(sdm_res_operation_t op) {
    return op == SDM_RES_RM || op == SDM_RES_WM || op == SDM_RES_RWM;
}

static inline sdm_obj_t *_sdm_find_obj(sdm_data_model_t *dm, fluf_oid_t oid) {
    for (uint16_t idx = 0; idx < dm->objs_count; idx++) {
        if (dm->objs[idx]->oid == oid) {
            return dm->objs[idx];
        }
    }
    return NULL;
}

static inline sdm_obj_inst_t *_sdm_find_inst(sdm_obj_t *obj, fluf_iid_t iid) {
    for (uint16_t idx = 0; idx < obj->inst_count; idx++) {
        if (obj->insts[idx]->iid == iid) {
            return obj->insts[idx];
        }
    }
    return NULL;
}

static inline sdm_res_t *_sdm_find_res(sdm_obj_inst_t *inst, fluf_rid_t rid) {
    for (uint16_t idx = 0; idx < inst->res_count; idx++) {
        if (inst->resources[idx].res_spec->rid == rid) {
            return &inst->resources[idx];
        }
    }
    return NULL;
}

static inline sdm_res_inst_t *_sdm_find_res_inst(sdm_res_t *res,
                                                 fluf_riid_t riid) {
    for (uint16_t idx = 0; idx < res->value.res_inst.inst_count; idx++) {
        if (res->value.res_inst.insts[idx]->riid == riid) {
            return res->value.res_inst.insts[idx];
        }
    }
    return NULL;
}

#ifdef __cplusplus
}
#endif

#endif // SDM_CORE_H
