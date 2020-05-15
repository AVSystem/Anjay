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

#ifndef ANJAY_COAP_CONTENT_FORMAT_H
#define ANJAY_COAP_CONTENT_FORMAT_H

/* for AVS_COAP_FORMAT_NONE */
#include <avsystem/coap/option.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

/** Auxiliary constants for common Content-Format Option values */

#ifdef ANJAY_WITH_LEGACY_CONTENT_FORMAT_SUPPORT
#    define ANJAY_COAP_FORMAT_LEGACY_PLAINTEXT 1541
#    define ANJAY_COAP_FORMAT_LEGACY_TLV 1542
#    define ANJAY_COAP_FORMAT_LEGACY_JSON 1543
#    define ANJAY_COAP_FORMAT_LEGACY_OPAQUE 1544
#endif // ANJAY_WITH_LEGACY_CONTENT_FORMAT_SUPPORT

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_COAP_CONTENT_FORMAT_H
