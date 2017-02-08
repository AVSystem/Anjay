/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
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

#include <unistd.h>

#include "../objects.h"
#include "../utils.h"

#define EXT_DEV_RES_OBU_ID 0             // string
#define EXT_DEV_RES_PLATE_NUMBER 1       // string
#define EXT_DEV_RES_IMEI 2               // string
#define EXT_DEV_RES_IMSI 3               // string
#define EXT_DEV_RES_ICCID 4              // string
#define EXT_DEV_RES_GPRS_RSSI 5          // int
#define EXT_DEV_RES_GPRS_PLMN 6          // int
#define EXT_DEV_RES_GPRS_ULMODULATION 7  // string
#define EXT_DEV_RES_GPRS_DLMODULATION 8  // string
#define EXT_DEV_RES_GPRS_ULFREQUENCY 9   // int
#define EXT_DEV_RES_GPRS_DLFREQUENCY 10  // int

#define EXT_DEV_RID_BOUND_ 11

typedef struct {
    const anjay_dm_object_def_t *def;
} extdev_repr_t;

static inline extdev_repr_t *get_extdev(const anjay_dm_object_def_t *const *obj_ptr) {
    if (!obj_ptr) {
        return NULL;
    }
    return container_of(obj_ptr, extdev_repr_t, def);
}

static int dev_resource_supported(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr,
                                  anjay_rid_t rid) {
    (void) anjay; (void) obj_ptr;
    switch (rid) {
    case EXT_DEV_RES_OBU_ID:
    case EXT_DEV_RES_PLATE_NUMBER:
    case EXT_DEV_RES_IMEI:
    case EXT_DEV_RES_IMSI:
    case EXT_DEV_RES_ICCID:
    case EXT_DEV_RES_GPRS_RSSI:
    case EXT_DEV_RES_GPRS_PLMN:
    case EXT_DEV_RES_GPRS_ULMODULATION:
    case EXT_DEV_RES_GPRS_DLMODULATION:
    case EXT_DEV_RES_GPRS_ULFREQUENCY:
    case EXT_DEV_RES_GPRS_DLFREQUENCY:
        return 1;
    default:
        return 0;
    }
}

static int dev_resource_present(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr,
                                anjay_iid_t iid,
                                anjay_rid_t rid) {
    (void) iid;
    return dev_resource_supported(anjay, obj_ptr, rid);
}

static int generate_fake_rssi_value(void) {
    return 50 + (int) (time_to_rand() % 20);
}

static int dev_read(anjay_t *anjay,
                    const anjay_dm_object_def_t *const *obj_ptr,
                    anjay_iid_t iid,
                    anjay_rid_t rid,
                    anjay_output_ctx_t *ctx) {
    (void) anjay; (void) obj_ptr; (void) iid;

    switch (rid) {
    case EXT_DEV_RES_OBU_ID:
        return anjay_ret_string(ctx, "Dummy_OBU_ID");
    case EXT_DEV_RES_PLATE_NUMBER:
        return anjay_ret_string(ctx, "PL 473N0");
    case EXT_DEV_RES_IMEI:
        return anjay_ret_string(ctx, "01-345678-901234");
    case EXT_DEV_RES_IMSI:
        return anjay_ret_string(ctx, "26000007");
    case EXT_DEV_RES_ICCID:
        return anjay_ret_string(ctx, "8926000000073");
    case EXT_DEV_RES_GPRS_RSSI:
        return anjay_ret_i32(ctx, generate_fake_rssi_value());
    case EXT_DEV_RES_GPRS_PLMN:
        return anjay_ret_i32(ctx, 26001);
    case EXT_DEV_RES_GPRS_ULMODULATION:
        return anjay_ret_string(ctx, "GMSK");
    case EXT_DEV_RES_GPRS_DLMODULATION:
        return anjay_ret_string(ctx, "GMSK");
    case EXT_DEV_RES_GPRS_ULFREQUENCY:
        return anjay_ret_i32(ctx, 1950);
    case EXT_DEV_RES_GPRS_DLFREQUENCY:
        return anjay_ret_i32(ctx, 2140);
    default:
        return ANJAY_ERR_NOT_FOUND;
    }
}

static const anjay_dm_object_def_t EXT_DEV_INFO = {
    .oid = EXT_DEV_INFO_OID,
    .rid_bound = EXT_DEV_RID_BOUND_,
    .instance_it = anjay_dm_instance_it_SINGLE,
    .instance_present = anjay_dm_instance_present_SINGLE,
    .resource_present = dev_resource_present,
    .resource_supported = dev_resource_supported,
    .resource_read = dev_read
};

const anjay_dm_object_def_t **ext_dev_info_object_create(void) {
    extdev_repr_t *repr = (extdev_repr_t*)calloc(1, sizeof(extdev_repr_t));
    if (!repr) {
        return NULL;
    }

    repr->def = &EXT_DEV_INFO;

    return &repr->def;
}

void ext_dev_info_object_release(const anjay_dm_object_def_t **def) {
    free(get_extdev(def));
}

void ext_dev_info_notify_time_dependent(anjay_t *anjay,
                                        const anjay_dm_object_def_t **def) {
    anjay_notify_changed(anjay, (*def)->oid, 0, EXT_DEV_RES_GPRS_RSSI);
}
