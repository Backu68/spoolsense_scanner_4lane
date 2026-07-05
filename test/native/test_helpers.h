#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>

static int _tests_run = 0;
static int _tests_passed = 0;
static int _tests_failed = 0;

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return false; \
    } \
} while(0)

#define TEST_ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("  FAIL: %s != %s (line %d)\n", #a, #b, __LINE__); \
        return false; \
    } \
} while(0)

#define TEST_ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        printf("  FAIL: \"%s\" != \"%s\" (line %d)\n", (a), (b), __LINE__); \
        return false; \
    } \
} while(0)

#define TEST_ASSERT_FLOAT_NEAR(a, b, tol) do { \
    float _diff = (a) - (b); \
    if (_diff < 0) _diff = -_diff; \
    if (_diff > (tol)) { \
        printf("  FAIL: %f !~ %f (tol=%f, line %d)\n", (double)(a), (double)(b), (double)(tol), __LINE__); \
        return false; \
    } \
} while(0)

#define RUN_TEST(func) do { \
    _tests_run++; \
    printf("  %s... ", #func); \
    if (func()) { \
        _tests_passed++; \
        printf("PASS\n"); \
    } else { \
        _tests_failed++; \
    } \
} while(0)

static inline void print_test_summary() {
    printf("\n%d tests, %d passed, %d failed\n", _tests_run, _tests_passed, _tests_failed);
}
