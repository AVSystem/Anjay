#include <anjay/anjay.h>
#include <anjay/attr_storage.h>
#include <anjay/security.h>
#include <anjay/server.h>
#include <avsystem/commons/log.h>

#include <poll.h>

int main_loop(anjay_t *anjay) {
    while (true) {
        // Obtain all network data sources
        AVS_LIST(avs_net_abstract_socket_t *const) sockets =
                anjay_get_sockets(anjay);

        // Prepare to poll() on them
        size_t numsocks = AVS_LIST_SIZE(sockets);
        struct pollfd pollfds[numsocks];
        size_t i = 0;
        AVS_LIST(avs_net_abstract_socket_t *const) sock;
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
            AVS_LIST(avs_net_abstract_socket_t *const) socket = NULL;
            AVS_LIST_FOREACH(socket, sockets) {
                if (pollfds[socket_id].revents) {
                    if (anjay_serve(anjay, *socket)) {
                        avs_log(tutorial, ERROR, "anjay_serve failed");
                    }
                }
                ++socket_id;
            }
        }

        // Finally run the scheduler (ignoring its return value, which
        // is the number of tasks executed)
        (void) anjay_sched_run(anjay);
    }
    return 0;
}

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

    int result = anjay_attr_storage_install(anjay);

    // Install necessary objects
    if (anjay_security_object_install(anjay)
            || anjay_server_object_install(anjay)) {
        result = -1;
    }

    if (result) {
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

    result = main_loop(anjay);

cleanup:
    anjay_delete(anjay);
    return result;
}
