#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#include <stdbool.h>
#include <stdint.h>

#include <fluf/fluf.h>
#include <fluf/fluf_defs.h>

#include <anj/anj_net.h>
#include <anj/sdm_device_object.h>
#include <anj/sdm_impl.h>
#include <anj/sdm_io.h>
#include <anj/sdm_security_object.h>
#include <anj/sdm_server_object.h>

#include "example_config.h"

/**
 * This is a simple event loop example that handles UDP connection, registration
 * process and data model requests. It is used to demonstrate how to integrate
 * the AVSystem Anjay4 library with the user code and network integration layer.
 */

/** The event loop state machine states */
typedef enum {
    EVENT_LOOP_STATE_INIT,
    EVENT_LOOP_STATE_OFFLINE,
    EVENT_LOOP_STATE_OPEN_IN_PROGRESS,
    EVENT_LOOP_STATE_REQUEST_SEND_RESULT,
    EVENT_LOOP_STATE_RESPONSE_SEND_RESULT,
    EVENT_LOOP_STATE_BOOTSTRAP_FINISH_RESPONSE_SEND_RESULT,
    EVENT_LOOP_STATE_CATCH_RESPONSE,
    EVENT_LOOP_STATE_IDLE,
    EVENT_LOOP_STATE_ERROR,
    EVENT_LOOP_STATE_CLOSE_IN_PROGRESS
} event_loop_state_t;

/** The event loop request types, for new LwM2M client request define new type.
 */
typedef enum {
    EXAMPLE_REQUEST_TYPE_BOOTSTRAP_REQUEST,
    EXAMPLE_REQUEST_TYPE_REGISTER,
    EXAMPLE_REQUEST_TYPE_UPDATE
} event_loop_request_type_t;

/** The event loop context contains various variables and buffers used by the
 * event loop. */
typedef struct {
    char incoming_msg[EXAMPLE_INCOMING_MSG_BUFFER_SIZE];
    size_t incoming_msg_size;
    char outgoing_msg[EXAMPLE_OUTGOING_MSG_BUFFER_SIZE];
    size_t outgoing_msg_size;
    char payload[EXAMPLE_PAYLOAD_BUFFER_SIZE];
    size_t payload_size;
    sdm_obj_t *objs_array[EXAMPLE_OBJS_ARRAY_SIZE];
    sdm_data_model_t dm;
    sdm_process_ctx_t dm_impl;
    sdm_server_obj_t server_obj;
    sdm_security_obj_t security_obj;
    bool registration_update_trigger_called;
    event_loop_state_t state;
    anj_net_conn_ref_t conn_ref;
    uint64_t last_update_timestamp;
    uint64_t timeout_timestamp;
    uint8_t retransmit_count;
    fluf_data_t msg;
    char *endpoint;
    char location_path[FLUF_MAX_ALLOWED_LOCATION_PATHS_NUMBER]
                      [EXAMPLE_REGISTER_PATH_BUFFER_SIZE];
    size_t location_count;
    event_loop_request_type_t request_type;
    bool block_transfer;
    bool bootstrap_in_progress;
} event_loop_ctx_t;

/**
 * Initializes the event loop context with the provided parameters.
 *
 * @param ctx                Event loop context.
 * @param endpoint           The endpoint name for register message.
 * @param device_obj_init    Device object initialization structure.
 * @param server_inst_init   Server instance initialization structure.
 * @param security_inst_init Security instance initialization structure.
 *
 * @return int 0 on success, negative value on failure.
 */
int event_loop_init(event_loop_ctx_t *ctx,
                    char *endpoint,
                    sdm_device_object_init_t *device_obj_init,
                    sdm_security_instance_init_t *security_inst_init);

/**
 * Runs the event loop, handling connection, registration process and data
 * model requests.
 *
 * @param ctx Event loop context.
 *
 * @return int 0 on success, negative value on failure.
 */
int event_loop_run(event_loop_ctx_t *ctx);

#endif // EVENT_LOOP_H
