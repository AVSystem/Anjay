/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
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

#include <boost/python.hpp>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net.h>
#include <mbedtls/ssl.h>
#include <mbedtls/ssl_cookie.h>
#include <mbedtls/timing.h>
#include <mbedtls/debug.h>
#include <mbedtls/error.h>

#include <vector>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include <arpa/inet.h>
#include <sys/socket.h>

using namespace boost::python;
using namespace std;

namespace {

struct bytes_converter {
    static PyObject *convert(const vector<uint8_t> &bytes) {
        return PyBytes_FromStringAndSize((const char*)&bytes[0], bytes.size());
    }

    static PyTypeObject *get_pytype() {
        return &PyBytes_Type;
    }
};

void debug_mbedtls(void * /*ctx*/, int /*level*/, const char *file, int line, const char *str) {
    fprintf(stderr, "%s:%04d: %s", file, line, str);
}

string to_hex(int n) {
    if (n < 0) {
        return "-" + to_hex(-n);
    } else {
        stringstream ss;
        ss << "0x" << hex << n;
        return ss.str();
    }
}

string mbedtls_error_string(int error) {
    char buf[1024];
    mbedtls_strerror(error, buf, sizeof(buf));
    return string(buf) + " (" + to_hex(error) + ")";
}

} // namespace

namespace ssl {

struct Socket {
    enum class Type {
        Client,
        Server
    };

    enum class HandshakeResult {
        Finished,
        HelloVerifyRequired
    };

    mbedtls_ssl_cookie_ctx cookie_ctx;
    mbedtls_ssl_context context;
    mbedtls_ssl_config config;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context rng;
    mbedtls_timing_delay_context timer;

    vector<uint8_t> psk_identity;
    vector<uint8_t> psk_key;
    vector<int> psk_ciphersuites;

    object py_socket;
    Type type;

    static int _send(void *socket_,
                     const unsigned char *buf,
                     size_t len) {
        Socket *socket = (Socket*)socket_;
        object py_buf = object(handle<>(PyMemoryView_FromMemory((char*)buf, len, PyBUF_READ)));

        call_method<void>(socket->py_socket.ptr(), "sendall", py_buf);
        return (int)len;
    }

    static int _recv(void *socket_,
                     unsigned char *buf,
                     size_t len,
                     uint32_t timeout_ms) {
        Socket *socket = (Socket*)socket_;
        object py_buf = object(handle<>(PyMemoryView_FromMemory((char*)buf, len, PyBUF_WRITE)));

        // this may be double or None, so "object" type needs to be used
        object orig_timeout_s = call_method<object>(socket->py_socket.ptr(), "gettimeout");
        // also, timeout == 0 sets a python socket in nonblocking mode
        // None (object()) has to be used instead to set infinite timeout
        object new_timeout = timeout_ms == 0 ? object()
                                             : object((double)timeout_ms / 1000.0);

        call_method<void>(socket->py_socket.ptr(), "settimeout", new_timeout);
        int bytes_received;

        try {
            bytes_received = call_method<int>(socket->py_socket.ptr(), "recv_into", py_buf);
            call_method<void>(socket->py_socket.ptr(), "settimeout", orig_timeout_s);
        } catch (const error_already_set &) {
            // TODO: assume any error is EAGAIN
            bytes_received = MBEDTLS_ERR_SSL_TIMEOUT;

            // temporarily clear the error so that `settimeout()` call can succeed
            PyObject *e, *v, *t;
            PyErr_Fetch(&e, &v, &t);

            call_method<void>(socket->py_socket.ptr(), "settimeout", orig_timeout_s);

            PyErr_Restore(e, v, t);
        }

        return bytes_received;
    }

    HandshakeResult _do_handshake() {
        for (;;) {
            int result = mbedtls_ssl_handshake(&context);
            if (result == 0) {
                break;
            } else if (result == MBEDTLS_ERR_SSL_HELLO_VERIFY_REQUIRED) {
                // mbedtls is unable to continue in such case; one needs to
                // reset the SSL context and try again
                return HandshakeResult::HelloVerifyRequired;
            } else if (result != MBEDTLS_ERR_SSL_WANT_READ
                    && result != MBEDTLS_ERR_SSL_WANT_WRITE) {
                throw runtime_error("mbedtls_ssl_handshake failed: "
                                    + mbedtls_error_string(result));
            }
        }

        return HandshakeResult::Finished;
    }

    Socket(object py_socket_,
           Type type_,
           const string &psk_identity_,
           const string &psk_key_,
           bool debug):
        py_socket(py_socket_),
        type(type_)
    {
        copy(psk_identity_.begin(), psk_identity_.end(),
             back_inserter(psk_identity));
        copy(psk_key_.begin(), psk_key_.end(),
             back_inserter(psk_key));

        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&rng);

