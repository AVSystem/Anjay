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

#ifndef ANJAY_COAP_LOG_H
#define ANJAY_COAP_LOG_H

#include <anjay_modules/utils.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

#define coap_log(...) _anjay_log(coap, __VA_ARGS__)

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_COAP_LOG_H
