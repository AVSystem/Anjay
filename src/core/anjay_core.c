/*
 * Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
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

#include <anjay_init.h>

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <avsystem/commons/avs_errno.h>
#include <avsystem/commons/avs_memory.h>
#include <avsystem/commons/avs_stream_net.h>
#include <avsystem/commons/avs_stream_v_table.h>

#include <avsystem/coap/coap.h>

#include <anjay/core.h>
#include <anjay/stats.h>

#include <anjay_modules/anjay_time_defs.h>

#include <anjay_config_log.h>

#include "anjay_core.h"
#include "coap/anjay_content_format.h"
#include "coap/anjay_msg_details.h"

#include "anjay_bootstrap_core.h"
#include "anjay_dm_core.h"
#include "anjay_downloader.h"
#include "anjay_io_core.h"
#include "anjay_servers_utils.h"
#include "anjay_utils_private.h"

VISIBILITY_SOURCE_BEGIN

#ifndef ANJAY_VERSION
#    define ANJAY_VERSION "2.8.0"
#endif // ANJAY_VERSION

static int init(anjay_t *anjay, const anjay_configuration_t *config) {

#ifdef ANJAY_WITH_BOOTSTRAP
    bool legacy_server_initiated_bootstrap =
            !config->disable_legacy_server_initiated_bootstrap;
    _anjay_bootstrap_init(&anjay->bootstrap, legacy_server_initiated_bootstrap);
#endif // ANJAY_WITH_BOOTSTRAP

    anjay->dtls_version = config->dtls_version;
    if (anjay->dtls_version == AVS_NET_SSL_VERSION_DEFAULT) {
        anjay->dtls_version = AVS_NET_SSL_VERSION_TLSv1_2;
    }

    anjay->endpoint_name = config->endpoint_name;
    if (!anjay->endpoint_name) {
        anjay_log(ERROR, _("endpoint name must not be null"));
        return -1;
    }

    anjay->socket_config = config->socket_config;
    anjay->udp_listen_port = config->udp_listen_port;
    anjay->current_connection.conn_type = ANJAY_CONNECTION_UNSET;

    const char *error_msg;
#ifdef WITH_AVS_COAP_UDP
    if (config->udp_tx_params) {
        if (!avs_coap_udp_tx_params_valid(config->udp_tx_params, &error_msg)) {
            anjay_log(ERROR,
                      _("UDP CoAP transmission parameters are invalid: ") "%s",
                      error_msg);
            return -1;
        }
        anjay->udp_tx_params = *config->udp_tx_params;
    } else {
        anjay->udp_tx_params =
                (avs_coap_udp_tx_params_t) ANJAY_COAP_DEFAULT_UDP_TX_PARAMS;
    }
    if (config->msg_cache_size) {
        anjay->udp_response_cache =
                avs_coap_udp_response_cache_create(config->msg_cache_size);
        if (!anjay->udp_response_cache) {
            anjay_log(ERROR, _("out of memory"));
            return -1;
        }
    }
#endif // WITH_AVS_COAP_UDP

    if (config->udp_dtls_hs_tx_params) {
        if (!avs_time_duration_less(config->udp_dtls_hs_tx_params->min,
                                    config->udp_dtls_hs_tx_params->max)) {
            anjay_log(ERROR,
                      _("UDP DTLS Handshake transmission parameters are "
                        "invalid: min >= max"));
            return -1;
        }
        anjay->udp_dtls_hs_tx_params = *config->udp_dtls_hs_tx_params;
    } else {
        anjay->udp_dtls_hs_tx_params = (avs_net_dtls_handshake_timeouts_t)
                ANJAY_DTLS_DEFAULT_UDP_HS_TX_PARAMS;
    }

    if (_anjay_copy_tls_ciphersuites(&anjay->default_tls_ciphersuites,
                                     &config->default_tls_ciphersuites)) {
        return -1;
    }

    anjay->servers = _anjay_servers_create();
    if (!anjay->servers) {
        anjay_log(ERROR, _("out of memory"));
        return -1;
    }

    anjay->in_shared_buffer = avs_shared_buffer_new(config->in_buffer_size);
    if (!anjay->in_shared_buffer) {
        anjay_log(ERROR, _("out of memory"));
        return -1;
    }
    anjay->out_shared_buffer = avs_shared_buffer_new(config->out_buffer_size);
    if (!anjay->out_shared_buffer) {
        anjay_log(ERROR, _("out of memory"));
        return -1;
    }

    _anjay_observe_init(&anjay->observe,
                        config->confirmable_notifications,
                        config->stored_notification_limit);

    anjay->online_transports =
            _anjay_transport_set_remove_unavailable(anjay,
                                                    ANJAY_TRANSPORT_SET_ALL);

#ifdef ANJAY_WITH_DOWNLOADER
    if (_anjay_downloader_init(&anjay->downloader, anjay)) {
        return -1;
    }
#endif // ANJAY_WITH_DOWNLOADER

    anjay->prefer_hierarchical_formats = config->prefer_hierarchical_formats;
    anjay->use_connection_id = config->use_connection_id;

    if (config->prng_ctx) {
        anjay->prng_ctx.allocated_by_user = true;
        anjay->prng_ctx.ctx = config->prng_ctx;
    } else {
        anjay->prng_ctx.ctx = avs_crypto_prng_new(NULL, NULL);
        if (!anjay->prng_ctx.ctx) {
            anjay_log(ERROR, _("failed to create PRNG context"));
            return -1;
        }
    }
    return 0;
}

const char *anjay_get_version(void) {
    return ANJAY_VERSION;
}

anjay_t *anjay_new(const anjay_configuration_t *config) {
    anjay_log(INFO, _("Initializing Anjay ") ANJAY_VERSION);
    _anjay_log_feature_list();
    anjay_t *out = (anjay_t *) avs_calloc(1, sizeof(*out));
    if (!out) {
        anjay_log(ERROR, _("out of memory"));
        return NULL;
    }
    out->sched = avs_sched_new("Anjay", out);
    if (!out->sched) {
        anjay_log(ERROR, _("out of memory"));
        avs_free(out);
        return NULL;
    }

    if (init(out, config)) {
        anjay_delete(out);
        return NULL;
    }
    return out;
}

static void anjay_delete_impl(anjay_t *anjay, bool deregister) {
    anjay_log(TRACE, _("deleting anjay object"));

#ifdef ANJAY_WITH_DOWNLOADER
    _anjay_downloader_cleanup(&anjay->downloader);
#endif // ANJAY_WITH_DOWNLOADER

    if (deregister) {
        _anjay_servers_deregister(anjay);
    }

    // Make sure to deregister from all servers *before* cleaning up the
    // scheduler. That prevents us from updating a registration even though
    // we're about to deregister anyway.
    _anjay_servers_cleanup(anjay);

    _anjay_bootstrap_cleanup(anjay);

    // we want to clear this now so that notifications won't be sent during
    // avs_sched_cleanup()
    _anjay_observe_cleanup(&anjay->observe);

    _anjay_dm_cleanup(anjay);
    _anjay_notify_clear_queue(&anjay->scheduled_notify.queue);

    avs_free(anjay->default_tls_ciphersuites.ids);

#ifdef WITH_AVS_COAP_UDP
    avs_coap_udp_response_cache_release(&anjay->udp_response_cache);
#endif // WITH_AVS_COAP_UDP

    avs_sched_del(&anjay->reload_servers_sched_job_handle);
    avs_sched_del(&anjay->scheduled_notify.handle);
    avs_sched_cleanup(&anjay->sched);

    if (!anjay->prng_ctx.allocated_by_user) {
        avs_crypto_prng_free(&anjay->prng_ctx.ctx);
    }

    avs_free(anjay->in_shared_buffer);
    avs_free(anjay->out_shared_buffer);
    _anjay_security_config_cache_cleanup(&anjay->security_config_from_dm_cache);

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
        anjay_log(WARNING, _("Duplicated attribute in query string: ") "%s",
                  key_str);
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
        anjay_log(WARNING, _("Duplicated attribute in query string: ") "%s",
                  key_str);
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

#ifdef ANJAY_WITH_CON_ATTR
static int parse_con(const char *value,
                     bool *out_present,
                     anjay_dm_con_attr_t *out_value) {
    if (*out_present) {
        anjay_log(WARNING, _("Duplicated attribute in query string: con"));
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
        anjay_log(WARNING, _("Invalid con attribute value: ") "%s", value);
        return -1;
    }
}
#endif // ANJAY_WITH_CON_ATTR

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
    } else if (!strcmp(key, ANJAY_ATTR_EPMIN)) {
        return parse_nullable_period(
                key, value, &out_attrs->has_min_eval_period,
                &out_attrs->values.standard.common.min_eval_period);
    } else if (!strcmp(key, ANJAY_ATTR_EPMAX)) {
        return parse_nullable_period(
                key, value, &out_attrs->has_max_eval_period,
                &out_attrs->values.standard.common.max_eval_period);
    } else if (!strcmp(key, ANJAY_ATTR_GT)) {
        return parse_nullable_double(key, value, &out_attrs->has_greater_than,
                                     &out_attrs->values.standard.greater_than);
    } else if (!strcmp(key, ANJAY_ATTR_LT)) {
        return parse_nullable_double(key, value, &out_attrs->has_less_than,
                                     &out_attrs->values.standard.less_than);
    } else if (!strcmp(key, ANJAY_ATTR_ST)) {
        return parse_nullable_double(key, value, &out_attrs->has_step,
                                     &out_attrs->values.standard.step);
#ifdef ANJAY_WITH_CON_ATTR
    } else if (!strcmp(key, ANJAY_CUSTOM_ATTR_CON)) {
        return parse_con(value, &out_attrs->custom.has_con,
                         &out_attrs->values.custom.data.con);
#endif // ANJAY_WITH_CON_ATTR
    } else {
        anjay_log(DEBUG, _("unrecognized query string: ") "%s" _(" = ") "%s",
                  key, value ? value : "(null)");
        return -1;
    }
}

static int parse_attributes(const avs_coap_request_header_t *hdr,
                            anjay_request_attributes_t *out_attrs) {
    memset(out_attrs, 0, sizeof(*out_attrs));
    out_attrs->values = ANJAY_DM_INTERNAL_R_ATTRS_EMPTY;

    char buffer[ANJAY_MAX_URI_QUERY_SEGMENT_SIZE];
    size_t attr_size;

    int result;
    avs_coap_option_iterator_t it = AVS_COAP_OPTION_ITERATOR_EMPTY;
    while ((result = avs_coap_options_get_string_it(
                    &hdr->options, AVS_COAP_OPTION_URI_QUERY, &it, &attr_size,
                    buffer, sizeof(buffer) - 1))
           == 0) {
        const char *key;
        const char *value;

        buffer[attr_size] = '\0';
        split_query_string(buffer, &key, &value);
        assert(key != NULL);

        if (parse_attribute(out_attrs, key, value)) {
            anjay_log(DEBUG, _("invalid query string: ") "%s" _(" = ") "%s",
                      key, value ? value : "(null)");
            return -1;
        }
    }

    if (result < 0) {
        anjay_log(WARNING, _("could not read Request-Query"));
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
    case ANJAY_ACTION_BOOTSTRAP_FINISH:
        return "Bootstrap Finish";
    }
    AVS_UNREACHABLE("invalid enum value");
    return "<invalid action>";
}

static int code_to_action(uint8_t code,
                          uint16_t requested_format,
                          bool is_bs_uri,
                          const anjay_uri_path_t *path,
                          bool has_content_format,
                          anjay_request_action_t *out_action) {
    switch (code) {
    case AVS_COAP_CODE_GET:
        if (requested_format == AVS_COAP_FORMAT_LINK_FORMAT) {
            *out_action = ANJAY_ACTION_DISCOVER;
        } else {
            *out_action = ANJAY_ACTION_READ;
        }
        return 0;
    case AVS_COAP_CODE_POST:
        if (is_bs_uri) {
            *out_action = ANJAY_ACTION_BOOTSTRAP_FINISH;
        } else if (_anjay_uri_path_leaf_is(path, ANJAY_ID_IID)) {
            *out_action = ANJAY_ACTION_WRITE_UPDATE;
        } else if (_anjay_uri_path_leaf_is(path, ANJAY_ID_RID)) {
            *out_action = ANJAY_ACTION_EXECUTE;
        } else if (_anjay_uri_path_leaf_is(path, ANJAY_ID_RIID)) {
            *out_action = ANJAY_ACTION_WRITE;
        } else {
            // root or object path
            *out_action = ANJAY_ACTION_CREATE;
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
        anjay_log(DEBUG, _("unrecognized CoAP method: ") "%s",
                  AVS_COAP_CODE_STRING(code));
        return -1;
    }
}

static int parse_action(const avs_coap_request_header_t *hdr,
                        anjay_request_t *inout_request) {
    if (avs_coap_options_get_u16(&hdr->options, AVS_COAP_OPTION_ACCEPT,
                                 &inout_request->requested_format)) {
        inout_request->requested_format = AVS_COAP_FORMAT_NONE;
    }

    bool has_content_format =
            (inout_request->content_format != AVS_COAP_FORMAT_NONE);
    int result = code_to_action(inout_request->request_code,
                                inout_request->requested_format,
                                inout_request->is_bs_uri, &inout_request->uri,
                                has_content_format, &inout_request->action);
    if (!result) {
        anjay_log(DEBUG, _("LwM2M action: ") "%s",
                  action_to_string(inout_request->action));
    }
    return result;
}

static int parse_request_uri_segment(const char *uri, uint16_t *out_id) {
    long long num;
    if (_anjay_safe_strtoll(uri, &num) || num < 0 || num >= UINT16_MAX) {
        anjay_log(DEBUG, _("invalid Uri-Path segment: ") "%s", uri);
        return -1;
    }

    *out_id = (uint16_t) num;
    return 0;
}

static int parse_bs_uri(const avs_coap_request_header_t *hdr, bool *out_is_bs) {
    char uri[ANJAY_MAX_URI_SEGMENT_SIZE] = "";
    size_t uri_size;

    *out_is_bs = false;

    avs_coap_option_iterator_t it = AVS_COAP_OPTION_ITERATOR_EMPTY;
    int result =
            avs_coap_options_get_string_it(&hdr->options,
                                           AVS_COAP_OPTION_URI_PATH, &it,
                                           &uri_size, uri, sizeof(uri) - 1);

    if (result) {
        return (result == AVS_COAP_OPTION_MISSING) ? 0 : result;
    }

    if (strcmp(uri, "bs") == 0) {
        result =
                avs_coap_options_get_string_it(&hdr->options,
                                               AVS_COAP_OPTION_URI_PATH, &it,
                                               &uri_size, uri, sizeof(uri) - 1);
        if (result == AVS_COAP_OPTION_MISSING) {
            *out_is_bs = true;
            return 0;
        }
    }

    return result;
}

static int parse_dm_uri(const avs_coap_request_header_t *hdr,
                        anjay_uri_path_t *out_uri) {
    char uri[ANJAY_MAX_URI_SEGMENT_SIZE] = "";
    size_t uri_size;

    *out_uri = MAKE_ROOT_PATH();

    avs_coap_option_iterator_t it = AVS_COAP_OPTION_ITERATOR_EMPTY;

    size_t segment_index = 0;
    bool expect_no_more_options = false;
    int result = 0;

    while (!(result = avs_coap_options_get_string_it(
                     &hdr->options, AVS_COAP_OPTION_URI_PATH, &it, &uri_size,
                     uri, sizeof(uri) - 1))) {
        if (segment_index == 0 && uri[0] == '\0') {
            // Empty URI segment is only allowed as the first and only segment
            // as an alternative representation of an empty path.
            expect_no_more_options = true;
        } else if (expect_no_more_options || uri[0] == '\0') {
            anjay_log(WARNING, _("superfluous empty Uri-Path segment"));
            return -1;
        } else if (segment_index >= AVS_ARRAY_SIZE(out_uri->ids)) {
            // 4 or more segments...
            anjay_log(WARNING, _("prefixed Uri-Path are not supported"));
            return -1;
        } else if (parse_request_uri_segment(uri,
                                             &out_uri->ids[segment_index])) {
            return -1;
        }
        ++segment_index;
    }

    return result == AVS_COAP_OPTION_MISSING ? 0 : result;
}

static int parse_request_uri(const avs_coap_request_header_t *hdr,
                             bool *out_is_bs,
                             anjay_uri_path_t *out_uri) {
    int result = parse_bs_uri(hdr, out_is_bs);
    if (result) {
        return result;
    }
    if (*out_is_bs) {
        *out_uri = MAKE_ROOT_PATH();
        return 0;
    } else {
        return parse_dm_uri(hdr, out_uri);
    }
}

int _anjay_parse_request(const avs_coap_request_header_t *hdr,
                         anjay_request_t *out_request) {
    memset(out_request, 0, sizeof(*out_request));
    out_request->request_code = hdr->code;
    if (parse_request_uri(hdr, &out_request->is_bs_uri, &out_request->uri)
            || parse_attributes(hdr, &out_request->attributes)
            || avs_coap_options_get_content_format(&hdr->options,
                                                   &out_request->content_format)
            || parse_action(hdr, out_request)) {
        return -1;
    }
    return 0;
}

uint8_t _anjay_make_error_response_code(int handler_result) {
    uint8_t handler_code = (uint8_t) (-handler_result);
    int cls = avs_coap_code_get_class(handler_code);
    if (cls == 4 || cls == 5) {
        return handler_code;
    } else {
        switch (handler_result) {
        case ANJAY_OUTCTXERR_FORMAT_MISMATCH:
        case ANJAY_OUTCTXERR_METHOD_NOT_IMPLEMENTED:
            return AVS_COAP_CODE_NOT_ACCEPTABLE;
        default:
            return -ANJAY_ERR_INTERNAL;
        }
    }
}

static bool critical_option_validator(uint8_t msg_code, uint32_t optnum) {
    if (optnum == AVS_COAP_OPTION_ACCEPT) {
        return true;
    }
    /* Note: BLOCK Options are handled inside stream.c */
    switch (msg_code) {
    case AVS_COAP_CODE_GET:
        return optnum == AVS_COAP_OPTION_URI_PATH;
    case AVS_COAP_CODE_PUT:
    case AVS_COAP_CODE_POST:
        return optnum == AVS_COAP_OPTION_URI_PATH
               || optnum == AVS_COAP_OPTION_URI_QUERY;
    case AVS_COAP_CODE_DELETE:
        return optnum == AVS_COAP_OPTION_URI_PATH;
    default:
        return false;
    }
}

