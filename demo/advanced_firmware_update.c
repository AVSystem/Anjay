/*
 * Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */

#include "advanced_firmware_update.h"
#include <avsystem/commons/avs_stream_file.h>
#include <errno.h>
#include <stdio.h>

#include <sys/stat.h>
#include <unistd.h>

#define HEADER_VER_AFU_SINGLE 3
#define HEADER_VER_AFU_MULTI 4

typedef struct {
    states_results_paths_t states_results_paths;
    char *download_file;
    anjay_advanced_fw_update_severity_t severity;
    avs_time_real_t last_state_change_time;
    avs_time_real_t update_deadline;
    char current_ver[IMG_VER_STR_MAX_LEN + 1];
} advanced_firmware_update_persistence_file_data_t;

typedef struct {
    anjay_t *anjay;
    anjay_iid_t iid;
    anjay_advanced_fw_update_state_t delayed_state;
    anjay_advanced_fw_update_result_t delayed_result;
} set_delayed_advanced_fw_update_result_args_t;

static void set_delayed_fw_update_result(avs_sched_t *sched, const void *arg) {
    (void) sched;
    const set_delayed_advanced_fw_update_result_args_t *args =
            (const set_delayed_advanced_fw_update_result_args_t *) arg;

    anjay_advanced_fw_update_set_state_and_result(
            args->anjay, args->iid, args->delayed_state, args->delayed_result);
}

static void fix_fw_meta_endianness(advanced_fw_metadata_t *meta) {
    meta->header_ver = avs_convert_be16(meta->header_ver);
    meta->force_error_case = avs_convert_be16(meta->force_error_case);
    meta->crc = avs_convert_be32(meta->crc);
}

static int read_fw_meta_from_file(FILE *f,
                                  advanced_fw_metadata_t *out_metadata,
                                  uint32_t *out_metadata_len) {
    advanced_fw_metadata_t m;
    memset(&m, 0, sizeof(m));

    if (fread(m.magic, sizeof(m.magic), 1, f) != 1
            || fread(&m.header_ver, sizeof(m.header_ver), 1, f) != 1
            || fread(&m.force_error_case, sizeof(m.force_error_case), 1, f) != 1
            || fread(&m.crc, sizeof(m.crc), 1, f) != 1
            || fread(m.linked, sizeof(m.linked), 1, f) != 1
            || fread(&m.pkg_ver_len, sizeof(m.pkg_ver_len), 1, f) != 1) {
        demo_log(ERROR, "could not read firmware metadata");
        return -1;
    }

    if (m.pkg_ver_len > IMG_VER_STR_MAX_LEN || m.pkg_ver_len == 0) {
        demo_log(ERROR, "Wrong pkg version len");
        return ANJAY_ADVANCED_FW_UPDATE_ERR_UNSUPPORTED_PACKAGE_TYPE;
    }

    if (fread(m.pkg_ver, m.pkg_ver_len, 1, f) != 1) {
        demo_log(ERROR, "could not read firmware metadata");
        return -1;
    }

    fix_fw_meta_endianness(&m);
    *out_metadata = m;
    *out_metadata_len = sizeof(m.magic) + sizeof(m.header_ver)
                        + sizeof(m.force_error_case) + sizeof(m.crc)
                        + sizeof(m.linked) + sizeof(m.pkg_ver_len)
                        + m.pkg_ver_len;
    return 0;
}

static int
handle_multipackage(FILE *f,
                    advanced_fw_multipkg_metadata_t *out_multiple_metadata) {
    advanced_fw_multipkg_metadata_t mm;
    memset(&mm, 0, sizeof(mm));

    if (fread(mm.magic, sizeof(mm.magic), 1, f) != 1
            || fread(&mm.header_ver, sizeof(mm.header_ver), 1, f) != 1) {
        demo_log(ERROR, "could not read firmware metadata");
        return -1;
    }

    mm.header_ver = avs_convert_be16(mm.header_ver);
    if (mm.header_ver == HEADER_VER_AFU_MULTI
            && !memcmp(mm.magic, "MULTIPKG", sizeof(mm.magic))) {
        demo_log(INFO, "Received multi package firmware");
        if (fread(&mm.packages_count, sizeof(mm.packages_count), 1, f) != 1) {
            demo_log(ERROR, "could not read firmware metadata");
            return -1;
        }
        mm.packages_count = avs_convert_be16(mm.packages_count);
        if (mm.packages_count > FW_UPDATE_IID_IMAGE_SLOTS) {
            demo_log(ERROR,
                     "Received packages_count %u is more than"
                     " available slots",
                     mm.packages_count);
            return -1;
        }
        for (uint16_t i = 0; i < mm.packages_count; ++i) {
            if (fread(&mm.package_len[i], sizeof(mm.package_len[i]), 1, f)
                    != 1) {
                demo_log(ERROR, "could not read firmware metadata");
                return -1;
            }
            mm.package_len[i] = avs_convert_be32(mm.package_len[i]);
            if (mm.package_len[i] == 0) {
                demo_log(
                        ERROR,
                        "Zero-length packages within multipackage not allowed");
                return -1;
            }
        }
        demo_log(INFO, "Multi meta: {header version: %u, packages_count: %u}",
                 mm.header_ver, mm.packages_count);
        *out_multiple_metadata = mm;
    } else {
        /* It is not multipackage, move stream to the beginning to easily handle
         * like standard package */
        if (fseek(f, 0L, SEEK_SET)) {
            demo_log(ERROR, "Could not seek in the multipackage file");
            return -1;
        }
    }
    return 0;
}

static int copy_file_contents_by_bytes(FILE *dst, FILE *src, uint32_t len) {
    char buf[4096];
    uint32_t to_read = 0;
    while (len) {
        to_read = len > sizeof(buf) ? sizeof(buf) : len;
        size_t bytes_read = fread(buf, 1, to_read, src);
        if (bytes_read != to_read) {
            return -1;
        }

        if (fwrite(buf, 1, bytes_read, dst) != bytes_read) {
            return -1;
        }
        len -= (uint32_t) bytes_read;
    }
    return 0;
}

