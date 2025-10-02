/*
 * Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */

#ifndef DEMO_UTILS_H
#define DEMO_UTILS_H

#include <math.h>
#include <stdbool.h>
#include <time.h>

#include <avsystem/commons/avs_log.h>
#include <avsystem/commons/avs_time.h>

#include <anjay/anjay.h>

#define demo_log(level, ...) avs_log(demo, level, __VA_ARGS__)

char **argv_get(void);
int argv_store(int argc, char **argv);
int argv_append(const char *arg);

static inline unsigned time_to_rand(void) {
    return 1103515245u * (unsigned) avs_time_real_now().since_real_epoch.seconds
           + 12345u;
}

// this is the most precise representation possible using the double type
#define PI_OVER_180 0.017453292519943295

static inline double deg2rad(double deg) {
    return deg * PI_OVER_180;
}

static inline double rad2deg(double rad) {
    // double(pi/180) has lower relative error than double(180/pi)
    // hence I decided to use division rather than defining 180/pi as a constant
    return rad / PI_OVER_180;
}

static inline bool latitude_valid(double value) {
    return isfinite(value) && value >= -90.0 && value <= 90.0;
}

static inline bool longitude_valid(double value) {
    return isfinite(value) && value >= -180.0 && value < 180.0;
}

static inline bool velocity_mps_valid(double value) {
    return !isnan(value) && value >= 0.0;
}

static inline bool velocity_bearing_deg_cw_n_valid(double value) {
    return isfinite(value) && value >= 0.0 && value < 360.0;
}

double geo_distance_m(double lat1, double lon1, double lat2, double lon2);

int demo_parse_long(const char *str, long *out_value);

int fetch_bytes(anjay_input_ctx_t *ctx, void **buffer, size_t *out_size);

char *generate_random_target_filepath(void);

int copy_file_contents(FILE *dst, FILE *src);

int calc_file_crc32(const char *filename, uint32_t *out_crc);

#ifdef ANJAY_WITH_CONN_STATUS_API
const char *translate_server_connection_status_enum_to_str(
        anjay_server_conn_status_t status);
#endif // ANJAY_WITH_CONN_STATUS_API

#if defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) \
        && defined(AVS_COMMONS_STREAM_WITH_FILE)
avs_error_t store_etag(avs_persistence_context_t *ctx,
                       const anjay_etag_t *etag);
avs_error_t restore_etag(avs_persistence_context_t *ctx, anjay_etag_t **etag);
#endif // defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) &&
       // defined(AVS_COMMONS_STREAM_WITH_FILE)

typedef struct {
    char data[1]; // actually a VLA, but struct cannot be empty
} anjay_demo_allocated_buffer_t;

#endif // DEMO_UTILS_H
