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

#include <math.h>
#include <stdbool.h>
#include <string.h>

#include "../objects.h"
#include "../utils.h"

#define GEOPOINTS_LATITUDE    0 // double, degrees
#define GEOPOINTS_LONGITUDE   1 // double, degrees
#define GEOPOINTS_RADIUS      2 // double, meters
#define GEOPOINTS_DESCRIPTION 3 // string
#define GEOPOINTS_INSIDE      4 // bool

#define GEOPOINTS_RID_BOUND_  5

typedef struct {
    anjay_iid_t iid;

    double latitude;
    double longitude;
    double radius_m;
    char description[1024];
    bool inside;

    bool has_latitude;
    bool has_longitude;
    bool has_radius_m;
} geopoint_t;

typedef struct {
    const anjay_dm_object_def_t *def;
    const anjay_dm_object_def_t **location_obj_ptr;
    AVS_LIST(geopoint_t) instances;
    AVS_LIST(geopoint_t) saved_instances;
} geopoints_t;

static inline geopoints_t *
get_geopoints(const anjay_dm_object_def_t *const *obj_ptr) {
    if (!obj_ptr) {
        return NULL;
    }
    return container_of(obj_ptr, geopoints_t, def);
}

static geopoint_t *find_instance(const geopoints_t *repr,
                                 anjay_iid_t iid) {
    AVS_LIST(geopoint_t) it;
    AVS_LIST_FOREACH(it, repr->instances) {
        if (it->iid == iid) {
            return it;
        } else if (it->iid > iid) {
            break;
        }
    }

    return NULL;
}

static int geopoints_instance_it(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj_ptr,
                                 anjay_iid_t *out,
                                 void **cookie) {
    (void)anjay;

    AVS_LIST(geopoint_t) inst = (AVS_LIST(geopoint_t)) *cookie;

    if (!inst) {
        inst = get_geopoints(obj_ptr)->instances;
    } else {
        inst = AVS_LIST_NEXT(inst);
    }

    *out = inst ? inst->iid : ANJAY_IID_INVALID;
    *cookie = inst;
    return 0;
}

static int geopoints_instance_present(anjay_t *anjay,
                                      const anjay_dm_object_def_t *const *obj_ptr,
                                      anjay_iid_t iid) {
    (void)anjay;
    return find_instance(get_geopoints(obj_ptr), iid) != NULL;
}

static anjay_iid_t get_new_iid(AVS_LIST(geopoint_t) instances) {
    anjay_iid_t iid = 1;
    AVS_LIST(geopoint_t) it;
    AVS_LIST_FOREACH(it, instances) {
        if (it->iid == iid) {
            ++iid;
        } else if (it->iid > iid) {
            break;
        }
    }
    return iid;
}

static int geopoints_instance_create(anjay_t *anjay,
                                     const anjay_dm_object_def_t *const *obj_ptr,
                                     anjay_iid_t *inout_iid,
                                     anjay_ssid_t ssid) {
    (void) anjay;
    (void) ssid;
    geopoints_t *repr = get_geopoints(obj_ptr);

    AVS_LIST(geopoint_t) created = AVS_LIST_NEW_ELEMENT(geopoint_t);
    if (!created) {
        return ANJAY_ERR_INTERNAL;
    }

    if (*inout_iid == ANJAY_IID_INVALID) {
        *inout_iid = get_new_iid(repr->instances);
        if (*inout_iid == ANJAY_IID_INVALID) {
            AVS_LIST_CLEAR(&created);
            return ANJAY_ERR_INTERNAL;
        }
    }

    created->iid = *inout_iid;

    AVS_LIST(geopoint_t) *ptr;
    AVS_LIST_FOREACH_PTR(ptr, &repr->instances) {
        if ((*ptr)->iid > created->iid) {
            break;
        }
    }

    AVS_LIST_INSERT(ptr, created);
    return 0;
}

static int geopoints_instance_remove(anjay_t *anjay,
                                     const anjay_dm_object_def_t *const *obj_ptr,
                                     anjay_iid_t iid) {
    (void)anjay;
    geopoints_t *repr = get_geopoints(obj_ptr);

    AVS_LIST(geopoint_t) *it;
    AVS_LIST_FOREACH_PTR(it, &repr->instances) {
        if ((*it)->iid == iid) {
            AVS_LIST_DELETE(it);
            return 0;
        }
    }

    return ANJAY_ERR_NOT_FOUND;
}

static int geopoints_resource_read(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_iid_t iid,
                                   anjay_rid_t rid,
                                   anjay_output_ctx_t *ctx) {
    (void) anjay;

    geopoint_t *inst = find_instance(get_geopoints(obj_ptr), iid);
    if (!inst) {
        return ANJAY_ERR_NOT_FOUND;
    }

    switch (rid) {
    case GEOPOINTS_LATITUDE:
        return anjay_ret_double(ctx, inst->latitude);
    case GEOPOINTS_LONGITUDE:
        return anjay_ret_double(ctx, inst->longitude);
    case GEOPOINTS_RADIUS:
        return anjay_ret_double(ctx, inst->radius_m);
    case GEOPOINTS_DESCRIPTION:
        return anjay_ret_string(ctx, inst->description);
    case GEOPOINTS_INSIDE:
        return anjay_ret_bool(ctx, inst->inside);
    default:
        return ANJAY_ERR_NOT_FOUND;
    }
}

