#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <avsystem/commons/avs_log.h>
#include <avsystem/commons/avs_utils.h>

#include <fluf/fluf.h>
#include <fluf/fluf_defs.h>

#include <anj/anj_net.h>
#include <anj/anj_time.h>
#include <anj/sdm_io.h>

#include "bootstrap_event_loop.h"
#include "example_config.h"
#include "net_integration.h"

#define event_loop_log(...) avs_log(event_loop, __VA_ARGS__)

static void prepare_retransmission_ctx(event_loop_ctx_t *ctx) {
    ctx->retransmit_count = 0;
    ctx->timeout_timestamp = anj_time_now() + EXAMPLE_REQUEST_ACK_TIMEOUT_MS;
}

static int decode_incoming_message(event_loop_ctx_t *ctx, fluf_data_t *msg) {
    int res = fluf_msg_decode(ctx->incoming_msg, ctx->incoming_msg_size,
                              FLUF_BINDING_UDP, msg);
    if (res) {
        event_loop_log(ERROR, "Failed to decode incoming message: %d", res);
        return res;
    }
    return 0;
}

static int send_msg(event_loop_ctx_t *ctx) {
    int res = fluf_msg_prepare(&ctx->msg, ctx->outgoing_msg,
                               EXAMPLE_OUTGOING_MSG_BUFFER_SIZE,
                               &ctx->outgoing_msg_size);
    if (res) {
        event_loop_log(ERROR, "Failed to prepare a message: %d", res);
        return res;
    }
    return (int) net_send(&ctx->conn_ref, ctx->outgoing_msg,
                          ctx->outgoing_msg_size);
}

static void handle_server_request(event_loop_ctx_t *ctx) {
    anj_net_op_res_t res =
            net_try_recv(&ctx->conn_ref, EXAMPLE_INCOMING_MSG_BUFFER_SIZE,
                         ctx->incoming_msg, &ctx->incoming_msg_size);
    if (net_is_again(res)) {
        return;
    } else if (net_is_err(res)) {
        ctx->state = EVENT_LOOP_STATE_ERROR;
        event_loop_log(ERROR, "UDP connection error");
        return;
    }
    if (decode_incoming_message(ctx, &ctx->msg)) {
        return;
    }
    // handle data model operation
    if (ctx->msg.operation == FLUF_OP_DM_READ
            || ctx->msg.operation == FLUF_OP_DM_DISCOVER
            || ctx->msg.operation == FLUF_OP_DM_WRITE_REPLACE
            || ctx->msg.operation == FLUF_OP_DM_WRITE_PARTIAL_UPDATE
            || ctx->msg.operation == FLUF_OP_DM_EXECUTE
            || ctx->msg.operation == FLUF_OP_DM_CREATE
            || ctx->msg.operation == FLUF_OP_DM_DELETE) {
        int res = sdm_process(&ctx->dm_impl, &ctx->dm, &ctx->msg,
                              ctx->bootstrap_in_progress, ctx->payload,
                              EXAMPLE_PAYLOAD_BUFFER_SIZE);
        if (res == SDM_IMPL_BLOCK_TRANSFER_NEEDED
                || res == SDM_IMPL_WANT_NEXT_MSG) {
            ctx->block_transfer = true;
        } else {
            ctx->block_transfer = false;
        }
        ctx->state = EVENT_LOOP_STATE_RESPONSE_SEND_RESULT;
    } else if (ctx->msg.operation == FLUF_OP_BOOTSTRAP_FINISH
               && ctx->bootstrap_in_progress) {
        ctx->msg.msg_code = FLUF_COAP_CODE_CHANGED;
        ctx->msg.payload_size = 0;
        ctx->msg.operation = FLUF_OP_RESPONSE;
        ctx->state = EVENT_LOOP_STATE_BOOTSTRAP_FINISH_RESPONSE_SEND_RESULT;
        event_loop_log(INFO, "Bootstrap finished");
    } else {
        event_loop_log(INFO, "Not supported operation");
        ctx->msg.msg_code = FLUF_COAP_CODE_METHOD_NOT_ALLOWED;
        ctx->msg.payload_size = 0;
        ctx->msg.operation = FLUF_OP_RESPONSE;
        ctx->state = EVENT_LOOP_STATE_RESPONSE_SEND_RESULT;
    }
    // always send response for decoded message
    if (send_msg(ctx)) {
        ctx->state = EVENT_LOOP_STATE_ERROR;
    }
}

