/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef PYMBEDTLS_PYBIND11_INTEROP
#define PYMBEDTLS_PYBIND11_INTEROP
#include <pybind11/eval.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <type_traits>

namespace py = pybind11;

template <typename Result, typename... Args>
typename std::enable_if<!std::is_void<Result>::value, Result>::type
call_method(py::object py_object, const char *name, Args &&... args) {
    auto f = py_object.attr(name);
    auto result = f(std::forward<Args>(args)...);
    return py::cast<Result>(result);
}

template <typename Result, typename... Args>
typename std::enable_if<std::is_void<Result>::value>::type
call_method(py::object py_object, const char *name, Args &&... args) {
    auto f = py_object.attr(name);
    (void) f(std::forward<Args>(args)...);
}

#endif // PYMBEDTLS_PYBIND11_INTEROP
