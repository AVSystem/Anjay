#define _DEFAULT_SOURCE // for fileno()
#include "./firmware_update.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    char *persisted_uri;
    uint32_t resume_offset;
    anjay_etag_t *resume_etag;
} download_state_t;

static const char *FW_DOWNLOAD_STATE_NAME = "firmware_dl_state.bin";

static int store_etag(FILE *fp, const anjay_etag_t *etag) {
    assert(etag);
    assert(etag->size > 0);
    if (fwrite(&etag->size, sizeof(etag->size), 1, fp) != 1
            || fwrite(etag->value, etag->size, 1, fp) != 1) {
        return -1;
    }
    return 0;
}

static int store_download_state(const download_state_t *state) {
    FILE *fp = fopen(FW_DOWNLOAD_STATE_NAME, "wb");
    if (!fp) {
        fprintf(stderr, "could not open %s for writing\n",
                FW_DOWNLOAD_STATE_NAME);
        return -1;
    }
    const uint16_t uri_length = strlen(state->persisted_uri);
    int result = 0;
    if (fwrite(&uri_length, sizeof(uri_length), 1, fp) != 1
            || fwrite(state->persisted_uri, uri_length, 1, fp) != 1
            || fwrite(&state->resume_offset, sizeof(state->resume_offset), 1,
                      fp) != 1
            || store_etag(fp, state->resume_etag)) {
        fprintf(stderr, "could not write firmware download state\n");
        result = -1;
    }
    fclose(fp);
    if (result) {
        unlink(FW_DOWNLOAD_STATE_NAME);
    }
    return result;
}

static int restore_etag(FILE *fp, anjay_etag_t **out_etag) {
    assert(out_etag && !*out_etag); // make sure out_etag is zero-initialized
    uint8_t size;
    if (fread(&size, sizeof(size), 1, fp) != 1 || size == 0) {
        return -1;
    }
    anjay_etag_t *etag = anjay_etag_new(size);
    if (!etag) {
        return -1;
    }

    if (fread(etag->value, size, 1, fp) != 1) {
        avs_free(etag);
        return -1;
    }
    *out_etag = etag;
    return 0;
}

static int restore_download_state(download_state_t *out_state) {
    download_state_t data;
    memset(&data, 0, sizeof(data));

    FILE *fp = fopen(FW_DOWNLOAD_STATE_NAME, "rb");
    if (!fp) {
        fprintf(stderr, "could not open %s for reading\n",
                FW_DOWNLOAD_STATE_NAME);
        return -1;
    }

    int result = 0;
    uint16_t uri_length;
    if (fread(&uri_length, sizeof(uri_length), 1, fp) != 1 || uri_length == 0) {
        result = -1;
    }
    if (!result) {
        data.persisted_uri = (char *) avs_calloc(1, uri_length + 1);
        if (!data.persisted_uri) {
            result = -1;
        }
    }
    if (!result
            && (fread(data.persisted_uri, uri_length, 1, fp) != 1
                || fread(&data.resume_offset, sizeof(data.resume_offset), 1, fp)
                           != 1
                || restore_etag(fp, &data.resume_etag))) {
        result = -1;
    }
    if (result) {
        fprintf(stderr, "could not restore download state from %s\n",
                FW_DOWNLOAD_STATE_NAME);
        avs_free(data.persisted_uri);
    } else {
        *out_state = data;
    }
    fclose(fp);
    return result;
}

static void reset_download_state(download_state_t *state) {
    avs_free(state->persisted_uri);
    avs_free(state->resume_etag);
    memset(state, 0, sizeof(*state));
    unlink(FW_DOWNLOAD_STATE_NAME);
}

static struct fw_state_t {
    FILE *firmware_file;
    // anjay instance this firmware update singleton is associated with
    anjay_t *anjay;
    // Current state of the download. It is updated and persited on each
    // fw_stream_write() call.
    download_state_t download_state;
} FW_STATE;

static const char *FW_IMAGE_DOWNLOAD_NAME = "/tmp/firmware_image.bin";

static int fw_open_download_file(long seek_offset) {
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
    if (fseek(FW_STATE.firmware_file, seek_offset, SEEK_SET)) {
        fprintf(stderr, "Could not seek to %ld\n", seek_offset);
        fclose(FW_STATE.firmware_file);
        FW_STATE.firmware_file = NULL;
        return -1;
    }
    // We've succeeded
    return 0;
}