static void check_net_send_result(event_loop_ctx_t *ctx,
                                  event_loop_state_t next_state) {
    size_t write_length;
    anj_net_op_res_t res = net_send_res(&ctx->conn_ref, &write_length);
    if (net_is_err(res)
            || (net_is_ok(res) && write_length != ctx->outgoing_msg_size)) {
        ctx->state = EVENT_LOOP_STATE_ERROR;
        event_loop_log(ERROR, "Failed to send a message");
    }
    if (net_is_ok(res)) {
        ctx->state = next_state;
        event_loop_log(DEBUG, "Message sent");
    }
}

static void catch_response(event_loop_ctx_t *ctx) {
    anj_net_op_res_t res =
            net_try_recv(&ctx->conn_ref, EXAMPLE_INCOMING_MSG_BUFFER_SIZE,
                         ctx->incoming_msg, &ctx->incoming_msg_size);
    if (net_is_again(res)) {
        if (anj_time_now() > ctx->timeout_timestamp) {
            ctx->retransmit_count++;
            if (ctx->retransmit_count > EXAMPLE_REQUEST_MAX_RETRANSMIT) {
                ctx->state = EVENT_LOOP_STATE_ERROR;
                event_loop_log(ERROR, "Failed to receive response");
                return;
            }
            // retransmision
            if (net_is_err(net_send(&ctx->conn_ref, ctx->outgoing_msg,
                                    ctx->outgoing_msg_size))) {
                ctx->state = EVENT_LOOP_STATE_ERROR;
                event_loop_log(ERROR, "Failed to retransmit a message");
                return;
            }
            ctx->state = EVENT_LOOP_STATE_REQUEST_SEND_RESULT;
            ctx->timeout_timestamp = anj_time_now()
                                     + pow(2, ctx->retransmit_count)
                                               * EXAMPLE_REQUEST_ACK_TIMEOUT_MS;
        }
        return;
    } else if (net_is_err(res)) {
        ctx->state = EVENT_LOOP_STATE_ERROR;
        event_loop_log(ERROR, "Failed to receive response");
        return;
    }
    fluf_data_t msg = { 0 };
    if (decode_incoming_message(ctx, &msg)) {
        return;
    }
    // match the repsonse with the request
    if (memcmp(ctx->msg.coap.coap_udp.token.bytes,
               msg.coap.coap_udp.token.bytes,
               msg.coap.coap_udp.token.size)) {
        // while waiting for the server to respond ignore other messages
        return;
    }
    // each request response must be handled here
    switch (ctx->request_type) {
    case EXAMPLE_REQUEST_TYPE_UPDATE:
        if (msg.msg_code == FLUF_COAP_CODE_CHANGED) {
            ctx->state = EVENT_LOOP_STATE_IDLE;
            event_loop_log(INFO, "Registration updated");
            ctx->last_update_timestamp = anj_time_now();
        } else {
            ctx->state = EVENT_LOOP_STATE_ERROR;
            event_loop_log(ERROR, "Failed to update registration");
        }
        break;
    case EXAMPLE_REQUEST_TYPE_REGISTER:
        if (msg.msg_code == FLUF_COAP_CODE_CREATED) {
            // copy location paths
            for (size_t i = 0; i < msg.location_path.location_count; i++) {
                if (msg.location_path.location_len[i]
                        >= EXAMPLE_REGISTER_PATH_BUFFER_SIZE) {
                    ctx->state = EVENT_LOOP_STATE_ERROR;
                    event_loop_log(ERROR, "Location path too long");
                    return;
                }
                memcpy(ctx->location_path[i], msg.location_path.location[i],
                       msg.location_path.location_len[i]);
            }
            ctx->location_count = msg.location_path.location_count;
            ctx->state = EVENT_LOOP_STATE_IDLE;
            event_loop_log(INFO, "Registration successful");
            ctx->last_update_timestamp = anj_time_now();
        } else {
            ctx->state = EVENT_LOOP_STATE_ERROR;
            event_loop_log(ERROR, "Registration failed");
        }
        break;
    case EXAMPLE_REQUEST_TYPE_BOOTSTRAP_REQUEST:
        if (msg.msg_code == FLUF_COAP_CODE_CHANGED) {
            ctx->state = EVENT_LOOP_STATE_IDLE;
            event_loop_log(INFO, "Bootstrap request sent");
        } else {
            ctx->state = EVENT_LOOP_STATE_ERROR;
            event_loop_log(ERROR, "Bootstrap request failed");
        }
        break;
    default:
        break;
    }
}

