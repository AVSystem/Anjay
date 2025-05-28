/*
 * Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
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

namespace helpers {

template <typename Callable>
class defer_obj {
private:
    Callable deferred;

public:
    defer_obj(defer_obj &&other) noexcept
            : deferred(std::move(other.deferred)) {}
    defer_obj(const defer_obj &) = delete;
    defer_obj &operator=(const defer_obj &) = delete;
    defer_obj &operator=(defer_obj &&) = delete;

    template <typename TempCallable>
    defer_obj(TempCallable &&deferred)
            : deferred(std::forward<TempCallable>(deferred)) {}
    ~defer_obj() {
        deferred();
    }
};

// This helper ensures that some code will be called on destruction, i.e. exit
// from the scope, no matter if it's a return, an exception or a normal exit.
template <typename Callable>
defer_obj<Callable> defer(Callable &&to_defer) {
    return { std::forward<Callable>(to_defer) };
}

}; // namespace helpers

#endif // PYMBEDTLS_COMMON_HPP
