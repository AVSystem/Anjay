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

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>
#include <arpa/inet.h>

#include "../objects.h"
#include "../utils.h"
#include "../wget_downloader.h"

#define FIRMWARE_UPDATE_OID 5

#define FW_RES_PACKAGE                  0
#define FW_RES_PACKAGE_URI              1
#define FW_RES_UPDATE                   2
#define FW_RES_STATE                    3
#define FW_RES_UPDATE_SUPPORTED_OBJECTS 4
#define FW_RES_UPDATE_RESULT            5
#define FW_RES_PKG_NAME                 6
#define FW_RES_PKG_VERSION              7
#define FW_RES_UPDATE_PROTOCOL_SUPPORT  8
#define FW_RES_UPDATE_DELIVERY_METHOD   9

#define FW_RES_BOUND_ 10

typedef enum {
    UPDATE_STATE_IDLE = 0,
    UPDATE_STATE_DOWNLOADING,
    UPDATE_STATE_DOWNLOADED,
    UPDATE_STATE_UPDATING
} fw_update_state_t;

typedef enum {
    UPDATE_RESULT_INITIAL = 0,
    UPDATE_RESULT_SUCCESS = 1,
    UPDATE_RESULT_NOT_ENOUGH_SPACE = 2,
    UPDATE_RESULT_OUT_OF_MEMORY = 3,
    UPDATE_RESULT_CONNECTION_LOST = 4,
    UPDATE_RESULT_INTEGRITY_FAILURE = 5,
    UPDATE_RESULT_UNSUPPORTED_PACKAGE_TYPE = 6,
    UPDATE_RESULT_INVALID_URI = 7,
    UPDATE_RESULT_FAILED = 8,
    UPDATE_RESULT_UNSUPPORTED_PROTOCOL = 9
} fw_update_result_t;

typedef struct firmware_metadata {
    uint8_t magic[8]; // "ANJAY_FW"
    uint16_t version;
    uint16_t force_error_case;
    uint32_t crc;
} firmware_metadata_t;

#define FORCE_ERROR_OUT_OF_MEMORY 1
#define FORCE_ERROR_FAILED_UPDATE 2

typedef struct fw_repr {
    const anjay_dm_object_def_t *def;
    iosched_t *iosched;
    wget_context_t *wget_context;

    firmware_metadata_t metadata;
    fw_update_state_t state;
    bool update_supported_objects;
    fw_update_result_t result;
    char package_uri[256];

    char next_target_path[256];
    char fw_updated_marker[256];
    bool cleanup_fw_on_upgrade;
} fw_repr_t;

static inline fw_repr_t *get_fw(const anjay_dm_object_def_t *const *obj_ptr) {
    if (!obj_ptr) {
        return NULL;
    }
    return container_of(obj_ptr, fw_repr_t, def);
}

static void set_update_result(anjay_t *anjay,
                              fw_repr_t *fw,
                              fw_update_result_t new_result) {
    if (fw->result != new_result) {
        fw->result = new_result;
        anjay_notify_changed(anjay, FIRMWARE_UPDATE_OID, 0,
                             FW_RES_UPDATE_RESULT);
    }
}

static void set_state(anjay_t *anjay,
                      fw_repr_t *fw,
                      fw_update_state_t new_state) {
    if (fw->state != new_state) {
        fw->state = new_state;
        anjay_notify_changed(anjay, FIRMWARE_UPDATE_OID, 0, FW_RES_STATE);
    }
}

static int generate_random_target_filepath(char *out_path_pattern,
                                           size_t path_pattern_size) {
    ssize_t result = snprintf(out_path_pattern, path_pattern_size,
                              "%s", "/tmp/anjay-fw-XXXXXX");
    if (result < 0 || result >= (ssize_t)path_pattern_size) {
        return -1;
    }

    int fd = mkstemp(out_path_pattern);
    if (fd == -1) {
        demo_log(ERROR, "could not generate firmware filename: %s",
                 strerror(errno));
        return -1;
    }
    close(fd);
    return 0;
}