static int registration_update_trigger(uint16_t ssid, void *arg_ptr) {
    event_loop_ctx_t *ctx = (event_loop_ctx_t *) arg_ptr;
    assert(ssid);
    ctx->registration_update_trigger_called = true;
    return 0;
}

static int send_update_message(event_loop_ctx_t *ctx) {
    // for a single server connection, the data model cannot
    // change without its knowledge
    memset(&ctx->msg, 0, sizeof(ctx->msg));
    ctx->msg.operation = FLUF_OP_UPDATE;
    ctx->msg.binding = FLUF_BINDING_UDP;
    ctx->msg.location_path.location_count = ctx->location_count;
    for (size_t i = 0; i < ctx->location_count; i++) {
        ctx->msg.location_path.location[i] = ctx->location_path[i];
        ctx->msg.location_path.location_len[i] = strlen(ctx->location_path[i]);
    }
    return send_msg(ctx);
}

static int send_register_message(event_loop_ctx_t *ctx) {
    memset(&ctx->msg, 0, sizeof(ctx->msg));
    ctx->msg.operation = FLUF_OP_REGISTER;
    ctx->msg.binding = FLUF_BINDING_UDP;
    ctx->msg.attr.register_attr.has_endpoint = true;
    ctx->msg.attr.register_attr.has_lifetime = true;
    ctx->msg.attr.register_attr.has_lwm2m_ver = true;
#ifdef EXAMPLE_WITH_QUEUE_MODE
    ctx->msg.attr.register_attr.has_Q = true;
#endif // EXAMPLE_WITH_QUEUE_MODE
    ctx->msg.attr.register_attr.endpoint = ctx->endpoint;
    // there is only one server instance
    ctx->msg.attr.register_attr.lifetime =
            ctx->server_obj.server_instance[0].lifetime;
    ctx->msg.attr.register_attr.lwm2m_ver = FLUF_LWM2M_VERSION_STR;
    // build register message payload
    if (sdm_process(&ctx->dm_impl, &ctx->dm, &ctx->msg, false, ctx->payload,
                    EXAMPLE_PAYLOAD_BUFFER_SIZE)) {
        return -1;
    }
    return send_msg(ctx);
}

static int send_bootstrap_request_message(event_loop_ctx_t *ctx) {
    memset(&ctx->msg, 0, sizeof(ctx->msg));
    ctx->msg.operation = FLUF_OP_BOOTSTRAP_REQ;
    ctx->msg.binding = FLUF_BINDING_UDP;
    ctx->msg.attr.bootstrap_attr.has_endpoint = true;
    ctx->msg.attr.bootstrap_attr.has_pct = true;
    ctx->msg.attr.bootstrap_attr.endpoint = ctx->endpoint;
    ctx->msg.attr.bootstrap_attr.pct = FLUF_COAP_FORMAT_SENML_CBOR;
    return send_msg(ctx);
}

static sdm_security_instance_t *get_security_instance(event_loop_ctx_t *ctx) {
    for (size_t idx = 0; idx < ctx->security_obj.obj.inst_count; idx++) {
        if (ctx->security_obj.security_instances[idx].bootstrap_server
                == ctx->bootstrap_in_progress) {
            return &ctx->security_obj.security_instances[idx];
        }
    }
    AVS_UNREACHABLE("No bootstrap server instance found");
    return NULL;
}

