/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <config.h>

#include <assert.h>
#include <math.h>
#include <ctype.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <avsystem/commons/stream/net.h>
#include <avsystem/commons/stream_v_table.h>

#include <anjay/core.h>

#include <anjay_modules/time.h>

#include "anjay.h"
#include "utils.h"
#include "dm.h"
#include "io.h"
#include "coap/stream.h"
#include "interface/bootstrap.h"
#include "interface/register.h"

#ifdef ANJAY_TEST
#include "test/mock_coap_stream.h"
#endif // ANJAY_TEST

VISIBILITY_SOURCE_BEGIN

static int init(anjay_t *anjay,
                const anjay_configuration_t *config) {
    anjay->dtls_version = config->dtls_version;
    if (anjay->dtls_version == AVS_NET_SSL_VERSION_DEFAULT) {
        anjay->dtls_version = AVS_NET_SSL_VERSION_TLSv1_2;
    }

    anjay->endpoint_name = config->endpoint_name;
    if (!anjay->endpoint_name) {
        anjay_log(ERROR, "endpoint name must not be null");
        return -1;
    }

    anjay->udp_socket_config = config->udp_socket_config;
    anjay->udp_listen_port = config->udp_listen_port;

    anjay->servers = _anjay_servers_create();

    if (_anjay_coap_socket_create(&anjay->coap_socket, NULL,
                                  config->msg_cache_size)) {
        return -1;
    }

    // buffers must be able to hold whole CoAP message + its length;
    // add a bit of extra space for length so that {in,out}_buffer_size
    // are exact limits for the CoAP message size
    const size_t extra_bytes_required = offsetof(anjay_coap_msg_t, header);
    anjay->in_buffer_size = config->in_buffer_size + extra_bytes_required;
    anjay->out_buffer_size = config->out_buffer_size + extra_bytes_required;
    anjay->in_buffer = (uint8_t *) malloc(anjay->in_buffer_size);
    anjay->out_buffer = (uint8_t *) malloc(anjay->out_buffer_size);

    if (_anjay_coap_stream_create(&anjay->comm_stream, anjay->coap_socket,
                                  anjay->in_buffer, anjay->in_buffer_size,
                                  anjay->out_buffer, anjay->out_buffer_size)) {
        _anjay_coap_socket_cleanup(&anjay->coap_socket);
        return -1;
    }

    anjay->sched = _anjay_sched_new(anjay);
    if (!anjay->sched) {
        return -1;
    }

    if (_anjay_observe_init(anjay, config->confirmable_notifications)) {
        return -1;
    }

    if ((config->sms_driver != NULL) != (config->local_msisdn != NULL)) {
        anjay_log(ERROR,
                  "inconsistent nullness of sms_driver and local_msisdn");
        return -1;
    }

    if (config->sms_driver) {
        anjay_log(ERROR, "SMS support not available in this version of Anjay");
        return -1;
    }

    return 0;
}

const char *anjay_get_version(void) {
    return ANJAY_VERSION;
}

anjay_t *anjay_new(const anjay_configuration_t *config) {
    anjay_t *out = (anjay_t *) calloc(1, sizeof(*out));
    if (out && init(out, config)) {
        anjay_delete(out);
        out = NULL;
    }
    return out;
}

void _anjay_release_server_stream_without_scheduling_queue(anjay_t *anjay) {
    if (avs_stream_net_setsock(anjay->comm_stream, NULL)) {
        anjay_log(ERROR, "could not set stream socket to NULL");
    }
}

void anjay_delete(anjay_t *anjay) {
    anjay_log(TRACE, "deleting anjay object");

    _anjay_bootstrap_cleanup(anjay);
    _anjay_servers_cleanup(anjay);
    _anjay_sched_del(anjay->sched, &anjay->reload_servers_sched_job_handle);

    _anjay_sched_delete(&anjay->sched);

    assert(avs_stream_net_getsock(anjay->comm_stream) == NULL);
    avs_stream_cleanup(&anjay->comm_stream);

    _anjay_dm_cleanup(anjay);
    _anjay_observe_cleanup(anjay);
    _anjay_notify_clear_queue(&anjay->scheduled_notify.queue);

    free(anjay->in_buffer);
    free(anjay->out_buffer);
    free(anjay);
}

