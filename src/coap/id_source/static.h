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

#ifndef ANJAY_COAP_IDSOURCE_STATIC_H
#define ANJAY_COAP_IDSOURCE_STATIC_H

#include "id_source.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

coap_id_source_t *
_anjay_coap_id_source_new_static(const anjay_coap_msg_identity_t *id);

void
_anjay_coap_id_source_static_reset(coap_id_source_t *self_,
                                   const anjay_coap_msg_identity_t *new_id);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_COAP_IDSOURCE_STATIC_H
