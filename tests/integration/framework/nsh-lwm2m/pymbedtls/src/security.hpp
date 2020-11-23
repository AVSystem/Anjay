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

#ifndef PYMBEDTLS_SECURITY_H
#define PYMBEDTLS_SECURITY_H
#include <mbedtls/ssl.h>

#include <memory>
#include <string>
#include <vector>

namespace ssl {
class Socket;

class SecurityInfo {
protected:
    std::vector<int> ciphersuites_;

    SecurityInfo(std::vector<int> &&default_ciphersuites)
            : ciphersuites_(std::move(default_ciphersuites)) {}

public:
    virtual ~SecurityInfo() = default;
    virtual void configure(Socket &socket);
    virtual std::string name() const = 0;
    void set_ciphersuites(const std::vector<int> &ciphersuites) {
        ciphersuites_ = ciphersuites;
    }
};

class PskSecurity : public SecurityInfo {
    std::string key_;
    std::string identity_;

public:
    PskSecurity(const std::string &key, const std::string &identity)
            : SecurityInfo({ MBEDTLS_TLS_PSK_WITH_AES_128_CCM_8 }),
              key_(key),
              identity_(identity) {}

    PskSecurity() = default;
    PskSecurity(const PskSecurity &) = default;
    virtual void configure(Socket &socket);
    virtual std::string name() const;
};

class CertSecurity : public SecurityInfo {
    mbedtls_pk_context pk_ctx_;
    mbedtls_x509_crt ca_certs_;
    mbedtls_x509_crt crt_;
    bool configure_ca_;
    bool configure_crt_;

public:
    /**
     * @param ca_path  Path containing the top-level PEM/DER encoded CA(s)
     * @param ca_file  The PEM/DER encoded file containing top-level CA(s)
     * @param crt_file The PEM/DER encoded file containing client/server
     *                 certificates
     * @param key_file The PEM/DER encoded file containing client/server key
     */
    CertSecurity(const char *ca_path,
                 const char *ca_file,
                 const char *crt_file,
                 const char *key_file);

    virtual ~CertSecurity();

    CertSecurity() = default;
    CertSecurity(const CertSecurity &) = default;
    virtual void configure(Socket &socket);
    virtual std::string name() const;
};

} // namespace ssl

#endif // PYMBEDTLS_SECURITY_H
