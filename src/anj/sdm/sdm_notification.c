/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <string.h>

#include <fluf/fluf_utils.h>

#include <anj/anj_time.h>
#include <anj/sdm_notification.h>

#include "sdm_core.h"

#define NOTIFICATION_RECORDS_COUNT 10

typedef struct {
    fluf_uri_path_t resource_path;

    fluf_attr_notification_t attributes_observe;
    bool attributes_set_by_observe;
    // attributes write by Write-Attributes operation
    fluf_attr_notification_t attributes_write;

    fluf_io_out_entry_t record;
    bool send_notification;

    uint64_t observe_number;
    fluf_coap_token_t token;
} notification_records_t;

static notification_records_t notification_records[NOTIFICATION_RECORDS_COUNT];

static notification_records_t *find_notification_or_maybe_return_empty_rec(
        const fluf_uri_path_t *resource_path, bool return_empty) {
    // try to find record with the same path
    for (size_t i = 0; i < NOTIFICATION_RECORDS_COUNT; i++) {
        if (fluf_uri_path_equal(resource_path,
                                &notification_records[i].resource_path)) {
            return (notification_records_t *) &notification_records[i];
        }
    }
    // find first empty record
    if (return_empty) {
        for (size_t i = 0; i < NOTIFICATION_RECORDS_COUNT; i++) {
            if (fluf_uri_path_length(&notification_records[i].resource_path)
                    == 0) {
                return (notification_records_t *) &notification_records[i];
            }
        }
    }
    return NULL;
}

static bool is_there_any_attribute(fluf_attr_notification_t *attr) {
    return (attr->has_min_period || attr->has_max_period
            || attr->has_greater_than || attr->has_less_than || attr->has_step
            || attr->has_min_eval_period || attr->has_max_eval_period
#ifdef FLUF_WITH_LWM2M12
            || attr->has_edge || attr->has_con || attr->has_hqmax
#endif // FLUF_WITH_LWM2M12
    );
}

static int validate_attributes(fluf_data_t *in_out_msg) {
    // for now we only support min_period and max_period
    if (in_out_msg->attr.notification_attr.has_greater_than
            || in_out_msg->attr.notification_attr.has_less_than
            || in_out_msg->attr.notification_attr.has_step
            || in_out_msg->attr.notification_attr.has_min_eval_period
            || in_out_msg->attr.notification_attr.has_max_eval_period
#ifdef FLUF_WITH_LWM2M12
            || in_out_msg->attr.notification_attr.has_edge
            || in_out_msg->attr.notification_attr.has_con
            || in_out_msg->attr.notification_attr.has_hqmax
#endif // FLUF_WITH_LWM2M12
    ) {
        in_out_msg->msg_code = FLUF_COAP_CODE_METHOD_NOT_ALLOWED;
        return SDM_ERR_CURRENTLY_UNSUPPORTED;
    }
    return 0;
}

static int handle_observe_operation(fluf_data_t *in_out_msg,
                                    sdm_data_model_t *dm,
                                    notification_records_t *notification_record,
                                    char *out_buff,
                                    size_t out_buff_len) {
    int ret_val = 0;
    fluf_io_out_ctx_t ctx;
    fluf_io_out_entry_t record;
    size_t buffer_usage;

    if ((ret_val = validate_attributes(in_out_msg))) {
        return ret_val;
    }

    if ((ret_val = _sdm_get_resource_value(
                 dm, &in_out_msg->uri, &record.value,
                 &record.type))) { // todo maybe change to sdm_operation_begin
        in_out_msg->msg_code = FLUF_COAP_CODE_METHOD_NOT_ALLOWED;
        return ret_val;
    }

    if ((ret_val = fluf_io_out_ctx_init(&ctx, FLUF_OP_INF_OBSERVE,
                                        &in_out_msg->uri, 1,
                                        in_out_msg->content_format))) {
        sdm_log(ERROR, "fluf_io ctx initialization failed");
        return ret_val;
    }

    record.path = in_out_msg->uri;
    record.timestamp = (double) anj_time_now() / 1000;

    if ((ret_val = fluf_io_out_ctx_new_entry(&ctx, &record))
            || (ret_val = fluf_io_out_ctx_get_payload(
                        &ctx, out_buff, out_buff_len, &buffer_usage))) {
        return ret_val;
    }

    notification_record->attributes_observe =
            in_out_msg->attr.notification_attr;
    notification_record->attributes_set_by_observe =
            is_there_any_attribute(&in_out_msg->attr.notification_attr);
    notification_record->resource_path = in_out_msg->uri;
    notification_record->record = record;
    notification_record->token = in_out_msg->coap.coap_udp.token;
    notification_record->observe_number =
            1; // todo if we don't use FLUF_OP_INF_NON_CON_NOTIFY as a response
               // to FLUF_OP_INF_OBSERVE then should be set to 0

    in_out_msg->payload = (void *) out_buff;
    in_out_msg->payload_size = buffer_usage;
    in_out_msg->msg_code = FLUF_COAP_CODE_CONTENT;
    in_out_msg->content_format = fluf_io_out_ctx_get_format(&ctx);
    return ret_val;
}

