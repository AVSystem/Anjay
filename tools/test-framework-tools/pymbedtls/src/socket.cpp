/*
 * Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */

#include <chrono>
#include <sstream>
#include <stdexcept>

#include <arpa/inet.h>

#include <sys/socket.h>
#include <sys/types.h>

#include "common.hpp"
#include "context.hpp"
#include "security.hpp"
#include "socket.hpp"

using namespace std;
using namespace chrono;

namespace {

int process_python_socket_error(py::error_already_set &err, int default_err) {
    // Ensure that the `socket` in the eval below is actually python
    // socket, and not some module found in the context of python code
    // that caused this _recv() to be called on c++ side.
    py::object scope = py::module::import("socket").attr("__dict__");

    if (err.matches(py::eval("timeout", scope))) {
        return MBEDTLS_ERR_SSL_TIMEOUT;
    } else {
        return default_err;
    }
}

int get_socket_type(const py::object &py_socket) {
    int result = py_socket.attr("type").cast<int>();
    // On Linux, some flags may be stored in the socket type value, and some
    // versions of Python update them when changing socket blocking state.
    // We need to strip them for the values to meaningfully compare to anything.
#ifdef SOCK_NONBLOCK
    result &= ~SOCK_NONBLOCK;
#endif // SOCK_NONBLOCK
#ifdef SOCK_CLOEXEC
    result &= ~SOCK_CLOEXEC;
#endif // SOCK_CLOEXEC
    return result;
}

} // namespace

