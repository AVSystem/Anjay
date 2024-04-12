/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef FLUF_ATTRIBUTES_H
#define FLUF_ATTRIBUTES_H

#include <fluf/fluf.h>
#include <fluf/fluf_config.h>

#include "fluf_options.h"

#ifdef __cplusplus
extern "C" {
#endif

int fluf_attr_notification_attr_decode(const fluf_coap_options_t *opts,
                                       fluf_attr_notification_t *attr);
int fluf_attr_discover_decode(const fluf_coap_options_t *opts,
                              fluf_attr_discover_t *attr);
int fluf_attr_register_prepare(fluf_coap_options_t *opts,
                               const fluf_attr_register_t *attr);
int fluf_attr_bootstrap_prepare(fluf_coap_options_t *opts,
                                const fluf_attr_bootstrap_t *attr);

#ifdef __cplusplus
}
#endif

#endif // FLUF_ATTRIBUTES_H