static int unpack_fw_to_file(FILE *fw,
                             uint32_t fw_len,
                             const char *target_path,
                             advanced_fw_metadata_t *out_metadata) {
    int result = -1;
    FILE *tmp = NULL;
    uint32_t metadata_len = 0;

    tmp = fopen(target_path, "wb");
    if (!tmp) {
        demo_log(ERROR, "could not open file: %s", target_path);
        goto cleanup;
    }

    result = read_fw_meta_from_file(fw, out_metadata, &metadata_len);
    if (result) {
        demo_log(ERROR, "could not read metadata");
        goto cleanup;
    }
    if (fw_len) {
        result = copy_file_contents_by_bytes(tmp, fw, fw_len - metadata_len);
    } else {
        result = copy_file_contents(tmp, fw);
    }
    if (result) {
        demo_log(ERROR, "could not copy firmware");
        goto cleanup;
    }

    result = 0;

cleanup:
    if (tmp) {
        fclose(tmp);
    }
    return result;
}

static void maybe_delete_firmware_file(advanced_fw_update_logic_t *fw) {
    if (fw->next_target_path) {
        unlink(fw->next_target_path);
        demo_log(INFO, "Deleted %s", fw->next_target_path);
        avs_free(fw->next_target_path);
        fw->next_target_path = NULL;
    }
}

const char *const MAGICS[] = {
    [FW_UPDATE_IID_APP] = "AJAY_APP",
    [FW_UPDATE_IID_TEE] = "AJAY_TEE",
    [FW_UPDATE_IID_BOOT] = "AJAYBOOT",
    [FW_UPDATE_IID_MODEM] = "AJAYMODE"
};

static int find_instance_magic_based(advanced_fw_metadata_t *meta,
                                     anjay_iid_t *iid) {
    for (size_t i = 0; i < AVS_ARRAY_SIZE(MAGICS); ++i) {
        if (!memcmp(meta->magic, MAGICS[i], sizeof(meta->magic))) {
            *iid = (anjay_iid_t) i;
            return 0;
        }
    }
    return -1;
}

static int unpack_firmware(FILE *firmware,
                           uint32_t len,
                           unpacked_imgs_info_t *unpacked_info) {
    char *tmp_path = generate_random_target_filepath();
    if (!tmp_path) {
        return -1;
    }

    advanced_fw_metadata_t metadata;
    memset(&metadata, 0x00, sizeof(metadata));
    int result = unpack_fw_to_file(firmware, len, tmp_path, &metadata);
    if (result) {
        goto cleanup;
    }
    anjay_iid_t iid;
    result = find_instance_magic_based(&metadata, &iid);
    if (!result) {
        unpacked_info[iid].path = tmp_path;
        unpacked_info[iid].meta = metadata;
    }

cleanup:
    if (result) {
        unlink(tmp_path);
        avs_free(tmp_path);
    }
    return result;
}

static bool is_state_downloaded(advanced_fw_update_logic_t *fw) {
    assert(fw->anjay);
    anjay_advanced_fw_update_state_t state =
            ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE;
    anjay_advanced_fw_update_get_state(fw->anjay, fw->iid, &state);
    return state == ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADED;
}

static int unpack_firmware_in_place(anjay_iid_t iid,
                                    advanced_fw_update_logic_t *fw_table,
                                    anjay_iid_t *downloaded_iids,
                                    int *downloaded_iids_count) {
    advanced_fw_update_logic_t *fw = &fw_table[iid];

    *downloaded_iids_count = 0;
    advanced_fw_multipkg_metadata_t multi_metadata;
    memset(&multi_metadata, 0x00, sizeof(multi_metadata));
    FILE *firmware = NULL;
    unpacked_imgs_info_t unpacked_info[FW_UPDATE_IID_IMAGE_SLOTS];
    memset(&unpacked_info, 0x00, sizeof(unpacked_info));
    uint16_t to_unpack = 0;
    firmware = fopen(fw->next_target_path, "rb");
    if (!firmware) {
        demo_log(ERROR, "could not open file: %s", fw->next_target_path);
        return -1;
    }
    int result = handle_multipackage(firmware, &multi_metadata);
    if (result) {
        goto cleanup;
    }

    /* packages_count == 0 means that it is not multipackage, but there is
     * still one 'normal' package to unpack */
    to_unpack = AVS_MAX(1, multi_metadata.packages_count);

    for (int i = 0; i < to_unpack; ++i) {
        if ((result = unpack_firmware(firmware, multi_metadata.package_len[i],
                                      unpacked_info))) {
            goto cleanup;
        }
    }

    for (anjay_iid_t i = 0; i < FW_UPDATE_IID_IMAGE_SLOTS; ++i) {
        if (unpacked_info[i].path && is_state_downloaded(&fw_table[i])) {
            demo_log(ERROR,
                     "Failure. Multipackage contains package for "
                     "instance /" AVS_QUOTE_MACRO(
                             ANJAY_ADVANCED_FW_UPDATE_OID) "/%d which is "
                                                           "already in "
                                                           "DOWNLOADED state.",
                     i);
            anjay_advanced_fw_update_set_conflicting_instances(fw->anjay,
                                                               fw->iid, &i, 1);
            result = ANJAY_ADVANCED_FW_UPDATE_ERR_CONFLICTING_STATE;
            goto cleanup;
        }
    }

    for (anjay_iid_t i = 0; i < FW_UPDATE_IID_IMAGE_SLOTS; ++i) {
        if (unpacked_info[i].path) {
            fw_update_common_maybe_create_firmware_file(&fw_table[i]);
            if ((result = rename(unpacked_info[i].path,
                                 fw_table[i].next_target_path))
                    == -1) {
                demo_log(ERROR, "could not rename %s to %s: %s",
                         unpacked_info[i].path, fw_table[i].next_target_path,
                         strerror(errno));
                goto cleanup;
            }
            if ((result = chmod(fw_table[i].next_target_path, 0700)) == -1) {
                demo_log(ERROR, "could not set permissions for %s: %s",
                         fw_table[i].next_target_path, strerror(errno));
                goto cleanup;
            }
            fw_table[i].metadata = unpacked_info[i].meta;
            downloaded_iids[*downloaded_iids_count] = i;
            (*downloaded_iids_count)++;
        }
    }

cleanup:
    for (int i = 0; i < FW_UPDATE_IID_IMAGE_SLOTS; ++i) {
        if (unpacked_info[i].path) {
            unlink(unpacked_info[i].path);
            avs_free(unpacked_info[i].path);
        }
    }

    if (firmware) {
        fclose(firmware);
    }
    if (result) {
        maybe_delete_firmware_file(fw);
    }
    return result;
}

