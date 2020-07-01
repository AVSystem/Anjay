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

#include <anjay_init.h>

#ifdef ANJAY_WITH_OBSERVE

#    include <inttypes.h>
#    include <math.h>

#    include <avsystem/commons/avs_errno.h>
#    include <avsystem/commons/avs_stream_membuf.h>
#    include <avsystem/commons/avs_stream_v_table.h>

#    include <anjay_modules/anjay_time_defs.h>

#    include "../anjay_core.h"
#    include "../anjay_io_core.h"
#    include "../anjay_servers_utils.h"
#    include "../coap/anjay_content_format.h"
#    include "../dm/anjay_dm_read.h"
#    include "../dm/anjay_query.h"

#    define ANJAY_OBSERVE_SOURCE

#    include "../anjay_servers_inactive.h"

#    include "anjay_observe_internal.h"

VISIBILITY_SOURCE_BEGIN

static int32_t connection_ref_cmp(const anjay_connection_ref_t *left,
                                  const anjay_connection_ref_t *right) {
    int32_t tmp_diff = (int32_t) _anjay_server_ssid(left->server)
                       - (int32_t) _anjay_server_ssid(right->server);
    if (!tmp_diff) {
        tmp_diff = (int32_t) left->conn_type - (int32_t) right->conn_type;
    }
    return tmp_diff;
}

int _anjay_observe_token_cmp(const avs_coap_token_t *left,
                             const avs_coap_token_t *right) {
    int tmp_diff = left->size - right->size;
    if (!tmp_diff) {
        tmp_diff = memcmp(left->bytes, right->bytes, left->size);
    }
    return tmp_diff < 0 ? -1 : (tmp_diff > 0 ? 1 : 0);
}

int _anjay_observation_cmp(const void *left, const void *right) {
    return _anjay_observe_token_cmp(
            &((const anjay_observation_t *) left)->token,
            &((const anjay_observation_t *) right)->token);
}

int _anjay_observe_path_entry_cmp(const void *left, const void *right) {
    return _anjay_uri_path_compare(
            &((const anjay_observe_path_entry_t *) left)->path,
            &((const anjay_observe_path_entry_t *) right)->path);
}

void _anjay_observe_init(anjay_observe_state_t *observe,
                         bool confirmable_notifications,
                         size_t stored_notification_limit) {
    assert(!observe->connection_entries);
    observe->confirmable_notifications = confirmable_notifications;

    if (stored_notification_limit == 0) {
        observe->notify_queue_limit_mode = NOTIFY_QUEUE_UNLIMITED;
    } else {
        observe->notify_queue_limit = stored_notification_limit;
        observe->notify_queue_limit_mode = NOTIFY_QUEUE_DROP_OLDEST;
    }
}

static inline bool is_error_value(const anjay_observation_value_t *value) {
    return _anjay_observe_is_error_details(&value->details);
}

static void delete_value(AVS_LIST(anjay_observation_value_t) *value_ptr) {
    assert(value_ptr && *value_ptr);
    if (!is_error_value(*value_ptr)) {
        for (size_t i = 0; i < (*value_ptr)->ref->paths_count; ++i) {
            if ((*value_ptr)->values[i]) {
                _anjay_batch_release(&(*value_ptr)->values[i]);
            }
        }
    }
    AVS_LIST_DELETE(value_ptr);
}

static inline const anjay_observe_path_entry_t *
path_entry_query(const anjay_uri_path_t *path) {
    return AVS_CONTAINER_OF(path, anjay_observe_path_entry_t, path);
}

static AVS_RBTREE_ELEM(anjay_observe_path_entry_t)
find_or_create_observe_path_entry(anjay_observe_connection_entry_t *connection,
                                  const anjay_uri_path_t *path) {
    AVS_RBTREE_ELEM(anjay_observe_path_entry_t) entry =
            AVS_RBTREE_FIND(connection->observed_paths, path_entry_query(path));
    if (!entry) {
        AVS_RBTREE_ELEM(anjay_observe_path_entry_t) new_entry =
                AVS_RBTREE_ELEM_NEW(anjay_observe_path_entry_t);
        if (!new_entry) {
            anjay_log(ERROR, _("out of memory"));
            return NULL;
        }

        memcpy((void *) (intptr_t) (const void *) &new_entry->path, path,
               sizeof(*path));
        entry = AVS_RBTREE_INSERT(connection->observed_paths, new_entry);
        assert(entry == new_entry);
    }
    return entry;
}

static int
add_path_to_observed_paths(anjay_observe_connection_entry_t *conn,
                           const anjay_uri_path_t *path,
                           AVS_RBTREE_ELEM(anjay_observation_t) observation) {
    AVS_RBTREE_ELEM(anjay_observe_path_entry_t) observed_path =
            find_or_create_observe_path_entry(conn, path);
    if (!observed_path) {
        return -1;
    }
    AVS_LIST(AVS_RBTREE_ELEM(anjay_observation_t)) entry =
            AVS_LIST_INSERT_NEW(AVS_RBTREE_ELEM(anjay_observation_t),
                                &observed_path->refs);
    if (!entry) {
        anjay_log(ERROR, _("out of memory"));
        if (!observed_path->refs) {
            AVS_RBTREE_DELETE_ELEM(conn->observed_paths, &observed_path);
        }
        return -1;
    }
    *entry = observation;
    return 0;
}

static void remove_path_from_observed_paths(
        anjay_observe_connection_entry_t *conn,
        const anjay_uri_path_t *path,
        AVS_RBTREE_ELEM(anjay_observation_t) observation) {
    AVS_RBTREE_ELEM(anjay_observe_path_entry_t) observed_path =
            AVS_RBTREE_FIND(conn->observed_paths, path_entry_query(path));
    assert(observed_path);
    AVS_LIST(AVS_RBTREE_ELEM(anjay_observation_t)) *ref_ptr;
    AVS_LIST_FOREACH_PTR(ref_ptr, &observed_path->refs) {
        if (**ref_ptr == observation) {
            AVS_LIST_DELETE(ref_ptr);
            if (!observed_path->refs) {
                AVS_RBTREE_DELETE_ELEM(conn->observed_paths, &observed_path);
            }
            return;
        }
    }
    AVS_UNREACHABLE("Observation not attached to observed paths");
}

int _anjay_observe_add_to_observed_paths(
        anjay_observe_connection_entry_t *conn,
        AVS_RBTREE_ELEM(anjay_observation_t) observation) {
    for (size_t i = 0; i < observation->paths_count; ++i) {
        int result = add_path_to_observed_paths(conn, &observation->paths[i],
                                                observation);
        if (result) {
            for (size_t j = 0; j < i; ++j) {
                remove_path_from_observed_paths(conn, &observation->paths[j],
                                                observation);
            }
            return result;
        }
    }
    return 0;
}

static void
remove_from_observed_paths(anjay_observe_connection_entry_t *conn,
                           AVS_RBTREE_ELEM(anjay_observation_t) observation) {
    for (size_t i = 0; i < observation->paths_count; ++i) {
        remove_path_from_observed_paths(conn, &observation->paths[i],
                                        observation);
    }
}

static void clear_observation(anjay_observe_connection_entry_t *connection,
                              anjay_observation_t *observation) {
    avs_sched_del(&observation->notify_task);
    while (observation->last_sent) {
        delete_value(&observation->last_sent);
    }

    if (observation->last_unsent) {
        anjay_observation_value_t **unsent_ptr;
        anjay_observation_value_t *helper;
        anjay_observation_value_t *server_last_unsent = NULL;
        AVS_LIST_DELETABLE_FOREACH_PTR(unsent_ptr, helper,
                                       &connection->unsent) {
            if ((*unsent_ptr)->ref != observation) {
                server_last_unsent = *unsent_ptr;
            } else {
                delete_value(unsent_ptr);
            }
        }
        connection->unsent_last = server_last_unsent;
        observation->last_unsent = NULL;
    }
}

static void
detach_observation(anjay_observe_connection_entry_t *conn,
                   AVS_RBTREE_ELEM(anjay_observation_t) observation) {
    AVS_RBTREE_DETACH(conn->observations, observation);
    remove_from_observed_paths(conn, observation);
}

void _anjay_observe_cleanup_connection(anjay_observe_connection_entry_t *conn) {
    while (conn->unsent) {
        delete_value(&conn->unsent);
    }
    AVS_RBTREE_DELETE(&conn->observations) {
        remove_from_observed_paths(conn, *conn->observations);
        avs_sched_del(&(*conn->observations)->notify_task);
        if ((*conn->observations)->last_sent) {
            delete_value(&(*conn->observations)->last_sent);
        }
        assert(!(*conn->observations)->last_sent);
    }
    if (conn->observed_paths) {
        assert(!AVS_RBTREE_FIRST(conn->observed_paths));
        AVS_RBTREE_DELETE(&conn->observed_paths);
    }
    if (conn->flush_task) {
        avs_sched_del(&conn->flush_task);
    }
}

