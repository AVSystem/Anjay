/*
 * Copyright 2017-2021 AVSystem <avsystem@avsystem.com>
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

#ifndef ANJAY_INCLUDE_ANJAY_CORE_H
#define ANJAY_INCLUDE_ANJAY_CORE_H

#include <stdint.h>

#include <avsystem/coap/udp.h>

#include <avsystem/commons/avs_list.h>
#include <avsystem/commons/avs_net.h>
#include <avsystem/commons/avs_prng.h>
#include <avsystem/commons/avs_sched.h>
#include <avsystem/commons/avs_stream.h>
#include <avsystem/commons/avs_time.h>

#include <anjay/anjay_config.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Short Server ID type. */
typedef uint16_t anjay_ssid_t;

/** A constant that may be used in @ref anjay_schedule_registration_update
 * call instead of Short Server ID to send Update messages to all connected
 * servers. */
#define ANJAY_SSID_ANY 0

/** An SSID value reserved by LwM2M to refer to the Bootstrap Server.
 * NOTE: The value of a "Short Server ID" Resource in the Security Object
 * Instance referring to the Bootstrap Server is irrelevant and cannot be used
 * to identify the Bootstrap Server. */
#define ANJAY_SSID_BOOTSTRAP UINT16_MAX

/** Anjay object containing all information required for LwM2M communication. */
typedef struct anjay_struct anjay_t;

/**
 * Default transmission params recommended by the CoAP specification (RFC 7252).
 */
// clang-format off
#define ANJAY_COAP_DEFAULT_UDP_TX_PARAMS \
    {                                    \
        /* .ack_timeout = */ { 2, 0 },   \
        /* .ack_random_factor = */ 1.5,  \
        /* .max_retransmit = */ 4,       \
        /* .nstart = */ 1                \
    }
// clang-format on

/**
 * Default handshake retransmission params recommended by the DTLS specification
 * (RFC 6347), i.e: 1s for the initial response, growing exponentially (with
 * each retransmission) up to maximum of 60s.
 */
// clang-format off
#define ANJAY_DTLS_DEFAULT_UDP_HS_TX_PARAMS \
    {                                       \
        /* .min = */ { 1, 0 },              \
        /* .max = */ { 60, 0 }              \
    }
// clang-format on

