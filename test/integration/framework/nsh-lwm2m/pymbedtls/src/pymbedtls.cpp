#include <boost/python.hpp>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net.h>
#include <mbedtls/ssl.h>
#include <mbedtls/ssl_cookie.h>
#include <mbedtls/timing.h>
#include <mbedtls/debug.h>

#include <vector>
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

#ifdef WITH_MBEDTLS_DEBUG
void debug_mbedtls(void * /*ctx*/, int /*level*/, const char *file, int line, const char *str) {
    fprintf(stderr, "%s:%04d: %s", file, line, str);
}
#endif

} // namespace

namespace ssl {

struct Socket {
    enum class Type {
        Client,
        Server
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

    void _do_handshake() {
        for (;;) {
            int result = mbedtls_ssl_handshake(&context);
            if (result == 0) {
                break;
            } else if (result != MBEDTLS_ERR_SSL_WANT_READ
                    && result != MBEDTLS_ERR_SSL_WANT_WRITE) {
                throw runtime_error("mbedtls_ssl_handshake failed");
            }
        }
    }

    Socket(object py_socket_,
           Type type_,
           const string &psk_identity_,
           const string &psk_key_):
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
            throw runtime_error("mbedtls_ctr_drbg_seed failed");
        }

        mbedtls_ssl_config_init(&config);
        result = mbedtls_ssl_config_defaults(
                &config,
                type == Type::Client ? MBEDTLS_SSL_IS_CLIENT : MBEDTLS_SSL_IS_SERVER,
                MBEDTLS_SSL_TRANSPORT_DATAGRAM, // TODO
                MBEDTLS_SSL_PRESET_DEFAULT);
        if (result) {
            throw runtime_error("mbedtls_ssl_config_defaults failed");
        }

#ifdef WITH_MBEDTLS_DEBUG
        mbedtls_ssl_conf_dbg(&config, debug_mbedtls, NULL);
        mbedtls_debug_set_threshold(9999);
#endif

        // TODO
        mbedtls_ssl_conf_min_version(&config,
                                     MBEDTLS_SSL_MAJOR_VERSION_3,
                                     MBEDTLS_SSL_MINOR_VERSION_3);

        mbedtls_ssl_conf_rng(&config, mbedtls_ctr_drbg_random, &rng);

        mbedtls_ssl_conf_psk(&config, &psk_key[0], psk_key.size(), &psk_identity[0], psk_identity.size());

        // TODO
        psk_ciphersuites = {MBEDTLS_TLS_PSK_WITH_AES_128_CCM_8, 0};
        mbedtls_ssl_conf_ciphersuites(&config, &psk_ciphersuites[0]);

        mbedtls_ssl_cookie_init(&cookie_ctx);
        result = mbedtls_ssl_cookie_setup(&cookie_ctx, mbedtls_ctr_drbg_random, &rng);
        if (result) {
            throw runtime_error("mbedtls_ssl_cookie_setup failed");
        }

        mbedtls_ssl_conf_dtls_cookies(&config, NULL, NULL, &cookie_ctx);

        mbedtls_ssl_init(&context);
        mbedtls_ssl_set_bio(&context,
                            this, &Socket::_send,
                            NULL, &Socket::_recv);
        mbedtls_ssl_set_timer_cb(&context, &timer,
                                 mbedtls_timing_set_delay,
                                 mbedtls_timing_get_delay);

        result = mbedtls_ssl_setup(&context, &config);
        if (result) {
            throw runtime_error("mbedtls_ssl_setup failed");
        }
    }

    Socket(object py_socket,
           const string &psk_identity,
           const string &psk_key):
        Socket(py_socket, Socket::Type::Client, psk_identity, psk_key)
    {}

    void connect(object address_port) {
        if (mbedtls_ssl_session_reset(&context)) {
            throw runtime_error("mbedtls_ssl_sssion_reset failed");
        }

        call_method<void>(py_socket.ptr(), "connect", address_port);
        _do_handshake();
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
                    throw runtime_error("mbedtls_ssl_write failed: " + to_string(sent));
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
                throw runtime_error("mbedtls_ssl_read failed: " + to_string(result));
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

struct ServerSocket {
    object py_socket;

    string psk_identity;
    string psk_key;

    ServerSocket(object py_socket_,
                 const string &psk_identity_,
                 const string &psk_key_ ):
        py_socket(py_socket_),
        psk_identity(psk_identity_),
        psk_key(psk_key_)
    {
        call_method<void>(py_socket.ptr(), "setsockopt", SOL_SOCKET, SO_REUSEADDR, 1);
    }

    Socket *accept() {
        // use old socket to communicate with client
        // create a new one for listening
        object bound_addr = call_method<object>(py_socket.ptr(), "getsockname");
        object data__remote_addr = call_method<object>(py_socket.ptr(), "recvfrom", 1, (int)MSG_PEEK);
        object remote_addr = extract<object>(data__remote_addr[1]);

        object client_py_sock = eval("socket.socket(socket.AF_INET, socket.SOCK_DGRAM)");
        call_method<void>(client_py_sock.ptr(), "setsockopt", (int)SOL_SOCKET, (int)SO_REUSEADDR, 1);
        call_method<void>(client_py_sock.ptr(), "bind", bound_addr);

        swap(py_socket, client_py_sock);

        call_method<void>(client_py_sock.ptr(), "connect", remote_addr);

        Socket *client_sock = new Socket(client_py_sock, Socket::Type::Server,
                                         psk_identity, psk_key);
        client_sock->connect(remote_addr);
        return client_sock;
    }

    object __getattr__(object name) {
        return call_method<object>(py_socket.ptr(), "__getattribute__", name);
    }
};

} // namespace ssl

BOOST_PYTHON_MODULE(pymbedtls) {
    using namespace ssl;

    to_python_converter<vector<uint8_t>, bytes_converter, true>();

    class_<ServerSocket>("ServerSocket", init<object, const string &, const string &>())
        .def("accept", &ServerSocket::accept, return_value_policy<manage_new_object>())
        .def("__getattr__", &ServerSocket::__getattr__);
    ;

    scope socket_scope =
        class_<Socket>("Socket", init<object, const string &, const string &>())
            .def("connect", &Socket::connect)
            .def("send", &Socket::send)
            .def("sendall", &Socket::send)
            .def("sendto", &Socket::fail<string, object>)
            .def("recv", &Socket::recv)
            .def("recv_into", &Socket::fail<object>)
            .def("recvfrom", &Socket::fail<int>)
            .def("recvfrom_into", &Socket::fail<object>)
            .def("settimeout", &Socket::settimeout)
            .def("__getattr__", &Socket::__getattr__);
    ;

    enum_<Socket::Type>("Type")
        .value("Client", Socket::Type::Client)
        .value("Server", Socket::Type::Server)
        .export_values()
    ;
}
