/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_INCLUDE_ANJAY_LWM2M_SEND_H
#define ANJAY_INCLUDE_ANJAY_LWM2M_SEND_H

#include <anjay/anjay.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ANJAY_WITH_SEND

typedef struct anjay_send_batch_builder_struct anjay_send_batch_builder_t;

typedef struct anjay_send_batch_struct anjay_send_batch_t;

/**
 * Struct used to build array of resources to be added to batch by using
 * @ref anjay_send_batch_data_add_current_multiple() .
 *
 * None of the fields MUST NOT be equal to @c UINT16_MAX .
 */
typedef struct {
    anjay_oid_t oid;
    anjay_iid_t iid;
    anjay_rid_t rid;
} anjay_send_resource_path_t;

/**
 * Send request has previously been deferred, the factors that caused it to be
 * deferred are no longer valid, but it could not be initiated for other
 * reasons.
 *
 * EXAMPLE: @ref anjay_send_deferrable may have been called when the server was
 * offline. The server is now online, but the Send request have been rejected
 * due to the registration having been performed with the LwM2M TS 1.0 protocol
 * version.
 *
 * NOTE: Any of the errors defined in #anjay_send_result_t may be mapped onto
 * this error code. There is currently no way to determine more detailed reason.
 */
#    define ANJAY_SEND_DEFERRED_ERROR (-3)

/**
 * Result passed to #anjay_send_finished_handler_t: No response from Server was
 * received and further retransmissions are aborted due to library cleanup or
 * because the socket used to communicate with the server is being disconnected
 * (e.g. when entering offline mode).
 */
#    define ANJAY_SEND_ABORT (-2)

/**
 * Result passed to #anjay_send_finished_handler_t: No response from Server was
 * received in expected time, or connection with the server has been lost.
 * Retransmissions will not continue - you may try to send the same batch again
 * using @ref anjay_send .
 */
#    define ANJAY_SEND_TIMEOUT (-1)

/**
 * Result passed to #anjay_send_finished_handler_t: Server confirmed successful
 * message delivery.
 */
#    define ANJAY_SEND_SUCCESS 0

/**
 * A handler called if acknowledgement for LwM2M Send operation is received from
 * the Server or all retransmissions of LwM2M Send have failed.
 *
 * @param anjay  Anjay object for which the Send operation was attempted.
 * @param ssid   Short Server ID of the server to which the batch was being
 *               sent.
 * @param batch  Pointer to a batch that was being sent. This pointer may be
 *               passed to @ref anjay_send for sending again; if you wish to
 *               store it for later usage, @ref anjay_send_batch_acquire MUST be
 *               used.
 * @param result Result of the Send message delivery attempt. May be one of:
 *               - @ref ANJAY_SEND_SUCCESS (0) - Server confirmed successful
 *                 message delivery.
 *               - A negative value if any kind of error occured:
 *                 - One of <c>ANJAY_SEND_*</c> constants for conditions
 *                   described by
 *                 - A negated @ref ANJAY_COAP_STATUS (i.e., one of
 *                   <c>ANJAY_ERR_*</c> constants) if there was an unexpected
 *                   (non-success) CoAP response from the server.
 * @param data   Data defined by user passed into the handler.
 */
typedef void anjay_send_finished_handler_t(anjay_t *anjay,
                                           anjay_ssid_t ssid,
                                           const anjay_send_batch_t *batch,
                                           int result,
                                           void *data);

/**
 * Creates a batch builder that may be used to build a payload with the data to
 * be sent to the LwM2M Server by means of LwM2M Send operation.
 *
 * Intended use of the batch builder may be divided into four steps, as follows:
 * 1. Create a batch builder.
 * 2. Fill in the builder with data, by calling anjay_send_batch_add_* functions
 * (possibly multiple times).
 * 3. Convert the builder into the final, immutable batch by @ref
 * anjay_send_batch_builder_compile function call.
 * 4. Pass the resulting batch to @ref anjay_send .
 *
 * Example use (error checking omitted for brevity):
 * @code
 * // Creates a builder for a batch.
 * anjay_send_batch_builder_t *builder = anjay_send_batch_builder_new();
 *
 * // Adds signed integer value to batch builder, without checking if such
 * // resource (oid=1, iid=2, rid=3) exists in datamodel.
 * anjay_send_batch_add_int(
 *         builder, 1, 2, 3, UINT16_MAX, avs_time_real_now(), 123);
 *
 * // Adds value from datamodel (oid=4, iid=5, rid=6) to batch builder if it
 * // exists.
 * anjay_send_batch_data_add_current(builder, anjay, 4, 5, 6);
 *
 * // Creates immutable data batch and releases builder.
 * anjay_send_batch_t *batch = anjay_send_batch_builder_compile(builder);
 *
 * // Puts LwM2M Send request on the scheduler queue. During next call to
 * // anjay_sched_run content of the batch will be sent to server with SSID=1
 * anjay_send(anjay, 1, batch, NULL, NULL);
 *
 * // Releases the batch if it's not used by some send operation.
 * anjay_send_batch_release(&batch);
 * @endcode
 *
 * @returns Pointer to dynamically allocated batch builder, which is freed
 *          implicitly in @ref anjay_send_batch_builder_compile() or has to be
 *          freed manually by calling @ref anjay_send_batch_builder_cleanup() .
 *          NULL in case of allocation failure.
 */