void _anjay_observe_cleanup(anjay_observe_state_t *observe) {
    AVS_LIST_CLEAR(&observe->connection_entries) {
        _anjay_observe_cleanup_connection(observe->connection_entries);
    }
}

static void
delete_connection(AVS_LIST(anjay_observe_connection_entry_t) *conn_ptr) {
    _anjay_observe_cleanup_connection(*conn_ptr);
    AVS_LIST_DELETE(conn_ptr);
}

static void delete_connection_if_empty(
        AVS_LIST(anjay_observe_connection_entry_t) *conn_ptr) {
    if (!AVS_RBTREE_FIRST((*conn_ptr)->observations)) {
        assert(!AVS_RBTREE_FIRST((*conn_ptr)->observed_paths));
        assert(!(*conn_ptr)->unsent);
        assert(!(*conn_ptr)->unsent_last);
        delete_connection(conn_ptr);
    }
}

typedef struct {
    anjay_observe_connection_entry_t *conn_state;
    anjay_observation_t *observation;
} trigger_observe_args_t;

static void trigger_observe(avs_sched_t *sched, const void *args_);

static const anjay_observation_value_t *
newest_value(const anjay_observation_t *observation) {
    if (observation->last_unsent) {
        return observation->last_unsent;
    } else {
        assert(observation->last_sent);
        return observation->last_sent;
    }
}

static int schedule_trigger(anjay_observe_connection_entry_t *conn_state,
                            anjay_observation_t *observation,
                            int32_t period) {
    if (period < 0) {
        return 0;
    }

    avs_time_monotonic_t monotonic_now = avs_time_monotonic_now();
    avs_time_real_t real_now = avs_time_real_now();

    avs_time_monotonic_t trigger_instant = avs_time_monotonic_add(
            monotonic_now,
            avs_time_duration_add(
                    avs_time_real_diff(newest_value(observation)->timestamp,
                                       real_now),
                    avs_time_duration_from_scalar(period, AVS_TIME_S)));
    if (avs_time_monotonic_before(trigger_instant, monotonic_now)) {
        trigger_instant = monotonic_now;
    }

    if (avs_time_monotonic_before(avs_sched_time(&observation->notify_task),
                                  trigger_instant)) {
        anjay_log(LAZY_TRACE,
                  _("Notify for token ") "%s" _(" already scheduled earlier "
                                                "than requested ") "%ld.%09lds",
                  ANJAY_TOKEN_TO_STRING(observation->token),
                  (long) trigger_instant.since_monotonic_epoch.seconds,
                  (long) trigger_instant.since_monotonic_epoch.nanoseconds);
        return 0;
    }

    anjay_log(LAZY_TRACE,
              _("Notify for token ") "%s" _(" scheduled: ") "%ld.%09lds",
              ANJAY_TOKEN_TO_STRING(observation->token),
              (long) trigger_instant.since_monotonic_epoch.seconds,
              (long) trigger_instant.since_monotonic_epoch.nanoseconds);

    int retval =
            AVS_SCHED_AT(_anjay_from_server(conn_state->conn_ref.server)->sched,
                         &observation->notify_task, trigger_instant,
                         trigger_observe,
                         (&(const trigger_observe_args_t) {
                             .conn_state = conn_state,
                             .observation = observation
                         }),
                         sizeof(trigger_observe_args_t));
    if (retval) {
        anjay_log(ERROR,
                  _("Could not schedule automatic notification trigger, "
                    "result: ") "%d",
                  retval);
    }
    return retval;
}

static AVS_LIST(anjay_observation_value_t)
create_observation_value(const anjay_msg_details_t *details,
                         avs_coap_notify_reliability_hint_t reliability_hint,
                         anjay_observation_t *ref,
                         const anjay_batch_t *const *values) {
    const size_t values_count =
            _anjay_observe_is_error_details(details) ? 0 : ref->paths_count;
    const size_t element_size = offsetof(anjay_observation_value_t, values)
                                + values_count * sizeof(anjay_batch_t *);
    AVS_LIST(anjay_observation_value_t) result = (AVS_LIST(
            anjay_observation_value_t)) AVS_LIST_NEW_BUFFER(element_size);
    if (!result) {
        anjay_log(ERROR, _("out of memory"));
        return NULL;
    }
    result->details = *details;
    result->reliability_hint = reliability_hint;
    memcpy((void *) (intptr_t) (const void *) &result->ref, &ref, sizeof(ref));
    result->timestamp = avs_time_real_now();
    for (size_t i = 0; i < values_count; ++i) {
        assert(values);
        assert(values[i]);
        result->values[i] = _anjay_batch_acquire(values[i]);
    }
    return result;
}

static size_t count_queued_notifications(const anjay_observe_state_t *observe) {
    size_t count = 0;

    AVS_LIST(anjay_observe_connection_entry_t) conn;
    AVS_LIST_FOREACH(conn, observe->connection_entries) {
        count += AVS_LIST_SIZE(conn->unsent);
    }

    return count;
}

static bool is_observe_queue_full(const anjay_observe_state_t *observe) {
    if (observe->notify_queue_limit_mode == NOTIFY_QUEUE_UNLIMITED) {
        return false;
    }

    size_t num_queued = count_queued_notifications(observe);
    anjay_log(TRACE, "%u/%u" _(" queued notifications"), (unsigned) num_queued,
              (unsigned) observe->notify_queue_limit);

    assert(num_queued <= observe->notify_queue_limit);
    return num_queued >= observe->notify_queue_limit;
}

