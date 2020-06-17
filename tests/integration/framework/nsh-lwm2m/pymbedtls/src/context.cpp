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

#include <cstring>
#include <stdexcept>

#include "context.hpp"

using namespace std;

namespace ssl {

Context::Context(std::shared_ptr<SecurityInfo> security,
                 bool debug,
                 std::string connection_id)
        : security_(security), debug_(debug), connection_id_(connection_id) {
    memset(&session_cache_, 0, sizeof(session_cache_));
    mbedtls_ssl_cache_init(&session_cache_);

#if !defined(MBEDTLS_SSL_DTLS_CONNECTION_ID)
    if (connection_id.size() > 0) {
        throw runtime_error(
                "connection_id is not supported in this version of pymbedtls");
    }
#endif // !MBEDTLS_SSL_DTLS_CONNECTION_ID
}

Context::~Context() {
    mbedtls_ssl_cache_free(&session_cache_);
}

} // namespace ssl