static bool fw_magic_valid(const advanced_fw_metadata_t *meta,
                           anjay_iid_t iid) {
    switch (iid) {
    case FW_UPDATE_IID_APP:
    case FW_UPDATE_IID_TEE:
    case FW_UPDATE_IID_BOOT:
    case FW_UPDATE_IID_MODEM:
        if (!memcmp(meta->magic, MAGICS[iid], sizeof(meta->magic))) {
            return true;
        }
        break;
    default:
        break;
    }
    demo_log(ERROR, "invalid firmware magic");
    return false;
}

static bool fw_header_version_valid(const advanced_fw_metadata_t *meta) {
    if (meta->header_ver != HEADER_VER_AFU_SINGLE) {
        demo_log(ERROR, "wrong header version");
        return false;
    }

    return true;
}

static int validate_firmware(advanced_fw_update_logic_t *fw) {
    if (!fw_magic_valid(&fw->metadata, fw->iid)
            || !fw_header_version_valid(&fw->metadata)) {
        return ANJAY_ADVANCED_FW_UPDATE_ERR_UNSUPPORTED_PACKAGE_TYPE;
    }

    uint32_t actual_crc;
    int result = calc_file_crc32(fw->next_target_path, &actual_crc);

    if (result) {
        demo_log(WARNING, "unable to check firmware CRC");
        return ANJAY_ADVANCED_FW_UPDATE_ERR_INTEGRITY_FAILURE;
    }

    if (fw->metadata.crc != actual_crc) {
        demo_log(WARNING, "CRC mismatch: expected %08x != %08x actual",
                 fw->metadata.crc, actual_crc);
        return ANJAY_ADVANCED_FW_UPDATE_ERR_INTEGRITY_FAILURE;
    }

    switch (fw->metadata.force_error_case) {
    case FORCE_ERROR_OUT_OF_MEMORY:
        return ANJAY_ADVANCED_FW_UPDATE_ERR_OUT_OF_MEMORY;
    default:
        break;
    }

    return 0;
}

static int process_linked(advanced_fw_update_logic_t *fw) {
    anjay_iid_t linked[METADATA_LINKED_SLOTS];
    size_t linked_count = 0;
    memset(linked, 0xFF, sizeof(linked));
    for (int i = 0; i < METADATA_LINKED_SLOTS; ++i) {
        // Below condition compares metadata.linked[] with
        // METADATA_LINKED_SLOTS, because max handled iid is derived from
        // available slots.
        if (fw->metadata.linked[i] < METADATA_LINKED_SLOTS) {
            linked[linked_count++] = fw->metadata.linked[i];
        } else if (fw->metadata.linked[i] != 0xFF) {
            demo_log(WARNING, "Unexpected linked instance iid");
        }
    }
    return anjay_advanced_fw_update_set_linked_instances(fw->anjay, fw->iid,
                                                         linked, linked_count);
}

static int preprocess_firmware(anjay_iid_t iid,
                               advanced_fw_update_logic_t *fw_table) {
    anjay_iid_t downloaded_iids[FW_UPDATE_IID_IMAGE_SLOTS];
    int downloaded_iids_count = 0;
    int result = unpack_firmware_in_place(iid, fw_table, downloaded_iids,
                                          &downloaded_iids_count);
    if (result) {
        return result;
    }

    result = -1;
    for (int i = 0; i < downloaded_iids_count; ++i) {
        advanced_fw_update_logic_t *fw = &fw_table[downloaded_iids[i]];
        if ((result = validate_firmware(fw))) {
            break;
        }
        if ((result = process_linked(fw))) {
            break;
        }
        demo_log(INFO,
                 "firmware for instance /" AVS_QUOTE_MACRO(
                         ANJAY_ADVANCED_FW_UPDATE_OID) "/%u downloaded "
                                                       "successfully",
                 downloaded_iids[i]);
        if ((result = anjay_advanced_fw_update_set_state_and_result(
                     fw->anjay, fw->iid,
                     ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADED,
                     ANJAY_ADVANCED_FW_UPDATE_RESULT_INITIAL))) {
            break;
        }
    }
    return result;
}

#if defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) \
        && defined(AVS_COMMONS_STREAM_WITH_FILE)