        int result = mbedtls_ctr_drbg_seed(&rng, mbedtls_entropy_func, &entropy, NULL, 0);
        if (result) {
            throw runtime_error("mbedtls_ctr_drbg_seed failed: "
                                + mbedtls_error_string(result));
        }

        mbedtls_ssl_config_init(&config);
        result = mbedtls_ssl_config_defaults(
                &config,
                type == Type::Client ? MBEDTLS_SSL_IS_CLIENT : MBEDTLS_SSL_IS_SERVER,
                MBEDTLS_SSL_TRANSPORT_DATAGRAM, // TODO
                MBEDTLS_SSL_PRESET_DEFAULT);
        if (result) {
            throw runtime_error("mbedtls_ssl_config_defaults failed: "
                                + mbedtls_error_string(result));
        }

        if (debug) {
            mbedtls_ssl_conf_dbg(&config, debug_mbedtls, NULL);
        }

        // TODO
        mbedtls_ssl_conf_min_version(&config,
                                     MBEDTLS_SSL_MAJOR_VERSION_3,
                                     MBEDTLS_SSL_MINOR_VERSION_3);

        mbedtls_ssl_conf_rng(&config, mbedtls_ctr_drbg_random, &rng);

        mbedtls_ssl_conf_psk(&config, &psk_key[0], psk_key.size(), &psk_identity[0], psk_identity.size());

        // TODO
        psk_ciphersuites = {MBEDTLS_TLS_PSK_WITH_AES_128_CCM_8, 0};
        mbedtls_ssl_conf_ciphersuites(&config, &psk_ciphersuites[0]);

        // NOTE: workaround for https://github.com/ARMmbed/mbedtls/issues/843
        memset(&cookie_ctx, 0, sizeof(cookie_ctx));
        mbedtls_ssl_cookie_init(&cookie_ctx);
        result = mbedtls_ssl_cookie_setup(&cookie_ctx, mbedtls_ctr_drbg_random, &rng);
        if (result) {
            throw runtime_error("mbedtls_ssl_cookie_setup failed: "
                                + mbedtls_error_string(result));
        }

        mbedtls_ssl_conf_dtls_cookies(&config, mbedtls_ssl_cookie_write,
                                      mbedtls_ssl_cookie_check, &cookie_ctx);

        mbedtls_ssl_init(&context);
        mbedtls_ssl_set_bio(&context,
                            this, &Socket::_send,
                            NULL, &Socket::_recv);
        mbedtls_ssl_set_timer_cb(&context, &timer,
                                 mbedtls_timing_set_delay,
                                 mbedtls_timing_get_delay);

        result = mbedtls_ssl_setup(&context, &config);
        if (result) {
            throw runtime_error("mbedtls_ssl_setup failed: "
                                + mbedtls_error_string(result));
        }
    }

    Socket(object py_socket,
           Type type,
           const string &psk_identity,
           const string &psk_key):
        Socket(py_socket, type, psk_identity, psk_key, false)
    {}

    Socket(object py_socket,
           const string &psk_identity,
           const string &psk_key,
           bool debug):
        Socket(py_socket, Socket::Type::Client, psk_identity, psk_key, debug)
    {}

    Socket(object py_socket,
           const string &psk_identity,
           const string &psk_key):
        Socket(py_socket, Socket::Type::Client, psk_identity, psk_key, false)
    {}

    void connect(object address_port) {
        HandshakeResult hs_result;

        do {
            int result = mbedtls_ssl_session_reset(&context);
            if (result) {
                throw runtime_error("mbedtls_ssl_sssion_reset failed: "
                                    + mbedtls_error_string(result));
            }

            const char *address = extract<const char *>(address_port[0]);
            result = mbedtls_ssl_set_client_transport_id(
                    &context, (const unsigned char *) address, len(address_port[0]));
            if (result) {
                throw runtime_error("mbedtls_ssl_set_client_transport_id failed: "
                                    + mbedtls_error_string(result));
            }

            call_method<void>(py_socket.ptr(), "connect", address_port);
            hs_result = _do_handshake();
        } while (hs_result == HandshakeResult::HelloVerifyRequired);
    }

    void send(const string &data) {
        size_t total_sent = 0;

        while (total_sent < data.size()) {
            int sent = mbedtls_ssl_write(&context,
                                         (const unsigned char*)&data[total_sent],
                                         data.size() - total_sent);
            if (sent < 0) {
                if (sent == MBEDTLS_ERR_SSL_WANT_READ
                        || sent == MBEDTLS_ERR_SSL_WANT_WRITE) {
                    continue;
                } else {
                    throw runtime_error("mbedtls_ssl_write failed: "
                                        + mbedtls_error_string(sent));
                }
            }

            total_sent += (size_t)sent;
        }
    }

