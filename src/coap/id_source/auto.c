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

#include "auto.h"

#include <stdlib.h>

#include <avsystem/commons/memory.h>

VISIBILITY_SOURCE_BEGIN

typedef struct coap_default_id_src {
    coap_id_source_t base;
    anjay_rand_seed_t rand_seed;
    uint16_t next_msg_id;
    uint8_t token_size;
} coap_default_id_src_t;

static avs_coap_msg_identity_t id_src_seq_get(coap_id_source_t *self_) {
    coap_default_id_src_t *self = (coap_default_id_src_t *) self_;

    avs_coap_msg_identity_t id = {
        .msg_id = self->next_msg_id,
        .token = {
            .size = self->token_size
        }
    };
    for (uint8_t i = 0; i < self->token_size; ++i) {
        id.token.bytes[i] = (char) _anjay_rand32(&self->rand_seed);
    }
    ++self->next_msg_id;

    return id;
}

static const coap_id_source_vt_t *const ID_SRC_SEQ_VTABLE =
        &(coap_id_source_vt_t) {
            .get = id_src_seq_get
        };

coap_id_source_t *_anjay_coap_id_source_auto_new(anjay_rand_seed_t rand_seed,
                                                 size_t token_size) {
    assert(token_size <= AVS_COAP_MAX_TOKEN_LENGTH);
    coap_default_id_src_t *src =
            (coap_default_id_src_t *) avs_malloc(sizeof(coap_default_id_src_t));
    if (!src) {
        return NULL;
    }

    memcpy((void *) (intptr_t) &src->base.vtable, &ID_SRC_SEQ_VTABLE,
           sizeof(ID_SRC_SEQ_VTABLE));
    src->rand_seed = rand_seed;
    src->next_msg_id = (uint16_t) _anjay_rand32(&src->rand_seed);
    src->token_size = (uint8_t) token_size;

    return &src->base;
}
