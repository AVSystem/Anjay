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

#include "../demo.h"
#include "../demo_utils.h"
#include "../objects.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>

#define GEOPOINTS_LATITUDE 0    // double, degrees
#define GEOPOINTS_LONGITUDE 1   // double, degrees
#define GEOPOINTS_RADIUS 2      // double, meters
#define GEOPOINTS_DESCRIPTION 3 // string
#define GEOPOINTS_INSIDE 4      // bool

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
    anjay_demo_t *demo;
    AVS_LIST(geopoint_t) instances;
    AVS_LIST(geopoint_t) saved_instances;
} geopoints_t;

static inline geopoints_t *
get_geopoints(const anjay_dm_object_def_t *const *obj_ptr) {
    assert(obj_ptr);
    return AVS_CONTAINER_OF(obj_ptr, geopoints_t, def);
}

static geopoint_t *find_instance(const geopoints_t *repr, anjay_iid_t iid) {
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

static int geopoints_list_instances(anjay_t *anjay,
                                    const anjay_dm_object_def_t *const *obj_ptr,
                                    anjay_dm_list_ctx_t *ctx) {
    (void) anjay;

    AVS_LIST(geopoint_t) it;
    AVS_LIST_FOREACH(it, get_geopoints(obj_ptr)->instances) {
        anjay_dm_emit(ctx, it->iid);
    }
    return 0;
}

static int
geopoints_instance_create(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_iid_t iid) {
    (void) anjay;
    geopoints_t *repr = get_geopoints(obj_ptr);

    AVS_LIST(geopoint_t) created = AVS_LIST_NEW_ELEMENT(geopoint_t);
    if (!created) {
        return ANJAY_ERR_INTERNAL;
    }

    created->iid = iid;

    AVS_LIST(geopoint_t) *ptr;
    AVS_LIST_FOREACH_PTR(ptr, &repr->instances) {
        if ((*ptr)->iid > created->iid) {
            break;
        }
    }

    AVS_LIST_INSERT(ptr, created);
    return 0;
}

static int
geopoints_instance_remove(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_iid_t iid) {
    (void) anjay;
    geopoints_t *repr = get_geopoints(obj_ptr);

    AVS_LIST(geopoint_t) *it;
    AVS_LIST_FOREACH_PTR(it, &repr->instances) {
        if ((*it)->iid == iid) {
            AVS_LIST_DELETE(it);
            return 0;
        }
    }

    assert(0);
    return ANJAY_ERR_NOT_FOUND;
}

static int geopoints_list_resources(anjay_t *anjay,
                                    const anjay_dm_object_def_t *const *obj_ptr,
                                    anjay_iid_t iid,
                                    anjay_dm_resource_list_ctx_t *ctx) {
    (void) anjay;
    (void) obj_ptr;
    (void) iid;

    anjay_dm_emit_res(ctx, GEOPOINTS_LATITUDE, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, GEOPOINTS_LONGITUDE, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, GEOPOINTS_RADIUS, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, GEOPOINTS_DESCRIPTION, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, GEOPOINTS_INSIDE, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    return 0;
}

static int geopoints_resource_read(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_iid_t iid,
                                   anjay_rid_t rid,
                                   anjay_riid_t riid,
                                   anjay_output_ctx_t *ctx) {
    (void) anjay;
    (void) riid;
    assert(riid == ANJAY_ID_INVALID);

    geopoint_t *inst = find_instance(get_geopoints(obj_ptr), iid);
    assert(inst);

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
        AVS_UNREACHABLE("Read called on unknown resource");
        return ANJAY_ERR_NOT_FOUND;
    }
}

static int geopoints_resource_write(anjay_t *anjay,
                                    const anjay_dm_object_def_t *const *obj_ptr,
                                    anjay_iid_t iid,
                                    anjay_rid_t rid,
                                    anjay_riid_t riid,
                                    anjay_input_ctx_t *ctx) {
    (void) anjay;
    (void) riid;
    assert(riid == ANJAY_ID_INVALID);

    geopoint_t *inst = find_instance(get_geopoints(obj_ptr), iid);
    assert(inst);

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
    case GEOPOINTS_DESCRIPTION: {
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
        // Bootstrap Server may try to write to GEOPOINTS_INSIDE,
        // so no AVS_UNREACHABLE() here
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

static int geopoints_instance_reset(anjay_t *anjay,
                                    const anjay_dm_object_def_t *const *obj_ptr,
                                    anjay_iid_t iid) {
    (void) anjay;
    geopoint_t *inst = find_instance(get_geopoints(obj_ptr), iid);
    AVS_ASSERT(inst, "could not find instance");
    memset(inst, 0, sizeof(geopoint_t));
    inst->iid = iid;
    return 0;
}

static const anjay_dm_object_def_t GEOPOINTS = {
    .oid = DEMO_OID_GEOPOINTS,
    .handlers = {
        .list_instances = geopoints_list_instances,
        .instance_create = geopoints_instance_create,
        .instance_remove = geopoints_instance_remove,
        .instance_reset = geopoints_instance_reset,
        .list_resources = geopoints_list_resources,
        .resource_read = geopoints_resource_read,
        .resource_write = geopoints_resource_write,
        .transaction_begin = geopoints_transaction_begin,
        .transaction_validate = geopoints_transaction_validate,
        .transaction_commit = geopoints_transaction_commit,
        .transaction_rollback = geopoints_transaction_rollback
    }
};

const anjay_dm_object_def_t **geopoints_object_create(anjay_demo_t *demo) {
    geopoints_t *repr = (geopoints_t *) avs_calloc(1, sizeof(geopoints_t));
    if (!repr) {
        return NULL;
    }

    repr->def = &GEOPOINTS;
    repr->demo = demo;

    return &repr->def;
}

void geopoints_object_release(const anjay_dm_object_def_t **def) {
    if (def) {
        geopoints_t *geopoints = get_geopoints(def);
        AVS_LIST_CLEAR(&geopoints->instances);
        AVS_LIST_CLEAR(&geopoints->saved_instances);
        avs_free(geopoints);
    }
}

int geopoints_get_instances(const anjay_dm_object_def_t **def,
                            AVS_LIST(anjay_iid_t) *out) {
    geopoints_t *geopoints = get_geopoints(def);
    assert(!*out);
    AVS_LIST(geopoint_t) it;
    AVS_LIST_FOREACH(it, geopoints->instances) {
        if (!(*out = AVS_LIST_NEW_ELEMENT(anjay_iid_t))) {
            demo_log(ERROR, "out of memory");
            return -1;
        }
        **out = it->iid;
        AVS_LIST_ADVANCE_PTR(&out);
    }
    return 0;
}

void geopoints_notify_time_dependent(anjay_t *anjay,
                                     const anjay_dm_object_def_t **def) {
    geopoints_t *geopoints = get_geopoints(def);

    const anjay_dm_object_def_t **location_obj_ptr =
            demo_find_object(geopoints->demo, DEMO_OID_LOCATION);
    if (!location_obj_ptr) {
        demo_log(ERROR, "Could not update geopoints, Location not installed");
        return;
    }

    double latitude, longitude;
    location_get(location_obj_ptr, &latitude, &longitude);

    AVS_LIST(geopoint_t) point;
    AVS_LIST_FOREACH(point, geopoints->instances) {
        bool inside = (geo_distance_m(latitude, longitude, point->latitude,
                                      point->longitude)
                       < point->radius_m);
        if (inside != point->inside) {
            point->inside = inside;
            anjay_notify_changed(anjay, (*def)->oid, point->iid,
                                 GEOPOINTS_INSIDE);
        }
    }
}