static int maybe_create_firmware_file(fw_repr_t *fw) {
    if (!fw->next_target_path[0]) {
        if (generate_random_target_filepath(fw->next_target_path,
                                            sizeof(fw->next_target_path))) {
            return -1;
        }
        demo_log(INFO, "Created %s", fw->next_target_path);
    }
    return 0;
}

static void maybe_delete_firmware_file(fw_repr_t *fw) {
    if (fw->next_target_path[0]) {
        unlink(fw->next_target_path);
        demo_log(INFO, "Deleted %s", fw->next_target_path);
        fw->next_target_path[0] = '\0';
    }
}

static void reset(anjay_t *anjay,
                  fw_repr_t *fw) {
    set_state(anjay, fw, UPDATE_STATE_IDLE);
    set_update_result(anjay, fw, UPDATE_RESULT_INITIAL);
    demo_log(INFO, "Firmware Object state reset");
}

static bool is_supported_protocol(const char *protocol) {
    return !strncasecmp(protocol, "http", 4)
           || !strncasecmp(protocol, "https", 5);
}

void firmware_update_set_package_path(anjay_t *anjay,
                                      const anjay_dm_object_def_t **fw_obj,
                                      const char *path) {
    fw_repr_t *fw = get_fw(fw_obj);

    if (fw->state == UPDATE_STATE_DOWNLOADING) {
        assert(0 && "cannot set package path while a download is in progress");
    }

    size_t path_len = strlen(path);
    if (path_len >= sizeof(fw->next_target_path)) {
        demo_log(ERROR, "path too long");
        return;
    }

    snprintf(fw->next_target_path, sizeof(fw->next_target_path), "%s", path);
    demo_log(INFO, "firmware package path set to %s", fw->next_target_path);

    set_state(anjay, fw, UPDATE_STATE_IDLE);
}

void firmware_update_set_fw_updated_marker_path(
        const anjay_dm_object_def_t **fw_obj, const char *path) {
    fw_repr_t *fw = get_fw(fw_obj);
    strncpy(fw->fw_updated_marker, path, sizeof(fw->fw_updated_marker));
}

static int fw_read(anjay_t *anjay,
                   const anjay_dm_object_def_t *const *obj_ptr,
                   anjay_iid_t iid,
                   anjay_rid_t rid,
                   anjay_output_ctx_t *ctx) {
    (void) anjay; (void) iid;
    fw_repr_t *fw = get_fw(obj_ptr);
    switch (rid) {
    case FW_RES_STATE:
        return anjay_ret_i32(ctx, fw->state);
    case FW_RES_UPDATE_SUPPORTED_OBJECTS:
        return anjay_ret_bool(ctx, fw->update_supported_objects);
    case FW_RES_UPDATE_RESULT:
        return anjay_ret_i32(ctx, fw->result);
    case FW_RES_PKG_NAME:
        return anjay_ret_string(ctx, "Cute Firmware");
    case FW_RES_PKG_VERSION:
        return anjay_ret_string(ctx, "1.0");
    case FW_RES_UPDATE_PROTOCOL_SUPPORT: {
        anjay_output_ctx_t *array = anjay_ret_array_start(ctx);
        if (!array) {
            return ANJAY_ERR_INTERNAL;
        }
        static const int32_t supported_protocols[] = {
            2, /* HTTP 1.1 */
            3, /* HTTPS 1.1 */
        };
        size_t index = 0;
        while (index < ARRAY_SIZE(supported_protocols)) {
            if (anjay_ret_array_index(array, (anjay_riid_t) index)
                    || anjay_ret_i32(array, supported_protocols[index])) {
                anjay_ret_array_finish(array);
                return ANJAY_ERR_INTERNAL;
            }
            index++;
        }
        return anjay_ret_array_finish(array);
    }
    case FW_RES_UPDATE_DELIVERY_METHOD:
        /* 2 -> pull && push */
        return anjay_ret_i32(ctx, 2);
    case FW_RES_UPDATE:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    default:
        return ANJAY_ERR_NOT_FOUND;
    }
}

