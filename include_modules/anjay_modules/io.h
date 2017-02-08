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

#ifndef ANJAY_INCLUDE_ANJAY_MODULES_IO_H
#define ANJAY_INCLUDE_ANJAY_MODULES_IO_H

#include <avsystem/commons/stream.h>

#include <stdint.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

uint32_t _anjay_htonf(float f);
uint64_t _anjay_htond(double d);
float _anjay_ntohf(uint32_t v);
double _anjay_ntohd(uint64_t v);

typedef int anjay_input_ctx_constructor_t(anjay_input_ctx_t **out,
                                          avs_stream_abstract_t **stream_ptr,
                                          bool autoclose);

anjay_input_ctx_constructor_t _anjay_input_tlv_create;

int _anjay_input_ctx_destroy(anjay_input_ctx_t **ctx_ptr);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_INCLUDE_ANJAY_MODULES_IO_H */
