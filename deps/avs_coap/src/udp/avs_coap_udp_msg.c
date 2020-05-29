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

#include <avs_coap_init.h>

#ifdef WITH_AVS_COAP_UDP

#    include <inttypes.h>

#    include <avsystem/coap/ctx.h>

#    include "udp/avs_coap_udp_msg.h"

#    include "avs_coap_code_utils.h"
#    include "options/avs_coap_iterator.h"

#    define MODULE_NAME coap_udp
#    include <avs_coap_x_log_config.h>

#    include "avs_coap_common_utils.h"
#    include "options/avs_coap_options.h"

VISIBILITY_SOURCE_BEGIN

static bool is_msg_header_valid(const avs_coap_udp_header_t *hdr) {
    uint8_t version = _avs_coap_udp_header_get_version(hdr);
    if (version != 1) {
        LOG(DEBUG, _("unsupported CoAP version: ") "%u", version);
        return false;
    }

    if (_avs_coap_udp_header_get_token_length(hdr)
            > AVS_COAP_MAX_TOKEN_LENGTH) {
        LOG(DEBUG, _("invalid token longer than ") "%u" _(" bytes"),
            (unsigned) AVS_COAP_MAX_TOKEN_LENGTH);
        return false;
    }

    const avs_coap_udp_type_t type = _avs_coap_udp_header_get_type(hdr);

    switch (type) {
    case AVS_COAP_UDP_TYPE_ACKNOWLEDGEMENT:
        if (avs_coap_code_is_request(hdr->code)) {
            LOG(DEBUG,
                _("Request code (") "%s" _(
                        ") on an Acknowledgement makes no sense"),
                AVS_COAP_CODE_STRING(hdr->code));
            return false;
        }
        break;

    case AVS_COAP_UDP_TYPE_RESET:
        if (hdr->code != AVS_COAP_CODE_EMPTY) {
            LOG(DEBUG,
                _("Reset message must use ") "%s" _(" CoAP code (got ") "%s" _(
                        ")"),
                AVS_COAP_CODE_STRING(AVS_COAP_CODE_EMPTY),
                AVS_COAP_CODE_STRING(hdr->code));
            return false;
        }
        break;

    default:
        break;
    }

    return true;
}

static avs_error_t parse_header(avs_coap_udp_header_t *out_hdr,
                                bytes_dispenser_t *dispenser) {
    if (_avs_coap_bytes_extract(dispenser, out_hdr, sizeof(*out_hdr))
            || !is_msg_header_valid(out_hdr)) {
        LOG(DEBUG, _("malformed CoAP/UDP header"));
        return _avs_coap_err(AVS_COAP_ERR_MALFORMED_MESSAGE);
    }

    if (out_hdr->code == AVS_COAP_CODE_EMPTY && dispenser->bytes_left > 0) {
        LOG(DEBUG, "%s" _(" message must not have token, options nor payload"),
            AVS_COAP_CODE_STRING(AVS_COAP_CODE_EMPTY));
        return _avs_coap_err(AVS_COAP_ERR_MALFORMED_MESSAGE);
    }

    return AVS_OK;
}

static avs_error_t parse_payload(const void **out_payload,
                                 size_t *out_payload_size,
                                 bytes_dispenser_t *dispenser) {
    *out_payload = dispenser->read_ptr;
    *out_payload_size = dispenser->bytes_left;

    if (*out_payload_size == 0) {
        // no payload after options
        return AVS_OK;
    }

    // ensured by parse_options
    assert(*dispenser->read_ptr == AVS_COAP_PAYLOAD_MARKER);

    *out_payload = dispenser->read_ptr + 1;
    *out_payload_size -= 1;

    if (*out_payload_size == 0) {
        // not MALFORMED_MESSAGE, because the header is still valid
        LOG(DEBUG, _("payload marker must be omitted if there is no payload"));
        return _avs_coap_err(AVS_COAP_ERR_MALFORMED_OPTIONS);
    }

    return AVS_OK;
}

static inline avs_error_t parse_token(avs_coap_udp_msg_t *out_msg,
                                      bytes_dispenser_t *dispenser) {
    return _avs_coap_parse_token(
            &out_msg->token,
            _avs_coap_udp_header_get_token_length(&out_msg->header), dispenser);
}

#    ifdef WITH_AVS_COAP_BLOCK
static inline avs_error_t
validate_block_opt(avs_coap_options_t *opts,
                   avs_coap_option_block_type_t block_type) {
    avs_coap_option_block_t block;
    if (avs_coap_options_get_block(opts, block_type, &block) == 0
            && block.is_bert) {
        LOG(DEBUG, _("BERT option in CoAP/UDP message"));
        return _avs_coap_err(AVS_COAP_ERR_MALFORMED_OPTIONS);
    }
    return AVS_OK;
}
#    endif // WITH_AVS_COAP_BLOCK