static int
handle_cancel_observe_operation(fluf_data_t *in_out_msg,
                                notification_records_t *notification_record) {
    notification_record->resource_path.uri_len = 0;
    memset(&notification_record->attributes_observe, 0,
           sizeof(notification_record->attributes_observe));
    notification_record->attributes_set_by_observe = false;
    notification_record->send_notification = false;

    in_out_msg->msg_code = FLUF_COAP_CODE_CONTENT;
    return 0;
}

static int
handle_write_attribute_operation(fluf_data_t *in_out_msg,
                                 notification_records_t *notification_record) {
    int ret_val = 0;

    if ((ret_val = validate_attributes(in_out_msg))) {
        return ret_val;
    }

    notification_record->attributes_write = in_out_msg->attr.notification_attr;

    in_out_msg->msg_code = FLUF_COAP_CODE_CHANGED;
    return ret_val;
}

static bool res_values_equal(fluf_res_value_t *left,
                             fluf_res_value_t *right,
                             fluf_data_type_t type) {
    switch (type) {
    case FLUF_DATA_TYPE_NULL:
        return true;
    case FLUF_DATA_TYPE_BYTES:
    case FLUF_DATA_TYPE_STRING:
        return !memcmp(left->bytes_or_string.data, right->bytes_or_string.data,
                       left->bytes_or_string.full_length_hint);
    case FLUF_DATA_TYPE_INT:
        return left->int_value == right->int_value;
    case FLUF_DATA_TYPE_DOUBLE:
        return left->double_value == right->double_value;
    case FLUF_DATA_TYPE_BOOL:
        return left->bool_value == right->bool_value;
    case FLUF_DATA_TYPE_OBJLNK:
        return left->objlnk.oid == right->objlnk.oid
               && left->objlnk.iid == right->objlnk.iid;
    case FLUF_DATA_TYPE_UINT:
        return left->uint_value == right->uint_value;
    case FLUF_DATA_TYPE_TIME:
        return left->time_value == right->time_value;
    case FLUF_DATA_TYPE_FLAG_EXTERNAL:
        // currently not supported
        return false;
    default:
        assert(false);
    }
    return false;
}

int sdm_notification(fluf_data_t *in_out_msg,
                     sdm_data_model_t *dm,
                     char *out_buff,
                     size_t out_buff_len) {
    assert(dm && in_out_msg);
    assert(in_out_msg->operation == FLUF_OP_DM_WRITE_ATTR
           || in_out_msg->operation == FLUF_OP_INF_OBSERVE
           || in_out_msg->operation == FLUF_OP_INF_CANCEL_OBSERVE);

    notification_records_t *notification_record;
    int ret_val = 0;
    _sdm_entity_ptrs_t ptrs;
    fluf_op_t operation = in_out_msg->operation;

    in_out_msg->msg_code = FLUF_COAP_CODE_INTERNAL_SERVER_ERROR;

    if (in_out_msg->operation != FLUF_OP_INF_OBSERVE) {
        in_out_msg->operation = FLUF_OP_RESPONSE;
    } else {
        in_out_msg->operation = FLUF_OP_INF_NON_CON_NOTIFY; // todo! ugh, hacky
    }

    if ((ret_val = _sdm_get_entity_ptrs(dm, &in_out_msg->uri, &ptrs))) {
        return ret_val;
    }

    // for now we only support notification for resources and we don't support
    // external data type
    if (!fluf_uri_path_is(&in_out_msg->uri, FLUF_ID_RID)
            || _sdm_is_multi_instance_resource(ptrs.res->res_spec->operation)
            || ptrs.res->res_spec->type == FLUF_DATA_TYPE_FLAG_EXTERNAL) {
        return SDM_ERR_CURRENTLY_UNSUPPORTED;
    }

    if (in_out_msg->uri.ids[FLUF_ID_OID] == 0
            || in_out_msg->uri.ids[FLUF_ID_OID] == 21) {
        in_out_msg->msg_code = FLUF_COAP_CODE_UNAUTHORIZED;
        return SDM_ERR_LOGIC;
    }

    if (in_out_msg->operation == FLUF_OP_INF_CANCEL_OBSERVE) {
        if (!(notification_record = find_notification_or_maybe_return_empty_rec(
                      &in_out_msg->uri, false))) {
            sdm_log(INFO, "Can't find observation related to given path");
            in_out_msg->msg_code = FLUF_COAP_CODE_NOT_FOUND;
            return 0;
        }
    } else {
        if (!(notification_record = find_notification_or_maybe_return_empty_rec(
                      &in_out_msg->uri, true))) {
            sdm_log(ERROR, "No space for new observation");
            return SDM_ERR_MEMORY;
        }
    }

    switch (operation) {
    case FLUF_OP_INF_OBSERVE:
        ret_val = handle_observe_operation(in_out_msg, dm, notification_record,
                                           out_buff, out_buff_len);
        break;
    case FLUF_OP_INF_CANCEL_OBSERVE:
        ret_val = handle_cancel_observe_operation(in_out_msg,
                                                  notification_record);
        break;
    case FLUF_OP_DM_WRITE_ATTR:
        ret_val = handle_write_attribute_operation(in_out_msg,
                                                   notification_record);
        break;
    default:
        ret_val = SDM_ERR_INPUT_ARG;
    }

    return ret_val;
}

