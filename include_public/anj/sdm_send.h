/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef SDM_SEND_H
#define SDM_SEND_H

#include <anj/anj_config.h>

#include <fluf/fluf_io.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Creates a Send message based on the current state of the data model and
 * list of paths to resources.
 *
 * @param      dm         Data model to operate on.
 * @param      format     Format in which the message will created. Currently
 *                        only @p FLUF_COAP_FORMAT_SENML_CBOR is supported.
 * @param      timestamp  Timestamp value in seconds attached to the Send
 *                        message.
 * @param[out] out_buff   Pointer to a buffer to which the message will be
 *                        written.
 * @param[out] inout_size Size of the buffer as input, writen bytes as output.
 * @param      paths      Array of paths to resources.
 * @param      path_cnt   Number of paths stored in the @p paths array.
 *
 * @returns 0 on success, a non zero value in case of error.
 */
int sdm_send_create_msg_from_dm(sdm_data_model_t *dm,
                                uint16_t format,
                                double timestamp,
                                uint8_t *out_buff,
                                size_t *inout_size,
                                const fluf_uri_path_t *paths,
                                const size_t path_cnt);

/**
 * Creates a Send message based on a list of records passed to the function.
 *
 * @param      format     Format in which the message will created. Currently
 *                        only @p FLUF_COAP_FORMAT_SENML_CBOR is supported.
 * @param[out] out_buff   Pointer to a buffer to which the message will be
 *                        written.
 * @param[out] inout_size Size of the buffer as input, writen bytes as output.
 * @param      records    Array of records of resource values.
 * @param      path_cnt   Number of records stored in the @p records array.
 *
 * @returns 0 on success, a non zero value in case of error.
 */
int sdm_send_create_msg_from_list_of_records(uint16_t format,
                                             uint8_t *out_buff,
                                             size_t *inout_size,
                                             const fluf_io_out_entry_t *records,
                                             const size_t record_cnt);

#ifdef __cplusplus
}
#endif

#endif // SDM_SEND_H
