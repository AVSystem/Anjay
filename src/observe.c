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

#include <config.h>

#include <math.h>

#include <avsystem/commons/stream_v_table.h>

#include <anjay_modules/time.h>

#include "anjay.h"
#include "dm/query.h"
#include "observe.h"

VISIBILITY_SOURCE_BEGIN

struct anjay_observe_entry_struct {
    const anjay_observe_key_t key;
    anjay_sched_handle_t notify_task;
    struct timespec last_confirmable;

    // last_sent has ALWAYS EXACTLY one element,
    // but is stored as a list to allow easy moving from unsent
    AVS_LIST(anjay_observe_resource_value_t) last_sent;

    // pointer to some element of the
    // anjay_observe_connection_entry_t::unsent list
    // may or may not be the same as
    // anjay_observe_connection_entry_t::unsent_last
    // (depending on whether the last unsent value in the server refers
    // to this resource+format or not)
    AVS_LIST(anjay_observe_resource_value_t) last_unsent;
};

struct anjay_observe_connection_entry_struct {
    anjay_observe_connection_key_t key;
    AVS_RBTREE(anjay_observe_entry_t) entries;
    anjay_sched_handle_t flush_task;

    AVS_LIST(anjay_observe_resource_value_t) unsent;
    // pointer to the last element of unsent
    AVS_LIST(anjay_observe_resource_value_t) unsent_last;
};

static inline const anjay_observe_entry_t *
entry_query(const anjay_observe_key_t *key) {
    return AVS_CONTAINER_OF(key, anjay_observe_entry_t, key);
}

static inline const anjay_observe_connection_entry_t *
connection_query(const anjay_observe_connection_key_t *key) {
    return AVS_CONTAINER_OF(key, anjay_observe_connection_entry_t, key);
}

static int connection_key_cmp(const anjay_observe_connection_key_t *left,
                              const anjay_observe_connection_key_t *right) {
    int32_t tmp_diff = left->ssid - right->ssid;
    if (!tmp_diff) {
        tmp_diff = (int32_t) left->type - (int32_t) right->type;
    }
    return tmp_diff;
}

static int connection_state_cmp(const void *left, const void *right) {
    return connection_key_cmp(
            &((const anjay_observe_connection_entry_t *) left)->key,
            &((const anjay_observe_connection_entry_t *) right)->key);
}

static int entry_key_cmp(const anjay_observe_key_t *left,
                         const anjay_observe_key_t *right) {
    int32_t tmp_diff =
            connection_key_cmp(&left->connection, &right->connection);
    if (!tmp_diff) {
        tmp_diff = left->oid - right->oid;
    }
    if (!tmp_diff) {
        tmp_diff = left->iid - right->iid;
    }
    if (!tmp_diff) {
        if (left->rid < right->rid) {
            return -1;
        } else if (left->rid > right->rid) {
            return 1;
        } else {
            tmp_diff = left->format - right->format;
        }
    }
    return tmp_diff < 0 ? -1 : (tmp_diff > 0 ? 1 : 0);
}

static int entry_cmp(const void *left, const void *right) {
    return entry_key_cmp(&((const anjay_observe_entry_t *) left)->key,
                         &((const anjay_observe_entry_t *) right)->key);
}

int _anjay_observe_init(anjay_t *anjay) {
    if (!(anjay->observe.connection_entries =
            AVS_RBTREE_NEW(anjay_observe_connection_entry_t,
                           connection_state_cmp))) {
        anjay_log(ERROR, "Could not initialize Observe structures");
        return -1;
    }
    return 0;
}

static void cleanup_connection(anjay_t *anjay,
                               anjay_observe_connection_entry_t *conn) {
    AVS_RBTREE_DELETE(&conn->entries) {
        _anjay_sched_del(anjay->sched, &(*conn->entries)->notify_task);
        AVS_LIST_CLEAR(&(*conn->entries)->last_sent);
    }
    _anjay_sched_del(anjay->sched, &conn->flush_task);
    AVS_LIST_CLEAR(&conn->unsent);
}

void _anjay_observe_cleanup(anjay_t *anjay) {
    AVS_RBTREE_DELETE(&anjay->observe.connection_entries) {
        cleanup_connection(anjay, *anjay->observe.connection_entries);
    }
}

static int observe_setup_for_sending(avs_stream_abstract_t *stream,
                                     const anjay_msg_details_t *details) {
    assert(!details->uri_path);
    assert(!details->uri_query);
    *((anjay_observe_stream_t *) stream)->details = *details;
    return 0;
}

const anjay_observe_stream_t *_anjay_observe_stream_initializer__(void) {
    static volatile bool initialized = false;

    static avs_stream_v_table_t vtable;
    static const anjay_observe_stream_t initializer = {
        .outbuf = { .vtable = &vtable }
    };
    static const anjay_coap_stream_ext_t coap_ext = {
        .setup_response = observe_setup_for_sending
    };
    static const avs_stream_v_table_extension_t extensions[] = {
        { ANJAY_COAP_STREAM_EXTENSION, &coap_ext },
        AVS_STREAM_V_TABLE_EXTENSION_NULL
    };

    if (!initialized) {
        memcpy(&vtable, AVS_STREAM_OUTBUF_STATIC_INITIALIZER.vtable,
               sizeof(avs_stream_v_table_t));
        vtable.extension_list = extensions;

        initialized = true;
    }

    return &initializer;
}

