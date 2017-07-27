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

#include "block_utils.h"
#include "log.h"
#include "msg_opt.h"

// For _anjay_is_power_of_2
#include "../utils.h"

VISIBILITY_SOURCE_BEGIN

int _anjay_coap_get_block_info(const anjay_coap_msg_t *msg,
                               coap_block_type_t type,
                               coap_block_info_t *out_info) {
    assert(msg);
    assert(out_info);
    uint16_t opt_number = type == COAP_BLOCK1
            ? ANJAY_COAP_OPT_BLOCK1
            : ANJAY_COAP_OPT_BLOCK2;
    const anjay_coap_opt_t *opt;
    memset(out_info, 0, sizeof(*out_info));
    if (_anjay_coap_msg_find_unique_opt(msg, opt_number, &opt)) {
        if (opt) {
            int num = opt_number == ANJAY_COAP_OPT_BLOCK1 ? 1 : 2;
            coap_log(ERROR, "multiple BLOCK%d options found", num);
            return -1;
        }
        return 0;
    }
    out_info->type = type;
    out_info->valid = !_anjay_coap_opt_block_seq_number(opt, &out_info->seq_num)
            && !_anjay_coap_opt_block_has_more(opt, &out_info->has_more)
            && !_anjay_coap_opt_block_size(opt, &out_info->size);

    return out_info->valid ? 0 : -1;
}

bool _anjay_coap_is_valid_block_size(uint16_t size) {
    return _anjay_is_power_of_2(size)
            && size <= ANJAY_COAP_MSG_BLOCK_MAX_SIZE
            && size >= ANJAY_COAP_MSG_BLOCK_MIN_SIZE;
}
