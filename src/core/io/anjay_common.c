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
