/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef DEMO_ARGS_H
#define DEMO_ARGS_H

#include <anjay/access_control.h>
#include <anjay/anjay.h>

#include "objects.h"
#include "utils.h"

typedef struct access_entry {
    anjay_ssid_t ssid;
    anjay_oid_t oid;
} access_entry_t;

typedef struct cmdline_args {
    const char *endpoint_name;
    uint16_t udp_listen_port;
    server_connection_args_t connection_args;
    const char *location_csv;
    time_t location_update_frequency_s;
    AVS_LIST(access_entry_t) access_entries;
    int32_t inbuf_size;
    int32_t outbuf_size;
    bool cleanup_fw_on_upgrade;
} cmdline_args_t;

int demo_parse_argv(cmdline_args_t *parsed_args, int argc, char **argv);

#endif
