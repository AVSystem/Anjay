/*
 * Copyright 2017-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include "advanced_firmware_update.h"
#include "demo_utils.h"

#include <errno.h>
#include <unistd.h>

#define RESTART_DELAY_SEC 3

static advanced_fw_update_logic_t *fw_global;

static int fw_stream_open(anjay_iid_t iid, void *fw_) {
    (void) iid;
    return fw_update_common_open(iid, fw_);
}

static int prepare_and_validate_update(advanced_fw_update_logic_t *fw) {
    demo_log(INFO,
             "Checking image of " AVS_QUOTE_MACRO(
                     ANJAY_ADVANCED_FW_UPDATE_OID) "/%u instance",
             fw->iid);
    if (fw->metadata.force_error_case) {
        demo_log(INFO, "force_error_case present and set to: %d",
                 (int) fw->metadata.force_error_case);
    }
    if (fw->metadata.force_error_case == FORCE_ERROR_FAILED_UPDATE) {
        demo_log(ERROR, "Image check failure");
        advanced_firmware_update_delete_persistence_file(fw);
        anjay_advanced_fw_update_set_state_and_result(
                fw->anjay, fw->iid, ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADED,
                ANJAY_ADVANCED_FW_UPDATE_RESULT_FAILED);
        return -1;
    }
    demo_log(ERROR, "Image check success");
    return 0;
}

static int write_persistence(advanced_fw_update_logic_t *fw_table) {
    advanced_fw_update_logic_t *fw =
            (advanced_fw_update_logic_t *) &fw_table[FW_UPDATE_IID_APP];
    states_results_paths_t states_results_paths;
    if (advanced_firmware_update_read_states_results_paths(
                fw_table, &states_results_paths)) {
        demo_log(ERROR, "Can't read states/results/paths.");
        return -1;
    }
    states_results_paths.inst_states[FW_UPDATE_IID_APP] =
            ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE;
    states_results_paths.inst_results[FW_UPDATE_IID_APP] =
            ANJAY_ADVANCED_FW_UPDATE_RESULT_SUCCESS;
    if (fw->persistence_file
            && advanced_firmware_update_write_persistence_file(
                       fw->persistence_file, &states_results_paths,
                       anjay_advanced_fw_update_get_severity(fw->anjay,
                                                             fw->iid),
                       anjay_advanced_fw_update_get_last_state_change_time(
                               fw->anjay, fw->iid),
                       anjay_advanced_fw_update_get_deadline(fw->anjay,
                                                             fw->iid),
                       fw->current_ver)) {
        advanced_firmware_update_delete_persistence_file(fw);
        demo_log(ERROR, "Can't write persistence file.");
        return -1;
    }
    return 0;
}

struct execute_new_app_args {
    advanced_fw_update_logic_t *fw_table;
};

static void execute_new_app(avs_sched_t *sched, const void *args_) {
    (void) sched;
    const struct execute_new_app_args *args =
            (const struct execute_new_app_args *) args_;
    advanced_fw_update_logic_t *fw =
            (advanced_fw_update_logic_t *) &args->fw_table[FW_UPDATE_IID_APP];
    if (write_persistence(args->fw_table)) {
        demo_log(ERROR, "Can't persist state. Execute new app failed.");
        return;
    }
    demo_log(INFO, "App image going to execv from %s", fw->next_target_path);
    execv(fw->next_target_path, argv_get());
    demo_log(ERROR, "execv failed (%s)", strerror(errno));
}

static int update(advanced_fw_update_logic_t *fw) {
    /* This function only works with APP so fw always points to fw_table: */
    advanced_fw_update_logic_t *fw_table = (advanced_fw_update_logic_t *) fw;
    demo_log(INFO, "*** FIRMWARE UPDATE: %s ***", fw->next_target_path);
    if (fw->metadata.force_error_case) {
        demo_log(INFO, "force_error_case present and set to: %d",
                 (int) fw->metadata.force_error_case);
        if (write_persistence(fw_table)) {
            demo_log(ERROR, "Can't persist state. Update failed.");
            return -1;
        }
    }
    switch (fw->metadata.force_error_case) {
    case FORCE_ERROR_FAILED_UPDATE:
        AVS_UNREACHABLE("Update process should fail earlier");
    case FORCE_DELAYED_SUCCESS:
        if (argv_append("--delayed-afu-result") || argv_append("1")) {
            demo_log(ERROR, "could not append delayed result to argv");
            return -1;
        }
        break;
    case FORCE_DELAYED_ERROR_FAILED_UPDATE:
        if (argv_append("--delayed-afu-result") || argv_append("8")) {
            demo_log(ERROR, "could not append delayed result to argv");
            return -1;
        }
        break;
    case FORCE_SET_SUCCESS_FROM_PERFORM_UPGRADE:
        if (anjay_advanced_fw_update_set_state_and_result(
                    fw->anjay, fw->iid, ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE,
                    ANJAY_ADVANCED_FW_UPDATE_RESULT_SUCCESS)) {
            demo_log(ERROR,
                     "anjay_advanced_fw_update_set_state_and_result failed");
            return -1;
        }
        return 0;
    case FORCE_SET_FAILURE_FROM_PERFORM_UPGRADE:
        if (anjay_advanced_fw_update_set_state_and_result(
                    fw->anjay, fw->iid, ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE,
                    ANJAY_ADVANCED_FW_UPDATE_RESULT_FAILED)) {
            demo_log(ERROR, "anjay_fw_update_set_result failed");
            return -1;
        }
        return 0;
    case FORCE_DO_NOTHING:
        return 0;
    case FORCE_DEFER:
        return ANJAY_ADVANCED_FW_UPDATE_ERR_DEFERRED;
    default:
        break;
    }
    struct execute_new_app_args args = {
        .fw_table = fw_table,
    };
    if (AVS_SCHED_DELAYED(anjay_get_scheduler(fw->anjay), &fw->update_job,
                          avs_time_duration_from_scalar(RESTART_DELAY_SEC,
                                                        AVS_TIME_S),
                          execute_new_app, &args, sizeof(args))) {
        demo_log(WARNING, "Could not schedule the upgrade job");
        return ANJAY_ERR_INTERNAL;
    }
    return 0;
}