static void split_query_string(char *query,
                               const char **out_key,
                               const char **out_value) {
    char *eq = strchr(query, '=');

    *out_key = query;

    if (eq) {
        *eq = '\0';
        *out_value = eq + 1;
    } else {
        *out_value = NULL;
    }
}

static int parse_nullable_time(const char *key_str,
                               const char *period_str,
                               bool *out_present,
                               time_t *out_value) {
    long long num;
    if (*out_present) {
        anjay_log(WARNING, "Duplicated attribute in query string: %s", key_str);
        return -1;
    } else if (!period_str) {
        *out_present = true;
        *out_value = ANJAY_ATTRIB_PERIOD_NONE;
        return 0;
    } else if (_anjay_safe_strtoll(period_str, &num) || num < 0) {
        return -1;
    } else {
        *out_present = true;
        *out_value = (time_t)num;
        return 0;
    }
}

static int parse_nullable_double(const char *key_str,
                                 const char *double_str,
                                 bool *out_present,
                                 double *out_value) {
    if (*out_present) {
        anjay_log(WARNING, "Duplicated attribute in query string: %s", key_str);
        return -1;
    } else if (!double_str) {
        *out_present = true;
        *out_value = ANJAY_ATTRIB_VALUE_NONE;
        return 0;
    } else if (_anjay_safe_strtod(double_str, out_value) || isnan(*out_value)) {
        return -1;
    } else {
        *out_present = true;
        return 0;
    }
}

#ifdef WITH_CON_ATTR
static int parse_con(const char *value,
                     bool *out_present,
                     anjay_dm_con_attr_t *out_value) {
    if (*out_present) {
        anjay_log(WARNING, "Duplicated attribute in query string: con");
        return -1;
    } else if (!value) {
        *out_present = true;
        *out_value = ANJAY_DM_CON_ATTR_DEFAULT;
        return 0;
    } else if (strcmp(value, "0") == 0) {
        *out_present = true;
        *out_value = ANJAY_DM_CON_ATTR_NON;
        return 0;
    } else if (strcmp(value, "1") == 0) {
        *out_present = true;
        *out_value = ANJAY_DM_CON_ATTR_CON;
        return 0;
    } else {
        anjay_log(WARNING, "Invalid con attribute value: %s", value);
        return -1;
    }
}
#endif // WITH_CON_ATTR

static int parse_attribute(anjay_request_attributes_t *out_attrs,
                           const char *key,
                           const char *value) {
    if (!strcmp(key, ANJAY_ATTR_PMIN)) {
        return parse_nullable_time(
                key, value, &out_attrs->has_min_period,
                &out_attrs->values.standard.common.min_period);
    } else if (!strcmp(key, ANJAY_ATTR_PMAX)) {
        return parse_nullable_time(
                key, value, &out_attrs->has_max_period,
                &out_attrs->values.standard.common.max_period);
    } else if (!strcmp(key, ANJAY_ATTR_GT)) {
        return parse_nullable_double(key, value, &out_attrs->has_greater_than,
                                     &out_attrs->values.standard.greater_than);
    } else if (!strcmp(key, ANJAY_ATTR_LT)) {
        return parse_nullable_double(key, value, &out_attrs->has_less_than,
                                     &out_attrs->values.standard.less_than);
    } else if (!strcmp(key, ANJAY_ATTR_ST)) {
        return parse_nullable_double(key, value, &out_attrs->has_step,
                                     &out_attrs->values.standard.step);
#ifdef WITH_CON_ATTR
    } else if (!strcmp(key, ANJAY_CUSTOM_ATTR_CON)) {
        return parse_con(value, &out_attrs->custom.has_con,
                         &out_attrs->values.custom.data.con);
#endif // WITH_CON_ATTR
    } else {
        anjay_log(ERROR, "unrecognized query string: %s = %s", key, value);
        return -1;
    }
}