static advanced_firmware_update_persistence_file_data_t
advanced_firmware_update_read_persistence_file(const char *path) {
    advanced_firmware_update_persistence_file_data_t data;
    memset(&data, 0, sizeof(data));
    avs_stream_t *stream = NULL;
    int8_t results8[FW_UPDATE_IID_IMAGE_SLOTS];
    memset(results8, 0x00, sizeof(results8));
    int8_t states8[FW_UPDATE_IID_IMAGE_SLOTS];
    memset(states8, 0x00, sizeof(states8));
    for (size_t i = 0; i < AVS_ARRAY_SIZE(results8); ++i) {
        results8[i] = (int8_t) ANJAY_ADVANCED_FW_UPDATE_RESULT_INITIAL;
        states8[i] = (int8_t) ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE;
    }
    if ((stream = avs_stream_file_create(path, AVS_STREAM_FILE_READ))) {
        // invalid or empty but existing file still signifies success but only
        // for APP instance
        results8[FW_UPDATE_IID_APP] =
                (int8_t) ANJAY_ADVANCED_FW_UPDATE_RESULT_SUCCESS;
    }
    avs_persistence_context_t ctx =
            avs_persistence_restore_context_create(stream);
    uint8_t severity8 = (uint8_t) ANJAY_ADVANCED_FW_UPDATE_SEVERITY_MANDATORY;
    int64_t last_state_change_timestamp = 0;
    int64_t update_timestamp = 0;
    char *current_ver = NULL;
    if (!stream
            || avs_is_err(avs_persistence_bytes(&ctx, (uint8_t *) &results8,
                                                sizeof(results8)))
            || avs_is_err(avs_persistence_bytes(&ctx, (uint8_t *) &states8,
                                                sizeof(states8)))
            || avs_is_err(avs_persistence_bytes(&ctx, &severity8, 1))
            || avs_is_err(
                       avs_persistence_i64(&ctx, &last_state_change_timestamp))
            || avs_is_err(avs_persistence_i64(&ctx, &update_timestamp))
            || avs_is_err(avs_persistence_string(&ctx, &current_ver))) {
        demo_log(WARNING,
                 "Invalid data in the firmware state persistence file");
        memset(&data, 0, sizeof(data));
    } else {
        for (anjay_iid_t iid = FW_UPDATE_IID_APP;
             iid < FW_UPDATE_IID_IMAGE_SLOTS;
             ++iid) {
            if (avs_is_err(avs_persistence_string(
                        &ctx,
                        &data.states_results_paths.next_target_paths[iid]))) {
                for (anjay_iid_t i = FW_UPDATE_IID_APP;
                     i < FW_UPDATE_IID_IMAGE_SLOTS;
                     ++i) {
                    avs_free(data.states_results_paths.next_target_paths[i]);
                }
                demo_log(WARNING,
                         "Invalid data in the firmware state persistence file");
                memset(&data, 0, sizeof(data));
                break;
            }
        }
    }
    if (current_ver && strlen(current_ver) <= IMG_VER_STR_MAX_LEN) {
        strcpy(data.current_ver, current_ver);
    } else {
        demo_log(WARNING, "Invalid version string");
    }
    avs_free(current_ver);
    for (size_t i = 0; i < AVS_ARRAY_SIZE(results8); ++i) {
        data.states_results_paths.inst_results[i] =
                (anjay_advanced_fw_update_result_t) results8[i];
        data.states_results_paths.inst_states[i] =
                (anjay_advanced_fw_update_state_t) states8[i];
    }
    data.severity = (anjay_advanced_fw_update_severity_t) severity8;
    data.last_state_change_time =
            avs_time_real_from_scalar(last_state_change_timestamp, AVS_TIME_S);
    data.update_deadline =
            avs_time_real_from_scalar(update_timestamp, AVS_TIME_S);
    if (stream) {
        avs_stream_cleanup(&stream);
    }
    return data;
}

int advanced_firmware_update_write_persistence_file(
        const char *path,
        states_results_paths_t *states_results_paths,
        anjay_advanced_fw_update_severity_t severity,
        avs_time_real_t last_state_change_time,
        avs_time_real_t update_deadline,
        const char *current_ver) {
    avs_stream_t *stream = avs_stream_file_create(path, AVS_STREAM_FILE_WRITE);
    avs_persistence_context_t ctx =
            avs_persistence_store_context_create(stream);
    int8_t results8[FW_UPDATE_IID_IMAGE_SLOTS];
    memset(results8, 0x00, sizeof(results8));
    int8_t states8[FW_UPDATE_IID_IMAGE_SLOTS];
    memset(states8, 0x00, sizeof(states8));
    for (size_t i = 0; i < AVS_ARRAY_SIZE(results8); ++i) {
        results8[i] = (int8_t) states_results_paths->inst_results[i];
        states8[i] = (int8_t) states_results_paths->inst_states[i];
    }
    uint8_t severity8 = (uint8_t) severity;
    int64_t last_state_change_timestamp = 0;
    avs_time_real_to_scalar(&last_state_change_timestamp, AVS_TIME_S,
                            last_state_change_time);
    int64_t update_timestamp = 0;
    avs_time_real_to_scalar(&update_timestamp, AVS_TIME_S, update_deadline);

    int retval = 0;
    if (!stream
            || avs_is_err(avs_persistence_bytes(&ctx, (uint8_t *) &results8,
                                                sizeof(results8)))
            || avs_is_err(avs_persistence_bytes(&ctx, (uint8_t *) &states8,
                                                sizeof(states8)))
            || avs_is_err(avs_persistence_bytes(&ctx, &severity8, 1))
            || avs_is_err(
                       avs_persistence_i64(&ctx, &last_state_change_timestamp))
            || avs_is_err(avs_persistence_i64(&ctx, &update_timestamp))
            || avs_is_err(avs_persistence_string(
                       &ctx, (char **) (intptr_t) &current_ver))) {
        demo_log(ERROR, "Could not write firmware state persistence file");
        retval = -1;
    } else {
        for (anjay_iid_t iid = FW_UPDATE_IID_APP;
             iid < FW_UPDATE_IID_IMAGE_SLOTS;
             ++iid) {
            if (avs_is_err(avs_persistence_string(
                        &ctx, &states_results_paths->next_target_paths[iid]))) {
                demo_log(ERROR,
                         "Could not write firmware state persistence file");
                retval = -1;
                break;
            }
        }
    }
    if (stream) {
        avs_stream_cleanup(&stream);
    }
    if (retval) {
        unlink(path);
    }
    return retval;
}

void advanced_firmware_update_delete_persistence_file(
        const advanced_fw_update_logic_t *fw) {
    if (fw->persistence_file) {
        unlink(fw->persistence_file);
    }
}

#else  // defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) &&
       // defined(AVS_COMMONS_STREAM_WITH_FILE)
advanced_firmware_update_persistence_file_data_t
advanced_firmware_update_read_persistence_file(const char *path) {
    (void) path;
    demo_log(WARNING, "Persistence not compiled in");
    persistence_file_data_t retval;
    memset(&retval, 0, sizeof(retval));
    return retval;
}

int advanced_firmware_update_write_persistence_file(
        const char *path,
        anjay_advanced_fw_update_state_t state,
        anjay_advanced_fw_update_state_t result) {
    (void) path;
    (void) state;
    (void) result;
    demo_log(WARNING, "Persistence not compiled in");
    return 0;
}

void advanced_firmware_update_delete_persistence_file(
        const advanced_fw_update_logic_t *fw) {
    (void) fw;
    demo_log(WARNING, "Persistence not compiled in");
}
#endif // defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) &&
       // defined(AVS_COMMONS_STREAM_WITH_FILE)

static void fw_reset(advanced_fw_update_logic_t *fw) {
    if (fw->stream) {
        fclose(fw->stream);
        fw->stream = NULL;
    }
    maybe_delete_firmware_file(fw);
    advanced_firmware_update_delete_persistence_file(fw);
}