static void clear_entry(anjay_t *anjay,
                        anjay_observe_connection_entry_t *connection,
                        anjay_observe_entry_t *entry) {
    _anjay_sched_del(anjay->sched, &entry->notify_task);
    AVS_LIST_CLEAR(&entry->last_sent);

    if (entry->last_unsent) {
        anjay_observe_resource_value_t **unsent_ptr;
        anjay_observe_resource_value_t *helper;
        anjay_observe_resource_value_t *server_last_unsent = NULL;
        AVS_LIST_DELETABLE_FOREACH_PTR(unsent_ptr, helper,
                                       &connection->unsent) {
            if ((*unsent_ptr)->ref != entry) {
                server_last_unsent = *unsent_ptr;
            } else {
                AVS_LIST_DELETE(unsent_ptr);
            }
        }
        connection->unsent_last = server_last_unsent;
        entry->last_unsent = NULL;
    }
}

static void delete_connection(
        anjay_t *anjay,
        AVS_RBTREE_ELEM(anjay_observe_connection_entry_t) *conn_ptr) {
    cleanup_connection(anjay, *conn_ptr);
    AVS_RBTREE_DELETE_ELEM(anjay->observe.connection_entries, conn_ptr);
}

static void delete_connection_if_empty(
        anjay_t *anjay,
        AVS_RBTREE_ELEM(anjay_observe_connection_entry_t) *conn_ptr) {
    if (!AVS_RBTREE_FIRST((*conn_ptr)->entries)) {
        assert(!(*conn_ptr)->unsent);
        assert(!(*conn_ptr)->unsent_last);
        delete_connection(anjay, conn_ptr);
    }
}

static int trigger_observe(anjay_t *anjay, void *entry_);

static const anjay_observe_resource_value_t *
newest_value(const anjay_observe_entry_t *entry) {
    if (entry->last_unsent) {
        return entry->last_unsent;
    } else {
        assert(entry->last_sent);
        return entry->last_sent;
    }
}

static int schedule_trigger(anjay_t *anjay,
                            anjay_observe_entry_t *entry,
                            time_t period) {
    struct timespec realtime_now;
    clock_gettime(CLOCK_REALTIME, &realtime_now);

    struct timespec delay;
    _anjay_time_diff(&delay, &newest_value(entry)->timestamp, &realtime_now);
    delay.tv_sec += period;
    if (delay.tv_sec < 0) {
        delay.tv_sec = 0;
        delay.tv_nsec = 0;
    }

    if (period >= 0) {
        _anjay_sched_del(anjay->sched, &entry->notify_task);
        if (_anjay_sched(anjay->sched, &entry->notify_task, delay,
                         trigger_observe, entry)) {
            return -1;
        }
    }
    return 0;
}

static AVS_LIST(anjay_observe_resource_value_t)
create_resource_value(const anjay_msg_details_t *details,
                      anjay_observe_entry_t *ref,
                      const anjay_coap_msg_identity_t *identity,
                      double numeric,
                      const void *data, size_t size) {
    AVS_LIST(anjay_observe_resource_value_t) result =
            (anjay_observe_resource_value_t *) AVS_LIST_NEW_BUFFER(
                    offsetof(anjay_observe_resource_value_t, value) + size);
    if (!result) {
        anjay_log(ERROR, "Out of memory");
        return NULL;
    }
    result->details = *details;
    result->ref = ref;
    result->identity = *identity;
    result->numeric = numeric;
    AVS_STATIC_ASSERT(sizeof(result->value_length) == sizeof(size),
                      length_size);
    memcpy((void *) (intptr_t) &result->value_length, &size, sizeof(size));
    assert(data || !size);
    if (data) {
        memcpy(result->value, data, size);
    }
    clock_gettime(CLOCK_REALTIME, &result->timestamp);
    return result;
}

static int insert_new_value(anjay_observe_connection_entry_t *conn_state,
                            anjay_observe_entry_t *entry,
                            const anjay_msg_details_t *details,
                            const anjay_coap_msg_identity_t *identity,
                            double numeric,
                            const void *data,
                            size_t size) {
    AVS_LIST(anjay_observe_resource_value_t) res_value =
            create_resource_value(details, entry, identity,
                                  numeric, data, size);
    if (!res_value) {
        return -1;
    }
    AVS_LIST_APPEND(&conn_state->unsent_last, res_value);
    conn_state->unsent_last = res_value;
    if (!conn_state->unsent) {
        conn_state->unsent = res_value;
    }
    entry->last_unsent = res_value;
    return 0;
}

static int insert_error(anjay_t *anjay,
                        anjay_observe_connection_entry_t *conn_state,
                        anjay_observe_entry_t *entry,
                        const anjay_coap_msg_identity_t *identity,
                        int outer_result) {
    _anjay_sched_del(anjay->sched, &entry->notify_task);
    const anjay_msg_details_t details = {
        .msg_type = ANJAY_COAP_MSG_NON_CONFIRMABLE,
        .msg_code = _anjay_make_error_response_code(outer_result),
        .format = ANJAY_COAP_FORMAT_NONE,
        .observe_serial = true
    };
    return insert_new_value(conn_state, entry, &details, identity,
                            NAN, NULL, 0);
}

