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

} // namespace

namespace ssl {

int Socket::_send(void *self, const unsigned char *buf, size_t len) try {
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
std::tuple<string, int> host_port_to_std_tuple(py::tuple host_port) {
    return std::make_tuple(py::cast<std::string>(host_port[0]),
                           py::cast<int>(host_port[1]));
}
} // namespace

int Socket::_recv(void *self,
                  unsigned char *buf,
                  size_t len,
                  uint32_t timeout_ms) {
    Socket *socket = reinterpret_cast<Socket *>(self);

    py::object py_buf = py::reinterpret_borrow<py::object>(
            PyMemoryView_FromMemory((char *) buf, len, PyBUF_WRITE));

    class TimeoutRestorer {
        Socket *socket_;
        py::object orig_timeout_s_;
        bool restored_;

    public:
        TimeoutRestorer(Socket *socket)
                : socket_(socket),
                  orig_timeout_s_(call_method<py::object>(socket_->py_socket_,
                                                          "gettimeout")),
                  restored_(false) {}

        void restore() {
            if (!restored_) {
                call_method<void>(socket_->py_socket_, "settimeout",
                                  orig_timeout_s_);
                restored_ = true;
            }
        }

        ~TimeoutRestorer() {
            restore();
        }
    } timeout_restorer{ socket };

    if (timeout_ms == 0) {
        timeout_ms = UINT32_MAX;
    }

    int bytes_received = 0;
    do {
        try {
            if (timeout_ms == UINT32_MAX) {
                call_method<void>(socket->py_socket_, "settimeout", py::none());
            } else {
                call_method<void>(socket->py_socket_, "settimeout",
                                  timeout_ms / 1000.0);
            }

            const auto before_recv = steady_clock::now();
            py::tuple num_received_and_peer =
                    call_method<py::tuple>(socket->py_socket_, "recvfrom_into",
                                           py_buf);
            if (timeout_ms != UINT32_MAX) {
                auto elapsed_ms = duration_cast<milliseconds>(
                                          steady_clock::now() - before_recv)
                                          .count();
                if (elapsed_ms > timeout_ms) {
                    timeout_ms = 0;
                } else {
                    timeout_ms -= elapsed_ms;
                }
            }

            bytes_received = py::cast<int>(num_received_and_peer[0]);

            // Unfortunately directly comparing two py::tuples yields false,
            // if they're not the same objects.
            auto peer_host_port_tuple =
                    py::cast<py::tuple>(num_received_and_peer[1]);
            auto recv_host_port = host_port_to_std_tuple(peer_host_port_tuple);

            if (socket->client_host_and_port_ != recv_host_port) {
                if (!socket->in_handshake_
                        && socket->context_->connection_id().size()) {
                    // The message may still originate from an endpoint that we
                    // know, but we cannot verify it at this stage, because no
                    // TLS record parsing has been made. We need to delay it
                    // till mbedtls_ssl_read() finishes.
                    socket->last_recv_host_and_port_ = recv_host_port;
                } else {
                    // ignore this message.
                    continue;
                }
            }

            // Ensure that we're still connected to the known (host, port). We
            // may not be, if someone "disconnected" the socket to test
            // connection_id behavior.
            call_method<void>(socket->py_socket_, "connect",
                              socket->client_host_and_port_);
            break;
        } catch (py::error_already_set &err) {
            bytes_received =
                    process_python_socket_error(err,
                                                MBEDTLS_ERR_NET_RECV_FAILED);
            if (!socket->in_handshake_) {
                // HACK: it's there, explicitly called, because for some reason
                // you can't call settimeout() when the "error is restored", and
                // very weird things happen if you try to do it.
                timeout_restorer.restore();
                err.restore();
            }
            break;
        }
    } while (timeout_ms > 0);

    return bytes_received;
}

Socket::HandshakeResult Socket::do_handshake() {
    class HandshakeRaii {
        Socket &self_;

    public:
        HandshakeRaii(Socket &self) : self_(self) {
            self_.in_handshake_ = true;
        }

        ~HandshakeRaii() {
            self_.in_handshake_ = false;
        }
    } handshake_raii_(*this);

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
                   const char *str) {
    fprintf(stderr, "%s:%04d: %s", file, line, str);
}

} // namespace