static int parse_attributes(avs_stream_abstract_t *stream,
                            anjay_request_attributes_t *out_attrs) {
    anjay_coap_opt_iterator_t it = ANJAY_COAP_OPT_ITERATOR_EMPTY;
    memset(out_attrs, 0, sizeof(*out_attrs));
    out_attrs->values = ANJAY_DM_INTERNAL_RES_ATTRS_EMPTY;

    char buffer[ANJAY_MAX_URI_QUERY_SEGMENT_SIZE];
    size_t attr_size;

    int result;
    while ((result = _anjay_coap_stream_get_option_string_it(
                    stream, ANJAY_COAP_OPT_URI_QUERY, &it,
                    &attr_size, buffer, sizeof(buffer) - 1)) == 0) {
        const char *key;
        const char *value;

        buffer[attr_size] = '\0';
        split_query_string(buffer, &key, &value);

        if (parse_attribute(out_attrs, key, value)) {
            anjay_log(ERROR, "invalid query string: %s = %s", key, value);
            return -1;
        }
    }

    if (result < 0) {
        anjay_log(ERROR, "could not read Request-Query");
        return -1;
    }

    return 0;
}

static const char *action_to_string(anjay_request_action_t action) {
    switch (action) {
    case ANJAY_ACTION_READ:             return "Read";
    case ANJAY_ACTION_DISCOVER:         return "Discover";
    case ANJAY_ACTION_WRITE:            return "Write";
    case ANJAY_ACTION_WRITE_UPDATE:     return "Write (Update)";
    case ANJAY_ACTION_WRITE_ATTRIBUTES: return "Write Attributes";
    case ANJAY_ACTION_EXECUTE:          return "Execute";
    case ANJAY_ACTION_CREATE:           return "Create";
    case ANJAY_ACTION_DELETE:           return "Delete";
    case ANJAY_ACTION_CANCEL_OBSERVE:   return "Cancel Observe";
    case ANJAY_ACTION_BOOTSTRAP_FINISH: return "Bootstrap Finish";
    }
    assert(0 && "invalid enum value");
    return "<invalid action>";
}

static int code_to_action(uint8_t code,
                          uint16_t requested_format,
                          bool is_bs_uri,
                          bool has_iid,
                          bool has_rid,
                          bool has_content_format,
                          anjay_request_action_t *out_action) {
    switch (code) {
    case ANJAY_COAP_CODE_GET:
        if (requested_format == ANJAY_COAP_FORMAT_APPLICATION_LINK) {
            *out_action = ANJAY_ACTION_DISCOVER;
        } else {
            *out_action = ANJAY_ACTION_READ;
        }
        return 0;
    case ANJAY_COAP_CODE_POST:
        if (is_bs_uri) {
            *out_action = ANJAY_ACTION_BOOTSTRAP_FINISH;
        } else if (has_rid) {
            *out_action = ANJAY_ACTION_EXECUTE;
        } else if (has_iid) {
            *out_action = ANJAY_ACTION_WRITE_UPDATE;
        } else {
            *out_action = ANJAY_ACTION_CREATE;
        }
        return 0;
    case ANJAY_COAP_CODE_PUT:
        if (has_content_format) {
            *out_action = ANJAY_ACTION_WRITE;
        } else {
            *out_action = ANJAY_ACTION_WRITE_ATTRIBUTES;
        }
        return 0;
    case ANJAY_COAP_CODE_DELETE:
        *out_action = ANJAY_ACTION_DELETE;
        return 0;
    default:
        anjay_log(ERROR, "unrecognized CoAP method: %s",
                  ANJAY_COAP_CODE_STRING(code));
        return -1;
    }
}

