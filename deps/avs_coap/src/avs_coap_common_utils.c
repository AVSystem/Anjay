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

#include <avsystem/commons/avs_errno.h>

#include <inttypes.h>

#include "options/avs_coap_iterator.h"

#define MODULE_NAME coap_utils
#include <avs_coap_x_log_config.h>

#include "avs_coap_common_utils.h"
#include "avs_coap_ctx.h"

VISIBILITY_SOURCE_BEGIN

int _avs_coap_bytes_append(bytes_appender_t *appender,
                           const void *data,
                           size_t size_bytes) {
    if (appender->bytes_left < size_bytes) {
        LOG(DEBUG,
            _("not enough space: required ") "%u" _(" free bytes, got ") "%u",
            (unsigned) size_bytes, (unsigned) appender->bytes_left);
        return -1;
    }
    memcpy(appender->write_ptr, data ? data : "", size_bytes);
    appender->write_ptr += size_bytes;
    appender->bytes_left -= size_bytes;
    return 0;
}

int _avs_coap_bytes_extract(bytes_dispenser_t *dispenser,
                            void *out,
                            size_t size_bytes) {
    if (dispenser->bytes_left < size_bytes) {
        LOG(DEBUG,
            _("incomplete data: tried to read ") "%u" _(" bytes, got ") "%u",
            (unsigned) size_bytes, (unsigned) dispenser->bytes_left);
        return -1;
    }

    if (out) {
        memcpy(out, dispenser->read_ptr, size_bytes);
    }

    dispenser->read_ptr += size_bytes;
    dispenser->bytes_left -= size_bytes;
    return 0;
}

avs_error_t _avs_coap_parse_token(avs_coap_token_t *out_token,
                                  uint8_t token_size,
                                  bytes_dispenser_t *dispenser) {
    out_token->size = token_size;

    AVS_ASSERT(token_size <= sizeof(out_token->bytes),
               "bug: not enough space for valid token");

    if (_avs_coap_bytes_extract(dispenser, out_token->bytes, out_token->size)) {
        LOG(DEBUG, _("truncated token"));
        return _avs_coap_err(AVS_COAP_ERR_MALFORMED_MESSAGE);
    }

    return AVS_OK;
}
