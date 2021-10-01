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

#define PERSISTENCE_FILENAME "at2-persistence.dat"

int persist_objects(anjay_t *anjay) {
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

    result = 0;
finish:
    avs_stream_cleanup(&file_stream);
    return result;
}

void initialize_objects_with_default_settings(anjay_t *anjay) {
    const anjay_security_instance_t security_instance = {
        .ssid = 1,
        .server_uri = "coap://try-anjay.avsystem.com:5683",
        .security_mode = ANJAY_SECURITY_NOSEC
    };

    const anjay_server_instance_t server_instance = {
        .ssid = 1,
        .lifetime = 86400,
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

    g_anjay = anjay_new(&CONFIG);
    if (!g_anjay) {
        avs_log(tutorial, ERROR, "Could not create Anjay object");
        return -1;
    }

    int result = 0;

    // Install Attribute storage and necessary objects
    if (anjay_attr_storage_install(g_anjay)
            || anjay_security_object_install(g_anjay)
            || anjay_server_object_install(g_anjay)) {
        result = -1;
        goto cleanup;
    }

    int restore_retval = restore_objects_if_possible(g_anjay);
    if (restore_retval < 0) {
        result = -1;
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
    anjay_delete(g_anjay);
    return result;
}
