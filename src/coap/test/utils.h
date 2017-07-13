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

#include <alloca.h>

#include <avsystem/commons/unit/test.h>

#include "../msg.h"

#define PAYLOAD_MARKER "\xFF"

#define VTTL(version, type, token_length) \
    ((((version) & 0x03) << 6) | (((type) & 0x03) << 4) | ((token_length) & 0x0f))

static void setup_msg(anjay_coap_msg_t *msg,
                      const uint8_t *content,
                      size_t content_length) {
    static const anjay_coap_msg_t TEMPLATE = {
        .header = {
            .version_type_token_length = VTTL(1, ANJAY_COAP_MSG_ACKNOWLEDGEMENT, 0),
            .code = ANJAY_COAP_CODE(3, 4),
            .message_id = { 5, 6 }
        }
    };
    memset(msg, 0, sizeof(*msg) + content_length);
    memcpy(msg, &TEMPLATE, offsetof(anjay_coap_msg_t, content));
    assert(content || content_length == 0);
    if (content_length) {
        memcpy(msg->content, content, content_length);
    }
    msg->length = (uint32_t)(sizeof(msg->header) + content_length);
}