static void fix_fw_meta_endianness(firmware_metadata_t *meta) {
    meta->version = ntohs(meta->version);
    meta->force_error_case = ntohs(meta->force_error_case);
    meta->crc = ntohl(meta->crc);
}

static int read_fw_meta_from_file(FILE *f,
                                  firmware_metadata_t *out_metadata) {
    firmware_metadata_t m;
    memset(&m, 0, sizeof(m));

    if (fread(m.magic, sizeof(m.magic), 1, f) != 1
            || fread(&m.version, sizeof(m.version), 1, f) != 1
            || fread(&m.force_error_case, sizeof(m.force_error_case), 1, f) != 1
            || fread(&m.crc, sizeof(m.crc), 1, f) != 1) {
        demo_log(ERROR, "could not read firmware metadata");
        return -1;
    }

    fix_fw_meta_endianness(&m);
    *out_metadata = m;
    return 0;
}

static int copy_file_contents(FILE *dst,
                              FILE *src) {
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

static int unpack_fw_to_file(const char *fw_pkg_path,
                             const char *target_path,
                             firmware_metadata_t *out_metadata) {
    int result = -1;

    FILE *fw = fopen(fw_pkg_path, "rb");
    FILE *tmp = NULL;
    if (!fw) {
        demo_log(ERROR, "could not open file: %s", fw_pkg_path);
        goto cleanup;
    }

    tmp = fopen(target_path, "wb");
    if (!tmp) {
        demo_log(ERROR, "could not open file: %s", target_path);
        goto cleanup;
    }

    result = read_fw_meta_from_file(fw, out_metadata);
    if (result) {
        demo_log(ERROR, "could not read metadata from file: %s", fw_pkg_path);
        goto cleanup;
    }
    result = copy_file_contents(tmp, fw);
    if (result) {
        demo_log(ERROR, "could not copy firmware from %s to %s",
                 fw_pkg_path, target_path);
        goto cleanup;
    }

    result = 0;

cleanup:
    fclose(fw);
    if (tmp) {
        fclose(tmp);
    }
    return result;
}

static int unpack_firmware_in_place(fw_repr_t *fw) {
    char tmp_path[128];
    int result = generate_random_target_filepath(tmp_path, sizeof(tmp_path));
    if (result) {
        return -1;
    }

    result = unpack_fw_to_file(fw->next_target_path, tmp_path, &fw->metadata);
    if (result) {
        goto cleanup;
    }

    if ((result = rename(tmp_path, fw->next_target_path)) == -1) {
        demo_log(ERROR, "could not rename %s to %s: %s",
                 tmp_path, fw->next_target_path, strerror(errno));
        goto cleanup;
    }
    if ((result = chmod(fw->next_target_path, 0700)) == -1) {
        demo_log(ERROR, "could not set permissions for %s: %s",
                 fw->next_target_path, strerror(errno));
        goto cleanup;
    }

cleanup:
    unlink(tmp_path);
    if (result) {
        maybe_delete_firmware_file(fw);
    }

    return result;
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
        for (size_t i = 0; i < ARRAY_SIZE(LOOKUP_TABLE); ++i) {
            LOOKUP_TABLE[i] = crc32_for_byte((uint8_t) i);
        }
    }

    for (size_t i = 0; i < size; ++i) {
        *inout_crc = LOOKUP_TABLE[data[i] ^ (uint8_t) *inout_crc]
                ^ (*inout_crc >> 8);
    }
}

static int get_file_crc32(const char *filename,
                          uint32_t *out_crc) {
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
            demo_log(ERROR, "could not read from %s: %s",
                     filename, strerror(errno));
            goto cleanup;
        }

        crc32(out_crc, buf, bytes_read);
    }

    result = 0;

cleanup:
    fclose(f);
    return result;
}