static avs_coap_udp_tx_params_t fw_get_coap_tx_params(
        anjay_iid_t iid, void *user_ptr, const char *download_uri) {
    (void) iid;
    (void) download_uri;
    advanced_fw_update_logic_t *fw_table =
            (advanced_fw_update_logic_t *) user_ptr;
    advanced_fw_update_logic_t *fw = &fw_table[FW_UPDATE_IID_APP];
    if (fw->auto_suspend) {
        anjay_advanced_fw_update_pull_reconnect(fw->anjay);
    }
    return fw->coap_tx_params;
}

static avs_time_duration_t fw_get_tcp_request_timeout(
        anjay_iid_t iid, void *user_ptr, const char *download_uri) {
    (void) iid;
    (void) download_uri;
    advanced_fw_update_logic_t *fw_table =
            (advanced_fw_update_logic_t *) user_ptr;
    advanced_fw_update_logic_t *fw = &fw_table[FW_UPDATE_IID_APP];
    return fw->tcp_request_timeout;
}

static anjay_advanced_fw_update_handlers_t handlers = {
    .stream_open = fw_stream_open,
    .stream_write = fw_update_common_write,
    .stream_finish = fw_update_common_finish,
    .reset = fw_update_common_reset,
    .get_pkg_version = fw_update_common_get_pkg_version,
    .get_current_version = fw_update_common_get_current_version,
    .perform_upgrade = fw_update_common_perform_upgrade
};

static int fw_get_security_config(anjay_iid_t iid,
                                  void *fw_,
                                  anjay_security_config_t *out_security_config,
                                  const char *download_uri) {
    (void) iid;
    advanced_fw_update_logic_t *fw_table = (advanced_fw_update_logic_t *) fw_;
    advanced_fw_update_logic_t *fw = &fw_table[FW_UPDATE_IID_APP];
    (void) download_uri;
    memset(out_security_config, 0, sizeof(*out_security_config));
    out_security_config->security_info = fw->security_info;
    return 0;
}

int advanced_firmware_update_application_install(
        anjay_t *anjay,
        advanced_fw_update_logic_t *fw_table,
        anjay_advanced_fw_update_initial_state_t *init_state,
        const avs_net_security_info_t *security_info,
        const avs_coap_udp_tx_params_t *tx_params,
        avs_time_duration_t tcp_request_timeout,
        bool auto_suspend) {
    advanced_fw_update_logic_t *fw_logic = &fw_table[FW_UPDATE_IID_APP];

    if (security_info) {
        memcpy(&fw_logic->security_info, security_info,
               sizeof(fw_logic->security_info));
        handlers.get_security_config = fw_get_security_config;
    } else {
        handlers.get_security_config = NULL;
    }

    if (tx_params || auto_suspend) {
        if (tx_params) {
            fw_logic->coap_tx_params = *tx_params;
        } else if (auto_suspend) {
            fw_logic->coap_tx_params = AVS_COAP_DEFAULT_UDP_TX_PARAMS;
        }
        fw_logic->auto_suspend = auto_suspend;
        handlers.get_coap_tx_params = fw_get_coap_tx_params;
    } else {
        handlers.get_coap_tx_params = NULL;
    }

    if (avs_time_duration_valid(tcp_request_timeout)) {
        fw_logic->tcp_request_timeout = tcp_request_timeout;
        handlers.get_tcp_request_timeout = fw_get_tcp_request_timeout;
    } else {
        handlers.get_tcp_request_timeout = NULL;
    }

    fw_global = fw_logic;
    int result = anjay_advanced_fw_update_instance_add(anjay,
                                                       fw_logic->iid,
                                                       "application",
                                                       &handlers,
                                                       fw_table,
                                                       init_state);
    if (!result) {
        fw_logic->check_yourself = prepare_and_validate_update;
        fw_logic->update_yourself = update;
    }
    if (result) {
        memset(fw_global, 0x00, sizeof(advanced_fw_update_logic_t));
    }
    return result;
}
