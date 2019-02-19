/*
 * Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
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

#include <anjay_config.h>

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <avsystem/commons/errno.h>
#include <avsystem/commons/memory.h>
#include <avsystem/commons/stream/stream_net.h>
#include <avsystem/commons/stream_v_table.h>

#include <anjay/core.h>
#include <anjay/stats.h>

#include <anjay_modules/time_defs.h>

#include <anjay_config_log.h>

#include "anjay_core.h"
#include "coap/coap_stream.h"
#include "coap/content_format.h"
#include "coap/id_source/auto.h"
#include "dm_core.h"
#include "downloader.h"
#include "interface/bootstrap_core.h"
#include "interface/register.h"
#include "io_core.h"
#include "servers_utils.h"
#include "utils_core.h"

VISIBILITY_SOURCE_BEGIN

static int init(anjay_t *anjay, const anjay_configuration_t *config) {
    _anjay_bootstrap_init(&anjay->bootstrap,
                          !config->disable_server_initiated_bootstrap);
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
    anjay->current_connection.conn_type = ANJAY_CONNECTION_UNSET;

    const char *error_msg;
    if (config->udp_tx_params) {
        if (!avs_coap_tx_params_valid(config->udp_tx_params, &error_msg)) {
            anjay_log(ERROR, "UDP CoAP transmission parameters are invalid: %s",
                      error_msg);
            return -1;
        }
        anjay->udp_tx_params = *config->udp_tx_params;
    } else {
        anjay->udp_tx_params =
                (avs_coap_tx_params_t) ANJAY_COAP_DEFAULT_UDP_TX_PARAMS;
    }

    if (config->udp_dtls_hs_tx_params) {
        if (!avs_time_duration_less(config->udp_dtls_hs_tx_params->min,
                                    config->udp_dtls_hs_tx_params->max)) {
            anjay_log(ERROR, "UDP DTLS Handshake transmission parameters are "
                             "invalid: min >= max");
            return -1;
        }
        anjay->udp_dtls_hs_tx_params = *config->udp_dtls_hs_tx_params;
    } else {
        anjay->udp_dtls_hs_tx_params = (avs_net_dtls_handshake_timeouts_t)
                ANJAY_DTLS_DEFAULT_UDP_HS_TX_PARAMS;
    }


    anjay->servers = _anjay_servers_create();
    if (!anjay->servers) {
        anjay_log(ERROR, "Out of memory");
        return -1;
    }

    if (avs_coap_ctx_create(&anjay->coap_ctx, config->msg_cache_size)) {
        anjay_log(ERROR, "Could not create CoAP context");
        return -1;
    }

    // buffers must be able to hold whole CoAP message + its length;
    // add a bit of extra space for length so that {in,out}_buffer_size
    // are exact limits for the CoAP message size
    const size_t extra_bytes_required = offsetof(avs_coap_msg_t, content);
    anjay->in_buffer_size = config->in_buffer_size + extra_bytes_required;
    anjay->out_buffer_size = config->out_buffer_size + extra_bytes_required;
    anjay->in_buffer = (uint8_t *) avs_malloc(anjay->in_buffer_size);
    anjay->out_buffer = (uint8_t *) avs_malloc(anjay->out_buffer_size);

    if (_anjay_coap_stream_create(&anjay->comm_stream, anjay->coap_ctx,
                                  anjay->in_buffer, anjay->in_buffer_size,
                                  anjay->out_buffer, anjay->out_buffer_size)) {
        avs_coap_ctx_cleanup(&anjay->coap_ctx);
        return -1;
    }

    anjay->sched = _anjay_sched_new(anjay);
    if (!anjay->sched) {
        anjay_log(ERROR, "Out of memory");
        return -1;
    }

    if (_anjay_observe_init(&anjay->observe,
                            config->confirmable_notifications,
                            config->stored_notification_limit)) {
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

    coap_id_source_t *id_source = NULL;
#ifdef WITH_BLOCK_DOWNLOAD
    if (!(id_source = _anjay_coap_id_source_auto_new(
                  0, AVS_COAP_MAX_TOKEN_LENGTH))) {
        anjay_log(ERROR, "Out of memory");
        return -1;
    }
#endif // WITH_BLOCK_DOWNLOAD
#ifdef WITH_DOWNLOADER
    if (_anjay_downloader_init(&anjay->downloader, anjay, &id_source)) {
        _anjay_coap_id_source_release(&id_source);
        return -1;
    }
#endif // WITH_DOWNLOADER
    assert(!id_source);

    return 0;
}

const char *anjay_get_version(void) {
    return ANJAY_VERSION;
}

anjay_t *anjay_new(const anjay_configuration_t *config) {
    anjay_log(INFO, "Initializing Anjay " ANJAY_VERSION);
    _anjay_log_feature_list();
    anjay_t *out = (anjay_t *) avs_calloc(1, sizeof(*out));
    if (!out) {
        anjay_log(ERROR, "Out of memory");
        return NULL;
    }
    if (init(out, config)) {
        anjay_delete(out);
        return NULL;
    }
    return out;
}

void _anjay_release_server_stream_without_scheduling_queue(anjay_t *anjay) {
    anjay->current_connection.server = NULL;
    anjay->current_connection.conn_type = ANJAY_CONNECTION_UNSET;
    avs_stream_reset(anjay->comm_stream);
    if (avs_stream_net_setsock(anjay->comm_stream, NULL)) {
        anjay_log(ERROR, "could not set stream socket to NULL");
    }
}

static void anjay_delete_impl(anjay_t *anjay, bool deregister) {
    anjay_log(TRACE, "deleting anjay object");

    // we want to clear this now so that notifications won't be sent during
    // _anjay_sched_delete()
    _anjay_observe_cleanup(&anjay->observe, anjay->sched);

#ifdef WITH_DOWNLOADER
    _anjay_downloader_cleanup(&anjay->downloader);
#endif // WITH_DOWNLOADER

    _anjay_bootstrap_cleanup(anjay);
    if (deregister) {
        _anjay_servers_deregister(anjay);
    }

    // Make sure to deregister from all servers *before* cleaning up the
    // scheduler. That prevents us from updating a registration even though
    // we're about to deregister anyway.
    _anjay_sched_del(anjay->sched, &anjay->reload_servers_sched_job_handle);
    _anjay_sched_del(anjay->sched, &anjay->scheduled_notify.handle);
    _anjay_sched_delete(&anjay->sched);

    // Note: this MUST NOT be called before _anjay_sched_del(), because
    // it avs_free()s anjay->servers, which might be used without null-guards in
    // scheduled jobs
    _anjay_servers_cleanup(anjay);

    assert(avs_stream_net_getsock(anjay->comm_stream) == NULL);
    avs_stream_cleanup(&anjay->comm_stream);

    _anjay_dm_cleanup(anjay);
    _anjay_notify_clear_queue(&anjay->scheduled_notify.queue);

    avs_free(anjay->in_buffer);
    avs_free(anjay->out_buffer);
    avs_free(anjay);
}

void anjay_delete(anjay_t *anjay) {
    anjay_delete_impl(anjay, true);
}

static void
split_query_string(char *query, const char **out_key, const char **out_value) {
    char *eq = strchr(query, '=');

    *out_key = query;

    if (eq) {
        *eq = '\0';
        *out_value = eq + 1;
    } else {
        *out_value = NULL;
    }
}

static int parse_nullable_period(const char *key_str,
                                 const char *period_str,
                                 bool *out_present,
                                 int32_t *out_value) {
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
        *out_value = (int32_t) num;
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
        return parse_nullable_period(
                key, value, &out_attrs->has_min_period,
                &out_attrs->values.standard.common.min_period);
    } else if (!strcmp(key, ANJAY_ATTR_PMAX)) {
        return parse_nullable_period(
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
        anjay_log(ERROR, "unrecognized query string: %s = %s", key,
                  value ? value : "(null)");
        return -1;
    }
}

static int parse_attributes(const avs_coap_msg_t *msg,
                            anjay_request_attributes_t *out_attrs) {
    memset(out_attrs, 0, sizeof(*out_attrs));
    out_attrs->values = ANJAY_DM_INTERNAL_RES_ATTRS_EMPTY;

    char buffer[ANJAY_MAX_URI_QUERY_SEGMENT_SIZE];
    size_t attr_size;

    int result;
    avs_coap_opt_iterator_t it = AVS_COAP_OPT_ITERATOR_EMPTY;
    while ((result = avs_coap_msg_get_option_string_it(
                    msg, AVS_COAP_OPT_URI_QUERY, &it, &attr_size, buffer,
                    sizeof(buffer) - 1))
           == 0) {
        const char *key;
        const char *value;

        buffer[attr_size] = '\0';
        split_query_string(buffer, &key, &value);
        assert(key != NULL);

        if (parse_attribute(out_attrs, key, value)) {
            anjay_log(ERROR, "invalid query string: %s = %s", key,
                      value ? value : "(null)");
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
    case ANJAY_ACTION_READ:
        return "Read";
    case ANJAY_ACTION_DISCOVER:
        return "Discover";
    case ANJAY_ACTION_WRITE:
        return "Write";
    case ANJAY_ACTION_WRITE_UPDATE:
        return "Write (Update)";
    case ANJAY_ACTION_WRITE_ATTRIBUTES:
        return "Write Attributes";
    case ANJAY_ACTION_EXECUTE:
        return "Execute";
    case ANJAY_ACTION_CREATE:
        return "Create";
    case ANJAY_ACTION_DELETE:
        return "Delete";
    case ANJAY_ACTION_CANCEL_OBSERVE:
        return "Cancel Observe";
    case ANJAY_ACTION_BOOTSTRAP_FINISH:
        return "Bootstrap Finish";
    }
    AVS_UNREACHABLE("invalid enum value");
    return "<invalid action>";
}

static int code_to_action(uint8_t code,
                          uint16_t requested_format,
                          bool is_bs_uri,
                          anjay_uri_path_type_t path_type,
                          bool has_content_format,
                          anjay_request_action_t *out_action) {
    switch (code) {
    case AVS_COAP_CODE_GET:
        if (requested_format == ANJAY_COAP_FORMAT_APPLICATION_LINK) {
            *out_action = ANJAY_ACTION_DISCOVER;
        } else {
            *out_action = ANJAY_ACTION_READ;
        }
        return 0;
    case AVS_COAP_CODE_POST:
        if (is_bs_uri) {
            *out_action = ANJAY_ACTION_BOOTSTRAP_FINISH;
        } else {
            switch (path_type) {
            case ANJAY_PATH_RESOURCE:
                *out_action = ANJAY_ACTION_EXECUTE;
                break;
            case ANJAY_PATH_INSTANCE:
                *out_action = ANJAY_ACTION_WRITE_UPDATE;
                break;
            case ANJAY_PATH_OBJECT:
            case ANJAY_PATH_ROOT:
                *out_action = ANJAY_ACTION_CREATE;
                break;
            }
        }
        return 0;
    case AVS_COAP_CODE_PUT:
        if (has_content_format) {
            *out_action = ANJAY_ACTION_WRITE;
        } else {
            *out_action = ANJAY_ACTION_WRITE_ATTRIBUTES;
        }
        return 0;
    case AVS_COAP_CODE_DELETE:
        *out_action = ANJAY_ACTION_DELETE;
        return 0;
    default:
        anjay_log(ERROR, "unrecognized CoAP method: %s",
                  AVS_COAP_CODE_STRING(code));
        return -1;
    }
}

static int get_msg_action(avs_coap_msg_type_t msg_type,
                          uint8_t code,
                          uint16_t requested_format,
                          bool is_bs_uri,
                          anjay_uri_path_type_t path_type,
                          bool has_content_format,
                          anjay_request_action_t *out_action) {
    int result = 0;
    switch (msg_type) {
    case AVS_COAP_MSG_RESET:
        *out_action = ANJAY_ACTION_CANCEL_OBSERVE;
        break;
    case AVS_COAP_MSG_CONFIRMABLE:
        result = code_to_action(code, requested_format, is_bs_uri, path_type,
                                has_content_format, out_action);
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

static int parse_action(const avs_coap_msg_t *msg,
                        anjay_request_t *inout_request) {
    if (avs_coap_msg_get_option_u16(msg, AVS_COAP_OPT_ACCEPT,
                                    &inout_request->requested_format)) {
        inout_request->requested_format = AVS_COAP_FORMAT_NONE;
    }

    return get_msg_action(inout_request->msg_type,
                          inout_request->request_code,
                          inout_request->requested_format,
                          inout_request->is_bs_uri,
                          inout_request->uri.type,
                          inout_request->content_format != AVS_COAP_FORMAT_NONE,
                          &inout_request->action);
}

static int parse_request_uri_segment(const char *uri,
                                     uint16_t *out_id,
                                     uint16_t max_valid_id) {
    long long num;
    if (_anjay_safe_strtoll(uri, &num) || num < 0 || num > max_valid_id) {
        anjay_log(ERROR, "invalid Uri-Path segment: %s", uri);
        return -1;
    }

    *out_id = (uint16_t) num;
    return 0;
}

static int parse_bs_uri(const avs_coap_msg_t *msg, bool *out_is_bs) {
    char uri[ANJAY_MAX_URI_SEGMENT_SIZE] = "";
    size_t uri_size;

    *out_is_bs = false;

    avs_coap_opt_iterator_t optit = AVS_COAP_OPT_ITERATOR_EMPTY;
    int result = avs_coap_msg_get_option_string_it(msg, AVS_COAP_OPT_URI_PATH,
                                                   &optit, &uri_size, uri,
                                                   sizeof(uri) - 1);
    if (result) {
        return (result == AVS_COAP_OPTION_MISSING) ? 0 : result;
    }

    if (strcmp(uri, "bs") == 0) {
        result = avs_coap_msg_get_option_string_it(msg, AVS_COAP_OPT_URI_PATH,
                                                   &optit, &uri_size, uri,
                                                   sizeof(uri) - 1);
        if (result == AVS_COAP_OPTION_MISSING) {
            *out_is_bs = true;
            return 0;
        }
    }

    return result;
}

static int parse_dm_uri(const avs_coap_msg_t *msg, anjay_uri_path_t *out_uri) {
    char uri[ANJAY_MAX_URI_SEGMENT_SIZE] = "";
    size_t uri_size;

    out_uri->type = ANJAY_PATH_ROOT;

    struct {
        uint16_t *id;
        anjay_uri_path_type_t type;
        uint16_t max_valid_value;
    } ids[] = { { &out_uri->oid, ANJAY_PATH_OBJECT, UINT16_MAX },
                { &out_uri->iid, ANJAY_PATH_INSTANCE, UINT16_MAX - 1 },
                { &out_uri->rid, ANJAY_PATH_RESOURCE, UINT16_MAX } };

    avs_coap_opt_iterator_t optit = AVS_COAP_OPT_ITERATOR_EMPTY;
    for (size_t i = 0; i < AVS_ARRAY_SIZE(ids); ++i) {
        int result =
                avs_coap_msg_get_option_string_it(msg, AVS_COAP_OPT_URI_PATH,
                                                  &optit, &uri_size, uri,
                                                  sizeof(uri) - 1);
        if (result == AVS_COAP_OPTION_MISSING) {
            return 0;
        } else if (result) {
            return result;
        }

        if (parse_request_uri_segment(uri, ids[i].id, ids[i].max_valid_value)) {
            return -1;
        }
        out_uri->type = ids[i].type;
    }

    // 3 or more segments...
    if (avs_coap_msg_get_option_string_it(msg, AVS_COAP_OPT_URI_PATH, &optit,
                                          &uri_size, uri, sizeof(uri) - 1)
            != AVS_COAP_OPTION_MISSING) {
        anjay_log(ERROR, "prefixed Uri-Path are not supported");
        return -1;
    }
    return 0;
}

static int parse_request_uri(const avs_coap_msg_t *msg,
                             bool *out_is_bs,
                             anjay_uri_path_t *out_uri) {
    int result = parse_bs_uri(msg, out_is_bs);
    if (result) {
        return result;
    }
    if (*out_is_bs) {
        out_uri->type = ANJAY_PATH_ROOT;
        return 0;
    } else {
        return parse_dm_uri(msg, out_uri);
    }
}

static int parse_observe(const avs_coap_msg_t *msg, anjay_coap_observe_t *out) {
    uint32_t raw_value;
    int retval =
            avs_coap_msg_get_option_u32(msg, AVS_COAP_OPT_OBSERVE, &raw_value);
    if (retval == AVS_COAP_OPTION_MISSING) {
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

static int parse_request(const avs_coap_msg_t *msg,
                         anjay_request_t *out_request) {
    memset(out_request, 0, sizeof(*out_request));
    out_request->msg_type = avs_coap_msg_get_type(msg);
    out_request->request_code = avs_coap_msg_get_code(msg);
    if (parse_observe(msg, &out_request->observe)
            || parse_request_uri(msg, &out_request->is_bs_uri,
                                 &out_request->uri)
            || parse_attributes(msg, &out_request->attributes)
            || avs_coap_msg_get_content_format(msg,
                                               &out_request->content_format)
            || parse_action(msg, out_request)) {
        return -1;
    }
    return 0;
}

uint8_t _anjay_make_error_response_code(int handler_result) {
    uint8_t handler_code = (uint8_t) (-handler_result);
    int cls = avs_coap_msg_code_get_class(handler_code);
    if (cls == 4 || cls == 5) {
        return handler_code;
    } else {
        switch (handler_result) {
        case ANJAY_OUTCTXERR_FORMAT_MISMATCH:
            return AVS_COAP_CODE_NOT_ACCEPTABLE;
        default:
            anjay_log(ERROR, "invalid error code: %d", handler_result);
            return -ANJAY_ERR_INTERNAL;
        }
    }
}

static bool critical_option_validator(uint8_t msg_code, uint32_t optnum) {
    /* Note: BLOCK Options are handled inside stream.c */
    switch (msg_code) {
    case AVS_COAP_CODE_GET:
        return optnum == AVS_COAP_OPT_URI_PATH || optnum == AVS_COAP_OPT_ACCEPT;
    case AVS_COAP_CODE_PUT:
    case AVS_COAP_CODE_POST:
        return optnum == AVS_COAP_OPT_URI_PATH
               || optnum == AVS_COAP_OPT_URI_QUERY
               || optnum == AVS_COAP_OPT_ACCEPT;
    case AVS_COAP_CODE_DELETE:
        return optnum == AVS_COAP_OPT_URI_PATH;
    default:
        return false;
    }
}

