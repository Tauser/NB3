#ifndef NB_TEST_H
#define NB_TEST_H

// Minimal host-test helpers shared by every test executable. A failed check
// prints its location and marks the run as failed; NB_TEST_MAIN_END turns
// that into the process exit code ctest reads.

#include <stdio.h>
#include <string.h>

static int nb_test_failures = 0;

#define NB_CHECK(cond)                                                          \
    do {                                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,    \
                    #cond);                                                     \
            nb_test_failures++;                                                 \
        }                                                                       \
    } while (0)

#define NB_CHECK_EQ_INT(expected, actual)                                       \
    do {                                                                        \
        long long nb_e_ = (long long)(expected), nb_a_ = (long long)(actual);   \
        if (nb_e_ != nb_a_) {                                                   \
            fprintf(stderr, "%s:%d: expected %s == %lld, got %lld\n", __FILE__, \
                    __LINE__, #actual, nb_e_, nb_a_);                           \
            nb_test_failures++;                                                 \
        }                                                                       \
    } while (0)

#define NB_CHECK_EQ_STR(expected, actual)                                       \
    do {                                                                        \
        const char *nb_e_ = (expected), *nb_a_ = (actual);                      \
        if (nb_a_ == NULL || strcmp(nb_e_, nb_a_) != 0) {                       \
            fprintf(stderr, "%s:%d: expected %s == \"%s\", got \"%s\"\n",       \
                    __FILE__, __LINE__, #actual, nb_e_,                         \
                    nb_a_ ? nb_a_ : "(null)");                                  \
            nb_test_failures++;                                                 \
        }                                                                       \
    } while (0)

#define NB_RUN(test_fn)                                                         \
    do {                                                                        \
        int nb_before_ = nb_test_failures;                                      \
        test_fn();                                                              \
        printf("%s %s\n", nb_test_failures == nb_before_ ? "PASS" : "FAIL",     \
               #test_fn);                                                       \
    } while (0)

#define NB_TEST_MAIN_END() return nb_test_failures == 0 ? 0 : 1

#endif // NB_TEST_H