typedef struct anjay_configuration {
    /**
     * Endpoint name as presented to the LwM2M server. Must be non-NULL, or
     * otherwise @ref anjay_new() will fail.
     *
     * NOTE: Endpoint name is copied during @ref anjay_new() and cannot be
     * modified later on.
     */
    const char *endpoint_name;

    /**
     * UDP port number that all listening sockets will be bound to. It may be
     * left at 0 - in that case, connection with each server will use a freshly
     * generated ephemeral port number.
     */
    uint16_t udp_listen_port;

    /**
     * DTLS version to use for communication. AVS_NET_SSL_VERSION_DEFAULT will
     * be automatically mapped to AVS_NET_SSL_VERSION_TLSv1_2, which is the
     * version mandated by LwM2M specification.
     */
    avs_net_ssl_version_t dtls_version;

    /**
     * Maximum size of a single incoming CoAP message. Decreasing this value
     * reduces memory usage, but packets bigger than this value will
     * be dropped.
     */
    size_t in_buffer_size;

    /**
     * Maximum size of a single outgoing CoAP message. If the message exceeds
     * this size, the library performs the block-wise CoAP transfer
     * ( https://tools.ietf.org/html/rfc7959 ).
     * NOTE: in case of block-wise transfers, this value limits the payload size
     * for a single block, not the size of a whole packet.
     */
    size_t out_buffer_size;

    /**
     * Number of bytes reserved for caching CoAP responses. If not 0,
     * the library looks up recently generated responses and reuses them
     * to handle retransmitted packets (ones with identical CoAP message ID).
     *
     * NOTE: while a single cache is used for all LwM2M servers, cached
     * responses are tied to a particular server and not reused for other ones.
     */
    size_t msg_cache_size;

    /**
     * Socket configuration to use when creating TCP/UDP sockets.
     *
     * Note that:
     * - <c>reuse_addr</c> will be forced to true.
     * - Value pointed to by the <c>preferred_endpoint</c> will be ignored.
     */
    avs_net_socket_configuration_t socket_config;

    /**
     * Configuration of the CoAP transmission params for UDP connection, as per
     * RFC 7252.
     *
     * If NULL, the default configuration @ref ANJAY_COAP_DEFAULT_UDP_TX_PARAMS
     * will be selected.
     *
     * NOTE: Parameters are copied during @ref anjay_new() and cannot be
     * modified later on.
     */
    const avs_coap_udp_tx_params_t *udp_tx_params;

    /**
     * Configuration of the DTLS handshake retransmission timeouts for UDP
     * connection.
     *
     * If NULL, the default configuration
     * @ref ANJAY_DTLS_DEFAULT_UDP_HS_TX_PARAMS will be selected.
     *
     * NOTE: Parameters are copied during @ref anjay_new() and cannot be
     * modified later on.
     *
     * IMPORTANT: In case of a need to adjust DTLS retransmission params to
     * match the CoAP retransmission params, the @ref udp_dtls_hs_tx_params
     * shall be initialized as `dtls_hs_params` is in the following code
     * snippet:
     * @code
     *  const avs_coap_udp_tx_params_t coap_tx_params = {
     *      // ... some initialization
     *  };
     *
     *  // Without ACK_RANDOM_FACTOR = 1.0, it is impossible to create a DTLS HS
     *  // configuration that matches CoAP retransmission configuration
     *  // perfectly.
     *  assert(coap_tx_params.ack_random_factor == 1.0);
     *
     *  const avs_net_dtls_handshake_timeouts_t dtls_hs_tx_params = {
     *      .min = avs_time_duration_fmul(coap_tx_params.ack_timeout,
     *                                    coap_tx_params.ack_random_factor),
     *      .max = avs_time_duration_fmul(
     *              coap_tx_params.ack_timeout,
     *              (1 << coap_tx_params.max_retransmit)
     *                  * coap_tx_params.ack_random_factor)
     *  };
     * @endcode
     */
    const avs_net_dtls_handshake_timeouts_t *udp_dtls_hs_tx_params;

    /**
     * Controls whether Notify operations are conveyed using Confirmable CoAP
     * messages by default.
     */
    bool confirmable_notifications;

    /**
     * If set to true, connection to the Bootstrap Server will be closed
     * immediately after making a successful connection to any regular LwM2M
     * Server and only opened again if (re)connection to a regular server is
     * rejected.
     *
     * If set to false, legacy Server-Initiated Bootstrap is possible, i.e. the
     * Bootstrap Server can reach the client at any time to re-initiate the
     * bootstrap sequence.
     *
     * NOTE: This parameter controls a legacy Server-Initiated Bootstrap
     * mechanism based on an interpretation of LwM2M 1.0 TS that is not
     * universally accepted. Server-Initiated Bootstrap as specified in LwM2M
     * 1.1 TS is always supported, regardless of this setting.
     */
    bool disable_legacy_server_initiated_bootstrap;

    /**
     * If "Notification Storing When Disabled or Offline" resource is set to
     * true and either the client is in offline mode, or uses Queue Mode,
     * Notify messages are enqueued and sent whenever the client is online
     * again. This value allows one to limit the size of said notification
     * queue. The limit applies to notifications queued for all servers.
     *
     * If set to 0, size of the stored notification queue is only limited by
     * the amount of available RAM.
     *
     * If set to a positive value, that much *most recent* notifications are
     * stored. Attempting to add a notification to the queue while it is
     * already full drops the oldest one to make room for new one.
     */
    size_t stored_notification_limit;

    /**
     * Sets the preference of the library for Content-Format used when
     * responding to a request without Accept option.
     *
     * If set to true, the formats used would be:
     *  - for LwM2M 1.0: TLV,
     *  - for LwM2M 1.1: SenML CBOR, or if not compiled in, SenML JSON, or if
     *    not compiled in TLV.
     */
    bool prefer_hierarchical_formats;

    /**
     * Enables support for DTLS connection_id extension for all DTLS
     * connections.
     */
    bool use_connection_id;

    /**
     * (D)TLS ciphersuites to use if the "DTLS/TLS Ciphersuite" Resource
     * (/0/x/16) is not available or empty.
     *
     * Passing a value with <c>num_ids == 0</c> (default) will cause defaults of
     * the TLS backend library to be used.
     *
     * Contents of the <c>ids</c> array are copied, so it is safe to free the
     * passed array after the call to @ref anjay_new.
     */
    avs_net_socket_tls_ciphersuites_t default_tls_ciphersuites;

    /**
     * Custom PRNG context to use. If @c NULL , a default one is used, with
     * entropy source specific to selected cryptograpic backend. If default
     * entropy source isn't available, creation of Anjay object will fail.
     *
     * Used for establishing TLS and DTLS connections, generation of tokens and
     * by OSCORE module, if it's available.
     *
     * If not @c NULL , then MUST outlive created Anjay object.
     */
    avs_crypto_prng_ctx_t *prng_ctx;

    /**
     * Callback that will be executed when initializing TLS and DTLS
     * connections, that can be used for additional configuration of the TLS
     * backend.
     */
    avs_ssl_additional_configuration_clb_t *additional_tls_config_clb;

} anjay_configuration_t;

