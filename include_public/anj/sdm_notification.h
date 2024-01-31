/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef SDM_NOTIFICATION_H
#define SDM_NOTIFICATION_H

#include <anj/sdm.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Processes certain incoming operations related to notifications and their
 * attributes (Observe Operation, Cancel Observation, Write-Attribute). Supports
 * only resource observations and pmin, pmax attributes.
 *
 * @param in_out_msg
 * @param dm
 * @param out_buff
 * @param out_buff_len
 * @return int
 */
int sdm_notification(fluf_data_t *in_out_msg,
                     sdm_data_model_t *dm,
                     char *out_buff,
                     size_t out_buff_len);

/**
 * Check if any notification should be sent.
 * Prepares only one notification per call even if more notifications should be
 * sent. This function should be called periodically.
 *
 * @param out_msg
 * @param dm
 * @param out_buff
 * @param out_buff_len
 * @param format
 * @return int
 */
int sdm_notification_process(fluf_data_t *out_msg,
                             sdm_data_model_t *dm,
                             char *out_buff,
                             size_t out_buff_len,
                             uint16_t format);

#ifdef __cplusplus
}
#endif

#endif // SDM_NOTIFICATION_H