static int open_connection(event_loop_ctx_t *ctx) {
    sdm_security_instance_t *security_instance = get_security_instance(ctx);
    const char *server_uri = security_instance->server_uri;
    // decode server_uri
    // coap url format: coap://host:port
    uint16_t port;
    char hostname[ANJ_SERVER_URI_MAX_SIZE] = { 0 };
    const char *host_ptr = strstr(server_uri, "://");
    assert(host_ptr);
    host_ptr += 3;
    const char *port_str = strstr(host_ptr, ":");
    assert(port_str);
    port_str++;
    sscanf(port_str, "%hu", &port);
    memcpy(hostname, host_ptr, port_str - host_ptr - 1);

#ifdef EXAMPLE_WITH_DTLS_PSK
    const char *identity = security_instance->public_key_or_identity;
    const char *psk = security_instance->secret_key;
    anj_net_op_res_t res =
            net_open_dtls(&ctx->conn_ref, hostname, port, identity, psk);
#else  // EXAMPLE_WITH_DTLS_PSK
    anj_net_op_res_t res = net_open_udp(&ctx->conn_ref, hostname, port);
#endif // EXAMPLE_WITH_DTLS_PSK
    // net_open_dtls/udp can't return ANJ_NET_OP_RES_AGAIN
    if (net_is_ok(res)) {
        return 0;
    } else {
        return -1;
    }
}

int event_loop_init(event_loop_ctx_t *ctx,
                    char *endpoint,
                    sdm_device_object_init_t *device_obj_init,
                    sdm_security_instance_init_t *security_inst_init) {
    assert(ctx && device_obj_init && security_inst_init && endpoint);

    srand(time(NULL));
    fluf_init((uint32_t) rand());

    avs_log_set_default_level(AVS_LOG_DEBUG);

    memset(ctx, 0, sizeof(*ctx));
    ctx->endpoint = endpoint;
    ctx->state = EVENT_LOOP_STATE_INIT;
    ctx->bootstrap_in_progress = true;

    sdm_initialize(&ctx->dm, ctx->objs_array, AVS_ARRAY_SIZE(ctx->objs_array));

    if (sdm_device_object_install(&ctx->dm, device_obj_init)) {
        event_loop_log(ERROR, "sdm_device_object_install failed");
        return -1;
    }

    sdm_server_obj_init(&ctx->server_obj);
    if (sdm_server_obj_install(&ctx->dm, &ctx->server_obj,
                               &(sdm_server_obj_handlers_t) {
                                   .registration_update_trigger =
                                           registration_update_trigger,
                                   .arg_ptr = ctx
                               })) {
        event_loop_log(ERROR, "sdm_server_obj_install failed");
        return -1;
    }

    sdm_security_obj_init(&ctx->security_obj);
    if (sdm_security_obj_add_instance(&ctx->security_obj, security_inst_init)) {
        event_loop_log(ERROR, "sdm_security_obj_add_instance failed");
        return -1;
    }
    if (sdm_security_obj_install(&ctx->dm, &ctx->security_obj)) {
        event_loop_log(ERROR, "sdm_security_obj_install failed");
        return -1;
    }
    return 0;
}

