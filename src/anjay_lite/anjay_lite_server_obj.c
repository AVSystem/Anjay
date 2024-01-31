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

#define SERVER_OBJ_RID_SSID 0
#define SERVER_OBJ_RID_LIFETIME 1
#define SERVER_OBJ_RID_NOTIFICATION_STORING 6
#define SERVER_OBJ_RID_BINDING 7
#define SERVER_OBJ_RID_UPDATE_TRIGGER 9

#define SERVER_OBJ_RID_SSID_IDX 0
#define SERVER_OBJ_RID_LIFETIME_IDX 1
#define SERVER_OBJ_RID_NOTIFICATION_STORING_IDX 2
#define SERVER_OBJ_RID_BINDING_IDX 3
#define SERVER_OBJ_RID_UPDATE_TRIGGER_IDX 4

typedef struct {
    sdm_obj_inst_t obj_inst;
    char binding_mode_buff[2];
    bool update_trigger;
} sdm_server_obj_inst_t;

static const sdm_res_spec_t res_spec_ssid = {
    .rid = SERVER_OBJ_RID_SSID,
    .operation = SDM_RES_R,
    .type = FLUF_DATA_TYPE_INT
};
static const sdm_res_spec_t res_spec_lifetime = {
    .rid = SERVER_OBJ_RID_LIFETIME,
    .operation = SDM_RES_RW,
    .type = FLUF_DATA_TYPE_INT
};
static const sdm_res_spec_t res_spec_notification_storing = {
    .rid = SERVER_OBJ_RID_NOTIFICATION_STORING,
    .operation = SDM_RES_RW,
    .type = FLUF_DATA_TYPE_BOOL
};
static const sdm_res_spec_t res_spec_binding = {
    .rid = SERVER_OBJ_RID_BINDING,
    .operation = SDM_RES_RW,
    .type = FLUF_DATA_TYPE_STRING
};
static const sdm_res_spec_t res_spec_update_trigger = {
    .rid = SERVER_OBJ_RID_UPDATE_TRIGGER,
    .operation = SDM_RES_E
};

static int binding_write(sdm_obj_t *obj,
                         sdm_obj_inst_t *obj_inst,
                         sdm_res_t *res,
                         sdm_res_inst_t *res_inst,
                         const fluf_res_value_t *value) {
    (void) (obj);
    (void) (res);
    (void) (res_inst);

    const char *data = (const char *) value->bytes_or_string.data;
    size_t data_len = value->bytes_or_string.chunk_length;

    // only UDP binding is now supported
    if (data_len != 1 || data[0] != 'U') {
        return SDM_ERR_METHOD_NOT_ALLOWED;
    }
    sdm_server_obj_inst_t *inst = (sdm_server_obj_inst_t *) obj_inst;
    inst->binding_mode_buff[0] = data[0];
    return 0;
}

static int update_trigger_callback(sdm_obj_t *obj,
                                   sdm_obj_inst_t *obj_inst,
                                   sdm_res_t *res,
                                   const char *execute_arg,
                                   size_t execute_arg_len) {
    (void) (obj);
    (void) (res);
    (void) (execute_arg);
    (void) (execute_arg_len);

    sdm_server_obj_inst_t *inst = (sdm_server_obj_inst_t *) obj_inst;
    inst->update_trigger = true;
    return 0;
}

static const sdm_res_handlers_t res_handlers = {
    .res_write = binding_write,
    .res_execute = update_trigger_callback
};

static sdm_res_t server_obj_resources[] = {
    [SERVER_OBJ_RID_SSID_IDX] = {
        .res_spec = &res_spec_ssid
    },
    [SERVER_OBJ_RID_LIFETIME_IDX] = {
        .res_spec = &res_spec_lifetime
    },
    [SERVER_OBJ_RID_NOTIFICATION_STORING_IDX] = {
        .res_spec = &res_spec_notification_storing,
        .value.res_value.value.bool_value = true
    },
    [SERVER_OBJ_RID_BINDING_IDX] = {
        .res_spec = &res_spec_binding,
        .res_handlers = &res_handlers
    },
    [SERVER_OBJ_RID_UPDATE_TRIGGER_IDX] = {
        .res_spec = &res_spec_update_trigger,
        .res_handlers = &res_handlers
    }
};

static sdm_server_obj_inst_t server_obj_instance = {
    .obj_inst = {
        .iid = 0,
        .res_count = AVS_ARRAY_SIZE(server_obj_resources),
        .resources = server_obj_resources
    }
};

static sdm_obj_inst_t *server_obj_instances[1] = { (
        sdm_obj_inst_t *) &server_obj_instance };

static sdm_obj_t server_obj = {
    .oid = FLUF_OBJ_ID_SERVER,
    .insts = server_obj_instances,
    .inst_count = 1,
    .max_inst_count = 1
};

sdm_obj_t *anjay_lite_server_obj_setup(uint16_t ssid,
                                       uint32_t lifetime,
                                       fluf_binding_type_t binding) {
    if (ssid == 0 || ssid == UINT16_MAX || lifetime == 0
            || binding != FLUF_BINDING_UDP) {
        return NULL;
    }

    server_obj_resources[SERVER_OBJ_RID_SSID_IDX]
            .value.res_value.value.int_value = (int64_t) ssid;
    server_obj_resources[SERVER_OBJ_RID_LIFETIME_IDX]
            .value.res_value.value.int_value = (int64_t) lifetime;
    server_obj_resources[SERVER_OBJ_RID_BINDING_IDX]
            .value.res_value.value.bytes_or_string.chunk_length = 1;
    server_obj_resources[SERVER_OBJ_RID_BINDING_IDX]
            .value.res_value.value.bytes_or_string.full_length_hint = 1;
    server_obj_resources[SERVER_OBJ_RID_BINDING_IDX]
            .value.res_value.value.bytes_or_string.data =
            server_obj_instance.binding_mode_buff;
    server_obj_instance.binding_mode_buff[0] = 'U';

    return &server_obj;
}

uint32_t anjay_lite_server_obj_get_lifetime(void) {
    return (uint32_t) server_obj_resources[SERVER_OBJ_RID_LIFETIME_IDX]
            .value.res_value.value.int_value;
}

bool anjay_lite_server_obj_update_trigger_active(void) {
    if (server_obj_instance.update_trigger == true) {
        server_obj_instance.update_trigger = false;
        return true;
    }
    return false;
}
