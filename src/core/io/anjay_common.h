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

#ifndef ANJAY_IO_COMMON_H
#define ANJAY_IO_COMMON_H

#include <anjay_modules/anjay_dm_utils.h>
#include <avsystem/commons/avs_utils.h>

#include "../anjay_io_core.h"

#include <assert.h>
#include <inttypes.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

#define MAX_PATH_STRING_SIZE sizeof("/65535/65535/65535/65535")
#define MAX_OBJLNK_STRING_SIZE sizeof("65535:65535")

/**
 * Enumeration for supported SenML labels. Their numeric values correspond to
 * their CBOR representation wherever possible.
 */
typedef enum {
    SENML_LABEL_BASE_TIME = -3,
    SENML_LABEL_BASE_NAME = -2,
    SENML_LABEL_NAME = 0,
    SENML_LABEL_VALUE = 2,
    SENML_LABEL_VALUE_STRING = 3,
    SENML_LABEL_VALUE_BOOL = 4,
    SENML_LABEL_TIME = 6,
    SENML_LABEL_VALUE_OPAQUE = 8,
    /* NOTE: Objlnk is represented as a string "vlo" */
    SENML_EXT_LABEL_OBJLNK = 0x766C6F /* "vlo */
} senml_label_t;

#define SENML_EXT_OBJLNK_REPR "vlo"

/**
 * Converts Object link from string format OID:IID to numeric OID and IID.
 *
 * @p out_oid and @p out_iid are modified only if this function succeeded.
 *
 * NOTE: @p objlnk may be modified by this function.
 */
int _anjay_io_parse_objlnk(char *objlnk,
                           anjay_oid_t *out_oid,
                           anjay_iid_t *out_iid);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_IO_COMMON_H */
