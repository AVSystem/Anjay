..
   Copyright 2017-2020 AVSystem <avsystem@avsystem.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

Migrating mbed TLS custom entropy initializers
==============================================

.. highlight:: c

Previous versions of ``avs_commons`` provided the following mechanism to allow
adding a custom entropy source at the time of mbed TLS initialization::

    // NOTE: Code compatible with Anjay <=2.2, avs_commons <=4.0
    // WITH_MBEDTLS_CUSTOM_ENTROPY_INITIALIZER needs to be enabled at compile time

    #include <avsystem/commons/defs.h>
    #include <mbedtls/entropy.h>
    #include <mbedtls/entropy_poll.h>

    static int entropy_poll(void *user_arg,
                            uint8_t *output,
                            size_t len,
                            size_t *out_len) {
        // TODO: Platform-specific entropy collection code
    }

    // NOTE: For Anjay 1.x / avs_commons 3.x, return type was "int"
    avs_error_t avs_net_mbedtls_entropy_init(mbedtls_entropy_context *entropy) {
        if (mbedtls_entropy_add_source(entropy, entropy_poll, NULL,
                                       MBEDTLS_ENTROPY_MIN_PLATFORM,
                                       MBEDTLS_ENTROPY_SOURCE_STRONG)) {
            return avs_errno(AVS_UNKNOWN_ERROR);
        }
        return AVS_OK;
    }

This mechanism has been removed in ``avs_commons`` 4.1, and as such is not
available when using Anjay 2.3.

To achieve a similar effect in the new version, you can provide your own custom
PRNG context, for example as follows::

    // Code compatible with Anjay >=2.3, avs_commons >=4.0

    #include <anjay/core.h>
    #include <avsystem/commons/avs_prng.h>
    #include <mbedtls/entropy.h>
    #include <mbedtls/entropy_poll.h>

    static mbedtls_entropy_context g_entropy_context;

    static int entropy_poll(void *user_arg,
                            uint8_t *output,
                            size_t len,
                            size_t *out_len) {
        // TODO: Platform-specific entropy collection code
    }

    static int entropy_callback(unsigned char *out_buf,
                                size_t out_buf_len,
                                void *dummy_user_arg) {
        (void) dummy_user_arg;
        return mbedtls_entropy_func(&g_entropy_context, out_buf, out_buf_len);
    }

    int main() {
        // ... before initializing Anjay ...
        avs_crypto_prng_ctx_t *prng_ctx;
        mbedtls_entropy_init(&g_entropy_context);
        if (mbedtls_entropy_add_source(&g_entropy_context, entropy_poll, NULL,
                                       MBEDTLS_ENTROPY_MIN_PLATFORM,
                                       MBEDTLS_ENTROPY_SOURCE_STRONG)
                 || !(prng_ctx = avs_crypto_prng_new(entropy_callback, NULL))) {
            // TODO: Better error handling
            return -1;
        }

        // ... when initializing Anjay ...
        const anjay_configuration_t anjay_config = {
            // TODO: Other configuration options
            .prng_ctx = prng_ctx
        };
        anjay_t *anjay = anjay_new(&anjay_config);

        // ... when cleaning up Anjay ...
        anjay_delete(anjay);
        avs_crypto_prng_free(&prng_ctx);

        // ...
        return 0;
    }

If you're using ``avs_coap`` contexts and/or raw ``avs_net`` sockets in addition
to, or instead of Anjay, you will need to pass such custom ``prng_ctx`` object
when initializing those as well. It is generally safe to have multiple objects
use the same PRNG context object.

.. note::

    If you don't need the custom mbed TLS entropy logic, it is safe to leave the
    ``prng_ctx`` field of ``anjay_configuration_t`` as ``NULL``. If you need a
    PRNG context for other purposes and don't need a custom entropy source, you
    can also initialize it as ``avs_crypto_prng_new(NULL, NULL)``.

    The mechanism described above is only required or intended for cases when a
    non-default entropy source is required.