static int fw_stream_open(void *user_ptr,
                          const char *package_uri,
                          const struct anjay_etag *package_etag) {
    // We don't use user_ptr.
    (void) user_ptr;

    // We only persist firmware download state if we have both package_uri
    // and package_etag. Otherwise the download could not be resumed.
    if (package_uri && package_etag) {
        FW_STATE.download_state.persisted_uri = avs_strdup(package_uri);
        int result = 0;
        if (!FW_STATE.download_state.persisted_uri) {
            fprintf(stderr, "Could not duplicate package URI\n");
            result = -1;
        }
        anjay_etag_t *etag_copy = NULL;
        if (!result && package_etag) {
            etag_copy = anjay_etag_clone(package_etag);
            if (!etag_copy) {
                fprintf(stderr, "Could not duplicate package ETag\n");
                result = -1;
            }
        }
        if (!result) {
            FW_STATE.download_state.resume_etag = etag_copy;
        } else {
            reset_download_state(&FW_STATE.download_state);
            return result;
        }
    }

    return fw_open_download_file(0);
}

static int fw_stream_write(void *user_ptr, const void *data, size_t length) {
    (void) user_ptr;
    // NOTE: fflush() and fsync() are done to be relatively sure that
    // the data is passed to the hardware and so that we can update
    // resume_offset in the download state. They are suboptimal on UNIX-like
    // platforms, and are used just to illustrate when is the right time to
    // update resume_offset on embedded platforms.
    if (fwrite(data, length, 1, FW_STATE.firmware_file) != 1
            || fflush(FW_STATE.firmware_file)
            || fsync(fileno(FW_STATE.firmware_file))) {
        fprintf(stderr, "Writing to firmware image failed\n");
        return -1;
    }
    if (FW_STATE.download_state.persisted_uri) {
        FW_STATE.download_state.resume_offset += length;
        if (store_download_state(&FW_STATE.download_state)) {
            // If we returned -1 here, the download would be aborted, so it
            // is probably better to continue instead.
            fprintf(stderr,
                    "Could not store firmware download state - ignoring\n");
        }
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
    // And reset any download state.
    reset_download_state(&FW_STATE.download_state);
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

static int fw_get_security_config(void *user_ptr,
                                  anjay_security_config_t *out_security_info,
                                  const char *download_uri) {
    (void) user_ptr;
    if (!anjay_security_config_from_dm(FW_STATE.anjay, out_security_info,
                                       download_uri)) {
        // found a match
        return 0;
    }

    // no match found, fallback to loading certificates from given paths
    memset(out_security_info, 0, sizeof(*out_security_info));
    const avs_net_certificate_info_t cert_info = {
        .server_cert_validation = true,
        .trusted_certs =
                avs_crypto_certificate_chain_info_from_path("./certs/CA.crt"),
        .client_cert = avs_crypto_certificate_chain_info_from_path(
                "./certs/client.crt"),
        .client_key = avs_crypto_private_key_info_from_file(
                "./certs/client.key", NULL)
    };
    // NOTE: this assignment is safe, because cert_info contains pointers to
    // string literals only. If the configuration were to load certificate info
    // from buffers they would have to be stored somewhere - e.g. on the heap.
    out_security_info->security_info =
            avs_net_security_info_from_certificates(cert_info);
    return 0;
}

static const anjay_fw_update_handlers_t HANDLERS = {
    .stream_open = fw_stream_open,
    .stream_write = fw_stream_write,
    .stream_finish = fw_stream_finish,
    .reset = fw_reset,
    .perform_upgrade = fw_perform_upgrade,
    .get_security_config = fw_get_security_config
};

const char *ENDPOINT_NAME = NULL;

int fw_update_install(anjay_t *anjay) {
    anjay_fw_update_initial_state_t state;
    memset(&state, 0, sizeof(state));

    if (access(FW_UPDATED_MARKER, F_OK) != -1) {
        // marker file exists, it means firmware update succeded!
        state.result = ANJAY_FW_UPDATE_INITIAL_SUCCESS;
        unlink(FW_UPDATED_MARKER);
        // we can get rid of any download state if the update succeeded
        reset_download_state(&FW_STATE.download_state);
    } else if (!restore_download_state(&FW_STATE.download_state)) {
        // download state restored, it means we can try using download
        // resumption
        if (fw_open_download_file(state.resume_offset)) {
            // the file cannot be opened or seeking failed
            reset_download_state(&FW_STATE.download_state);
        } else {
            state.persisted_uri = FW_STATE.download_state.persisted_uri;
            state.resume_offset = FW_STATE.download_state.resume_offset;
            state.resume_etag = FW_STATE.download_state.resume_etag;
            state.result = ANJAY_FW_UPDATE_INITIAL_DOWNLOADING;
        }
    }
    // make sure this module is installed for single Anjay instance only
    assert(FW_STATE.anjay == NULL);
    FW_STATE.anjay = anjay;
    // install the module, pass handlers that we implemented and initial state
    // that we discovered upon startup
    return anjay_fw_update_install(anjay, &HANDLERS, NULL, &state);
}
