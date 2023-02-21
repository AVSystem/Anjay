/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_IO_TLV_H
#define ANJAY_IO_TLV_H

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef enum {
    TLV_ID_IID = 0,
    TLV_ID_RIID = 1,
    TLV_ID_RID_ARRAY = 2,
    TLV_ID_RID = 3
} tlv_id_type_t;

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_IO_TLV_H */
