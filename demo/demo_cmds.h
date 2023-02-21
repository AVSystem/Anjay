/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
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