anjay_send_batch_builder_t *anjay_send_batch_builder_new(void);

/**
 * Releases batch builder and discards all data. It has no effect if builder was
 * previously compiled.
 *
 * @param builder Pointer to pointer to data builder. Set to NULL after cleanup.
 */
void anjay_send_batch_builder_cleanup(anjay_send_batch_builder_t **builder);

/**
 * Adds a value to batch builder.
 *
 * IMPORTANT NOTE:
 * If @p timestamp is earlier than 1978-07-04 21:24:16 UTC (2**28 seconds since
 * Unix epoch), then it's assumed to be relative to some arbitrary point in
 * time, and will be encoded as relative to "now". Otherwise, the time is
 * assumed to be an Unix timestamp, and encoded as time since Unix epoch. See
 * also: RFC 8428, "Requirements and Design Goals"
 *
 * @param builder   Pointer to batch builder
 * @param oid       Object ID, MUST NOT be @c UINT16_MAX
 * @param iid       Instance ID, MUST NOT be @c UINT16_MAX
 * @param rid       Resource ID, MUST NOT be @c UINT16_MAX
 * @param riid      Resource Instance ID, @c UINT16_MAX for no RIID
 * @param timestamp Time related to value being sent (e.g. when the measurement
 *                  corresponding to the passed value was made)
 * @param value     Value to add to the batch.
 *
 * @returns 0 on success, negative value otherwise. In case of failure, the
 *          @p builder is left unchanged.
 */
int anjay_send_batch_add_int(anjay_send_batch_builder_t *builder,
                             anjay_oid_t oid,
                             anjay_iid_t iid,
                             anjay_rid_t rid,
                             anjay_riid_t riid,
                             avs_time_real_t timestamp,
                             int64_t value);

/**
 * @copydoc anjay_send_batch_add_int()
 */
int anjay_send_batch_add_uint(anjay_send_batch_builder_t *builder,
                              anjay_oid_t oid,
                              anjay_iid_t iid,
                              anjay_rid_t rid,
                              anjay_riid_t riid,
                              avs_time_real_t timestamp,
                              uint64_t value);

/**
 * @copydoc anjay_send_batch_add_int()
 */
int anjay_send_batch_add_double(anjay_send_batch_builder_t *builder,
                                anjay_oid_t oid,
                                anjay_iid_t iid,
                                anjay_rid_t rid,
                                anjay_riid_t riid,
                                avs_time_real_t timestamp,
                                double value);

/**
 * @copydoc anjay_send_batch_add_int()
 */
int anjay_send_batch_add_bool(anjay_send_batch_builder_t *builder,
                              anjay_oid_t oid,
                              anjay_iid_t iid,
                              anjay_rid_t rid,
                              anjay_riid_t riid,
                              avs_time_real_t timestamp,
                              bool value);

/**
 * Adds a string to batch builder.
 *
 * IMPORTANT NOTE:
 * If @p timestamp is earlier than 1978-07-04 21:24:16 UTC (2**28 seconds since
 * Unix epoch), then it's assumed to be relative to some arbitrary point in
 * time, and will be encoded as relative to "now". Otherwise, the time is
 * assumed to be an Unix timestamp, and encoded as time since Unix epoch. See
 * also: RFC 8428, "Requirements and Design Goals"
 *
 * @param builder   Pointer to batch builder
 * @param oid       Object ID, MUST NOT be @c UINT16_MAX
 * @param iid       Instance ID, MUST NOT be @c UINT16_MAX
 * @param rid       Resource ID, MUST NOT be @c UINT16_MAX
 * @param riid      Resource Instance ID, @c UINT16_MAX for no RIID
 * @param timestamp Time related to string being send (e.g. when the measurement
 *                  corresponding to the passed string was made)
 * @param str       Pointer to a NULL-terminated string. Must not be NULL.
 *                  No longer required by batch builder after call to this
 *                  function, because internal copy is made.
 *
 * @returns 0 on success, negative value otherwise. In case of failure, the
 *          @p builder is left unchanged.
 */