int advanced_firmware_update_read_states_results_paths(
        advanced_fw_update_logic_t *fw_table,
        states_results_paths_t *out_states_results_paths) {
    for (int i = 0; i < FW_UPDATE_IID_IMAGE_SLOTS; ++i) {
        if (anjay_advanced_fw_update_get_state(
                    fw_table[i].anjay,
                    fw_table[i].iid,
                    &out_states_results_paths->inst_states[i])) {
            return -1;
        }
        if (anjay_advanced_fw_update_get_result(
                    fw_table[i].anjay,
                    fw_table[i].iid,
                    &out_states_results_paths->inst_results[i])) {
            return -1;
        }
        out_states_results_paths->next_target_paths[i] =
                fw_table[i].next_target_path;
    }
    return 0;
}

int fw_update_common_open(anjay_iid_t iid, void *fw_) {
    advanced_fw_update_logic_t *fw_table = (advanced_fw_update_logic_t *) fw_;
    advanced_fw_update_logic_t *fw = &fw_table[iid];
    assert(!fw->stream);

    if (fw_update_common_maybe_create_firmware_file(fw)) {
        return -1;
    }
    if (!(fw->stream = fopen(fw->next_target_path, "wb"))) {
        return -1;
    }

    states_results_paths_t states_results_paths;
    if (advanced_firmware_update_read_states_results_paths(
                fw_table, &states_results_paths)) {
        return -1;
    }
    states_results_paths.inst_states[FW_UPDATE_IID_APP] =
            ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADING;
    states_results_paths.inst_results[FW_UPDATE_IID_APP] =
            ANJAY_ADVANCED_FW_UPDATE_RESULT_INITIAL;
    if (fw->persistence_file
            && advanced_firmware_update_write_persistence_file(
                       fw->persistence_file, &states_results_paths,
                       anjay_advanced_fw_update_get_severity(fw->anjay,
                                                             fw->iid),
                       anjay_advanced_fw_update_get_last_state_change_time(
                               fw->anjay, fw->iid),
                       anjay_advanced_fw_update_get_deadline(fw->anjay,
                                                             fw->iid),
                       fw->current_ver)) {
        fw_reset(fw);
        return -1;
    }
    return 0;
}

int fw_update_common_write(anjay_iid_t iid,
                           void *fw_,
                           const void *data,
                           size_t length) {
    advanced_fw_update_logic_t *fw_table = (advanced_fw_update_logic_t *) fw_;
    advanced_fw_update_logic_t *fw = &fw_table[iid];
    if (!fw->stream) {
        demo_log(ERROR, "stream not open");
        return -1;
    }
    if (length
            && (fwrite(data, length, 1, fw->stream) != 1
                // Firmware update integration tests measure download
                // progress by checking file size, so avoiding buffering
                // is required.
                || fflush(fw->stream) != 0)) {
        demo_log(ERROR, "fwrite or fflush failed: %s", strerror(errno));
        return ANJAY_ADVANCED_FW_UPDATE_ERR_NOT_ENOUGH_SPACE;
    }
    return 0;
}

static int stream_finish(anjay_iid_t iid, void *fw_) {
    advanced_fw_update_logic_t *fw_table = (advanced_fw_update_logic_t *) fw_;
    advanced_fw_update_logic_t *fw = &fw_table[iid];
    if (fw->auto_suspend) {
        anjay_advanced_fw_update_pull_suspend(fw->anjay);
    }
    if (!fw->stream) {
        demo_log(ERROR, "stream not open");
        return -1;
    }
    fclose(fw->stream);
    fw->stream = NULL;

    anjay_advanced_fw_update_state_t tee_state;
    anjay_advanced_fw_update_result_t tee_result;
    anjay_advanced_fw_update_get_state(fw_table[FW_UPDATE_IID_TEE].anjay,
                                       FW_UPDATE_IID_TEE, &tee_state);
    anjay_advanced_fw_update_get_result(fw_table[FW_UPDATE_IID_TEE].anjay,
                                        FW_UPDATE_IID_TEE, &tee_result);

    states_results_paths_t states_results_paths;
    int result = advanced_firmware_update_read_states_results_paths(
            fw_table, &states_results_paths);
    if (result) {
        return result;
    }
    states_results_paths.inst_states[FW_UPDATE_IID_APP] =
            ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADED;
    states_results_paths.inst_results[FW_UPDATE_IID_APP] =
            ANJAY_ADVANCED_FW_UPDATE_RESULT_INITIAL;
    if ((result = preprocess_firmware(iid, fw_table))
            || (fw->persistence_file
                && (result = advanced_firmware_update_write_persistence_file(
                            fw->persistence_file, &states_results_paths,
                            anjay_advanced_fw_update_get_severity(fw->anjay,
                                                                  fw->iid),
                            anjay_advanced_fw_update_get_last_state_change_time(
                                    fw->anjay, fw->iid),
                            anjay_advanced_fw_update_get_deadline(fw->anjay,
                                                                  fw->iid),
                            fw->current_ver)))) {
        fw_reset(fw);
    }
    return result;
}

const char *fw_update_common_get_current_version(anjay_iid_t iid, void *fw_) {
    advanced_fw_update_logic_t *fw_table = (advanced_fw_update_logic_t *) fw_;
    advanced_fw_update_logic_t *fw = &fw_table[iid];
    return (const char *) fw->current_ver;
}

const char *fw_update_common_get_pkg_version(anjay_iid_t iid, void *fw_) {
    advanced_fw_update_logic_t *fw_table = (advanced_fw_update_logic_t *) fw_;
    advanced_fw_update_logic_t *fw = &fw_table[iid];
    return (const char *) fw->metadata.pkg_ver;
}

static int
add_conflicting(anjay_iid_t (*inout_conflicting)[FW_UPDATE_IID_IMAGE_SLOTS],
                size_t *inout_conflicting_len,
                anjay_iid_t add_conf) {
    if (*inout_conflicting_len + 1 > FW_UPDATE_IID_IMAGE_SLOTS
            || ((*inout_conflicting) == NULL && *inout_conflicting_len != 0)) {
        return -1;
    }

    anjay_iid_t new_conflicting[FW_UPDATE_IID_IMAGE_SLOTS];
    memset(new_conflicting, 0x00, sizeof(new_conflicting));
    size_t position = 0;

    for (position = 0; position < *inout_conflicting_len; ++position) {
        if ((*inout_conflicting)[position] == add_conf) {
            return 0;
        } else if ((*inout_conflicting)[position] > add_conf) {
            break;
        }
    }

    for (size_t i = 0, j = 0; j < *inout_conflicting_len; ++i, ++j) {
        if (i == position) {
            i++;
        }
        new_conflicting[i] = (*inout_conflicting)[j];
    }
    new_conflicting[position] = add_conf;

    for (size_t j = 0; j < *inout_conflicting_len + 1; ++j) {
        (*inout_conflicting)[j] = new_conflicting[j];
    }
    *inout_conflicting_len = *inout_conflicting_len + 1;
    return 0;
}

