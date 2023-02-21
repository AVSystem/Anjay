/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
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
