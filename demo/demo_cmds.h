/*
 * Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */

#ifndef DEMO_CMDS_H
#define DEMO_CMDS_H

typedef struct {
    struct anjay_demo_struct *demo;
    char cmd[];
} demo_command_invocation_t;

void demo_command_dispatch(const demo_command_invocation_t *invocation);

#endif
