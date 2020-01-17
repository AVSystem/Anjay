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
