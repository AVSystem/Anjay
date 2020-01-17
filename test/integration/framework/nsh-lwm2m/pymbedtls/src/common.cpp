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

#include <mbedtls/error.h>

#include <sstream>

#include "common.hpp"

using namespace std;

namespace ssl {
namespace detail {
string to_hex(int n) {
    if (n < 0) {
        return "-" + to_hex(-n);
    } else {
        stringstream ss;
        ss << "0x" << hex << n;
        return ss.str();
    }
}

} // namespace detail

string mbedtls_error_string(int error_code) {
    char buf[1024];
    mbedtls_strerror(error_code, buf, sizeof(buf));
    return string(buf) + " (" + detail::to_hex(error_code) + ")";
}

} // namespace ssl
