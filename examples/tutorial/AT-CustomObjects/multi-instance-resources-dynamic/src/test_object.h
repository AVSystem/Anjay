#ifndef TEST_OBJECT_H
#define TEST_OBJECT_H

#include <anjay/anjay.h>

const anjay_dm_object_def_t **create_test_object(void);
void delete_test_object(const anjay_dm_object_def_t **obj);

#endif /* TEST_OBJECT_H */
