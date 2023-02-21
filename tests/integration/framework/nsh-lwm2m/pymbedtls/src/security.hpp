/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
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