/**
 * @returns pointer to the string representing current version of the library.
 */
const char *anjay_get_version(void);

/**
 * Creates a new Anjay object.
 *
 * @param config Initial configuration. For details, see
 *               @ref anjay_configuration_t .
 *
 * @returns Created Anjay object on success, NULL in case of error.
 */
anjay_t *anjay_new(const anjay_configuration_t *config);

/**
 * Cleans up all resources and releases the Anjay object.
 *
 * NOTE: It shall be called <strong>before</strong> freeing LwM2M Objects
 * registered within the <c>anjay</c> object.
 *
 * @param anjay Anjay object to delete. MUST NOT be @c NULL .
 */
void anjay_delete(anjay_t *anjay);

/**
 * Retrieves a list of sockets used for communication with LwM2M servers.
 * Returned list must not be freed nor modified.
 *
 * Example usage: poll()-based application loop
 *
 * @code
 * struct pollfd poll_fd = { .events = POLLIN, .fd = -1 };
 *
 * while (true) {
 *     AVS_LIST(avs_net_socket_t*) sockets = anjay_get_sockets(anjay);
 *     if (sockets) {
 *         // assuming there is only one socket
 *         poll_fd.fd = *(const int*)avs_net_socket_get_system(*sockets);
 *     } else {
 *         // sockets not initialized yet
 *         poll_fd.fd = -1;
 *     }
 *     if (poll(&poll_fd, 1, 1000) > 0) {
 *          if (poll_fd.revents & POLLIN) {
 *              if (anjay_serve(anjay, *sockets)) {
 *                  log("anjay_serve failed");
 *              }
 *          }
 *     }
 * }
 * @endcode
 *
 * @param anjay Anjay object to operate on.
 *
 * @returns A list of valid server sockets on success,
 *          NULL when the device is not connected to any server.
 */
AVS_LIST(avs_net_socket_t *const) anjay_get_sockets(anjay_t *anjay);

typedef enum {
    ANJAY_SOCKET_TRANSPORT_INVALID = -1,
    ANJAY_SOCKET_TRANSPORT_UDP = 0,
    ANJAY_SOCKET_TRANSPORT_TCP
} anjay_socket_transport_t;

/**
 * A structure that describes an open socket used by Anjay. Returned by
 * @ref anjay_get_socket_entries.
 */
typedef struct {
    /**
     * The socket described by this structure. It is intended to be used
     * directly only for checking whether there is data ready, using mechanisms
     * such as <c>select()</c> or <c>poll()</c>.
     */
    avs_net_socket_t *socket;

    /**
     * Transport layer used by <c>socket</c>.
     *
     * Guaranteed to not be @ref ANJAY_SOCKET_TRANSPORT_INVALID, that value is
     * only used internally.
     */
    anjay_socket_transport_t transport;

    /**
     * SSID of the server to which the socket is related. May be:
     * - <c>ANJAY_SSID_ANY</c> if the socket is not directly and unambiguously
     *   related to any server, which includes:
     *   - download sockets
     *   - SMS communication socket (common for all servers)
     * - <c>ANJAY_SSID_BOOTSTRAP</c> for the Bootstrap Server socket
     * - any other value for sockets related to regular LwM2M servers
     */
    anjay_ssid_t ssid;

    /**
     * Flag that is true in the following cases:
     * - it is a UDP communication socket for a regular LwM2M server that is
     *   configured to use the "queue mode", or
     * - it is an SMS communication socket and all LwM2M servers that use this
     *   transport use the "queue mode"
     *
     * In either case, a queue mode socket will stop being returned from
     * @ref anjay_get_sockets and @ref anjay_get_socket_entries after period
     * defined by CoAP <c>MAX_TRANSMIT_WAIT</c> since last communication.
     */
    bool queue_mode;
} anjay_socket_entry_t;

