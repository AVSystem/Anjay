/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#include "anjay_common.h"

VISIBILITY_SOURCE_BEGIN

int _anjay_io_parse_objlnk(char *objlnk,
                           anjay_oid_t *out_oid,
                           anjay_iid_t *out_iid) {
    char *colon = strchr(objlnk, ':');
    if (!colon) {
        return -1;
    }
    *colon = '\0';
    long long oid;
    long long iid;
    if (_anjay_safe_strtoll(objlnk, &oid)
            || _anjay_safe_strtoll(colon + 1, &iid) || oid < 0
            || oid > UINT16_MAX || iid < 0 || iid > UINT16_MAX) {
        return -1;
    }
    *out_oid = (anjay_oid_t) oid;
    *out_iid = (anjay_iid_t) iid;
    return 0;
}
