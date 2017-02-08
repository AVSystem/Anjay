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

#include <avsystem/commons/unit/test.h>
#include <anjay_test/mock_clock.h>

static int increment_task(anjay_t *anjay,
                          void *counter_) {
    (void)anjay;
    ++*(int*)counter_;
    return 0;
}

static int increment_and_fail_task(anjay_t *anjay,
                                   void *counter_) {
    (void)anjay;
    ++*(int*)counter_;
    return -1;
}

static int return_int_task(anjay_t *anjay,
                           void *value_) {
    (void)anjay;
    return *(int*)value_;
}

typedef struct {
    anjay_sched_t *sched;
} sched_test_env_t;

static sched_test_env_t setup_test(void) {
    _anjay_mock_clock_start(&ANJAY_TIME_ZERO);
    return (sched_test_env_t){
        _anjay_sched_new(NULL)
    };
}

static void teardown_test(sched_test_env_t *env) {
    _anjay_mock_clock_finish();
    _anjay_sched_delete(&env->sched);
}

AVS_UNIT_TEST(sched, sched_now) {
    sched_test_env_t env = setup_test();

    int counter = 0;
    anjay_sched_handle_t task = NULL;
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_sched_now(env.sched, &task, increment_task, &counter));
    AVS_UNIT_ASSERT_NOT_NULL(task);
    AVS_UNIT_ASSERT_EQUAL(1, _anjay_sched_run(env.sched));
    AVS_UNIT_ASSERT_EQUAL(1, counter);
    AVS_UNIT_ASSERT_NULL(task);

    teardown_test(&env);
}

AVS_UNIT_TEST(sched, sched_delayed) {
    sched_test_env_t env = setup_test();

    const struct timespec delay = { 1, 0 };
    int counter = 0;
    anjay_sched_handle_t task = NULL;
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_sched(env.sched, &task, delay, increment_task, &counter));
    AVS_UNIT_ASSERT_NOT_NULL(task);
    AVS_UNIT_ASSERT_EQUAL(0, _anjay_sched_run(env.sched));
    AVS_UNIT_ASSERT_EQUAL(0, counter);
    AVS_UNIT_ASSERT_NOT_NULL(task);

    _anjay_mock_clock_advance(&delay);
    AVS_UNIT_ASSERT_EQUAL(1, _anjay_sched_run(env.sched));
    AVS_UNIT_ASSERT_EQUAL(1, counter);
    AVS_UNIT_ASSERT_NULL(task);

    teardown_test(&env);
}

AVS_UNIT_TEST(sched, sched_del) {
    sched_test_env_t env = setup_test();

    const struct timespec delay = { 1, 0 };
    int counter = 0;
    anjay_sched_handle_t task = NULL;
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_sched(env.sched, &task, delay, increment_task, &counter));
    AVS_UNIT_ASSERT_NOT_NULL(task);
    AVS_UNIT_ASSERT_EQUAL(0, _anjay_sched_run(env.sched));
    AVS_UNIT_ASSERT_EQUAL(0, counter);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_sched_del(env.sched, &task));
    AVS_UNIT_ASSERT_NULL(task);

    _anjay_mock_clock_advance(&delay);
    AVS_UNIT_ASSERT_EQUAL(0, _anjay_sched_run(env.sched));
    AVS_UNIT_ASSERT_EQUAL(0, counter);

    teardown_test(&env);
}

static void assert_executes_after_delay(sched_test_env_t *env,
                                        struct timespec delay) {
    struct timespec epsilon;
    _anjay_time_from_ms(&epsilon, 1 * 1000 * 1000);

    struct timespec time_to_next;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_sched_time_to_next(env->sched,
                                                      &time_to_next));

    AVS_UNIT_ASSERT_TRUE(_anjay_time_before(&time_to_next, &delay));
    _anjay_time_diff(&delay, &delay, &epsilon);
    AVS_UNIT_ASSERT_TRUE(_anjay_time_before(&delay, &time_to_next));

    _anjay_mock_clock_advance(&time_to_next);
    AVS_UNIT_ASSERT_EQUAL(1, _anjay_sched_run(env->sched));
}

AVS_UNIT_TEST(sched, retryable_retry) {
    sched_test_env_t env = setup_test();

    const anjay_sched_retryable_backoff_t backoff = {
        .delay = { 1, 0 },
        .max_delay = { 5, 0 }
    };

    int counter = 0;
    anjay_sched_handle_t task = NULL;
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_sched_retryable(env.sched, &task, ANJAY_TIME_ZERO, backoff,
                                   increment_and_fail_task, &counter));
    AVS_UNIT_ASSERT_NOT_NULL(task);

    // initial execution
    AVS_UNIT_ASSERT_EQUAL(1, _anjay_sched_run(env.sched));
    AVS_UNIT_ASSERT_EQUAL(1, counter);
    AVS_UNIT_ASSERT_NOT_NULL(task);

    // first retry
    struct timespec delay = backoff.delay;
    assert_executes_after_delay(&env, delay);
    AVS_UNIT_ASSERT_EQUAL(2, counter);
    AVS_UNIT_ASSERT_NOT_NULL(task);

    // second retry
    _anjay_time_add(&delay, &delay);
    assert_executes_after_delay(&env, delay);
    AVS_UNIT_ASSERT_EQUAL(3, counter);
    AVS_UNIT_ASSERT_NOT_NULL(task);

    // third retry
    _anjay_time_add(&delay, &delay);
    assert_executes_after_delay(&env, delay);
    AVS_UNIT_ASSERT_EQUAL(4, counter);
    AVS_UNIT_ASSERT_NOT_NULL(task);

    // following attempts should use max_delay
    delay = backoff.max_delay;
    assert_executes_after_delay(&env, delay);
    AVS_UNIT_ASSERT_EQUAL(5, counter);
    AVS_UNIT_ASSERT_NOT_NULL(task);

    assert_executes_after_delay(&env, delay);
    AVS_UNIT_ASSERT_EQUAL(6, counter);
    AVS_UNIT_ASSERT_NOT_NULL(task);

    teardown_test(&env);
    AVS_UNIT_ASSERT_NULL(task);
}

