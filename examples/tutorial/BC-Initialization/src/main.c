#include <anjay/anjay.h>
#include <avsystem/commons/avs_log.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        avs_log(tutorial, ERROR, "usage: %s ENDPOINT_NAME", argv[0]);
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

    anjay_event_loop_run(anjay, avs_time_duration_from_scalar(1, AVS_TIME_S));

    anjay_delete(anjay);
    return 0;
}
