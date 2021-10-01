#include <anjay/anjay.h>
#include <anjay/download.h>
#include <avsystem/commons/avs_log.h>

#include <stdio.h>
#include <string.h>

// This example uses hard-coded file path for simplicity.
static const char DOWNLOAD_TARGET[] = "/tmp/coap-download";

static avs_error_t coap_write_block(anjay_t *anjay,
                                    const uint8_t *data,
                                    size_t data_size,
                                    const anjay_etag_t *etag,
                                    void *file_) {
    (void) anjay;
    // ETag value can be saved to allow resuming the download later, in case
    // where it could be interrupted at any point.
    //
    // To resume the download, one can pass `etag` and `start_offset` values
    // in @ref anjay_download_config_t . If the downloaded file is still
    // available, and its ETag did not change, the download will proceed as if
    // no interruption happened.
    //
    // This example ignores ETag value for simplicity.
    (void) etag;

    FILE *file = (FILE *) file_;
    if (fwrite(data, data_size, 1, file) != 1) {
        avs_log(tutorial, ERROR, "could not write file");
        return avs_errno(AVS_EIO);
    }

    return AVS_OK;
}

static void coap_download_finished(anjay_t *anjay,
                                   anjay_download_status_t status,
                                   void *file_) {
    (void) anjay;

    FILE *file = (FILE *) file_;
    fclose(file);

    if (status.result == ANJAY_DOWNLOAD_FINISHED) {
        avs_log(tutorial, INFO, "download complete: %s", DOWNLOAD_TARGET);
    } else {
        avs_log(tutorial, ERROR, "download failed: result = %d",
                (int) status.result);
        remove(DOWNLOAD_TARGET);
    }
}

static int request_coap_download(anjay_t *anjay,
                                 const char *url,
                                 const char *psk_identity,
                                 const char *psk_key) {
    FILE *file = fopen(DOWNLOAD_TARGET, "wb");
    if (!file) {
        avs_log(tutorial, ERROR, "could not open file %s for writing",
                DOWNLOAD_TARGET);
        return -1;
    }

    avs_net_psk_info_t psk = {
        .psk = psk_key,
        .psk_size = strlen(psk_key),
        .identity = psk_identity,
        .identity_size = strlen(psk_identity)
    };

    anjay_download_config_t cfg = {
        .url = url,
        .on_next_block = coap_write_block,
        .on_download_finished = coap_download_finished,
        .user_data = file,
        .security_config = {
            .security_info = avs_net_security_info_from_psk(psk)
        }
    };

    anjay_download_handle_t handle;
    if (avs_is_err(anjay_download(anjay, &cfg, &handle))) {
        avs_log(tutorial, ERROR, "could not schedule download: %s", url);
        // In case of anjay_download failure, cfg.user_data needs to be freed
        fclose(file);
        return -1;
    }

    // After anjay_download succeeds, cfg.on_download_finished is guaranteed
    // to be called at some point. If cfg.user_data requires some cleanup or
    // deallocation, it should be done in on_download_finished handler.
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        avs_log(tutorial, ERROR, "usage: %s ENDPOINT_NAME", argv[0]);
        return -1;
    }

    const anjay_configuration_t CONFIG = {
        .endpoint_name = argv[1],
        .in_buffer_size = 4000,
        .out_buffer_size = 4000
    };

    anjay_t *anjay = anjay_new(&CONFIG);
    if (!anjay) {
        avs_log(tutorial, ERROR, "Could not create Anjay object");
        return -1;
    }
    int result = 0;

    // For simplicity, no LwM2M objects are installed. This application is
    // unable to handle any LwM2M traffic.

    if (request_coap_download(anjay, "coaps://try-anjay.avsystem.com:5684/file",
                              "psk_identity", "psk_key")) {
        result = -1;
        goto cleanup;
    }

    result = anjay_event_loop_run(anjay,
                                  avs_time_duration_from_scalar(1, AVS_TIME_S));

cleanup:
    anjay_delete(anjay);
    return result;
}
