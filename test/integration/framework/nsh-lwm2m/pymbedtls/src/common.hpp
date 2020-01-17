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
