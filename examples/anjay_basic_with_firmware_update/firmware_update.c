/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#define _DEFAULT_SOURCE

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <anj/sdm_fw_update.h>
#include <anj/sdm_io.h>

#include "firmware_update.h"

static const char *FW_IMAGE_DOWNLOAD_NAME = "/tmp/firmware_image.bin";
static const char *FW_UPDATED_MARKER = "/tmp/fw-updated-marker";

typedef struct {
    const char *endpoint_name;
    const char *firmware_version;
    FILE *firmware_file;
    size_t offset;
    bool waiting_for_reboot;
} firmware_update_t;

static firmware_update_t firmware_update;

static sdm_fw_update_result_t fu_write_start(void *user_ptr) {
    firmware_update_t *fu = (firmware_update_t *) user_ptr;

    assert(fu->firmware_file == NULL);
    fu->firmware_file = fopen(FW_IMAGE_DOWNLOAD_NAME, "wb");
    if (!fu->firmware_file) {
        printf("Could not open %s\n", FW_IMAGE_DOWNLOAD_NAME);
        return SDM_FW_UPDATE_RESULT_FAILED;
    }
    printf("Firmware download begins\n");
    return SDM_FW_UPDATE_RESULT_SUCCESS;
}

static sdm_fw_update_result_t
fu_write(void *user_ptr, void *data, size_t data_size) {
    firmware_update_t *fu = (firmware_update_t *) user_ptr;
    assert(fu->firmware_file != NULL);

    printf("Writing %lu bytes with %lu offset\n", data_size, fu->offset);
    fu->offset += data_size;
    if (fwrite(data, data_size, 1, fu->firmware_file) != 1) {
        printf("Writing to firmware image failed\n");
        return SDM_FW_UPDATE_RESULT_FAILED;
    }
    return SDM_FW_UPDATE_RESULT_SUCCESS;
}

static sdm_fw_update_result_t fu_write_finish(void *user_ptr) {
    firmware_update_t *fu = (firmware_update_t *) user_ptr;
    assert(fu->firmware_file != NULL);

    if (fclose(fu->firmware_file)) {
        printf("Closing firmware image failed\n");
        fu->firmware_file = NULL;
        return SDM_FW_UPDATE_RESULT_FAILED;
    }
    fu->offset = 0;
    fu->firmware_file = NULL;
    printf("Firmware download ends\n");
    return SDM_FW_UPDATE_RESULT_SUCCESS;
}

static sdm_fw_update_result_t fu_update_start(void *user_ptr) {
    (void) user_ptr;
    printf("fu_update_start\n");
    firmware_update.waiting_for_reboot = true;
    return SDM_FW_UPDATE_RESULT_SUCCESS;
}

static void fu_reset(void *user_ptr) {
    firmware_update_t *fu = (firmware_update_t *) user_ptr;
    printf("fu_reset\n");
    if (fu->firmware_file) {
        (void) fclose(fu->firmware_file);
        fu->firmware_file = NULL;
    }
    unlink(FW_IMAGE_DOWNLOAD_NAME);
}

static const char *fu_get_version(void *user_ptr) {
    firmware_update_t *fu = (firmware_update_t *) user_ptr;
    printf("fu_get_version\n");
    return fu->firmware_version;
}

static sdm_fw_update_handlers_t fu_handlers = {
    .package_write_start_handler = fu_write_start,
    .package_write_handler = fu_write,
    .package_write_finish_handler = fu_write_finish,
    .update_start_handler = fu_update_start,
    .reset_handler = fu_reset,
    .get_version = fu_get_version
};

void fw_update_check(void) {
    if (firmware_update.waiting_for_reboot) {
        printf("perform reset\n");
        firmware_update.waiting_for_reboot = false;
        if (chmod(FW_IMAGE_DOWNLOAD_NAME, 0700) == -1) {
            printf("Could not make firmware executable: %s\n", strerror(errno));
            return;
        }
        FILE *marker = fopen(FW_UPDATED_MARKER, "w");
        if (!marker) {
            printf("Marker file could not be created\n");
            return;
        }
        fclose(marker);

        (void) execl(FW_IMAGE_DOWNLOAD_NAME,
                     FW_IMAGE_DOWNLOAD_NAME,
                     firmware_update.endpoint_name,
                     NULL);
        printf("execl() failed: %s\n", strerror(errno));
        unlink(FW_UPDATED_MARKER);
    }
}

int fw_update_object_install(sdm_data_model_t *dm,
                             const char *firmware_version,
                             const char *endpoint_name) {

    firmware_update.firmware_version = firmware_version;
    firmware_update.endpoint_name = endpoint_name;
    firmware_update.waiting_for_reboot = false;

    if (sdm_fw_update_object_install(dm,
                                     &fu_handlers,
                                     &firmware_update,
                                     (uint8_t) SDM_FW_UPDATE_PROTOCOL_COAP)) {
        return -1;
    }

    if (access(FW_UPDATED_MARKER, F_OK) != -1) {
        printf("firmware update succeded\n");
        unlink(FW_UPDATED_MARKER);
        return sdm_fw_update_object_set_update_result(
                SDM_FW_UPDATE_RESULT_SUCCESS);
    }

    return 0;
}
