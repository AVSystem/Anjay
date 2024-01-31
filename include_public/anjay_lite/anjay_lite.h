/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_LITE_H
#define ANJAY_LITE_H

#include <stdbool.h>
#include <stdint.h>

#include <anjay_lite/anjay_lite_config.h>
#include <anjay_lite/anjay_net.h>

#include <fluf/fluf_io.h>

#include <anj/sdm_impl.h>
#include <anj/sdm_io.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ANJAY_SECURITY_PSK = 0,         //< Pre-Shared Key mode
    ANJAY_SECURITY_RPK = 1,         //< Raw Public Key mode
    ANJAY_SECURITY_CERTIFICATE = 2, //< Certificate mode
    ANJAY_SECURITY_NOSEC = 3,       //< NoSec mode
    ANJAY_SECURITY_EST = 4          //< Certificate mode with EST
} anjay_security_mode_t;

typedef struct {
    uint16_t ssid;
    uint32_t lifetime;
    fluf_binding_type_t binding;
    anjay_security_mode_t security_mode;
    char *hostname;
    uint16_t port;
} anjay_lite_server_conf_t;

typedef union {
    anjay_net_op_open_udp_args_t udp;
    anjay_net_op_open_dtls_args_t dtls;
} anjay_lite_conn_conf_t;

typedef struct {
    sdm_obj_t *objs_array[ANJAY_LITE_ALLOWED_OBJECT_NUMBER];
    sdm_data_model_t dm;
    sdm_process_ctx_t dm_impl;
    anjay_lite_server_conf_t server_conf;
    char *endpoint_name;
} anjay_lite_t;

int anjay_lite_init(anjay_lite_t *anjay_lite);

void anjay_lite_process(anjay_lite_t *anjay_lite);

void anjay_lite_send(uint8_t *payload, size_t size);

#ifdef __cplusplus
}
#endif

#endif // ANJAY_LITE_H
