if(NOT OPENSSL_SSL_LIBRARY)
    message(FATAL_ERROR "OpenSSL libraries are required for unit tests")
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -w")
add_executable(dtls_echo_server EXCLUDE_FROM_ALL dtls_echo.c)
target_link_libraries(dtls_echo_server ssl crypto)
