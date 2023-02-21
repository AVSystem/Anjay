/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
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
