/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <stdexcept>

#include "common.hpp"
#include "security.hpp"
#include "socket.hpp"

#include "pybind11_interop.hpp"

using namespace std;

namespace ssl {

void SecurityInfo::configure(Socket &socket) {
    if (!ciphersuites_.empty()) {
        socket.ciphersuites_ = ciphersuites_;
        socket.ciphersuites_.push_back(0);
        mbedtls_ssl_conf_ciphersuites(&socket.config_,
                                      socket.ciphersuites_.data());
    }
}

void PskSecurity::configure(Socket &socket) {
    mbedtls_ssl_conf_psk(&socket.config_,
                         reinterpret_cast<const unsigned char *>(key_.data()),
                         key_.size(),
                         reinterpret_cast<const unsigned char *>(
                                 identity_.data()),
                         identity_.size());

    SecurityInfo::configure(socket);
}

string PskSecurity::name() const {
    return "psk";
}

CertSecurity::CertSecurity(const char *ca_path,
                           const char *ca_file,
                           const char *crt_file,
                           const char *key_file)
        : SecurityInfo({ MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8 }),
          configure_ca_(ca_path || ca_file),
          configure_crt_(crt_file && key_file) {
    mbedtls_pk_init(&pk_ctx_);
    mbedtls_x509_crt_init(&ca_certs_);
    mbedtls_x509_crt_init(&crt_);

    int result;
    if (ca_path
            && (result = mbedtls_x509_crt_parse_path(&ca_certs_, ca_path))) {
        throw mbedtls_error(string("Could not load certificates from CA-path ")
                                    + ca_path,
                            result);
    }
    if (ca_file
            && (result = mbedtls_x509_crt_parse_file(&ca_certs_, ca_file))) {
        throw mbedtls_error(string("Could not load certificate from CA-file ")
                                    + ca_file,
                            result);
    }
    if (key_file) {
#if MBEDTLS_VERSION_NUMBER >= 0x03000000
        mbedtls_ctr_drbg_context rng;
        mbedtls_ctr_drbg_init(&rng);
#endif // MBEDTLS_VERSION_NUMBER >= 0x03000000
        result = mbedtls_pk_parse_keyfile(&pk_ctx_, key_file, nullptr
#if MBEDTLS_VERSION_NUMBER >= 0x03000000
                                          ,
                                          mbedtls_ctr_drbg_random, &rng
#endif // MBEDTLS_VERSION_NUMBER >= 0x03000000
        );
        if (result) {
            throw mbedtls_error(string("Could not parse private-key file ")
                                        + key_file,
                                result);
        }
    }
    if (crt_file && (result = mbedtls_x509_crt_parse_file(&crt_, crt_file))) {
        throw mbedtls_error(string("Could not load certificate from file ")
                                    + crt_file,
                            result);
    }
}

CertSecurity::~CertSecurity() {
    mbedtls_x509_crt_free(&crt_);
    mbedtls_x509_crt_free(&ca_certs_);
    mbedtls_pk_free(&pk_ctx_);
}

void CertSecurity::configure(Socket &socket) {
    SecurityInfo::configure(socket);
    mbedtls_ssl_conf_authmode(&socket.config_, MBEDTLS_SSL_VERIFY_NONE);

    if (configure_ca_) {
        mbedtls_ssl_conf_authmode(&socket.config_, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&socket.config_, &ca_certs_, nullptr);
    }
    if (configure_crt_) {
        int result =
                mbedtls_ssl_conf_own_cert(&socket.config_, &crt_, &pk_ctx_);
        if (result) {
            throw mbedtls_error("Could not set own certificate", result);
        }
    }
}

string CertSecurity::name() const {
    return "cert";
}

} // namespace ssl
