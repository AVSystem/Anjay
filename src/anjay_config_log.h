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

#ifndef ANJAY_CONFIG_LOG_H
#define ANJAY_CONFIG_LOG_H

#include <anjay_modules/anjay_utils_core.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

// clang-format off
static inline void _anjay_log_feature_list(void) {
    _anjay_log(anjay, TRACE, "ANJAY_DTLS_SESSION_BUFFER_SIZE = " AVS_QUOTE_MACRO(ANJAY_DTLS_SESSION_BUFFER_SIZE));
    _anjay_log(anjay, TRACE, "ANJAY_MAX_DOUBLE_STRING_SIZE = " AVS_QUOTE_MACRO(ANJAY_MAX_DOUBLE_STRING_SIZE));
    _anjay_log(anjay, TRACE, "ANJAY_MAX_PK_OR_IDENTITY_SIZE = " AVS_QUOTE_MACRO(ANJAY_MAX_PK_OR_IDENTITY_SIZE));
    _anjay_log(anjay, TRACE, "ANJAY_MAX_SECRET_KEY_SIZE = " AVS_QUOTE_MACRO(ANJAY_MAX_SECRET_KEY_SIZE));
    _anjay_log(anjay, TRACE, "ANJAY_MAX_SERVER_PK_OR_IDENTITY_SIZE = " AVS_QUOTE_MACRO(ANJAY_MAX_SERVER_PK_OR_IDENTITY_SIZE));
    _anjay_log(anjay, TRACE, "ANJAY_MAX_URI_QUERY_SEGMENT_SIZE = " AVS_QUOTE_MACRO(ANJAY_MAX_URI_QUERY_SEGMENT_SIZE));
    _anjay_log(anjay, TRACE, "ANJAY_MAX_URI_SEGMENT_SIZE = " AVS_QUOTE_MACRO(ANJAY_MAX_URI_SEGMENT_SIZE));
#ifdef ANJAY_WITH_ACCESS_CONTROL
    _anjay_log(anjay, TRACE, "ANJAY_WITH_ACCESS_CONTROL = ON");
#else // ANJAY_WITH_ACCESS_CONTROL
    _anjay_log(anjay, TRACE, "ANJAY_WITH_ACCESS_CONTROL = OFF");
#endif // ANJAY_WITH_ACCESS_CONTROL
#ifdef ANJAY_WITH_BOOTSTRAP
    _anjay_log(anjay, TRACE, "ANJAY_WITH_BOOTSTRAP = ON");
#else // ANJAY_WITH_BOOTSTRAP
    _anjay_log(anjay, TRACE, "ANJAY_WITH_BOOTSTRAP = OFF");
#endif // ANJAY_WITH_BOOTSTRAP
#ifdef ANJAY_WITH_CBOR
    _anjay_log(anjay, TRACE, "ANJAY_WITH_CBOR = ON");
#else // ANJAY_WITH_CBOR
    _anjay_log(anjay, TRACE, "ANJAY_WITH_CBOR = OFF");
#endif // ANJAY_WITH_CBOR
#ifdef ANJAY_WITH_COAP_DOWNLOAD
    _anjay_log(anjay, TRACE, "ANJAY_WITH_COAP_DOWNLOAD = ON");
#else // ANJAY_WITH_COAP_DOWNLOAD
    _anjay_log(anjay, TRACE, "ANJAY_WITH_COAP_DOWNLOAD = OFF");
#endif // ANJAY_WITH_COAP_DOWNLOAD
#ifdef ANJAY_WITH_COAP_OSCORE
    _anjay_log(anjay, TRACE, "ANJAY_WITH_COAP_OSCORE = ON");
#else // ANJAY_WITH_COAP_OSCORE
    _anjay_log(anjay, TRACE, "ANJAY_WITH_COAP_OSCORE = OFF");
#endif // ANJAY_WITH_COAP_OSCORE
#ifdef ANJAY_WITH_CON_ATTR
    _anjay_log(anjay, TRACE, "ANJAY_WITH_CON_ATTR = ON");
#else // ANJAY_WITH_CON_ATTR
    _anjay_log(anjay, TRACE, "ANJAY_WITH_CON_ATTR = OFF");
#endif // ANJAY_WITH_CON_ATTR
#ifdef ANJAY_WITH_CORE_PERSISTENCE
    _anjay_log(anjay, TRACE, "ANJAY_WITH_CORE_PERSISTENCE = ON");
#else // ANJAY_WITH_CORE_PERSISTENCE
    _anjay_log(anjay, TRACE, "ANJAY_WITH_CORE_PERSISTENCE = OFF");
#endif // ANJAY_WITH_CORE_PERSISTENCE
#ifdef ANJAY_WITH_DISCOVER
    _anjay_log(anjay, TRACE, "ANJAY_WITH_DISCOVER = ON");
#else // ANJAY_WITH_DISCOVER
    _anjay_log(anjay, TRACE, "ANJAY_WITH_DISCOVER = OFF");
#endif // ANJAY_WITH_DISCOVER
#ifdef ANJAY_WITH_DOWNLOADER
    _anjay_log(anjay, TRACE, "ANJAY_WITH_DOWNLOADER = ON");
#else // ANJAY_WITH_DOWNLOADER
    _anjay_log(anjay, TRACE, "ANJAY_WITH_DOWNLOADER = OFF");
#endif // ANJAY_WITH_DOWNLOADER
#ifdef ANJAY_WITH_HTTP_DOWNLOAD
    _anjay_log(anjay, TRACE, "ANJAY_WITH_HTTP_DOWNLOAD = ON");
#else // ANJAY_WITH_HTTP_DOWNLOAD
    _anjay_log(anjay, TRACE, "ANJAY_WITH_HTTP_DOWNLOAD = OFF");
#endif // ANJAY_WITH_HTTP_DOWNLOAD
#ifdef ANJAY_WITH_LEGACY_CONTENT_FORMAT_SUPPORT
    _anjay_log(anjay, TRACE, "ANJAY_WITH_LEGACY_CONTENT_FORMAT_SUPPORT = ON");
#else // ANJAY_WITH_LEGACY_CONTENT_FORMAT_SUPPORT
    _anjay_log(anjay, TRACE, "ANJAY_WITH_LEGACY_CONTENT_FORMAT_SUPPORT = OFF");
#endif // ANJAY_WITH_LEGACY_CONTENT_FORMAT_SUPPORT
#ifdef ANJAY_WITH_LOGS
    _anjay_log(anjay, TRACE, "ANJAY_WITH_LOGS = ON");
#else // ANJAY_WITH_LOGS
    _anjay_log(anjay, TRACE, "ANJAY_WITH_LOGS = OFF");
#endif // ANJAY_WITH_LOGS
#ifdef ANJAY_WITH_LWM2M11
    _anjay_log(anjay, TRACE, "ANJAY_WITH_LWM2M11 = ON");
#else // ANJAY_WITH_LWM2M11
    _anjay_log(anjay, TRACE, "ANJAY_WITH_LWM2M11 = OFF");
#endif // ANJAY_WITH_LWM2M11
#ifdef ANJAY_WITH_LWM2M_JSON
    _anjay_log(anjay, TRACE, "ANJAY_WITH_LWM2M_JSON = ON");
#else // ANJAY_WITH_LWM2M_JSON
    _anjay_log(anjay, TRACE, "ANJAY_WITH_LWM2M_JSON = OFF");
#endif // ANJAY_WITH_LWM2M_JSON
#ifdef ANJAY_WITH_MODULE_ACCESS_CONTROL
    _anjay_log(anjay, TRACE, "ANJAY_WITH_MODULE_ACCESS_CONTROL = ON");
#else // ANJAY_WITH_MODULE_ACCESS_CONTROL
    _anjay_log(anjay, TRACE, "ANJAY_WITH_MODULE_ACCESS_CONTROL = OFF");
#endif // ANJAY_WITH_MODULE_ACCESS_CONTROL
#ifdef ANJAY_WITH_MODULE_ATTR_STORAGE
    _anjay_log(anjay, TRACE, "ANJAY_WITH_MODULE_ATTR_STORAGE = ON");
#else // ANJAY_WITH_MODULE_ATTR_STORAGE
    _anjay_log(anjay, TRACE, "ANJAY_WITH_MODULE_ATTR_STORAGE = OFF");
#endif // ANJAY_WITH_MODULE_ATTR_STORAGE
#ifdef ANJAY_WITH_MODULE_AT_SMS
    _anjay_log(anjay, TRACE, "ANJAY_WITH_MODULE_AT_SMS = ON");
#else // ANJAY_WITH_MODULE_AT_SMS
    _anjay_log(anjay, TRACE, "ANJAY_WITH_MODULE_AT_SMS = OFF");
#endif // ANJAY_WITH_MODULE_AT_SMS
#ifdef ANJAY_WITH_MODULE_BG96_NIDD
    _anjay_log(anjay, TRACE, "ANJAY_WITH_MODULE_BG96_NIDD = ON");
#else // ANJAY_WITH_MODULE_BG96_NIDD
    _anjay_log(anjay, TRACE, "ANJAY_WITH_MODULE_BG96_NIDD = OFF");
#endif // ANJAY_WITH_MODULE_BG96_NIDD
#ifdef ANJAY_WITH_MODULE_BOOTSTRAPPER
    _anjay_log(anjay, TRACE, "ANJAY_WITH_MODULE_BOOTSTRAPPER = ON");
#else // ANJAY_WITH_MODULE_BOOTSTRAPPER
    _anjay_log(anjay, TRACE, "ANJAY_WITH_MODULE_BOOTSTRAPPER = OFF");
#endif // ANJAY_WITH_MODULE_BOOTSTRAPPER
#ifdef ANJAY_WITH_MODULE_FW_UPDATE
    _anjay_log(anjay, TRACE, "ANJAY_WITH_MODULE_FW_UPDATE = ON");
#else // ANJAY_WITH_MODULE_FW_UPDATE
    _anjay_log(anjay, TRACE, "ANJAY_WITH_MODULE_FW_UPDATE = OFF");
#endif // ANJAY_WITH_MODULE_FW_UPDATE
#ifdef ANJAY_WITH_MODULE_OSCORE
    _anjay_log(anjay, TRACE, "ANJAY_WITH_MODULE_OSCORE = ON");
#else // ANJAY_WITH_MODULE_OSCORE
    _anjay_log(anjay, TRACE, "ANJAY_WITH_MODULE_OSCORE = OFF");
#endif // ANJAY_WITH_MODULE_OSCORE
#ifdef ANJAY_WITH_MODULE_SECURITY
    _anjay_log(anjay, TRACE, "ANJAY_WITH_MODULE_SECURITY = ON");
#else // ANJAY_WITH_MODULE_SECURITY
    _anjay_log(anjay, TRACE, "ANJAY_WITH_MODULE_SECURITY = OFF");
#endif // ANJAY_WITH_MODULE_SECURITY
#ifdef ANJAY_WITH_MODULE_SERVER
    _anjay_log(anjay, TRACE, "ANJAY_WITH_MODULE_SERVER = ON");
#else // ANJAY_WITH_MODULE_SERVER
    _anjay_log(anjay, TRACE, "ANJAY_WITH_MODULE_SERVER = OFF");
#endif // ANJAY_WITH_MODULE_SERVER
#ifdef ANJAY_WITH_NET_STATS
    _anjay_log(anjay, TRACE, "ANJAY_WITH_NET_STATS = ON");
#else // ANJAY_WITH_NET_STATS
    _anjay_log(anjay, TRACE, "ANJAY_WITH_NET_STATS = OFF");
#endif // ANJAY_WITH_NET_STATS
#ifdef ANJAY_WITH_NIDD
    _anjay_log(anjay, TRACE, "ANJAY_WITH_NIDD = ON");
#else // ANJAY_WITH_NIDD
    _anjay_log(anjay, TRACE, "ANJAY_WITH_NIDD = OFF");
#endif // ANJAY_WITH_NIDD
#ifdef ANJAY_WITH_OBSERVATION_STATUS
    _anjay_log(anjay, TRACE, "ANJAY_WITH_OBSERVATION_STATUS = ON");
#else // ANJAY_WITH_OBSERVATION_STATUS
    _anjay_log(anjay, TRACE, "ANJAY_WITH_OBSERVATION_STATUS = OFF");
#endif // ANJAY_WITH_OBSERVATION_STATUS
#ifdef ANJAY_WITH_OBSERVE
    _anjay_log(anjay, TRACE, "ANJAY_WITH_OBSERVE = ON");
#else // ANJAY_WITH_OBSERVE
    _anjay_log(anjay, TRACE, "ANJAY_WITH_OBSERVE = OFF");
#endif // ANJAY_WITH_OBSERVE
#ifdef ANJAY_WITH_SEND
    _anjay_log(anjay, TRACE, "ANJAY_WITH_SEND = ON");
#else // ANJAY_WITH_SEND
    _anjay_log(anjay, TRACE, "ANJAY_WITH_SEND = OFF");
#endif // ANJAY_WITH_SEND
#ifdef ANJAY_WITH_SENML_JSON
    _anjay_log(anjay, TRACE, "ANJAY_WITH_SENML_JSON = ON");
#else // ANJAY_WITH_SENML_JSON
    _anjay_log(anjay, TRACE, "ANJAY_WITH_SENML_JSON = OFF");
#endif // ANJAY_WITH_SENML_JSON
#ifdef ANJAY_WITH_SMS
    _anjay_log(anjay, TRACE, "ANJAY_WITH_SMS = ON");
#else // ANJAY_WITH_SMS
    _anjay_log(anjay, TRACE, "ANJAY_WITH_SMS = OFF");
#endif // ANJAY_WITH_SMS
#ifdef ANJAY_WITH_SMS_MULTIPART
    _anjay_log(anjay, TRACE, "ANJAY_WITH_SMS_MULTIPART = ON");
#else // ANJAY_WITH_SMS_MULTIPART
    _anjay_log(anjay, TRACE, "ANJAY_WITH_SMS_MULTIPART = OFF");
#endif // ANJAY_WITH_SMS_MULTIPART
#ifdef ANJAY_WITH_TRACE_LOGS
    _anjay_log(anjay, TRACE, "ANJAY_WITH_TRACE_LOGS = ON");
#else // ANJAY_WITH_TRACE_LOGS
    _anjay_log(anjay, TRACE, "ANJAY_WITH_TRACE_LOGS = OFF");
#endif // ANJAY_WITH_TRACE_LOGS
}
// clang-format on

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_CONFIG_LOG_H */
