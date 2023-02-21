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

#    include <avsystem/commons/avs_errno.h>

#    include "avs_coap_tcp_utils.h"

#    include "avs_coap_code_utils.h"
#    include "options/avs_coap_iterator.h"

#    define MODULE_NAME coap_tcp
#    include <avs_coap_x_log_config.h>

#    include "avs_coap_tcp_ctx.h"
#    include "avs_coap_tcp_signaling.h"
#    include "options/avs_coap_options.h"

VISIBILITY_SOURCE_BEGIN

static avs_error_t handle_csm(avs_coap_tcp_csm_t *csm,
                              const avs_coap_borrowed_msg_t *msg) {
    csm->received = true;
    bool size_updated = false;
    bool block_updated = false;
    const avs_coap_options_t *opts = &msg->options;
    avs_coap_option_iterator_t it =
            _avs_coap_optit_begin((avs_coap_options_t *) (intptr_t) opts);

    // Options are guaranteed to be valid here, because they were checked
    // during receiving of the message.
    assert(_avs_coap_options_valid(opts));

    for (; !_avs_coap_optit_end(&it); _avs_coap_optit_next(&it)) {
        assert(it.curr_opt >= opts->begin);
        const avs_coap_option_t *opt = _avs_coap_optit_current(&it);
        uint32_t opt_number = _avs_coap_optit_number(&it);

        switch (opt_number) {
        case _AVS_COAP_OPTION_MAX_MESSAGE_SIZE: {
            if (!size_updated) {
                uint32_t max_msg_size;
                if (_avs_coap_option_u32_value(opt, &max_msg_size)) {
                    LOG(DEBUG, _("Max Message Size: value too big"));
                    // TODO Add Bad-CSM-Option to Abort message?
                    return _avs_coap_err(
                            AVS_COAP_ERR_TCP_MALFORMED_CSM_OPTIONS_RECEIVED);
                }
                csm->max_message_size = max_msg_size;
                size_updated = true;
            }
            break;
        }
        case _AVS_COAP_OPTION_BLOCK_WISE_TRANSFER_CAPABILITY:
            // TODO T2251
            // Inform upper layer that blocks are not supported by peer.
            // Currently we assume, that blocks are supported.
            if (!block_updated) {
                csm->block_wise_transfer_capable = true;
                block_updated = true;
            }
            break;
        default:
            // Options passed validation before this function was called, so
            // opt_number can be safely casted here.
            if (_avs_coap_option_is_critical((uint16_t) opt_number)) {
                LOG(DEBUG, _("unknown critical option"));
                return _avs_coap_err(
                        AVS_COAP_ERR_TCP_UNKNOWN_CSM_CRITICAL_OPTION_RECEIVED);
            }
            break;
        }
    }

    if (size_updated || block_updated) {
        LOG(DEBUG,
            _("Peer's Capabilities and Settings updated. "
              "Max-Message-Size: ") "%u" _(", Block-Wise-Transfer "
                                           "Capability: ") "%s",
            (unsigned) csm->max_message_size,
            csm->block_wise_transfer_capable ? "yes" : "no");
    }

    return AVS_OK;
}

static avs_error_t send_pong(avs_coap_tcp_ctx_t *ctx,
                             const avs_coap_borrowed_msg_t *msg) {
    uint8_t buf[8];
    avs_coap_borrowed_msg_t pong = {
        .code = AVS_COAP_CODE_PONG,
        .token = msg->token,
        .options = avs_coap_options_create_empty(buf, sizeof(buf))
    };
    (void) avs_coap_options_add_empty(&pong.options, _AVS_COAP_OPTION_CUSTODY);
    return _avs_coap_tcp_send_msg(ctx, &pong);
}

static void handle_abort(const avs_coap_borrowed_msg_t *msg) {
    LOG(DEBUG, _("Abort message received, the context should be destroyed"));
    if (msg->payload_size) {
        size_t bytes_escaped = 0;
        char escaped_string[128];
        do {
            bytes_escaped += _avs_coap_tcp_escape_payload(
                    (const char *) msg->payload + bytes_escaped,
                    msg->payload_size - bytes_escaped,
                    escaped_string,
                    sizeof(escaped_string));
            LOG(DEBUG, _("diagnostic payload: ") "%s", escaped_string);
        } while (bytes_escaped < msg->payload_size);
    }
}

avs_error_t
_avs_coap_tcp_handle_signaling_message(avs_coap_tcp_ctx_t *ctx,
                                       avs_coap_tcp_csm_t *peer_csm,
                                       const avs_coap_borrowed_msg_t *msg) {
    if (msg->payload_offset + msg->payload_size != msg->total_payload_size) {
        LOG(DEBUG, _("ignoring non-last chunk of Signaling message"));
        return AVS_OK;
    }

    switch (msg->code) {
    case AVS_COAP_CODE_CSM:
        return handle_csm(peer_csm, msg);
    case AVS_COAP_CODE_PING:
        return send_pong(ctx, msg);
    case AVS_COAP_CODE_PONG:
        LOG(DEBUG, _("unexpected Pong message arrived, ignoring"));
        break;
    case AVS_COAP_CODE_RELEASE:
        // All responses to incoming requests were sent already. If there is
        // some not completed block request, we can ignore it because:
        // "It is NOT RECOMMENDED for the sender of a Release message to
        //  continue sending requests on the connection it already indicated to
        //  be released: the peer might close the connection at any time and
        //  miss those requests.  The peer is not obligated to check for this
        //  condition, though."
        LOG(DEBUG,
            _("Release message received, the context should be destroyed"));
        return _avs_coap_err(AVS_COAP_ERR_TCP_RELEASE_RECEIVED);
    case AVS_COAP_CODE_ABORT:
        handle_abort(msg);
        return _avs_coap_err(AVS_COAP_ERR_TCP_ABORT_RECEIVED);
    default:
        LOG(DEBUG, _("unknown Signaling Message code, ignoring"));
    }
    return AVS_OK;
}

#endif // WITH_AVS_COAP_TCP