int anjay_send_batch_add_string(anjay_send_batch_builder_t *builder,
                                anjay_oid_t oid,
                                anjay_iid_t iid,
                                anjay_rid_t rid,
                                anjay_riid_t riid,
                                avs_time_real_t timestamp,
                                const char *str);

/**
 * Adds bytes to batch builder.
 *
 * IMPORTANT NOTE:
 * If @p timestamp is earlier than 1978-07-04 21:24:16 UTC (2**28 seconds since
 * Unix epoch), then it's assumed to be relative to some arbitrary point in
 * time, and will be encoded as relative to "now". Otherwise, the time is
 * assumed to be an Unix timestamp, and encoded as time since Unix epoch. See
 * also: RFC 8428, "Requirements and Design Goals"
 *
 * @param builder   Pointer to batch builder
 * @param oid       Object ID, MUST NOT be @c UINT16_MAX
 * @param iid       Instance ID, MUST NOT be @c UINT16_MAX
 * @param rid       Resource ID, MUST NOT be @c UINT16_MAX
 * @param riid      Resource Instance ID, @c UINT16_MAX for no RIID
 * @param timestamp Time related to bytes being send (e.g. when the measurement
 *                  corresponding to the passed bytes was made)
 * @param data      Pointer to data. No longer required by batch builder after
 *                  call to this function, because internal copy is made. Can be
 *                  NULL only if @p length is 0.
 * @param length    Length of data in bytes.
 *
 * @returns 0 on success, negative value otherwise. In case of failure, the
 *          @p builder is left unchanged.
 */
int anjay_send_batch_add_bytes(anjay_send_batch_builder_t *builder,
                               anjay_oid_t oid,
                               anjay_iid_t iid,
                               anjay_rid_t rid,
                               anjay_riid_t riid,
                               avs_time_real_t timestamp,
                               const void *data,
                               size_t length);

/**
 * Adds an Object Link to batch builder.
 *
 * IMPORTANT NOTE:
 * If @p timestamp is earlier than 1978-07-04 21:24:16 UTC (2**28 seconds since
 * Unix epoch), then it's assumed to be relative to some arbitrary point in
 * time, and will be encoded as relative to "now". Otherwise, the time is
 * assumed to be an Unix timestamp, and encoded as time since Unix epoch. See
 * also: RFC 8428, "Requirements and Design Goals"
 *
 * @param builder    Pointer to batch builder
 * @param oid        Object ID, MUST NOT be @c UINT16_MAX
 * @param iid        Instance ID, MUST NOT be @c UINT16_MAX
 * @param rid        Resource ID, MUST NOT be @c UINT16_MAX
 * @param riid       Resource Instance ID, @c UINT16_MAX for no RIID
 * @param timestamp  Time related to Object Link being send (e.g. when the
 *                   measurement corresponding to the passed Object Link was
 *                   made)
 * @param objlnk_oid OID of Object Link
 * @param objlnk_iid IID of Object Link
 *
 * @returns 0 on success, negative value otherwise. In case of failure, the
 *          @p builder is left unchanged.
 */
int anjay_send_batch_add_objlnk(anjay_send_batch_builder_t *builder,
                                anjay_oid_t oid,
                                anjay_iid_t iid,
                                anjay_rid_t rid,
                                anjay_riid_t riid,
                                avs_time_real_t timestamp,
                                anjay_oid_t objlnk_oid,
                                anjay_iid_t objlnk_iid);

/**
 * Reads value from data model of object @p anjay (without checking access
 * privileges) and adds it to the builder with timestamp set to
 * @c avs_time_real_now().
 *
 * May possibly add multiple entries if /oid/iid/rid is a Multiple Resource.
 *
 * @param builder Pointer to batch builder, MUST NOT be @c NULL
 * @param anjay   Pointer to Anjay object, MUST NOT be @c NULL
 * @param oid     Object ID, MUST NOT be @c UINT16_MAX , @c 0 (Security object
 *                ID) or @c 21 (OSCORE object ID).
 * @param iid     Instance ID, MUST NOT be @c UINT16_MAX
 * @param rid     Resource ID, MUST NOT be @c UINT16_MAX
 *
 * @returns 0 on success, negative value otherwise. In case of failure, the
 *          @p builder is left unchanged.
 */
