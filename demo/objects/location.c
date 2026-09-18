/*
 * Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */

#if !defined(_POSIX_C_SOURCE) && !defined(__APPLE__)
#    define _POSIX_C_SOURCE 200809L
#endif

#include "../demo_utils.h"
#include "../objects.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <avsystem/commons/avs_utils.h>

#define LOCATION_LATITUDE 0
#define LOCATION_LONGITUDE 1
#define LOCATION_ALTITUDE 2
#define LOCATION_RADIUS 3
#define LOCATION_VELOCITY 4
#define LOCATION_TIMESTAMP 5
#define LOCATION_SPEED 6

typedef struct {
    const anjay_dm_object_def_t *def;
    time_t timestamp;
    unsigned rand_seed;
    double latitude;
    double longitude;
    bool location_values_forced;
} location_t;

static inline location_t *
get_location(const anjay_dm_object_def_t *const *obj_ptr) {
    assert(obj_ptr);
    return AVS_CONTAINER_OF(obj_ptr, location_t, def);
}

static void normalize_angle(double *value) {
    *value = fmod(fmod(*value + 180.0, 360.0) + 360.0, 360.0) - 180.0;
}

static void normalize_location(location_t *location) {
    // some extremely weird values, including non-finite ones,
    // may occur with coordinates close to the North or South Pole

    // longitude
    if (!isfinite(location->longitude)) {
        location->longitude = 0.0;
    }
    normalize_angle(&location->longitude);

    // latitude
    if (!isfinite(location->latitude)) {
        if (location->latitude < 0.0) {
            location->latitude = -90.0;
        } else {
            location->latitude = 90.0;
        }
    }
    normalize_angle(&location->latitude);
    if (location->latitude > 90.0) {
        location->latitude = 180.0 - location->latitude;
        location->longitude += 180.0;
        normalize_angle(&location->longitude);
    } else if (location->latitude < -90.0) {
        location->latitude = -180.0 - location->latitude;
        location->longitude += 180.0;
        normalize_angle(&location->longitude);
    }
}

static void get_meters_per_degree(double *out_m_per_deg_lat,
                                  double *out_m_per_deg_lon,
                                  double latitude) {
    double lat_rad = deg2rad(latitude);
    // The formulas come from
    // https://en.wikipedia.org/wiki/Geographic_coordinate_system#Expressing_latitude_and_longitude_as_linear_units
    // (retrieved 2016-01-12)
    *out_m_per_deg_lat = 111132.92 - 559.82 * cos(2.0 * lat_rad)
                         + 1.175 * cos(4.0 * lat_rad)
                         - 0.0023 * cos(6.0 * lat_rad);
    *out_m_per_deg_lon = 111412.84 * cos(lat_rad) - 93.5 * cos(3.0 * lat_rad)
                         - 0.118 * cos(5.0 * lat_rad);
}

static double rand_double(unsigned *seed, double min, double max) {
    return min + (max - min) * avs_rand_r(seed) / (double) AVS_RAND_MAX;
}

static int update_location_random(location_t *location) {
    double m_per_deg_lat, m_per_deg_lon;
    get_meters_per_degree(&m_per_deg_lat, &m_per_deg_lon, location->latitude);

    // random movement of at most 1 m in each direction
    double lat_change = rand_double(&location->rand_seed, -1.0 / m_per_deg_lat,
                                    1.0 / m_per_deg_lat);
    double lon_change = rand_double(&location->rand_seed, -1.0 / m_per_deg_lon,
                                    1.0 / m_per_deg_lon);
    location->latitude += lat_change;
    location->longitude += lon_change;
    normalize_location(location);

    return 1;
}

static int location_list_resources(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_iid_t iid,
                                   anjay_dm_resource_list_ctx_t *ctx) {
    (void) anjay;
    (void) obj_ptr;
    (void) iid;

    anjay_dm_emit_res(ctx, LOCATION_LATITUDE, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, LOCATION_LONGITUDE, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, LOCATION_ALTITUDE, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, LOCATION_RADIUS, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, LOCATION_VELOCITY, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, LOCATION_TIMESTAMP, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    return 0;
}

static int location_resource_read(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr,
                                  anjay_iid_t iid,
                                  anjay_rid_t rid,
                                  anjay_riid_t riid,
                                  anjay_output_ctx_t *ctx) {
    (void) anjay;
    (void) obj_ptr;
    (void) iid;
    (void) riid;
    assert(riid == ANJAY_ID_INVALID);
    location_t *location = get_location(obj_ptr);

    switch (rid) {
    case LOCATION_LATITUDE:
        return anjay_ret_double(ctx, location->latitude);
    case LOCATION_LONGITUDE:
        return anjay_ret_double(ctx, location->longitude);
    case LOCATION_ALTITUDE:
        return anjay_ret_double(ctx, 0.0);
    case LOCATION_RADIUS:
        return anjay_ret_double(ctx, 0.0);
    case LOCATION_VELOCITY:
        return anjay_ret_bytes(ctx, NULL, 0);
    case LOCATION_TIMESTAMP:
        return anjay_ret_i64(ctx, location->timestamp);
    default:
        AVS_UNREACHABLE("Read called on unknown resource");
        return ANJAY_ERR_NOT_FOUND;
    }
}

static const anjay_dm_object_def_t LOCATION = {
    .oid = DEMO_OID_LOCATION,
    .handlers = {
        .list_instances = anjay_dm_list_instances_SINGLE,
        .list_resources = location_list_resources,
        .resource_read = location_resource_read
    }
};

const anjay_dm_object_def_t **location_object_create(
        double latitude, double longitude, bool location_values_provided) {
    location_t *repr = (location_t *) avs_calloc(1, sizeof(location_t));
    if (!repr) {
        return NULL;
    }

    repr->def = &LOCATION;
    repr->timestamp = avs_time_real_now().since_real_epoch.seconds;
    repr->rand_seed = (unsigned) repr->timestamp;
    repr->latitude = latitude;
    repr->longitude = longitude;
    repr->location_values_forced = location_values_provided;

    return &repr->def;
}

void location_object_release(const anjay_dm_object_def_t **def) {
    if (def) {
        avs_free(get_location(def));
    }
}

void location_notify_time_dependent(anjay_t *anjay,
                                    const anjay_dm_object_def_t **def) {
    location_t *repr = get_location(def);
    if (repr->location_values_forced) {
        return;
    }

    time_t current_time = time(NULL);
    if (current_time != repr->timestamp) {
        bool updated = false;
        do {
            updated = (update_location_random(repr) || updated);
        } while (++repr->timestamp < current_time);
        if (updated) {
            anjay_notify_changed(anjay, (*def)->oid, 0, LOCATION_LATITUDE);
            anjay_notify_changed(anjay, (*def)->oid, 0, LOCATION_LONGITUDE);
            anjay_notify_changed(anjay, (*def)->oid, 0, LOCATION_VELOCITY);
            anjay_notify_changed(anjay, (*def)->oid, 0, LOCATION_TIMESTAMP);
        }
    }
}

void location_get(const anjay_dm_object_def_t **def,
                  double *out_latitude,
                  double *out_longitude) {
    location_t *repr = get_location(def);
    *out_latitude = repr->latitude;
    *out_longitude = repr->longitude;
}
