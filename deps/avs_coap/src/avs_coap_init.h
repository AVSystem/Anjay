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

#include <avsystem/commons/avs_defs.h>

#include <avsystem/coap/avs_coap_config.h>

#if !defined(AVS_COMMONS_WITH_AVS_BUFFER)                  \
        || !defined(AVS_COMMONS_WITH_AVS_COMPAT_THREADING) \
        || !defined(AVS_COMMONS_WITH_AVS_LIST)             \
        || !defined(AVS_COMMONS_WITH_AVS_NET)              \
        || !defined(AVS_COMMONS_WITH_AVS_SCHED)            \
        || !defined(AVS_COMMONS_WITH_AVS_UTILS)
#    error "avs_coap requires following avs_commons components to be enabled: avs_buffer avs_compat_threading avs_list avs_net avs_sched avs_utils"
#endif

#if defined(WITH_AVS_COAP_LOGS) && !defined(AVS_COMMONS_WITH_AVS_LOG)
#    error "WITH_AVS_COAP_LOGS requires avs_log to be enabled"
#endif

#if defined(WITH_AVS_COAP_STREAMING_API) \
        && !defined(AVS_COMMONS_WITH_AVS_STREAM)
#    error "WITH_AVS_COAP_STREAMING_API requires avs_stream to be enabled"
#endif

#if defined(WITH_AVS_COAP_OBSERVE_PERSISTENCE) \
        && !defined(AVS_COMMONS_WITH_AVS_PERSISTENCE)
#    error "WITH_AVS_COAP_OBSERVE_PERSISTENCE requires avs_persistence to be enabled"
#endif

#if defined(WITH_AVS_COAP_OSCORE)                 \
        && (!defined(AVS_COMMONS_WITH_AVS_CRYPTO) \
            || !defined(AVS_COMMONS_WITH_AVS_CRYPTO_ADVANCED_FEATURES))
#    error "WITH_AVS_COAP_OSCORE requires avs_crypto with advanced features to be enabled"
#endif

#ifdef AVS_COMMONS_HAVE_VISIBILITY
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

#if defined(WITH_AVS_COAP_POISONING) && !defined(AVS_UNIT_TESTING)
#    include "avs_coap_poison.h"
#endif

#if defined(AVS_UNIT_TESTING) && defined(__GNUC__)
#    define WEAK_IN_TESTS __attribute__((weak))
#elif defined(AVS_UNIT_TESTING)
#    error "Tests require GCC compatible compiler"
#else
#    define WEAK_IN_TESTS
#endif

#define _(Arg) AVS_DISPOSABLE_LOG(Arg)