namespace ssl {

int Socket::bio_send(void *self,
                     const unsigned char *buf,
                     size_t len) noexcept try {
    Socket *socket = reinterpret_cast<Socket *>(self);

    call_method<void>(
            socket->py_socket_, "sendall",
            py::reinterpret_borrow<py::object>(
                    PyMemoryView_FromMemory((char *) buf, len, PyBUF_READ)));
    return (int) len;
} catch (py::error_already_set &err) {
    return process_python_socket_error(err, MBEDTLS_ERR_NET_SEND_FAILED);
}

namespace {
tuple<string, int> host_port_to_std_tuple(py::tuple host_port) {
    return make_tuple(py::cast<string>(host_port[0]),
                      py::cast<int>(host_port[1]));
}
} // namespace

bool py_timeout_finite(py::object timeout) {
    return !timeout.is(py::none());
}

uint32_t to_millis_timeout(py::object timeout) {
    if (!py_timeout_finite(timeout)) {
        return UINT32_MAX;
    }
    return (uint32_t) (py::cast<double>(timeout) * 1000.0);
}

int Socket::bio_recv(void *self,
                     unsigned char *buf,
                     size_t len,
                     uint32_t mbedtls_timeout) noexcept {
    Socket *socket = reinterpret_cast<Socket *>(self);

    py::object py_buf = py::reinterpret_borrow<py::object>(
            PyMemoryView_FromMemory((char *) buf, len, PyBUF_WRITE));

    py::object py_timeout =
            call_method<py::object>(socket->py_socket_, "gettimeout");

    // Since this method will possibly do multiple recv() calls, we'll be
    // adjusting the underlying timeout in the runtime. When we're done,
    // restore the original timeout.
    const auto restore_timeout = helpers::defer([&] {
        call_method<void>(socket->py_socket_, "settimeout", py_timeout);
    });

    // If the timeout set by mbedTLS is 0 (infinite), assume the timeout we set
    // to the underlying socket (as we do in avs_commons). Otherwise, timeout
    // from mbedTLS gets precedence (this happens e.g. during handshake).
    uint32_t timeout_ms = mbedtls_timeout > 0 ? mbedtls_timeout
                                              : to_millis_timeout(py_timeout);
    bool timeout_finite = mbedtls_timeout > 0 || py_timeout_finite(py_timeout);
    int socket_type = get_socket_type(socket->py_socket_);

    int bytes_received = 0;
    do {
        try {
            if (timeout_finite) {
                call_method<void>(socket->py_socket_, "settimeout",
                                  timeout_ms / 1000.0);
            }

            py::tuple num_received_and_peer;
            const auto before_recv = steady_clock::now();
            if (socket_type == SOCK_DGRAM) {
                num_received_and_peer =
                        call_method<py::tuple>(socket->py_socket_,
                                               "recvfrom_into", py_buf);
                bytes_received = py::cast<int>(num_received_and_peer[0]);
            } else {
                bytes_received = call_method<int>(socket->py_socket_,
                                                  "recv_into", py_buf);
            }

            if (timeout_finite) {
                auto elapsed_ms = duration_cast<milliseconds>(
                                          steady_clock::now() - before_recv)
                                          .count();
                if (elapsed_ms > timeout_ms) {
                    timeout_ms = 0;
                } else {
                    timeout_ms -= elapsed_ms;
                }
            }

            if (socket_type == SOCK_DGRAM) {
                // Unfortunately directly comparing two py::tuples yields false,
                // if they're not the same objects.
                auto peer_host_port_tuple =
                        py::cast<py::tuple>(num_received_and_peer[1]);
                auto recv_host_port =
                        host_port_to_std_tuple(peer_host_port_tuple);

                if (socket->client_host_and_port_ != recv_host_port) {
                    if (!socket->in_handshake_
                            && socket->context_->connection_id().size()) {
                        // The message may still originate from an endpoint that
                        // we know, but we cannot verify it at this stage,
                        // because no TLS record parsing has been made. We need
                        // to delay it till mbedtls_ssl_read() finishes.
                        socket->last_recv_host_and_port_ = recv_host_port;
                    } else {
                        // ignore this message.
                        continue;
                    }
                }

                // Ensure that we're still connected to the known (host, port).
                // We may not be, if someone "disconnected" the socket to test
                // connection_id behavior.
                call_method<void>(socket->py_socket_, "connect",
                                  socket->client_host_and_port_);
            }
            break;
        } catch (py::error_already_set &err) {
            bytes_received =
                    process_python_socket_error(err,
                                                MBEDTLS_ERR_NET_RECV_FAILED);

            // when in handshake, Python exceptions are not rethrown
            if (!socket->in_handshake_) {
                if (!socket->exception_capturer_) {
                    terminate();
                }
                *socket->exception_capturer_ = current_exception();
            }
            break;
        }
    } while (timeout_ms > 0);

    return bytes_received;
}

Socket::HandshakeResult Socket::do_handshake() {
    in_handshake_ = true;
    const auto clear_in_handshake =
            helpers::defer([&] { in_handshake_ = false; });

    for (;;) {
        int result = mbedtls_ssl_handshake(&mbedtls_context_);
        if (result == 0) {
            break;
        } else if (result == MBEDTLS_ERR_SSL_HELLO_VERIFY_REQUIRED) {
            // mbedtls is unable to continue in such case; one needs to
            // reset the SSL context and try again
            return HandshakeResult::HelloVerifyRequired;
        } else if (result != MBEDTLS_ERR_SSL_WANT_READ
                   && result != MBEDTLS_ERR_SSL_WANT_WRITE) {
            throw mbedtls_error("mbedtls_ssl_handshake failed", result);
        }
    }

    return HandshakeResult::Finished;
}

namespace {

void debug_mbedtls(void * /*ctx*/,
                   int /*level*/,
                   const char *file,
                   int line,
                   const char *str) noexcept {
    fprintf(stderr, "%s:%04d: %s", file, line, str);
}

} // namespace

Socket::Socket(shared_ptr<Context> context,
               py::object py_socket,
               SocketType type)
        : context_(context),
          type_(type),
          py_socket_(py_socket),
          in_handshake_(false),
          exception_capturer_(nullptr),
          client_host_and_port_(),
          last_recv_host_and_port_() {
    mbedtls_ssl_init(&mbedtls_context_);
    // Zeroize cookie context. This prevents issue
    // https://github.com/ARMmbed/mbedtls/issues/843.
    memset(&cookie_, 0, sizeof(cookie_));
    mbedtls_ssl_cookie_init(&cookie_);
    mbedtls_ssl_config_init(&config_);
    mbedtls_entropy_init(&entropy_);
    mbedtls_ctr_drbg_init(&rng_);

    int result = mbedtls_ctr_drbg_seed(&rng_, mbedtls_entropy_func, &entropy_,
                                       NULL, 0);
    if (result) {
        throw mbedtls_error("mbedtls_ctr_drbg_seed failed", result);
    }

    int socket_type = get_socket_type(py_socket_);
    result = mbedtls_ssl_config_defaults(
            &config_,
            type == SocketType::Client ? MBEDTLS_SSL_IS_CLIENT
                                       : MBEDTLS_SSL_IS_SERVER,
            socket_type == SOCK_DGRAM ? MBEDTLS_SSL_TRANSPORT_DATAGRAM
                                      : MBEDTLS_SSL_TRANSPORT_STREAM,
            MBEDTLS_SSL_PRESET_DEFAULT);
    if (result) {
        throw mbedtls_error("mbedtls_ssl_config_defaults failed", result);
    }

    if (context_->debug()) {
        mbedtls_ssl_conf_dbg(&config_, debug_mbedtls, NULL);
    }

    // Force (D)TLS 1.2 or higher
    mbedtls_ssl_conf_min_version(&config_, MBEDTLS_SSL_MAJOR_VERSION_3,
                                 MBEDTLS_SSL_MINOR_VERSION_3);
    mbedtls_ssl_conf_rng(&config_, mbedtls_ctr_drbg_random, &rng_);

#if defined(MBEDTLS_SSL_DTLS_CONNECTION_ID)
    if (context->connection_id().size() > 0
            && (result = mbedtls_ssl_conf_cid(
                        &config_, context->connection_id().size(),
                        MBEDTLS_SSL_UNEXPECTED_CID_IGNORE))) {
        throw mbedtls_error("mbedtls_ssl_conf_cid failed", result);
    }
#endif // MBEDTLS_SSL_DTLS_CONNECTION_ID

    context_->security()->configure(*this);

    if ((result = mbedtls_ssl_cookie_setup(&cookie_, mbedtls_ctr_drbg_random,
                                           &rng_))) {
        throw mbedtls_error("mbedtls_ssl_cookie_setup failed", result);
    }

    mbedtls_ssl_conf_dtls_cookies(&config_,
                                  mbedtls_ssl_cookie_write,
                                  mbedtls_ssl_cookie_check,
                                  &cookie_);

    mbedtls_ssl_conf_session_cache(&config_, context_->session_cache(),
                                   mbedtls_ssl_cache_get,
                                   mbedtls_ssl_cache_set);
    mbedtls_ssl_set_bio(&mbedtls_context_, this, &Socket::bio_send, NULL,
                        &Socket::bio_recv);
    mbedtls_ssl_set_timer_cb(&mbedtls_context_, &timer_,
                             mbedtls_timing_set_delay,
                             mbedtls_timing_get_delay);

    if ((result = mbedtls_ssl_setup(&mbedtls_context_, &config_))) {
        throw mbedtls_error("mbedtls_ssl_setup failed", result);
    }
#if defined(MBEDTLS_SSL_DTLS_CONNECTION_ID)
    if (context->connection_id().size() > 0
            && (result = mbedtls_ssl_set_cid(
                        &mbedtls_context_,
                        MBEDTLS_SSL_CID_ENABLED,
                        reinterpret_cast<const unsigned char *>(
                                context->connection_id().data()),
                        context->connection_id().size()))) {
        throw mbedtls_error("mbedtls_ssl_set_cid failed", result);
    }
#endif // MBEDTLS_SSL_DTLS_CONNECTION_ID
}

Socket::~Socket() {
    mbedtls_entropy_free(&entropy_);
    mbedtls_ssl_config_free(&config_);
    mbedtls_ssl_cookie_free(&cookie_);
    mbedtls_ssl_free(&mbedtls_context_);
}

void Socket::perform_handshake(py::tuple host_port,
                               py::object handshake_timeouts_s_,
                               bool py_connect) {
    if (py_connect) {
        call_method<void>(py_socket_, "connect", host_port);
    }

    last_recv_host_and_port_ = client_host_and_port_ = host_port_to_std_tuple(
            call_method<py::tuple>(py_socket_, "getpeername"));

    if (!handshake_timeouts_s_.is_none()) {
        auto handshake_timeouts_s = py::cast<py::tuple>(handshake_timeouts_s_);
        auto min = py::cast<double>(handshake_timeouts_s[0]);
        auto max = py::cast<double>(handshake_timeouts_s[1]);
        mbedtls_ssl_conf_handshake_timeout(&config_, uint32_t(min * 1000.0),
                                           uint32_t(max * 1000.0));
    }

    HandshakeResult hs_result;

    do {
        int result = mbedtls_ssl_session_reset(&mbedtls_context_);
        if (result) {
            throw mbedtls_error("mbedtls_ssl_sssion_reset failed", result);
        }
        string address = get<0>(client_host_and_port_);
        if (type_ == SocketType::Client) {
            result = mbedtls_ssl_set_hostname(&mbedtls_context_,
                                              address.c_str());
            if (result) {
                throw mbedtls_error("mbedtls_ssl_set_hostname failed", result);
            }
        } else {
            result = mbedtls_ssl_set_client_transport_id(
                    &mbedtls_context_,
                    reinterpret_cast<const unsigned char *>(address.c_str()),
                    address.length());
            if (result) {
                throw mbedtls_error(
                        "mbedtls_ssl_set_client_transport_id failed", result);
            }
        }
        hs_result = do_handshake();
    } while (hs_result == HandshakeResult::HelloVerifyRequired);
}

void Socket::send(const string &data) {
    size_t total_sent = 0;

    while (total_sent < data.size()) {
        int sent = mbedtls_ssl_write(&mbedtls_context_,
                                     reinterpret_cast<const unsigned char *>(
                                             &data[total_sent]),
                                     data.size() - total_sent);
        if (sent < 0) {
            if (sent == MBEDTLS_ERR_SSL_WANT_READ
                    || sent == MBEDTLS_ERR_SSL_WANT_WRITE) {
                continue;
            } else {
                throw mbedtls_error("mbedtls_ssl_write failed", sent);
            }
        }

        total_sent += (size_t) sent;
    }
}

py::bytes Socket::recv(int) {
    unsigned char buffer[65536];
    int result = 0;

    exception_ptr captured_exception;
    exception_capturer_ = &captured_exception;
    const auto clear_capturer =
            helpers::defer([&] { exception_capturer_ = nullptr; });

    do {
        result = mbedtls_ssl_read(&mbedtls_context_, buffer, sizeof(buffer));
    } while (result == MBEDTLS_ERR_SSL_WANT_READ
             || result == MBEDTLS_ERR_SSL_WANT_WRITE);

    if (result < 0) {
        if (result == MBEDTLS_ERR_SSL_TIMEOUT
                || result == MBEDTLS_ERR_NET_RECV_FAILED) {
            if (captured_exception) {
                rethrow_exception(captured_exception);
            }
            throw runtime_error("Expected a Python exception to rethrow");
        } else if (result == MBEDTLS_ERR_SSL_CLIENT_RECONNECT) {
            try {
                do_handshake();
            } catch (mbedtls_error &) {
                // ignore handshake errors, if any, to make sure that the
                // read error is the one that's actually thrown
            }
        }
        throw mbedtls_error("mbedtls_ssl_read failed", result);
    }
    if (captured_exception) {
        throw runtime_error("Expected no Python exception to rethrow");
    }

    if (last_recv_host_and_port_ != client_host_and_port_) {
        // During Socket::_recv(), there had to be a message from a (host, port)
        // we weren't sure about, but enabled connection_id verified it is the
        // same client but from the different address. Let's adjust.
        client_host_and_port_ = last_recv_host_and_port_;
        call_method<void>(py_socket_, "connect", client_host_and_port_);
    }

    return py::bytes(reinterpret_cast<const char *>(buffer), result);
}

py::bytes Socket::peer_cert() {
    const mbedtls_x509_crt *cert = mbedtls_ssl_get_peer_cert(&mbedtls_context_);
    if (cert) {
#if MBEDTLS_VERSION_NUMBER >= 0x03000000 && MBEDTLS_VERSION_NUMBER < 0x03010000
        return py::bytes(reinterpret_cast<const char *>(
                                 cert->MBEDTLS_PRIVATE(raw).MBEDTLS_PRIVATE(p)),
                         cert->MBEDTLS_PRIVATE(raw).MBEDTLS_PRIVATE(len));
#else  // MBEDTLS_VERSION_NUMBER >= 0x03000000
       // && MBEDTLS_VERSION_NUMBER < 0x03010000
        return py::bytes(reinterpret_cast<const char *>(cert->raw.p),
                         cert->raw.len);
#endif // MBEDTLS_VERSION_NUMBER >= 0x03000000
       // && MBEDTLS_VERSION_NUMBER < 0x03010000
    }
    return py::bytes();
}

py::object Socket::__getattr__(py::object name) {
    if (py::cast<string>(name) == "py_socket") {
        return py_socket_;
    } else {
        return call_method<py::object>(py_socket_, "__getattribute__", name);
    }
}

void Socket::__setattr__(py::object name, py::object value) {
    if (py::cast<string>(name) == "py_socket") {
        py_socket_ = value;
    } else {
        call_method<py::object>(py_socket_, "__setattribute__", name, value);
    }
}

namespace {

void enable_reuse(const py::object &socket) {
    // Socket binding reuse on *nixes is crazy.
    // See http://stackoverflow.com/a/14388707 for details.
    //
    // In short:
    //
    // On *BSD and macOS, we need both SO_REUSEADDR and SO_REUSEPORT, so
    // that we can bind multiple sockets to exactly the same address and
    // port (before calling connect(), which will resolve the ambiguity).
    //
    // On Linux, SO_REUSEADDR alone already has those semantics for UDP
    // sockets. Linux also has SO_REUSEPORT, but for UDP sockets, it has
    // very special meaning that enables round-robin load-balancing between
    // sockets bound to the same address and port, and we don't want that.
    //
    // Some more exotic systems (Windows, Solaris) do not have SO_REUSEPORT
    // at all, so we can always just set SO_REUSEADDR and see what happens.
    // It may or may not work, but at least it'll compile ;)
#ifdef SO_REUSEADDR
    call_method<void>(socket, "setsockopt", SOL_SOCKET, SO_REUSEADDR, 1);
#endif
#if !defined(__linux__) && defined(SO_REUSEPORT)
    call_method<void>(socket, "setsockopt", SOL_SOCKET, SO_REUSEPORT, 1);
#endif
}

} // namespace

ServerSocket::ServerSocket(shared_ptr<Context> context, py::object py_socket)
        : context_(context), py_socket_(py_socket) {
    enable_reuse(py_socket_);
}

unique_ptr<Socket> ServerSocket::accept(py::object handshake_timeouts_s) {
    int socket_type = get_socket_type(py_socket_);
    py::object client_py_sock;
    py::tuple remote_addr;

    if (socket_type == SOCK_DGRAM) {
        // use old socket to communicate with client
        // create a new one for listening
        py::object bound_addr =
                call_method<py::object>(py_socket_, "getsockname");
        py::tuple data__remote_addr =
                call_method<py::tuple>(py_socket_, "recvfrom", 1,
                                       (int) MSG_PEEK);
        remote_addr = py::cast<py::tuple>(data__remote_addr[1]);

        client_py_sock = py::eval("socket.socket")(py_socket_.attr("family"),
                                                   socket_type,
                                                   py_socket_.attr("proto"));
        enable_reuse(client_py_sock);

        call_method<void>(client_py_sock, "bind", bound_addr);

        // we have called recvfrom() on py_socket_ and we now want that data
        // to show up on the client_socket - so let's swap them
        swap(py_socket_, client_py_sock);

        call_method<void>(client_py_sock, "connect", remote_addr);
    } else {
        // TCP
        py::tuple client_py_sock__remote_addr =
                call_method<py::tuple>(py_socket_, "accept");
        client_py_sock = client_py_sock__remote_addr[0];
        remote_addr = py::cast<py::tuple>(client_py_sock__remote_addr[1]);

        enable_reuse(client_py_sock);
    }

    unique_ptr<Socket> client_sock =
            make_unique<Socket>(context_, move(client_py_sock),
                                SocketType::Server);
    client_sock->perform_handshake(remote_addr, handshake_timeouts_s, false);
    return client_sock;
}

py::object ServerSocket::__getattr__(py::object name) {
    if (py::cast<string>(name) == "py_socket") {
        return py_socket_;
    } else {
        return call_method<py::object>(py_socket_, "__getattribute__", name);
    }
}

void ServerSocket::__setattr__(py::object name, py::object value) {
    if (py::cast<string>(name) == "py_socket") {
        py_socket_ = value;
    } else {
        call_method<py::object>(py_socket_, "__setattribute__", name, value);
    }
}

} // namespace ssl