avs_error_t _avs_coap_udp_msg_parse(avs_coap_udp_msg_t *out_msg,
                                    const uint8_t *packet,
                                    size_t packet_size) {
    bytes_dispenser_t dispenser = {
        .read_ptr = packet,
        .bytes_left = packet_size
    };

    avs_error_t err;
    (void) (avs_is_err((err = parse_header(&out_msg->header, &dispenser)))
            || avs_is_err((err = parse_token(out_msg, &dispenser)))
            || avs_is_err((err = _avs_coap_options_parse(
                                   &out_msg->options, &dispenser, NULL, NULL)))
            || avs_is_err((err = parse_payload(&out_msg->payload,
                                               &out_msg->payload_size,
                                               &dispenser))));

#    ifdef WITH_AVS_COAP_BLOCK
    if (avs_is_ok(err)) {
        (void) (avs_is_err((err = validate_block_opt(&out_msg->options,
                                                     AVS_COAP_BLOCK1)))
                || avs_is_err((err = validate_block_opt(&out_msg->options,
                                                        AVS_COAP_BLOCK2))));
    }

    if (avs_is_ok(err)
            && !_avs_coap_options_block_payload_valid(&out_msg->options,
                                                      out_msg->header.code,
                                                      out_msg->payload_size)) {
        err = _avs_coap_err(AVS_COAP_ERR_MALFORMED_OPTIONS);
    }
#    endif // WITH_AVS_COAP_BLOCK

    return err;
}

void _avs_coap_udp_msg_parse_truncated(avs_coap_udp_msg_t *out_msg,
                                       const uint8_t *packet,
                                       size_t packet_size,
                                       bool *out_has_token,
                                       bool *out_has_options) {
    bytes_dispenser_t dispenser = {
        .read_ptr = packet,
        .bytes_left = packet_size
    };

    memset(out_msg, 0, sizeof(*out_msg));

    *out_has_token = false;
    *out_has_options = false;
    if (avs_is_err(parse_header(&out_msg->header, &dispenser))
            || avs_is_err(_avs_coap_parse_token(
                       &out_msg->token,
                       _avs_coap_udp_header_get_token_length(&out_msg->header),
                       &dispenser))) {
        return;
    }
    *out_has_token = true;

    if (avs_is_ok(_avs_coap_options_parse(&out_msg->options, &dispenser, NULL,
                                          NULL))) {
        *out_has_options = true;
    }
}

avs_error_t _avs_coap_udp_msg_serialize(const avs_coap_udp_msg_t *msg,
                                        uint8_t *buf,
                                        size_t buf_size,
                                        size_t *out_bytes_written) {
    assert(msg);
    assert(buf);
    assert(out_bytes_written);

    assert(_avs_coap_udp_header_get_token_length(&msg->header)
           == msg->token.size);

    bytes_appender_t appender = {
        .write_ptr = buf,
        .bytes_left = buf_size
    };

    if (_avs_coap_bytes_append(&appender, &msg->header, sizeof(msg->header))
            || _avs_coap_bytes_append(&appender, msg->token.bytes,
                                      msg->token.size)
            || _avs_coap_bytes_append(&appender, msg->options.begin,
                                      msg->options.size)) {
        return _avs_coap_err(AVS_COAP_ERR_MESSAGE_TOO_BIG);
    }

    if (msg->payload && msg->payload_size > 0) {
        if (_avs_coap_bytes_append(&appender, &AVS_COAP_PAYLOAD_MARKER,
                                   sizeof(AVS_COAP_PAYLOAD_MARKER))
                || _avs_coap_bytes_append(&appender, msg->payload,
                                          msg->payload_size)) {
            return _avs_coap_err(AVS_COAP_ERR_MESSAGE_TOO_BIG);
        }
    }

    *out_bytes_written = buf_size - appender.bytes_left;
    return AVS_OK;
}

avs_error_t _avs_coap_udp_msg_copy(const avs_coap_udp_msg_t *src,
                                   avs_coap_udp_msg_t *dst,
                                   uint8_t *packet_buf,
                                   size_t packet_buf_size) {
    size_t written;
    avs_error_t err = _avs_coap_udp_msg_serialize(src, packet_buf,
                                                  packet_buf_size, &written);
    if (avs_is_err(err)) {
        return err;
    }

    // TODO: optimize
    return _avs_coap_udp_msg_parse(dst, packet_buf, written);
}

#endif // WITH_AVS_COAP_UDP
