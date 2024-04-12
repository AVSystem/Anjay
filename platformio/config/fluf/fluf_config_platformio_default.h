/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef FLUF_CONFIG_H
#define FLUF_CONFIG_H

#define FLUF_WITH_CBOR
#define FLUF_WITH_CBOR_DECIMAL_FRACTIONS
#define FLUF_WITH_CBOR_HALF_FLOAT
#define FLUF_WITH_CBOR_INDEFINITE_BYTES
#define FLUF_WITH_CBOR_STRING_TIME
#define FLUF_WITH_LWM2M12
#define FLUF_WITH_LWM2M_CBOR
/* #undef FLUF_WITHOUT_BOOTSTRAP_DISCOVER_CTX */
/* #undef FLUF_WITHOUT_DISCOVER_CTX */
/* #undef FLUF_WITHOUT_REGISTER_CTX */
#define FLUF_WITH_PLAINTEXT
#define FLUF_WITH_SENML_CBOR
#define FLUF_WITH_OPAQUE

#define FLUF_MAX_ALLOWED_OPTIONS_NUMBER 15
#define FLUF_MAX_ALLOWED_LOCATION_PATHS_NUMBER 1
#define FLUF_ATTR_OPTION_MAX_SIZE 100

#endif // FLUF_CONFIG_H
