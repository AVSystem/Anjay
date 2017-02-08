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

#ifndef ANJAY_COAP_IDSOURCE_H
#define ANJAY_COAP_IDSOURCE_H

#include "../msg.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct coap_id_source coap_id_source_t;

typedef anjay_coap_msg_identity_t coap_id_source_get_t(coap_id_source_t *self);

typedef struct coap_id_source_vt {
    coap_id_source_get_t *get;
} coap_id_source_vt_t;

struct coap_id_source {
    const coap_id_source_vt_t *const vtable;
};

static inline void
_anjay_coap_id_source_release(coap_id_source_t **src) {
    if (src && *src) {
        free(*src);
        *src = NULL;
    }
}

static inline anjay_coap_msg_identity_t
_anjay_coap_id_source_get(coap_id_source_t *src) {
    return src->vtable->get(src);
}

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_COAP_IDSOURCE_H
