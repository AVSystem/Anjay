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

#include <avsystem/commons/unit/mock_helpers.h>
#include <avsystem/commons/unit/test.h>

#include <avsystem/commons/stream/stream_outbuf.h>
#include <avsystem/commons/stream/stream_inbuf.h>

#include <anjay/access_control.h>
#include <anjay/anjay.h>

#include "../access_control.h"

static int null_instance_it(anjay_t *anjay,
                            const anjay_dm_object_def_t *const *obj_ptr,
                            anjay_iid_t *out,
                            void **cookie) {
    (void) anjay;
    (void) obj_ptr;
    (void) cookie;
    *out = ANJAY_IID_INVALID;
    return 0;
}

static anjay_dm_object_def_t *make_mock_object(anjay_oid_t oid) {
    anjay_dm_object_def_t *obj =
            (anjay_dm_object_def_t *) calloc(1, sizeof(anjay_dm_object_def_t));
    if (obj) {
        obj->oid = oid;
        obj->instance_it = null_instance_it;
    }
    return obj;
}

static anjay_dm_object_def_t *make_mock_aco(void) {
    anjay_dm_object_def_t *obj =
            (anjay_dm_object_def_t *) calloc(1, sizeof(anjay_dm_object_def_t));
    if (obj) {
        obj->oid = ANJAY_DM_OID_ACCESS_CONTROL;
        obj->transaction_begin = anjay_dm_transaction_NOOP;
        obj->transaction_validate = anjay_dm_transaction_NOOP;
        obj->transaction_commit = anjay_dm_transaction_NOOP;
    }
    return obj;
}

typedef bool comparator_t(const void *a, const void *b);

static bool lists_equal(AVS_LIST(void) a,
                        AVS_LIST(void) b,
                        comparator_t *equals) {
    AVS_LIST(void) p = a;
    AVS_LIST(void) q = b;
    while (p && q) {
        if (!equals(p, q)) {
            return false;
        }
        p = AVS_LIST_NEXT(p);
        q = AVS_LIST_NEXT(q);
    }
    return p == q;
}

static bool acl_entry_equal(const void *a, const void *b) {
    const acl_entry_t *p = (const acl_entry_t *) a;
    const acl_entry_t *q = (const acl_entry_t *) b;
    return p == q || (p->mask == q->mask && p->ssid == q->ssid);
}

static bool instances_equal(const void *a, const void *b) {
    const access_control_instance_t *p = (const access_control_instance_t *) a;
    const access_control_instance_t *q = (const access_control_instance_t *) b;
    return (p == q) || (p->iid == q->iid &&
                        p->target.oid == q->target.oid &&
                        p->target.iid == q->target.iid &&
                        p->owner == q->owner &&
                        lists_equal(p->acl, q->acl, acl_entry_equal));
}

static bool aco_equal(access_control_t *a,
                      access_control_t *b) {
    return lists_equal(a->current.instances, b->current.instances,
                       instances_equal);
}

static anjay_t *ac_test_create_fake_anjay(void) {
    anjay_configuration_t fake_config;
    memset(&fake_config, 0, sizeof(fake_config));
    fake_config.endpoint_name = "fake";
    anjay_t *fake_anjay = anjay_new(&fake_config);
    AVS_UNIT_ASSERT_NOT_NULL(fake_anjay);
    return fake_anjay;
}

typedef struct {
    char buffer[8192];
    avs_stream_inbuf_t in;
    avs_stream_outbuf_t out;
} storage_ctx_t;

static void init_context(storage_ctx_t *ctx) {
    memcpy(&ctx->in, &AVS_STREAM_INBUF_STATIC_INITIALIZER, sizeof(avs_stream_inbuf_t));
    memcpy(&ctx->out, &AVS_STREAM_OUTBUF_STATIC_INITIALIZER, sizeof(avs_stream_outbuf_t));
    ctx->out.buffer = ctx->buffer;
    ctx->out.buffer_size = sizeof(ctx->buffer);
    ctx->in.buffer = ctx->buffer;
}

AVS_UNIT_TEST(access_control_persistence, empty_aco) {
    anjay_t *fake_anjay = ac_test_create_fake_anjay();

    storage_ctx_t ctx = { .buffer = {} };
    init_context(&ctx);
    const anjay_dm_object_def_t *const *aco1 =
            anjay_access_control_object_new(fake_anjay);
    const anjay_dm_object_def_t *const *aco2 =
            anjay_access_control_object_new(fake_anjay);
    AVS_UNIT_ASSERT_SUCCESS(anjay_access_control_persist(aco1, (avs_stream_abstract_t *) &ctx.out));

    ctx.in.buffer_size = avs_stream_outbuf_offset(&ctx.out);
    AVS_UNIT_ASSERT_SUCCESS(anjay_access_control_restore(aco2, (avs_stream_abstract_t *) &ctx.in));
    AVS_UNIT_ASSERT_TRUE(aco_equal(_anjay_access_control_get(aco1),
                                   _anjay_access_control_get(aco2)));
    AVS_UNIT_ASSERT_EQUAL(
            AVS_LIST_SIZE(_anjay_access_control_get(aco1)->current.instances),
            0);
    anjay_access_control_object_delete(aco1);
    anjay_access_control_object_delete(aco2);

    anjay_delete(fake_anjay);
}