Socket::Socket(std::shared_ptr<Context> context,
               py::object py_socket,
               SocketType type)
        : context_(context),
          type_(type),
          py_socket_(py_socket),
          in_handshake_(false),
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

    result = mbedtls_ssl_config_defaults(&config_,
                                         type == SocketType::Client
                                                 ? MBEDTLS_SSL_IS_CLIENT
                                                 : MBEDTLS_SSL_IS_SERVER,
                                         MBEDTLS_SSL_TRANSPORT_DATAGRAM, // TODO
                                         MBEDTLS_SSL_PRESET_DEFAULT);
    if (result) {
        throw mbedtls_error("mbedtls_ssl_config_defaults failed", result);
    }

    if (context_->debug()) {
        mbedtls_ssl_conf_dbg(&config_, debug_mbedtls, NULL);
    }

    // TODO
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
    mbedtls_ssl_set_bio(&mbedtls_context_, this, &Socket::_send, NULL,
                        &Socket::_recv);
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

void Socket::connect(py::tuple host_port, py::object handshake_timeouts_s_) {
    client_host_and_port_ = host_port_to_std_tuple(host_port);
    last_recv_host_and_port_ = host_port_to_std_tuple(host_port);

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
        string address = std::get<0>(client_host_and_port_);
        result = mbedtls_ssl_set_client_transport_id(
                &mbedtls_context_,
                reinterpret_cast<const unsigned char *>(address.c_str()),
                address.length());
        if (result) {
            throw mbedtls_error("mbedtls_ssl_set_client_transport_id failed",
                                result);
        }
        call_method<void>(py_socket_, "connect", client_host_and_port_);
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
    do {
        result = mbedtls_ssl_read(&mbedtls_context_, buffer, sizeof(buffer));
    } while (result == MBEDTLS_ERR_SSL_WANT_READ
             || result == MBEDTLS_ERR_SSL_WANT_WRITE);

    if (result < 0) {
        if (result == MBEDTLS_ERR_SSL_TIMEOUT
                || result == MBEDTLS_ERR_NET_RECV_FAILED) {
            throw py::error_already_set();
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

    if (last_recv_host_and_port_ != client_host_and_port_) {
        // During Socket::_recv(), there had to be a message from a (host, port)
        // we weren't sure about, but enabled connection_id verified it is the
        // same client but from the different address. Let's adjust.
        client_host_and_port_ = last_recv_host_and_port_;
        call_method<void>(py_socket_, "connect", client_host_and_port_);
    }

    return py::bytes(reinterpret_cast<const char *>(buffer), result);
}

void Socket::settimeout(py::object timeout_s_or_none) {
    uint32_t timeout_ms = 0; // no timeout

    if (!timeout_s_or_none.is(py::none())) {
        timeout_ms = (uint32_t) (py::cast<double>(timeout_s_or_none) * 1000.0);
    }

    mbedtls_ssl_conf_read_timeout(&config_, timeout_ms);
}

py::bytes Socket::peer_cert() {
    const mbedtls_x509_crt *cert = mbedtls_ssl_get_peer_cert(&mbedtls_context_);
    if (cert) {
        return py::bytes(reinterpret_cast<const char *>(cert->raw.p),
                         cert->raw.len);
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

ServerSocket::ServerSocket(std::shared_ptr<Context> context,
                           py::object py_socket)
        : context_(context), py_socket_(py_socket) {
    enable_reuse(py_socket_);
}

unique_ptr<Socket> ServerSocket::accept(py::object handshake_timeouts_s) {
    // use old socket to communicate with client
    // create a new one for listening
    py::object bound_addr = call_method<py::object>(py_socket_, "getsockname");
    py::tuple data__remote_addr =
            call_method<py::tuple>(py_socket_, "recvfrom", 1, (int) MSG_PEEK);
    py::tuple remote_addr = py::cast<py::tuple>(data__remote_addr[1]);

    py::object client_py_sock = py::eval(
            "socket.socket")(py_socket_.attr("family"), py_socket_.attr("type"),
                             py_socket_.attr("proto"));
    enable_reuse(client_py_sock);

    call_method<void>(client_py_sock, "bind", bound_addr);

    // we have called recvfrom() on py_socket_ and we now want that data
    // to show up on the client_socket - so let's swap them
    swap(py_socket_, client_py_sock);

    call_method<void>(client_py_sock, "connect", remote_addr);

    // Unfortunately C++11 is retarded and does not implement make_unique.
    unique_ptr<Socket> client_sock =
            unique_ptr<Socket>(new Socket(context_, std::move(client_py_sock),
                                          SocketType::Server));
    client_sock->connect(remote_addr, handshake_timeouts_s);
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