static int get_msg_action(anjay_coap_msg_type_t msg_type,
                          uint8_t code,
                          uint16_t requested_format,
                          bool is_bs_uri,
                          bool has_iid,
                          bool has_rid,
                          bool has_content_format,
                          anjay_request_action_t *out_action) {
    int result = 0;
    switch (msg_type) {
    case ANJAY_COAP_MSG_RESET:
        *out_action = ANJAY_ACTION_CANCEL_OBSERVE;
        break;
    case ANJAY_COAP_MSG_CONFIRMABLE:
        result = code_to_action(code, requested_format, is_bs_uri, has_iid,
                                has_rid, has_content_format, out_action);
        break;
    default:
        anjay_log(ERROR, "invalid CoAP message type: %d", (int) msg_type);
        return -1;
    }

    if (!result) {
        anjay_log(DEBUG, "LwM2M action: %s", action_to_string(*out_action));
    }
    return result;
}

static int parse_type_and_code(avs_stream_abstract_t *stream,
                               anjay_request_details_t *inout_details) {
    int result;

    if ((result = _anjay_coap_stream_get_msg_type(stream,
                                                  &inout_details->msg_type))
            || (result = _anjay_coap_stream_get_code(
                    stream, &inout_details->request_code))) {
        return result;
    }

    anjay_log(DEBUG, "CoAP method: %s",
              ANJAY_COAP_CODE_STRING(inout_details->request_code));
    return 0;
}

static int parse_action(avs_stream_abstract_t *stream,
                        anjay_request_details_t *inout_details) {
    if (_anjay_coap_stream_get_option_uint(
            stream, ANJAY_COAP_OPT_ACCEPT, &inout_details->requested_format,
            sizeof(inout_details->requested_format))) {
        inout_details->requested_format = ANJAY_COAP_FORMAT_NONE;
    }

    return get_msg_action(inout_details->msg_type,
                          inout_details->request_code,
                          inout_details->requested_format,
                          inout_details->is_bs_uri,
                          inout_details->uri.has_iid,
                          inout_details->uri.has_rid,
                          inout_details->content_format
                                  != ANJAY_COAP_FORMAT_NONE,
                          &inout_details->action);
}

static int parse_request_uri_segment(const char *uri,
                                     uint16_t *out_id,
                                     uint16_t max_valid_id) {
    long long num;
    if (_anjay_safe_strtoll(uri, &num)
            || num < 0
            || num > max_valid_id) {
        anjay_log(ERROR, "invalid Uri-Path segment: %s", uri);
        return -1;
    }

    *out_id = (uint16_t)num;
    return 0;
}

static int parse_bs_uri(avs_stream_abstract_t *stream, bool *out_is_bs) {
    char uri[ANJAY_MAX_URI_SEGMENT_SIZE] = "";
    size_t uri_size;

    *out_is_bs = false;

    anjay_coap_opt_iterator_t optit = ANJAY_COAP_OPT_ITERATOR_EMPTY;
    int result = _anjay_coap_stream_get_option_string_it(
            stream, ANJAY_COAP_OPT_URI_PATH, &optit,
            &uri_size, uri, sizeof(uri) - 1);
    if (result) {
        return (result == ANJAY_COAP_OPTION_MISSING) ? 0 : result;
    }

    if (strcmp(uri, "bs") == 0) {
        result = _anjay_coap_stream_get_option_string_it(
                stream, ANJAY_COAP_OPT_URI_PATH, &optit,
                &uri_size, uri, sizeof(uri) - 1);
        if (result == ANJAY_COAP_OPTION_MISSING) {
            *out_is_bs = true;
            return 0;
        }
    }

    return result;
}