static bool fw_magic_valid(const firmware_metadata_t *meta) {
    if (memcmp(meta->magic, "ANJAY_FW", sizeof(meta->magic))) {
        demo_log(ERROR, "invalid firmware magic");
        return false;
    }

    return true;
}

static bool fw_version_supported(const firmware_metadata_t *meta) {
    if (meta->version != 1) {
        demo_log(ERROR, "unsupported firmware version: %u", meta->version);
        return false;
    }

    return true;
}

static int validate_firmware(anjay_t *anjay,
                             fw_repr_t *fw) {
    if (!fw_magic_valid(&fw->metadata)
            || !fw_version_supported(&fw->metadata)) {
        set_state(anjay, fw, UPDATE_STATE_IDLE);
        set_update_result(anjay, fw, UPDATE_RESULT_UNSUPPORTED_PACKAGE_TYPE);
        return -1;
    }

    uint32_t actual_crc;
    int result = get_file_crc32(fw->next_target_path, &actual_crc);

    if (result) {
        demo_log(WARNING, "unable to check firmware CRC");

        set_state(anjay, fw, UPDATE_STATE_IDLE);
        set_update_result(anjay, fw, UPDATE_RESULT_INTEGRITY_FAILURE);
        return -1;
    }

    if (fw->metadata.crc != actual_crc) {
        demo_log(WARNING, "CRC mismatch: expected %08x != %08x actual",
                 fw->metadata.crc, actual_crc);

        set_state(anjay, fw, UPDATE_STATE_IDLE);
        set_update_result(anjay, fw, UPDATE_RESULT_INTEGRITY_FAILURE);
        return -1;
    }

    switch (fw->metadata.force_error_case) {
        case FORCE_ERROR_OUT_OF_MEMORY:
            set_state(anjay, fw, UPDATE_STATE_IDLE);
            set_update_result(anjay, fw, UPDATE_RESULT_OUT_OF_MEMORY);
            return -1;
        default:
            break;
    }

    set_state(anjay, fw, UPDATE_STATE_DOWNLOADED);
    set_update_result(anjay, fw, UPDATE_RESULT_INITIAL);
    return 0;
}

static fw_update_result_t result_from_wget_code(wget_result_t result) {
    switch (result) {
    case WGET_RESULT_OK:
        return UPDATE_RESULT_INITIAL;
    case WGET_RESULT_ERR_IO:
        return UPDATE_RESULT_NOT_ENOUGH_SPACE;
    case WGET_RESULT_ERR_NET:
    case WGET_RESULT_ERR_SSL:
    case WGET_RESULT_ERR_AUTH:
    case WGET_RESULT_ERR_PROTO:
    case WGET_RESULT_ERR_SERVER:
    case WGET_RESULT_ERR_PARSE:
    case WGET_RESULT_ERR_GENERIC:
        return UPDATE_RESULT_INVALID_URI;
    default:
        assert(0 && "should never happen");
        return UPDATE_RESULT_FAILED;
    }
}

typedef struct {
    anjay_t *anjay;
    struct fw_repr *fw;
} wget_handler_args_t;

static void wget_finish_callback(wget_result_t result,
                                 const wget_download_stats_t *stats,
                                 void *data) {
    (void) stats;
    wget_handler_args_t *args = (wget_handler_args_t *) data;

    anjay_t *anjay = args->anjay;
    fw_repr_t *fw = args->fw;

    if (result != WGET_RESULT_OK) {
        set_state(anjay, fw, UPDATE_STATE_IDLE);
        set_update_result(anjay, fw, result_from_wget_code(result));
        maybe_delete_firmware_file(fw);
        return;
    }

    if (unpack_firmware_in_place(fw)) {
        set_state(anjay, fw, UPDATE_STATE_IDLE);
        set_update_result(anjay, fw,
                          UPDATE_RESULT_UNSUPPORTED_PACKAGE_TYPE);
        return;
    }

    if (validate_firmware(anjay, fw)) {
        // specific state/update result set by validate_firmware itself
        return;
    }

    demo_log(INFO, "firmware downloaded successfully");
}