/**
 * Retrieves a list of structures that describe sockets used for communication
 * with LwM2M servers. Returned list must not be freed nor modified.
 *
 * The returned data is equivalent to the one that can be retrieved using
 * @ref anjay_get_sockets - but includes additional data that describes the
 * socket in addition to the socket itself. See @ref anjay_socket_entry_t for
 * details.
 *
 * @param anjay Anjay object to operate on.
 *
 * @returns A list of valid server socket entries on success,
 *          NULL when the device is not connected to any server.
 */
AVS_LIST(const anjay_socket_entry_t) anjay_get_socket_entries(anjay_t *anjay);

/**
 * Reads a message from given @p ready_socket and handles it appropriately.
 *
 * Initially, the receive method on the underlying socket is called with receive
 * timeout set to zero. Subsequent receive requests may block with non-zero
 * timeout values when e.g. waiting for retransmissions or subsequent BLOCK
 * chunks - this is necessary to hide this complexity from the user callbacks in
 * streaming mode.
 *
 * This function may handle more than one request at once. Upon successful
 * return, it is guaranteed that there is no more data to be received on the
 * socket at the moment.
 *
 * @param anjay        Anjay object to operate on.
 * @param ready_socket A socket to read the message from.
 *
 * @returns 0 on success, a negative value in case of error. Note that it
 *          includes non-fatal errors, such as receiving a malformed packet.
 */
int anjay_serve(anjay_t *anjay, avs_net_socket_t *ready_socket);

/** Object ID */
typedef uint16_t anjay_oid_t;

/** Object Instance ID */
typedef uint16_t anjay_iid_t;

/** Resource ID */
typedef uint16_t anjay_rid_t;

/** Resource Instance ID */
typedef uint16_t anjay_riid_t;

/**
 * Value reserved by the LwM2M spec for all kinds of IDs (Object IDs, Object
 * Instance IDs, Resource IDs, Resource Instance IDs, Short Server IDs).
 */
#define ANJAY_ID_INVALID UINT16_MAX

/** Helper macro used to define ANJAY_ERR_ constants.
 * Generated values are valid CoAP Status Codes encoded as a single byte. */
#define ANJAY_COAP_STATUS(Maj, Min) ((uint8_t) ((Maj << 5) | (Min & 0x1F)))

/** Error values that may be returned from data model handlers. @{ */
/**
 * Request sent by the LwM2M Server was malformed or contained an invalid
 * value.
 */
#define ANJAY_ERR_BAD_REQUEST (-ANJAY_COAP_STATUS(4, 0))
/**
 * LwM2M Server is not allowed to perform the operation due to lack of
 * necessary access rights.
 */
#define ANJAY_ERR_UNAUTHORIZED (-ANJAY_COAP_STATUS(4, 1))
/**
 * Low-level CoAP error code; used internally by Anjay when CoAP option values
 * were invalid.
 */
#define ANJAY_ERR_BAD_OPTION (-ANJAY_COAP_STATUS(4, 2))
#define ANJAY_ERR_FORBIDDEN (-ANJAY_COAP_STATUS(4, 3))
/** Target of the operation (Object/Instance/Resource) does not exist. */
#define ANJAY_ERR_NOT_FOUND (-ANJAY_COAP_STATUS(4, 4))
/**
 * Operation is not allowed in current device state or the attempted operation
 * is invalid for this target (Object/Instance/Resource)
 */
#define ANJAY_ERR_METHOD_NOT_ALLOWED (-ANJAY_COAP_STATUS(4, 5))
/**
 * Low-level CoAP error code; used internally by Anjay when the client is
 * unable to encode response in requested content format.
 */
