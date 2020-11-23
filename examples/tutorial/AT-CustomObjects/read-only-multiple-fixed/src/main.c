#include <assert.h>
#include <poll.h>

#include <anjay/anjay.h>
#include <anjay/attr_storage.h>
#include <anjay/security.h>
#include <anjay/server.h>

#include <avsystem/commons/avs_log.h>

typedef struct test_instance {
    const char *label;
    int32_t value;
} test_instance_t;

typedef struct test_object {
    // handlers
    const anjay_dm_object_def_t *obj_def;

    // object state
    test_instance_t instances[2];
} test_object_t;

static test_object_t *get_test_object(const anjay_dm_object_def_t *const *obj) {
    assert(obj);

    // use the container_of pattern to retrieve test_object_t pointer
    // AVS_CONTAINER_OF macro provided by avsystem/commons/defs.h
    return AVS_CONTAINER_OF(obj, test_object_t, obj_def);
}

static int test_list_instances(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_dm_list_ctx_t *ctx) {
    (void) anjay; // unused

    test_object_t *test = get_test_object(obj_ptr);

    for (anjay_iid_t iid = 0;
         (size_t) iid < sizeof(test->instances) / sizeof(test->instances[0]);
         ++iid) {
        anjay_dm_emit(ctx, iid);
    }

    return 0;
}

static int test_list_resources(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid,
                               anjay_dm_resource_list_ctx_t *ctx) {
    (void) anjay;   // unused
    (void) obj_ptr; // unused
    (void) iid;     // unused

    anjay_dm_emit_res(ctx, 0, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, 1, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    return 0;
}

static int test_resource_read(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t iid,
                              anjay_rid_t rid,
                              anjay_riid_t riid,
                              anjay_output_ctx_t *ctx) {
    (void) anjay; // unused

    test_object_t *test = get_test_object(obj_ptr);

    // IID validity was checked by the `anjay_dm_list_instances_t` handler.
    // If the Object Instance set does not change, or can only be modifed
    // via LwM2M Create/Delete requests, it is safe to assume IID is correct.
    assert((size_t) iid < sizeof(test->instances) / sizeof(test->instances[0]));
    const struct test_instance *current_instance = &test->instances[iid];

    // We have no Multiple-Instance Resources, so it is safe to assume
    // that RIID is never set.
    assert(riid == ANJAY_ID_INVALID);

    switch (rid) {
    case 0:
        return anjay_ret_string(ctx, current_instance->label);
    case 1:
        return anjay_ret_i32(ctx, current_instance->value);
    default:
        // control will never reach this part due to test_list_resources
        return ANJAY_ERR_INTERNAL;
    }
}

static const anjay_dm_object_def_t OBJECT_DEF = {
    // Object ID
    .oid = 1234,

    .handlers = {
        .list_instances = test_list_instances,

        .list_resources = test_list_resources,
        .resource_read = test_resource_read

        // all other handlers can be left NULL if only Read operation is
        // required
    }
};

static int setup_security_object(anjay_t *anjay) {
    const anjay_security_instance_t security_instance = {
        .ssid = 1,
        .server_uri = "coap://try-anjay.avsystem.com:5683",
        .security_mode = ANJAY_SECURITY_NOSEC
    };

    if (anjay_security_object_install(anjay)) {
        return -1;
    }

    // let Anjay assign an Object Instance ID
    anjay_iid_t security_instance_id = ANJAY_ID_INVALID;
    if (anjay_security_object_add_instance(anjay, &security_instance,
                                           &security_instance_id)) {
        return -1;
    }

    return 0;
}

static int setup_server_object(anjay_t *anjay) {
    const anjay_server_instance_t server_instance = {
        .ssid = 1,
        .lifetime = 86400,
        .default_min_period = -1,
        .default_max_period = -1,
        .disable_timeout = -1,
        .binding = "U"
    };

    if (anjay_server_object_install(anjay)) {
        return -1;
    }

    anjay_iid_t server_instance_id = ANJAY_ID_INVALID;
    if (anjay_server_object_add_instance(anjay, &server_instance,
                                         &server_instance_id)) {
        return -1;
    }

    return 0;
}

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
                    anjay_serve(anjay, *socket);
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
        return -1;
    }

    int result = 0;

    if (anjay_attr_storage_install(anjay) || setup_security_object(anjay)
            || setup_server_object(anjay)) {
        result = -1;
        goto cleanup;
    }

    // initialize and register the test object
    const test_object_t test_object = {
        .obj_def = &OBJECT_DEF,
        .instances = { { "First", 1 }, { "Second", 2 } }
    };

    anjay_register_object(anjay, &test_object.obj_def);

    result = main_loop(anjay);

cleanup:
    anjay_delete(anjay);

    // test object does not need cleanup
    return result;
}
