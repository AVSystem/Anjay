/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef PYMBEDTLS_COMMON_HPP
#define PYMBEDTLS_COMMON_HPP
#include <stdexcept>
#include <string>

namespace ssl {

std::string mbedtls_error_string(int error_code);

class mbedtls_error : public std::runtime_error {
public:
    mbedtls_error(const std::string &message, int error_code)
            : std::runtime_error(message + ": "
                                 + mbedtls_error_string(error_code)) {}
};

} // namespace ssl

#endif // PYMBEDTLS_COMMON_HPP