static int block_request_equality_validator(const avs_coap_msg_t *msg,
                                            void *orig_request_) {
    const anjay_request_t *orig_request =
            (const anjay_request_t *) orig_request_;
    anjay_request_t block_request;
    if (avs_coap_msg_validate_critical_options(msg, critical_option_validator)
            || parse_request(msg, &block_request)
            || !_anjay_request_equal(&block_request, orig_request)) {
        return -1;
    }
    return 0;
}

static int handle_request(anjay_t *anjay,
                          const avs_coap_msg_identity_t *request_identity,
                          const anjay_request_t *request) {
    int result = -1;

    if (_anjay_dm_current_ssid(anjay) == ANJAY_SSID_BOOTSTRAP) {
        result = _anjay_bootstrap_perform_action(anjay, request);
    } else {
        result = _anjay_dm_perform_action(anjay, request_identity, request);
    }

    if (result) {
        uint8_t error_code = _anjay_make_error_response_code(result);

        if (avs_coap_msg_code_is_client_error(error_code)) {
            // the request was invalid; that's not really an error on our side,
            anjay_log(TRACE, "invalid request: %s",
                      AVS_COAP_CODE_STRING(request->request_code));
            result = 0;
        } else {
            anjay_log(ERROR, "could not handle request: %s",
                      AVS_COAP_CODE_STRING(request->request_code));
        }

        if (_anjay_coap_stream_set_error(anjay->comm_stream, error_code)) {
            anjay_log(ERROR, "could not setup error response");
            return -1;
        }
    }

    int finish_result = 0;
    if (request->msg_type == AVS_COAP_MSG_CONFIRMABLE) {
        finish_result = avs_stream_finish_message(anjay->comm_stream);
    }

    if (_anjay_dm_current_ssid(anjay) != ANJAY_SSID_BOOTSTRAP) {
        _anjay_observe_sched_flush_current_connection(anjay);
    }
    return result ? result : finish_result;
}

