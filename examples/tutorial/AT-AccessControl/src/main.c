#include <anjay/access_control.h>
#include <anjay/anjay.h>
#include <anjay/attr_storage.h>
#include <anjay/security.h>
#include <anjay/server.h>
#include <avsystem/commons/avs_log.h>

#include <poll.h>

#include "test_object.h"

int main_loop(anjay_t *anjay) {
    while (true) {
        // Obtain all network data sources
        AVS_LIST(avs_net_socket_t *const) sockets = anjay_get_sockets(anjay);

        // Prepare to poll() on them
        size_t numsocks = AVS_LIST_SIZE(sockets);
        struct pollfd pollfds[numsocks];
        size_t i = 0;
        AVS_LIST(avs_net_socket_t *const) sock;
        AVS_LIST_FOREACH(sock, sockets) {
            pollfds[i].fd = *(const int *) avs_net_socket_get_system(*sock);
            pollfds[i].events = POLLIN;
            pollfds[i].revents = 0;
            ++i;
        }

        const int max_wait_time_ms = 1000;
        // Determine the expected time to the next job in milliseconds.
        // If there is no job we will wait till something arrives for
        // at most 1 second (i.e. max_wait_time_ms).
        int wait_ms =
                anjay_sched_calculate_wait_time_ms(anjay, max_wait_time_ms);

        // Wait for the events if necessary, and handle them.
        if (poll(pollfds, numsocks, wait_ms) > 0) {
            int socket_id = 0;
            AVS_LIST(avs_net_socket_t *const) socket = NULL;
            AVS_LIST_FOREACH(socket, sockets) {
                if (pollfds[socket_id].revents) {
                    if (anjay_serve(anjay, *socket)) {
                        avs_log(tutorial, ERROR, "anjay_serve failed");
                    }
                }
                ++socket_id;
            }
        }

        // Finally run the scheduler
        anjay_sched_run(anjay);
    }
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

    int result;
    if (anjay_attr_storage_install(anjay) || anjay_access_control_install(anjay)
            || anjay_security_object_install(anjay)
            || anjay_server_object_install(anjay)) {
        result = -1;
    }

    // Instantiate test object
    const anjay_dm_object_def_t **test_obj = create_test_object();

    // For some reason we were unable to instantiate or install
    if (!test_obj || result) {
        result = -1;
        goto cleanup;
    }

    // Register test object within Anjay
    if (anjay_register_object(anjay, test_obj)) {
        result = -1;
        goto cleanup;
    }

    // LwM2M Server account with SSID = 1
    const anjay_security_instance_t security_instance1 = {
        .ssid = 1,
        .server_uri = "coap://try-anjay.avsystem.com:5683",
        .security_mode = ANJAY_SECURITY_NOSEC
    };

    const anjay_server_instance_t server_instance1 = {
        .ssid = 1,
        .lifetime = 86400,
        .default_min_period = -1,
        .default_max_period = -1,
        .disable_timeout = -1,
        .binding = "U"
    };

    // LwM2M Server account with SSID = 2
    const anjay_security_instance_t security_instance2 = {
        .ssid = 2,
        .server_uri = "coap://127.0.0.1:5683",
        .security_mode = ANJAY_SECURITY_NOSEC
    };

    const anjay_server_instance_t server_instance2 = {
        .ssid = 2,
        .lifetime = 86400,
        .default_min_period = -1,
        .default_max_period = -1,
        .disable_timeout = -1,
        .binding = "U"
    };

    // Setup first LwM2M Server
    anjay_iid_t server_instance_iid1 = ANJAY_ID_INVALID;
    anjay_security_object_add_instance(anjay, &security_instance1,
                                       &(anjay_iid_t) { ANJAY_ID_INVALID });
    anjay_server_object_add_instance(anjay, &server_instance1,
                                     &server_instance_iid1);

    // Setup second LwM2M Server
    anjay_iid_t server_instance_iid2 = ANJAY_ID_INVALID;
    anjay_security_object_add_instance(anjay, &security_instance2,
                                       &(anjay_iid_t) { ANJAY_ID_INVALID });
    anjay_server_object_add_instance(anjay, &server_instance2,
                                     &server_instance_iid2);

    // Set LwM2M Create permission rights for SSID = 1, this will make SSID=1
    // an exclusive owner of the Test Object
    anjay_access_control_set_acl(anjay, 1234, ANJAY_ID_INVALID, 1,
                                 ANJAY_ACCESS_MASK_CREATE);

    // Allow both LwM2M Servers to read their Server Instances
    anjay_access_control_set_acl(anjay, 1, server_instance_iid1,
                                 server_instance1.ssid, ANJAY_ACCESS_MASK_READ);
    anjay_access_control_set_acl(anjay, 1, server_instance_iid2,
                                 server_instance2.ssid, ANJAY_ACCESS_MASK_READ);

    result = main_loop(anjay);

cleanup:
    anjay_delete(anjay);
    delete_test_object(test_obj);
    return result;
}
