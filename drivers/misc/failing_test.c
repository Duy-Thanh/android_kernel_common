#include <kunit/test.h>

/* See b/402985621. */
static void always_fails(struct kunit *test)
{
        KUNIT_FAIL(test, "This test never passes.");
}

static struct kunit_case failing_test_cases[] = {
        KUNIT_CASE(always_fails),
        {}
};

static struct kunit_suite failing_test_suite = {
        .name = "failing-test",
        .test_cases = failing_test_cases,
};
kunit_test_suite(failing_test_suite);

MODULE_LICENSE("GPL");
