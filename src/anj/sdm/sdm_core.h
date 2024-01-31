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

#ifdef __cplusplus
extern "C" {
#endif

#ifdef SDM_WITH_LOGS
#    ifndef AVS_COMMONS_WITH_AVS_LOG
#        error "SDM_WITH_LOGS requires avs_log to be enabled"
#    endif
#    include <avsystem/commons/avs_log.h>
#    define sdm_log(...) avs_log(sdm, __VA_ARGS__)
#else // SDM_WITH_LOGS
#    define sdm_log(...) \
        do {             \
        } while (0)
#endif // SDM_WITH_LOGS

#define _SDM_ONGOING_OP_ERROR_CHECK(Dm)             \
    do {                                            \
        if (!Dm->op_in_progress) {                  \
            sdm_log(ERROR, "No ongoing operation"); \
            return SDM_ERR_LOGIC;                   \
        }                                           \
    } while (0)

#define _SDM_ONGOING_OP_COUNT_ERROR_CHECK(Dm)          \
    do {                                               \
        if (!Dm->op_count) {                           \
            sdm_log(ERROR, "No more records to read"); \
            Dm->result = SDM_ERR_LOGIC;                \
            return Dm->result;                         \
        }                                              \
    } while (0)

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

int _sdm_create_object_instance(sdm_data_model_t *dm, fluf_iid_t iid);

int _sdm_process_delete_op(sdm_data_model_t *dm,
                           const fluf_uri_path_t *base_path);

int _sdm_delete_res_instance(sdm_data_model_t *dm);

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

#ifdef __cplusplus
}
#endif

#endif // SDM_CORE_H