static int handle_request(anjay_t *anjay, const anjay_request_t *request) {
    int result = -1;

    if (_anjay_dm_current_ssid(anjay) == ANJAY_SSID_BOOTSTRAP) {
        result = _anjay_bootstrap_perform_action(anjay, request);
    } else {
        result = _anjay_dm_perform_action(anjay, request);
    }

    if (_anjay_dm_current_ssid(anjay) != ANJAY_SSID_BOOTSTRAP) {
        _anjay_observe_sched_flush(anjay->current_connection);
    }
    return result;
}

typedef struct {
    anjay_t *anjay;
    int serve_result;
} handle_incoming_message_args_t;

static int
handle_incoming_message(avs_coap_streaming_request_ctx_t *ctx,
                        const avs_coap_request_header_t *request_header,
                        avs_stream_t *payload_stream,
                        const avs_coap_observe_id_t *observe_id,
                        void *args_) {
    handle_incoming_message_args_t *args =
            (handle_incoming_message_args_t *) args_;

    if (_anjay_dm_current_ssid(args->anjay) == ANJAY_SSID_BOOTSTRAP) {
        anjay_log(DEBUG, _("bootstrap server"));
    } else {
        anjay_log(DEBUG, _("server ID = ") "%u",
                  _anjay_dm_current_ssid(args->anjay));
    }

    anjay_request_t request;
    if (avs_coap_options_validate_critical(request_header,
                                           critical_option_validator)
            || _anjay_parse_request(request_header, &request)) {
        return AVS_COAP_CODE_BAD_OPTION;
    }
    request.ctx = ctx;
    request.payload_stream = payload_stream;
    request.observe = observe_id;

    int result = handle_request(args->anjay, &request);
    if (result) {
        const uint8_t error_code = _anjay_make_error_response_code(result);
        if (error_code != -result) {
            anjay_log(WARNING, _("invalid error code: ") "%d", result);
        }

        if (avs_coap_code_is_client_error(error_code)) {
            // the request was invalid; that's not really an error on our side,
            anjay_log(TRACE, _("invalid request: ") "%s",
                      AVS_COAP_CODE_STRING(request_header->code));
            args->serve_result = 0;
        } else {
            anjay_log(DEBUG, _("could not handle request: ") "%s",
                      AVS_COAP_CODE_STRING(request_header->code));
            args->serve_result = result;
        }
        return error_code;
    }
    return 0;
}

