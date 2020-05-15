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

#ifndef AVSYSTEM_COAP_CTX_H
#define AVSYSTEM_COAP_CTX_H

#include <avsystem/coap/avs_coap_config.h>

#include <avsystem/commons/avs_sched.h>
#include <avsystem/commons/avs_socket.h>

#include <avsystem/coap/option.h>
#include <avsystem/coap/token.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /**
     * Number of retransmitted messages. For CoAP/TCP it's always 0.
     */
    uint32_t outgoing_retransmissions_count;

    /**
     * Number of incoming retransmissions. For CoAP/TCP it's always 0.
     */
    uint32_t incoming_retransmissions_count;
} avs_coap_stats_t;

typedef struct avs_coap_request_header {
    /**
     * Request code. See @ref AVS_COAP_CODE or @ref avs_coap_code_constants .
     *
     * NOTE: only 0.xx codes other than 0.00 are allowed on requests.
     */
    uint8_t code;

    /** Request options. See @ref avs_coap_options_add . */
    avs_coap_options_t options;
} avs_coap_request_header_t;

typedef struct avs_coap_response_header {
    /**
     * Response code. See @ref AVS_COAP_CODE or @ref avs_coap_code_constants .
     *
     * NOTE: only 2.xx/4.xx/5.xx codes are allowed on responses.
     */
    uint8_t code;

    /** Response options. See @ref avs_coap_options_add . */
    avs_coap_options_t options;
} avs_coap_response_header_t;

/**
 * CoAP context object.
 *
 * The context must be able to associate async packets with some remote
 * endpoint to know where to send packets. For UDP, we could in theory use a
 * single socket for all CoAP traffic (which would require some DTLS session
 * state shenanigans); for TCP we're unable to do such thing though.
 *
 * The easiest way to achieve multi-protocol support is to have a separate
 * socket object for each remote endpoint, and to associate a separate CoAP
 * context with each one.
 *
 * Despite being tied to a specific socket, the context *does not* own the
 * socket it uses, and *does not* manage the socket connection in any way.
 */
typedef struct avs_coap_ctx avs_coap_ctx_t;

/**
 * Associates the socket with @p ctx .
 *
 * In case of an error, all CoAP context state remains untouched -- except the
 * context error, which is set by this function on failure.
 *
 * NOTE [TCP]: This function will block until Capabilities and Settings Message
 * will be sent and peer's CSM will be received. This function will wait for
 * peer's CSM until request timeout defined during creation of TCP context will
 * pass.
 *
 * NOTE: This function can be used once per context entire lifetime. It is
 * either implicitly called by an appropriate context constructor, or by the
 * user.
 *
 * @param ctx    CoAP context object to modify.
 * @param socket New (and not yet used to send / receive data) socket to be
 *               associated with @p ctx .
 *
 *               The context does not take ownership of the socket. Passed
 *               socket object MUST outlive the context, i.e. @p socket MUST
 *               be valid until @ref avs_coap_ctx_cleanup is called on @p ctx .
 *
 *               The socket MUST be in connected state.
 *
 * @returns @ref AVS_OK for success, or an error condition for which the
 *          operation failed.
 */
avs_error_t avs_coap_ctx_set_socket(avs_coap_ctx_t *ctx,
                                    avs_net_socket_t *socket);

/**
 * @returns true if socket was already set with @ref avs_coap_ctx_set_socket(),
 * false otherwise.
 */
bool avs_coap_ctx_has_socket(avs_coap_ctx_t *ctx);

/**
 * Frees all resources associated with @p ctx .
 *
 * Calls <c>response_handler</c> with @ref AVS_COAP_EXCHANGE_CANCEL result
 * for all unconfirmed asynchronous requests associated with the context.
 *
 * @param ctx Pointer to the CoAP context to delete. After the call to this
 *            function, <c>*ctx</c> is set to NULL.
 *
 * Note: because the context object does not own the socket it is associated
 * with, the socket is not affected by a call to this function.
 */
void avs_coap_ctx_cleanup(avs_coap_ctx_t **ctx);

/**
 * Calculates the maximum transport-specific message payload size able to be
 * received in a single CoAP message given the expected @p options set and
 * @p message_code .
 *
 * This function can be used to plan an asynchronous BLOCK-wise request, to make
 * sure the response would fit into internal receive buffer.
 *
 * For example, one can provide the function with a worst-case option set
 * expected to be received from the peer. Then, one can take the maximum
 * power of two that is less than or equal the value returned to be the BLOCK2
 * option size.
 *
 * NOTE: It is fine to not use this function, if one accepts the additional
 * network level overhead of BLOCK-wise renegotiations taking place underneath,
 * or if one can't predict in any way the response size from the peer.
 *
 * @param options      Option set expected to be received.
 * @param message_code Expected CoAP code to be received in message.
 */