static int parse_dm_uri(avs_stream_abstract_t *stream,
                        anjay_uri_path_t *out_uri) {
    char uri[ANJAY_MAX_URI_SEGMENT_SIZE] = "";
    size_t uri_size;

    out_uri->has_oid = false;
    out_uri->has_iid = false;
    out_uri->has_rid = false;

    struct {
        uint16_t *id;
        bool *has_id;
        uint16_t max_valid_value;
    } ids[] = {
        { &out_uri->oid, &out_uri->has_oid, UINT16_MAX },
        { &out_uri->iid, &out_uri->has_iid, UINT16_MAX - 1 },
        { &out_uri->rid, &out_uri->has_rid, UINT16_MAX }
    };

    anjay_coap_opt_iterator_t optit = ANJAY_COAP_OPT_ITERATOR_EMPTY;
    for (size_t i = 0; i < ANJAY_ARRAY_SIZE(ids); ++i) {
        int result = _anjay_coap_stream_get_option_string_it(
                    stream, ANJAY_COAP_OPT_URI_PATH, &optit,
                    &uri_size, uri, sizeof(uri) - 1);
        if (result == ANJAY_COAP_OPTION_MISSING) {
            return 0;
        } else if (result) {
            return result;
        }

        if (parse_request_uri_segment(uri, ids[i].id, ids[i].max_valid_value)) {
            return -1;
        }
        *ids[i].has_id = true;
    }

    // 3 or more segments...
    if (_anjay_coap_stream_get_option_string_it(
                    stream, ANJAY_COAP_OPT_URI_PATH, &optit,
                    &uri_size, uri, sizeof(uri) - 1)
            != ANJAY_COAP_OPTION_MISSING) {
        anjay_log(ERROR, "prefixed Uri-Path are not supported");
        return -1;
    }
    return 0;
}

static int parse_request_uri(avs_stream_abstract_t *stream,
                             bool *out_is_bs,
                             anjay_uri_path_t *out_uri) {
    int result = parse_bs_uri(stream, out_is_bs);
    if (result) {
        return result;
    }
    if (*out_is_bs) {
        out_uri->has_oid = false;
        out_uri->has_iid = false;
        out_uri->has_rid = false;
        return 0;
    } else {
        return parse_dm_uri(stream, out_uri);
    }
}

static int parse_observe(avs_stream_abstract_t *stream,
                         anjay_coap_observe_t *out) {
    uint32_t raw_value;
    int retval = _anjay_coap_stream_get_option_uint(
            stream, ANJAY_COAP_OPT_OBSERVE, &raw_value, sizeof(raw_value));
    if (retval == ANJAY_COAP_OPTION_MISSING) {
        *out = ANJAY_COAP_OBSERVE_NONE;
        return 0;
    } else if (retval) {
        return retval;
    }
    switch (raw_value) {
    case 0:
        *out = ANJAY_COAP_OBSERVE_REGISTER;
        return 0;
    case 1:
        *out = ANJAY_COAP_OBSERVE_DEREGISTER;
        return 0;
    default:
        anjay_log(ERROR, "Invalid value for Observe request");
        return -1;
    }
}

static int parse_request_options(avs_stream_abstract_t *stream,
                                 anjay_request_details_t *out_details) {
    if (parse_observe(stream, &out_details->observe)
        || parse_request_uri(stream, &out_details->is_bs_uri, &out_details->uri)
        || parse_attributes(stream, &out_details->attributes)
        || _anjay_coap_stream_get_content_format(stream,
                                                 &out_details->content_format)
        || parse_action(stream, out_details)
        || _anjay_coap_stream_get_request_identity(
                   stream, &out_details->request_identity)) {
        return -1;
    }
    return 0;
}

uint8_t _anjay_make_error_response_code(int handler_result) {
    uint8_t handler_code = (uint8_t)(-handler_result);
    int cls = _anjay_coap_msg_code_get_class(&handler_code);
    if (cls == 4 || cls == 5) {
        return handler_code;
    } else switch (handler_result) {
    case ANJAY_OUTCTXERR_FORMAT_MISMATCH:
        return ANJAY_COAP_CODE_NOT_ACCEPTABLE;
    default:
        anjay_log(ERROR, "invalid error code: %d", handler_result);
        return -ANJAY_ERR_INTERNAL;
    }
}

