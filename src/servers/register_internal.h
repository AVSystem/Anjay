/*
 * Copyright 2017-2018 AVSystem <avsystem@avsystem.com>
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

#ifndef ANJAY_SERVERS_INTERNALS
#error "Headers from servers/ are not meant to be included from outside"
#endif

VISIBILITY_PRIVATE_HEADER_BEGIN

bool
_anjay_server_registration_connection_valid(anjay_server_info_t *server);

bool _anjay_server_registration_expired(anjay_server_info_t *server);

/**
 * Makes sure that the @p server has a valid registration state. May send
 * Register or Update messages as necessary. If the server is already properly
 * registered, does nothing - unless
 * <c>server-&gt;data_active.registration_info.needs_update<c> is set.
 *
 * @param anjay  Anjay object to operate on.
 * @param server Active non-bootstrap server for which to manage the
 *               registration state.
 *
 * @returns @li 0 in case of success
 *          @li Negative value in case of an error above the network layer. In
 *              case of CoAP-layer errors, <c>ANJAY_ERR_*</c> values will be
 *              returned.
 *          @li Positive value in case of an error on the network layer or
 *              below (indicating a need to reconnect the socket). If relevant,
 *              <c>errno</c> values (e.g. <c>ECONNREFUSED</c> will be returned.
 */
int _anjay_server_ensure_valid_registration(anjay_t *anjay,
                                            anjay_server_info_t *server);

int _anjay_server_reschedule_update_job(anjay_t *anjay,
                                        anjay_server_info_t *server);

int _anjay_server_deregister(anjay_t *anjay,
                             anjay_server_info_t *server);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_SERVERS_REGISTER_H