static int ensure_present(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj,
                          anjay_iid_t iid,
                          int32_t rid) {
    int result = 0;
    if (iid != ANJAY_IID_INVALID) {
        result = _anjay_dm_map_present_result(
                _anjay_dm_instance_present(anjay, obj, iid));
        if (result) {
            return result;
        }
    }
    if (rid >= 0) {
        result = _anjay_dm_map_present_result(
                _anjay_dm_resource_supported_and_present(
                        anjay, obj, iid, (anjay_rid_t) rid));
        if (result) {
            return result;
        }
    }
    return 0;
}

static int get_obj_and_attrs(anjay_t *anjay,
                             const anjay_dm_object_def_t *const **out_obj,
                             anjay_dm_attributes_t *out_attrs,
                             const anjay_observe_key_t *key) {
    *out_obj = _anjay_dm_find_object_by_oid(anjay, key->oid);
    if (!*out_obj || !**out_obj) {
        return -1;
    }
    int result = ensure_present(anjay, *out_obj, key->iid, key->rid);
    if (result) {
        return result;
    }

    anjay_dm_attrs_query_details_t details = {
        .obj = *out_obj,
        .iid = key->iid,
        .rid = key->rid,
        .ssid = key->connection.ssid,
        .with_server_level_attrs = true
    };
    return _anjay_dm_effective_attrs(anjay, &details, out_attrs);
}

static inline int get_attrs(anjay_t *anjay,
                            anjay_dm_attributes_t *out_attrs,
                            const anjay_observe_key_t *key) {
    const anjay_dm_object_def_t *const *obj_placeholder;
    return get_obj_and_attrs(anjay, &obj_placeholder, out_attrs, key);
}

static int insert_initial_value(
        anjay_t *anjay,
        anjay_observe_connection_entry_t *conn_state,
        anjay_observe_entry_t *entry,
        const anjay_msg_details_t *details,
        const anjay_coap_msg_identity_t *identity,
        double numeric,
        const void *data,
        size_t size) {
    assert(!entry->last_sent);
    assert(!entry->last_unsent);

    struct timespec realtime_now;
    clock_gettime(CLOCK_REALTIME, &realtime_now);

    int result;
    anjay_dm_attributes_t attrs;
    // we assume that the initial value should be treated as sent,
    // even though we haven't actually sent it ourselves
    if (!(result = get_attrs(anjay, &attrs, &entry->key))
            && (entry->last_sent =
                    create_resource_value(details, entry, identity,
                                          numeric, data, size))
            && !(result = schedule_trigger(anjay, entry,
                                           attrs.max_period))) {
        entry->last_confirmable = realtime_now;
    } else {
        clear_entry(anjay, conn_state, entry);
    }
    return result;
}

static AVS_RBTREE_ELEM(anjay_observe_entry_t)
find_or_create_observe_entry(anjay_observe_connection_entry_t *connection,
                             const anjay_observe_key_t *key) {
    AVS_RBTREE_ELEM(anjay_observe_entry_t) new_entry =
            AVS_RBTREE_ELEM_NEW(anjay_observe_entry_t);
    if (!new_entry) {
        anjay_log(ERROR, "Out of memory");
        return NULL;
    }

    memcpy((void *) (intptr_t) (const void *) &new_entry->key, key,
           sizeof(*key));
    AVS_RBTREE_ELEM(anjay_observe_entry_t) entry =
            AVS_RBTREE_INSERT(connection->entries, new_entry);
    if (entry != new_entry) {
        AVS_RBTREE_ELEM_DELETE_DETACHED(&new_entry);
    }
    return entry;
}

static AVS_RBTREE_ELEM(anjay_observe_connection_entry_t)
find_or_create_connection_state(anjay_t *anjay,
                                const anjay_observe_connection_key_t *key) {
    AVS_RBTREE_ELEM(anjay_observe_connection_entry_t) conn =
            AVS_RBTREE_FIND(anjay->observe.connection_entries,
                            connection_query(key));
    if (!conn) {
        conn = AVS_RBTREE_ELEM_NEW(anjay_observe_connection_entry_t);
        if (!conn || !(conn->entries = AVS_RBTREE_NEW(anjay_observe_entry_t,
                                                      entry_cmp))) {
            anjay_log(ERROR, "Out of memory");
            AVS_RBTREE_ELEM_DELETE_DETACHED(&conn);
            return NULL;
        }
        conn->key = *key;
        AVS_RBTREE_INSERT(anjay->observe.connection_entries, conn);
    }
    return conn;
}

int _anjay_observe_put_entry(anjay_t *anjay,
                             const anjay_observe_key_t *key,
                             const anjay_msg_details_t *details,
                             const anjay_coap_msg_identity_t *identity,
                             double numeric,
                             const void *data,
                             size_t size) {
    assert(key->rid >= -1 && key->rid <= UINT16_MAX);
    AVS_RBTREE_ELEM(anjay_observe_connection_entry_t) conn =
            find_or_create_connection_state(anjay, &key->connection);
    if (!conn) {
        return -1;
    }

    AVS_RBTREE_ELEM(anjay_observe_entry_t) entry =
            find_or_create_observe_entry(conn, key);
    if (!entry) {
        delete_connection_if_empty(anjay, &conn);
        return -1;
    }

    clear_entry(anjay, conn, entry);
    int result = insert_initial_value(anjay, conn, entry, details, identity,
                                      numeric, data, size);
    if (!result) {
        return 0;
    }

    anjay_log(ERROR, "Could not put OBSERVE entry");
    AVS_RBTREE_DELETE_ELEM(conn->entries, &entry);
    delete_connection_if_empty(anjay, &conn);
    return result;
}