avs_time_duration_t
_anjay_max_transmit_wait_for_transport(anjay_t *anjay,
                                       anjay_socket_transport_t transport) {
    switch (transport) {
#ifdef WITH_AVS_COAP_UDP
    case ANJAY_SOCKET_TRANSPORT_UDP:
        return avs_coap_udp_max_transmit_wait(&anjay->udp_tx_params);
#endif // WITH_AVS_COAP_UDP
    default:
        AVS_UNREACHABLE("Should never happen");
        return AVS_TIME_DURATION_INVALID;
    }
}

avs_time_duration_t
_anjay_exchange_lifetime_for_transport(anjay_t *anjay,
                                       anjay_socket_transport_t transport) {
    switch (transport) {
#ifdef WITH_AVS_COAP_UDP
    case ANJAY_SOCKET_TRANSPORT_UDP:
        return avs_coap_udp_exchange_lifetime(&anjay->udp_tx_params);
#endif // WITH_AVS_COAP_UDP
    default:
        AVS_UNREACHABLE("Should never happen");
        return AVS_TIME_DURATION_INVALID;
    }
}

int _anjay_bind_connection(anjay_t *anjay, anjay_connection_ref_t ref) {
    avs_net_socket_t *socket = _anjay_connection_get_online_socket(ref);
    if (!socket) {
        anjay_log(ERROR, _("server connection is not online"));
        return -1;
    }
    assert(!anjay->current_connection.server);
    anjay->current_connection = ref;
    return 0;
}

