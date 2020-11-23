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

#ifndef PYMBEDTLS_SOCKET_HPP
#define PYMBEDTLS_SOCKET_HPP
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/debug.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/version.h>
#if MBEDTLS_VERSION_NUMBER >= 0x02040000 // mbed TLS 2.4 deprecated net.h
#    include <mbedtls/net_sockets.h>
#else // support mbed TLS <=2.3
#    include <mbedtls/net.h>
#endif
#include <mbedtls/ssl.h>
#include <mbedtls/ssl_cache.h>
#include <mbedtls/ssl_cookie.h>
#include <mbedtls/timing.h>

#include <memory>

#include "pybind11_interop.hpp"

namespace ssl {
class Context;

enum class SocketType { Client, Server };

class Socket {
    friend class SecurityInfo;
    friend class PskSecurity;
    friend class CertSecurity;

    enum class HandshakeResult { Finished, HelloVerifyRequired };

    std::shared_ptr<Context> context_;
    mbedtls_ssl_context mbedtls_context_;
    mbedtls_ssl_cookie_ctx cookie_;
    mbedtls_ssl_config config_;
    mbedtls_entropy_context entropy_;
    mbedtls_ctr_drbg_context rng_;
    mbedtls_timing_delay_context timer_;
    std::vector<int> ciphersuites_;

    SocketType type_;
    py::object py_socket_;
    bool in_handshake_;

    // Used to match incoming packets with a client we initially are
    // connect()'ed to. It may change, if, for example connection_id extension
    // is used and we received a packet from a different endpoint but the
    // connection_id matched.
    std::tuple<std::string, int> client_host_and_port_;
    // Updated whenever we receive a packet from an endpoint we don't recognize.
    // It must be there, because at the time of performing recv() we haven't
    // parsed the packet as TLS record, and we cannot extract the connection_id
    // (if any) to see if the packet is indeed valid and should be handled.
    std::tuple<std::string, int> last_recv_host_and_port_;

    static int _send(void *self, const unsigned char *buf, size_t len);
    static int
    _recv(void *self, unsigned char *buf, size_t len, uint32_t timeout_ms);

    HandshakeResult do_handshake();

public:
    Socket(std::shared_ptr<Context> context,
           py::object py_socket,
           SocketType type);

    ~Socket();

    void connect(py::tuple host_port, py::object handshake_timeouts_s_);
    void send(const std::string &data);
    py::bytes recv(int);
    void settimeout(py::object timeout_s_or_none);
    py::bytes peer_cert();
    py::object __getattr__(py::object name);
    void __setattr__(py::object name, py::object value);
};

class ServerSocket {
    std::shared_ptr<Context> context_;
    py::object py_socket_;

public:
    ServerSocket(std::shared_ptr<Context> context, py::object py_socket);

    std::unique_ptr<Socket> accept(py::object handshake_timeouts_s);

    py::object __getattr__(py::object name);
    void __setattr__(py::object name, py::object value);
};

} // namespace ssl

#endif // PYMBEDTLS_SOCKET_HPP
