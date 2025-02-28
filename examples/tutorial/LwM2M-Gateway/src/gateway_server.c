#include <anjay/anjay.h>
#include <anjay/lwm2m_gateway.h>

#include <avsystem/commons/avs_list.h>
#include <avsystem/commons/avs_log.h>
#include <avsystem/commons/avs_sched.h>

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "gateway_server.h"
#include "temperature_object.h"

static avs_sched_handle_t serve_gateway_job_handle;

static void cleanup_end_device(anjay_t *anjay, end_device_t *end_device) {
    close(end_device->cl_poll_fd.fd);

    if (end_device->iid == ANJAY_ID_INVALID) {
        return;
    }
    avs_sched_del(&end_device->notify_job_handle);
    avs_sched_del(&end_device->evaluation_period_job_handle);
    if (end_device->temperature_object) {
        if (anjay_lwm2m_gateway_unregister_object(
                    anjay, end_device->iid, end_device->temperature_object)) {
            avs_log(tutorial, ERROR, "Failed to unregister Temperature Object");
        }
        temperature_object_release(end_device->temperature_object);
    }
    if (anjay_lwm2m_gateway_deregister_device(anjay, end_device->iid)) {
        avs_log(tutorial, ERROR, "Failed to deregister End Device");
    }
}

typedef struct {
    anjay_t *anjay;
    end_device_t *end_device;
} job_args_t;

static void calculate_evaluation_period_job(avs_sched_t *sched,
                                            const void *args_ptr) {
    const job_args_t *args = (const job_args_t *) args_ptr;

    // Schedule run of the same function to track the evaluation period
    // continuously
    AVS_SCHED_DELAYED(sched, &args->end_device->evaluation_period_job_handle,
                      avs_time_duration_from_scalar(EVALUATION_CALC_JOB_PERIOD,
                                                    AVS_TIME_S),
                      calculate_evaluation_period_job, args, sizeof(*args));

    int32_t prev_evaluation_period = args->end_device->evaluation_period;
    int32_t new_evaluation_period = DEFAULT_MAXIMAL_EVALUATION_PERIOD;

    temperature_object_evaluation_period_update_value(
            args->anjay,
            args->end_device->temperature_object,
            &new_evaluation_period);
    if (new_evaluation_period == prev_evaluation_period) {
        return;
    }
    args->end_device->evaluation_period = new_evaluation_period;

    // if evaluation period has changed, notify job should be rescheduled
    // accordingly to new period
    avs_time_monotonic_t new_notify_instant = avs_time_monotonic_add(
            avs_time_monotonic_add(
                    avs_sched_time(&args->end_device->notify_job_handle),
                    avs_time_duration_from_scalar(-prev_evaluation_period,
                                                  AVS_TIME_S)),
            avs_time_duration_from_scalar(new_evaluation_period, AVS_TIME_S));
    AVS_RESCHED_AT(&args->end_device->notify_job_handle, new_notify_instant);
}

static void notify_job(avs_sched_t *sched, const void *args_ptr) {
    const job_args_t *args = (const job_args_t *) args_ptr;

    temperature_object_update_value(args->anjay,
                                    args->end_device->temperature_object);

    AVS_SCHED_DELAYED(sched, &args->end_device->notify_job_handle,
                      avs_time_duration_from_scalar(
                              args->end_device->evaluation_period, AVS_TIME_S),
                      notify_job, args, sizeof(*args));
}

