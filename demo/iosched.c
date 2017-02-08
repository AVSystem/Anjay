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

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <poll.h>

#include <avsystem/commons/list.h>

#include "iosched.h"
#include "utils.h"

typedef struct iosched_instant_entry {
    iosched_handler_t *handler;
} iosched_instant_entry_t;

typedef struct iosched_poll_entry {
    int fd;
    short events;
    iosched_poll_handler_t *handler;
} iosched_poll_entry_t;

typedef enum iosched_entry_type {
    SCHED_ENTRY_INSTANT,
    SCHED_ENTRY_POLL
} iosched_entry_type_t;

struct iosched_entry {
    iosched_entry_type_t type;
    void *arg;
    iosched_free_arg_t *free_arg;

    union {
        iosched_instant_entry_t instant;
        iosched_poll_entry_t poll;
    } data;
};

struct iosched_struct {
    // invariant: entries are sorted by type
    AVS_LIST(iosched_entry_t) entries;
};

iosched_t *iosched_create(void) {
    return (iosched_t *) calloc(1, sizeof(iosched_t));
}

void iosched_release(iosched_t *sched) {
    if (sched) {
        AVS_LIST_CLEAR(&sched->entries) {
            if (sched->entries->free_arg) {
                sched->entries->free_arg(sched->entries->arg);
            }
        }
        free(sched);
    }
}

static void insert_entry(iosched_t *sched,
                         AVS_LIST(iosched_entry_t) entry) {
    AVS_LIST(iosched_entry_t) *it;
    AVS_LIST_FOREACH_PTR(it, &sched->entries) {
        if ((*it)->type >= entry->type) {
            AVS_LIST_INSERT(it, entry);
            return;
        }
    }

    AVS_LIST_INSERT(it, entry);
}

const iosched_entry_t *iosched_poll_entry_new(iosched_t *sched,
                                              int fd,
                                              short events,
                                              iosched_poll_handler_t *handler,
                                              void *arg,
                                              iosched_free_arg_t *free_arg) {
    if (fd < 0 || !events || !handler) {
        return NULL;
    }

    iosched_entry_t *entry = AVS_LIST_NEW_ELEMENT(iosched_entry_t);
    if (entry) {
        entry->type = SCHED_ENTRY_POLL;
        entry->arg = arg;
        entry->free_arg = free_arg;
        entry->data.poll.fd = fd;
        entry->data.poll.events = events;
        entry->data.poll.handler = handler;
        insert_entry(sched, entry);
    }
    return entry;
}

const iosched_entry_t *iosched_instant_entry_new(iosched_t *sched,
                                                 iosched_handler_t *handler,
                                                 void *arg,
                                                 iosched_free_arg_t *free_arg) {
    if (!handler) {
        return NULL;
    }

    iosched_entry_t *entry = AVS_LIST_NEW_ELEMENT(iosched_entry_t);
    if (entry) {
        entry->type = SCHED_ENTRY_INSTANT;
        entry->arg = arg;
        entry->free_arg = free_arg;
        entry->data.instant.handler = handler;
        insert_entry(sched, entry);
    }
    return entry;
}

void iosched_entry_remove(iosched_t *sched,
                          const iosched_entry_t *entry) {
    AVS_LIST(iosched_entry_t) *entry_ptr =
            AVS_LIST_FIND_PTR(&sched->entries, entry);

    if (entry_ptr) {
        if ((*entry_ptr)->free_arg) {
            (*entry_ptr)->free_arg((*entry_ptr)->arg);
        }
        AVS_LIST_DELETE(entry_ptr);
    }
}

static void handle_instant_entries(iosched_t *sched) {
    AVS_LIST(iosched_entry_t) *entry;
    AVS_LIST(iosched_entry_t) helper;
    AVS_LIST_DELETABLE_FOREACH_PTR(entry, helper, &sched->entries) {
        if ((*entry)->type != SCHED_ENTRY_INSTANT) {
            return;
        }

        (*entry)->data.instant.handler((*entry)->arg);
        if ((*entry)->free_arg) {
            (*entry)->free_arg((*entry)->arg);
        }
        AVS_LIST_DELETE(entry);
    }
}

static size_t get_poll_fds(iosched_t *sched,
                           struct pollfd *poll_fds,
                           size_t max_poll_fds) {
    iosched_entry_t *entry;
    size_t count = 0;

    AVS_LIST_FOREACH(entry, sched->entries) {
        assert(entry->type == SCHED_ENTRY_POLL);
        assert(count < max_poll_fds);
        (void) max_poll_fds;

        memset(&poll_fds[count], 0, sizeof(struct pollfd));
        poll_fds[count].fd = entry->data.poll.fd;
        poll_fds[count].events = entry->data.poll.events;
        ++count;
    }

    return count;
}

static int handle_poll_entries(iosched_t *sched,
                               int poll_timeout_ms) {
    struct pollfd poll_fds[AVS_LIST_SIZE(sched->entries)];
    size_t poll_fds_count = get_poll_fds(sched, poll_fds, ARRAY_SIZE(poll_fds));

    int result = poll(poll_fds, poll_fds_count, poll_timeout_ms);
    if (result <= 0) {
        return result;
    }

    AVS_LIST(iosched_entry_t) *entry;
    AVS_LIST(iosched_entry_t) helper;

    size_t i = 0;
    AVS_LIST_DELETABLE_FOREACH_PTR(entry, helper, &sched->entries) {
        while (i < poll_fds_count
               && poll_fds[i].fd != (*entry)->data.poll.fd) {
            ++i;
        }

        if (i >= poll_fds_count) {
            break;
        }

        if (poll_fds[i].revents) {
            (*entry)->data.poll.handler(poll_fds[i].revents, (*entry)->arg);
        }
        ++i;
    }

    return 0;
}

int iosched_run(iosched_t *sched, int timeout_ms) {
    handle_instant_entries(sched);
    return handle_poll_entries(sched, timeout_ms);
}
