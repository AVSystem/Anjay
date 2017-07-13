/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
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
#include <time.h>

#include <avsystem/commons/net.h>
#include <avsystem/commons/list.h>

#ifdef __cplusplus
extern "C" {
#endif

/** LwM2M Enabler Version */
#define ANJAY_SUPPORTED_ENABLER_VERSION "1.0"

/** Anjay object containing all information required for LwM2M communication. */
typedef struct anjay_struct anjay_t;

/** Driver for communication over the SMS transport. Used only by the commercial
 * version of Anjay. */
typedef struct anjay_smsdrv_struct anjay_smsdrv_t;

/**
 * Cleans up all resources and releases an SMS driver object.
 *
 * NOTE: If an Anjay object has been using the SMS driver, the SMS driver shall
 * be cleaned up <strong>after</strong> freeing the Anjay object using
 * @ref anjay_delete.
 *
 * @param smsdrv_ptr Pointer to an SMS driver object to delete.
 */
void anjay_smsdrv_cleanup(anjay_smsdrv_t **smsdrv_ptr);

typedef struct anjay_configuration {
    /** Endpoint name as presented to the LwM2M server. If not set, defaults
     * to ANJAY_DEFAULT_ENDPOINT_NAME. */
    const char *endpoint_name;

    /** UDP port number that all listening sockets will be bound to. It may be
     * left at 0 - in that case, connection with each server will use a freshly
     * generated ephemeral port number. */
    uint16_t udp_listen_port;

    /** DTLS version to use for communication. AVS_NET_SSL_VERSION_DEFAULT will
     * be automatically mapped to AVS_NET_SSL_VERSION_TLSv1_2, which is the
     * version mandated by LwM2M specification. */
    avs_net_ssl_version_t dtls_version;

    /** Maximum size of a single incoming CoAP message. Decreasing this value
     * reduces memory usage, but packets bigger than this value will
     * be dropped. */
    size_t in_buffer_size;

    /** Maximum size of a single outgoing CoAP message. If the message exceeds
     * this size, the library performs the block-wise CoAP transfer
     * ( https://tools.ietf.org/html/rfc7959 ).
     * NOTE: in case of block-wise transfers, this value limits the payload size
     * for a single block, not the size of a whole packet. */
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

    /** Socket configuration to use when creating UDP sockets.
     *
     * Note that:
     * - <c>reuse_addr</c> will be forced to true.
     * - Value pointed to by the <c>preferred_endpoint</c> will be ignored.
     */
    avs_net_socket_configuration_t udp_socket_config;

    /** Controls whether Notify operations are conveyed using Confirmable CoAP
     * messages by default. */
    bool confirmable_notifications;

    /** Specifies the cellular modem driver to use, enabling the SMS transport
     * if not NULL.
     *
     * NOTE: in the Apache-licensed version of Anjay, this feature is not
     * supported, this field exists only for API compatibility with the
     * commercial version, and setting it to non-NULL will cause an error. */
    anjay_smsdrv_t *sms_driver;

    /** Phone number at which the local device is reachable, formatted as an
     * MSISDN (international number without neither the international dialing
     * prefix nor the "+" sign).
     *
     * NOTE: Either both <c>sms_driver</c> and <c>local_msisdn</c> have to be
     * <c>NULL</c>, or both have to be non-<c>NULL</c>. */
    const char *local_msisdn;

    /** If set to true, Anjay will prefer using Concatenated SMS messages when
     * seding large chunks of data over the SMS transport.
     *
     * NOTE: This is only a preference; even if set to true, Concatenated SMS
     * may not be used e.g. when the SMS driver does not support it; even if set
     * to false, Concatenated SMS may be used in cases when it is impossible to
     * split the message in another way, e.g. during DTLS handshake. */
    bool prefer_multipart_sms;
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
 * @param anjay Anjay object to delete.
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
 *     AVS_LIST(avs_net_abstract_socket_t*) sockets = anjay_get_sockets(anjay);
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
AVS_LIST(avs_net_abstract_socket_t *const) anjay_get_sockets(anjay_t *anjay);

/**
 * Reads a message from given @p ready_socket and handles it appropriately.
 *
 * @param anjay        Anjay object to operate on.
 * @param ready_socket A socket to read the message from.
 *
 * @returns 0 on success, a negative value in case of error. Note that it
 *          includes non-fatal errors, such as receiving a malformed packet.
 */
int anjay_serve(anjay_t *anjay,
                avs_net_abstract_socket_t *ready_socket);

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

/** Object ID */
typedef uint16_t anjay_oid_t;

/** Object Instance ID */
typedef uint16_t anjay_iid_t;

/** Object Instance ID value reserved by the LwM2M spec */
#define ANJAY_IID_INVALID UINT16_MAX

/** Resource ID */
typedef uint16_t anjay_rid_t;

/** Resource Instance ID */
typedef uint16_t anjay_riid_t;

/** Helper macro used to define ANJAY_ERR_ constants.
 * Generated values are valid CoAP Status Codes encoded as a single byte. */
#define ANJAY_COAP_STATUS(Maj, Min) ((uint8_t) ((Maj << 5) | (Min & 0x1F)))

/** Error values that may be returned from data model handlers. @{ */
/** Request sent by the LwM2M Server was malformed or contained an invalid
 * value. */
#define ANJAY_ERR_BAD_REQUEST                (-ANJAY_COAP_STATUS(4,  0))
/** LwM2M Server is not allowed to perform the operation due to lack of
 * necessary access rights. */
#define ANJAY_ERR_UNAUTHORIZED               (-ANJAY_COAP_STATUS(4,  1))
/** Low-level CoAP error code; used internally by Anjay when CoAP option values
 * were invalid. */
#define ANJAY_ERR_BAD_OPTION                 (-ANJAY_COAP_STATUS(4,  2))
/** Target of the operation (Object/Instance/Resource) does not exist. */
#define ANJAY_ERR_NOT_FOUND                  (-ANJAY_COAP_STATUS(4,  4))
/** Operation is not allowed in current device state or the attempted operation
 * is invalid for this target (Object/Instance/Resource) */
#define ANJAY_ERR_METHOD_NOT_ALLOWED         (-ANJAY_COAP_STATUS(4,  5))
/** Low-level CoAP error code; used internally by Anjay when the client is
 * unable to encode response in requested content format. */
#define ANJAY_ERR_NOT_ACCEPTABLE             (-ANJAY_COAP_STATUS(4,  6))
/** Low-level CoAP error code; used internally by Anjay in case of unrecoverable
 * problems during block-wise transfer. */
#define ANJAY_ERR_REQUEST_ENTITY_INCOMPLETE  (-ANJAY_COAP_STATUS(4,  8))
/** Unspecified error, no other error code was suitable. */
#define ANJAY_ERR_INTERNAL                   (-ANJAY_COAP_STATUS(5,  0))
/** Operation is not implemented by the LwM2M Client. */
#define ANJAY_ERR_NOT_IMPLEMENTED            (-ANJAY_COAP_STATUS(5,  1))
/** LwM2M Client is busy processing some other request; LwM2M Server may retry
 * sending the same request after some delay. */
#define ANJAY_ERR_SERVICE_UNAVAILABLE        (-ANJAY_COAP_STATUS(5,  3))
/** @} */

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
int anjay_sched_time_to_next(anjay_t *anjay,
                             struct timespec *out_delay);

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
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_sched_run(anjay_t *anjay);

/**
 * Schedules sending an Update message to the server identified by given
 * Short Server ID.
 *
 * The Update will be sent during the next @ref anjay_sched_run call.
 *
 * Note: This function will not schedule registration update if Anjay is in
 * offline mode.
 *
 * @param anjay              Anjay object to operate on.
 * @param ssid               Short Server ID of the server to send Update to or
 *                           @ref ANJAY_SSID_ANY to send Updates to all
 *                           connected servers.
 *                           NOTE: Since Updates are not useful for the
 *                           Bootstrap Server, this function does not send one
 *                           for @ref ANJAY_SSID_BOOTSTRAP @p ssid .
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_schedule_registration_update(anjay_t *anjay,
                                       anjay_ssid_t ssid);

/**
 * Reconnects sockets associated with all connected servers. Should be called if
 * something related to the device's IP connection has changed.
 *
 * The reconnection will be performed during the next @ref anjay_sched_run call
 * and will trigger Registration Update.
 *
 * Note: This function makes Anjay enter online mode.
 *
 * @param anjay              Anjay object to operate on.
 */
int anjay_schedule_reconnect(anjay_t *anjay);

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
 * Checks whether anjay is currently in offline state.
 *
 * @param anjay Anjay object to operate on.
 * @returns true if Anjay's instance is offline, false otherwise.
 */
bool anjay_is_offline(anjay_t *anjay);

/**
 * Puts the LwM2M client into offline mode. This should be done when the
 * Internet connection is deemed to be unavailable or lost.
 *
 * During the next call to @ref anjay_sched_run, Anjay will close all of its
 * sockets and stop attempting to make any contact with remote hosts. It will
 * remain in this state until the call to @ref anjay_exit_offline.
 *
 * User code shall still interface normally with the library while in the
 * offline state, which includes regular calls to @ref anjay_sched_run.
 * Notifications (as reported using @ref anjay_notify_changed and
 * @ref anjay_notify_instances_changed) continue to be tracked, and may be sent
 * after reconnecting, depending on values of the "Notification Storing When
 * Disabled or Offline" resource.
 *
 * @param anjay Anjay object to operate on.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_enter_offline(anjay_t *anjay);

/**
 * Exits the offline state entered using the @ref anjay_enter_offline function.
 *
 * During subsequent calls to @ref anjay_sched_run, new connections to all
 * configured LwM2M Servers will be attempted, and Registration Update
 * (or Register, if the registration lifetime passed in the meantime) messages
 * will be sent.
 *
 * @param anjay Anjay object to operate on.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_exit_offline(anjay_t *anjay);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*ANJAY_INCLUDE_ANJAY_CORE_H*/