static void
delete_entry(anjay_t *anjay,
             AVS_RBTREE_ELEM(anjay_observe_connection_entry_t) *conn_ptr,
             AVS_RBTREE_ELEM(anjay_observe_entry_t) *entry_ptr) {
    clear_entry(anjay, *conn_ptr, *entry_ptr);
    AVS_RBTREE_DELETE_ELEM((*conn_ptr)->entries, entry_ptr);
    delete_connection_if_empty(anjay, conn_ptr);
}

void _anjay_observe_remove_entry(anjay_t *anjay,
                                 const anjay_observe_key_t *key) {
    AVS_RBTREE_ELEM(anjay_observe_connection_entry_t) conn =
            AVS_RBTREE_FIND(anjay->observe.connection_entries,
                            connection_query(&key->connection));
    if (conn) {
        AVS_RBTREE_ELEM(anjay_observe_entry_t) entry =
                AVS_RBTREE_FIND(conn->entries, entry_query(key));
        if (entry) {
            delete_entry(anjay, &conn, &entry);
        }
    }
}

void _anjay_observe_remove_by_msg_id(anjay_t *anjay,
                                     uint16_t notify_id) {
    AVS_RBTREE_ELEM(anjay_observe_connection_entry_t) conn;
    AVS_RBTREE_FOREACH(conn, anjay->observe.connection_entries) {
        AVS_RBTREE_ELEM(anjay_observe_entry_t) entry;
        AVS_RBTREE_FOREACH(entry, conn->entries) {
            uint16_t last_notify_id = newest_value(entry)->identity.msg_id;
            if (last_notify_id == notify_id) {
                delete_entry(anjay, &conn, &entry);
                return;
            }
        }
    }
}

typedef struct {
    anjay_active_server_info_t *active_server;
    anjay_inactive_server_info_t *inactive_server;
} ssid_iterator_t;

static ssid_iterator_t ssid_iterator_init(anjay_t *anjay) {
    return (const ssid_iterator_t) {
        anjay->servers.active, anjay->servers.inactive
    };
}

static const anjay_ssid_t *ssid_iterator_next(ssid_iterator_t *it) {
    const anjay_ssid_t *result = NULL;
    if (it->active_server
            && (!it->inactive_server
                    || it->active_server->ssid <= it->inactive_server->ssid)) {
        result = &it->active_server->ssid;
        it->active_server = AVS_LIST_NEXT(it->active_server);
    } else if (it->inactive_server) {
        result = &it->inactive_server->ssid;
        it->inactive_server = AVS_LIST_NEXT(it->inactive_server);
    }
    return result;
}

void _anjay_observe_gc(anjay_t *anjay) {
    AVS_RBTREE_ELEM(anjay_observe_connection_entry_t) conn =
            AVS_RBTREE_FIRST(anjay->observe.connection_entries);
    ssid_iterator_t it = ssid_iterator_init(anjay);
    const anjay_ssid_t *ssid_ptr;
    while (conn && (ssid_ptr = ssid_iterator_next(&it))) {
        while (conn && conn->key.ssid < *ssid_ptr) {
            AVS_RBTREE_ELEM(anjay_observe_connection_entry_t) to_remove = conn;
            conn = AVS_RBTREE_ELEM_NEXT(conn);
            delete_connection(anjay, &to_remove);
        }
        while (conn && conn->key.ssid == *ssid_ptr) {
            conn = AVS_RBTREE_ELEM_NEXT(conn);
        }
    }
    while (conn) {
        AVS_RBTREE_ELEM(anjay_observe_connection_entry_t) to_remove = conn;
        conn = AVS_RBTREE_ELEM_NEXT(conn);
        delete_connection(anjay, &to_remove);
    }
}

static bool notify_is_forced(const anjay_observe_resource_value_t *value,
                             const anjay_dm_attributes_t *attrs) {
    if (attrs->max_period >= 0) {
        struct timespec realtime_now;
        clock_gettime(CLOCK_REALTIME, &realtime_now);

        struct timespec since_update;
        _anjay_time_diff(&since_update, &realtime_now, &value->timestamp);
        return since_update.tv_sec >= attrs->max_period;
    }
    return false;
}

static bool check_range(const anjay_dm_attributes_t *attrs,
                        double value) {
    if (!isnan(attrs->greater_than)) {
        if (!isnan(attrs->less_than)) {
            if (attrs->less_than < attrs->greater_than) {
                return value < attrs->less_than || value > attrs->greater_than;
            } else {
                return value < attrs->less_than && value > attrs->greater_than;
            }
        } else {
            return value > attrs->greater_than;
        }
    } else {
        if (!isnan(attrs->less_than)) {
            return value < attrs->less_than;
        } else {
            return true;
        }
    }
}

static bool should_update(const anjay_observe_resource_value_t *previous,
                          const anjay_dm_attributes_t *attrs,
                          const anjay_msg_details_t *details,
                          double numeric,
                          const char *data,
                          size_t length) {
    if (details->format == previous->details.format
            && length == previous->value_length
            && memcmp(data, previous->value, length) == 0) {
        return false;
    }

    if (isnan(numeric) || (isnan(attrs->greater_than) && isnan(attrs->less_than)
            && isnan(attrs->step))) {
        return true;
    }

    if (!check_range(attrs, numeric)) {
        return false;
    }

    return (isnan(attrs->step) || isnan(previous->numeric)
            || fabs(numeric - previous->numeric) >= attrs->step);
}

