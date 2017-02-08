#include <avsystem/commons/log.h>
#include <anjay/anjay.h>
#include <anjay/security.h>
#include <anjay/server.h>

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

    // Instantiate necessary objects
    const anjay_dm_object_def_t **security_obj = anjay_security_object_create();
    const anjay_dm_object_def_t **server_obj = anjay_server_object_create();

    // For some reason we were unable to instantiate objects.
    if (!security_obj || !server_obj) {
        result = -1;
        goto cleanup;
    }

    // Register them within Anjay
    if (anjay_register_object(anjay, security_obj)
            || anjay_register_object(anjay, server_obj)) {
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
        .binding = ANJAY_BINDING_U
    };

    anjay_iid_t security_instance_id = ANJAY_IID_INVALID;
    anjay_iid_t server_instance_id = ANJAY_IID_INVALID;
    anjay_security_object_add_instance(security_obj, &security_instance,
                                       &security_instance_id);
    anjay_server_object_add_instance(server_obj, &server_instance,
                                     &server_instance_id);

    // Event loop will go here

cleanup:
    anjay_delete(anjay);
    anjay_security_object_delete(security_obj);
    anjay_server_object_delete(server_obj);
    return result;
}
