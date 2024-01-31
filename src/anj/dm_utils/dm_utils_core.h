/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_DM_UTILS_CORE_H
#define ANJAY_DM_UTILS_CORE_H

#include <inttypes.h>

#ifdef AVS_COMMONS_WITH_AVS_LOG
#    include <avsystem/commons/avs_log.h>
#    define _(Arg) AVS_DISPOSABLE_LOG(Arg)
#else // AVS_COMMONS_WITH_AVS_LOG
#    define _(Arg) Arg
#endif // AVS_COMMONS_WITH_AVS_LOG

#ifdef DM_WITH_LOGS
#    ifndef AVS_COMMONS_WITH_AVS_LOG
#        error "DM_WITH_LOGS requires avs_log to be enabled"
#    endif
// these macros interfere with avs_log() macro implementation
#    ifdef TRACE
#        undef TRACE
#    endif
#    ifdef DEBUG
#        undef DEBUG
#    endif
#    ifdef INFO
#        undef INFO
#    endif
#    ifdef WARNING
#        undef WARNING
#    endif
#    ifdef ERROR
#        undef ERROR
#    endif
#    include <avsystem/commons/avs_log.h>
#    define dm_log(...) avs_log(dm, __VA_ARGS__)
#else
#    include <stdio.h>
#    define dm_log(Module, ...) ((void) sizeof(printf(__VA_ARGS__)))

#endif // DM_WITH_LOGS

#include <avsystem/commons/avs_list.h>
#include <avsystem/commons/avs_url.h>

#define DM_FOREACH_BREAK INT_MIN
#define DM_FOREACH_CONTINUE 0
#define DM_OID_SECURITY 0

#endif /* ANJAY_DM_UTILS_CORE_H */