AVS_UNIT_TEST(sched, retryable_success) {
    sched_test_env_t env = setup_test();

    const anjay_sched_retryable_backoff_t backoff = {
        .delay = { 1, 0 },
        .max_delay = { 4, 0 }
    };

    int counter = 0;
    anjay_sched_handle_t task = NULL;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_sched_retryable(env.sched, &task,
                                                   ANJAY_TIME_ZERO, backoff,
                                                   increment_task, &counter));
    AVS_UNIT_ASSERT_NOT_NULL(task);

    // initial execution - success
    AVS_UNIT_ASSERT_EQUAL(1, _anjay_sched_run(env.sched));
    AVS_UNIT_ASSERT_EQUAL(1, counter);
    AVS_UNIT_ASSERT_NULL(task);

    // the task should not be repeated after success
    struct timespec time_to_next;
    AVS_UNIT_ASSERT_FAILED(_anjay_sched_time_to_next(env.sched, &time_to_next));

    teardown_test(&env);
}

AVS_UNIT_TEST(sched, retryable_retry_then_success) {
    sched_test_env_t env = setup_test();

    const anjay_sched_retryable_backoff_t backoff = {
        .delay = { 1, 0 },
        .max_delay = { 4, 0 }
    };

    int counter = 1;
    anjay_sched_handle_t task = NULL;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_sched_retryable(env.sched, &task,
                                                   ANJAY_TIME_ZERO, backoff,
                                                   return_int_task, &counter));
    AVS_UNIT_ASSERT_NOT_NULL(task);

    // initial execution - fail
    AVS_UNIT_ASSERT_EQUAL(1, _anjay_sched_run(env.sched));
    AVS_UNIT_ASSERT_NOT_NULL(task);

    // first retry - succeed
    counter = 0;
    assert_executes_after_delay(&env, backoff.delay);
    AVS_UNIT_ASSERT_NULL(task);

    // the task should not be repeated after success
    struct timespec time_to_next;
    AVS_UNIT_ASSERT_FAILED(_anjay_sched_time_to_next(env.sched, &time_to_next));

    teardown_test(&env);
}

typedef struct {
    anjay_sched_handle_t task;
    int n;
} global_t;

static int assert_task_null_oneshot_job(anjay_t *anjay, void *context) {
    (void) anjay;
    global_t *global = (global_t *) context;
    AVS_UNIT_ASSERT_NULL(global->task);
    return 0;
}

AVS_UNIT_TEST(sched, oneshot_job_handle_nullification) {
    sched_test_env_t env = setup_test();

    global_t global = { NULL, 0 };
    AVS_UNIT_ASSERT_SUCCESS(_anjay_sched_now(
            env.sched, &global.task, assert_task_null_oneshot_job, &global));
    AVS_UNIT_ASSERT_NOT_NULL(global.task);
    AVS_UNIT_ASSERT_EQUAL(1, _anjay_sched_run(env.sched));
    AVS_UNIT_ASSERT_NULL(global.task);
    teardown_test(&env);
}

static int assert_task_null_retryable_job(anjay_t *anjay, void *context) {
    (void) anjay;
    global_t *global = (global_t *) context;
    AVS_UNIT_ASSERT_NULL(global->task);
    if (global->n < 2) {
        global->n++;
        return -1;
    }
    return 0;
}
AVS_UNIT_TEST(sched, retryable_job_handle_nullification) {
    sched_test_env_t env = setup_test();
    const anjay_sched_retryable_backoff_t backoff = {
        .delay = { 1, 0 },
        .max_delay = { 5, 0 }
    };
    global_t global = { NULL, 0 };
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_sched_retryable(env.sched, &global.task, ANJAY_TIME_ZERO, backoff,
                                   assert_task_null_retryable_job, &global));
    AVS_UNIT_ASSERT_NOT_NULL(global.task);

    // Failure (n == 0)
    AVS_UNIT_ASSERT_EQUAL(1, _anjay_sched_run(env.sched));
    AVS_UNIT_ASSERT_EQUAL(1, global.n);
    AVS_UNIT_ASSERT_NOT_NULL(global.task);

    // Failure (n == 1)
    struct timespec delay = backoff.delay;
    assert_executes_after_delay(&env, delay);
    AVS_UNIT_ASSERT_EQUAL(2, global.n);
    AVS_UNIT_ASSERT_NOT_NULL(global.task);

    // Success (n == 2)
    _anjay_time_add(&delay, &delay);
    assert_executes_after_delay(&env, delay);
    AVS_UNIT_ASSERT_EQUAL(2, global.n);
    AVS_UNIT_ASSERT_NULL(global.task);
    teardown_test(&env);
}
