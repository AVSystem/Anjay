/*
 * Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
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

#include <anjay_config.h>

#include <inttypes.h>
#include <math.h>

#include <avsystem/commons/stream_v_table.h>

#include <anjay_modules/time_defs.h>

#include "../anjay_core.h"
#include "../coap/content_format.h"
#include "../dm/query.h"
#include "../servers_utils.h"

#include "observe_internal.h"

VISIBILITY_SOURCE_BEGIN

static inline const anjay_observe_connection_entry_t *
connection_query(const anjay_connection_key_t *key) {
    return AVS_CONTAINER_OF(key, anjay_observe_connection_entry_t, key);
}

static int connection_key_cmp(const anjay_connection_key_t *left,
                              const anjay_connection_key_t *right) {
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

int _anjay_observe_key_cmp(const anjay_observe_key_t *left,
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

int _anjay_observe_entry_cmp(const void *left, const void *right) {
    return _anjay_observe_key_cmp(
            &((const anjay_observe_entry_t *) left)->key,
            &((const anjay_observe_entry_t *) right)->key);
}

int _anjay_observe_init(anjay_observe_state_t *observe,
                        bool confirmable_notifications,
                        size_t stored_notification_limit) {
    if (!(observe->connection_entries = AVS_RBTREE_NEW(
                  anjay_observe_connection_entry_t, connection_state_cmp))) {
        anjay_log(ERROR, "Could not initialize Observe structures");
        return -1;
    }
    observe->confirmable_notifications = confirmable_notifications;

    if (stored_notification_limit == 0) {
        observe->notify_queue_limit_mode = NOTIFY_QUEUE_UNLIMITED;
    } else {
        observe->notify_queue_limit = stored_notification_limit;
        observe->notify_queue_limit_mode = NOTIFY_QUEUE_DROP_OLDEST;
    }

    return 0;
}

void _anjay_observe_cleanup_connection(anjay_sched_t *sched,
                                       anjay_observe_connection_entry_t *conn) {
    /*
     * Usually, we wouldn't bother checking if the scheduler task handles are
     * NULL, as _anjay_sched_del handles them just fine. This function is
     * extremely useful during cleanup after failed persistence restore
     * operation, in which case both @p sched and all scheduler task handles
     * must be NULL.
     */
    AVS_RBTREE_DELETE(&conn->entries) {
        if ((*conn->entries)->notify_task) {
            _anjay_sched_del(sched, &(*conn->entries)->notify_task);
        }
        AVS_LIST_CLEAR(&(*conn->entries)->last_sent);
    }
    if (conn->flush_task) {
        _anjay_sched_del(sched, &conn->flush_task);
    }
    AVS_LIST_CLEAR(&conn->unsent);
}

