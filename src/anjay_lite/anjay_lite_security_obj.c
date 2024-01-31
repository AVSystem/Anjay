/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <stdint.h>
#include <string.h>

#include <avsystem/commons/avs_defs.h>

#include <fluf/fluf_defs.h>

#include <anj/sdm_io.h>

#include <anjay_lite/anjay_lite.h>

#include "anjay_lite_objs.h"

#define SECURITY_OBJ_RID_URI 0
#define SECURITY_OBJ_RID_BOOTSTRAP_SERVER 1
#define SECURITY_OBJ_RID_SEC_MODE 2
#define SECURITY_OBJ_RID_PUBLIC_KEY 3
#define SECURITY_OBJ_RID_SERVER_KEY 4
#define SECURITY_OBJ_RID_SECRET_KEY 5
#define SECURITY_OBJ_RID_SSID 10

#define SECURITY_OBJ_RID_URI_IDX 0
#define SECURITY_OBJ_RID_BOOTSTRAP_SERVER_IDX 1
#define SECURITY_OBJ_RID_SEC_MODE_IDX 2
#define SECURITY_OBJ_RID_PUBLIC_KEY_IDX 3
#define SECURITY_OBJ_RID_SERVER_KEY_IDX 4
#define SECURITY_OBJ_RID_SECRET_KEY_IDX 5
#define SECURITY_OBJ_RID_SSID_IDX 6

static const sdm_res_spec_t res_spec_uri = {
    .rid = SECURITY_OBJ_RID_URI,
    .operation = SDM_RES_R,
    .type = FLUF_DATA_TYPE_STRING
};
static const sdm_res_spec_t res_spec_bootstrap_server = {
    .rid = SECURITY_OBJ_RID_BOOTSTRAP_SERVER,
    .operation = SDM_RES_R,
    .type = FLUF_DATA_TYPE_BOOL
};
static const sdm_res_spec_t res_spec_sec_mode = {
    .rid = SECURITY_OBJ_RID_SEC_MODE,
    .operation = SDM_RES_R,
    .type = FLUF_DATA_TYPE_INT
};
static const sdm_res_spec_t res_spec_public_key = {
    .rid = SECURITY_OBJ_RID_PUBLIC_KEY,
    .operation = SDM_RES_R,
    .type = FLUF_DATA_TYPE_STRING
};
static const sdm_res_spec_t res_spec_server_key = {
    .rid = SECURITY_OBJ_RID_SERVER_KEY,
    .operation = SDM_RES_R,
    .type = FLUF_DATA_TYPE_STRING
};
static const sdm_res_spec_t res_spec_secret_key = {
    .rid = SECURITY_OBJ_RID_SECRET_KEY,
    .operation = SDM_RES_R,
    .type = FLUF_DATA_TYPE_STRING
};
static const sdm_res_spec_t res_spec_ssid = {
    .rid = SECURITY_OBJ_RID_SSID,
    .operation = SDM_RES_R,
    .type = FLUF_DATA_TYPE_INT
};

static sdm_res_t security_obj_resources[] = {
    [SECURITY_OBJ_RID_URI_IDX] = {
        .res_spec = &res_spec_uri
    },
    [SECURITY_OBJ_RID_BOOTSTRAP_SERVER_IDX] = {
        .res_spec = &res_spec_bootstrap_server,
        .value.res_value.value.bool_value = false
    },
    [SECURITY_OBJ_RID_SEC_MODE_IDX] = {
        .res_spec = &res_spec_sec_mode
    },
    [SECURITY_OBJ_RID_PUBLIC_KEY_IDX] = {
        .res_spec = &res_spec_public_key
    },
    [SECURITY_OBJ_RID_SERVER_KEY_IDX] = {
        .res_spec = &res_spec_server_key
    },
    [SECURITY_OBJ_RID_SECRET_KEY_IDX] = {
        .res_spec = &res_spec_secret_key
    },
    [SECURITY_OBJ_RID_SSID_IDX] = {
        .res_spec = &res_spec_ssid
    }
};

static sdm_obj_inst_t security_obj_instance = {
    .iid = 0,
    .res_count = AVS_ARRAY_SIZE(security_obj_resources),
    .resources = security_obj_resources
};

static sdm_obj_inst_t *security_obj_instances[1] = { &security_obj_instance };

static sdm_obj_t security_obj = {
    .oid = FLUF_OBJ_ID_SECURITY,
    .insts = security_obj_instances,
    .inst_count = 1,
    .max_inst_count = 1
};

sdm_obj_t *anjay_lite_security_obj_setup(uint16_t ssid,
                                         char *uri,
                                         anjay_security_mode_t sec_mode) {
    if (ssid == 0 || ssid == UINT16_MAX || !uri
            || sec_mode != ANJAY_SECURITY_NOSEC) {
        return NULL;
    }

    security_obj_resources[SECURITY_OBJ_RID_SSID_IDX]
            .value.res_value.value.int_value = (int64_t) ssid;
    security_obj_resources[SECURITY_OBJ_RID_URI_IDX]
            .value.res_value.value.bytes_or_string.data = uri;
    security_obj_resources[SECURITY_OBJ_RID_URI_IDX]
            .value.res_value.value.bytes_or_string.chunk_length = strlen(uri);
    security_obj_resources[SECURITY_OBJ_RID_SEC_MODE_IDX]
            .value.res_value.value.int_value = (int64_t) sec_mode;

    return &security_obj;
}