static void check_version_logic(
        anjay_iid_t iid_in_check,
        advanced_fw_update_logic_t *fw_table,
        anjay_iid_t (*inout_conflicting_instances)[FW_UPDATE_IID_IMAGE_SLOTS],
        size_t *inout_conflicting_instances_count) {
    if (iid_in_check == FW_UPDATE_IID_APP) {
        const char *app_pkg_ver = fw_update_common_get_pkg_version(
                fw_table[FW_UPDATE_IID_APP].iid, fw_table);
        const char *tee_cur_ver = fw_update_common_get_current_version(
                fw_table[FW_UPDATE_IID_TEE].iid, fw_table);
        /* Check major version, assuming that it is one digit len, first in ver
         * string */
        if (app_pkg_ver[0] > tee_cur_ver[0]) {
            add_conflicting(inout_conflicting_instances,
                            inout_conflicting_instances_count,
                            fw_table[FW_UPDATE_IID_TEE].iid);
        }
    }
}

int fw_update_common_finish(anjay_iid_t iid, void *fw_) {
    advanced_fw_update_logic_t *fw_table = (advanced_fw_update_logic_t *) fw_;
    int result = -1;
    result = stream_finish(iid, fw_);
    if (!result) {
        /* Below code checks two things:
         * 1. Relationship between instances in DOWNLOADED state
         *    and their linked instances with context of common_finish
         * 2. Version logic which is logic specific for targeted platform
         * Then it sets conflicting instances accordingly
         * */
        for (int i = 0; i < FW_UPDATE_IID_IMAGE_SLOTS; ++i) {
            if (is_state_downloaded(&fw_table[i])) {
                const anjay_iid_t *linked_instances;
                size_t linked_instances_count = 0;
                anjay_iid_t conflicting_instances[FW_UPDATE_IID_IMAGE_SLOTS];
                size_t conflicting_instances_count = 0;
                anjay_advanced_fw_update_get_linked_instances(
                        fw_table[i].anjay, fw_table[i].iid, &linked_instances,
                        &linked_instances_count);
                for (size_t j = 0; j < linked_instances_count; ++j) {
                    if (!is_state_downloaded(&fw_table[linked_instances[j]])) {
                        conflicting_instances[conflicting_instances_count++] =
                                linked_instances[j];
                    }
                }

                check_version_logic(fw_table[i].iid,
                                    fw_table,
                                    &conflicting_instances,
                                    &conflicting_instances_count);

                anjay_advanced_fw_update_set_conflicting_instances(
                        fw_table[i].anjay, fw_table[i].iid,
                        conflicting_instances, conflicting_instances_count);
            }
        }
    }
    return result;
}

void fw_update_common_reset(anjay_iid_t iid, void *fw_) {
    advanced_fw_update_logic_t *fw_table = (advanced_fw_update_logic_t *) fw_;
    advanced_fw_update_logic_t *fw = &fw_table[iid];
    if (fw->stream) {
        fclose(fw->stream);
        fw->stream = NULL;
    }
    maybe_delete_firmware_file(fw);
    advanced_firmware_update_delete_persistence_file(fw);
    anjay_advanced_fw_update_set_conflicting_instances(fw->anjay, fw->iid, NULL,
                                                       0);
    anjay_advanced_fw_update_set_linked_instances(fw->anjay, fw->iid, NULL, 0);
    if (fw->update_job) {
        avs_sched_del(&fw->update_job);
    }
    demo_log(INFO,
             "Reset done for instance: /" AVS_QUOTE_MACRO(
                     ANJAY_ADVANCED_FW_UPDATE_OID) "/%u",
             iid);

    /* Below code checks two things:
     * 1. Relationship between instances in DOWNLOADED state
     *    and their linked instances with context of common_reset
     * 2. Version logic which is logic specific for targeted platform
     * Then it sets conflicting instances accordingly
     * */
    for (int i = 0; i < FW_UPDATE_IID_IMAGE_SLOTS; ++i) {
        /* Reset could be called by anjay befor all instances are initialized.
         * Check of fw_table[i].anjay below makes sure that inst already
         * initialized */
        if (fw_table[i].anjay && is_state_downloaded(&fw_table[i])) {
            const anjay_iid_t *linked_instances;
            size_t linked_instances_count = 0;
            anjay_iid_t conflicting_instances[FW_UPDATE_IID_IMAGE_SLOTS];
            size_t conflicting_instances_count = 0;
            anjay_advanced_fw_update_get_linked_instances(
                    fw_table[i].anjay, fw_table[i].iid, &linked_instances,
                    &linked_instances_count);
            for (size_t j = 0; j < linked_instances_count; ++j) {
                if ((!is_state_downloaded(&fw_table[linked_instances[j]]))
                        || linked_instances[j] == fw->iid) {
                    conflicting_instances[conflicting_instances_count++] =
                            linked_instances[j];
                }
            }

            check_version_logic(fw_table[i].iid,
                                fw_table,
                                &conflicting_instances,
                                &conflicting_instances_count);

            anjay_advanced_fw_update_set_conflicting_instances(
                    fw_table[i].anjay, fw_table[i].iid, conflicting_instances,
                    conflicting_instances_count);
        }
    }
    if (fw->auto_suspend) {
        anjay_advanced_fw_update_pull_suspend(fw->anjay);
    }
}

