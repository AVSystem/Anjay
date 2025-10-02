/*
 * Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
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

#include <exception>
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

    // Used to capture exceptions that may be thrown in callbacks that are
    // implemented in C++, but called from C code. As it's generally wrong to
    // throw exception through the stack which incorporates C code, we capture
    // the exception in case we expect some callback to generate it, and then
    // rethrow it in a safe place.
    //
    // As we want to capture exceptions only when explicicitly requested, we use
    // a pointer to std::exception_ptr for additional level of indirection.
    std::exception_ptr *exception_capturer_;

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

    static int
    bio_send(void *self, const unsigned char *buf, size_t len) noexcept;
    static int bio_recv(void *self,
                        unsigned char *buf,
                        size_t len,
                        uint32_t timeout_ms) noexcept;

    HandshakeResult do_handshake();

public:
    Socket(std::shared_ptr<Context> context,
           py::object py_socket,
           SocketType type);

    ~Socket();

    void perform_handshake(py::tuple host_port,
                           py::object handshake_timeouts_s_,
                           bool py_connect);
    void send(const std::string &data);
    py::bytes recv(int);
    py::bytes peer_cert();

    // __getattr__ (and __setattr__) is called when Python is unable to directly
    // find the attribute of an object. By redirecting this to __get_attribute__
    // of the py_socket_ field, we're essentially extending the class of
    // py_socket_.
    py::object __getattr__(py::object name);
    void __setattr__(py::object name, py::object value);

    void connect(py::tuple host_port, py::object handshake_timeouts_s) {
        perform_handshake(host_port, handshake_timeouts_s, true);
    }
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
