#ifndef GATEWAY_SERVER_H
#define GATEWAY_SERVER_H

#include <poll.h>

#include <avsystem/commons/avs_list.h>

#include <anjay/dm.h>

#define SOCKET_PATH "/tmp/lwm2m-gateway.sock"

#define END_DEVICE_NAME_LEN sizeof("urn:dev:00000")
#define VALUE_MESSAGE_MAX_LEN sizeof("xx.yy")
#define EXECUTE_MSG_RESPONSE_LEN sizeof("OK")

#define DEFAULT_MAXIMAL_EVALUATION_PERIOD 60
#define EVALUATION_CALC_JOB_PERIOD 1

typedef struct {
    struct pollfd cl_poll_fd;
    const anjay_dm_object_def_t **temperature_object;
    anjay_iid_t iid;
    char end_device_name[END_DEVICE_NAME_LEN];
    int32_t evaluation_period;
    avs_sched_handle_t notify_job_handle;
    avs_sched_handle_t evaluation_period_job_handle;
} end_device_t;

typedef struct {
    anjay_t *anjay;
    int srv_socket;
    AVS_LIST(end_device_t) end_devices;
} gateway_srv_t;

typedef enum {
    GATEWAY_REQUEST_TYPE_GET_ID,
    GATEWAY_REQUEST_TYPE_GET_TEMPERATURE,
    GATEWAY_REQUEST_TYPE_GET_MAX_MEASURED_VALUE,
    GATEWAY_REQUEST_TYPE_GET_MIN_MEASURED_VALUE,
    GATEWAY_REQUEST_TYPE_RESET_MIN_AND_MAX_MEASURED_VALUES
} gateway_request_type_t;

int gateway_setup_server(gateway_srv_t *gateway_srv);
void gateway_cleanup_server(gateway_srv_t *gateway_srv);
int gateway_request(gateway_srv_t *gateway_srv,
                    anjay_iid_t end_device_iid,
                    gateway_request_type_t request_type,
                    char *out_buffer,
                    size_t out_buffer_size);

#endif // GATEWAY_SERVER_H
