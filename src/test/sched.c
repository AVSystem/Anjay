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

#include <anjay_test/mock_clock.h>
#include <avsystem/commons/unit/test.h>

static void increment_task(anjay_t *anjay, const void *counter_ptr_ptr) {
    (void) anjay;
    ++**(int *const *) counter_ptr_ptr;
}

typedef struct {
    anjay_sched_t *sched;
} sched_test_env_t;

static sched_test_env_t setup_test(void) {
    _anjay_mock_clock_start(avs_time_monotonic_from_scalar(0, AVS_TIME_S));
    return (sched_test_env_t) { _anjay_sched_new(NULL) };
}

static void teardown_test(sched_test_env_t *env) {
    _anjay_mock_clock_finish();
    _anjay_sched_delete(&env->sched);
}

AVS_UNIT_TEST(sched, sched_now) {
    sched_test_env_t env = setup_test();

    int counter = 0;
    anjay_sched_handle_t task = NULL;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_sched_now(env.sched, &task, increment_task,
                                             &(int *) { &counter },
                                             sizeof(int *)));
    AVS_UNIT_ASSERT_NOT_NULL(task);
    AVS_UNIT_ASSERT_EQUAL(1, _anjay_sched_run(env.sched));
    AVS_UNIT_ASSERT_EQUAL(1, counter);
    AVS_UNIT_ASSERT_NULL(task);

    teardown_test(&env);
}

AVS_UNIT_TEST(sched, sched_delayed) {
    sched_test_env_t env = setup_test();

    const avs_time_duration_t delay =
            avs_time_duration_from_scalar(1, AVS_TIME_S);
    int counter = 0;
    anjay_sched_handle_t task = NULL;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_sched(env.sched, &task, delay,
                                         increment_task, &(int *) { &counter },
                                         sizeof(int *)));
    AVS_UNIT_ASSERT_NOT_NULL(task);
    AVS_UNIT_ASSERT_EQUAL(0, _anjay_sched_run(env.sched));
    AVS_UNIT_ASSERT_EQUAL(0, counter);
    AVS_UNIT_ASSERT_NOT_NULL(task);

    _anjay_mock_clock_advance(delay);
    AVS_UNIT_ASSERT_EQUAL(1, _anjay_sched_run(env.sched));
    AVS_UNIT_ASSERT_EQUAL(1, counter);
    AVS_UNIT_ASSERT_NULL(task);

    teardown_test(&env);
}

AVS_UNIT_TEST(sched, sched_del) {
    sched_test_env_t env = setup_test();

    const avs_time_duration_t delay =
            avs_time_duration_from_scalar(1, AVS_TIME_S);
    int counter = 0;
    anjay_sched_handle_t task = NULL;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_sched(env.sched, &task, delay,
                                         increment_task, &(int *) { &counter },
                                         sizeof(int *)));
    AVS_UNIT_ASSERT_NOT_NULL(task);
    AVS_UNIT_ASSERT_EQUAL(0, _anjay_sched_run(env.sched));
    AVS_UNIT_ASSERT_EQUAL(0, counter);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_sched_del(env.sched, &task));
    AVS_UNIT_ASSERT_NULL(task);

    _anjay_mock_clock_advance(delay);
    AVS_UNIT_ASSERT_EQUAL(0, _anjay_sched_run(env.sched));
    AVS_UNIT_ASSERT_EQUAL(0, counter);

    teardown_test(&env);
}

typedef struct {
    anjay_sched_handle_t task;
    int n;
} global_t;

static void assert_task_null_oneshot_job(anjay_t *anjay, const void *context) {
    (void) anjay;
    global_t *global = *(global_t *const *) context;
    AVS_UNIT_ASSERT_NULL(global->task);
}

AVS_UNIT_TEST(sched, oneshot_job_handle_nullification) {
    sched_test_env_t env = setup_test();

    global_t global = { NULL, 0 };
    AVS_UNIT_ASSERT_SUCCESS(_anjay_sched_now(
            env.sched, &global.task, assert_task_null_oneshot_job,
            &(global_t *) { &global }, sizeof(global_t *)));
    AVS_UNIT_ASSERT_NOT_NULL(global.task);
    AVS_UNIT_ASSERT_EQUAL(1, _anjay_sched_run(env.sched));
    AVS_UNIT_ASSERT_NULL(global.task);
    teardown_test(&env);
}