static int geopoints_resource_write(anjay_t *anjay,
                                    const anjay_dm_object_def_t *const *obj_ptr,
                                    anjay_iid_t iid,
                                    anjay_rid_t rid,
                                    anjay_input_ctx_t *ctx) {
    (void) anjay;

    geopoint_t *inst = find_instance(get_geopoints(obj_ptr), iid);
    if (!inst) {
        return ANJAY_ERR_NOT_FOUND;
    }

    double value;
    int result;

    switch (rid) {
    case GEOPOINTS_LATITUDE:
        result = anjay_get_double(ctx, &value);
        if (result) {
            return result;
        } else if (!latitude_valid(value)) {
            return ANJAY_ERR_BAD_REQUEST;
        }
        inst->latitude = value;
        inst->has_latitude = true;
        return 0;
    case GEOPOINTS_LONGITUDE:
        result = anjay_get_double(ctx, &value);
        if (result) {
            return result;
        } else if (!longitude_valid(value)) {
            return ANJAY_ERR_BAD_REQUEST;
        }
        inst->longitude = value;
        inst->has_longitude = true;
        return 0;
    case GEOPOINTS_RADIUS:
        result = anjay_get_double(ctx, &value);
        if (result) {
            return result;
        } else if (!isfinite(value) || value < 0.0) {
            return ANJAY_ERR_BAD_REQUEST;
        }
        inst->radius_m = value;
        inst->has_radius_m = true;
        return 0;
    case GEOPOINTS_DESCRIPTION:
    {
        char buf[sizeof(inst->description)];
        result = anjay_get_string(ctx, buf, sizeof(buf));
        if (result) {
            return result;
        } else {
            strcpy(inst->description, buf);
            return 0;
        }
    }
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int
geopoints_transaction_begin(anjay_t *anjay,
                            const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    geopoints_t *repr = get_geopoints(obj_ptr);
    if (!repr->instances) {
        return 0;
    }
    repr->saved_instances = AVS_LIST_SIMPLE_CLONE(repr->instances);
    if (!repr->saved_instances) {
        demo_log(ERROR, "cannot clone Geopoint instances");
        return ANJAY_ERR_INTERNAL;
    }
    return 0;
}

static int
geopoints_transaction_validate(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    AVS_LIST(geopoint_t) it;
    AVS_LIST_FOREACH(it, get_geopoints(obj_ptr)->instances) {
        if (!it->has_latitude || !it->has_longitude || !it->has_radius_m) {
            return ANJAY_ERR_BAD_REQUEST;
        }
    }
    return 0;
}

static int
geopoints_transaction_commit(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    AVS_LIST_CLEAR(&get_geopoints(obj_ptr)->saved_instances);
    return 0;
}

static int
geopoints_transaction_rollback(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    geopoints_t *repr = get_geopoints(obj_ptr);
    AVS_LIST_CLEAR(&repr->instances);
    repr->instances = repr->saved_instances;
    repr->saved_instances = NULL;
    return 0;
}

static int
geopoints_instance_reset(anjay_t *anjay,
                         const anjay_dm_object_def_t *const *obj_ptr,
                         anjay_iid_t iid) {
    (void) anjay;
    geopoint_t *inst = find_instance(get_geopoints(obj_ptr), iid);
    memset(inst, 0, sizeof(geopoint_t));
    inst->iid = iid;
    return 0;
}

static const anjay_dm_object_def_t GEOPOINTS = {
    .oid = 12360,
    .rid_bound = GEOPOINTS_RID_BOUND_,
    .instance_it = geopoints_instance_it,
    .instance_present = geopoints_instance_present,
    .instance_create = geopoints_instance_create,
    .instance_remove = geopoints_instance_remove,
    .instance_reset = geopoints_instance_reset,
    .resource_present = anjay_dm_resource_present_TRUE,
    .resource_supported = anjay_dm_resource_supported_TRUE,
    .resource_read = geopoints_resource_read,
    .resource_write = geopoints_resource_write,
    .transaction_begin = geopoints_transaction_begin,
    .transaction_validate = geopoints_transaction_validate,
    .transaction_commit = geopoints_transaction_commit,
    .transaction_rollback = geopoints_transaction_rollback
};

const anjay_dm_object_def_t **
geopoints_object_create(const anjay_dm_object_def_t **location_obj_ptr) {
    geopoints_t *repr = (geopoints_t *) calloc(1, sizeof(geopoints_t));
    if (!repr) {
        return NULL;
    }

    repr->def = &GEOPOINTS;
    repr->location_obj_ptr = location_obj_ptr;

    return &repr->def;
}

void geopoints_object_release(const anjay_dm_object_def_t **def) {
    geopoints_t *geopoints = get_geopoints(def);
    AVS_LIST_CLEAR(&geopoints->instances);
    AVS_LIST_CLEAR(&geopoints->saved_instances);
    free(geopoints);
}

void geopoints_notify_time_dependent(anjay_t *anjay,
                                     const anjay_dm_object_def_t **def) {
    geopoints_t *geopoints = get_geopoints(def);

    double latitude, longitude;
    location_get(geopoints->location_obj_ptr, &latitude, &longitude);

    AVS_LIST(geopoint_t) point;
    AVS_LIST_FOREACH(point, geopoints->instances) {
        bool inside = (geo_distance_m(latitude, longitude,
                                      point->latitude,
                                      point->longitude) < point->radius_m);
        if (inside != point->inside) {
            point->inside = inside;
            anjay_notify_changed(anjay,
                                 (*def)->oid, point->iid, GEOPOINTS_INSIDE);
        }
    }
}