static bool critical_option_validator(uint8_t msg_code, uint32_t optnum) {
    /* Note: BLOCK Options are handled inside stream.c */
    switch (msg_code) {
    case ANJAY_COAP_CODE_GET:
        return optnum == ANJAY_COAP_OPT_URI_PATH
            || optnum == ANJAY_COAP_OPT_ACCEPT;
    case ANJAY_COAP_CODE_PUT:
    case ANJAY_COAP_CODE_POST:
        return optnum == ANJAY_COAP_OPT_URI_PATH
            || optnum == ANJAY_COAP_OPT_URI_QUERY;
    case ANJAY_COAP_CODE_DELETE:
        return optnum == ANJAY_COAP_OPT_URI_PATH;
    default:
        return false;
    }
}

static int handle_request(anjay_t *anjay,
                          const anjay_request_details_t *details) {
    int result = -1;

    if (details->ssid == ANJAY_SSID_BOOTSTRAP) {
        result = _anjay_bootstrap_perform_action(anjay, details);
    } else {
        result = _anjay_dm_perform_action(anjay, details);
    }

    if (result) {
        uint8_t error_code = _anjay_make_error_response_code(result);

        if (_anjay_coap_msg_code_is_client_error(error_code)) {
            // the request was invalid; that's not really an error on our side,
            anjay_log(TRACE, "invalid request: %s",
                      ANJAY_COAP_CODE_STRING(details->request_code));
            result = 0;
        } else {
            anjay_log(ERROR, "could not handle request: %s",
                      ANJAY_COAP_CODE_STRING(details->request_code));
        }

        if (_anjay_coap_stream_set_error(anjay->comm_stream, error_code)) {
            anjay_log(ERROR, "could not setup error response");
            return -1;
        }
    }

    int finish_result = 0;
    if (details->msg_type == ANJAY_COAP_MSG_CONFIRMABLE) {
        finish_result = avs_stream_finish_message(anjay->comm_stream);
    }

    if (details->ssid != ANJAY_SSID_BOOTSTRAP) {
        _anjay_observe_sched_flush(anjay, details->ssid, details->conn_type);
    }
    return result ? result : finish_result;
}

static int handle_incoming_message(anjay_t *anjay,
                                   anjay_connection_ref_t connection) {
    int result = -1;
    anjay_request_details_t details;
    memset(&details, 0, sizeof(details));
    details.ssid = connection.server->ssid;
    details.conn_type = connection.conn_type;

    if (details.ssid == ANJAY_SSID_BOOTSTRAP) {
        anjay_log(DEBUG, "bootstrap server");
    } else {
        anjay_log(DEBUG, "server ID = %u", details.ssid);
    }

    if ((result = parse_type_and_code(anjay->comm_stream, &details))) {
        if (result == ANJAY_COAP_SOCKET_ERR_DUPLICATE) {
            anjay_log(TRACE, "duplicate request received");
            result = 0;
        } else if (result == ANJAY_COAP_SOCKET_ERR_MSG_WAS_PING) {
            anjay_log(TRACE, "received CoAP ping");
            result = 0;
        } else {
            anjay_log(ERROR, "received packet is not a valid CoAP message");
        }
        goto cleanup;
    }

    if (parse_request_options(anjay->comm_stream, &details)
            || _anjay_coap_stream_validate_critical_options(
                    anjay->comm_stream, critical_option_validator)) {
        if (_anjay_coap_msg_code_is_request(details.request_code)) {
            if (_anjay_coap_stream_set_error(anjay->comm_stream,
                                             -ANJAY_ERR_BAD_OPTION)
                    || avs_stream_finish_message(anjay->comm_stream)) {
                anjay_log(WARNING, "could not send Bad Option response");
            }
        }
        goto cleanup;
    }

    result = handle_request(anjay, &details);

cleanup:
    avs_stream_reset(anjay->comm_stream);
    return result;
}

anjay_server_connection_t *
_anjay_get_server_connection(anjay_connection_ref_t ref) {
    switch (ref.conn_type) {
    case ANJAY_CONNECTION_UDP:
        return &ref.server->udp_connection;
    default:
        return NULL;
    }
}

