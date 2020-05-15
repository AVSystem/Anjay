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
    double value_mps;
    double bearing_deg_cw_n;
} velocity_t;

typedef struct {
    const anjay_dm_object_def_t *def;
    time_t timestamp;
    unsigned rand_seed;
    double latitude;
    double longitude;
    velocity_t velocity;
    FILE *csv;
    time_t csv_frequency;
} location_t;

static inline location_t *
get_location(const anjay_dm_object_def_t *const *obj_ptr) {
    assert(obj_ptr);
    return AVS_CONTAINER_OF(obj_ptr, location_t, def);
}

static int ret_velocity(anjay_output_ctx_t *ctx, const velocity_t *velocity) {
    // see http://www.3gpp.org/DynaReport/23032.htm section 8 for details
    // we are using the "Horizontal Velocity" mode only
    uint8_t data[4];

    uint16_t bearing = (uint16_t) velocity->bearing_deg_cw_n;
    data[0] = (uint8_t) ((bearing >> 8) & 1);
    data[1] = (uint8_t) (bearing & UINT8_MAX);

    double value_kph = velocity->value_mps * 3.6;
    uint16_t value_kph_u16;
    if (!isfinite(value_kph)) {
        value_kph_u16 = 0;
    } else if (value_kph > UINT16_MAX) {
        value_kph_u16 = UINT16_MAX;
    } else {
        value_kph_u16 = (uint16_t) (value_kph + 0.5);
    }
    data[2] = (uint8_t) (value_kph_u16 >> 8);
    data[3] = (uint8_t) (value_kph_u16 & UINT8_MAX);

    return anjay_ret_bytes(ctx, data, sizeof(data));
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
        return ret_velocity(ctx, &location->velocity);
    case LOCATION_TIMESTAMP:
        return anjay_ret_i64(ctx, location->timestamp);
    default:
        AVS_UNREACHABLE("Read called on unknown resource");
        return ANJAY_ERR_NOT_FOUND;
    }
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

static void calculate_velocity(velocity_t *out,
                               double lat1,
                               double lon1,
                               double lat2,
                               double lon2,
                               double time_change_s) {
    out->value_mps = geo_distance_m(lat1, lon1, lat2, lon2) / time_change_s;

    double dlon = lon2 - lon1;
    normalize_angle(&dlon);
    double dlat = lat2 - lat1;
    normalize_angle(&dlat);
    out->bearing_deg_cw_n = rad2deg(atan2(dlon, dlat));
    if (!isfinite(out->bearing_deg_cw_n)) {
        out->bearing_deg_cw_n = 0.0;
    } else if (out->bearing_deg_cw_n < 0.0) {
        out->bearing_deg_cw_n += 360.0;
    }
}

static int update_location_random(location_t *location) {
    double m_per_deg_lat, m_per_deg_lon;
    get_meters_per_degree(&m_per_deg_lat, &m_per_deg_lon, location->latitude);

    // random movement of at most 1 m in each direction
    double lat_change = rand_double(&location->rand_seed, -1.0 / m_per_deg_lat,
                                    1.0 / m_per_deg_lat);
    double lon_change = rand_double(&location->rand_seed, -1.0 / m_per_deg_lon,
                                    1.0 / m_per_deg_lon);

    double old_lat = location->latitude;
    double old_lon = location->longitude;
    location->latitude += lat_change;
    location->longitude += lon_change;
    normalize_location(location);

    calculate_velocity(&location->velocity, old_lat, old_lon,
                       location->latitude, location->longitude, 1.0);
    return 1;
}

static int
try_parse_location_line(location_t *location, char *line, size_t line_length) {
    if (line_length > 1 && line[line_length - 1] == '\n') {
        line[--line_length] = '\0';
    }

    double latitude = 0.0, longitude = 0.0;
    double vel_mps = 0.0, vel_bearing_deg_cw_n = 0.0;
    int chars_read = 0;
    int scanf_result =
            sscanf(line, " %lf , %lf %n", &latitude, &longitude, &chars_read);
    if (scanf_result < 2 || !latitude_valid(latitude)
            || !longitude_valid(longitude)) {
        goto invalid;
    }

    if ((size_t) chars_read < line_length) {
        int more_chars_read;
        scanf_result = sscanf(line + chars_read, ", %lf , %lf %n", &vel_mps,
                              &vel_bearing_deg_cw_n, &more_chars_read);
        if (scanf_result < 2 || !velocity_mps_valid(vel_mps)
                || !velocity_bearing_deg_cw_n_valid(vel_bearing_deg_cw_n)
                || (size_t) (chars_read + more_chars_read) != line_length) {
            goto invalid;
        }
        location->velocity.value_mps = vel_mps;
        location->velocity.bearing_deg_cw_n = vel_bearing_deg_cw_n;
    } else {
        calculate_velocity(&location->velocity, location->latitude,
                           location->longitude, latitude, longitude,
                           (double) location->csv_frequency);
    }
    location->latitude = latitude;
    location->longitude = longitude;
    return 1;

invalid:
    demo_log(DEBUG, "Invalid CSV line, ignoring: %s", line);
    return 0;
}

static int update_location_csv(location_t *location) {
    if (!location->csv) {
        return -1;
    }

    if (location->timestamp % location->csv_frequency != 0) {
        return 0;
    }

    int result = 0;
    while (!feof(location->csv) && result == 0) {
        char line[256];
        if (fgets(line, sizeof(line), location->csv)) {
            size_t length = strlen(line);
            result = try_parse_location_line(location, line, length);
        } else if (ferror(location->csv)) {
            result = -1;
        }
    }

    if (result < 0) {
        demo_log(ERROR, "Could not read data from CSV, "
                        "switching back to random location mode");
        fclose(location->csv);
        location->csv = NULL;
    }
    return result;
}

static bool update_location(location_t *location) {
    int result = update_location_csv(location);
    if (result < 0) {
        result = update_location_random(location);
    }
    return result;
}

static const anjay_dm_object_def_t LOCATION = {
    .oid = DEMO_OID_LOCATION,
    .handlers = {
        .list_instances = anjay_dm_list_instances_SINGLE,
        .list_resources = location_list_resources,
        .resource_read = location_resource_read
    }
};

const anjay_dm_object_def_t **location_object_create(void) {
    location_t *repr = (location_t *) avs_calloc(1, sizeof(location_t));
    if (!repr) {
        return NULL;
    }

    repr->def = &LOCATION;
    repr->timestamp = avs_time_real_now().since_real_epoch.seconds;
    repr->rand_seed = (unsigned) repr->timestamp;

    // initial coordinates are of the AVSystem HQ
    repr->latitude = 50.083463;
    repr->longitude = 19.901325;

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
    time_t current_time = time(NULL);
    if (current_time != repr->timestamp) {
        bool updated = false;
        do {
            updated = (update_location(repr) || updated);
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

int location_open_csv(const anjay_dm_object_def_t **def,
                      const char *file_name,
                      time_t frequency_s) {
    location_t *location = get_location(def);
    if (frequency_s <= 0) {
        demo_log(ERROR, "Invalid CSV time frequency: %ld", (long) frequency_s);
        return -1;
    }
    FILE *csv = fopen(file_name, "r");
    if (!csv) {
        demo_log(ERROR, "Could not open CSV: %s", file_name);
        return -1;
    }
    if (location->csv) {
        fclose(location->csv);
    }
    location->csv = csv;
    location->csv_frequency = frequency_s;
    demo_log(INFO, "CSV loaded: %s (frequency_s = %ld)", file_name,
             frequency_s);
    return 0;
}
