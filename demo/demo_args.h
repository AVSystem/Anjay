/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
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
    avs_net_ssl_version_t dtls_version;
    server_connection_args_t connection_args;
    const char *location_csv;
    time_t location_update_frequency_s;
#ifdef ANJAY_WITH_MODULE_ACCESS_CONTROL
    AVS_LIST(access_entry_t) access_entries;
#endif // ANJAY_WITH_MODULE_ACCESS_CONTROL
    int32_t inbuf_size;
    int32_t outbuf_size;
    int32_t msg_cache_size;
    bool confirmable_notifications;
    bool disable_stdin;
#ifdef ANJAY_WITH_MODULE_FW_UPDATE
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
#endif // ANJAY_WITH_MODULE_FW_UPDATE

#ifdef AVS_COMMONS_STREAM_WITH_FILE
#    ifdef ANJAY_WITH_ATTR_STORAGE
    const char *attr_storage_file;
#    endif // ANJAY_WITH_ATTR_STORAGE
#    ifdef AVS_COMMONS_WITH_AVS_PERSISTENCE
    const char *dm_persistence_file;
#    endif // AVS_COMMONS_WITH_AVS_PERSISTENCE
#endif     // AVS_COMMONS_STREAM_WITH_FILE

    bool disable_legacy_server_initiated_bootstrap;
#ifdef AVS_COMMONS_STREAM_WITH_FILE
#endif // AVS_COMMONS_STREAM_WITH_FILE

#ifdef ANJAY_WITH_MODULE_FACTORY_PROVISIONING
    const char *provisioning_file;
#endif // ANJAY_WITH_MODULE_FACTORY_PROVISIONING
    avs_coap_udp_tx_params_t tx_params;
    avs_net_dtls_handshake_timeouts_t dtls_hs_tx_params;
#ifdef ANJAY_WITH_MODULE_FW_UPDATE
    /**
     * This flag allows to enable callback providing tx_params for firmware
     * update only if some of parameters were changed by passing proper command
     * line argument to demo. Otherwise tx_params should be inherited from
     * Anjay.
     */
    bool fwu_tx_params_modified;
    avs_coap_udp_tx_params_t fwu_tx_params;
#endif // ANJAY_WITH_MODULE_FW_UPDATE
#ifdef ANJAY_WITH_LWM2M11
    anjay_lwm2m_version_config_t lwm2m_version_config;
#endif // ANJAY_WITH_LWM2M11
    size_t stored_notification_limit;

    bool prefer_hierarchical_formats;
    bool use_connection_id;

    uint32_t *default_ciphersuites;
    size_t default_ciphersuites_count;
    bool prefer_same_socket_downloads;
#ifdef ANJAY_WITH_SEND
    bool fw_update_use_send;
#endif // ANJAY_WITH_SEND
#ifdef ANJAY_WITH_LWM2M11
    const char *pkix_trust_store;
    bool rebuild_client_cert_chain;
#endif // ANJAY_WITH_LWM2M11

    bool alternative_logger;

} cmdline_args_t;

int demo_parse_argv(cmdline_args_t *parsed_args, int argc, char **argv);

#endif
