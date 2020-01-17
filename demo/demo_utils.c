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

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "demo_utils.h"

static struct {
    size_t argc;
    char **argv;
} g_saved_args;

char **argv_get(void) {
    AVS_ASSERT(g_saved_args.argv, "argv_store not called before argv_get");
    return g_saved_args.argv;
}

int argv_store(int argc, char **argv) {
    AVS_ASSERT(argc >= 0, "unexpected negative value of argc");

    char **argv_copy = (char **) avs_calloc((size_t) argc + 1, sizeof(char *));
    if (!argv_copy) {
        return -1;
    }

    for (size_t i = 0; i < (size_t) argc; ++i) {
        argv_copy[i] = argv[i];
    }

    avs_free(g_saved_args.argv);
    g_saved_args.argv = argv_copy;
    g_saved_args.argc = (size_t) argc;
    return 0;
}

int argv_append(const char *arg) {
    assert(arg);

    size_t new_argc = g_saved_args.argc + 1;

    char **new_argv = (char **) avs_realloc(g_saved_args.argv,
                                            (new_argc + 1) * sizeof(char *));
    if (new_argv == NULL) {
        return -1;
    }

    new_argv[new_argc - 1] = (char *) (intptr_t) arg;
    new_argv[new_argc] = NULL;
    g_saved_args.argv = new_argv;
    g_saved_args.argc = new_argc;
    return 0;
}

static double geo_distance_m_with_radians(double lat1,
                                          double lon1,
                                          double lat2,
                                          double lon2) {
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

int demo_parse_long(const char *str, long *out_value) {
    if (!str) {
        return -1;
    }

    char *endptr = NULL;

    errno = 0;
    long value = strtol(str, &endptr, 0);

    if ((errno == ERANGE && (value == LONG_MAX || value == LONG_MIN))
            || (errno != 0 && value == 0) || endptr == str || !endptr
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
            avs_free(*buffer);
            *buffer = NULL;
            return 0;
        }
        void *block = avs_realloc(*buffer, *out_size + bytes_read);
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
    avs_free(*buffer);
    *buffer = NULL;
    return result;
}

int open_temporary_file(char *path) {
    mode_t old_umask = (mode_t) umask(S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH
                                      | S_IWOTH | S_IXOTH);
    int fd = mkstemp(path);
    umask(old_umask);
    return fd;
}
