#include <anjay/anjay.h>
#include <anjay/attr_storage.h>
#include <anjay/security.h>
#include <anjay/server.h>
#include <avsystem/commons/avs_log.h>
#include <avsystem/commons/avs_stream_file.h>

#include <errno.h>
#include <signal.h>
#include <unistd.h>

static anjay_t *volatile g_anjay;

void signal_handler(int signum) {
    if (signum == SIGINT && g_anjay) {
        anjay_event_loop_interrupt(g_anjay);
    }
}

#define OBJECT_PERSISTENCE_FILENAME "cf-object-persistence.dat"
#define CORE_PERSISTENCE_FILENAME "cf-core-persistence.dat"

int persist_objects(anjay_t *anjay) {
    avs_log(tutorial, INFO,
            "Persisting objects to " OBJECT_PERSISTENCE_FILENAME);

    avs_stream_t *file_stream =
            avs_stream_file_create(OBJECT_PERSISTENCE_FILENAME,
                                   AVS_STREAM_FILE_WRITE);

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

    result = 0;
finish:
    avs_stream_cleanup(&file_stream);
    return result;
}

int restore_objects_if_possible(anjay_t *anjay) {
    avs_log(tutorial, INFO, "Attempting to restore objects from persistence");
    int result;

    errno = 0;
    if ((result = access(OBJECT_PERSISTENCE_FILENAME, F_OK))) {
        switch (errno) {
        case ENOENT:
        case ENOTDIR:
            // no persistence file means there is nothing to restore
            return 1;
        default:
            // some other unpredicted error
            return result;
        }
    } else if ((result = access(OBJECT_PERSISTENCE_FILENAME, R_OK))) {
        // most likely file is just not readable
        return result;
    }

    avs_stream_t *file_stream =
            avs_stream_file_create(OBJECT_PERSISTENCE_FILENAME,
                                   AVS_STREAM_FILE_READ);

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

    result = 0;
finish:
    avs_stream_cleanup(&file_stream);
    return result;
}

anjay_t *
anjay_new_try_from_core_persistence(const anjay_configuration_t *config) {
    avs_log(tutorial, INFO,
            "Attempting to initialize Anjay from core persistence");

    avs_stream_t *file_stream =
            avs_stream_file_create(CORE_PERSISTENCE_FILENAME,
                                   AVS_STREAM_FILE_READ);

    anjay_t *result;
    if (!file_stream
            || !(result = anjay_new_from_core_persistence(config,
                                                          file_stream))) {
        result = anjay_new(config);
    }

    avs_stream_cleanup(&file_stream);
    // remove persistence file to prevent client from reading
    // outdated state in case it doesn't shut down gracefully
    unlink(CORE_PERSISTENCE_FILENAME);
    return result;
}

int anjay_delete_try_with_core_persistence(anjay_t *anjay) {
    avs_log(tutorial, INFO,
            "Attempting to shut down Anjay and persist its state");

    avs_stream_t *file_stream =
            avs_stream_file_create(CORE_PERSISTENCE_FILENAME,
                                   AVS_STREAM_FILE_WRITE);
    if (file_stream) {
        int result = anjay_delete_with_core_persistence(anjay, file_stream);
        avs_stream_cleanup(&file_stream);
        if (result) {
            unlink(CORE_PERSISTENCE_FILENAME);
        }
        return result;
    } else {
        anjay_delete(anjay);
        return -1;
    }
}

void initialize_objects_with_default_settings(anjay_t *anjay) {
    static const char PSK_IDENTITY[] = "identity";
    static const char PSK_KEY[] = "P4s$w0rd";

    const anjay_security_instance_t security_instance = {
        .ssid = 1,
        .server_uri = "coaps://eu.iot.avsystem.cloud:5684",
        .security_mode = ANJAY_SECURITY_PSK,
        .public_cert_or_psk_identity = (const uint8_t *) PSK_IDENTITY,
        .public_cert_or_psk_identity_size = strlen(PSK_IDENTITY),
        .private_cert_or_psk_key = (const uint8_t *) PSK_KEY,
        .private_cert_or_psk_key_size = strlen(PSK_KEY)
    };

    const anjay_server_instance_t server_instance = {
        .ssid = 1,
        .lifetime = 60,
        .default_min_period = -1,
        .default_max_period = -1,
        .disable_timeout = -1,
        .binding = "U"
    };

    anjay_iid_t security_instance_id = ANJAY_ID_INVALID;
    anjay_iid_t server_instance_id = ANJAY_ID_INVALID;
    anjay_security_object_add_instance(anjay, &security_instance,
                                       &security_instance_id);
    anjay_server_object_add_instance(anjay, &server_instance,
                                     &server_instance_id);
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
        .out_buffer_size = 4000
    };

    g_anjay = anjay_new_try_from_core_persistence(&CONFIG);
    if (!g_anjay) {
        avs_log(tutorial, ERROR, "Could not create Anjay object");
        return -1;
    }

    int result = -1;

    // Setup necessary objects
    if (anjay_security_object_install(g_anjay)
            || anjay_server_object_install(g_anjay)) {
        goto cleanup;
    }

    int restore_retval = restore_objects_if_possible(g_anjay);
    if (restore_retval < 0) {
        goto cleanup;
    } else if (restore_retval > 0) {
        initialize_objects_with_default_settings(g_anjay);
    }

    result = anjay_event_loop_run(g_anjay,
                                  avs_time_duration_from_scalar(1, AVS_TIME_S));

    int persist_result = persist_objects(g_anjay);
    if (!result) {
        result = persist_result;
    }

cleanup:
    if (result) {
        anjay_delete(g_anjay);
    } else {
        result = anjay_delete_try_with_core_persistence(g_anjay);
    }
    return result;
}
