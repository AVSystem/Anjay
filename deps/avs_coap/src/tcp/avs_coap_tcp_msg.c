/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <avs_coap_init.h>

#ifdef WITH_AVS_COAP_TCP

#    include <avsystem/commons/avs_buffer.h>
#    include <avsystem/commons/avs_errno.h>

#    include "avs_coap_code_utils.h"

#    define MODULE_NAME coap_tcp
#    include <avs_coap_x_log_config.h>

#    include "options/avs_coap_options.h"
#    include "tcp/avs_coap_tcp_msg.h"

VISIBILITY_SOURCE_BEGIN

avs_error_t _avs_coap_tcp_serialize_msg(const avs_coap_borrowed_msg_t *msg,
                                        void *buf,
                                        size_t buf_size,
                                        size_t *out_msg_size) {
    assert(buf_size >= _AVS_COAP_TCP_MAX_HEADER_LENGTH);
    assert(buf);
    assert(out_msg_size);

    avs_coap_tcp_header_t header =
            _avs_coap_tcp_header_init(msg->payload_size, msg->options.size,
                                      msg->token.size, msg->code);
    size_t header_size =
            _avs_coap_tcp_header_serialize(&header, (uint8_t *) buf, buf_size);

    bytes_appender_t appender = {
        .write_ptr = (uint8_t *) buf + header_size,
        .bytes_left = buf_size - header_size
    };

    int retval;
    (void) ((retval = _avs_coap_bytes_append(&appender, msg->token.bytes,
                                             msg->token.size))
            || (retval = _avs_coap_bytes_append(&appender, msg->options.begin,
                                                msg->options.size)));
    if (!retval && msg->payload_size) {
        (void) ((retval = _avs_coap_bytes_append(
                         &appender, &AVS_COAP_PAYLOAD_MARKER,
                         sizeof(AVS_COAP_PAYLOAD_MARKER)))
                || (retval = _avs_coap_bytes_append(&appender, msg->payload,
                                                    msg->payload_size)));
    }

    if (retval) {
        LOG(ERROR, _("message too big to fit into output buffer"));
        return _avs_coap_err(AVS_COAP_ERR_MESSAGE_TOO_BIG);
    }
    *out_msg_size = buf_size - appender.bytes_left;
    return AVS_OK;
}

avs_error_t _avs_coap_tcp_pack_options(avs_coap_tcp_cached_msg_t *inout_msg,
                                       const avs_buffer_t *data) {
    assert(inout_msg);
    assert(data);

    if (!inout_msg->remaining_bytes) {
        return AVS_OK;
    }

    const uint8_t *data_ptr = (const uint8_t *) avs_buffer_data(data);
    const size_t data_size = avs_buffer_data_size(data);
    AVS_ASSERT(data_size <= inout_msg->remaining_bytes,
               "bug: more than one message in buffer");
    bytes_dispenser_t dispenser = {
        .read_ptr = data_ptr,
        .bytes_left = data_size
    };

    bool payload_marker_reached;
    bool truncated;
    avs_error_t err =
            _avs_coap_options_parse(&inout_msg->content.options, &dispenser,
                                    &truncated, &payload_marker_reached);
    if (avs_is_err(err)) {
        // There must be check if we didn't receive a complete message,
        // because single option may be truncated, but there'll be no more data.
        return (truncated && data_size != inout_msg->remaining_bytes)
                       ? _avs_coap_err(AVS_COAP_ERR_MORE_DATA_REQUIRED)
                       : err;
    }

    if (!payload_marker_reached
            && inout_msg->content.options.size < inout_msg->remaining_bytes) {
        // Payload exists after options and marker isn't parsed yet.
        return _avs_coap_err(AVS_COAP_ERR_MORE_DATA_REQUIRED);
    }

    size_t bytes_parsed = data_size - dispenser.bytes_left;
    if (payload_marker_reached) {
        assert(*dispenser.read_ptr == AVS_COAP_PAYLOAD_MARKER);
        _avs_coap_bytes_extract(&dispenser, NULL,
                                sizeof(AVS_COAP_PAYLOAD_MARKER));
        bytes_parsed += sizeof(AVS_COAP_PAYLOAD_MARKER);
        if (inout_msg->remaining_bytes - bytes_parsed == 0) {
            // not MALFORMED_MESSAGE, because the header is still valid
            LOG(DEBUG, _("invalid message - no payload after payload marker"));
            return _avs_coap_err(AVS_COAP_ERR_MALFORMED_OPTIONS);
        }
    }

    size_t total_payload_length = inout_msg->remaining_bytes - bytes_parsed;

#    ifdef WITH_AVS_COAP_BLOCK
    if (!_avs_coap_options_block_payload_valid(&inout_msg->content.options,
                                               inout_msg->content.code,
                                               total_payload_length)) {
        return _avs_coap_err(AVS_COAP_ERR_MALFORMED_OPTIONS);
    }
#    endif // WITH_AVS_COAP_BLOCK

    inout_msg->remaining_bytes -= bytes_parsed;
    inout_msg->content.total_payload_size = total_payload_length;
    return AVS_OK;
}

void _avs_coap_tcp_pack_payload(avs_coap_tcp_cached_msg_t *inout_msg,
                                const uint8_t *data,
                                size_t data_size) {
    assert(data_size <= inout_msg->remaining_bytes);
    assert(!data_size || data);

    inout_msg->content.payload_offset =
            inout_msg->content.total_payload_size - inout_msg->remaining_bytes;

    inout_msg->remaining_bytes -= data_size;
    inout_msg->content.payload = data;
    inout_msg->content.payload_size = data_size;
}

#endif // WITH_AVS_COAP_TCP