static int setup_end_device(gateway_srv_t *gateway_srv,
                            end_device_t *end_device,
                            const char *msg) {
    anjay_iid_t iid = ANJAY_ID_INVALID;
    anjay_t *anjay = gateway_srv->anjay;

    strcpy(end_device->end_device_name, msg);
    if (anjay_lwm2m_gateway_register_device(anjay, end_device->end_device_name,
                                            &iid)) {
        avs_log(tutorial, ERROR, "Failed to add End Device");
        return -1;
    }
    end_device->iid = iid;
    end_device->evaluation_period = DEFAULT_MAXIMAL_EVALUATION_PERIOD;

    const anjay_dm_object_def_t **obj =
            temperature_object_create(iid, gateway_srv);
    if (!obj) {
        avs_log(tutorial, ERROR, "Failed to create Temperature Object");
        return -1;
    }
    end_device->temperature_object = obj;

    if (anjay_lwm2m_gateway_register_object(anjay, iid, obj)) {
        avs_log(tutorial, ERROR, "Failed to register Temperature Object");
        return -1;
    }

    calculate_evaluation_period_job(anjay_get_scheduler(anjay),
                                    &(const job_args_t) {
                                        .anjay = anjay,
                                        .end_device = end_device
                                    });

    notify_job(anjay_get_scheduler(anjay),
               &(const job_args_t) {
                   .anjay = anjay,
                   .end_device = end_device
               });

    avs_log(tutorial, INFO, "End Device %s added", end_device->end_device_name);
    return 0;
}

static int request_process(end_device_t *end_device,
                           gateway_request_type_t request_type,
                           char *out_buffer,
                           size_t out_buffer_size) {
    static const char *const request_value = "get temperature";
    static const char *const request_id = "get id";
    static const char *const request_max_measured_value = "get max";
    static const char *const request_min_measured_value = "get min";
    static const char *const request_reset_min_max = "reset";

    struct pollfd *cl_poll_fd = &end_device->cl_poll_fd;
    const char *request;
    switch (request_type) {
    case GATEWAY_REQUEST_TYPE_GET_ID:
        request = request_id;
        break;
    case GATEWAY_REQUEST_TYPE_GET_TEMPERATURE:
        request = request_value;
        break;
    case GATEWAY_REQUEST_TYPE_GET_MAX_MEASURED_VALUE:
        request = request_max_measured_value;
        break;
    case GATEWAY_REQUEST_TYPE_GET_MIN_MEASURED_VALUE:
        request = request_min_measured_value;
        break;
    case GATEWAY_REQUEST_TYPE_RESET_MIN_AND_MAX_MEASURED_VALUES:
        request = request_reset_min_max;
        break;
    default:
        return -1;
    }

    if (write(cl_poll_fd->fd, request, strlen(request)) == -1) {
        avs_log(tutorial, ERROR, "Failed to send request to client %d",
                cl_poll_fd->fd);
        return -1;
    }
    // timeout set to 1 second
    if ((poll(cl_poll_fd, 1, 1000) > 0) && (cl_poll_fd->revents & POLLIN)) {
        // leave one byte for null terminator
        int bytes_read = read(cl_poll_fd->fd, out_buffer, out_buffer_size - 1);
        if (bytes_read <= 0) {
            avs_log(tutorial, INFO, "Connection closed by client %d",
                    cl_poll_fd->fd);
            return -1;
        }
        out_buffer[bytes_read] = '\0';
        avs_log(tutorial, INFO, "Received message: %s", out_buffer);
    } else {
        avs_log(tutorial, WARNING, "No response from client");
        return -1;
    }
    return 0;
}

int gateway_request(gateway_srv_t *gateway_srv,
                    anjay_iid_t end_device_iid,
                    gateway_request_type_t request_type,
                    char *out_buffer,
                    size_t out_buffer_size) {
    AVS_LIST(end_device_t) end_device;
    AVS_LIST_FOREACH(end_device, gateway_srv->end_devices) {
        if (end_device->iid == end_device_iid) {
            return request_process(end_device, request_type, out_buffer,
                                   out_buffer_size);
        }
    }
    avs_log(tutorial, ERROR, "End Device not found");
    return -1;
}