static inline ssize_t read_new_value(anjay_t *anjay,
                                     const anjay_dm_object_def_t *const *obj,
                                     const anjay_observe_entry_t *entry,
                                     anjay_msg_details_t *out_details,
                                     double *out_numeric,
                                     char *buffer,
                                     size_t size) {
    return _anjay_dm_read_for_observe(
            anjay, obj,
            &(const anjay_dm_read_args_t) {
                .ssid = entry->key.connection.ssid,
                .oid = entry->key.oid,
                .has_iid = (entry->key.iid != ANJAY_IID_INVALID),
                .iid = entry->key.iid,
                .has_rid = (entry->key.rid >= 0),
                .rid = (anjay_rid_t) entry->key.rid,
                .requested_format = entry->key.format,
                .observe_serial = true
            }, out_details, out_numeric, buffer, size);
}

static avs_stream_abstract_t *
get_stream_by_ssid(anjay_t *anjay,
                   anjay_active_server_info_t **out_server,
                   anjay_ssid_t ssid,
                   anjay_connection_type_t conn_type) {
    *out_server = _anjay_servers_find_active(&anjay->servers, ssid);
    if (!*out_server) {
        return NULL;
    }
    anjay_connection_ref_t ref = {
        .server = *out_server,
        .conn_type = conn_type
    };
    avs_stream_abstract_t *stream = _anjay_get_server_stream(anjay, ref);
    assert(ref.conn_type == conn_type);
    return stream;
}

static bool should_use_confirmable(const struct timespec *realtime_now,
                                   const anjay_observe_entry_t *entry) {
    struct timespec since_confirmable;
    _anjay_time_diff(&since_confirmable,
                     realtime_now, &entry->last_confirmable);
    return (since_confirmable.tv_sec >= 24 * 60 * 60);
}

static anjay_observe_resource_value_t *
detach_first_unsent_value(anjay_observe_connection_entry_t *conn_state) {
    assert(conn_state->unsent);
    anjay_observe_entry_t *entry = conn_state->unsent->ref;
    if (entry->last_unsent == conn_state->unsent) {
        entry->last_unsent = NULL;
    }
    anjay_observe_resource_value_t *result =
            AVS_LIST_DETACH(&conn_state->unsent);
    if (conn_state->unsent_last == result) {
        assert(!conn_state->unsent);
        conn_state->unsent_last = NULL;
    }
    return result;
}

static void value_sent(anjay_observe_connection_entry_t *conn_state) {
    anjay_observe_resource_value_t *sent =
            detach_first_unsent_value(conn_state);
    anjay_observe_entry_t *entry = sent->ref;
    assert(AVS_LIST_SIZE(entry->last_sent) <= 1);
    AVS_LIST_CLEAR(&entry->last_sent);
    entry->last_sent = sent;
}

static int send_entry(anjay_t *anjay,
                      anjay_observe_connection_entry_t *conn_state) {
    anjay_active_server_info_t *server;
    avs_stream_abstract_t *stream =
            get_stream_by_ssid(anjay, &server,
                               conn_state->key.ssid, conn_state->key.type);
    if (!stream) {
        return -1;
    }
    int result;
    assert(conn_state->unsent);
    anjay_observe_entry_t *entry = conn_state->unsent->ref;
    const anjay_coap_msg_identity_t *id = &conn_state->unsent->identity;
    anjay_msg_details_t details = conn_state->unsent->details;
    anjay_coap_msg_identity_t notify_id;

    struct timespec realtime_now;
    clock_gettime(CLOCK_REALTIME, &realtime_now);
    if (should_use_confirmable(&realtime_now, entry)) {
        details.msg_type = ANJAY_COAP_MSG_CONFIRMABLE;
    }

    (void) ((result = _anjay_coap_stream_setup_request(
                    stream, &details, &id->token, id->token_size))
            || (result = avs_stream_write(stream, conn_state->unsent->value,
                                          conn_state->unsent->value_length))
            || (result = _anjay_coap_stream_get_request_identity(stream,
                                                                 &notify_id))
            || (result = avs_stream_finish_message(stream)));

    avs_stream_reset(stream);
    _anjay_release_server_stream(
            anjay,
            (anjay_connection_ref_t) { server, conn_state->key.type });

    if (!result) {
        if (details.msg_type == ANJAY_COAP_MSG_CONFIRMABLE) {
            entry->last_confirmable = realtime_now;
        }
        value_sent(conn_state);
        entry->last_sent->identity.msg_id = notify_id.msg_id;
    }
    return result;
}

typedef struct {
    bool server_active : 1;
    bool notification_storing_enabled : 1;
} observe_server_state_t;

static observe_server_state_t server_state(anjay_t *anjay, anjay_ssid_t ssid) {
    observe_server_state_t result = {
        .server_active = !!_anjay_servers_find_active(&anjay->servers, ssid),
        .notification_storing_enabled = true
    };

    anjay_iid_t server_iid;
    if (!_anjay_find_server_iid(anjay, ssid, &server_iid)) {
        const anjay_resource_path_t path = {
            ANJAY_DM_OID_SERVER,
            server_iid,
            ANJAY_DM_RID_SERVER_NOTIFICATION_STORING
        };
        bool storing;
        if (!_anjay_dm_res_read_bool(anjay, &path, &storing) && !storing) {
            // default value is true, use false only if explicitly set
            result.notification_storing_enabled = false;
        }
    }

    anjay_log(TRACE, "observe state for SSID %u: active %d, notification "
              "storing %d", ssid, result.server_active,
              result.notification_storing_enabled);
    return result;
}