#define ANJAY_ERR_NOT_ACCEPTABLE (-ANJAY_COAP_STATUS(4, 6))
/**
 * Low-level CoAP error code; used internally by Anjay in case of unrecoverable
 * problems during block-wise transfer.
 */
#define ANJAY_ERR_REQUEST_ENTITY_INCOMPLETE (-ANJAY_COAP_STATUS(4, 8))
/**
 * The server requested operation has a Content Format option that is
 * unsupported by Anjay.
 */
#define ANJAY_ERR_UNSUPPORTED_CONTENT_FORMAT (-ANJAY_COAP_STATUS(4, 15))
/** Unspecified error, no other error code was suitable. */
#define ANJAY_ERR_INTERNAL (-ANJAY_COAP_STATUS(5, 0))
/** Operation is not implemented by the LwM2M Client. */
#define ANJAY_ERR_NOT_IMPLEMENTED (-ANJAY_COAP_STATUS(5, 1))
/**
 * LwM2M Client is busy processing some other request; LwM2M Server may retry
 * sending the same request after some delay.
 */
#define ANJAY_ERR_SERVICE_UNAVAILABLE (-ANJAY_COAP_STATUS(5, 3))
/** @} */

/**
 * Extracts the scheduler used by Anjay allowing the user to schedule his own
 * tasks.
 *
 * See docs of [avs_commons library](https://github.com/AVSystem/avs_commons/
 * blob/master/include_public/avsystem/commons/sched.h) for API of
 * @c avs_sched_t object.
 *
 * **Must not** use @c avs_sched_cleanup on the returned scheduler. Anjay will
 * cleanup it itself.
 *
 * <strong>NOTE:</strong> If Anjay is compiled with thread safety enabled, this
 * scheduler object is normally used with the Anjay mutex locked. You will need
 * to ensure thread safety yourself if using this function.
 *
 * @param anjay Anjay object to operate on.
 *
 * @returns non-null scheduler object used by Anjay.
 */
avs_sched_t *anjay_get_scheduler(anjay_t *anjay);

/**
 * Determines time of next scheduled task.
 *
 * May be used to determine how long the device may wait before calling
 * @ref anjay_sched_run .
 *
 * @param      anjay     Anjay object to operate on.
 * @param[out] out_delay Relative time from now of next scheduled task.
 *
 * @returns 0 on success, or a negative value if no tasks are scheduled.
 */
int anjay_sched_time_to_next(anjay_t *anjay, avs_time_duration_t *out_delay);

/**
 * Determines time of next scheduled task in milliseconds.
 *
 * This function is equivalent to @ref anjay_sched_time_to_next but, as a
 * convenience for users of system calls such as <c>poll()</c>, the result is
 * returned as a single integer number of milliseconds.
 *
 * @param      anjay        Anjay object to operate on.
 * @param[out] out_delay_ms Relative time from now of next scheduled task, in
 *                          milliseconds.
 *
 * @returns 0 on success, or a negative value if no tasks are scheduled.
 */
int anjay_sched_time_to_next_ms(anjay_t *anjay, int *out_delay_ms);

/**
 * Calculates time in milliseconds the client code may wait for incoming events
 * before the need to call @ref anjay_sched_run .
 *
 * This function combines @ref anjay_sched_time_to_next_ms with a user-provided
 * limit, so that a conclusive value will always be returned. It is provided as
 * a convenience for users of system calls such as <c>poll()</c>.
 *
 * @param anjay    Anjay object to operate on.
 * @param limit_ms The longest amount of time the function shall return.
 *
 * @returns Relative time from now of next scheduled task, in milliseconds, if
 *          such task exists and it's scheduled to run earlier than
 *          <c>limit_ms</c> seconds from now, or <c>limit_ms</c> otherwise.
 */
int anjay_sched_calculate_wait_time_ms(anjay_t *anjay, int limit_ms);

/**
 * Runs all scheduled events which need to be invoked at or before the time of
 * this function invocation.
 *
 * @param anjay Anjay object to operate on.
 */
void anjay_sched_run(anjay_t *anjay);