size_t avs_coap_max_incoming_message_payload(avs_coap_ctx_t *ctx,
                                             const avs_coap_options_t *options,
                                             uint8_t message_code);

/**
 * <c>avs_error_t</c> category for values of type @ref avs_coap_error_runtime_t.
 */
#define AVS_COAP_ERR_CATEGORY 22627 // 'acoap' on phone keypad

/**
 * Suggested recovery action after encountering a specific kind of error.
 */
typedef enum avs_coap_error_recovery_action {
    /** CoAP context is still usable. No recovery action is required. */
    AVS_COAP_ERR_RECOVERY_NONE = 0x1000,
    /**
     * CoAP context needs to be recreated to be useful again. If the underlying
     * socket needs to keep any kind of state (e.g. TCP, or even DTLS over UDP),
     * its state is indeterminate. Recreating it (or at least reconnecting) is
     * most probably necessary.
     */
    AVS_COAP_ERR_RECOVERY_RECREATE_CONTEXT = 0x2000,
    /**
     * The error happened on a different layer than avs_coap, it is unclear
     * whether the context may still be used.
     */
    AVS_COAP_ERR_RECOVERY_UNKNOWN = 0x3000
} avs_coap_error_recovery_action_t;

/**
 * General classes of avs_coap errors, indicating whether the error is fatal or
 * not, and what should be done in order to mitigate the error.
 */
typedef enum avs_coap_error_class {
    /**
     * Recoverable errors caused by data received from the remote endpoint.
     * The CoAP context is still usable, user code is free to ignore these
     * errors.
     */
    AVS_COAP_ERR_CLASS_INPUT_RECOVERABLE = AVS_COAP_ERR_RECOVERY_NONE | 0x000,
    /**
     * Recoverable errors caused by unexpected/improper API usage. Correcting
     * them requires user code modification. The library may not behave in the
     * way user expected, but the CoAP context is still usable. User code is
     * free to ignore these errors.
     */
    AVS_COAP_ERR_CLASS_BUG_USER = AVS_COAP_ERR_RECOVERY_NONE | 0x100,
    /**
     * Recoverable errors caused by resource limitations, unexpected OS API
     * behavior, or unimplemented/disabled features. The CoAP context is still
     * usable, but fixing the root cause requires a change in the user or
     * library code, or even the hardware itself.
     */
    AVS_COAP_ERR_CLASS_RUNTIME = AVS_COAP_ERR_RECOVERY_NONE | 0x200,
    /**
     * Unrecoverable errors caused by data received from the remote endpoint.
     * The CoAP context is unusable and should be destroyed.
     */
    AVS_COAP_ERR_CLASS_INPUT_FATAL =
            AVS_COAP_ERR_RECOVERY_RECREATE_CONTEXT | 0x000,
    /**
     * Unrecoverable errors caused by implementation bugs. Correcting them
     * requires fixing avs_coap code. CoAP context becomes unusable, any attempt
     * to recover must involve recreating it anew.
     */
    AVS_COAP_ERR_CLASS_BUG_LIBRARY =
            AVS_COAP_ERR_RECOVERY_RECREATE_CONTEXT | 0x100,
    /**
     * Errors that may or may not be severe; more information is necessary
     * to determine the correct course of action (e.g. inspecting underlying
     * socket errno)
     */
    AVS_COAP_ERR_CLASS_OTHER = AVS_COAP_ERR_RECOVERY_UNKNOWN | 0x000
} avs_coap_error_class_t;

