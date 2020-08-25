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

#ifndef ANJAY_SERVERS_REGISTER_H
#define ANJAY_SERVERS_REGISTER_H

#include "../anjay_core.h"

#include "anjay_servers_internal.h"

#ifndef ANJAY_SERVERS_INTERNALS
#    error "Headers from servers/ are not meant to be included from outside"
#endif

VISIBILITY_PRIVATE_HEADER_BEGIN

void _anjay_registration_info_cleanup(anjay_registration_info_t *info);

void _anjay_registration_exchange_state_cleanup(
        anjay_registration_async_exchange_state_t *state);

bool _anjay_server_primary_connection_valid(anjay_server_info_t *server);

typedef enum {
    /** Successfully registered/updated */
    ANJAY_REGISTRATION_SUCCESS,
    /** No response received */
    ANJAY_REGISTRATION_ERROR_TIMEOUT,
    /** A non-timeout communication error */
    ANJAY_REGISTRATION_ERROR_NETWORK,
    /** Non-success CoAP response received */
    ANJAY_REGISTRATION_ERROR_REJECTED,
    /**
     * Fallback to older protocol version requested. Fully handled internally,
     * should not be returned from _anjay_register/_anjay_update_registration.
     */
    ANJAY_REGISTRATION_ERROR_FALLBACK_REQUESTED,
    /** Other failure */
    ANJAY_REGISTRATION_ERROR_OTHER
} anjay_registration_result_t;

/**
 * Makes sure that the @p server has a valid registration state. May send
 * Register or Update messages as necessary. If the server is already properly
 * registered, does nothing - unless
 * server->data_active.registration_info.needs_update is set.
 *
 * @param server Active non-bootstrap server for which to manage the
 *               registration state.
 */
void _anjay_server_ensure_valid_registration(anjay_server_info_t *server);

int _anjay_server_reschedule_update_job(anjay_server_info_t *server);

#ifndef ANJAY_WITHOUT_DEREGISTER
avs_error_t _anjay_server_deregister(anjay_server_info_t *server);
#endif // ANJAY_WITHOUT_DEREGISTER

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_SERVERS_REGISTER_H