static AVS_LIST(anjay_observe_connection_entry_t)
find_oldest_queued_notification(anjay_observe_state_t *observe) {
    AVS_LIST(anjay_observe_connection_entry_t) oldest = NULL;

    AVS_LIST(anjay_observe_connection_entry_t) conn;
    AVS_LIST_FOREACH(conn, observe->connection_entries) {
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

static anjay_observation_value_t *
detach_first_unsent_value(anjay_observe_connection_entry_t *conn_state) {
    assert(conn_state->unsent);
    anjay_observation_t *observation = conn_state->unsent->ref;
    if (observation->last_unsent == conn_state->unsent) {
        observation->last_unsent = NULL;
    }
    anjay_observation_value_t *result = AVS_LIST_DETACH(&conn_state->unsent);
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

    anjay_observation_value_t *entry = detach_first_unsent_value(oldest);
    delete_value(&entry);
}

static int insert_new_value(anjay_observe_connection_entry_t *conn_state,
                            anjay_observation_t *observation,
                            avs_coap_notify_reliability_hint_t reliability_hint,
                            const anjay_msg_details_t *details,
                            const anjay_batch_t *const *values) {
    anjay_observe_state_t *observe =
            &_anjay_from_server(conn_state->conn_ref.server)->observe;
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

    AVS_LIST(anjay_observation_value_t) res_value =
            create_observation_value(details, reliability_hint, observation,
                                     values);
    if (!res_value) {
        return -1;
    }

    AVS_LIST_APPEND(&conn_state->unsent_last, res_value);
    conn_state->unsent_last = res_value;
    if (!conn_state->unsent) {
        conn_state->unsent = res_value;
    }
    observation->last_unsent = res_value;
    return 0;
}

static int insert_error(anjay_observe_connection_entry_t *conn_state,
                        anjay_observation_t *observation,
                        int outer_result) {
    avs_sched_del(&observation->notify_task);
    const anjay_msg_details_t details = {
        .msg_code = _anjay_make_error_response_code(outer_result),
        .format = AVS_COAP_FORMAT_NONE
    };
    if (details.msg_code != -outer_result) {
        anjay_log(DEBUG, _("invalid error code: ") "%d", outer_result);
    }
    return insert_new_value(conn_state, observation,
                            AVS_COAP_NOTIFY_PREFER_CONFIRMABLE, &details, NULL);
}

static int get_effective_attrs(anjay_t *anjay,
                               anjay_dm_internal_r_attrs_t *out_attrs,
                               const anjay_uri_path_t *path,
                               anjay_ssid_t ssid) {
    anjay_dm_attrs_query_details_t details = {
        .obj = _anjay_uri_path_has(path, ANJAY_ID_OID)
                       ? _anjay_dm_find_object_by_oid(anjay,
                                                      path->ids[ANJAY_ID_OID])
                       : NULL,
        .iid = ANJAY_ID_INVALID,
        .rid = ANJAY_ID_INVALID,
        .riid = ANJAY_ID_INVALID,
        .ssid = ssid,
        .with_server_level_attrs = true
    };

    if (details.obj && *details.obj && _anjay_uri_path_has(path, ANJAY_ID_IID)
            && !_anjay_dm_verify_instance_present(anjay, details.obj,
                                                  path->ids[ANJAY_ID_IID])) {
        details.iid = path->ids[ANJAY_ID_IID];
    } else {
        return _anjay_dm_effective_attrs(anjay, &details, out_attrs);
    }

    if (_anjay_uri_path_has(path, ANJAY_ID_RID)
            && !_anjay_dm_verify_resource_present(
                       anjay, details.obj, path->ids[ANJAY_ID_IID],
                       path->ids[ANJAY_ID_RID], NULL)) {
        details.rid = path->ids[ANJAY_ID_RID];
    } else {
        return _anjay_dm_effective_attrs(anjay, &details, out_attrs);
    }

    if (_anjay_uri_path_has(path, ANJAY_ID_RIID)
            && !_anjay_dm_verify_resource_instance_present(
                       anjay, details.obj, path->ids[ANJAY_ID_IID],
                       path->ids[ANJAY_ID_RID], path->ids[ANJAY_ID_RIID])) {
        details.riid = path->ids[ANJAY_ID_RIID];
    }
    return _anjay_dm_effective_attrs(anjay, &details, out_attrs);
}

static inline bool is_pmax_valid(anjay_dm_oi_attributes_t attr) {
    if (attr.max_period < 0) {
        return false;
    }

    if (attr.max_period == 0 || attr.max_period < attr.min_period) {
        anjay_log(DEBUG,
                  _("invalid pmax (") "%" PRIi32 _(
                          "); expected pmax > 0 && pmax >= pmin (") "%" PRIi32
                          _(")"),
                  attr.max_period, attr.min_period);
        return false;
    }

    return true;
}

static void update_batch_pmax(int32_t *out_ptr,
                              const anjay_dm_internal_r_attrs_t *attrs) {
    if (is_pmax_valid(attrs->standard.common)
            && (*out_ptr < 0 || attrs->standard.common.max_period < *out_ptr)) {
        *out_ptr = attrs->standard.common.max_period;
    }
}

int _anjay_observe_schedule_pmax_trigger(
        anjay_observe_connection_entry_t *conn_state,
        anjay_observation_t *observation) {
    int32_t pmax = -1;

    for (size_t i = 0; i < observation->paths_count; ++i) {
        anjay_dm_internal_r_attrs_t attrs;
        int result = get_effective_attrs(
                _anjay_from_server(conn_state->conn_ref.server), &attrs,
                &observation->paths[i],
                _anjay_server_ssid(conn_state->conn_ref.server));
        if (result) {
            anjay_log(DEBUG,
                      _("Could not get observe attributes, result: ") "%d",
                      result);
            return result;
        }

        update_batch_pmax(&pmax, &attrs);
    }

    if (pmax >= 0) {
        return schedule_trigger(conn_state, observation, pmax);
    }
    return 0;
}

static int insert_initial_value(anjay_observe_connection_entry_t *conn_state,
                                anjay_observation_t *observation,
                                const anjay_msg_details_t *details,
                                const anjay_batch_t *const *values) {
    assert(!observation->last_sent);
    assert(!observation->last_unsent);

    avs_time_real_t now = avs_time_real_now();

    int result = -1;
    // we assume that the initial value should be treated as sent,
    // even though we haven't actually sent it ourselves
    if ((observation->last_sent = create_observation_value(
                 details, AVS_COAP_NOTIFY_PREFER_NON_CONFIRMABLE, observation,
                 values))
            && !(result = _anjay_observe_schedule_pmax_trigger(conn_state,
                                                               observation))) {
        observation->last_confirmable = now;
    }
    return result;
}

typedef enum { PATHS_POINTER_LIST, PATHS_POINTER_ARRAY } paths_pointer_type_t;

typedef struct {
    paths_pointer_type_t type;
    const anjay_uri_path_t *paths;
    size_t count;
} paths_arg_t;

static AVS_RBTREE_ELEM(anjay_observation_t)
create_detached_observation(const avs_coap_token_t *token,
                            anjay_request_action_t action,
                            const paths_arg_t *paths) {
    AVS_RBTREE_ELEM(anjay_observation_t) new_observation =
            (AVS_RBTREE_ELEM(anjay_observation_t)) AVS_RBTREE_ELEM_NEW_BUFFER(
                    offsetof(anjay_observation_t, paths)
                    + paths->count * sizeof(const anjay_uri_path_t));
    if (!new_observation) {
        anjay_log(ERROR, _("out of memory"));
        return NULL;
    }
    memcpy((void *) (intptr_t) (const void *) &new_observation->token, token,
           sizeof(*token));
    memcpy((void *) (intptr_t) (const void *) &new_observation->action, &action,
           sizeof(action));
    memcpy((void *) (intptr_t) (const void *) &new_observation->paths_count,
           &paths->count, sizeof(paths->count));
    if (paths->type == PATHS_POINTER_LIST) {
        AVS_LIST(const anjay_uri_path_t) it = paths->paths;
        for (size_t i = 0; i < paths->count; ++i) {
            memcpy((void *) (intptr_t) (const void *) &new_observation
                           ->paths[i],
                   it, sizeof(*it));
            AVS_LIST_ADVANCE(&it);
        }
    } else {
        assert(paths->count == 1);
        memcpy((void *) (intptr_t) (const void *) &new_observation->paths[0],
               paths->paths, sizeof(*paths->paths));
    }
    return new_observation;
}

static AVS_LIST(anjay_observe_connection_entry_t) *
find_connection_state_insert_ptr(anjay_connection_ref_t ref) {
    AVS_LIST(anjay_observe_connection_entry_t) *conn_ptr =
            &_anjay_from_server(ref.server)->observe.connection_entries;
    while (*conn_ptr && connection_ref_cmp(&(*conn_ptr)->conn_ref, &ref) < 0) {
        AVS_LIST_ADVANCE_PTR(&conn_ptr);
    }
    return conn_ptr;
}

static AVS_LIST(anjay_observe_connection_entry_t) *
find_connection_state(anjay_connection_ref_t ref) {
    AVS_LIST(anjay_observe_connection_entry_t) *conn_ptr =
            find_connection_state_insert_ptr(ref);
    if (*conn_ptr && connection_ref_cmp(&(*conn_ptr)->conn_ref, &ref) == 0) {
        return conn_ptr;
    }
    return NULL;
}

static AVS_LIST(anjay_observe_connection_entry_t) *
find_or_create_connection_state(anjay_connection_ref_t ref) {
    AVS_LIST(anjay_observe_connection_entry_t) *conn_ptr =
            find_connection_state_insert_ptr(ref);
    if (!*conn_ptr || connection_ref_cmp(&(*conn_ptr)->conn_ref, &ref) != 0) {
        if (!AVS_LIST_INSERT_NEW(anjay_observe_connection_entry_t, conn_ptr)
                || !((*conn_ptr)->observations = AVS_RBTREE_NEW(
                             anjay_observation_t, _anjay_observation_cmp))
                || !((*conn_ptr)->observed_paths =
                             AVS_RBTREE_NEW(anjay_observe_path_entry_t,
                                            _anjay_observe_path_entry_cmp))) {
            anjay_log(ERROR, _("out of memory"));
            if (*conn_ptr) {
                AVS_RBTREE_DELETE(&(*conn_ptr)->observations);
                AVS_LIST_DELETE(conn_ptr);
            }
            return NULL;
        }
        memcpy((void *) (intptr_t) (const void *) &(*conn_ptr)->conn_ref, &ref,
               sizeof(ref));
    }
    return conn_ptr;
}

static void
delete_observation(AVS_LIST(anjay_observe_connection_entry_t) *conn_ptr,
                   AVS_RBTREE_ELEM(anjay_observation_t) *observation_ptr) {
    clear_observation(*conn_ptr, *observation_ptr);
    detach_observation(*conn_ptr, *observation_ptr);
    AVS_RBTREE_ELEM_DELETE_DETACHED(observation_ptr);
    delete_connection_if_empty(conn_ptr);
}

static void observe_remove_entry(anjay_connection_ref_t connection,
                                 const avs_coap_token_t *token) {
    AVS_LIST(anjay_observe_connection_entry_t) *conn_ptr =
            find_connection_state(connection);
    if (!conn_ptr) {
        return;
    }
    if (avs_coap_exchange_id_valid((*conn_ptr)->notify_exchange_id)) {
        avs_coap_exchange_cancel(_anjay_connection_get_coap(
                                         (*conn_ptr)->conn_ref),
                                 (*conn_ptr)->notify_exchange_id);
    }
    assert(!avs_coap_exchange_id_valid((*conn_ptr)->notify_exchange_id));
    assert(!(*conn_ptr)->serialization_state.membuf_stream);
    assert(!(*conn_ptr)->serialization_state.out_ctx);
    AVS_RBTREE_ELEM(anjay_observation_t) observation =
            AVS_RBTREE_FIND((*conn_ptr)->observations,
                            _anjay_observation_query(token));
    if (observation) {
        delete_observation(conn_ptr, &observation);
    }
}

void _anjay_observe_cancel_handler(avs_coap_observe_id_t id, void *ref_ptr) {
    observe_remove_entry(*(anjay_connection_ref_t *) ref_ptr, &id.token);
    avs_free(ref_ptr);
}

static int start_coap_observe(anjay_connection_ref_t connection,
                              const anjay_request_t *request) {
    anjay_connection_ref_t *heap_conn = (anjay_connection_ref_t *) avs_malloc(
            sizeof(anjay_connection_ref_t));
    if (!heap_conn) {
        return -1;
    }
    *heap_conn = connection;
    if (avs_is_err(avs_coap_observe_streaming_start(
                request->ctx, *request->observe, _anjay_observe_cancel_handler,
                heap_conn))) {
        avs_free(heap_conn);
        return -1;
    }
    return 0;
}

static int
attach_new_observation(anjay_observe_connection_entry_t *conn_state,
                       AVS_RBTREE_ELEM(anjay_observation_t) observation) {
    AVS_RBTREE_INSERT(conn_state->observations, observation);
    int result = _anjay_observe_add_to_observed_paths(conn_state, observation);
    if (result) {
        AVS_RBTREE_DETACH(conn_state->observations, observation);
    }
    return result;
}

static AVS_RBTREE_ELEM(anjay_observation_t)
put_entry_into_connection_state(const anjay_request_t *request,
                                anjay_observe_connection_entry_t *conn_state,
                                const paths_arg_t *paths) {
    AVS_RBTREE_ELEM(anjay_observation_t) observation =
            create_detached_observation(&request->observe->token,
                                        request->action, paths);
    if (!observation) {
        return NULL;
    }
    assert(!AVS_RBTREE_FIND(conn_state->observations, observation));

    if (attach_new_observation(conn_state, observation)) {
        clear_observation(conn_state, observation);
        AVS_RBTREE_ELEM_DELETE_DETACHED(&observation);
        return NULL;
    }
    return observation;
}

static int read_as_batch(anjay_t *anjay,
                         const anjay_dm_object_def_t *const *obj_ptr,
                         const anjay_dm_path_info_t *path_info,
                         anjay_request_action_t action,
                         anjay_ssid_t connection_ssid,
                         anjay_batch_t **out_batch) {
    assert(out_batch && !*out_batch);
    anjay_batch_builder_t *builder = _anjay_batch_builder_new();
    if (!builder) {
        anjay_log(ERROR, _("out of memory"));
        return -1;
    }

    int result = _anjay_dm_read_into_batch(builder, anjay, obj_ptr, path_info,
                                           connection_ssid);
    (void) action;
    if (!result && !(*out_batch = _anjay_batch_builder_compile(&builder))) {
        anjay_log(ERROR, _("out of memory"));
        result = -1;
    }
    _anjay_batch_builder_cleanup(&builder);
    return result;
}

static inline const anjay_batch_t *const *
cast_to_const_batch_array(anjay_batch_t **batch_array) {
    return (const anjay_batch_t *const *) batch_array;
}

static inline anjay_uri_path_t
get_observation_path(const anjay_observation_t *observation) {
    return (observation->action == ANJAY_ACTION_READ ? observation->paths[0]
                                                     : MAKE_ROOT_PATH());
}

static int write_notify_payload(size_t payload_offset,
                                void *payload_buf,
                                size_t payload_buf_size,
                                size_t *out_payload_chunk_size,
                                void *conn_) {
    anjay_observe_connection_entry_t *conn =
            (anjay_observe_connection_entry_t *) conn_;
    if (payload_offset != conn->serialization_state.expected_offset) {
        anjay_log(DEBUG,
                  _("Server requested unexpected chunk of payload (expected "
                    "offset ") "%u" _(", got ") "%u" _(")"),
                  (unsigned) conn->serialization_state.expected_offset,
                  (unsigned) payload_offset);
        return -1;
    }

    anjay_t *anjay = _anjay_from_server(conn->conn_ref.server);
    anjay_observation_value_t *value = conn->unsent;
    anjay_observation_t *observation = value->ref;

    char *write_ptr = (char *) payload_buf;
    const char *end_ptr = write_ptr + payload_buf_size;
    while (true) {
        size_t bytes_read;
        if (avs_is_err(avs_stream_read(conn->serialization_state.membuf_stream,
                                       &bytes_read, NULL, write_ptr,
                                       (size_t) (end_ptr - write_ptr)))) {
            return -1;
        }
        write_ptr += bytes_read;
        if (write_ptr >= end_ptr || !conn->serialization_state.out_ctx) {
            break;
        }
        // NOTE: Access Control permissions have been checked during the
        // _anjay_dm_read_as_batch() stage, so we're "spoofing"
        // ANJAY_SSID_BOOTSTRAP as the permissions are checked now
        int result = _anjay_batch_data_output_entry(
                anjay, value->values[conn->serialization_state.curr_value_idx],
                ANJAY_SSID_BOOTSTRAP,
                conn->serialization_state.serialization_time,
                &conn->serialization_state.output_state,
                conn->serialization_state.out_ctx);
        if (!result && !conn->serialization_state.output_state) {
            ++conn->serialization_state.curr_value_idx;
            if (conn->serialization_state.curr_value_idx
                    >= observation->paths_count) {
                result = _anjay_output_ctx_destroy_and_process_result(
                        &conn->serialization_state.out_ctx, result);
            }
        }
        if (result) {
            return result;
        }
    }
    *out_payload_chunk_size = (size_t) (write_ptr - (char *) payload_buf);
    conn->serialization_state.expected_offset += *out_payload_chunk_size;
    return 0;
}

static anjay_msg_details_t
initial_response_details(anjay_t *anjay,
                         const anjay_request_t *request,
                         const anjay_batch_t *const *values) {
    bool requires_hierarchical_format;
    {
        assert(request->action == ANJAY_ACTION_READ);
        assert(values);
        requires_hierarchical_format =
                _anjay_batch_data_requires_hierarchical_format(values[0]);
    }
    return _anjay_dm_response_details_for_read(
            anjay, request, requires_hierarchical_format,
            _anjay_server_registration_info(anjay->current_connection.server)
                    ->lwm2m_version);
}

static int send_initial_response(anjay_t *anjay,
                                 const anjay_msg_details_t *details,
                                 const anjay_request_t *request,
                                 size_t values_count,
                                 const anjay_batch_t *const *values) {

    avs_stream_t *notify_stream =
            _anjay_coap_setup_response_stream(request->ctx, details);
    if (!notify_stream) {
        return -1;
    }
    anjay_output_ctx_t *out_ctx = NULL;
    int result = _anjay_output_dynamic_construct(&out_ctx, notify_stream,
                                                 &request->uri, details->format,
                                                 request->action);
    for (size_t i = 0; !result && i < values_count; ++i) {
        // NOTE: Access Control permissions have been checked during the
        // _anjay_dm_read_as_batch() stage, so we're "spoofing"
        // ANJAY_SSID_BOOTSTRAP as the permissions are checked now
        result = _anjay_batch_data_output(anjay, values[i],
                                          ANJAY_SSID_BOOTSTRAP, out_ctx);
    }
    return _anjay_output_ctx_destroy_and_process_result(&out_ctx, result);
}

#    ifdef ANJAY_TEST
// This defines a mock macro for send_initial_response(),
// so it needs to be included after definition of the real
// send_initial_response(), but before its usages.
#        include "tests/core/observe/observe_mock.h"
#    endif // ANJAY_TEST

static void delete_batch_array(anjay_batch_t ***batches_ptr,
                               size_t batches_count) {
    for (size_t i = 0; i < batches_count; ++i) {
        if ((*batches_ptr)[i]) {
            _anjay_batch_release(&(*batches_ptr)[i]);
        }
    }
    avs_free(*batches_ptr);
    *batches_ptr = NULL;
}

static int read_observation_path(anjay_t *anjay,
                                 const anjay_uri_path_t *path,
                                 anjay_request_action_t action,
                                 anjay_ssid_t connection_ssid,
                                 anjay_batch_t **out_batch) {
    const anjay_dm_object_def_t *const *obj = NULL;
    if (_anjay_uri_path_has(path, ANJAY_ID_OID)) {
        obj = _anjay_dm_find_object_by_oid(anjay, path->ids[ANJAY_ID_OID]);
    }
    int result;
    anjay_dm_path_info_t path_info;
    (void) ((result = _anjay_dm_path_info(anjay, obj, path, &path_info))
            || (result = read_as_batch(anjay, obj, &path_info, action,
                                       connection_ssid, out_batch)));
    return result;
}

static int read_observation_values(anjay_t *anjay,
                                   const paths_arg_t *paths,
                                   anjay_request_action_t action,
                                   anjay_ssid_t connection_ssid,
                                   anjay_batch_t ***out_batches) {
    assert(out_batches && !*out_batches);
    assert(paths->type != PATHS_POINTER_LIST
           || paths->count == AVS_LIST_SIZE(paths->paths));

    if (paths->count
            && !(*out_batches = (anjay_batch_t **) avs_calloc(
                         paths->count, sizeof(anjay_batch_t *)))) {
        anjay_log(ERROR, _("out of memory"));
        return -1;
    }

    int result = 0;
    switch (paths->type) {
    case PATHS_POINTER_LIST: {
        size_t index = 0;
        AVS_LIST(const anjay_uri_path_t) path;
        AVS_LIST_FOREACH(path, paths->paths) {
            if ((result = read_observation_path(anjay, path, action,
                                                connection_ssid,
                                                &(*out_batches)[index]))) {
                break;
            }
            ++index;
        }
        break;
    }
    case PATHS_POINTER_ARRAY:
        for (size_t index = 0; index < paths->count; ++index) {
            if ((result = read_observation_path(anjay, &paths->paths[index],
                                                action, connection_ssid,
                                                &(*out_batches)[index]))) {
                break;
            }
        }
        break;
    default:
        AVS_UNREACHABLE("The switch above shall be exhaustive");
        result = -1;
    }

    if (result) {
        delete_batch_array(out_batches, paths->count);
    }
    return result;
}

static int observe_handle(anjay_t *anjay,
                          const paths_arg_t *paths,
                          const anjay_request_t *request) {
    AVS_LIST(anjay_observe_connection_entry_t) *conn_ptr =
            find_or_create_connection_state(anjay->current_connection);
    if (!conn_ptr) {
        return -1;
    }

    AVS_RBTREE_ELEM(anjay_observation_t) observation = NULL;
    anjay_msg_details_t response_details;
    anjay_batch_t **batches = NULL;
    int send_result = -1;
    int result =
            read_observation_values(anjay, paths, request->action,
                                    _anjay_dm_current_ssid(anjay), &batches);
    if (result) {
        delete_connection_if_empty(conn_ptr);
        return result;
    }
    response_details =
            initial_response_details(anjay, request,
                                     cast_to_const_batch_array(batches));

    if (!(observation =
                  put_entry_into_connection_state(request, *conn_ptr, paths))
            || insert_initial_value(*conn_ptr, observation, &response_details,
                                    cast_to_const_batch_array(batches))
            || start_coap_observe(anjay->current_connection, request)) {
        result = -1;
    }
    // No matter if we succeeded with adding the observation to internal
    // state or not, as long as we have some payload, we may as well just
    // "process the request as usual" (RFC 7641, section 4.1).
    send_result = send_initial_response(anjay, &response_details, request,
                                        paths->count,
                                        cast_to_const_batch_array(batches));

    if (result || send_result) {
        observe_remove_entry(anjay->current_connection,
                             &request->observe->token);
        if (conn_ptr && *conn_ptr) {
            delete_connection_if_empty(conn_ptr);
        }
    }
    delete_batch_array(&batches, paths->count);

    if (result && !send_result) {
        // we sent the response as if it was a read request
        result = 0;
    }
    return result;
}

int _anjay_observe_handle(anjay_t *anjay, const anjay_request_t *request) {
    assert(request->action == ANJAY_ACTION_READ);
    return observe_handle(anjay,
                          &(const paths_arg_t) {
                              .type = PATHS_POINTER_ARRAY,
                              .paths = &request->uri,
                              .count = 1
                          },
                          request);
}

static int observe_gc_ssid_iterate(anjay_t *anjay,
                                   anjay_ssid_t ssid,
                                   void *conn_ptr_ptr_) {
    (void) anjay;
    AVS_LIST(anjay_observe_connection_entry_t) **conn_ptr_ptr =
            (AVS_LIST(anjay_observe_connection_entry_t) **) conn_ptr_ptr_;
    while (**conn_ptr_ptr
           && _anjay_server_ssid((**conn_ptr_ptr)->conn_ref.server) < ssid) {
        delete_connection(*conn_ptr_ptr);
    }
    while (**conn_ptr_ptr
           && _anjay_server_ssid((**conn_ptr_ptr)->conn_ref.server) == ssid) {
        AVS_LIST_ADVANCE_PTR(conn_ptr_ptr);
    }
    return 0;
}

void _anjay_observe_gc(anjay_t *anjay) {
    AVS_LIST(anjay_observe_connection_entry_t) *conn_ptr =
            &anjay->observe.connection_entries;
    _anjay_servers_foreach_ssid(anjay, observe_gc_ssid_iterate, &conn_ptr);
    while (*conn_ptr) {
        delete_connection(conn_ptr);
    }
}

static bool has_pmax_expired(const anjay_observation_value_t *value,
                             const anjay_dm_oi_attributes_t *attrs) {
    return is_pmax_valid(*attrs)
           && avs_time_real_diff(avs_time_real_now(), value->timestamp).seconds
                      >= attrs->max_period;
}

static bool has_epmin_expired(const anjay_batch_t *value_element,
                              const anjay_dm_oi_attributes_t *attrs) {
    return attrs->min_eval_period == ANJAY_ATTRIB_PERIOD_NONE
           || avs_time_real_diff(avs_time_real_now(),
                                 _anjay_batch_get_compilation_time(
                                         value_element))
                              .seconds
                      >= attrs->min_eval_period;
}

static bool process_step(const anjay_dm_r_attributes_t *attrs,
                         double previous_value,
                         double new_value) {
    return !isnan(attrs->step)
           && fabs(new_value - previous_value) >= attrs->step;
}

static bool
process_ltgt(double threshold, double previous_value, double new_value) {
    return !isnan(threshold)
           && ((previous_value <= threshold && new_value > threshold)
               || (previous_value >= threshold && new_value < threshold));
}

static bool should_update(const anjay_uri_path_t *path,
                          const anjay_dm_r_attributes_t *attrs,
                          const anjay_batch_t *previous_value,
                          const anjay_batch_t *new_value) {
    if (_anjay_batch_values_equal(previous_value, new_value)) {
        return false;
    }

    double previous_numeric = NAN;
    double new_numeric = NAN;
    if (_anjay_uri_path_has(path, ANJAY_ID_RID)) {
        previous_numeric = _anjay_batch_data_numeric_value(previous_value);
        new_numeric = _anjay_batch_data_numeric_value(new_value);
    }
    if (isnan(new_numeric) || isnan(previous_numeric)
            || (isnan(attrs->greater_than) && isnan(attrs->less_than)
                && isnan(attrs->step))) {
        // either previous or current value is not numeric, or none of lt/gt/st
        // attributes are set - notifying each value change
        return true;
    }

    return process_step(attrs, previous_numeric, new_numeric)
           || process_ltgt(attrs->less_than, previous_numeric, new_numeric)
           || process_ltgt(attrs->greater_than, previous_numeric, new_numeric);
}

static bool confirmable_required(const anjay_observe_connection_entry_t *conn) {
    anjay_t *anjay = _anjay_from_server(conn->conn_ref.server);
    anjay_socket_transport_t transport =
            _anjay_connection_transport(conn->conn_ref);
    anjay_observation_t *observation = conn->unsent->ref;
    avs_time_real_t confirmable_necessary_at = avs_time_real_add(
            observation->last_confirmable,
            avs_time_duration_diff(
                    avs_time_duration_from_scalar(1, AVS_TIME_DAY),
                    _anjay_max_transmit_wait_for_transport(anjay, transport)));
    return !avs_time_real_before(avs_time_real_now(), confirmable_necessary_at);
}

static void value_sent(anjay_observe_connection_entry_t *conn_state) {
    anjay_observation_value_t *sent = detach_first_unsent_value(conn_state);
    anjay_observation_t *observation = sent->ref;
    assert(AVS_LIST_SIZE(observation->last_sent) <= 1);
    if (observation->last_sent) {
        delete_value(&observation->last_sent);
    }
    observation->last_sent = sent;
}

static bool notification_storing_enabled(anjay_connection_ref_t conn_ref) {
    anjay_t *anjay = _anjay_from_server(conn_ref.server);
    anjay_iid_t server_iid;
    if (!_anjay_find_server_iid(anjay, _anjay_server_ssid(conn_ref.server),
                                &server_iid)) {
        const anjay_uri_path_t path =
                MAKE_RESOURCE_PATH(ANJAY_DM_OID_SERVER, server_iid,
                                   ANJAY_DM_RID_SERVER_NOTIFICATION_STORING);
        bool storing;
        if (!_anjay_dm_read_resource_bool(anjay, &path, &storing) && !storing) {
            // default value is true, use false only if explicitly set
            return false;
        }
    }
    return true;
}

static void remove_all_unsent_values(anjay_observe_connection_entry_t *conn) {
    while (conn->unsent && !is_error_value(conn->unsent)) {
        AVS_LIST(anjay_observation_value_t) value =
                detach_first_unsent_value(conn);
        delete_value(&value);
    }
}

static void schedule_all_triggers(anjay_observe_connection_entry_t *conn) {
    AVS_RBTREE_ELEM(anjay_observation_t) observation;
    AVS_RBTREE_FOREACH(observation, conn->observations) {
        if (!observation->notify_task) {
            _anjay_observe_schedule_pmax_trigger(conn, observation);
        }
    }
}

static bool connection_exists(anjay_t *anjay,
                              anjay_observe_connection_entry_t *conn) {
    AVS_LIST(anjay_observe_connection_entry_t) *conn_ptr =
            (AVS_LIST(anjay_observe_connection_entry_t) *) AVS_LIST_FIND_PTR(
                    &anjay->observe.connection_entries, conn);
    return conn_ptr && *conn_ptr == conn;
}

static void flush_next_unsent(anjay_observe_connection_entry_t *conn);

static void on_network_error(anjay_connection_ref_t conn_ref, avs_error_t err) {
    anjay_log(WARNING, _("network communication error while sending Notify"));
    if (conn_ref.conn_type == ANJAY_CONNECTION_PRIMARY) {
        _anjay_server_on_server_communication_error(conn_ref.server, err);
    }
}

static void flush_send_queue_job(avs_sched_t *sched, const void *conn_ptr) {
    (void) sched;
    anjay_observe_connection_entry_t *conn =
            *(anjay_observe_connection_entry_t *const *) conn_ptr;
    if (conn && conn->unsent
            && !avs_coap_exchange_id_valid(conn->notify_exchange_id)
            && _anjay_connection_ready_for_outgoing_message(conn->conn_ref)
            && _anjay_connection_get_online_socket(conn->conn_ref)) {
        flush_next_unsent(conn);
    }
}

static int
sched_flush_send_queue(AVS_LIST(anjay_observe_connection_entry_t) conn) {
    if (conn->flush_task
            || avs_coap_exchange_id_valid(conn->notify_exchange_id)) {
        anjay_log(TRACE, _("skipping notification flush scheduling: flush "
                           "already scheduled"));
        return 0;
    }
    if (AVS_SCHED_NOW(_anjay_from_server(conn->conn_ref.server)->sched,
                      &conn->flush_task, flush_send_queue_job, &conn,
                      sizeof(conn))) {
        anjay_log(WARNING, _("Could not schedule notification flush"));
        return -1;
    }
    return 0;
}

static void on_entry_flushed(anjay_observe_connection_entry_t *conn,
                             avs_error_t err) {
    if (avs_is_ok(err)) {
        if (conn->unsent) {
            sched_flush_send_queue(conn);
        } else {
            schedule_all_triggers(conn);
        }
        return;
    }

    if (err.category == AVS_COAP_ERR_CATEGORY) {
        if (avs_coap_error_recovery_action(err)
                == AVS_COAP_ERR_RECOVERY_RECREATE_CONTEXT) {
            on_network_error(conn->conn_ref, err);
            return;
        } else if (err.code == AVS_COAP_ERR_UDP_RESET_RECEIVED
                   || err.code == AVS_COAP_ERR_EXCHANGE_CANCELED) {
            // These cases are handled by avs_coap;
            // observation has been already cancelled.
            return;
        } else {
            // All other cases are non-fatal errors that we handle by cancelling
            // the observation anyway. Fall through to outside this 'if' ladder.
        }
    } else if (err.category == AVS_ERRNO_CATEGORY
               && (err.code == AVS_EINVAL || err.code == AVS_EMSGSIZE
                   || err.code == AVS_ENOMEM)) {
        // These are socket-layer errors: AVS_EINVAL - some invalid argument has
        // been passed somewhere; AVS_EMSGISZE - truncated datagram received;
        // AVS_ENOMEM - out of memory. In each of these cases, socket is still
        // ready for further communication. We treat them as non-fatal errors -
        // fall through to outside this 'if' ladder.
    } else {
        // All other errors are treated as fatal socket errors, i.e., we assume
        // that the socket is no longer usable.
        on_network_error(conn->conn_ref, err);
        return;
    }

    // NOTE: we couldn't send the notification due to some kind of non-fatal
    // condition, but observe is not canceled on CoAP layer.
    if (!notification_storing_enabled(conn->conn_ref)) {
        remove_all_unsent_values(conn);
    }
    anjay_log(WARNING, _("Could not send Observe notification: ") "%s",
              AVS_COAP_STRERROR(err));
}

static void
cleanup_serialization_state(anjay_observation_serialization_state_t *state) {
    _anjay_output_ctx_destroy(&state->out_ctx);
    avs_stream_cleanup(&state->membuf_stream);
}

static int
initialize_serialization_state(anjay_observe_connection_entry_t *conn) {
    assert(!conn->serialization_state.membuf_stream);
    assert(!conn->serialization_state.out_ctx);
    memset(&conn->serialization_state, 0, sizeof(conn->serialization_state));

    anjay_observation_value_t *value = conn->unsent;
    anjay_observation_t *observation = value->ref;

    // DO NOT ATTEMPT TO INLINE get_observation_path() HERE.
    //
    // Doing so makes some old GCC versions place this variable in read-only
    // memory (!?), causing a crash at initialization below, unless the const is
    // removed.
    //
    // After some manual checks on x86_64, crashes happen when compiling this
    // with GCC <5.5 and 6.0-6.3.
    //
    // I was not able to find a relevant bug report nor a patch that fixed it to
    // link here. -- marian
    const anjay_uri_path_t root_path = get_observation_path(observation);

    if (!(conn->serialization_state.membuf_stream = avs_stream_membuf_create())
            || _anjay_output_dynamic_construct(
                       &conn->serialization_state.out_ctx,
                       conn->serialization_state.membuf_stream, &root_path,
                       value->details.format, observation->action)) {
        return -1;
    }
    conn->serialization_state.serialization_time = avs_time_real_now();
    return 0;
}

static void
handle_notify_delivery(avs_coap_ctx_t *coap, avs_error_t err, void *conn_) {
    (void) coap;
    anjay_observe_connection_entry_t *conn =
            (anjay_observe_connection_entry_t *) conn_;

    conn->notify_exchange_id = AVS_COAP_EXCHANGE_ID_INVALID;
    cleanup_serialization_state(&conn->serialization_state);
    if (avs_is_ok(err)) {
        assert(!is_error_value(conn->unsent));
        if (conn->unsent->reliability_hint
                == AVS_COAP_NOTIFY_PREFER_CONFIRMABLE) {
            conn->unsent->ref->last_confirmable = avs_time_real_now();
        }
        value_sent(conn);
    }
    on_entry_flushed(conn, err);
}

static void flush_next_unsent(anjay_observe_connection_entry_t *conn) {
    assert(conn->unsent);
    anjay_observation_t *observation = conn->unsent->ref;
    anjay_msg_details_t details = conn->unsent->details;

    if (confirmable_required(conn)) {
        conn->unsent->reliability_hint = AVS_COAP_NOTIFY_PREFER_CONFIRMABLE;
    }

    anjay_connection_ref_t conn_ref = conn->conn_ref;
    avs_coap_ctx_t *coap = _anjay_connection_get_coap(conn_ref);
    assert(coap);

    // Note: if we are dealing with a non-composite Observe, we assert that the
    // observation was issued on exactly one path and we use it as the root
    // path. That way, if that path is not the leaf (e.g. it's an Object path
    // that contains Instances), the output context will be able to serialize
    // the paths as relative to the root, using basename and name SenML
    // attributes if available. For Observe-Composite, we cannot make assumption
    // that there is any common root, so we use /.
    assert(observation->action != ANJAY_ACTION_READ
           || observation->paths_count == 1);
    assert(!avs_coap_exchange_id_valid(conn->notify_exchange_id));

    avs_coap_response_header_t response = { 0 };
    avs_error_t err = _anjay_coap_fill_response_header(&response, &details);
    if (avs_is_err(err)) {
        on_entry_flushed(conn, err);
    } else {
        avs_coap_payload_writer_t *payload_writer = NULL;
        if (!is_error_value(conn->unsent)) {
            payload_writer = write_notify_payload;
            if (initialize_serialization_state(conn)) {
                err = avs_errno(AVS_ENOMEM);
            }
        }
        if (avs_is_err(err)) {
            on_entry_flushed(conn, err);
        } else if (avs_is_err(
                           (err = avs_coap_notify_async(
                                    coap, &conn->notify_exchange_id,
                                    (avs_coap_observe_id_t) {
                                        .token = observation->token
                                    },
                                    &response, conn->unsent->reliability_hint,
                                    payload_writer, conn,
                                    handle_notify_delivery, conn)))
                   && connection_exists(_anjay_from_server(conn_ref.server),
                                        conn)) {
            cleanup_serialization_state(&conn->serialization_state);
            on_entry_flushed(conn, err);
        }
    }
    avs_coap_options_cleanup(&response.options);
    // on_entry_flushed() may have closed the socket already,
    // so we need to check if it's still open
    if (_anjay_connection_get_online_socket(conn_ref)) {
        _anjay_connection_schedule_queue_mode_close(conn_ref);
    }
}

void _anjay_observe_interrupt(anjay_connection_ref_t ref) {
    AVS_LIST(anjay_observe_connection_entry_t) *conn_ptr =
            find_connection_state(ref);
    if (!conn_ptr) {
        return;
    }
    if ((*conn_ptr)->flush_task) {
        anjay_log(TRACE,
                  _("Cancelling notifications flush task for server "
                    "SSID ") "%u" _(", connection type ") "%d",
                  _anjay_server_ssid(ref.server), ref.conn_type);
        avs_sched_del(&(*conn_ptr)->flush_task);
    }
    if (avs_coap_exchange_id_valid((*conn_ptr)->notify_exchange_id)) {
        anjay_log(TRACE,
                  _("Cancelling notification attempt for server SSID ") "%u" _(
                          ", connection type ") "%d",
                  _anjay_server_ssid(ref.server), ref.conn_type);
        avs_coap_exchange_cancel(_anjay_connection_get_coap(ref),
                                 (*conn_ptr)->notify_exchange_id);
        assert(!avs_coap_exchange_id_valid((*conn_ptr)->notify_exchange_id));
    }
}

int _anjay_observe_sched_flush(anjay_connection_ref_t ref) {
    anjay_log(TRACE,
              _("scheduling notifications flush for server SSID ") "%u" _(
                      ", connection type ") "%d",
              _anjay_server_ssid(ref.server), ref.conn_type);
    AVS_LIST(anjay_observe_connection_entry_t) *conn_ptr =
            find_connection_state(ref);
    if (!conn_ptr) {
        anjay_log(TRACE, _("skipping notification flush scheduling: no "
                           "appropriate connection found"));
        return 0;
    }
    return sched_flush_send_queue(*conn_ptr);
}

static int
update_notification_value(anjay_observe_connection_entry_t *conn_state,
                          anjay_observation_t *observation) {
    if (is_error_value(newest_value(observation))) {
        return 0;
    }

    anjay_t *anjay = _anjay_from_server(conn_state->conn_ref.server);
    anjay_ssid_t ssid = _anjay_server_ssid(conn_state->conn_ref.server);
    anjay_batch_t **batches = NULL;
    bool should_update_batch = false;
    int32_t pmax = -1;
    anjay_dm_con_attr_t con = ANJAY_DM_CON_ATTR_DEFAULT;

    if (observation->paths_count
            && !(batches = (anjay_batch_t **) avs_calloc(
                         observation->paths_count, sizeof(anjay_batch_t *)))) {
        anjay_log(ERROR, _("Out of memory"));
        return -1;
    }

    int result = 0;
    for (size_t i = 0; i < observation->paths_count; ++i) {
        anjay_dm_internal_r_attrs_t attrs;
        if ((result = get_effective_attrs(anjay, &attrs, &observation->paths[i],
                                          ssid))) {
            anjay_log(ERROR, _("Could not get attributes of path ") "%s",
                      ANJAY_DEBUG_MAKE_PATH(&observation->paths[i]));
            goto finish;
        }

        if (has_epmin_expired(newest_value(observation)->values[i],
                              &attrs.standard.common)) {
            if ((result = read_observation_path(anjay, &observation->paths[i],
                                                observation->action, ssid,
                                                &batches[i]))) {
                anjay_log(ERROR,
                          _("Could not read path ") "%s" _(" for notifying"),
                          ANJAY_DEBUG_MAKE_PATH(&observation->paths[i]));
                goto finish;
            }
        } else {
            anjay_log(DEBUG,
                      _("epmin == ") "%" PRId32 _(" set for path ") "%s" _(
                              " caused holding from reading a new value"),
                      attrs.standard.common.min_eval_period,
                      ANJAY_DEBUG_MAKE_PATH(&observation->paths[i]));
            // Do not even call read_handler, just copy previous value
            batches[i] =
                    _anjay_batch_acquire(newest_value(observation)->values[i]);
        }

        if (!should_update_batch
                && (has_pmax_expired(newest_value(observation),
                                     &attrs.standard.common)
                    || should_update(&observation->paths[i], &attrs.standard,
                                     newest_value(observation)->values[i],
                                     batches[i]))) {
            should_update_batch = true;
        }

        update_batch_pmax(&pmax, &attrs);
#    ifdef ANJAY_WITH_CON_ATTR
        con = AVS_MAX(con, attrs.custom.data.con);
#    endif // ANJAY_WITH_CON_ATTR
    }

    if (should_update_batch) {
        if (con < 0 && anjay->observe.confirmable_notifications) {
            con = ANJAY_DM_CON_ATTR_CON;
        }

        avs_coap_notify_reliability_hint_t reliability_hint =
                (con > 0) ? AVS_COAP_NOTIFY_PREFER_CONFIRMABLE
                          : AVS_COAP_NOTIFY_PREFER_NON_CONFIRMABLE;
        result = insert_new_value(conn_state, observation, reliability_hint,
                                  &newest_value(observation)->details,
                                  cast_to_const_batch_array(batches));
    }

    if (!result && pmax >= 0) {
        schedule_trigger(conn_state, observation, pmax);
    }

finish:
    delete_batch_array(&batches, observation->paths_count);
    return result;
}

static void trigger_observe(avs_sched_t *sched, const void *args_) {
    (void) sched;
    const trigger_observe_args_t *args = (const trigger_observe_args_t *) args_;
    assert(args->conn_state);
    assert(args->observation);
    bool ready_for_notifying =
            _anjay_connection_ready_for_outgoing_message(
                    args->conn_state->conn_ref)
            && _anjay_socket_transport_is_online(
                       _anjay_from_server(args->conn_state->conn_ref.server),
                       _anjay_connection_transport(args->conn_state->conn_ref));
    if (ready_for_notifying
            || notification_storing_enabled(args->conn_state->conn_ref)) {
        int result =
                update_notification_value(args->conn_state, args->observation);
        if (result) {
            insert_error(args->conn_state, args->observation, result);
        }
    }
    if (ready_for_notifying && args->conn_state->unsent
            && !avs_coap_exchange_id_valid(
                       args->conn_state->notify_exchange_id)) {
        avs_sched_del(&args->conn_state->flush_task);
        assert(!args->conn_state->flush_task);
        if (_anjay_connection_get_online_socket(args->conn_state->conn_ref)) {
            flush_next_unsent(args->conn_state);
        } else if (_anjay_server_registration_info(
                           args->conn_state->conn_ref.server)
                           ->queue_mode) {
            _anjay_connection_bring_online(args->conn_state->conn_ref);
            // once the connection is up, _anjay_observe_sched_flush()
            // will be called; we're done here
        } else if (!notification_storing_enabled(args->conn_state->conn_ref)) {
            remove_all_unsent_values(args->conn_state);
        }
    }
}

static anjay_dm_oi_attributes_t
get_oi_attributes(anjay_observe_connection_entry_t *connection,
                  anjay_observe_path_entry_t *path_entry) {
    anjay_dm_internal_r_attrs_t attrs = ANJAY_DM_INTERNAL_R_ATTRS_EMPTY;
    if (get_effective_attrs(_anjay_from_server(connection->conn_ref.server),
                            &attrs, &path_entry->path,
                            _anjay_server_ssid(connection->conn_ref.server))) {
        return ANJAY_DM_OI_ATTRIBUTES_EMPTY;
    }
    return attrs.standard.common;
}

static int notify_path_changed(anjay_observe_connection_entry_t *connection,
                               anjay_observe_path_entry_t *path_entry,
                               void *result_ptr) {
    int32_t period = get_oi_attributes(connection, path_entry).min_period;
    period = AVS_MAX(period, 0);

    AVS_LIST(AVS_RBTREE_ELEM(anjay_observation_t)) ref;
    AVS_LIST_FOREACH(ref, path_entry->refs) {
        assert(ref);
        assert(*ref);
        _anjay_update_ret((int *) result_ptr,
                          schedule_trigger(connection, *ref, period));
    }
    return 0;
}

typedef int
observe_for_each_matching_clb_t(anjay_observe_connection_entry_t *connection,
                                anjay_observe_path_entry_t *path_entry,
                                void *arg);

static int
observe_for_each_in_bounds(anjay_observe_connection_entry_t *connection,
                           const anjay_uri_path_t *lower_bound,
                           const anjay_uri_path_t *upper_bound,
                           observe_for_each_matching_clb_t *clb,
                           void *clb_arg) {
    int retval = 0;
    AVS_RBTREE_ELEM(anjay_observe_path_entry_t) it =
            AVS_RBTREE_LOWER_BOUND(connection->observed_paths,
                                   path_entry_query(lower_bound));
    AVS_RBTREE_ELEM(anjay_observe_path_entry_t) end =
            AVS_RBTREE_UPPER_BOUND(connection->observed_paths,
                                   path_entry_query(upper_bound));
    // if it == NULL, end must also be NULL
    assert(it || !end);

    for (; it != end; it = AVS_RBTREE_ELEM_NEXT(it)) {
        assert(it);
        if ((retval = clb(connection, it, clb_arg))) {
            return retval;
        }
    }
    return 0;
}

static int
observe_for_each_in_wildcard(anjay_observe_connection_entry_t *connection,
                             const anjay_uri_path_t *specimen_path,
                             anjay_id_type_t wildcard_level,
                             observe_for_each_matching_clb_t *clb,
                             void *clb_arg) {
    anjay_uri_path_t path = *specimen_path;
    for (int i = wildcard_level; i < _ANJAY_URI_PATH_MAX_LENGTH; ++i) {
        path.ids[i] = ANJAY_ID_INVALID;
        path.ids[i] = ANJAY_ID_INVALID;
    }
    return observe_for_each_in_bounds(connection, &path, &path, clb, clb_arg);
}

/**
 * Calls <c>clb()</c> on all registered Observe path entries that match
 * <c>path</c>.
 *
 * This is harder than may seem at the first glance, because both <c>path</c>
 * (the query) and keys of the registered Observe path entries may contain
 * wildcards.
 *
 * An observation may be registered for either of:
 * - A whole object (OID)
 * - A whole object instance (OID+IID)
 * - A specific resource (OID+IID+RID)
 * - A specific resource instance (OID+IID+RID+RIID)
 *
 * Each of these cases needs to be addressed in a slightly different manner.
 *
 * Wildcard representation
 * -----------------------
 * A wildcard for any type of ID is represented as the number 65535. The
 * registered observation entries for any given connection are stored in a
 * sorted tree, with the sort key being (OID, IID, RID, RIID) - in
 * lexicographical order over all elements of that tuple - much like C++11's
 * <c>std::tuple</c> comparison operators.
 *
 * Example: querying for OID+IID
 * -----------------------------
 * When the queried path is only OID+IID, we actually perform three searches:
 * - the entry for the root path, i.e. (U16_MAX, U16_MAX, U16_MAX, U16_MAX)
 * - the entry for the Object, i.e. (OID, U16_MAX, U16_MAX, U16_MAX)
 * - entries between (OID, IID, 0, 0) and (OID, IID, U16_MAX, U16_MAX), i.e.
 *   entries for the Instance or any Resources or Resource Instances under that
 *   Object Instance
 *
 * For paths of different lengths, there will be appropriately more or less
 * wildcard searches. If the search term is the root path, only the final
 * bounded search (between (0, 0, 0, 0) and
 * (U16_MAX, U16_MAX, U16_MAX, U16_MAX)) will be performed. If the search term
 * is OID+IID+RID+RIID, there will be five searches - for parent paths of all
 * lengths up to OID+IID+RID, and the final search for the actual search term
 * path.
 */
static int
observe_for_each_matching(anjay_observe_connection_entry_t *connection,
                          const anjay_uri_path_t *path,
                          observe_for_each_matching_clb_t *clb,
                          void *clb_arg) {
    int retval = 0;
    anjay_uri_path_t lower_bound = *path;
    anjay_uri_path_t upper_bound = *path;

    size_t path_length = _anjay_uri_path_length(path);
    for (size_t i = 0; i < path_length; ++i) {
        if ((retval = observe_for_each_in_wildcard(
                     connection, path, (anjay_id_type_t) i, clb, clb_arg))) {
            goto finish;
        }
    }

    for (size_t i = path_length; i < _ANJAY_URI_PATH_MAX_LENGTH; ++i) {
        lower_bound.ids[i] = 0;
        upper_bound.ids[i] = ANJAY_ID_INVALID;
    }

    retval = observe_for_each_in_bounds(connection, &lower_bound, &upper_bound,
                                        clb, clb_arg);
finish:
    return retval == ANJAY_FOREACH_BREAK ? 0 : retval;
}

static int observe_notify_impl(anjay_t *anjay,
                               const anjay_uri_path_t *path,
                               anjay_ssid_t ssid,
                               bool invert_server_match,
                               observe_for_each_matching_clb_t *clb) {
    // iterate through all SSIDs we have
    int result = 0;
    AVS_LIST(anjay_observe_connection_entry_t) connection;
    AVS_LIST_FOREACH(connection, anjay->observe.connection_entries) {
        /* Some compilers complain about promotion of comparison result, so
         * we're casting it to bool explicitly */
        if ((bool) (_anjay_server_ssid(connection->conn_ref.server) == ssid)
                == invert_server_match) {
            continue;
        }
        observe_for_each_matching(connection, path, clb, &result);
    }
    return result;
}

int _anjay_observe_notify(anjay_t *anjay,
                          const anjay_uri_path_t *path,
                          anjay_ssid_t ssid,
                          bool invert_ssid_match) {
    // This extra level of indirection is required to be able to mock
    // notify_path_changed in unit tests.
    // Hopefully compilers will inline it in production builds.
    return observe_notify_impl(anjay, path, ssid, invert_ssid_match,
                               notify_path_changed);
}

#    ifdef ANJAY_WITH_OBSERVATION_STATUS
static int get_observe_status(anjay_observe_connection_entry_t *connection,
                              anjay_observe_path_entry_t *entry,
                              void *out_status_) {
    anjay_resource_observation_status_t *out_status =
            (anjay_resource_observation_status_t *) out_status_;
    anjay_dm_oi_attributes_t attrs = get_oi_attributes(connection, entry);
    out_status->is_observed = true;
    if (attrs.min_period != ANJAY_ATTRIB_PERIOD_NONE
            && (attrs.min_period < out_status->min_period
                || out_status->min_period == ANJAY_ATTRIB_PERIOD_NONE)) {
        out_status->min_period = attrs.min_period;
    }
    if (attrs.max_eval_period != ANJAY_ATTRIB_PERIOD_NONE
            && (attrs.max_eval_period < out_status->max_eval_period
                || out_status->max_eval_period == ANJAY_ATTRIB_PERIOD_NONE)) {
        out_status->max_eval_period = attrs.max_eval_period;
    }
    return 0;
}

anjay_resource_observation_status_t _anjay_observe_status(anjay_t *anjay,
                                                          anjay_oid_t oid,
                                                          anjay_iid_t iid,
                                                          anjay_rid_t rid) {
    assert(oid != ANJAY_ID_INVALID);
    assert(iid != ANJAY_ID_INVALID);
    assert(rid != ANJAY_ID_INVALID);

    anjay_resource_observation_status_t result = {
        .is_observed = false,
        .min_period = ANJAY_ATTRIB_PERIOD_NONE,
        .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
    };
    AVS_LIST(anjay_observe_connection_entry_t) connection;
    AVS_LIST_FOREACH(connection, anjay->observe.connection_entries) {
        int retval =
                observe_for_each_matching(connection,
                                          &MAKE_RESOURCE_PATH(oid, iid, rid),
                                          get_observe_status, &result);
        assert(!retval);
        (void) retval;
    }
    result.min_period = AVS_MAX(result.min_period, 0);

    return result;
}
#    endif // ANJAY_WITH_OBSERVATION_STATUS

#    ifdef ANJAY_TEST
#        include "tests/core/observe/observe.c"
#    endif // ANJAY_TEST

#endif // ANJAY_WITH_OBSERVE
