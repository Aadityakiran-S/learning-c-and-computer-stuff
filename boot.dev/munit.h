/* Simplified munit.h for boot.dev course exercises */
#ifndef MUNIT_H
#define MUNIT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Enable assert aliases */
#define MUNIT_ENABLE_ASSERT_ALIASES

/* Test result type */
typedef enum {
    MUNIT_OK,
    MUNIT_FAIL,
    MUNIT_SKIP,
    MUNIT_ERROR
} MunitResult;

/* Test function type */
typedef MunitResult (*MunitTestFunc)(void*, void*);

/* Test structure */
typedef struct {
    const char* name;
    MunitTestFunc test;
} MunitTest;

/* Suite structure */
typedef struct {
    const char* prefix;
    MunitTest* tests;
} MunitSuite;

/* Test categories */
#define RUN 0
#define SUBMIT 1

/* Macro to define a test case */
#define munit_case(category, name, body) \
    MunitResult name(void* params, void* data) { \
        (void)params; (void)data; \
        body \
        return MUNIT_OK; \
    }

/* Macro to create a test entry */
#define munit_test(name_str, func) { name_str, func }

/* Null terminator for test array */
#define munit_null_test { NULL, NULL }

/* Macro to create a suite */
#define munit_suite(prefix, tests) { prefix, tests }

/* Assert macros */
#define assert_int(a, op, b, msg) \
    do { \
        int _a = (a); \
        int _b = (b); \
        if (!(_a op _b)) { \
            fprintf(stderr, "FAIL: %s\n", msg); \
            fprintf(stderr, "  Expected: %s %s %s\n", #a, #op, #b); \
            fprintf(stderr, "  Got: %d %s %d\n", _a, #op, _b); \
            return MUNIT_FAIL; \
        } \
    } while(0)

#define assert_string_equal(a, b, msg) \
    do { \
        const char* _a = (a); \
        const char* _b = (b); \
        if (strcmp(_a, _b) != 0) { \
            fprintf(stderr, "FAIL: %s\n", msg); \
            fprintf(stderr, "  Expected: \"%s\"\n", _b); \
            fprintf(stderr, "  Got: \"%s\"\n", _a); \
            return MUNIT_FAIL; \
        } \
    } while(0)

#define assert_true(expr, msg) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "FAIL: %s\n", msg); \
            fprintf(stderr, "  Expression was false: %s\n", #expr); \
            return MUNIT_FAIL; \
        } \
    } while(0)

#define assert_false(expr, msg) \
    do { \
        if (expr) { \
            fprintf(stderr, "FAIL: %s\n", msg); \
            fprintf(stderr, "  Expression was true: %s\n", #expr); \
            return MUNIT_FAIL; \
        } \
    } while(0)

#define assert_ptr_equal(a, b, msg) \
    do { \
        const void* _a = (a); \
        const void* _b = (b); \
        if (_a != _b) { \
            fprintf(stderr, "FAIL: %s\n", msg); \
            fprintf(stderr, "  Expected: %p\n", _b); \
            fprintf(stderr, "  Got: %p\n", _a); \
            return MUNIT_FAIL; \
        } \
    } while(0)

#define assert_null(ptr, msg) assert_ptr_equal(ptr, NULL, msg)
#define assert_not_null(ptr, msg) assert_true((ptr) != NULL, msg)

/* munit_ prefixed aliases (some boot.dev tests use these) */
#define munit_assert_int(a, op, b, msg) assert_int(a, op, b, msg)
#define munit_assert_string_equal(a, b, msg) assert_string_equal(a, b, msg)
#define munit_assert_true(expr, msg) assert_true(expr, msg)
#define munit_assert_false(expr, msg) assert_false(expr, msg)
#define munit_assert_null(ptr, msg) assert_null(ptr, msg)
#define munit_assert_not_null(ptr, msg) assert_not_null(ptr, msg)
#define munit_assert_ptr_equal(a, b, msg) assert_ptr_equal(a, b, msg)

/* Main function to run suite */
static inline int munit_suite_main(const MunitSuite* suite, void* user_data, int argc, char* const argv[]) {
    (void)user_data;
    (void)argc;
    (void)argv;
    
    int passed = 0;
    int failed = 0;
    int total = 0;
    
    printf("\n=== Running test suite: %s ===\n\n", suite->prefix);
    
    for (int i = 0; suite->tests[i].name != NULL; i++) {
        total++;
        printf("Test %s%s ... ", suite->prefix, suite->tests[i].name);
        fflush(stdout);
        
        MunitResult result = suite->tests[i].test(NULL, NULL);
        
        if (result == MUNIT_OK) {
            printf("\033[32mPASSED\033[0m\n");
            passed++;
        } else if (result == MUNIT_SKIP) {
            printf("\033[33mSKIPPED\033[0m\n");
        } else {
            printf("\033[31mFAILED\033[0m\n");
            failed++;
        }
    }
    
    printf("\n=== Results: %d/%d passed", passed, total);
    if (failed > 0) {
        printf(", \033[31m%d failed\033[0m", failed);
    }
    printf(" ===\n\n");
    
    return failed > 0 ? 1 : 0;
}

#endif /* MUNIT_H */