int fw_update_common_perform_upgrade(
        anjay_iid_t iid,
        void *fw_,
        const anjay_iid_t *requested_supplemental_iids,
        size_t requested_supplemental_iids_count) {
    advanced_fw_update_logic_t *fw_table = (advanced_fw_update_logic_t *) fw_;
    advanced_fw_update_logic_t *fw = &fw_table[iid];
    const anjay_iid_t *conflicting_instances = NULL;
    size_t conflicting_instances_count;
    anjay_advanced_fw_update_get_conflicting_instances(
            fw->anjay, iid, &conflicting_instances,
            &conflicting_instances_count);
    if (conflicting_instances) {
        demo_log(ERROR,
                 "Trying to update /" AVS_QUOTE_MACRO(
                         ANJAY_ADVANCED_FW_UPDATE_OID) "/%u, but there are "
                                                       "conflicting "
                                                       "images",
                 fw->iid);
        return ANJAY_ADVANCED_FW_UPDATE_ERR_DEPENDENCY_ERROR;
    }

    const anjay_iid_t *update_with_iid = NULL;
    size_t update_with_iid_count = 0;
    if (requested_supplemental_iids) {
        demo_log(INFO, "Received supplemental iids");
        update_with_iid = requested_supplemental_iids;
        update_with_iid_count = requested_supplemental_iids_count;
    } else {
        const anjay_iid_t *linked_instances;
        size_t linked_instances_count;
        anjay_advanced_fw_update_get_linked_instances(
                fw->anjay, iid, &linked_instances, &linked_instances_count);
        if (linked_instances) {
            update_with_iid = linked_instances;
            update_with_iid_count = linked_instances_count;
        }
    }
    int result = 0;
    if (update_with_iid) {
        for (size_t i = 0; i < update_with_iid_count; ++i) {
            anjay_advanced_fw_update_set_state_and_result(
                    fw_table[update_with_iid[i]].anjay,
                    fw_table[update_with_iid[i]].iid,
                    ANJAY_ADVANCED_FW_UPDATE_STATE_UPDATING,
                    ANJAY_ADVANCED_FW_UPDATE_RESULT_INITIAL);
            assert(fw_table[update_with_iid[i]].check_yourself);
            if (fw_table[update_with_iid[i]].check_yourself(
                        &fw_table[update_with_iid[i]])) {
                result = -1;
            }
        }
    }
    assert(fw->check_yourself);
    if (fw->check_yourself(fw)) {
        result = -1;
    }
    if (result) {
        return result;
    }

    if (update_with_iid) {
        for (size_t i = 0; i < update_with_iid_count; ++i) {
            assert(fw_table[update_with_iid[i]].update_yourself);
            if ((result = fw_table[update_with_iid[i]].update_yourself(
                         &fw_table[update_with_iid[i]]))) {
                return result;
            }
        }
    }
    assert(fw->update_yourself);
    return fw->update_yourself(fw);
}

int fw_update_common_maybe_create_firmware_file(
        advanced_fw_update_logic_t *fw) {
    if (fw->next_target_path) {
        return 0;
    }
    if (fw->administratively_set_target_path) {
        fw->next_target_path = avs_strdup(fw->administratively_set_target_path);
    } else {
        fw->next_target_path = generate_random_target_filepath();
    }
    if (!fw->next_target_path) {
        return -1;
    }
    demo_log(INFO, "Created %s", fw->next_target_path);
    return 0;
}

static void afu_logic_destroy(advanced_fw_update_logic_t *fw) {
    assert(fw);
    if (fw->stream) {
        fclose(fw->stream);
    }
    if (fw->update_job) {
        avs_sched_del(&fw->update_job);
    }
    avs_free(fw->administratively_set_target_path);
    avs_free(fw->next_target_path);
}

const char *const ADD_IMG_NAMES[] = {
    [FW_UPDATE_IID_TEE] = "TEE",
    [FW_UPDATE_IID_BOOT] = "Bootloader",
    [FW_UPDATE_IID_MODEM] = "Modem"
};

