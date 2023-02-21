/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
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
#ifdef MBEDTLS_USE_PSA_CRYPTO
    if (psa_crypto_init() != PSA_SUCCESS) {
        throw runtime_error("psa_crypto_init() failed");
    }
#endif // MBEDTLS_USE_PSA_CRYPTO

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