static inline bool is_error_value(const anjay_observe_resource_value_t *value) {
    return _anjay_coap_msg_code_get_class(&value->details.msg_code) >= 4;
}

static void remove_all_unsent_values(anjay_observe_connection_entry_t *conn) {
    while (conn->unsent) {
        AVS_LIST(anjay_observe_resource_value_t) value =
                detach_first_unsent_value(conn);
        AVS_LIST_DELETE(&value);
    }
}

static int handle_send_queue_entry(anjay_t *anjay,
                                   anjay_observe_connection_entry_t *conn_state,
                                   observe_server_state_t observe_state) {
    assert(conn_state->unsent);
    assert(observe_state.server_active);
    bool is_error = is_error_value(conn_state->unsent);
    int result = send_entry(anjay, conn_state);
    if (result > 0) {
        anjay_log(INFO, "Reset received as reply to notification, result == %d",
                  result);
    } else if (result < 0) {
        anjay_log(ERROR, "Could not send Observe notification, result == %d",
                  result);
        if (!observe_state.notification_storing_enabled) {
            remove_all_unsent_values(conn_state);
        }
    }
    if (is_error
            && (result == 0 || !observe_state.notification_storing_enabled)) {
        result = 1;
    }
    return result;
}

static void schedule_all_triggers(anjay_t *anjay,
                                  anjay_observe_connection_entry_t *conn) {
    anjay_observe_key_t observe_key;
    memset(&observe_key, 0, sizeof(observe_key));
    observe_key.connection = conn->key;
    observe_key.rid = INT32_MIN;
    AVS_RBTREE_ELEM(anjay_observe_entry_t) entry;
    AVS_RBTREE_FOREACH(entry, conn->entries) {
        if (!entry->notify_task) {
            anjay_dm_attributes_t attrs;
            if (get_attrs(anjay, &attrs, &entry->key)
                    || schedule_trigger(anjay, entry, attrs.max_period)) {
                anjay_log(ERROR,
                          "Could not schedule automatic notification trigger");
            }
        }
    }
}

static int flush_send_queue(anjay_t *anjay, void *conn_) {
    anjay_observe_connection_entry_t *conn =
            (anjay_observe_connection_entry_t *) conn_;
    int result = 0;

    observe_server_state_t observe_state;
    bool observe_state_filled = false;

    while (result >= 0 && conn && conn->unsent) {
        anjay_observe_key_t key = conn->unsent->ref->key;
        if (!observe_state_filled) {
            observe_state = server_state(anjay, key.connection.ssid);
            observe_state_filled = true;
            if (!observe_state.server_active) {
                break;
            }
        }
        if ((result = handle_send_queue_entry(anjay, conn,
                                              observe_state)) > 0) {
            _anjay_observe_remove_entry(anjay, &key);
            // the above might've deleted the connection entry,
            // so we "re-find" it to check if it's still valid
            conn = AVS_RBTREE_FIND(anjay->observe.connection_entries,
                                   connection_query(&key.connection));
        }
    }
    if (result >= 0 && conn && !conn->unsent) {
        schedule_all_triggers(anjay, conn);
    }
    return result;
}

static int sched_flush_send_queue(anjay_t *anjay,
                                  anjay_observe_connection_entry_t *conn) {
    if (!conn || conn->flush_task) {
        anjay_log(TRACE, "skipping notification flush scheduling: %s",
                  !conn ? "no appropriate connection found"
                        : "flush task already scheduled");
        return 0;
    }
    if (_anjay_sched_now(anjay->sched, &conn->flush_task, flush_send_queue,
                         conn)) {
        anjay_log(ERROR, "Could not schedule notification flush");
        return -1;
    }
    return 0;
}

int _anjay_observe_sched_flush(anjay_t *anjay,
                               anjay_ssid_t ssid,
                               anjay_connection_type_t conn_type) {
    anjay_log(TRACE, "scheduling notifications flush for server SSID %u, "
              "connection type %d", ssid, conn_type);
    const anjay_observe_connection_key_t query_key = {
        .ssid = ssid,
        .type = conn_type
    };
    anjay_observe_connection_entry_t *conn =
            AVS_RBTREE_FIND(anjay->observe.connection_entries,
                            connection_query(&query_key));
    return sched_flush_send_queue(anjay, conn);
}

static int
update_notification_value(anjay_t *anjay,
                          anjay_observe_connection_entry_t *conn_state,
                          anjay_observe_entry_t *entry) {
    if (is_error_value(newest_value(entry))) {
        return 0;
    }

    const anjay_dm_object_def_t *const *obj;
    anjay_dm_attributes_t attrs;
    int result = get_obj_and_attrs(anjay, &obj, &attrs, &entry->key);
    if (result) {
        return result;
    }

    bool force = notify_is_forced(newest_value(entry), &attrs);
    char buf[ANJAY_MAX_OBSERVABLE_RESOURCE_SIZE];
    anjay_msg_details_t observe_details;
    double numeric = NAN;
    ssize_t size = read_new_value(anjay, obj, entry, &observe_details, &numeric,
                                  buf, sizeof(buf));
    if (size < 0) {
        return (int) size;
    }
    observe_details.msg_type = ANJAY_COAP_MSG_NON_CONFIRMABLE;

    if (force || should_update(newest_value(entry), &attrs, &observe_details,
                               numeric, buf, (size_t) size)) {
        result = insert_new_value(conn_state, entry, &observe_details,
                                  &newest_value(entry)->identity, numeric,
                                  buf, (size_t) size);
    }

    if (schedule_trigger(anjay, entry, attrs.max_period)) {
        anjay_log(ERROR, "Could not schedule automatic notification trigger");
    }

    return result;
}

