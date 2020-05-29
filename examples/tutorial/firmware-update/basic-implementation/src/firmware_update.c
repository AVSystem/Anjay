#include "./firmware_update.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

static struct fw_state_t { FILE *firmware_file; } FW_STATE;

static const char *FW_IMAGE_DOWNLOAD_NAME = "/tmp/firmware_image.bin";

static int fw_stream_open(void *user_ptr,
                          const char *package_uri,
                          const struct anjay_etag *package_etag) {
    // For a moment, we don't need to care about any of the arguments passed.
    (void) user_ptr;
    (void) package_uri;
    (void) package_etag;

    // It's worth ensuring we start with a NULL firmware_file. In the end
    // it would be our responsibility to manage this pointer, and we want
    // to make sure we never leak any memory.
    assert(FW_STATE.firmware_file == NULL);
    // We're about to create a firmware file for writing
    FW_STATE.firmware_file = fopen(FW_IMAGE_DOWNLOAD_NAME, "wb");
    if (!FW_STATE.firmware_file) {
        fprintf(stderr, "Could not open %s\n", FW_IMAGE_DOWNLOAD_NAME);
        return -1;
    }
    // We've succeeded
    return 0;
}

static int fw_stream_write(void *user_ptr, const void *data, size_t length) {
    (void) user_ptr;
    // We only need to write to file and check if that succeeded
    if (fwrite(data, length, 1, FW_STATE.firmware_file) != 1) {
        fprintf(stderr, "Writing to firmware image failed\n");
        return -1;
    }
    return 0;
}

static int fw_stream_finish(void *user_ptr) {
    (void) user_ptr;
    assert(FW_STATE.firmware_file != NULL);

    if (fclose(FW_STATE.firmware_file)) {
        fprintf(stderr, "Closing firmware image failed\n");
        FW_STATE.firmware_file = NULL;
        return -1;
    }
    FW_STATE.firmware_file = NULL;
    return 0;
}

static void fw_reset(void *user_ptr) {
    // Reset can be issued even if the download never started.
    if (FW_STATE.firmware_file) {
        // We ignore the result code of fclose(), as fw_reset() can't fail.
        (void) fclose(FW_STATE.firmware_file);
        // and reset our global state to initial value.
        FW_STATE.firmware_file = NULL;
    }
    // Finally, let's remove any downloaded payload
    unlink(FW_IMAGE_DOWNLOAD_NAME);
}

// A part of a rather simple logic checking if the firmware update was
// successfully performed.
static const char *FW_UPDATED_MARKER = "/tmp/fw-updated-marker";

static int fw_perform_upgrade(void *user_ptr) {
    if (chmod(FW_IMAGE_DOWNLOAD_NAME, 0700) == -1) {
        fprintf(stderr,
                "Could not make firmware executable: %s\n",
                strerror(errno));
        return -1;
    }
    // Create a marker file, so that the new process knows it is the "upgraded"
    // one
    FILE *marker = fopen(FW_UPDATED_MARKER, "w");
    if (!marker) {
        fprintf(stderr, "Marker file could not be created\n");
        return -1;
    }
    fclose(marker);

    assert(ENDPOINT_NAME);
    // If the call below succeeds, the firmware is considered as "upgraded",
    // and we hope the newly started client registers to the Server.
    (void) execl(FW_IMAGE_DOWNLOAD_NAME, FW_IMAGE_DOWNLOAD_NAME, ENDPOINT_NAME,
                 NULL);
    fprintf(stderr, "execl() failed: %s\n", strerror(errno));
    // If we are here, it means execl() failed. Marker file MUST now be removed,
    // as the firmware update failed.
    unlink(FW_UPDATED_MARKER);
    return -1;
}

static const anjay_fw_update_handlers_t HANDLERS = {
    .stream_open = fw_stream_open,
    .stream_write = fw_stream_write,
    .stream_finish = fw_stream_finish,
    .reset = fw_reset,
    .perform_upgrade = fw_perform_upgrade
};

const char *ENDPOINT_NAME = NULL;

int fw_update_install(anjay_t *anjay) {
    anjay_fw_update_initial_state_t state;
    memset(&state, 0, sizeof(state));

    if (access(FW_UPDATED_MARKER, F_OK) != -1) {
        // marker file exists, it means firmware update succeded!
        state.result = ANJAY_FW_UPDATE_INITIAL_SUCCESS;
        unlink(FW_UPDATED_MARKER);
    }
    // install the module, pass handlers that we implemented and initial state
    // that we discovered upon startup
    return anjay_fw_update_install(anjay, &HANDLERS, NULL, &state);
}
