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

#include <anjay/anjay_config.h>
#include <avsystem/commons/avs_commons_config.h>

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