    vector<uint8_t> recv(int) {
        unsigned char buffer[65536];

        int result = 0;
        do {
            result = mbedtls_ssl_read(&context, buffer, sizeof(buffer));
        } while (result == MBEDTLS_ERR_SSL_WANT_READ
                 || result == MBEDTLS_ERR_SSL_WANT_WRITE);

        if (result < 0) {
            if (result == MBEDTLS_ERR_SSL_TIMEOUT) {
                throw_error_already_set();
            } else {
                throw runtime_error("mbedtls_ssl_read failed: "
                                    + mbedtls_error_string(result));
            }
        }

        return vector<uint8_t>(buffer, buffer + result);
    }

    void settimeout(object timeout_s_or_none) {
        uint32_t timeout_ms = 0; // no timeout

        if (timeout_s_or_none != object()) {
            timeout_ms = (uint32_t)(extract<double>(timeout_s_or_none) * 1000.0);
        }

        mbedtls_ssl_conf_read_timeout(&config, timeout_ms);
    }

    object __getattr__(object name) {
        return call_method<object>(py_socket.ptr(), "__getattribute__", name);
    }

    template<typename... Args>
    void fail(const Args &...) {
        throw logic_error("method not implemented");
    }
};

class ServerSocket {
    void enable_reuse(const object &socket) {
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
        call_method<void>(socket.ptr(), "setsockopt", SOL_SOCKET, SO_REUSEADDR, 1);
#endif
#if !defined(__linux__) && defined(SO_REUSEPORT)
        call_method<void>(socket.ptr(), "setsockopt", SOL_SOCKET, SO_REUSEPORT, 1);
#endif
    }

public:
    object py_socket;

    string psk_identity;
    string psk_key;

    bool debug;

    ServerSocket(object py_socket_,
                 const string &psk_identity_,
                 const string &psk_key_,
                 bool debug_):
        py_socket(py_socket_),
        psk_identity(psk_identity_),
        psk_key(psk_key_),
        debug(debug_)
    {
        enable_reuse(py_socket);
    }

    Socket *accept() {
        // use old socket to communicate with client
        // create a new one for listening
        object bound_addr = call_method<object>(py_socket.ptr(), "getsockname");
        object data__remote_addr = call_method<object>(py_socket.ptr(), "recvfrom", 1, (int)MSG_PEEK);
        object remote_addr = extract<object>(data__remote_addr[1]);

        object client_py_sock = eval("socket.socket(socket.AF_INET, socket.SOCK_DGRAM)");
        enable_reuse(client_py_sock);
        call_method<void>(client_py_sock.ptr(), "bind", bound_addr);

        swap(py_socket, client_py_sock);

        call_method<void>(client_py_sock.ptr(), "connect", remote_addr);

        Socket *client_sock = new Socket(client_py_sock, Socket::Type::Server,
                                         psk_identity, psk_key, debug);
        client_sock->connect(remote_addr);
        return client_sock;
    }

    ServerSocket(object py_socket,
                 const string &psk_identity,
                 const string &psk_key):
        ServerSocket(py_socket, psk_identity, psk_key, false)
    {}

    object __getattr__(object name) {
        return call_method<object>(py_socket.ptr(), "__getattribute__", name);
    }
};

} // namespace ssl

BOOST_PYTHON_MODULE(pymbedtls) {
    using namespace ssl;

    to_python_converter<vector<uint8_t>, bytes_converter, true>();

    class_<ServerSocket>("ServerSocket", init<object, const string &, const string &, bool>())
        .def(init<object, const string &, const string &>())
        .def("accept", &ServerSocket::accept, return_value_policy<manage_new_object>())
        .def("__getattr__", &ServerSocket::__getattr__);
    ;

    scope socket_scope =
        class_<Socket>("Socket", init<object, Socket::Type, const string &, const string &, bool>())
            .def(init<object, Socket::Type, const string &, const string &>())
            .def(init<object, const string &, const string &, bool>())
            .def(init<object, const string &, const string &>())
            .def("connect", &Socket::connect)
            .def("send", &Socket::send)
            .def("sendall", &Socket::send)
            .def("sendto", &Socket::fail<string, object>)
            .def("recv", &Socket::recv)
            .def("recv_into", &Socket::fail<object>)
            .def("recvfrom", &Socket::fail<int>)
            .def("recvfrom_into", &Socket::fail<object>)
            .def("settimeout", &Socket::settimeout)
            .def("__getattr__", &Socket::__getattr__)
    ;

    enum_<Socket::Type>("Type")
        .value("Client", Socket::Type::Client)
        .value("Server", Socket::Type::Server)
        .export_values()
    ;

    // most verbose logs available
    mbedtls_debug_set_threshold(4);
}