int anjay_send_batch_data_add_current(anjay_send_batch_builder_t *builder,
                                      anjay_t *anjay,
                                      anjay_oid_t oid,
                                      anjay_iid_t iid,
                                      anjay_rid_t rid);

/**
 * Reads values from data model of object @p anjay (without checking access
 * privileges) and adds them to the builder with the same timestamp for every
 * value. Timestamp is set to @c avs_time_real_now().
 *
 * @param builder      Pointer to batch builder, MUST NOT be @c NULL
 * @param anjay        Pointer to Anjay object, MUST NOT be @c NULL
 * @param paths        Pointer to array of @ref anjay_send_resource_path_t .
 * @param paths_length Length of @p paths array.
 *
 * @returns 0 on success, negative value otherwise. In case of failure, the
 *          @p builder is left unchanged.
 */
int anjay_send_batch_data_add_current_multiple(
        anjay_send_batch_builder_t *builder,
        anjay_t *anjay,
        const anjay_send_resource_path_t *paths,
        size_t paths_length);

/**
 * Reads values from data model of object @p anjay (without checking access
 * privileges) and adds them to the builder with the same timestamp for every
 * value. Timestamp is set to @c avs_time_real_now().
 *
 * If a resource is not found, it's ignored, the error isn't returned and the
 * function adds next resources from the @p paths.
 *
 * @param builder      Pointer to batch builder, MUST NOT be @c NULL
 * @param anjay        Pointer to Anjay object, MUST NOT be @c NULL
 * @param paths        Pointer to array of @ref anjay_send_resource_path_t .
 * @param paths_length Length of @p paths array.
 *
 * @returns 0 on success, negative value otherwise. In case of failure, the
 *          @p builder is left unchanged.
 */
int anjay_send_batch_data_add_current_multiple_ignore_not_found(
        anjay_send_batch_builder_t *builder,
        anjay_t *anjay,
        const anjay_send_resource_path_t *paths,
        size_t paths_length);

/**
 * Makes a dynamically-allocated, reference-counted immutable data batch using
 * data from batch builder. Created batch can be used for multiple calls of
 * @c anjay_send().
 *
 * @param builder Pointer to pointer to batch builder. Set to NULL after
 *                successful return.
 *
 * @returns Pointer to compiled batch in case of success, NULL otherwise.
 *          If this function fails, batch builder is not modified and must be
 *          freed manually with @ref anjay_send_batch_builder_cleanup() if it's
 *          not to be used anymore.
 */
anjay_send_batch_t *
anjay_send_batch_builder_compile(anjay_send_batch_builder_t **builder);

/**
 * Increments the refcount for a *batch. Must always be used if batch would be
 * referenced out of current scope, especially when the pointer would be saved
 * to an object that is dynamically allocated. Each call of this function must
 * have complementary @ref anjay_send_batch_release call at some point.
 *
 * @param batch Non-null batch which refcount will be incremented
 *
 * @returns @p batch
 */
anjay_send_batch_t *anjay_send_batch_acquire(const anjay_send_batch_t *batch);

/**
 * Decreases the refcount for a *batch, sets it to NULL, and frees it if the
 * refcount has reached zero.
 *
 * @param *batch Pointer to compiled data batch.
 */
void anjay_send_batch_release(anjay_send_batch_t **batch);

/**
 * All possible error codes that will be returned by the @ref anjay_send()
 */
typedef enum {
    ANJAY_SEND_OK = 0,

    /**
     * This version of Anjay does not support LwM2M Send operation.
     */
    ANJAY_SEND_ERR_UNSUPPORTED,

    /**
     * LwM2M Send cannot be performed because of "Mute Send" Resource is set to
     * true.
     *
     * NOTE: The value of "Mute Send" Resource is controlled by the LwM2M Server
     * itself.
     */
    ANJAY_SEND_ERR_MUTED,

    /**
     * Passed Short Server ID refers to a server, connection to which is
     * currently offline. The LwM2M Send operation may be retried after making
     * the connection back online.
     */
    ANJAY_SEND_ERR_OFFLINE,

    /**
     * Anjay is in process of a Bootstrap. The LwM2M Send operation may be
     * retried after finishing the Bootstrap stage.
     */
    ANJAY_SEND_ERR_BOOTSTRAP,

    /**
     * Passed Short Server ID does not correspond to any existing / connected,
     * non-Bootstrap Server. Especially passing @ref ANJAY_SSID_ANY or @ref
     * ANJAY_SSID_BOOTSTRAP causes this error to be returned.
     */
    ANJAY_SEND_ERR_SSID,

    /**
     * The LwM2M protocol version used to connect to an LwM2M Server does not
     * support the LwM2M Send operation.
     */
    ANJAY_SEND_ERR_PROTOCOL,

    /**
     * Internal error. Very likely caused by the out-of-memory condition. The
     * LwM2M Send operation may be retried after freeing some memory.
     */
    ANJAY_SEND_ERR_INTERNAL
} anjay_send_result_t;