void _anjay_release_connection(anjay_t *anjay) {
    _anjay_connection_schedule_queue_mode_close(anjay->current_connection);
    anjay->current_connection.server = NULL;
    anjay->current_connection.conn_type = ANJAY_CONNECTION_UNSET;
}

static int serve_connection(anjay_t *anjay, anjay_connection_ref_t connection) {
    if (_anjay_bind_connection(anjay, connection)) {
        return -1;
    }

    avs_coap_ctx_t *coap = _anjay_connection_get_coap(connection);
    assert(coap);

    handle_incoming_message_args_t args = {
        .anjay = anjay,
        .serve_result = 0
    };
    avs_error_t err = avs_coap_streaming_handle_incoming_packet(
            coap, handle_incoming_message, &args);
    _anjay_release_connection(anjay);

    avs_coap_error_recovery_action_t recovery_action =
            avs_coap_error_recovery_action(err);
    if (recovery_action == AVS_COAP_ERR_RECOVERY_RECREATE_CONTEXT) {
        _anjay_server_on_fatal_coap_error(connection);
    } else if (err.category == AVS_ERRNO_CATEGORY && err.code == AVS_ENODEV) {
        anjay_log(WARNING,
                  _("ENODEV returned from the networking layer, ignoring"));
    } else if (recovery_action == AVS_COAP_ERR_RECOVERY_UNKNOWN
               && (err.category != AVS_COAP_ERR_CATEGORY
                   || avs_coap_error_class(err) != AVS_COAP_ERR_CLASS_OTHER)) {
        _anjay_server_on_server_communication_error(connection.server, err);
    }

    return avs_is_ok(err) ? args.serve_result : -1;
}

