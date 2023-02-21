/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_INCLUDE_ANJAY_MODULES_BOOTSTRAP_H
#define ANJAY_INCLUDE_ANJAY_MODULES_BOOTSTRAP_H

#include <stdbool.h>

#include <anjay_modules/anjay_io_utils.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

#ifdef ANJAY_WITH_BOOTSTRAP

bool _anjay_bootstrap_in_progress(anjay_unlocked_t *anjay);

#    if defined(ANJAY_WITH_MODULE_FACTORY_PROVISIONING)
avs_error_t _anjay_bootstrap_delete_everything(anjay_unlocked_t *anjay);

int _anjay_bootstrap_finish(anjay_unlocked_t *anjay);
#    endif /* defined(ANJAY_WITH_MODULE_BOOTSTRAPPER) || \
              defined(ANJAY_WITH_MODULE_FACTORY_PROVISIONING) */

#    ifdef ANJAY_WITH_MODULE_FACTORY_PROVISIONING
int _anjay_bootstrap_write_composite(anjay_unlocked_t *anjay,
                                     anjay_unlocked_input_ctx_t *in_ctx);
#    endif // ANJAY_WITH_MODULE_FACTORY_PROVISIONING

#    ifdef ANJAY_WITH_LWM2M11
int _anjay_schedule_bootstrap_request_unlocked(anjay_unlocked_t *anjay);
#    endif // ANJAY_WITH_LWM2M11

#else

#    define _anjay_bootstrap_in_progress(anjay) ((void) (anjay), false)

#endif

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_INCLUDE_ANJAY_MODULES_BOOTSTRAP_H */
