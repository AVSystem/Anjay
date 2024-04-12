/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <avsystem/commons/avs_unit_test.h>

AVS_UNIT_TEST(DataModelCoreStatics, DefaultDepthObject) {
    fluf_uri_path_t uri = FLUF_MAKE_OBJECT_PATH(1);
    AVS_UNIT_ASSERT_EQUAL(infer_depth(&uri, NULL), FLUF_ID_RID);
}

AVS_UNIT_TEST(DataModelCoreStatics, DefaultDepthInstance) {
    fluf_uri_path_t uri = FLUF_MAKE_INSTANCE_PATH(1, 1);
    AVS_UNIT_ASSERT_EQUAL(infer_depth(&uri, NULL), FLUF_ID_RID);
}

AVS_UNIT_TEST(DataModelCoreStatics, DefaultDepthResource) {
    fluf_uri_path_t uri = FLUF_MAKE_RESOURCE_PATH(1, 1, 1);
    AVS_UNIT_ASSERT_EQUAL(infer_depth(&uri, NULL), FLUF_ID_RIID);
}

AVS_UNIT_TEST(DataModelCoreStatics, ArbitraryDepthObject) {
    fluf_uri_path_t uri = FLUF_MAKE_OBJECT_PATH(1);
    uint8_t depth = 0;
    AVS_UNIT_ASSERT_EQUAL(infer_depth(&uri, &depth), FLUF_ID_OID);
    depth = 1;
    AVS_UNIT_ASSERT_EQUAL(infer_depth(&uri, &depth), FLUF_ID_IID);
    depth = 2;
    AVS_UNIT_ASSERT_EQUAL(infer_depth(&uri, &depth), FLUF_ID_RID);
    depth = 3;
    AVS_UNIT_ASSERT_EQUAL(infer_depth(&uri, &depth), FLUF_ID_RIID);

    // depth above permitted value
    depth = 4;
    AVS_UNIT_ASSERT_EQUAL(infer_depth(&uri, &depth), FLUF_ID_RIID);
}

AVS_UNIT_TEST(DataModelCoreStatics, ArbitraryDepthInstance) {
    fluf_uri_path_t uri = FLUF_MAKE_INSTANCE_PATH(1, 1);
    uint8_t depth = 0;
    AVS_UNIT_ASSERT_EQUAL(infer_depth(&uri, &depth), FLUF_ID_IID);
    depth = 1;
    AVS_UNIT_ASSERT_EQUAL(infer_depth(&uri, &depth), FLUF_ID_RID);
    depth = 2;
    AVS_UNIT_ASSERT_EQUAL(infer_depth(&uri, &depth), FLUF_ID_RIID);

    // depth above logical value
    depth = 3;
    AVS_UNIT_ASSERT_EQUAL(infer_depth(&uri, &depth), FLUF_ID_RIID);
}

AVS_UNIT_TEST(DataModelCoreStatics, ArbitraryDepthResource) {
    fluf_uri_path_t uri = FLUF_MAKE_RESOURCE_PATH(1, 1, 1);
    uint8_t depth = 0;
    AVS_UNIT_ASSERT_EQUAL(infer_depth(&uri, &depth), FLUF_ID_RID);
    depth = 1;
    AVS_UNIT_ASSERT_EQUAL(infer_depth(&uri, &depth), FLUF_ID_RIID);

    // depth above logical value
    depth = 2;
    AVS_UNIT_ASSERT_EQUAL(infer_depth(&uri, &depth), FLUF_ID_RIID);
}