static int handle_incoming_message(anjay_t *anjay) {
    int result = -1;

    if (_anjay_dm_current_ssid(anjay) == ANJAY_SSID_BOOTSTRAP) {
        anjay_log(DEBUG, "bootstrap server");
    } else {
        anjay_log(DEBUG, "server ID = %u", _anjay_dm_current_ssid(anjay));
    }

    const avs_coap_msg_t *request_msg;
    if ((result = _anjay_coap_stream_get_incoming_msg(anjay->comm_stream,
                                                      &request_msg))) {
        if (result == AVS_COAP_CTX_ERR_DUPLICATE) {
            anjay_log(TRACE, "duplicate request received");
            return 0;
        } else if (result == AVS_COAP_CTX_ERR_MSG_WAS_PING) {
            anjay_log(TRACE, "received CoAP ping");
            return 0;
        } else {
            anjay_log(ERROR, "received packet is not a valid CoAP message");
            return result;
        }
    }

    avs_coap_msg_identity_t request_identity = AVS_COAP_MSG_IDENTITY_EMPTY;
    anjay_request_t request;
    if (_anjay_coap_stream_get_request_identity(anjay->comm_stream,
                                                &request_identity)
            || avs_coap_msg_validate_critical_options(request_msg,
                                                      critical_option_validator)
            || parse_request(request_msg, &request)) {
        if (avs_coap_msg_code_is_request(avs_coap_msg_get_code(request_msg))) {
            if (_anjay_coap_stream_set_error(anjay->comm_stream,
                                             -ANJAY_ERR_BAD_OPTION)
                    || avs_stream_finish_message(anjay->comm_stream)) {
                anjay_log(WARNING, "could not send Bad Option response");
            }
        }
        return 0;
    }

    _anjay_coap_stream_set_block_request_validator(
            anjay->comm_stream, block_request_equality_validator, &request);
    return handle_request(anjay, &request_identity, &request);
}

