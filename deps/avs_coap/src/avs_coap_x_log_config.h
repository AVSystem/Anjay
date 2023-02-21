/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifdef LOG
#    undef LOG
#endif

#ifndef MODULE_NAME
#    error "You need to define MODULE_NAME before including this header"
#endif

#ifdef WITH_AVS_COAP_LOGS
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

#    ifdef WITH_AVS_COAP_TRACE_LOGS
#        define AVS_LOG_WITH_TRACE
#    endif
#    include <avsystem/commons/avs_log.h>
#    define LOG(...) avs_log(MODULE_NAME, __VA_ARGS__)
#else // WITH_AVS_COAP_LOGS
#    define LOG(...) ((void) 0)
// used by tcp_ctx
#    define avs_log_internal_l__(...) ((void) 0)
#endif // WITH_AVS_COAP_LOG