anjay_connection_type_t
_anjay_get_default_connection_type(anjay_active_server_info_t *server) {
    const anjay_connection_ref_t sms_ref = {
        .server = server,
        .conn_type = ANJAY_CONNECTION_SMS
    };
    const anjay_connection_ref_t udp_ref = {
        .server = server,
        .conn_type = ANJAY_CONNECTION_UDP
    };
    if (_anjay_connection_current_mode(sms_ref) != ANJAY_CONNECTION_DISABLED
            && _anjay_connection_current_mode(udp_ref)
                    == ANJAY_CONNECTION_DISABLED) {
        return ANJAY_CONNECTION_SMS;
    } else {
        (void) server;
        return ANJAY_CONNECTION_UDP;
    }
}

int _anjay_bind_server_stream(anjay_t *anjay, anjay_connection_ref_t ref) {
    const coap_transmission_params_t *tx_params;
    switch (ref.conn_type) {
    case ANJAY_CONNECTION_UDP:
        tx_params = &_anjay_coap_DEFAULT_TX_PARAMS;
        break;
    case ANJAY_CONNECTION_SMS:
        tx_params = &_anjay_coap_SMS_TX_PARAMS;
        break;
    default:
        assert(0 && "Should never happen");
        return -1;
    }

    anjay_server_connection_t *connection = _anjay_get_server_connection(ref);
    if (!connection) {
        anjay_log(ERROR, "could not get server connection");
        return -1;
    }

    avs_net_abstract_socket_t *socket = _anjay_connection_get_prepared_socket(
            anjay, ref.server, connection);
    if (!socket
            || avs_stream_net_setsock(anjay->comm_stream, socket)
            || _anjay_coap_stream_set_tx_params(anjay->comm_stream,
                                                tx_params)) {
        anjay_log(ERROR, "could not set stream socket");
        return -1;
    }

    return 0;
}

typedef struct {
    anjay_ssid_t ssid;
    uint16_t conn_type; // semantically anjay_connection_type_t
} queue_mode_close_socket_args_t;

static void *
queue_mode_close_socket_args_encode(queue_mode_close_socket_args_t args) {
    AVS_STATIC_ASSERT(sizeof(void *) >= sizeof(queue_mode_close_socket_args_t),
                      pointer_big_enough);
    // ensure that ANJAY_CONNECTION_WILDCARD is the last value
    assert(args.conn_type < ANJAY_CONNECTION_WILDCARD);
    void *result = NULL;
    memcpy(&result, &args, sizeof(args));
    return result;
}

static queue_mode_close_socket_args_t
queue_mode_close_socket_args_decode(void *value) {
    queue_mode_close_socket_args_t result;
    memcpy(&result, &value, sizeof(result));
    assert(result.conn_type < ANJAY_CONNECTION_WILDCARD);
    return result;
}

static int queue_mode_close_socket(anjay_t *anjay, void *args_) {
    queue_mode_close_socket_args_t args =
            queue_mode_close_socket_args_decode(args_);
    anjay_connection_ref_t ref = {
        .server = _anjay_servers_find_active(&anjay->servers, args.ssid),
        .conn_type = (anjay_connection_type_t) args.conn_type
    };
    if (!ref.server) {
        return -1;
    }
    _anjay_connection_suspend(ref);
    return 0;
}

static void queue_mode_activate_socket(anjay_t *anjay,
                                       anjay_connection_ref_t ref) {
    assert(anjay->comm_stream);

    anjay_server_connection_t *connection = _anjay_get_server_connection(ref);
    assert(connection);
    assert(connection->queue_mode_close_socket_clb_handle == NULL);

    coap_transmission_params_t tx_params;
    if (_anjay_coap_stream_get_tx_params(anjay->comm_stream, &tx_params)) {
        anjay_log(ERROR, "could not get current CoAP transmission parameters");
    }

    struct timespec delay;
    _anjay_time_from_ms(&delay,
                        _anjay_coap_max_transmit_wait_ms(&tx_params));

    queue_mode_close_socket_args_t args = {
        .ssid = ref.server->ssid,
        .conn_type = (uint16_t) ref.conn_type
    };
    // see comment on field declaration for logic summary
    if (_anjay_sched(anjay->sched,
                     &connection->queue_mode_close_socket_clb_handle,
                     delay, queue_mode_close_socket,
                     queue_mode_close_socket_args_encode(args))) {
        anjay_log(ERROR, "could not schedule queue mode operations");
    }
}

