#include <anjay/anjay.h>
#include <anjay/security.h>
#include <anjay/server.h>
#include <avsystem/commons/log.h>

int main(int argc, char *argv[]) {
    static const anjay_configuration_t CONFIG = {
        .endpoint_name = "urn:dev:os:anjay-tutorial",
        .in_buffer_size = 4000,
        .out_buffer_size = 4000
    };

    anjay_t *anjay = anjay_new(&CONFIG);
    if (!anjay) {
        avs_log(tutorial, ERROR, "Could not create Anjay object");
        return -1;
    }
    int result = 0;

    // Install necessary objects
    if (anjay_security_object_install(anjay)
            || anjay_server_object_install(anjay)) {
        result = -1;
        goto cleanup;
    }

    const anjay_security_instance_t security_instance = {
        .ssid = 1,
        .server_uri = "coap://127.0.0.1:5683",
        .security_mode = ANJAY_UDP_SECURITY_NOSEC
    };

    const anjay_server_instance_t server_instance = {
        .ssid = 1,
        .lifetime = 86400,
        .default_min_period = -1,
        .default_max_period = -1,
        .disable_timeout = -1,
        .binding = "U"
    };

    anjay_iid_t security_instance_id = ANJAY_IID_INVALID;
    anjay_iid_t server_instance_id = ANJAY_IID_INVALID;
    anjay_security_object_add_instance(anjay, &security_instance,
                                       &security_instance_id);
    anjay_server_object_add_instance(anjay, &server_instance,
                                     &server_instance_id);

    // Event loop will go here

cleanup:
    anjay_delete(anjay);
    return result;
}
