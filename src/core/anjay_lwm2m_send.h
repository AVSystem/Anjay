/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_LWM2M_SEND_H
#define ANJAY_LWM2M_SEND_H

#include <avsystem/commons/avs_list.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct anjay_send_entry anjay_send_entry_t;

typedef struct {
    AVS_LIST(anjay_send_entry_t) entries;
} anjay_sender_t;

bool _anjay_send_in_progress(anjay_connection_ref_t ref);

void _anjay_send_interrupt(anjay_connection_ref_t ref);

void _anjay_send_cleanup(anjay_sender_t *sender);

bool _anjay_send_has_deferred(anjay_unlocked_t *anjay, anjay_ssid_t ssid);

int _anjay_send_sched_retry_deferred(anjay_unlocked_t *anjay,
                                     anjay_ssid_t ssid);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_LWM2M_SEND_H */
