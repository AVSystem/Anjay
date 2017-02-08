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
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "utils.h"

static double geo_distance_m_with_radians(double lat1, double lon1,
                                          double lat2, double lon2) {
    static const double MEAN_EARTH_PERIMETER_M = 12742017.6;
    // Haversine formula
    // code heavily inspired from http://stackoverflow.com/a/21623206
    double a = 0.5 - 0.5 * cos(lat2 - lat1)
            + cos(lat1) * cos(lat2) * 0.5 * (1.0 - cos(lon2 - lon1));
    return MEAN_EARTH_PERIMETER_M * asin(sqrt(a));
}

double geo_distance_m(double lat1, double lon1, double lat2, double lon2) {
    return geo_distance_m_with_radians(deg2rad(lat1), deg2rad(lon1),
                                       deg2rad(lat2), deg2rad(lon2));
}

int demo_parse_long(const char *str,
                    long *out_value) {
    char *endptr = NULL;

    errno = 0;
    long value = strtol(str, &endptr, 10);

    if ((errno == ERANGE
                && (value == LONG_MAX || value == LONG_MIN))
            || (errno != 0 && value == 0)
            || endptr == str
            || !endptr
            || *endptr != '\0') {
        demo_log(ERROR, "could not parse number: %s", str);
        return -1;
    }

    *out_value = value;
    return 0;
}

int fetch_bytes(anjay_input_ctx_t *ctx, void **buffer, size_t *out_size) {
    char tmp[1024];
    bool finished = 0;
    int result;
    // This will be used as a counter now.
    *out_size = 0;
    do {
        size_t bytes_read = 0;
        if ((result = anjay_get_bytes(ctx, &bytes_read, &finished, tmp,
                                      sizeof(tmp)))) {
            goto error;
        }
        if (*out_size + bytes_read == 0) {
            free(*buffer);
            *buffer = NULL;
            return 0;
        }
        void *block = realloc(*buffer, *out_size + bytes_read);
        if (!block) {
            result = ANJAY_ERR_INTERNAL;
            goto error;
        }
        memcpy((char *) block + *out_size, tmp, bytes_read);
        *buffer = block;
        *out_size += bytes_read;
    } while (!finished);
    return 0;

error:
    free(*buffer);
    *buffer = NULL;
    return result;
}