static int schedule_download_in_background(anjay_t *anjay,
                                           fw_repr_t *fw) {
    if (maybe_create_firmware_file(fw)) {
        return -1;
    }

    wget_handler_args_t *args =
            (wget_handler_args_t*)calloc(1, sizeof(wget_handler_args_t));
    if (!args) {
        demo_log(ERROR, "out of memory");
        goto error;
    }
    args->anjay = anjay;
    args->fw = fw;

    if (wget_register_finish_callback(fw->wget_context, wget_finish_callback,
                                      args, free)) {
        goto error;
    }

    if (wget_background_download(fw->wget_context, fw->package_uri,
                                 fw->next_target_path)) {
        set_update_result(anjay, fw, UPDATE_RESULT_FAILED);
        goto error;
    }

    set_update_result(anjay, fw, UPDATE_RESULT_INITIAL);
    set_state(anjay, fw, UPDATE_STATE_DOWNLOADING);
    return 0;

error:
    maybe_delete_firmware_file(fw);
    free(args);
    return -1;
}

static int write_firmware_to_file(anjay_t *anjay,
                                  fw_repr_t *fw,
                                  FILE *f,
                                  anjay_input_ctx_t *ctx) {
    int result = 0;
    size_t written = 0;
    bool finished = false;
    while (!finished) {
        size_t bytes_read;
        char buffer[1024];
        if ((result = anjay_get_bytes(ctx, &bytes_read, &finished, buffer,
                            sizeof(buffer)))) {
            demo_log(ERROR, "anjay_get_bytes() failed");

            set_state(anjay, fw, UPDATE_STATE_IDLE);
            set_update_result(anjay, fw, UPDATE_RESULT_FAILED);
            return result;
        }

        if (fwrite(buffer, 1, bytes_read, f) != bytes_read) {
            demo_log(ERROR, "fwrite failed");

            set_state(anjay, fw, UPDATE_STATE_IDLE);
            set_update_result(anjay, fw, UPDATE_RESULT_NOT_ENOUGH_SPACE);
            return ANJAY_ERR_INTERNAL;
        }
        written += bytes_read;
    }

    demo_log(INFO, "write finished, %lu B written", (unsigned long)written);
    return 0;
}

static int expect_no_firmware_content(anjay_input_ctx_t *ctx) {
    char ignored_byte;
    size_t bytes_read;
    bool finished = false;
    if (anjay_get_bytes(ctx, &bytes_read, &finished, &ignored_byte, 1)) {
        demo_log(ERROR, "anjay_get_bytes() failed");
        return ANJAY_ERR_INTERNAL;
    } else if (bytes_read > 0 || !finished) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    return 0;
}

static int write_firmware(anjay_t *anjay,
                          fw_repr_t *fw,
                          anjay_input_ctx_t *ctx) {
    if (fw->state == UPDATE_STATE_DOWNLOADING) {
        demo_log(ERROR,
                 "cannot set Package resource while downloading");
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }

    if (maybe_create_firmware_file(fw)) {
        return -1;
    }

    demo_log(INFO, "writing package to %s", fw->next_target_path);

    FILE *f = fopen(fw->next_target_path, "wb");
    if (!f) {
        demo_log(ERROR, "could not open file: %s", fw->next_target_path);
        return -1;
    }

    int result = write_firmware_to_file(anjay, fw, f, ctx);
    fclose(f);
    return result;
}