static int check_if_notification_should_be_send(sdm_data_model_t *dm) {
    int ret_val = 0;

    for (size_t i = 0; i < NOTIFICATION_RECORDS_COUNT; i++) {
        if (fluf_uri_path_length(&notification_records[i].resource_path) != 0) {
            int ret;
            fluf_attr_notification_t *attributes =
                    notification_records[i].attributes_set_by_observe
                            ? &notification_records[i].attributes_observe
                            : &notification_records[i].attributes_write;
            double elapsed_time = (double) anj_time_now() / 1000
                                  - notification_records[i].record.timestamp;
            fluf_res_value_t value;
            fluf_data_type_t type;

            if (attributes->has_min_period
                    && attributes->min_period > elapsed_time) {
                continue;
            }

            if (attributes->has_max_period && attributes->max_period != 0
                    && attributes->max_period
                                   >= (attributes->has_min_period
                                               ? attributes->min_period
                                               : 0)
                    && elapsed_time >= attributes->max_period) {
                notification_records[i].send_notification = true;
                continue;
            }

            if ((ret = _sdm_get_resource_value( // todo maybe change to
                                                // sdm_operation_begin
                         dm, &notification_records[i].resource_path, &value,
                         &type))) {
                ret_val = ret;
                continue;
            }

            assert(type == notification_records[i].record.type);
            if (!res_values_equal(&notification_records[i].record.value, &value,
                                  type)) {
                notification_records[i].send_notification = true;
            }
        }
    }

    return ret_val;
}

static int prepare_notify_message(sdm_data_model_t *dm,
                                  notification_records_t *notification_record,
                                  fluf_data_t *out_msg,
                                  char *out_buff,
                                  size_t out_buff_len,
                                  uint16_t format) {
    fluf_io_out_ctx_t out_ctx;
    fluf_io_out_entry_t record;
    size_t buffer_usage;
    int ret_val = 0;

    if ((ret_val = _sdm_get_resource_value( // todo maybe change to
                                            // sdm_operation_begin
                 dm, &notification_record->resource_path, &record.value,
                 &record.type))) {
        return ret_val;
    }

    if ((ret_val = fluf_io_out_ctx_init(&out_ctx, FLUF_OP_INF_NON_CON_NOTIFY,
                                        &notification_record->resource_path, 1,
                                        format))) {
        sdm_log(ERROR, "fluf_io ctx initialization failed");
        return ret_val;
    }

    record.timestamp = (double) anj_time_now() / 1000;
    record.path = notification_record->resource_path;

    if ((ret_val = fluf_io_out_ctx_new_entry(&out_ctx, &record))
            || (ret_val = fluf_io_out_ctx_get_payload(
                        &out_ctx, out_buff, out_buff_len, &buffer_usage))) {
        return ret_val;
    }

    notification_record->record = record;
    notification_record->send_notification = false;

    out_msg->operation = FLUF_OP_INF_NON_CON_NOTIFY;
    out_msg->content_format = format;
    out_msg->observe_number = notification_record->observe_number++;
    out_msg->payload = (void *) out_buff;
    out_msg->payload_size = buffer_usage;
    out_msg->coap.coap_udp.token = notification_record->token;

    return ret_val;
}

int sdm_notification_process(fluf_data_t *out_msg,
                             sdm_data_model_t *dm,
                             char *out_buff,
                             size_t out_buff_len,
                             uint16_t format) {
    assert(dm && out_msg && out_buff);
    int ret_val = 0;

    if ((ret_val = check_if_notification_should_be_send(dm))) {
        sdm_log(WARN, "Failed to check all observations");
    }

    for (size_t i = 0; i < NOTIFICATION_RECORDS_COUNT; i++) {
        if (notification_records[i].send_notification) {
            if ((ret_val = prepare_notify_message(dm, &notification_records[i],
                                                  out_msg, out_buff,
                                                  out_buff_len, format))) {
                sdm_log(WARN, "Preparing notify message has failed");
            } else {
                // just one notify message per sdm_notification_process call
                break;
            }
        }
    }

    return ret_val;
}