int advanced_firmware_update_install(
        anjay_t *anjay,
        advanced_fw_update_logic_t *fw_table,
        const char *persistence_file,
        const avs_net_security_info_t *security_info,
        const avs_coap_udp_tx_params_t *tx_params,
        avs_time_duration_t tcp_request_timeout,
        anjay_advanced_fw_update_result_t delayed_result,
        bool prefer_same_socket_downloads,
        const char *original_img_file_path,
#ifdef ANJAY_WITH_SEND
        bool use_lwm2m_send,
#endif // ANJAY_WITH_SEND
        bool auto_suspend) {
    advanced_fw_update_logic_t *fw_logic_app = NULL;
    int result = -1;

    anjay_advanced_fw_update_global_config_t config = {
#ifdef ANJAY_WITH_SEND
        .use_lwm2m_send = use_lwm2m_send,
#endif // ANJAY_WITH_SEND
        .prefer_same_socket_downloads = prefer_same_socket_downloads
    };
    result = anjay_advanced_fw_update_install(anjay, &config);
    if (!result && !original_img_file_path) {
        demo_log(
                INFO,
                "Advanced Firmware Update init not finished. Lack of original "
                "image path, which is a path to file used to compare with file "
                "obtained from server during update.");
        /* Already installed object (by anjay_advanced_fw_update_install())
         * stays in demo and is not destroyed because some integration tests
         * (other than AFU) needs accordance between objects in demo and objects
         * defined in test_utils.py */
        return 0;
    }

    advanced_firmware_update_persistence_file_data_t data =
            advanced_firmware_update_read_persistence_file(persistence_file);

    if (!result) {
        fw_logic_app = &fw_table[FW_UPDATE_IID_APP];
        fw_logic_app->iid = FW_UPDATE_IID_APP;

        fw_logic_app->anjay = anjay;
        fw_logic_app->persistence_file = persistence_file;
        memcpy(fw_logic_app->current_ver, VER_DEFAULT, sizeof(VER_DEFAULT));

        advanced_firmware_update_delete_persistence_file(fw_logic_app);
        demo_log(
                INFO,
                "Initial state of firmware upgrade of instance "
                "/" AVS_QUOTE_MACRO(
                        ANJAY_ADVANCED_FW_UPDATE_OID) "/%u - "
                                                      "state: %d, result: %d, ",
                (int) fw_logic_app->iid,
                (int) data.states_results_paths.inst_states[FW_UPDATE_IID_APP],
                (int) data.states_results_paths
                        .inst_results[FW_UPDATE_IID_APP]);
        fw_logic_app->next_target_path =
                data.states_results_paths.next_target_paths[FW_UPDATE_IID_APP];
        data.states_results_paths.next_target_paths[FW_UPDATE_IID_APP] = NULL;
        anjay_advanced_fw_update_initial_state_t state = {
            .state = data.states_results_paths.inst_states[FW_UPDATE_IID_APP],
            .result = data.states_results_paths.inst_results[FW_UPDATE_IID_APP],
            .persisted_severity = data.severity,
            .persisted_last_state_change_time = data.last_state_change_time,
            .persisted_update_deadline = data.update_deadline
        };

        if (delayed_result != ANJAY_ADVANCED_FW_UPDATE_RESULT_INITIAL) {
            demo_log(INFO,
                     "delayed_result == %d; initializing Advanced Firmware "
                     "Update in UPDATING state",
                     (int) delayed_result);
            state.state = ANJAY_ADVANCED_FW_UPDATE_STATE_UPDATING;
            state.result = ANJAY_ADVANCED_FW_UPDATE_RESULT_INITIAL;

            // Simulate FOTA process that finishes after the LwM2M client starts
            // by changing the Update Result later at runtime
            set_delayed_advanced_fw_update_result_args_t args = {
                .anjay = anjay,
                .iid = FW_UPDATE_IID_APP,
                .delayed_result = delayed_result,
            };
            if (args.delayed_result
                    == ANJAY_ADVANCED_FW_UPDATE_RESULT_SUCCESS) {
                args.delayed_state = ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE;
            } else if (args.delayed_result
                       == ANJAY_ADVANCED_FW_UPDATE_RESULT_FAILED) {
                args.delayed_state = ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE;
            } else {
                demo_log(WARNING, "Other configurations should not occur.");
            }
            if (AVS_SCHED_NOW(anjay_get_scheduler(anjay), NULL,
                              set_delayed_fw_update_result, &args,
                              sizeof(args))) {
                result = -1;
                goto exit;
            }
        }

        if (state.state == ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADING) {
            if (!fw_logic_app->next_target_path
                    || !(fw_logic_app->stream =
                                 fopen(fw_logic_app->next_target_path, "ab"))) {
                if (fw_logic_app->stream) {
                    fclose(fw_logic_app->stream);
                    fw_logic_app->stream = NULL;
                }
                state.state = ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE;
            }
        } else if (state.state == ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE) {
            // we're initializing in the "Idle" state, so the firmware file is
            // not supposed to exist; delete it if we have it for any weird
            // reason
            maybe_delete_firmware_file(fw_logic_app);
        }
        result = advanced_firmware_update_application_install(
                anjay, fw_table, &state, security_info, tx_params,
                tcp_request_timeout, auto_suspend);
        if (result) {
            demo_log(ERROR, "AFU instance %u install failed",
                     FW_UPDATE_IID_APP);
            result = -1;
        }
    }

    for (anjay_iid_t i = FW_UPDATE_IID_TEE; i < FW_UPDATE_IID_IMAGE_SLOTS;
         ++i) {
        if (!result) {
            advanced_fw_update_logic_t *fw_logic_add_inst = &fw_table[i];
            fw_logic_add_inst->iid = i;
            fw_logic_add_inst->anjay = anjay;
            fw_logic_add_inst->original_img_file_path = original_img_file_path;
            anjay_advanced_fw_update_initial_state_t state = {
                .state = data.states_results_paths.inst_states[i],
                .result = data.states_results_paths.inst_results[i]
            };
            fw_logic_add_inst->next_target_path =
                    data.states_results_paths.next_target_paths[i];
            data.states_results_paths.next_target_paths[i] = NULL;
            demo_log(INFO,
                     "Initial state of firmware upgrade of instance "
                     "/" AVS_QUOTE_MACRO(
                             ANJAY_ADVANCED_FW_UPDATE_OID) "/%u - "
                                                           "state: %d, result: "
                                                           "%d, ",
                     (int) fw_logic_add_inst->iid, (int) state.state,
                     (int) state.result);
            result = advanced_firmware_update_additional_image_install(
                    anjay, fw_logic_add_inst->iid, fw_table, &state,
                    security_info, ADD_IMG_NAMES[i]);

            if (result) {
                demo_log(ERROR, "AFU instance %u install failed",
                         FW_UPDATE_IID_TEE);
                result = -1;
                break;
            }
        }
    }

    if (!result) {
        if (auto_suspend) {
            anjay_advanced_fw_update_pull_suspend(anjay);
        }
        demo_log(INFO, "AFU object install success");
    }

exit:
    if (result) {
        for (int i = 0; i < FW_UPDATE_IID_IMAGE_SLOTS; ++i) {
            afu_logic_destroy(&fw_table[i]);
            // If next_target_paths were read properly but some of
            // image_install() failed, there still could be need to free
            // allocated memory
            avs_free(data.states_results_paths.next_target_paths[i]);
        }
    }
    return result;
}

void advanced_firmware_update_set_package_path(
        advanced_fw_update_logic_t *fw_logic, const char *path) {
    if (fw_logic->stream) {
        demo_log(ERROR,
                 "cannot set package path while a download is in progress");
        return;
    }
    char *new_target_path = avs_strdup(path);
    if (!new_target_path) {
        demo_log(ERROR, "out of memory");
        return;
    }

    avs_free(fw_logic->administratively_set_target_path);
    fw_logic->administratively_set_target_path = new_target_path;
    demo_log(INFO, "firmware package path set to %s",
             fw_logic->administratively_set_target_path);
}

void advanced_firmware_update_uninstall(advanced_fw_update_logic_t *fw_table) {
    for (int i = 0; i < FW_UPDATE_IID_IMAGE_SLOTS; ++i) {
        afu_logic_destroy(&fw_table[i]);
    }
}

int advanced_firmware_update_get_security_config(
        anjay_iid_t iid,
        void *fw_,
        anjay_security_config_t *out_security_config,
        const char *download_uri) {
    (void) iid;
    advanced_fw_update_logic_t *fw_table = (advanced_fw_update_logic_t *) fw_;
    advanced_fw_update_logic_t *fw = &fw_table[FW_UPDATE_IID_APP];
    (void) download_uri;
    memset(out_security_config, 0, sizeof(*out_security_config));
    out_security_config->security_info = fw->security_info;
    return 0;
}