static int fw_write(anjay_t *anjay,
                    const anjay_dm_object_def_t *const *obj_ptr,
                    anjay_iid_t iid,
                    anjay_rid_t rid,
                    anjay_input_ctx_t *ctx) {
    (void) iid;

    fw_repr_t *fw = get_fw(obj_ptr);
    switch (rid) {
    case FW_RES_PACKAGE:
        {
            int result = 0;
            if (fw->state == UPDATE_STATE_DOWNLOADED) {
                result = expect_no_firmware_content(ctx);
                if (!result) {
                    reset(anjay, fw);
                }
            } else {
                result = write_firmware(anjay, fw, ctx);
                if (result
                        || unpack_firmware_in_place(fw)
                        || validate_firmware(anjay, fw)) {
                    // unpack_firmware_in_place/validate_firmware result
                    // deliberately not propagated up: write itself succeeded
                    maybe_delete_firmware_file(fw);
                }
                return result;
            }
            return result;
        }
    case FW_RES_PACKAGE_URI:
        {
            char buffer[sizeof(fw->package_uri)];
            if (anjay_get_string(ctx, buffer, sizeof(buffer)) < 0) {
                return ANJAY_ERR_INTERNAL;
            }

            if (fw->state == UPDATE_STATE_DOWNLOADED) {
                if (strlen(buffer) == 0) {
                    reset(anjay, fw);
                    return 0;
                } else {
                    return ANJAY_ERR_BAD_REQUEST;
                }
            }

            if (!is_supported_protocol(buffer)) {
                set_update_result(anjay, fw, UPDATE_RESULT_UNSUPPORTED_PROTOCOL);
                return ANJAY_ERR_BAD_REQUEST;
            }

            ssize_t result = snprintf(fw->package_uri, sizeof(fw->package_uri),
                                      "%s", buffer);
            if (result < 0 || result >= (ssize_t)sizeof(fw->package_uri)) {
                return ANJAY_ERR_INTERNAL;
            }

            if (schedule_download_in_background(anjay, fw)) {
                return ANJAY_ERR_INTERNAL;
            }

            return 0;
        }
    case FW_RES_UPDATE_SUPPORTED_OBJECTS:
        return anjay_get_bool(ctx, &fw->update_supported_objects);
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int create_update_marker_file(fw_repr_t *fw) {
    FILE *f = fopen(fw->fw_updated_marker, "w");
    if (!f) {
        return -1;
    }
    if (fw->cleanup_fw_on_upgrade
            && fprintf(f, "%s", fw->next_target_path) < 0) {
        demo_log(ERROR, "Couldn't write to firmware update marker");
    }
    fclose(f);
    return 0;
}

static void delete_update_marker_file(const fw_repr_t *fw) {
    unlink(fw->fw_updated_marker);
}

typedef struct {
    anjay_t *anjay;
    fw_repr_t *fw;
    char firmware_path[256];
} perform_upgrade_args_t;

static void perform_upgrade(void *args_) {
    perform_upgrade_args_t *args = (perform_upgrade_args_t*)args_;

    demo_log(INFO, "*** FIRMWARE UPDATE: %s ***", args->firmware_path);
    if (args->fw->metadata.force_error_case == FORCE_ERROR_FAILED_UPDATE) {
        demo_log(ERROR, "update failed");
        delete_update_marker_file(args->fw);
        set_state(args->anjay, args->fw, UPDATE_STATE_DOWNLOADED);
        set_update_result(args->anjay, args->fw, UPDATE_RESULT_FAILED);
        return;
    }

    execv(args->firmware_path, saved_argv);

    demo_log(ERROR, "execv failed (%s)", strerror(errno));
    delete_update_marker_file(args->fw);
    set_update_result(args->anjay, args->fw, UPDATE_RESULT_FAILED);
    set_state(args->anjay, args->fw, UPDATE_STATE_IDLE);
}

static int fw_execute(anjay_t *anjay,
                      const anjay_dm_object_def_t *const *obj_ptr,
                      anjay_iid_t iid,
                      anjay_rid_t rid,
                      anjay_execute_ctx_t *ctx) {
    (void) iid; (void) ctx;

    fw_repr_t *fw = get_fw(obj_ptr);
    switch (rid) {
    case FW_RES_UPDATE:
        {
            if (fw->state != UPDATE_STATE_DOWNLOADED) {
                demo_log(WARNING, "Firmware Update requested, but firmware not "
                         "yet downloaded (state = %d)", fw->state);
                return ANJAY_ERR_METHOD_NOT_ALLOWED;
            }

            perform_upgrade_args_t *args = (perform_upgrade_args_t*)
                    calloc(1, sizeof(perform_upgrade_args_t));

            if (!args || create_update_marker_file(fw)) {
                free(args);
                delete_update_marker_file(fw);
                set_update_result(anjay, fw, UPDATE_RESULT_FAILED);
                return ANJAY_ERR_INTERNAL;
            }

            args->anjay = anjay;
            args->fw = fw;

            STATIC_ASSERT(sizeof(args->firmware_path)
                          == sizeof(fw->next_target_path),
                          incompatible_buffer_sizes);
            memcpy(args->firmware_path, fw->next_target_path,
                   sizeof(args->firmware_path));

            if (!iosched_instant_entry_new(fw->iosched,
                                           perform_upgrade, args, free)) {
                free(args);
                delete_update_marker_file(fw);
                set_update_result(anjay, fw, UPDATE_RESULT_FAILED);
                return ANJAY_ERR_INTERNAL;
            }
            set_update_result(anjay, fw, UPDATE_RESULT_INITIAL);
            set_state(anjay, fw, UPDATE_STATE_UPDATING);
            return 0;
        }
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static const anjay_dm_object_def_t FIRMWARE_UPDATE = {
    .oid = FIRMWARE_UPDATE_OID,
    .rid_bound = FW_RES_BOUND_,
    .instance_it = anjay_dm_instance_it_SINGLE,
    .instance_present = anjay_dm_instance_present_SINGLE,
    .resource_present = anjay_dm_resource_present_TRUE,
    .resource_supported = anjay_dm_resource_supported_TRUE,
    .resource_read = fw_read,
    .resource_write = fw_write,
    .resource_execute = fw_execute,
    .transaction_begin = anjay_dm_transaction_NOOP,
    .transaction_validate = anjay_dm_transaction_NOOP,
    .transaction_commit = anjay_dm_transaction_NOOP,
    .transaction_rollback = anjay_dm_transaction_NOOP,
};

static void cleanup_after_upgrade(const char *fw_updated_marker) {
    FILE *f = fopen(fw_updated_marker, "r");
    char buf[128];
    if (!f || !fgets(buf, sizeof(buf), f)) {
        demo_log(ERROR, "Cannot determine whether firmware removal is necessary");
    } else {
        demo_log(INFO, "Deleted firmware upgrade image %s", buf);
        unlink(buf);
    }

    if (f) {
        fclose(f);
    }
    unlink(fw_updated_marker);
}

static fw_update_result_t
determine_update_result(const char *fw_updated_marker) {
    if (access(fw_updated_marker, F_OK) == F_OK) {
        return UPDATE_RESULT_SUCCESS;
    }
    return UPDATE_RESULT_INITIAL;
}

const anjay_dm_object_def_t **
firmware_update_object_create(iosched_t *iosched,
                              bool cleanup_fw_on_upgrade) {
    fw_repr_t *repr = (fw_repr_t*)calloc(1, sizeof(fw_repr_t));
    if (!repr) {
        return NULL;
    }

    strcpy(repr->fw_updated_marker, "/tmp/anjay-fw-updated");
    repr->def = &FIRMWARE_UPDATE;
    repr->iosched = iosched;
    repr->result = determine_update_result(repr->fw_updated_marker);

    if (repr->result == UPDATE_RESULT_SUCCESS) {
        cleanup_after_upgrade(repr->fw_updated_marker);
    }

    repr->wget_context = wget_context_new(iosched);
    repr->cleanup_fw_on_upgrade = cleanup_fw_on_upgrade;
    if (!repr->wget_context) {
        free(repr);
        return NULL;
    }

    return &repr->def;
}

void firmware_update_object_release(const anjay_dm_object_def_t **def) {
    fw_repr_t *fw = get_fw(def);
    maybe_delete_firmware_file(fw);
    wget_context_delete(&fw->wget_context);
    free(fw);
}