void _anjay_release_server_stream(anjay_t *anjay,
                                  anjay_connection_ref_t ref) {
    anjay_server_connection_t *connection = _anjay_get_server_connection(ref);
    assert(connection);
    _anjay_sched_del(anjay->sched,
                     &connection->queue_mode_close_socket_clb_handle);

    if (connection->queue_mode) {
        queue_mode_activate_socket(anjay, ref);
    }

    _anjay_release_server_stream_without_scheduling_queue(anjay);
}

size_t _anjay_num_non_bootstrap_servers(anjay_t *anjay) {
    size_t num_servers = 0;
    {
        AVS_LIST(anjay_active_server_info_t) it;
        AVS_LIST_FOREACH(it, anjay->servers.active) {
            if (it->ssid != ANJAY_SSID_BOOTSTRAP) {
                ++num_servers;
            }
        }
    }
    {
        AVS_LIST(anjay_inactive_server_info_t) it;
        AVS_LIST_FOREACH(it, anjay->servers.inactive) {
            if (it->ssid != ANJAY_SSID_BOOTSTRAP) {
                ++num_servers;
            }
        }
    }
    return num_servers;
}

static int udp_serve(anjay_t *anjay,
                     avs_net_abstract_socket_t *ready_socket) {
    anjay_connection_ref_t connection = {
        .server = _anjay_servers_find_by_udp_socket(&anjay->servers,
                                                    ready_socket),
        .conn_type = ANJAY_CONNECTION_UDP
    };
    if (!connection.server
            || _anjay_bind_server_stream(anjay, connection)) {
        return -1;
    }

    int result = handle_incoming_message(anjay, connection);
    _anjay_release_server_stream(anjay, connection);
    return result;
}

static int sms_serve(anjay_t *anjay) {
    (void) anjay;
    assert(0 && "SMS not supported in this version of Anjay");
    return -1;
}

int anjay_serve(anjay_t *anjay,
                avs_net_abstract_socket_t *ready_socket) {
    if (_anjay_sms_router(anjay)
            && ready_socket == _anjay_sms_poll_socket(anjay)) {
        return sms_serve(anjay);
    }
    return udp_serve(anjay, ready_socket);
}

int anjay_sched_time_to_next(anjay_t *anjay,
                             struct timespec *out_delay) {
    return _anjay_sched_time_to_next(anjay->sched, out_delay);
}

int anjay_sched_time_to_next_ms(anjay_t *anjay, int *out_delay_ms) {
    struct timespec delay;
    int result = anjay_sched_time_to_next(anjay, &delay);
    if (!result) {
        *out_delay_ms = (int) (delay.tv_sec * 1000 + delay.tv_nsec / 1000000);
    }
    return result;
}

int anjay_sched_calculate_wait_time_ms(anjay_t *anjay, int limit_ms) {
    int time_to_next_ms;
    if (!anjay_sched_time_to_next_ms(anjay, &time_to_next_ms)
            && time_to_next_ms < limit_ms) {
        return time_to_next_ms;
    }
    return limit_ms;
}

int anjay_sched_run(anjay_t *anjay) {
    ssize_t tasks_executed = _anjay_sched_run(anjay->sched);
    if (tasks_executed < 0) {
        anjay_log(ERROR, "sched_run failed");
        return -1;
    }

    if (tasks_executed > 0) {
        anjay_log(DEBUG, "executed %ld tasks", (long)tasks_executed);
    }

    return 0;
}

void anjay_smsdrv_cleanup(anjay_smsdrv_t **smsdrv_ptr) {
    if (*smsdrv_ptr) {
        assert(0 && "SMS drivers not supported by this version of Anjay");
    }
}


#ifdef ANJAY_TEST
#include "test/anjay.c"
#endif // ANJAY_TEST
