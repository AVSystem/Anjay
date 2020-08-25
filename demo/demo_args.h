/*
 * Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
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
#include <anjay/anjay_config.h>
#include <anjay/fw_update.h>

#include "demo_utils.h"
#include "objects.h"

typedef struct access_entry {
    anjay_ssid_t ssid;
    anjay_oid_t oid;
    anjay_iid_t iid;
    anjay_access_mask_t mask;
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
    int32_t msg_cache_size;
    bool confirmable_notifications;
#ifndef _WIN32
    bool disable_stdin;
#endif // _WIN32
    const char *fw_updated_marker_path;
    avs_net_security_info_t fw_security_info;
    /**
     * If nonzero (not @ref ANJAY_FW_UPDATE_RESULT_INITIAL), Firmware Update
     * object will be initialized in UPDATING state. In that case,
     * @ref anjay_fw_update_set_result will be used after a while to trigger a
     * transition to this update result. This simulates a FOTA procedure during
     * which the client is restarted while upgrade is still in progress.
     */
    anjay_fw_update_result_t fw_update_delayed_result;
    const char *attr_storage_file;
    const char *dm_persistence_file;

    bool disable_legacy_server_initiated_bootstrap;
    avs_coap_udp_tx_params_t tx_params;
    avs_net_dtls_handshake_timeouts_t dtls_hs_tx_params;
    /**
     * This flag allows to enable callback providing tx_params for firmware
     * update only if some of parameters were changed by passing proper command
     * line argument to demo. Otherwise tx_params should be inherited from
     * Anjay.
     */
    bool fwu_tx_params_modified;
    avs_coap_udp_tx_params_t fwu_tx_params;
    size_t stored_notification_limit;

    bool prefer_hierarchical_formats;
    bool use_connection_id;

    uint32_t *default_ciphersuites;
    size_t default_ciphersuites_count;

} cmdline_args_t;

int demo_parse_argv(cmdline_args_t *parsed_args, int argc, char **argv);

#endif