static void serve_gateway_job(avs_sched_t *sched, const void *args_ptr) {
    gateway_srv_t *gateway_srv = *(gateway_srv_t *const *) args_ptr;

    // Discover incoming connections
    struct pollfd srv_poll_fd = {
        .fd = gateway_srv->srv_socket,
        .events = POLLIN
    };
    if ((poll(&srv_poll_fd, 1, 0) > 0) && srv_poll_fd.revents & POLLIN) {
        int client_socket = accept(gateway_srv->srv_socket, NULL, NULL);
        if (client_socket == -1) {
            avs_log(tutorial, ERROR, "Failed to accept a new connection %s",
                    strerror(errno));
        } else {
            avs_log(tutorial, INFO, "New connection accepted %d",
                    client_socket);

            AVS_LIST(end_device_t) new_end_device =
                    AVS_LIST_NEW_ELEMENT(end_device_t);
            assert(new_end_device);
            new_end_device->cl_poll_fd.fd = client_socket;
            new_end_device->cl_poll_fd.events = POLLIN;
            new_end_device->temperature_object = NULL;
            new_end_device->iid = ANJAY_ID_INVALID;

            // register new end device
            char buffer[END_DEVICE_NAME_LEN];
            if (request_process(new_end_device, GATEWAY_REQUEST_TYPE_GET_ID,
                                buffer, END_DEVICE_NAME_LEN)
                    || setup_end_device(gateway_srv, new_end_device, buffer)) {
                cleanup_end_device(gateway_srv->anjay, new_end_device);
                AVS_LIST_DELETE(&new_end_device);
                avs_log(tutorial, ERROR, "Failed to add new end device");
            } else {
                AVS_LIST_INSERT(&gateway_srv->end_devices, new_end_device);
            }
        }
    }

    AVS_LIST(end_device_t) *ptr;
    AVS_LIST(end_device_t) helper;
    AVS_LIST_DELETABLE_FOREACH_PTR(ptr, helper, &gateway_srv->end_devices) {
        end_device_t *end_device = *ptr;
        int ret = poll(&end_device->cl_poll_fd, 1, 0);
        if (ret == -1
                || (end_device->cl_poll_fd.revents
                    & (POLLERR | POLLHUP | POLLNVAL))) {
            cleanup_end_device(gateway_srv->anjay, end_device);
            AVS_LIST_DELETE(ptr);
            avs_log(tutorial, INFO, "End Device removed");
        }
    }
    // Schedule run of the same function after 1 second
    AVS_SCHED_DELAYED(sched, &serve_gateway_job_handle,
                      avs_time_duration_from_scalar(1, AVS_TIME_S),
                      serve_gateway_job, &gateway_srv, sizeof(gateway_srv));
}

int gateway_setup_server(gateway_srv_t *gateway_srv) {
    struct sockaddr_un server_addr;

    if ((gateway_srv->srv_socket = socket(AF_UNIX, SOCK_SEQPACKET, 0)) == -1) {
        avs_log(tutorial, ERROR, "Failed to create a socket %s",
                strerror(errno));
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    avs_simple_snprintf(server_addr.sun_path, sizeof(server_addr.sun_path),
                        "%s", SOCKET_PATH);

    // remove the socket file if it already exists
    unlink(SOCKET_PATH);

    if (bind(gateway_srv->srv_socket, (struct sockaddr *) &server_addr,
             sizeof(server_addr))
            == -1) {
        avs_log(tutorial, ERROR, "Failed to bind a socket %s", strerror(errno));
        gateway_cleanup_server(gateway_srv);
        return -1;
    }

    if (listen(gateway_srv->srv_socket, 1) == -1) {
        avs_log(tutorial, ERROR, "Failed to listen on a socket %s",
                strerror(errno));
        gateway_cleanup_server(gateway_srv);
        return -1;
    }
    gateway_srv->end_devices = NULL;

    avs_log(tutorial, INFO, "Local server is listening on %s", SOCKET_PATH);

    serve_gateway_job(anjay_get_scheduler(gateway_srv->anjay), &gateway_srv);
    return 0;
}

void gateway_cleanup_server(gateway_srv_t *gateway_srv) {
    close(gateway_srv->srv_socket);
    unlink(SOCKET_PATH);
    AVS_LIST(end_device_t) list = gateway_srv->end_devices;
    AVS_LIST_CLEAR(&list) {
        cleanup_end_device(gateway_srv->anjay, list);
    }

    avs_sched_del(&serve_gateway_job_handle);
}
