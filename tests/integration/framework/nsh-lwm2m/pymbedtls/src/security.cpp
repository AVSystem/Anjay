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
    if (key_file
            && (result = mbedtls_pk_parse_keyfile(
                        &pk_ctx_, key_file, nullptr))) {
        throw mbedtls_error(
                string("Could not parse private-key file ") + key_file, result);
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
