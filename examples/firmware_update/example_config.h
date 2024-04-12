#ifndef EXAMPLE_CONFIG_H
#define EXAMPLE_CONFIG_H

// disable to run without QUEUE mode
#define EXAMPLE_WITH_QUEUE_MODE

// disable to run with NO-SEC mode
#define EXAMPLE_WITH_DTLS_PSK

#ifdef EXAMPLE_WITH_DTLS_PSK
#    define EXAMPLE_SUPPORTED_CIPHERSUITE \
        MBEDTLS_TLS_PSK_WITH_AES_128_CBC_SHA256
#endif // EXAMPLE_WITH_DTLS_PSK

#define EXAMPLE_INCOMING_MSG_BUFFER_SIZE 1200
#define EXAMPLE_OUTGOING_MSG_BUFFER_SIZE 700
// The size of the payload buffer must be a multiple of 2.
#define EXAMPLE_PAYLOAD_BUFFER_SIZE 512

// security, server, device, fu objects
#define EXAMPLE_OBJS_ARRAY_SIZE 4

#define EXAMPLE_REQUEST_ACK_TIMEOUT_MS 2000
#define EXAMPLE_REQUEST_MAX_RETRANSMIT 4
#define EXAMPLE_RECONNECTION_TIME_MS 10000

// location path buffer size for register operation
#define EXAMPLE_REGISTER_PATH_BUFFER_SIZE 30

#endif // EXAMPLE_CONFIG_H