AVS_UNIT_TEST(access_control_persistence, normal_usage) {
    anjay_t *fake_anjay = ac_test_create_fake_anjay();

    storage_ctx_t ctx = { .buffer = {} };
    init_context(&ctx);

    const anjay_dm_object_def_t *const *aco1 =
            anjay_access_control_object_new(fake_anjay);
    const anjay_dm_object_def_t *const *aco2 =
            anjay_access_control_object_new(fake_anjay);
    access_control_t *ac1 = _anjay_access_control_get(aco1);
    access_control_t *ac2 = _anjay_access_control_get(aco2);

    const anjay_dm_object_def_t *fake_wrapped_aco = make_mock_aco();

    AVS_UNIT_ASSERT_SUCCESS(anjay_register_object(fake_anjay,
                                                  &fake_wrapped_aco));
    const anjay_dm_object_def_t *mock_obj1 = make_mock_object(32);
    AVS_UNIT_ASSERT_SUCCESS(anjay_register_object(fake_anjay, &mock_obj1));
    const anjay_dm_object_def_t *mock_obj2 = make_mock_object(64);
    AVS_UNIT_ASSERT_SUCCESS(anjay_register_object(fake_anjay, &mock_obj2));
    AVS_LIST(anjay_oid_t) oids_to_sync = NULL;
    *AVS_LIST_APPEND_NEW(anjay_oid_t, &oids_to_sync) = mock_obj1->oid;
    *AVS_LIST_APPEND_NEW(anjay_oid_t, &oids_to_sync) = mock_obj2->oid;
    _anjay_dm_transaction_begin(fake_anjay);
    anjay_notify_queue_t dm_changes = NULL;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_access_control_sync_instances(
            ac1, ANJAY_ACCESS_LIST_OWNER_BOOTSTRAP, oids_to_sync, &dm_changes));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_access_control_sync_instances(
            ac2, ANJAY_ACCESS_LIST_OWNER_BOOTSTRAP, oids_to_sync, &dm_changes));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_dm_transaction_finish(fake_anjay, 0));
    AVS_LIST_CLEAR(&oids_to_sync);
    _anjay_notify_clear_queue(&dm_changes);
    // There are now 2 bootstrap instances.
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(ac1->current.instances), 2);

    AVS_LIST(acl_entry_t) acl1 = NULL;
    AVS_LIST_APPEND(&acl1, AVS_LIST_NEW_ELEMENT(acl_entry_t));
    *acl1 = (acl_entry_t) { .mask = 0xFFFF, .ssid = 1 };
    AVS_LIST_INSERT(&acl1, AVS_LIST_NEW_ELEMENT(acl_entry_t));
    *acl1 = (acl_entry_t) { .mask = 0xDEAD, .ssid = 0xBABE };
    access_control_instance_t instance1 = (access_control_instance_t) {
        .target = {
            .oid = 32,
            .iid = 42
        },
        .iid = 3,
        .owner = 23,
        .has_acl = true,
        .acl = acl1
    };
    access_control_instance_t instance2 = (access_control_instance_t) {
        .target = {
            .oid = 64,
            .iid = 43
        },
        .iid = 4,
        .owner = 32
    };
    AVS_LIST(access_control_instance_t) entry1 =
            AVS_LIST_NEW_ELEMENT(access_control_instance_t);
    AVS_LIST(access_control_instance_t) entry2 =
            AVS_LIST_NEW_ELEMENT(access_control_instance_t);
    *entry1 = instance1;
    *entry2 = instance2;
    AVS_LIST_APPEND(&ac1->current.instances, entry1);
    AVS_LIST_APPEND(&ac1->current.instances, entry2);
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(ac1->current.instances), 4);
    AVS_UNIT_ASSERT_SUCCESS(anjay_access_control_persist(aco1, (avs_stream_abstract_t *) &ctx.out));

    ctx.in.buffer_size = avs_stream_outbuf_offset(&ctx.out);
    AVS_UNIT_ASSERT_SUCCESS(anjay_access_control_restore(aco2, (avs_stream_abstract_t *) &ctx.in));

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(ac2->current.instances), 4);
    AVS_UNIT_ASSERT_TRUE(aco_equal(ac1, ac2));

    anjay_delete(fake_anjay);

    anjay_access_control_object_delete(aco1);
    anjay_access_control_object_delete(aco2);
    free((anjay_dm_object_def_t *) (intptr_t) mock_obj1);
    free((anjay_dm_object_def_t *) (intptr_t) mock_obj2);
    free((anjay_dm_object_def_t *) (intptr_t) fake_wrapped_aco);
}
