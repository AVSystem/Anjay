#include <anjay/anjay.h>
#include <anjay/attr_storage.h>
#include <anjay/security.h>
#include <anjay/server.h>
#include <avsystem/commons/avs_log.h>
#include <avsystem/commons/avs_stream_file.h>

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

static anjay_t *volatile g_anjay;

void signal_handler(int signum) {
    if (signum == SIGINT && g_anjay) {
        anjay_event_loop_interrupt(g_anjay);
    }
}

#define PERSISTENCE_FILENAME "anjay-est-persistence.dat"

int persist_objects_if_necessary(anjay_t *anjay) {
    if ((!anjay_security_object_is_modified(anjay)
         && !anjay_server_object_is_modified(anjay)
         && !anjay_attr_storage_is_modified(anjay))
            || !anjay_est_state_is_ready_for_persistence(anjay)) {
        avs_log(tutorial, INFO,
                "Persistence not necessary - NOT persisting objects");
        return 0;
    }

    avs_log(tutorial, INFO, "Persisting objects to %s", PERSISTENCE_FILENAME);

    avs_stream_t *file_stream =
            avs_stream_file_create(PERSISTENCE_FILENAME, AVS_STREAM_FILE_WRITE);

    if (!file_stream) {
        avs_log(tutorial, ERROR, "Could not open file for writing");
        return -1;
    }

    int result = -1;

    if (avs_is_err(anjay_security_object_persist(anjay, file_stream))) {
        avs_log(tutorial, ERROR, "Could not persist Security Object");
        goto finish;
    }

    if (avs_is_err(anjay_server_object_persist(anjay, file_stream))) {
        avs_log(tutorial, ERROR, "Could not persist Server Object");
        goto finish;
    }

    if (avs_is_err(anjay_attr_storage_persist(anjay, file_stream))) {
        avs_log(tutorial, ERROR, "Could not persist LwM2M attribute storage");
        goto finish;
    }

    if (avs_is_err(anjay_est_state_persist(anjay, file_stream))) {
        avs_log(tutorial, ERROR, "Could not persist EST state");
        goto finish;
    }

    result = 0;
finish:
    avs_stream_cleanup(&file_stream);
    return result;
}

int restore_objects_if_possible(anjay_t *anjay) {
    avs_log(tutorial, INFO, "Attempting to restore objects from persistence");
    int result;

    errno = 0;
    if ((result = access(PERSISTENCE_FILENAME, F_OK))) {
        switch (errno) {
        case ENOENT:
        case ENOTDIR:
            // no persistence file means there is nothing to restore
            return 1;
        default:
            // some other unpredicted error
            return result;
        }
    } else if ((result = access(PERSISTENCE_FILENAME, R_OK))) {
        // most likely file is just not readable
        return result;
    }

    avs_stream_t *file_stream =
            avs_stream_file_create(PERSISTENCE_FILENAME, AVS_STREAM_FILE_READ);

    if (!file_stream) {
        return -1;
    }

    result = -1;

    if (avs_is_err(anjay_security_object_restore(anjay, file_stream))) {
        avs_log(tutorial, ERROR, "Could not restore Security Object");
        goto finish;
    }

    if (avs_is_err(anjay_server_object_restore(anjay, file_stream))) {
        avs_log(tutorial, ERROR, "Could not restore Server Object");
        goto finish;
    }

    if (avs_is_err(anjay_attr_storage_restore(anjay, file_stream))) {
        avs_log(tutorial, ERROR, "Could not restore LwM2M attribute storage");
        goto finish;
    }

    if (avs_is_err(anjay_est_state_restore(anjay, file_stream))) {
        avs_log(tutorial, ERROR, "Could not restore EST state");
        goto finish;
    }

    result = 0;
finish:
    avs_stream_cleanup(&file_stream);
    return result;
}

