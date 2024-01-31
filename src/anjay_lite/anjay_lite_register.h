/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_LITE_REGISTER_H
#define ANJAY_LITE_REGISTER_H

#include <stdbool.h>
#include <stdint.h>

#include <anjay_lite/anjay_lite.h>
#include <anjay_lite/anjay_lite_config.h>
#include <anjay_lite/anjay_net.h>

int anjay_lite_register_add_server(anjay_lite_conn_conf_t *server_conf,
                                   fluf_binding_type_t binding,
                                   char *endpoint,
                                   uint32_t lifetime);
void anjay_lite_register_process(anjay_lite_t *anjay_lite);

#endif // ANJAY_LITE_REGISTER_H