int anjay_serve(anjay_t *anjay, avs_net_socket_t *ready_socket) {
    _anjay_security_config_cache_cleanup(&anjay->security_config_from_dm_cache);

#ifdef ANJAY_WITH_DOWNLOADER
    if (!_anjay_downloader_handle_packet(&anjay->downloader, ready_socket)) {
        return 0;
    }
#endif // ANJAY_WITH_DOWNLOADER

    anjay_connection_ref_t connection = {
        .server = _anjay_servers_find_by_primary_socket(anjay, ready_socket),
        .conn_type = ANJAY_CONNECTION_PRIMARY
    };
    if (!connection.server) {
        return -1;
    }
    return serve_connection(anjay, connection);
}

avs_sched_t *anjay_get_scheduler(anjay_t *anjay) {
    return anjay->sched;
}

int anjay_sched_time_to_next(anjay_t *anjay, avs_time_duration_t *out_delay) {
    *out_delay = avs_sched_time_to_next(anjay->sched);
    return avs_time_duration_valid(*out_delay) ? 0 : -1;
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

void anjay_sched_run(anjay_t *anjay) {
    avs_sched_run(anjay->sched);
}

anjay_etag_t *anjay_etag_new(uint8_t etag_size) {
    anjay_etag_t *result = (anjay_etag_t *) avs_calloc(
            offsetof(anjay_etag_t, value) + etag_size, sizeof(char));

    if (result) {
        result->size = (uint8_t) etag_size;
    }

    return result;
}

anjay_etag_t *anjay_etag_clone(const anjay_etag_t *old_etag) {
    if (old_etag == NULL) {
        return NULL;
    }

    anjay_etag_t *result = anjay_etag_new(old_etag->size);

    if (result) {
        memcpy(result->value, old_etag->value, result->size);
    }

    return result;
}

avs_error_t anjay_download(anjay_t *anjay,
                           const anjay_download_config_t *config,
                           anjay_download_handle_t *out_handle) {
#ifdef ANJAY_WITH_DOWNLOADER
    return _anjay_downloader_download(&anjay->downloader, out_handle, config);
#else  // ANJAY_WITH_DOWNLOADER
    (void) anjay;
    (void) config;
    (void) out_handle;
    anjay_log(ERROR, _("CoAP download support disabled"));
    return avs_errno(AVS_ENOTSUP);
#endif // ANJAY_WITH_DOWNLOADER
}

avs_error_t
anjay_download_set_next_block_offset(anjay_t *anjay,
                                     anjay_download_handle_t dl_handle,
                                     size_t next_block_offset) {
#ifdef ANJAY_WITH_DOWNLOADER
    return _anjay_downloader_set_next_block_offset(
            &anjay->downloader, dl_handle, next_block_offset);
#else  // ANJAY_WITH_DOWNLOADER
    (void) anjay;
    (void) dl_handle;
    (void) next_block_offset;
    anjay_log(ERROR, _("CoAP download support disabled"));
    return avs_errno(AVS_ENOTSUP);
#endif // ANJAY_WITH_DOWNLOADER
}

void anjay_download_abort(anjay_t *anjay, anjay_download_handle_t handle) {
#ifdef ANJAY_WITH_DOWNLOADER
    _anjay_downloader_abort(&anjay->downloader, handle);
#else  // ANJAY_WITH_DOWNLOADER
    (void) anjay;
    (void) handle;
    anjay_log(ERROR, _("CoAP download support disabled"));
#endif // ANJAY_WITH_DOWNLOADER
}

#ifdef ANJAY_TEST
#    include "tests/core/anjay.c"
#endif // ANJAY_TEST