/**
 * Schedules sending an Update message to the server identified by given
 * Short Server ID.
 *
 * The Update will be sent during the next @ref anjay_sched_run call.
 *
 * Note: This function will not schedule registration update if Anjay is in
 * offline mode.
 *
 * @param anjay Anjay object to operate on.
 * @param ssid  Short Server ID of the server to send Update to or
 *              @ref ANJAY_SSID_ANY to send Updates to all connected servers.
 *              NOTE: Since Updates are not useful for the Bootstrap Server,
 *              this function does not send one for @ref ANJAY_SSID_BOOTSTRAP
 *              @p ssid .
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_schedule_registration_update(anjay_t *anjay, anjay_ssid_t ssid);

/**
 * This function shall be called when an LwM2M Server Object shall be disabled.
 * The standard case for this is when Execute is performed on the Disable
 * resource (/1/x/4).
 *
 * The server will be disabled for the period of time determined by the value
 * of the Disable Timeout resource (/1/x/5). The resource is read soon after
 * the invocation of this function (during next @ref anjay_sched_run) and is
 * <strong>not</strong> updated upon any subsequent Writes to that resource.
 *
 * @param anjay Anjay object to operate on.
 * @param ssid  Short Server ID of the server to put in a disabled state.
 *              NOTE: disabling a server requires a Server Object Instance
 *              to be present for given @p ssid . Because the Bootstrap Server
 *              does not have one, this function does nothing when called with
 *              @ref ANJAY_SSID_BOOTSTRAP .
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_disable_server(anjay_t *anjay, anjay_ssid_t ssid);

/**
 * This function shall be called when an LwM2M Server Object shall be disabled.
 * The standard case for this is when Execute is performed on the Disable
 * resource (/1/x/4). It may also be used to prevent reconnections if the
 * server becomes unreachable.
 *
 * The server will become disabled during next @ref anjay_sched_run call.
 *
 * NOTE: disabling a server with dual binding (e.g. UDP+SMS trigger) closes both
 * communication channels. Shutting down only one of them requires changing
 * the Binding Resource in Server object.
 *
 * @param anjay   Anjay object to operate on.
 * @param ssid    Short Server ID of the server to put in a disabled state.
 * @param timeout Disable timeout. If set to @c AVS_TIME_DURATION_INVALID,
 *                the server will remain disabled until explicit call to
 *                @ref anjay_enable_server . Otherwise, the server will get
 *                enabled automatically after @p timeout .
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_disable_server_with_timeout(anjay_t *anjay,
                                      anjay_ssid_t ssid,
                                      avs_time_duration_t timeout);

/**
 * Schedules a job for re-enabling a previously disabled (with a call to
 * @ref anjay_disable_server_with_timeout ) server. The server will be enabled
 * during next @ref anjay_sched_run call.
 *
 * @param anjay Anjay object to operate on.
 * @param ssid  Short Server ID of the server to enable.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_enable_server(anjay_t *anjay, anjay_ssid_t ssid);

/**
 * Structure defining the set of transports that
 * @ref anjay_transport_is_offline, @ref anjay_transport_enter_offline,
 * @ref anjay_transport_exit_offline, @ref anjay_transport_set_online and
 * @ref anjay_transport_schedule_reconnect operate on.
 */
typedef struct {
    bool udp : 1;
    bool tcp : 1;
} anjay_transport_set_t;

/**
 * @ref anjay_transport_set_t constant with all fields set to <c>true</c>.
 */
extern const anjay_transport_set_t ANJAY_TRANSPORT_SET_ALL;

/**
 * @ref anjay_transport_set_t constant with <c>udp</c> and <c>tcp</c> fields set
 * to <c>true</c>.
 *
 * NOTE: In the open-source version, @ref ANJAY_TRANSPORT_SET_ALL and
 * @ref ANJAY_TRANSPORT_SET_IP are equivalent.
 */
extern const anjay_transport_set_t ANJAY_TRANSPORT_SET_IP;

/**
 * @ref anjay_transport_set_t constant with just the <c>udp</c> field set to
 * <c>true</c>.
 */
extern const anjay_transport_set_t ANJAY_TRANSPORT_SET_UDP;

/**
 * @ref anjay_transport_set_t constant with just the <c>tcp</c> field set to
 * <c>true</c>.
 */
extern const anjay_transport_set_t ANJAY_TRANSPORT_SET_TCP;

/**
 * Checks whether all the specified transports are in offline mode.
 *
 * @param anjay         Anjay object to operate on.
 * @param transport_set Set of transports to check.
 *
 * @returns true if all of the transports speicifed by @p transport_set are in
 *          offline mode, false otherwise
 */
