/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
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
#include <unistd.h>

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

static int open_temporary_file(char *path) {
    mode_t old_umask = (mode_t) umask(S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH
                                      | S_IWOTH | S_IXOTH);
    int fd = mkstemp(path);
    umask(old_umask);
    return fd;
}

char *generate_random_target_filepath(void) {
    char *result = NULL;
    if (!(result = avs_strdup("/tmp/anjay-fw-XXXXXX"))) {
        return NULL;
    }

    int fd = open_temporary_file(result);
    if (fd == -1) {
        demo_log(ERROR, "could not generate firmware filename: %s",
                 strerror(errno));
        avs_free(result);
        return NULL;
    }
    close(fd);
    return result;
}

int copy_file_contents(FILE *dst, FILE *src) {
    while (!feof(src)) {
        char buf[4096];

        size_t bytes_read = fread(buf, 1, sizeof(buf), src);
        if (bytes_read == 0 && ferror(src)) {
            return -1;
        }

        if (fwrite(buf, 1, bytes_read, dst) != bytes_read) {
            return -1;
        }
    }
    return 0;
}

// CRC32 code adapted from http://home.thep.lu.se/~bjorn/crc/
static uint32_t crc32_for_byte(uint8_t value) {
    uint32_t result = value;
    for (int i = 0; i < 8; ++i) {
        if (result & 1) {
            result >>= 1;
        } else {
            result = (result >> 1) ^ (uint32_t) 0xEDB88320UL;
        }
    }
    return result ^ (uint32_t) 0xFF000000UL;
}

static void crc32(uint32_t *inout_crc, const uint8_t *data, size_t size) {
    static uint32_t LOOKUP_TABLE[256];
    if (!*LOOKUP_TABLE) {
        for (size_t i = 0; i < AVS_ARRAY_SIZE(LOOKUP_TABLE); ++i) {
            LOOKUP_TABLE[i] = crc32_for_byte((uint8_t) i);
        }
    }

    for (size_t i = 0; i < size; ++i) {
        *inout_crc = LOOKUP_TABLE[data[i] ^ (uint8_t) *inout_crc]
                     ^ (*inout_crc >> 8);
    }
}

int calc_file_crc32(const char *filename, uint32_t *out_crc) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        demo_log(ERROR, "could not open %s", filename);
        return -1;
    }

    *out_crc = 0;
    unsigned char buf[4096];
    int result = -1;

    while (!feof(f)) {
        size_t bytes_read = fread(buf, 1, sizeof(buf), f);
        if (bytes_read == 0 && ferror(f)) {
            demo_log(ERROR, "could not read from %s: %s", filename,
                     strerror(errno));
            goto cleanup;
        }

        crc32(out_crc, buf, bytes_read);
    }

    result = 0;

cleanup:
    fclose(f);
    return result;
}

#if defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) \
        && defined(AVS_COMMONS_STREAM_WITH_FILE)
avs_error_t store_etag(avs_persistence_context_t *ctx,
                       const anjay_etag_t *etag) {
    bool use_etag = (etag != NULL);
    avs_error_t err;

    (void) (avs_is_err((err = avs_persistence_bool(ctx, &use_etag))) || !etag
            || avs_is_err((err = avs_persistence_u8(
                                   ctx, (uint8_t *) (intptr_t) &etag->size)))
            || avs_is_err((err = avs_persistence_bytes(
                                   ctx, (uint8_t *) (intptr_t) etag->value,
                                   etag->size))));
    return err;
}

avs_error_t restore_etag(avs_persistence_context_t *ctx, anjay_etag_t **etag) {
    assert(etag && !*etag);
    bool use_etag;
    avs_error_t err;
    uint8_t size8;
    if (avs_is_err((err = avs_persistence_bool(ctx, &use_etag))) || !use_etag
            || avs_is_err((err = avs_persistence_u8(ctx, &size8)))
            || avs_is_err((err = ((*etag = anjay_etag_new(size8))
                                          ? avs_errno(AVS_NO_ERROR)
                                          : avs_errno(AVS_ENOMEM))))) {
        return err;
    }

    if (avs_is_err(err = avs_persistence_bytes(ctx, (*etag)->value, size8))) {
        avs_free(*etag);
        *etag = NULL;
    }
    return err;
}
#endif // defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) &&
       // defined(AVS_COMMONS_STREAM_WITH_FILE)

avs_coap_udp_tx_params_t g_tx_params;

void fw_set_coap_tx_params(const avs_coap_udp_tx_params_t *tx_params) {
    g_tx_params = *tx_params;
}

avs_coap_udp_tx_params_t fw_get_coap_tx_params(void *user_ptr,
                                               const char *download_uri) {
    (void) user_ptr;
    (void) download_uri;
    return g_tx_params;
}