static int trigger_observe(anjay_t *anjay, void *entry_) {
    anjay_observe_entry_t *entry = (anjay_observe_entry_t *) entry_;
    AVS_RBTREE_ELEM(anjay_observe_connection_entry_t) conn =
            AVS_RBTREE_FIND(anjay->observe.connection_entries,
                            connection_query(&entry->key.connection));
    assert(conn);
    observe_server_state_t state =
            server_state(anjay, entry->key.connection.ssid);
    if (!state.server_active && !state.notification_storing_enabled) {
        return 0;
    }

    int result = update_notification_value(anjay, conn, entry);
    if (result) {
        result = insert_error(anjay, conn, entry,
                              &newest_value(entry)->identity, result);
    }
    if (state.server_active) {
        int flush_result = sched_flush_send_queue(anjay, conn);
        if (!result) {
            result = flush_result;
        }
    }
    return result;
}

static inline int notify_entry(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj,
                               anjay_observe_entry_t *entry) {
    anjay_dm_attributes_t attrs = ANJAY_DM_ATTRIBS_EMPTY;
    int result = ensure_present(anjay, obj, entry->key.iid, entry->key.rid);
    if (result) {
        return result;
    }
    anjay_dm_attrs_query_details_t details = {
        .obj = obj,
        .iid = entry->key.iid,
        .rid = entry->key.rid,
        .ssid = entry->key.connection.ssid,
        .with_server_level_attrs = true
    };
    result = _anjay_dm_effective_attrs(anjay, &details, &attrs);
    if (result) {
        return result;
    }
    _anjay_sched_del(anjay->sched, &entry->notify_task);
    time_t period = attrs.min_period;
    if (period < 0) {
        period = 0;
    }
    return schedule_trigger(anjay, entry, period);
}

#ifdef ANJAY_TEST
#include "test/observe_mock.h"
#endif // ANJAY_TEST

static void update_retval(int *retval_ptr, int local_retval) {
    if (!*retval_ptr) {
        *retval_ptr = local_retval;
    }
}

static int observe_notify_bound(anjay_t *anjay,
                                anjay_observe_connection_entry_t *connection,
                                const anjay_observe_key_t *lower_bound,
                                const anjay_observe_key_t *upper_bound,
                                const anjay_dm_object_def_t *const *obj) {
    int retval = 0;
    AVS_RBTREE_ELEM(anjay_observe_entry_t) it =
            AVS_RBTREE_LOWER_BOUND(connection->entries,
                                   entry_query(lower_bound));
    AVS_RBTREE_ELEM(anjay_observe_entry_t) end =
            AVS_RBTREE_UPPER_BOUND(connection->entries,
                                   entry_query(upper_bound));
    for (; it != end; it = AVS_RBTREE_ELEM_NEXT(it)) {
        update_retval(&retval, notify_entry(anjay, obj, it));
    }
    return retval;
}

static int
observe_notify_wildcard_impl(anjay_t *anjay,
                             anjay_observe_connection_entry_t *connection,
                             const anjay_observe_key_t *specimen_key,
                             const anjay_dm_object_def_t *const *obj,
                             bool iid_wildcard) {
    anjay_observe_key_t lower_bound = *specimen_key;
    anjay_observe_key_t upper_bound = *specimen_key;
    lower_bound.format = 0;
    upper_bound.format = UINT16_MAX;
    if (iid_wildcard) {
        lower_bound.iid = ANJAY_IID_INVALID;
        upper_bound.iid = ANJAY_IID_INVALID;
    }
    lower_bound.rid = -1;
    upper_bound.rid = -1;
    return observe_notify_bound(anjay, connection,
                                &lower_bound, &upper_bound, obj);
}

static inline int
observe_notify_iid_wildcard(anjay_t *anjay,
                            anjay_observe_connection_entry_t *connection,
                            const anjay_observe_key_t *specimen_key,
                            const anjay_dm_object_def_t *const *obj) {
    return observe_notify_wildcard_impl(anjay, connection,
                                        specimen_key, obj, true);
}

static inline int
observe_notify_rid_wildcard(anjay_t *anjay,
                            anjay_observe_connection_entry_t *connection,
                            const anjay_observe_key_t *specimen_key,
                            const anjay_dm_object_def_t *const *obj) {
    return observe_notify_wildcard_impl(anjay, connection,
                                        specimen_key, obj, false);
}