typedef enum {
    /**
     * @defgroup @ref AVS_COAP_ERR_CLASS_INPUT_RECOVERABLE class errors.
     * @{
     */
    /**
     * Received a CoAP/UDP Reset response to sent message. Remote host refuses
     * to accept the message, retransmitting it further is pointless. In case
     * of Observe notifications, Reset response implies cancelling the
     * observation.
     */
    AVS_COAP_ERR_UDP_RESET_RECEIVED = AVS_COAP_ERR_CLASS_INPUT_RECOVERABLE,
    /** Data could not be parsed as valid CoAP message. */
    AVS_COAP_ERR_MALFORMED_MESSAGE,
    /**
     * Data contains a valid CoAP header, but the data that follows it
     * (options list/payload marker) is malformed.
     */
    AVS_COAP_ERR_MALFORMED_OPTIONS,
    /**
     * Remote endpoint requested sending request payload in blocks larger than
     * before.
     */
    AVS_COAP_ERR_BLOCK_SIZE_RENEGOTIATION_INVALID,
    /** Received a truncated message. */
    AVS_COAP_ERR_TRUNCATED_MESSAGE_RECEIVED,
    /**
     * BLOCK sequence number overflowed as a result of block size
     * renegotiation. Block transfer cannot be continued.
     */
    AVS_COAP_ERR_BLOCK_SEQ_NUM_OVERFLOW,
    /**
     * Received ETag option is different than expected, indicating that
     * download continuation is impossible.
     */
    AVS_COAP_ERR_ETAG_MISMATCH,
    /** Received 2.31 Continue response when it was not expected. */
    AVS_COAP_ERR_UNEXPECTED_CONTINUE_RESPONSE,
    /**
     * Exchange timed out. This may mean either:
     * - all retransmissions of a confirmable message were sent but no reply
     *   was received on time,
     * - remote client started a BLOCK-wise request but later stopped sending
     *   requests for further blocks of data.
     */
    AVS_COAP_ERR_TIMEOUT,
    /**
     * Message received over streaming transport is incomplete. It is expected
     * to be finished on subsequent recv() call, but the socket does not report
     * any data available.
     */
    AVS_COAP_ERR_MORE_DATA_REQUIRED,
    /** Incoming message doesn't contain an OSCORE option. */
    AVS_COAP_ERR_OSCORE_OPTION_MISSING,
    /** @} */

    /**
     * @defgroup @ref AVS_COAP_ERR_CLASS_BUG_USER class errors.
     * @{
     */
    /**
     * User requested an operation that requires large buffer space while the
     * shared message buffer associated with the context is already in use.
     * This may happen e.g. when requesting to receive a message while another
     * one is being processed, or to send a message while another is being
     * constructed.
     */
    AVS_COAP_ERR_SHARED_BUFFER_IN_USE = AVS_COAP_ERR_CLASS_BUG_USER,
    /** Attempted to set a socket on a context that already has one. */
    AVS_COAP_ERR_SOCKET_ALREADY_SET,
    /** User-defined payload_writer failed. Message could not be constructed. */
    AVS_COAP_ERR_PAYLOAD_WRITER_FAILED,
    /** @} */

    /**
     * @defgroup @ref AVS_COAP_ERR_CLASS_RUNTIME class errors.
     * @{
     */
    /**
     * A message could not be constructed because either the internal buffer or
     * socket MTU is too small; or incoming message is too large to fit in
     * internal buffer.
     */
    AVS_COAP_ERR_MESSAGE_TOO_BIG = AVS_COAP_ERR_CLASS_RUNTIME,
    /**
     * Calculated time/duration invalid, possibly as a result of dubious
     * retransmission parameters set for the context, or system clock broken.
     */
    AVS_COAP_ERR_TIME_INVALID,
    /** Feature not implemented by the library. */
    AVS_COAP_ERR_NOT_IMPLEMENTED,
    /** Operation support disabled at compile-time. */
    AVS_COAP_ERR_FEATURE_DISABLED,
    /** Data created in OSCORE context is too big. */
    AVS_COAP_ERR_OSCORE_DATA_TOO_BIG,
    /** Error caused by PRNG failure. */
    AVS_COAP_ERR_PRNG_FAIL,
    /** @} */

    /**
     * @defgroup @ref AVS_COAP_ERR_CLASS_INPUT_FATAL class errors
     * @{
     */
    /** CoAP/TCP: Abort message was sent because of an unrecoverable failure. */
    AVS_COAP_ERR_TCP_ABORT_SENT = AVS_COAP_ERR_CLASS_INPUT_FATAL,
    /** CoAP/TCP: Abort message was received. */
    AVS_COAP_ERR_TCP_ABORT_RECEIVED,
    /** CoAP/TCP: Release message was received. */
    AVS_COAP_ERR_TCP_RELEASE_RECEIVED,
    /** CoAP/TCP: CSM message not received when expected. */
    AVS_COAP_ERR_TCP_CSM_NOT_RECEIVED,
    /**
     * CoAP/TCP: unable to parse incoming CSM because of malformed options list.
     */
    AVS_COAP_ERR_TCP_MALFORMED_CSM_OPTIONS_RECEIVED,
    /** CoAP/TCP: unsupported "critical" class CSM option received. */
    AVS_COAP_ERR_TCP_UNKNOWN_CSM_CRITICAL_OPTION_RECEIVED,
    /** TCP connection closed by peer. */
    AVS_COAP_ERR_TCP_CONN_CLOSED,
    /**
     * OSCORE security context is outdated, because too many messages have been
     * sent using current keys. New parameters must be established.
     */
    AVS_COAP_ERR_OSCORE_NEEDS_RECREATE,
    /** @} */

    /**
     * @defgroup @ref AVS_COAP_ERR_CLASS_BUG_LIBRARY class errors.
     * @{
     */
    /** Assertion failure in release mode. */
    AVS_COAP_ERR_ASSERT_FAILED = AVS_COAP_ERR_CLASS_BUG_LIBRARY,
    /** @} */

    /** User handler canceled an exchange the CoAP context was operating on. */
    AVS_COAP_ERR_EXCHANGE_CANCELED = AVS_COAP_ERR_CLASS_OTHER
} avs_coap_error_t;

