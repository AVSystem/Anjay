/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */
#ifndef ANJAY_INCLUDE_ANJAY_FACTORY_PROVISIONING_H
#define ANJAY_INCLUDE_ANJAY_FACTORY_PROVISIONING_H

#include <avsystem/commons/avs_stream.h>

#include <anjay/core.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Reads Bootstrap Information from the stream (@p data_stream) and initializes
 * Anjay's data model. Expected format of the stream data is SenML CBOR, as used
 * for a Write-Composite operation.

 *
 * @param anjay         Anjay Object to operate on.
 * @param data_stream   Bootstrap Information data stream.
 * @returns
 *  - @ref AVS_OK for success
 *  - <c>avs_errno(AVS_EBADMSG)</c> if Anjay failed to apply bootstrap
 information
 *  - <c>avs_errno(AVS_ENOMEM)</c> if Anjay failed to allocate memory
 *  - <c>avs_errno(AVS_EAGAIN)</c> if connection with Bootstrap Server is in
 progress
 *  - <c>avs_errno(AVS_EPROTO)</c> in case of other internal errors
 */
avs_error_t anjay_factory_provision(anjay_t *anjay, avs_stream_t *data_stream);

#ifdef __cplusplus
}
#endif

#endif /* ANJAY_INCLUDE_ANJAY_FACTORY_PROVISIONING_H */