const avs_coap_tx_params_t *
_anjay_tx_params_for_conn_type(anjay_t *anjay,
                               anjay_connection_type_t conn_type) {
    switch (conn_type) {
    case ANJAY_CONNECTION_UDP:
        return &anjay->udp_tx_params;
    default:
        AVS_UNREACHABLE("Should never happen");
        return NULL;
    }
}

int _anjay_bind_server_stream(anjay_t *anjay, anjay_connection_ref_t ref) {
    avs_net_abstract_socket_t *socket =
            _anjay_connection_get_online_socket(ref);
    if (!socket) {
        anjay_log(ERROR, "server connection is not online");
        return -1;
    }
    if (avs_stream_net_setsock(anjay->comm_stream, socket)
            || _anjay_coap_stream_set_tx_params(
                       anjay->comm_stream,
                       _anjay_tx_params_for_conn_type(anjay, ref.conn_type))) {
        anjay_log(ERROR, "could not set stream socket");
        return -1;
    }

    assert(!anjay->current_connection.server);
    anjay->current_connection = ref;
    return 0;
}

void _anjay_release_server_stream(anjay_t *anjay) {
    _anjay_connection_schedule_queue_mode_close(anjay,
                                                anjay->current_connection);
    _anjay_release_server_stream_without_scheduling_queue(anjay);
}

