/*
 * Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */

#include <anjay/anjay_config.h>
#include <avsystem/coap/avs_coap_config.h>
#include <avsystem/commons/avs_commons_config.h>

// Checks for usage of removed configuration macros
#ifdef ANJAY_WITH_MODULE_ATTR_STORAGE
#    error "ANJAY_WITH_MODULE_ATTR_STORAGE has been removed since Anjay 3.0. Please update your anjay_config.h to use ANJAY_WITH_ATTR_STORAGE instead."
#endif // ANJAY_WITH_MODULE_ATTR_STORAGE

#ifdef ANJAY_WITH_LWM2M_GATEWAY
#    ifndef ANJAY_WITH_LWM2M11
#        error "LwM2M Gateway functionality requires LwM2M 1.1 support."
#    endif // ANJAY_WITH_LWM2M11
#endif     // ANJAY_WITH_LWM2M_GATEWAY

#if defined(AVS_COMMONS_HAVE_VISIBILITY) && !defined(ANJAY_TEST)
/* set default visibility for external symbols */
#    pragma GCC visibility push(default)
#    define VISIBILITY_SOURCE_BEGIN _Pragma("GCC visibility push(hidden)")
#    define VISIBILITY_PRIVATE_HEADER_BEGIN \
        _Pragma("GCC visibility push(hidden)")
#    define VISIBILITY_PRIVATE_HEADER_END _Pragma("GCC visibility pop")
#else
#    define VISIBILITY_SOURCE_BEGIN
#    define VISIBILITY_PRIVATE_HEADER_BEGIN
#    define VISIBILITY_PRIVATE_HEADER_END
#endif

#ifdef ANJAY_WITH_TRACE_LOGS
#    define AVS_LOG_WITH_TRACE
#endif

#ifdef AVS_COMMONS_WITH_AVS_LOG
#    include <avsystem/commons/avs_log.h>
#    define _(Arg) AVS_DISPOSABLE_LOG(Arg)
#else // AVS_COMMONS_WITH_AVS_LOG
#    define _(Arg) Arg
#endif // AVS_COMMONS_WITH_AVS_LOG
