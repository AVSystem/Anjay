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

#include "static.h"

#include <stdlib.h>

VISIBILITY_SOURCE_BEGIN

typedef struct coap_static_id_src {
    coap_id_source_t base;
    anjay_coap_msg_identity_t id;
} coap_static_id_src_t;

static anjay_coap_msg_identity_t
id_src_static_get(coap_id_source_t *self_) {
    coap_static_id_src_t *self = (coap_static_id_src_t *)self_;
    return self->id;
}

static const coap_id_source_vt_t *const ID_SRC_STATIC_VTABLE =
    &(coap_id_source_vt_t){
        .get = id_src_static_get
    };

coap_id_source_t *
_anjay_coap_id_source_new_static(const anjay_coap_msg_identity_t *id) {
    coap_static_id_src_t *src = (coap_static_id_src_t *)
            malloc(sizeof(coap_static_id_src_t));
    if (!src) {
        return NULL;
    }

    memcpy((void*)(intptr_t)&src->base.vtable, &ID_SRC_STATIC_VTABLE,
           sizeof(ID_SRC_STATIC_VTABLE));
    src->id = *id;

    return &src->base;
}

void
_anjay_coap_id_source_static_reset(coap_id_source_t *self_,
                                   const anjay_coap_msg_identity_t *new_id) {
    assert(self_->vtable == ID_SRC_STATIC_VTABLE);

    coap_static_id_src_t *self = (coap_static_id_src_t *)self_;
    self->id = *new_id;
}