bool anjay_transport_is_offline(anjay_t *anjay,
                                anjay_transport_set_t transport_set);

/**
 * Puts all the transports specified by @p transport_set into offline mode.
 * This should be done when the connectivity for these transports is deemed
 * unavailable or lost.
 *
 * During subsequent calls to @ref anjay_sched_run, Anjay will close all of the
 * sockets corresponding to the specified transport and stop attempting to make
 * any contact with remote hosts over it, until a call to
 * @ref anjay_transport_exit_offline for any of the corresponding transports.
 *
 * Note that offline mode also affects downloads. E.g., putting the TCP
 * transport into offline mode will pause all ongoing downloads over TCP and
 * prevent new such download requests from being performed.
 *
 * User code shall still interface normally with the library, even if all the
 * transports are in the offline state. This include regular calls to
 * @ref anjay_sched_run. Notifications (as reported using
 * @ref anjay_notify_changed and @ref anjay_notify_instances_changed) continue
 * to be tracked, and may be sent after reconnecting, depending on values of the
 * "Notification Storing When Disabled or Offline" resource.
 *
 * @param anjay         Anjay object to operate on.
 * @param transport_set Set of transports to put into offline mode.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_transport_enter_offline(anjay_t *anjay,
                                  anjay_transport_set_t transport_set);

/**
 * Puts all the transports specified by @p transport_set back into online
 * mode, if any of them were previously put into offline mode using
 * @ref anjay_transport_enter_offline.
 *
 * Transports that are unavailable due to compile-time or runtime configuration
 * are ignored.
 *
 * During subsequent calls to @ref anjay_sched_run, new connections to all
 * LwM2M servers disconnected due to offline mode will be attempted, and
 * Register or Registration Update messages will be sent as appropriate.
 * Downloads paused due to offline mode will be resumed as well.
 *
 * @param anjay         Anjay object to operate on.
 * @param transport_set Set of transports to put into online mode.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_transport_exit_offline(anjay_t *anjay,
                                 anjay_transport_set_t transport_set);

/**
 * Puts all the transports that are enabled through the compile-time and
 * runtime configuration, and specified in @p transport_set, into online mode.
 * At the same time, puts all the other transports into offline mode.
 *
 * This function combines the functionality of @p anjay_transport_enter_offline
 * and @p anjay_transport_exit_offline into a single function. See their
 * documentation for details about the semantics of online and offline modes.
 *
 * @param anjay         Anjay object to operate on.
 * @param transport_set Set of transports to put into online mode.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_transport_set_online(anjay_t *anjay,
                               anjay_transport_set_t transport_set);

/**
 * Reconnects sockets associated with all servers and ongoing downloads over the
 * specified transports. Should be called if something related to the
 * connectivity over those transports changes.
 *
 * The reconnection will be performed during the next @ref anjay_sched_run call
 * and will trigger sending any messages necessary to maintain valid
 * registration (DTLS session resumption and/or Register or Update RPCs).
 *
 * In case of ongoing downloads (started via @ref anjay_download or the
 * <c>fw_update</c> module), if the reconnection fails, the download will be
 * aborted with an error.
 *
 * Note: This function puts all the transports in @p transport_set into online
 * mode.
 *
 * @param anjay         Anjay object to operate on.
 * @param transport_set Set of transports whose sockets shall be reconnected.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_transport_schedule_reconnect(anjay_t *anjay,
                                       anjay_transport_set_t transport_set);

/**
 * Checks whether all the transports are currently in offline mode.
 *
 * DEPRECATED since Anjay 2.4. Provided for backwards compatibility.
 *
 * @param anjay Anjay object to operate on.
 * @returns true if Anjay's instance is offline, false otherwise.
 */
static inline bool anjay_is_offline(anjay_t *anjay) {
    return anjay_transport_is_offline(anjay, ANJAY_TRANSPORT_SET_ALL);
}

/**
 * Puts all the transports into offline mode.
 *
 * DEPRECATED since Anjay 2.4. Provided for backwards compatibility.
 *
 * @param anjay Anjay object to operate on.
 *
 * @returns 0 on success, a negative value in case of error.
 */
