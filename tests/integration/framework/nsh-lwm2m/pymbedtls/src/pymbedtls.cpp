/*
 * Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */

#include <exception>
#include <iostream>
#include <string>

#include "context.hpp"
#include "security.hpp"
#include "socket.hpp"

#include "pybind11_interop.hpp"

#include <mbedtls/version.h>

using namespace std;

namespace {

template <typename... Args>
void method_unimplemented(py::object self, const Args &...) {
    throw logic_error("method not implemented");
}

} // namespace

PYBIND11_MODULE(pymbedtls, m) {
    using namespace ssl;

    py::class_<SecurityInfo, shared_ptr<SecurityInfo>>(m, "SecurityInfo")
            .def("name", &SecurityInfo::name)
            .def("set_ciphersuites", &SecurityInfo::set_ciphersuites);

    py::class_<PskSecurity, SecurityInfo, shared_ptr<PskSecurity>>(
            m, "PskSecurity")
            .def(py::init<const string &, const string &>(),
                 py::arg("key"),
                 py::arg("identity"));

    py::class_<CertSecurity, SecurityInfo, shared_ptr<CertSecurity>>(
            m, "CertSecurity")
            .def(py::init<const char *, const char *, const char *,
                          const char *>(),
                 py::arg("ca_path"),
                 py::arg("ca_file"),
                 py::arg("crt_file"),
                 py::arg("key_file"));

    py::class_<Context, shared_ptr<Context>>(m, "Context")
            .def(py::init<shared_ptr<SecurityInfo>, bool, std::string>(),
                 py::arg("security"),
                 py::arg("debug") = false,
                 py::arg("connection_id") = "")
            .def_static("supports_connection_id", []() -> bool {
#if defined(MBEDTLS_SSL_DTLS_CONNECTION_ID)
                return true;
#else
                    return false;
#endif
            });

    py::class_<ServerSocket>(m, "ServerSocket")
            .def(py::init<shared_ptr<Context>, py::object>(),
                 py::arg("context"),
                 py::arg("socket"))
            .def("accept", &ServerSocket::accept,
                 py::arg("handshake_timeouts_s") = py::none())
            .def("__getattr__", &ServerSocket::__getattr__)
            .def("__setattr__", &ServerSocket::__setattr__);

    auto socket_scope =
            py::class_<Socket>(m, "Socket")
                    .def(py::init<shared_ptr<Context>, py::object,
                                  SocketType>(),
                         py::arg("context"),
                         py::arg("socket"),
                         py::arg("socket_type"))
                    .def("connect", &Socket::connect, py::arg("host_port"),
                         py::arg("handshake_timeouts_s") = py::none())
                    .def("send", &Socket::send)
                    .def("sendall", &Socket::send)
                    .def("sendto", &method_unimplemented<string, py::object>)
                    .def("recv", &Socket::recv)
                    .def("recv_into", &method_unimplemented<py::object>)
                    .def("recvfrom", &method_unimplemented<int>)
                    .def("recvfrom_into", &method_unimplemented<py::object>)
                    .def("peer_cert", &Socket::peer_cert)
                    .def("__getattr__", &Socket::__getattr__)
                    .def("__setattr__", &Socket::__setattr__);

    py::enum_<SocketType>(socket_scope, "Type")
            .value("Client", SocketType::Client)
            .value("Server", SocketType::Server)
            .export_values();
    // most verbose logs available
    mbedtls_debug_set_threshold(4);

    set_terminate([]() {
        cerr << "Terminate called in pymbedtls. This almost certainly means "
                "that an exception was thrown in a callback, that indirectly "
                "was called by a callee not expecting an exception that would "
                "require rethrowing. Consider analyzing the core dump in gdb "
                "to determine the C++ stack trace leading to this point."
             << endl;
        exception_ptr eptr = current_exception();
        if (eptr) {
            try {
                rethrow_exception(eptr);
            } catch (const exception &e) {
                cerr << "Uncaught exception with reason: " << e.what() << endl;
            } catch (...) {
                cerr << "Uncaught unknown throwable" << endl;
            }
        } else {
            cerr << "Couldn't determine the throwable that caused the "
                    "terminate call"
                 << endl;
        }
        cerr << flush;
        std::abort();
    });
}