static inline avs_coap_error_class_t avs_coap_error_class(avs_error_t err) {
    if (avs_is_ok(err) || err.category != AVS_COAP_ERR_CATEGORY) {
        return AVS_COAP_ERR_CLASS_OTHER;
    } else {
        static const uint16_t ERROR_CLASS_MASK = 0xff00;
        return (avs_coap_error_class_t) (err.code & ERROR_CLASS_MASK);
    }
}

static inline avs_coap_error_recovery_action_t
avs_coap_error_recovery_action(avs_error_t err) {
    if (avs_is_ok(err)) {
        return AVS_COAP_ERR_RECOVERY_NONE;
    } else {
        static const unsigned RECOVERY_ACTION_MASK = 0xf000;
        return (avs_coap_error_recovery_action_t) ((unsigned)
                                                           avs_coap_error_class(
                                                                   err)
                                                   & RECOVERY_ACTION_MASK);
    }
}

/**
 * Converts an error returned by some avs_coap function into a human-readable
 * string.
 *
 * @param error    The error to report as a string value
 *
 * @param buf      The buffer that may be used to stringify numeric values of an
 *                 unknown error into.
 *
 * @param buf_size Number of bytes available in @buf .
 *
 * @returns a human-readable string for a value returned by
 *          @ref avs_coap_ctx_error . May be either @p buf (if the error is
 *          unknown and there is enough space there) or some
 *          statically allocated string.
 */
const char *avs_coap_strerror(avs_error_t error, char *buf, size_t buf_size);

/**
 * Wrapper over @ref avs_coap_strerror that creates a temporary stack-allocated
 * buffer for stringifying unknown errors.
 *
 * The pointer returned by this macro is safe to use within the same statement
 * (e.g. as an argument to a logging function).
 */
#define AVS_COAP_STRERROR(Err) avs_coap_strerror((Err), &(char[64]){ 0 }[0], 64)

/**
 * Getter for statistics of CoAP context. See docs for @ref avs_coap_stats_t
 * for more details.
 *
 * If it's not implemented, returns @ref avs_coap_stats_t filled with zeros.
 *
 * @param ctx       Context to get statistics from.
 */
avs_coap_stats_t avs_coap_get_stats(avs_coap_ctx_t *ctx);

/**
 * A callback that determines whether given option number is appropriate for
 * a message with specific CoAP code.
 *
 * @param msg_code Code of the CoAP message.
 * @param optnum   Option number to check. This will always be a number
 *                 referring to a critical option (as defined in RFC7252).
 *
 * @returns Should return true if the option is acceptable, false otherwise.
 */
typedef bool avs_coap_critical_option_validator_t(uint8_t msg_code,
                                                  uint32_t optnum);

/**
 * Checks whether critical options from @p msg are valid. BLOCK1 and BLOCK2
 * options are handled internally, other options need to be checked
 * by @p validator.
 *
 * @param request_header CoAP Message header to validation options in.
 * @param validator      Callback that checks validity of a critical option.
 *                       Must not be NULL.
 *
 * @returns 0 if all critical options are considered valid, a negative value
 *          otherwise.
 */
int avs_coap_options_validate_critical(
        const avs_coap_request_header_t *request_header,
        avs_coap_critical_option_validator_t validator);

#ifdef __cplusplus
}
#endif

#endif // AVSYSTEM_COAP_CTX_H