void _anjay_observe_cleanup(anjay_observe_state_t *observe,
                            anjay_sched_t *sched) {
    AVS_RBTREE_DELETE(&observe->connection_entries) {
        _anjay_observe_cleanup_connection(sched, *observe->connection_entries);
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
        .outbuf = {
            .vtable = &vtable
        }
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

static void
delete_connection(anjay_t *anjay,
                  AVS_RBTREE_ELEM(anjay_observe_connection_entry_t) *conn_ptr) {
    _anjay_observe_cleanup_connection(anjay->sched, *conn_ptr);
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

static void trigger_observe(anjay_t *anjay, const void *entry_ptr);

static const anjay_observe_resource_value_t *
newest_value(const anjay_observe_entry_t *entry) {
    if (entry->last_unsent) {
        return entry->last_unsent;
    } else {
        assert(entry->last_sent);
        return entry->last_sent;
    }
}

static int
schedule_trigger(anjay_t *anjay, anjay_observe_entry_t *entry, int32_t period) {
    if (period < 0) {
        return 0;
    }

    avs_time_duration_t delay =
            avs_time_real_diff(newest_value(entry)->timestamp,
                               avs_time_real_now());
    delay = avs_time_duration_add(
            delay, avs_time_duration_from_scalar(period, AVS_TIME_S));
    if (avs_time_duration_less(delay, AVS_TIME_DURATION_ZERO)) {
        delay = AVS_TIME_DURATION_ZERO;
    }

    anjay_log(TRACE,
              "Notify %s (format %" PRIu16 ", SSID %" PRIu16 ", "
              "connection type %d) scheduled: +%ld.%09lds",
              ANJAY_DEBUG_MAKE_PATH(&MAKE_INSTANCE_OR_RESOURCE_PATH(
                      entry->key.oid, entry->key.iid, entry->key.rid)),
              entry->key.format, entry->key.connection.ssid,
              (int) entry->key.connection.type, (long) delay.seconds,
              (long) delay.nanoseconds);

    _anjay_sched_del(anjay->sched, &entry->notify_task);

    int retval = _anjay_sched(anjay->sched, &entry->notify_task, delay,
                              trigger_observe, &entry, sizeof(entry));
    if (retval) {
        anjay_log(ERROR,
                  "Could not schedule automatic notification trigger, result: "
                  "%d",
                  retval);
    }
    return retval;
}

static AVS_LIST(anjay_observe_resource_value_t)
create_resource_value(const anjay_msg_details_t *details,
                      anjay_observe_entry_t *ref,
                      const avs_coap_msg_identity_t *identity,
                      double numeric,
                      const void *data,
                      size_t size) {
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
    result->timestamp = avs_time_real_now();
    return result;
}

static size_t count_queued_notifications(const anjay_observe_state_t *observe) {
    size_t count = 0;

    AVS_RBTREE_ELEM(anjay_observe_connection_entry_t) conn;
    AVS_RBTREE_FOREACH(conn, observe->connection_entries) {
        count += AVS_LIST_SIZE(conn->unsent);
    }

    return count;
}

static bool is_observe_queue_full(const anjay_observe_state_t *observe) {
    if (observe->notify_queue_limit_mode == NOTIFY_QUEUE_UNLIMITED) {
        return false;
    }

    size_t num_queued = count_queued_notifications(observe);
    anjay_log(TRACE, "%u/%u queued notifications", (unsigned) num_queued,
              (unsigned) observe->notify_queue_limit);

    assert(num_queued <= observe->notify_queue_limit);
    return num_queued >= observe->notify_queue_limit;
}

static AVS_LIST(anjay_observe_connection_entry_t)
find_oldest_queued_notification(anjay_observe_state_t *observe) {
    AVS_LIST(anjay_observe_connection_entry_t) oldest = NULL;

    AVS_RBTREE_ELEM(anjay_observe_connection_entry_t) conn;
    AVS_RBTREE_FOREACH(conn, observe->connection_entries) {
        if (conn->unsent) {
            if (!oldest
                    || avs_time_real_before(conn->unsent->timestamp,
                                            oldest->unsent->timestamp)) {
                oldest = conn;
            }
        }
    }

    return oldest;
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

static void drop_oldest_queued_notification(anjay_observe_state_t *observe) {
    AVS_LIST(anjay_observe_connection_entry_t) oldest =
            find_oldest_queued_notification(observe);

    AVS_ASSERT(oldest, "function is not supposed to be called when there are "
                       "no queued notifications");

    anjay_observe_resource_value_t *entry = detach_first_unsent_value(oldest);
    AVS_LIST_DELETE(&entry);
}

static int insert_new_value(anjay_observe_state_t *observe,
                            anjay_observe_connection_entry_t *conn_state,
                            anjay_observe_entry_t *entry,
                            const anjay_msg_details_t *details,
                            const avs_coap_msg_identity_t *identity,
                            double numeric,
                            const void *data,
                            size_t size) {
    if (is_observe_queue_full(observe)) {
        switch (observe->notify_queue_limit_mode) {
        case NOTIFY_QUEUE_UNLIMITED:
            AVS_UNREACHABLE("is_observe_queue_full broken");
            return -1;

        case NOTIFY_QUEUE_DROP_OLDEST:
            assert(observe->notify_queue_limit != 0);
            drop_oldest_queued_notification(observe);
            break;
        }
    }

    AVS_LIST(anjay_observe_resource_value_t) res_value =
            create_resource_value(details, entry, identity, numeric, data,
                                  size);
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
                        const avs_coap_msg_identity_t *identity,
                        int outer_result) {
    _anjay_sched_del(anjay->sched, &entry->notify_task);
    const anjay_msg_details_t details = {
        .msg_type = AVS_COAP_MSG_CONFIRMABLE,
        .msg_code = _anjay_make_error_response_code(outer_result),
        .format = AVS_COAP_FORMAT_NONE
    };
    return insert_new_value(&anjay->observe, conn_state, entry, &details,
                            identity, NAN, NULL, 0);
}

static int get_effective_attrs(anjay_t *anjay,
                               anjay_dm_internal_res_attrs_t *out_attrs,
                               const anjay_dm_object_def_t *const *obj,
                               const anjay_observe_key_t *key) {
    assert(!obj || !*obj || (*obj)->oid == key->oid);
    anjay_dm_attrs_query_details_t details = {
        .obj = obj,
        .iid = key->iid,
        .rid = key->rid,
        .ssid = key->connection.ssid,
        .with_server_level_attrs = true
    };

    // Some of the details above may be invalid, e.g. when the object, instance
    // or resource are no longer valid. Here we sanitize the details so that if
    // some component is invalid, all lower-level path components are also
    // invalid. This is so that <c>_anjay_dm_effective_attrs()</c> will return
    // the appropriate defaults.
    if (!obj || !*obj) {
        // if object is invalid, any instance is invalid
        details.iid = ANJAY_IID_INVALID;
    }
    if (details.iid != ANJAY_IID_INVALID
            && _anjay_dm_map_present_result(_anjay_dm_instance_present(
                       anjay, obj, details.iid, NULL))) {
        // instance is no longer present, use invalid instead
        details.iid = ANJAY_IID_INVALID;
    }
    if (details.iid == ANJAY_IID_INVALID) {
        // if instance is invalid, any resource is invalid
        details.rid = -1;
    }
    if (details.rid >= 0
            && _anjay_dm_map_present_result(
                       _anjay_dm_resource_supported_and_present(
                               anjay, obj, key->iid, (anjay_rid_t) details.rid,
                               NULL))) {
        // if resource is no longer present, use invalid instead
        details.rid = -1;
    }
    return _anjay_dm_effective_attrs(anjay, &details, out_attrs);
}

static inline int get_attrs(anjay_t *anjay,
                            anjay_dm_internal_res_attrs_t *out_attrs,
                            const anjay_observe_key_t *key) {
    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, key->oid);
    return get_effective_attrs(anjay, out_attrs, obj, key);
}

static inline bool is_pmax_valid(anjay_dm_attributes_t attr) {
    if (attr.max_period < 0) {
        return false;
    }

    if (attr.max_period == 0 || attr.max_period < attr.min_period) {
        anjay_log(DEBUG,
                  "invalid pmax (%" PRIi32
                  "); expected pmax > 0 && pmax >= pmin (%" PRIi32 ")",
                  attr.max_period, attr.min_period);
        return false;
    }

    return true;
}

int _anjay_observe_schedule_pmax_trigger(anjay_t *anjay,
                                         anjay_observe_entry_t *entry) {
    anjay_dm_internal_res_attrs_t attrs;
    int result;

    if ((result = get_attrs(anjay, &attrs, &entry->key))) {
        anjay_log(DEBUG, "Could not get observe attributes, result: %d",
                  result);
        return result;
    }

    if (is_pmax_valid(attrs.standard.common)) {
        result = schedule_trigger(anjay, entry,
                                  attrs.standard.common.max_period);
    }

    return result;
}

static int insert_initial_value(anjay_t *anjay,
                                anjay_observe_connection_entry_t *conn_state,
                                anjay_observe_entry_t *entry,
                                const anjay_msg_details_t *details,
                                const avs_coap_msg_identity_t *identity,
                                double numeric,
                                const void *data,
                                size_t size) {
    assert(!entry->last_sent);
    assert(!entry->last_unsent);

    avs_time_real_t now = avs_time_real_now();

    int result = -1;
    // we assume that the initial value should be treated as sent,
    // even though we haven't actually sent it ourselves
    if ((entry->last_sent = create_resource_value(details, entry, identity,
                                                  numeric, data, size))
            && !(result = _anjay_observe_schedule_pmax_trigger(anjay, entry))) {
        entry->last_confirmable = now;
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
                                const anjay_connection_key_t *key) {
    AVS_RBTREE_ELEM(anjay_observe_connection_entry_t) conn =
            AVS_RBTREE_FIND(anjay->observe.connection_entries,
                            connection_query(key));
    if (!conn) {
        conn = AVS_RBTREE_ELEM_NEW(anjay_observe_connection_entry_t);
        if (!conn
                || !(conn->entries =
                             AVS_RBTREE_NEW(anjay_observe_entry_t,
                                            _anjay_observe_entry_cmp))) {
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
                             const avs_coap_msg_identity_t *identity,
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
                AVS_RBTREE_FIND(conn->entries, _anjay_observe_entry_query(key));
        if (entry) {
            delete_entry(anjay, &conn, &entry);
        }
    }
}

void _anjay_observe_remove_by_msg_id(anjay_t *anjay, uint16_t notify_id) {
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

static int
observe_gc_ssid_iterate(anjay_t *anjay, anjay_ssid_t ssid, void *conn_ptr_) {
    AVS_RBTREE_ELEM(anjay_observe_connection_entry_t) *conn_ptr =
            (AVS_RBTREE_ELEM(anjay_observe_connection_entry_t) *) conn_ptr_;
    while (*conn_ptr && (*conn_ptr)->key.ssid < ssid) {
        AVS_RBTREE_ELEM(anjay_observe_connection_entry_t) to_remove = *conn_ptr;
        *conn_ptr = AVS_RBTREE_ELEM_NEXT(*conn_ptr);
        delete_connection(anjay, &to_remove);
    }
    while (*conn_ptr && (*conn_ptr)->key.ssid == ssid) {
        *conn_ptr = AVS_RBTREE_ELEM_NEXT(*conn_ptr);
    }
    return 0;
}

void _anjay_observe_gc(anjay_t *anjay) {
    AVS_RBTREE_ELEM(anjay_observe_connection_entry_t) conn =
            AVS_RBTREE_FIRST(anjay->observe.connection_entries);
    _anjay_servers_foreach_ssid(anjay, observe_gc_ssid_iterate, &conn);
    while (conn) {
        AVS_RBTREE_ELEM(anjay_observe_connection_entry_t) to_remove = conn;
        conn = AVS_RBTREE_ELEM_NEXT(conn);
        delete_connection(anjay, &to_remove);
    }
}

static bool has_pmax_expired(const anjay_observe_resource_value_t *value,
                             const anjay_dm_attributes_t *attrs) {
    return is_pmax_valid(*attrs)
           && avs_time_real_diff(avs_time_real_now(), value->timestamp).seconds
                      >= attrs->max_period;
}

static bool process_step(const anjay_observe_resource_value_t *previous,
                         const anjay_dm_resource_attributes_t *attrs,
                         double value) {
    return !isnan(attrs->step)
           && fabs(value - previous->numeric) >= attrs->step;
}

static bool process_ltgt(const anjay_observe_resource_value_t *previous,
                         double threshold,
                         double value) {
    return !isnan(threshold)
           && ((previous->numeric <= threshold && value > threshold)
               || (previous->numeric >= threshold && value < threshold));
}

static bool should_update(const anjay_observe_resource_value_t *previous,
                          const anjay_dm_resource_attributes_t *attrs,
                          const anjay_msg_details_t *details,
                          double numeric,
                          const char *data,
                          size_t length) {
    if (details->format == previous->details.format
            && length == previous->value_length
            && memcmp(data, previous->value, length) == 0) {
        return false;
    }

    if (isnan(numeric) || isnan(previous->numeric)
            || (isnan(attrs->greater_than) && isnan(attrs->less_than)
                && isnan(attrs->step))) {
        // either previous or current value is not numeric, or none of lt/gt/st
        // attributes are set - notifying each value change
        return true;
    }

    return process_step(previous, attrs, numeric)
           || process_ltgt(previous, attrs->less_than, numeric)
           || process_ltgt(previous, attrs->greater_than, numeric);
}

static inline ssize_t read_new_value(anjay_t *anjay,
                                     const anjay_dm_object_def_t *const *obj,
                                     const anjay_observe_entry_t *entry,
                                     anjay_msg_details_t *out_details,
                                     double *out_numeric,
                                     char *buffer,
                                     size_t size) {
    anjay_uri_path_type_t path_type = ANJAY_PATH_OBJECT;
    if (entry->key.rid >= 0) {
        path_type = ANJAY_PATH_RESOURCE;
    } else if (entry->key.iid != ANJAY_IID_INVALID) {
        path_type = ANJAY_PATH_INSTANCE;
    }
    return _anjay_dm_read_for_observe(
            anjay, obj,
            &(const anjay_dm_read_args_t) {
                .ssid = entry->key.connection.ssid,
                .uri = {
                    .oid = entry->key.oid,
                    .iid = entry->key.iid,
                    .rid = (anjay_rid_t) entry->key.rid,
                    .type = path_type
                },
                .requested_format = entry->key.format,
                .observe_serial = true
            },
            out_details, out_numeric, buffer, size);
}

static bool confirmable_required(const avs_time_real_t now,
                                 const anjay_observe_entry_t *entry) {
    return !avs_time_duration_less(
            avs_time_real_diff(now, entry->last_confirmable),
            avs_time_duration_from_scalar(1, AVS_TIME_DAY));
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
    assert(conn_state->unsent);
    anjay_observe_entry_t *entry = conn_state->unsent->ref;
    const avs_coap_msg_identity_t *id = &conn_state->unsent->identity;
    anjay_msg_details_t details = conn_state->unsent->details;
    avs_coap_msg_identity_t notify_id;

    avs_time_real_t now = avs_time_real_now();
    if (details.msg_type != AVS_COAP_MSG_CONFIRMABLE
            && confirmable_required(now, entry)) {
        details.msg_type = AVS_COAP_MSG_CONFIRMABLE;
    }

    int result;
    (void) ((result = _anjay_coap_stream_setup_request(anjay->comm_stream,
                                                       &details, &id->token))
            || (result = avs_stream_write(anjay->comm_stream,
                                          conn_state->unsent->value,
                                          conn_state->unsent->value_length))
            || (result = _anjay_coap_stream_get_request_identity(
                        anjay->comm_stream, &notify_id))
            || (result = avs_stream_finish_message(anjay->comm_stream)));

    if (!result) {
        if (details.msg_type == AVS_COAP_MSG_CONFIRMABLE) {
            entry->last_confirmable = now;
        }
        value_sent(conn_state);
        entry->last_sent->identity.msg_id = notify_id.msg_id;
    }
    return result;
}

static bool notification_storing_enabled(anjay_t *anjay, anjay_ssid_t ssid) {
    anjay_iid_t server_iid;
    if (!_anjay_find_server_iid(anjay, ssid, &server_iid)) {
        const anjay_uri_path_t path =
                MAKE_RESOURCE_PATH(ANJAY_DM_OID_SERVER, server_iid,
                                   ANJAY_DM_RID_SERVER_NOTIFICATION_STORING);
        bool storing;
        if (!_anjay_dm_res_read_bool(anjay, &path, &storing) && !storing) {
            // default value is true, use false only if explicitly set
            return false;
        }
    }
    return true;
}

typedef struct {
    anjay_connection_ref_t ref;
    bool server_active;
    bool notification_storing_enabled;
} observe_conn_state_t;

static observe_conn_state_t conn_state(anjay_t *anjay,
                                       const anjay_connection_key_t *key) {
    observe_conn_state_t result = {
        .ref = {
            .conn_type = key->type
        },
        .notification_storing_enabled =
                notification_storing_enabled(anjay, key->ssid)
    };

    if (!anjay_is_offline(anjay)
            && (result.ref.server =
                        _anjay_servers_find_active(anjay, key->ssid))) {
        // It is now possible for the socket to exist and be connected even
        // though the server has no valid registration. This may happen during
        // the _anjay_connection_internal_bring_online() backoff. We don't want
        // to send notifications if we don't have a valid registration, so we
        // treat such server as inactive for notification purposes.
        result.server_active =
                !_anjay_server_registration_expired(result.ref.server);
    }

    anjay_log(TRACE,
              "observe state for SSID %u: active %d, notification storing %d",
              key->ssid, result.server_active,
              result.notification_storing_enabled);
    return result;
}

static inline bool is_error_value(const anjay_observe_resource_value_t *value) {
    return avs_coap_msg_code_get_class(value->details.msg_code) >= 4;
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
                                   const observe_conn_state_t *observe_state) {
    assert(conn_state->unsent);
    assert(observe_state->server_active);
    bool is_error = is_error_value(conn_state->unsent);
    int result = send_entry(anjay, conn_state);
    if (result > 0) {
        anjay_log(INFO, "Reset received as reply to notification, result == %d",
                  result);
    } else if (result < 0) {
        anjay_log(ERROR, "Could not send Observe notification, result == %d",
                  result);
        if (result != AVS_COAP_CTX_ERR_NETWORK
                && result != AVS_COAP_CTX_ERR_TIMEOUT
                && !observe_state->notification_storing_enabled) {
            remove_all_unsent_values(conn_state);
        }
    }
    if (is_error && result != AVS_COAP_CTX_ERR_NETWORK
            && result != AVS_COAP_CTX_ERR_TIMEOUT
            && (result == 0 || !observe_state->notification_storing_enabled)) {
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
            _anjay_observe_schedule_pmax_trigger(anjay, entry);
        }
    }
}

static void flush_send_queue(anjay_t *anjay,
                             anjay_observe_connection_entry_t *conn,
                             const observe_conn_state_t *observe_state) {
    assert(conn);
    assert(observe_state);
    assert(observe_state->ref.server);
    assert(observe_state->server_active);

    int result = _anjay_bind_server_stream(anjay, observe_state->ref);
    assert(result == 0);

    while (result >= 0 && conn && conn->unsent) {
        anjay_observe_key_t key = conn->unsent->ref->key;
        if ((result = handle_send_queue_entry(anjay, conn, observe_state))
                > 0) {
            _anjay_observe_remove_entry(anjay, &key);
            // the above might've deleted the connection entry,
            // so we "re-find" it to check if it's still valid
            conn = AVS_RBTREE_FIND(anjay->observe.connection_entries,
                                   connection_query(&key.connection));
        }
    }

    _anjay_release_server_stream(anjay);

    if (result >= 0 && conn && !conn->unsent) {
        schedule_all_triggers(anjay, conn);
    } else if (result == AVS_COAP_CTX_ERR_NETWORK) {
        anjay_log(ERROR, "network communication error while sending Notify");
        if (observe_state->ref.conn_type
                == _anjay_server_primary_conn_type(observe_state->ref.server)) {
            _anjay_server_on_server_communication_error(
                    anjay, observe_state->ref.server);
        }
    }
}

static void flush_send_queue_job(anjay_t *anjay, const void *conn_ptr) {
    anjay_observe_connection_entry_t *conn =
            *(anjay_observe_connection_entry_t *const *) conn_ptr;
    if (conn && conn->unsent) {
        observe_conn_state_t observe_state =
                conn_state(anjay, &conn->unsent->ref->key.connection);
        if (observe_state.server_active
                && _anjay_connection_get_online_socket(observe_state.ref)) {
            flush_send_queue(anjay, conn, &observe_state);
        }
    }
}

int _anjay_observe_sched_flush_current_connection(anjay_t *anjay) {
    const anjay_connection_key_t query_key = {
        .ssid = _anjay_dm_current_ssid(anjay),
        .type = anjay->current_connection.conn_type
    };
    return _anjay_observe_sched_flush(anjay, query_key);
}

int _anjay_observe_sched_flush(anjay_t *anjay, anjay_connection_key_t key) {
    anjay_log(TRACE,
              "scheduling notifications flush for server SSID %u, "
              "connection type %d",
              key.ssid, key.type);
    anjay_observe_connection_entry_t *conn =
            AVS_RBTREE_FIND(anjay->observe.connection_entries,
                            connection_query(&key));
    if (!conn || conn->flush_task) {
        anjay_log(TRACE, "skipping notification flush scheduling: %s",
                  !conn ? "no appropriate connection found"
                        : "flush task already scheduled");
        return 0;
    }
    if (_anjay_sched_now(anjay->sched, &conn->flush_task, flush_send_queue_job,
                         &conn, sizeof(conn))) {
        anjay_log(WARNING, "Could not schedule notification flush");
        return -1;
    }
    return 0;
}

static int
update_notification_value(anjay_t *anjay,
                          anjay_observe_connection_entry_t *conn_state,
                          anjay_observe_entry_t *entry) {
    if (is_error_value(newest_value(entry))) {
        return 0;
    }

    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, entry->key.oid);
    if (!obj) {
        return ANJAY_ERR_NOT_FOUND;
    }

    anjay_dm_internal_res_attrs_t attrs;
    int result = get_effective_attrs(anjay, &attrs, obj, &entry->key);
    if (result) {
        return result;
    }

    bool pmax_expired =
            has_pmax_expired(newest_value(entry), &attrs.standard.common);
    char buf[ANJAY_MAX_OBSERVABLE_RESOURCE_SIZE];
    anjay_msg_details_t observe_details;
    double numeric = NAN;
    ssize_t size = read_new_value(anjay, obj, entry, &observe_details, &numeric,
                                  buf, sizeof(buf));
    if (size < 0) {
        return (int) size;
    }
#ifdef WITH_CON_ATTR
    if (attrs.custom.data.con >= 0) {
        observe_details.msg_type = (attrs.custom.data.con > 0)
                                           ? AVS_COAP_MSG_CONFIRMABLE
                                           : AVS_COAP_MSG_NON_CONFIRMABLE;
    } else
#endif // WITH_CON_ATTR
    {
        observe_details.msg_type = anjay->observe.confirmable_notifications
                                           ? AVS_COAP_MSG_CONFIRMABLE
                                           : AVS_COAP_MSG_NON_CONFIRMABLE;
    }

    if (pmax_expired
            || should_update(newest_value(entry), &attrs.standard,
                             &observe_details, numeric, buf, (size_t) size)) {
        result = insert_new_value(&anjay->observe, conn_state, entry,
                                  &observe_details,
                                  &newest_value(entry)->identity, numeric, buf,
                                  (size_t) size);
    }

    if (is_pmax_valid(attrs.standard.common)) {
        schedule_trigger(anjay, entry, attrs.standard.common.max_period);
    }

    return result;
}

static void trigger_observe(anjay_t *anjay, const void *entry_ptr) {
    anjay_observe_entry_t *entry = *(anjay_observe_entry_t *const *) entry_ptr;
    AVS_RBTREE_ELEM(anjay_observe_connection_entry_t) conn =
            AVS_RBTREE_FIND(anjay->observe.connection_entries,
                            connection_query(&entry->key.connection));
    assert(conn);
    observe_conn_state_t state = conn_state(anjay, &entry->key.connection);
    if (!state.server_active && !state.notification_storing_enabled) {
        return;
    }

    int result = update_notification_value(anjay, conn, entry);
    if (result) {
        insert_error(anjay, conn, entry, &newest_value(entry)->identity,
                     result);
    }
    if (state.server_active && conn->unsent) {
        _anjay_sched_del(anjay->sched, &conn->flush_task);
        assert(!conn->flush_task);
        assert(state.ref.server);
        if (_anjay_connection_get_online_socket(state.ref)) {
            flush_send_queue(anjay, conn, &state);
        } else if (_anjay_connection_current_mode(state.ref)
                   == ANJAY_CONNECTION_QUEUE) {
            _anjay_connection_bring_online(anjay, state.ref);
            // once the connection is up, _anjay_observe_sched_flush()
            // will be called; we're done here
        } else if (!state.notification_storing_enabled) {
            remove_all_unsent_values(conn);
        }
    }
}

static int32_t get_min_period(anjay_t *anjay, const anjay_observe_key_t *key) {
    anjay_dm_internal_res_attrs_t attrs = ANJAY_DM_INTERNAL_RES_ATTRS_EMPTY;
    if (!get_attrs(anjay, &attrs, key)
            && attrs.standard.common.min_period > 0) {
        return attrs.standard.common.min_period;
    }
    return 0;
}

static int
notify_entry(anjay_t *anjay, anjay_observe_entry_t *entry, void *result_ptr) {
    _anjay_update_ret((int *) result_ptr,
                      schedule_trigger(anjay, entry,
                                       get_min_period(anjay, &entry->key)));
    return 0;
}

#ifdef ANJAY_TEST
#    include "test/observe_mock.h"
#endif // ANJAY_TEST

typedef int observe_for_each_matching_clb_t(anjay_t *anjay,
                                            anjay_observe_entry_t *entry,
                                            void *arg);

static int
observe_for_each_in_bounds(anjay_t *anjay,
                           anjay_observe_connection_entry_t *connection,
                           const anjay_observe_key_t *lower_bound,
                           const anjay_observe_key_t *upper_bound,
                           observe_for_each_matching_clb_t *clb,
                           void *clb_arg) {
    int retval = 0;
    AVS_RBTREE_ELEM(anjay_observe_entry_t) it =
            AVS_RBTREE_LOWER_BOUND(connection->entries,
                                   _anjay_observe_entry_query(lower_bound));
    AVS_RBTREE_ELEM(anjay_observe_entry_t) end =
            AVS_RBTREE_UPPER_BOUND(connection->entries,
                                   _anjay_observe_entry_query(upper_bound));
    // if it == NULL, end must also be NULL
    assert(it || !end);

    for (; it != end; it = AVS_RBTREE_ELEM_NEXT(it)) {
        assert(it);
        if ((retval = clb(anjay, it, clb_arg))) {
            return retval;
        }
    }
    return 0;
}

static int
observe_for_each_in_wildcard_impl(anjay_t *anjay,
                                  anjay_observe_connection_entry_t *connection,
                                  const anjay_observe_key_t *specimen_key,
                                  bool iid_wildcard,
                                  observe_for_each_matching_clb_t *clb,
                                  void *clb_arg) {
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
    return observe_for_each_in_bounds(anjay, connection, &lower_bound,
                                      &upper_bound, clb, clb_arg);
}

static inline int
observe_for_each_in_iid_wildcard(anjay_t *anjay,
                                 anjay_observe_connection_entry_t *connection,
                                 const anjay_observe_key_t *specimen_key,
                                 observe_for_each_matching_clb_t *clb,
                                 void *clb_arg) {
    return observe_for_each_in_wildcard_impl(anjay, connection, specimen_key,
                                             true, clb, clb_arg);
}

static inline int
observe_for_each_in_rid_wildcard(anjay_t *anjay,
                                 anjay_observe_connection_entry_t *connection,
                                 const anjay_observe_key_t *specimen_key,
                                 observe_for_each_matching_clb_t *clb,
                                 void *clb_arg) {
    return observe_for_each_in_wildcard_impl(anjay, connection, specimen_key,
                                             false, clb, clb_arg);
}

/**
 * Calls <c>clb()</c> on all registered Observe entries that match <c>key</c>.
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
static int
observe_for_each_matching(anjay_t *anjay,
                          anjay_observe_connection_entry_t *connection,
                          const anjay_observe_key_t *key,
                          observe_for_each_matching_clb_t *clb,
                          void *clb_arg) {
    assert(key->format == AVS_COAP_FORMAT_NONE);
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
        } else if ((retval = observe_for_each_in_iid_wildcard(
                            anjay, connection, key, clb, clb_arg))) {
            goto finish;
        }
    } else if ((retval = observe_for_each_in_rid_wildcard(anjay, connection,
                                                          key, clb, clb_arg))
               || (retval = observe_for_each_in_iid_wildcard(
                           anjay, connection, key, clb, clb_arg))) {
        goto finish;
    }

    retval = observe_for_each_in_bounds(anjay, connection, &lower_bound,
                                        &upper_bound, clb, clb_arg);
finish:
    return retval == ANJAY_FOREACH_BREAK ? 0 : retval;
}

static int observe_notify_impl(anjay_t *anjay,
                               const anjay_observe_key_t *key,
                               bool invert_server_match,
                               observe_for_each_matching_clb_t *clb) {
    assert(key->format == AVS_COAP_FORMAT_NONE);

    // iterate through all SSIDs we have
    int result = 0;
    anjay_observe_key_t modified_key = *key;
    AVS_RBTREE_ELEM(anjay_observe_connection_entry_t) connection;
    AVS_RBTREE_FOREACH(connection, anjay->observe.connection_entries) {
        /* Some compilers complain about promotion of comparison result, so
         * we're casting it to bool explicitly */
        if ((bool) (connection->key.ssid == key->connection.ssid)
                == invert_server_match) {
            continue;
        }
        modified_key.connection = connection->key;
        observe_for_each_matching(anjay, connection, &modified_key, clb,
                                  &result);
    }
    return result;
}

int _anjay_observe_notify(anjay_t *anjay,
                          const anjay_observe_key_t *key,
                          bool invert_server_match) {
    // This extra level of indirection is required to be able to mock
    // notify_entry in unit tests.
    // Hopefully compilers will inline it in production builds.
    return observe_notify_impl(anjay, key, invert_server_match, notify_entry);
}


#ifdef ANJAY_TEST
#    include "test/observe.c"
#endif // ANJAY_TEST
