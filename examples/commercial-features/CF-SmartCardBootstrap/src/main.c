#include <anjay/anjay.h>
#include <anjay/bootstrapper.h>
#include <anjay/security.h>
#include <anjay/server.h>
#include <avsystem/commons/avs_log.h>
#include <avsystem/commons/avs_stream_file.h>

static int bootstrap_from_file(anjay_t *anjay, const char *filename) {
    avs_log(tutorial, INFO, "Attempting to bootstrap from file");

    avs_stream_t *file_stream =
            avs_stream_file_create(filename, AVS_STREAM_FILE_READ);

    if (!file_stream) {
        avs_log(tutorial, ERROR, "Could not open file");
        return -1;
    }

    int result = 0;
    if (avs_is_err(anjay_bootstrapper(anjay, file_stream))) {
        avs_log(tutorial, ERROR, "Could not bootstrap from file");
        result = -1;
    }

    avs_stream_cleanup(&file_stream);
    return result;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        avs_log(tutorial, ERROR, "usage: %s ENDPOINT_NAME BOOTSTRAP_INFO_FILE",
                argv[0]);
        return -1;
    }

    const anjay_configuration_t CONFIG = {
        .endpoint_name = argv[1],
        .in_buffer_size = 4000,
        .out_buffer_size = 4000,
        .msg_cache_size = 4000
    };

    anjay_t *anjay = anjay_new(&CONFIG);
    if (!anjay) {
        avs_log(tutorial, ERROR, "Could not create Anjay object");
        return -1;
    }

    int result = 0;
    // Setup necessary objects
    if (anjay_security_object_install(anjay)
            || anjay_server_object_install(anjay)) {
        result = -1;
    }

    if (!result) {
        result = bootstrap_from_file(anjay, argv[2]);
    }

    if (!result) {
        result = anjay_event_loop_run(
                anjay, avs_time_duration_from_scalar(1, AVS_TIME_S));
    }

    anjay_delete(anjay);
    return result;
}
