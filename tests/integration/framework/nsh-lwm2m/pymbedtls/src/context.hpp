/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
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
