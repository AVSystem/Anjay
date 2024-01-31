/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_CONFIG_H
#define ANJAY_CONFIG_H

#ifndef ANJ_FOTA_PULL_METHOD_SUPPORTED
#    define ANJ_FOTA_PULL_METHOD_SUPPORTED 0
#endif // ANJ_FOTA_PULL_METHOD_SUPPORTED

#ifndef ANJ_FOTA_PUSH_METHOD_SUPPORTED
#    define ANJ_FOTA_PUSH_METHOD_SUPPORTED 1
#endif // ANJ_FOTA_PUSH_METHOD_SUPPORTED

#ifndef ANJ_TIME_POSIX_COMPAT
#    define ANJ_TIME_POSIX_COMPAT 1
#endif // ANJ_TIME_POSIX_COMPAT

#endif // ANJAY_CONFIG_H