static inline int anjay_enter_offline(anjay_t *anjay) {
    return anjay_transport_enter_offline(anjay, ANJAY_TRANSPORT_SET_ALL);
}

/**
 * Puts all the available transports into online mode.
 *
 * DEPRECATED since Anjay 2.4. Provided for backwards compatibility.
 *
 * @param anjay Anjay object to operate on.
 *
 * @returns 0 on success, a negative value in case of error.
 */
static inline int anjay_exit_offline(anjay_t *anjay) {
    return anjay_transport_exit_offline(anjay, ANJAY_TRANSPORT_SET_ALL);
}

/**
 * Reconnects sockets associated with all connected servers and ongoing
 * downloads over all transports.
 *
 * DEPRECATED since Anjay 2.4. Provided for backwards compatibility.
 *
 * @param anjay Anjay object to operate on.
 *
 * @returns 0 on success, a negative value in case of error.
 */
static inline int anjay_schedule_reconnect(anjay_t *anjay) {
    return anjay_transport_schedule_reconnect(anjay, ANJAY_TRANSPORT_SET_ALL);
}

/**
 * Tests if Anjay gave up on any further server connection attempts. It will
 * happen if none of the configured servers could be reached.
 *
 * If this function returns <c>true</c>, it means that Anjay is in an
 * essentially non-operational state. @ref anjay_schedule_reconnect may be
 * called to reset the failure state and retry connecting to all configured
 * servers. @ref anjay_transport_schedule_reconnect will do the same, but only
 * for the specified transports. Alternatively, @ref anjay_enable_server may be
 * used to retry connection only to a specific server.
 *
 * @param anjay Anjay object to operate on.
 *
 * @returns 0 on success, a negative value in case of error.
 */
bool anjay_all_connections_failed(anjay_t *anjay);

typedef struct {
    /**
     * DTLS keys or certificates.
     */
    avs_net_security_info_t security_info;

    /**
     * Single DANE TLSA record to use for certificate verification, if
     * applicable.
     */
    const avs_net_socket_dane_tlsa_record_t *dane_tlsa_record;

    /**
     * TLS ciphersuites to use.
     *
     * A value with <c>num_ids == 0</c> (default) will cause defaults configured
     * through <c>anjay_configuration_t::default_tls_ciphersuites</c>
     * to be used.
     */
    avs_net_socket_tls_ciphersuites_t tls_ciphersuites;
} anjay_security_config_t;

/**
 * Queries security configuration appropriate for a specified URI.
 *
 * Given a URI, the Security object is scanned for instances with Server URI
 * resource matching it in the following way:
 * - if there is at least one instance with matching hostname, protocol and port
 *   number, and valid secure connection configuration, the first such instance
 *   (in the order as returned via @ref anjay_dm_list_instances_t) is used
 * - otherwise, if there is at least one instance with matching hostname and
 *   valid secure connection configuration, the first such instance (in the
 *   order as returned via @ref anjay_dm_list_instances_t) is used
 *
 * The returned security information is exactly the same configuration that is
 * used for LwM2M connection with the server chosen with the rules described
 * above.
 *
 * @param anjay      Anjay object whose data model shall be queried.
 *
 * @param out_config Pointer to an @ref anjay_security_config_t structure that
 *                   will be filled with the appropriate information, if found.
 *
 * @param uri        URI for which to find security configuration.
 *
 * @returns 0 for success, or a negative value in case of error, including if
 *          no suitable LwM2M Security Object instance could be found.
 *
 * <strong>NOTE:</strong> The returned structure will contain pointers to
 * buffers allocated within the @p anjay object. They will only be valid until
 * next call to <c>anjay_security_config_from_dm()</c> or @ref anjay_serve.
 * Note that this is enough for direct use in
 * @ref anjay_fw_update_get_security_config_t implementations. If you need this
 * information for a longer period, you will need to manually create a deep
 * copy.
 */
int anjay_security_config_from_dm(anjay_t *anjay,
                                  anjay_security_config_t *out_config,
                                  const char *uri);

/**
 * Returns @c false if registration to all LwM2M Servers either succeeded or
 * failed with no more retries pending and @c true if registration is in
 * progress.
 */
bool anjay_ongoing_registration_exists(anjay_t *anjay);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*ANJAY_INCLUDE_ANJAY_CORE_H*/