int event_loop_run(event_loop_ctx_t *ctx) {
    assert(ctx);
    switch (ctx->state) {

    case EVENT_LOOP_STATE_INIT:
        if (!open_connection(ctx)) {
            ctx->state = EVENT_LOOP_STATE_OPEN_IN_PROGRESS;
            event_loop_log(DEBUG, "Trying to open a UDP connection");
        } else {
            // set reconnection timestamp
            ctx->timeout_timestamp =
                    anj_time_now() + EXAMPLE_RECONNECTION_TIME_MS;
            ctx->state = EVENT_LOOP_STATE_OFFLINE;
            // Fall back to Client-Initiated Bootstrap
            ctx->bootstrap_in_progress = true;
            event_loop_log(ERROR, "Failed to open a UDP connection");
        }
        break;

    case EVENT_LOOP_STATE_OPEN_IN_PROGRESS: {
        // after opening a connection, send a bootstrap request/register message
#ifdef EXAMPLE_WITH_DTLS_PSK
        anj_net_op_res_t res = net_open_dtls_res(&ctx->conn_ref);
#else  // EXAMPLE_WITH_DTLS_PSK
        anj_net_op_res_t res = net_open_udp_res(&ctx->conn_ref);
#endif // EXAMPLE_WITH_DTLS_PSK
        if (net_is_ok(res)) {
            event_loop_log(INFO, "UDP connection opened");
            int res = ctx->bootstrap_in_progress
                              ? send_bootstrap_request_message(ctx)
                              : send_register_message(ctx);
            if (res) {
                ctx->state = EVENT_LOOP_STATE_ERROR;
                event_loop_log(ERROR,
                               "Failed to send a register/bootstrap message");
            } else {
                ctx->request_type =
                        ctx->bootstrap_in_progress
                                ? EXAMPLE_REQUEST_TYPE_BOOTSTRAP_REQUEST
                                : EXAMPLE_REQUEST_TYPE_REGISTER;
                ctx->state = EVENT_LOOP_STATE_REQUEST_SEND_RESULT;
                prepare_retransmission_ctx(ctx);
                event_loop_log(DEBUG, "Start registration/bootstrap process");
            }
        } else if (net_is_err(res)) {
            ctx->state = EVENT_LOOP_STATE_ERROR;
            event_loop_log(ERROR, "Failed to open a UDP connection");
        }
        break;
    }

    case EVENT_LOOP_STATE_RESPONSE_SEND_RESULT:
        // after send confirmation, go to IDLE state
        check_net_send_result(ctx, EVENT_LOOP_STATE_IDLE);
        break;

    case EVENT_LOOP_STATE_REQUEST_SEND_RESULT:
        // after send confirmation, wait for response
        check_net_send_result(ctx, EVENT_LOOP_STATE_CATCH_RESPONSE);
        break;

    case EVENT_LOOP_STATE_BOOTSTRAP_FINISH_RESPONSE_SEND_RESULT:
        // after send confirmation, reopen connection
        check_net_send_result(ctx, EVENT_LOOP_STATE_OFFLINE);
        if (ctx->state == EVENT_LOOP_STATE_OFFLINE) {
            ctx->bootstrap_in_progress = false;
            net_close(&ctx->conn_ref);
            // net_close_res can't return ANJ_NET_OP_RES_AGAIN
            net_close_res(&ctx->conn_ref);
            net_cleanup(&ctx->conn_ref);
            // no delay before reconnecting
            ctx->timeout_timestamp = anj_time_now();
        }
        break;

    case EVENT_LOOP_STATE_CATCH_RESPONSE:
        // wait for LwM2M Server response
        // only Piggybacked are currently supported
        catch_response(ctx);
        break;

    case EVENT_LOOP_STATE_IDLE:
        // send register update message if needed or check for requests
        // during block transfer we don't want to send any messages
        if (!ctx->block_transfer && !ctx->bootstrap_in_progress
                && ((anj_time_now() - ctx->last_update_timestamp) * 2
                            > ctx->server_obj.server_instance[0].lifetime * 1000
                    || ctx->registration_update_trigger_called)) {
            ctx->registration_update_trigger_called = false;
            if (send_update_message(ctx)) {
                ctx->state = EVENT_LOOP_STATE_ERROR;
                event_loop_log(ERROR, "Failed to send an update message");
            } else {
                ctx->state = EVENT_LOOP_STATE_REQUEST_SEND_RESULT;
                ctx->request_type = EXAMPLE_REQUEST_TYPE_UPDATE;
                prepare_retransmission_ctx(ctx);
            }
        } else {
            handle_server_request(ctx);
        }
        break;

    case EVENT_LOOP_STATE_ERROR:
        // close connection and go offline in case of network layer error
        // or not allowed LwM2M Server response
        net_close(&ctx->conn_ref);
        ctx->state = EVENT_LOOP_STATE_CLOSE_IN_PROGRESS;
        if (ctx->block_transfer) {
            // cancel ongoing transaction
            sdm_process_stop(&ctx->dm_impl, &ctx->dm);
            ctx->block_transfer = false;
        }
        break;

    case EVENT_LOOP_STATE_CLOSE_IN_PROGRESS:
        // close and cleanup connection ctx
        net_close_res(&ctx->conn_ref);
        net_cleanup(&ctx->conn_ref);
        event_loop_log(INFO, "Connection closed, reconnecting in %d ms",
                       EXAMPLE_RECONNECTION_TIME_MS);
        ctx->state = EVENT_LOOP_STATE_OFFLINE;
        // set reconnection timestamp
        ctx->timeout_timestamp = anj_time_now() + EXAMPLE_RECONNECTION_TIME_MS;
        break;

    case EVENT_LOOP_STATE_OFFLINE:
        // delay before reconnecting
        if (anj_time_now() > ctx->timeout_timestamp) {
            ctx->state = EVENT_LOOP_STATE_INIT;
        }
        break;

    default:
        break;
    }
    return 0;
}
