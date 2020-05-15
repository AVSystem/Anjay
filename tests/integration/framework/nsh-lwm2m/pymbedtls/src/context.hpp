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

#ifndef PYMBEDTLS_CONTEXT_HPP
#define PYMBEDTLS_CONTEXT_HPP

#include <memory>
#include <string>

#include <mbedtls/ssl_cache.h>

namespace ssl {

class SecurityInfo;

class Context {
    mbedtls_ssl_cache_context session_cache_;
    std::shared_ptr<SecurityInfo> security_;
    bool debug_;
    std::string connection_id_;

public:
    Context(std::shared_ptr<SecurityInfo> security,
            bool debug,
            std::string connection_id);
    ~Context();

    mbedtls_ssl_cache_context *session_cache() {
        return &session_cache_;
    }

    std::shared_ptr<SecurityInfo> security() const {
        return security_;
    }

    std::string connection_id() const {
        return connection_id_;
    }

    bool debug() const {
        return debug_;
    }
};

} // namespace ssl

#endif // PYMBEDTLS_CONTEXT_HPP