static int
load_buffer_from_file(uint8_t **out, size_t *out_size, const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        avs_log(tutorial, ERROR, "could not open %s", filename);
        return -1;
    }
    int result = -1;
    if (fseek(f, 0, SEEK_END)) {
        goto finish;
    }
    long size = ftell(f);
    if (size < 0 || (unsigned long) size > SIZE_MAX || fseek(f, 0, SEEK_SET)) {
        goto finish;
    }
    *out_size = (size_t) size;
    if (!(*out = (uint8_t *) avs_malloc(*out_size))) {
        goto finish;
    }
    if (fread(*out, *out_size, 1, f) != 1) {
        avs_free(*out);
        *out = NULL;
        goto finish;
    }
    result = 0;
finish:
    fclose(f);
    if (result) {
        avs_log(tutorial, ERROR, "could not read %s", filename);
    }
    return result;
}

static int initialize_objects_with_default_settings(anjay_t *anjay) {
    anjay_security_instance_t security_instance = {
        .bootstrap_server = true,
        .server_uri = "coaps://eu.iot.avsystem.cloud:5694",
        .security_mode = ANJAY_SECURITY_EST
    };

    int result = 0;

    if (load_buffer_from_file(
                (uint8_t **) &security_instance.public_cert_or_psk_identity,
                &security_instance.public_cert_or_psk_identity_size,
                "client_cert.der")
            || load_buffer_from_file(
                       (uint8_t **) &security_instance.private_cert_or_psk_key,
                       &security_instance.private_cert_or_psk_key_size,
                       "client_key.der")
            || load_buffer_from_file(
                       (uint8_t **) &security_instance.server_public_key,
                       &security_instance.server_public_key_size,
                       "server_cert.der")) {
        result = -1;
        goto cleanup;
    }

    // Anjay will assign Instance ID automatically
    anjay_iid_t security_instance_id = ANJAY_ID_INVALID;
    if (anjay_security_object_add_instance(anjay, &security_instance,
                                           &security_instance_id)) {
        result = -1;
    }

cleanup:
    avs_free((uint8_t *) security_instance.public_cert_or_psk_identity);
    avs_free((uint8_t *) security_instance.private_cert_or_psk_key);
    avs_free((uint8_t *) security_instance.server_public_key);

    return result;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        avs_log(tutorial, ERROR, "usage: %s ENDPOINT_NAME", argv[0]);
        return -1;
    }

    signal(SIGINT, signal_handler);

    const anjay_configuration_t CONFIG = {
        .endpoint_name = argv[1],
        .in_buffer_size = 4000,
        .out_buffer_size = 4000,
        .msg_cache_size = 4000,

        .trust_store_certs = avs_crypto_certificate_chain_info_from_file(
                "/etc/ssl/certs/ca-certificates.crt"),
        .est_reenroll_config = &(const anjay_est_reenroll_config_t) {
            .enable = true,
            .nominal_usage = 0.8,
            .max_margin = avs_time_duration_from_scalar(7, AVS_TIME_DAY)
        },
        .est_cacerts_policy = ANJAY_EST_CACERTS_FOR_EST_SECURITY
    };

    g_anjay = anjay_new(&CONFIG);
    if (!g_anjay) {
        avs_log(tutorial, ERROR, "Could not create Anjay object");
        return -1;
    }

    int result = 0;
    // Setup necessary objects
    if (anjay_security_object_install(g_anjay)
            || anjay_server_object_install(g_anjay)) {
        result = -1;
        goto cleanup;
    }

    int restore_retval = restore_objects_if_possible(g_anjay);
    if (restore_retval < 0
            || (restore_retval > 0
                && initialize_objects_with_default_settings(g_anjay))) {
        result = -1;
        goto cleanup;
    }

    result = anjay_event_loop_run(g_anjay,
                                  avs_time_duration_from_scalar(1, AVS_TIME_S));

    int persist_result = persist_objects_if_necessary(g_anjay);
    if (!result) {
        result = persist_result;
    }

cleanup:
    anjay_delete(g_anjay);
    return result;
}