static int udp_serve(anjay_t *anjay, avs_net_abstract_socket_t *ready_socket) {
    anjay_connection_ref_t connection = {
        .server = _anjay_servers_find_by_udp_socket(anjay, ready_socket),
        .conn_type = ANJAY_CONNECTION_UDP
    };
    if (!connection.server || _anjay_bind_server_stream(anjay, connection)) {
        return -1;
    }

    int result = handle_incoming_message(anjay);
    _anjay_release_server_stream(anjay);
    return result;
}


int anjay_serve(anjay_t *anjay, avs_net_abstract_socket_t *ready_socket) {
#ifdef WITH_DOWNLOADER
    if (!_anjay_downloader_handle_packet(&anjay->downloader, ready_socket)) {
        return 0;
    }
#endif // WITH_DOWNLOADER

    return udp_serve(anjay, ready_socket);
}

int anjay_sched_time_to_next(anjay_t *anjay, avs_time_duration_t *out_delay) {
    return _anjay_sched_time_to_next(anjay->sched, out_delay);
}

int anjay_sched_time_to_next_ms(anjay_t *anjay, int *out_delay_ms) {
    avs_time_duration_t delay;
    int result = anjay_sched_time_to_next(anjay, &delay);
    if (!result) {
        int64_t delay_ms;
        result = avs_time_duration_to_scalar(&delay_ms, AVS_TIME_MS, delay);
        if (!result) {
            assert(delay_ms >= 0); // guaranteed by _anjay_sched_time_to_next()
            *out_delay_ms = (int) AVS_MIN(delay_ms, INT_MAX);
        }
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

    return 0;
}

anjay_download_handle_t anjay_download(anjay_t *anjay,
                                       const anjay_download_config_t *config) {
#ifdef WITH_DOWNLOADER
    anjay_download_handle_t result = NULL;
    errno = -_anjay_downloader_download(&anjay->downloader, &result, config);
    return result;
#else  // WITH_DOWNLOADER
    (void) anjay;
    (void) config;
    anjay_log(ERROR, "CoAP download support disabled");
    errno = ENOTSUP;
    return NULL;
#endif // WITH_DOWNLOADER
}

void anjay_download_abort(anjay_t *anjay, anjay_download_handle_t handle) {
#ifdef WITH_DOWNLOADER
    _anjay_downloader_abort(&anjay->downloader, handle);
#else  // WITH_DOWNLOADER
    (void) anjay;
    (void) handle;
    anjay_log(ERROR, "CoAP download support disabled");
#endif // WITH_DOWNLOADER
}

void anjay_smsdrv_cleanup(anjay_smsdrv_t **smsdrv_ptr) {
    if (*smsdrv_ptr) {
        AVS_UNREACHABLE("SMS drivers not supported by this version of Anjay");
    }
}

uint64_t anjay_get_tx_bytes(anjay_t *anjay) {
#ifdef WITH_NET_STATS
    return avs_coap_ctx_get_tx_bytes(anjay->coap_ctx);
#else
    (void) anjay;
    return 0;
#endif
}

uint64_t anjay_get_rx_bytes(anjay_t *anjay) {
#ifdef WITH_NET_STATS
    return avs_coap_ctx_get_rx_bytes(anjay->coap_ctx);
#else
    (void) anjay;
    return 0;
#endif
}

uint64_t anjay_get_num_incoming_retransmissions(anjay_t *anjay) {
#ifdef WITH_NET_STATS
    return avs_coap_ctx_get_num_incoming_retransmissions(anjay->coap_ctx);
#else
    (void) anjay;
    return 0;
#endif
}

uint64_t anjay_get_num_outgoing_retransmissions(anjay_t *anjay) {
#ifdef WITH_NET_STATS
    return avs_coap_ctx_get_num_outgoing_retransmissions(anjay->coap_ctx);
#else
    (void) anjay;
    return 0;
#endif
}


#ifdef ANJAY_TEST
#    include "test/anjay.c"
#endif // ANJAY_TEST