/**
 * Sends data to the LwM2M Server without explicit request by that Server.
 *
 * During the next call to @ref anjay_sched_run @p data will be sent
 * asynchronously to the Server with specified @p ssid but only if Mute Send
 * resource of the server instance associated with @p ssid is set to false.
 * Otherwise nothing is sent and @ref ANJAY_SEND_ERR_MUTED is returned.
 *
 * @p data must not be NULL but it can be everything successfully returned from
 * @ref anjay_send_batch_builder_compile . Even empty batch is acceptable:
 * @code
 * anjay_send_batch_builder_t *builder = anjay_send_batch_builder_new();
 * anjay_send_batch_t *empty_batch = anjay_send_batch_builder_compile(&builder);
 * anjay_send(anjay, ssid, empty_batch, NULL, NULL);
 * @endcode
 * Before sending content of @p data is filtered according to Access Control
 * permissions of a particular server. The server will get only those entries of
 * @p data which paths were configured by @ref anjay_access_control_set_acl with
 * enabled @c ANJAY_ACCESS_MASK_READ .
 *
 * If @p finished_handler is not NULL it will always be called at some
 * point - after receiving acknowledgement from the Server or if no response was
 * received in expected time.
 *
 * Success of this function means only that the data has been sent, not
 * necessarily delivered. Data is delivered if and only if
 * @p finished_handler with status @c ANJAY_SEND_SUCCESS is called.
 *
 * @param anjay                 Anjay object to operate on.
 * @param ssid                  Short Server ID of target LwM2M Server. Cannot
 *                              be ANJAY_SSID_ANY or ANJAY_SSID_BOOTSTRAP.
 * @param data                  Content of the message compiled previously with
 *                              @ref anjay_send_batch_builder_compile .
 * @param finished_handler      Handler called if the server confirmed message
 *                              delivery or if no response was received in
 *                              expected time (handler can be NULL).
 * @param finished_handler_data Data for the handler.
 *
 * @returns one of the @ref anjay_send_result_t enum values.
 */
anjay_send_result_t anjay_send(anjay_t *anjay,
                               anjay_ssid_t ssid,
                               const anjay_send_batch_t *data,
                               anjay_send_finished_handler_t *finished_handler,
                               void *finished_handler_data);

/**
 * Sends data to the LwM2M server, either immediately, or deferring it until
 * such operation will be possible.
 *
 * This function is equivalent to @ref anjay_send, but in cases when the former
 * would return @ref ANJAY_SEND_ERR_OFFLINE or @ref ANJAY_SEND_ERR_BOOTSTRAP,
 * this variant returns success and postpones the actual Send operation until
 * the server connection identified by @p ssid is online.
 *
 * If at that time, the server in question will be removed from the data model,
 * registered using a LwM2M version that does not support the Send operation
 * (i.e., LwM2M 1.0), or the Mute Send resource changes while the Send is
 * deferred, the operation is cancelled and @p finished_handler is called with
 * the @c result argument set to @ref ANJAY_SEND_DEFERRED_ERROR.
 *
 * @param anjay                 Anjay object to operate on.
 * @param ssid                  Short Server ID of target LwM2M Server. Cannot
 *                              be ANJAY_SSID_ANY or ANJAY_SSID_BOOTSTRAP.
 * @param data                  Content of the message compiled previously with
 *                              @ref anjay_send_batch_builder_compile .
 * @param finished_handler      Handler called if the server confirmed message
 *                              delivery or if no response was received in
 *                              expected time (handler can be NULL).
 * @param finished_handler_data Data for the handler.
 *
 * @returns one of the @ref anjay_send_result_t enum values.
 */
anjay_send_result_t
anjay_send_deferrable(anjay_t *anjay,
                      anjay_ssid_t ssid,
                      const anjay_send_batch_t *data,
                      anjay_send_finished_handler_t *finished_handler,
                      void *finished_handler_data);

#endif // ANJAY_WITH_SEND

#ifdef __cplusplus
}
#endif

#endif // ANJAY_INCLUDE_ANJAY_LWM2M_SEND_H