/**
 * Calls <c>notify_entry()</c> on all registered Observe entries that match
 * <c>key</c>.
 *
 * This is harder than may seem at the first glance, because both <c>key</c>
 * (the query) and keys of the registered Observe entries may contain wildcards.
 *
 * An observation may be registered for either of:
 * - A whole object (OID)
 * - A whole object instance (OID+IID)
 * - A specific resource (OID+IID+RID)
 * Each of those may also have either explicit or implicit Content-Format, so in
 * the end, there are six types of observation entry keys:
 * - OID
 * - OID+format
 * - OID+IID
 * - OID+IID+format
 * - OID+IID+RID
 * - OID+IID+RID+format
 *
 * The query is guaranteed to never have an explicit Content-Format
 * specification (and we <c>assert()</c> that), but still, we have three
 * possible types of those:
 * - OID
 * - OID+IID
 * - OID+IID+RID
 *
 * Each of these cases needs to be addressed in a slightly different manner.
 *
 * Wildcard representation
 * -----------------------
 * A wildcard for IID is represented as the number 65535. A wildcard for RID is
 * represented as the number -1. The registered observation entries are stored
 * in a sorted tree, with the sort key being (SSID, conn_type, OID, IID, RID,
 * Content-Format) - in lexicographical order over all elements of that tuple -
 * much like C++11's <c>std::tuple</c> comparison operators.
 *
 * Querying for just OID
 * ---------------------
 * It is sufficient to search for the whole range of possible keys that match
 * (SSID, conn_type, OID). We will find all entries, including those registered
 * for OID, OID+IID and OID+IID+RID.
 *
 * So the lower bound for search is (SSID, conn_type, OID, 0, I32_MIN, 0) and
 * the upper bound is (SSID, conn_type, OID, U16_MAX, I32_MAX, U16_MAX). All
 * entries within this inclusive range will be notified.
 *
 * Querying for OID+IID
 * --------------------
 * With the fixed IID, in a similar manner, we set the lower bound for search to
 * (SSID, conn_type, OID, IID, I32_MIN, 0) and the upper bound to
 * (SSID, conn_type, OID, IID, I32_MAX, U16_MAX). This covers entries registered
 * for OID+IID and OID+IID+RID keys, but the entries registered on a wildcard
 * IID will get omitted, as 65535 is not equal to the specified IID.
 *
 * Because of this, we need to call notification on an additional range with the
 * lower bound set to (SSID, conn_type, OID, 65535, I32_MIN, 0) and the upper
 * bound to (SSID, conn_type, OID, 65535, I32_MAX, U16_MAX).
 *
 * Querying for OID+IID+RID
 * ------------------------
 * Similarly, the natural query for OID+IID+RID, with the lower bound set to
 * (SSID, conn_type, OID, IID, RID, 0) and the upper bound to
 * (SSID, conn_type, OID, IID, RID, U16_MAX), will miss all the wildcards.
 *
 * We also need to notify the OID+IID entries (with wildcard RID), so we do
 * another search, with the lower bound at (SSID, conn_type, OID, IID, -1, 0)
 * and the upper bound at (SSID, conn_type, OID, IID, -1, U16_MAX).
 *
 * We also need to notify the OID entries (with wildcard IID and RID), so we do
 * yet another search, with lower bound at (SSID, conn_type, OID, 65535, -1, 0)
 * and the upper bound at (SSID, conn_type, OID, 65535, -1, U16_MAX).
 */
static int observe_notify(anjay_t *anjay,
                          anjay_observe_connection_entry_t *connection,
                          const anjay_observe_key_t *key,
                          const anjay_dm_object_def_t *const *obj) {
    assert(key->format == ANJAY_COAP_FORMAT_NONE);
    assert(obj && *obj && (*obj)->oid == key->oid);
    assert(key->rid >= -1 && key->rid <= UINT16_MAX);

    int retval = 0;

    anjay_observe_key_t lower_bound = *key;
    anjay_observe_key_t upper_bound = *key;
    lower_bound.format = 0;
    upper_bound.format = UINT16_MAX;
    if (key->rid < 0) {
        lower_bound.rid = INT32_MIN;
        upper_bound.rid = INT32_MAX;
        if (key->iid == ANJAY_IID_INVALID) {
            lower_bound.iid = 0;
            upper_bound.iid = ANJAY_IID_INVALID;
        } else {
            update_retval(&retval,
                          observe_notify_iid_wildcard(anjay, connection,
                                                      key, obj));
        }
    } else {
        update_retval(&retval, observe_notify_rid_wildcard(anjay, connection,
                                                           key, obj));
        update_retval(&retval, observe_notify_iid_wildcard(anjay, connection,
                                                           key, obj));
    }

    update_retval(&retval, observe_notify_bound(anjay, connection, &lower_bound,
                                                &upper_bound, obj));
    return retval;
}

int _anjay_observe_notify(anjay_t *anjay,
                          const anjay_observe_key_t *key,
                          bool invert_server_match) {
    assert(key->format == ANJAY_COAP_FORMAT_NONE);
    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, key->oid);
    if (!obj || !*obj) {
        return -1;
    }

    // iterate through all SSIDs we have
    int result = 0;
    anjay_observe_key_t modified_key = *key;
    AVS_RBTREE_ELEM(anjay_observe_connection_entry_t) connection;
    AVS_RBTREE_FOREACH(connection, anjay->observe.connection_entries) {
        if ((connection->key.ssid == key->connection.ssid)
                == invert_server_match) {
            continue;
        }
        modified_key.connection = connection->key;
        update_retval(&result, observe_notify(anjay, connection,
                                              &modified_key, obj));
    }
    return result;
}

#ifdef ANJAY_TEST
#include "test/observe.c"
#endif // ANJAY_TEST
