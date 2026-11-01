#include <string.h>

#include "nb_panic_record.h"
#include "nb_test.h"

static void fill(nb_panic_record_t *r) {
    memset(r, 0, sizeof(*r));
    r->core = 1;
    r->exception = 4;
    r->cause = 28;
    r->pc = 0x42001234;
    r->excvaddr = 0x10;
    r->depth = 2;
    r->bt_pc[0] = 0x42001234;
    r->bt_sp[0] = 0x3fc90000;
    r->bt_pc[1] = 0x42005678;
    r->bt_sp[1] = 0x3fc90040;
}

static void test_unsealed_memory_is_not_valid(void) {
    nb_panic_record_t r;
    memset(&r, 0xA5, sizeof(r));
    NB_CHECK(!nb_panic_record_valid(&r));
    memset(&r, 0, sizeof(r));
    NB_CHECK(!nb_panic_record_valid(&r));
}

static void test_sealed_record_is_valid_until_altered(void) {
    nb_panic_record_t r;
    fill(&r);
    nb_panic_record_seal(&r);
    NB_CHECK(nb_panic_record_valid(&r));
    r.pc ^= 1;
    NB_CHECK(!nb_panic_record_valid(&r));
}

static void test_clear_consumes_the_record(void) {
    nb_panic_record_t r;
    fill(&r);
    nb_panic_record_seal(&r);
    nb_panic_record_clear(&r);
    NB_CHECK(!nb_panic_record_valid(&r));
}

static void test_seal_caps_depth(void) {
    nb_panic_record_t r;
    fill(&r);
    r.depth = 99;
    nb_panic_record_seal(&r);
    NB_CHECK_EQ_INT(NB_PANIC_BACKTRACE_DEPTH, r.depth);
    NB_CHECK(nb_panic_record_valid(&r));
}

static void test_format_lists_addr2line_pairs(void) {
    nb_panic_record_t r;
    fill(&r);
    nb_panic_record_seal(&r);
    char out[256];
    size_t n = nb_panic_record_format(&r, out, sizeof(out));
    NB_CHECK(n == strlen(out));
    NB_CHECK(strstr(out, "pc=0x42001234") != NULL);
    NB_CHECK(strstr(out, "0x42005678:0x3fc90040") != NULL);
}

static void test_format_truncates_without_overflow(void) {
    nb_panic_record_t r;
    fill(&r);
    nb_panic_record_seal(&r);
    char out[40];
    size_t n = nb_panic_record_format(&r, out, sizeof(out));
    NB_CHECK(n < sizeof(out));
    NB_CHECK(out[n] == '\0');
    NB_CHECK(nb_panic_record_format(&r, out, 0) == 0);
}

int main(void) {
    NB_RUN(test_unsealed_memory_is_not_valid);
    NB_RUN(test_sealed_record_is_valid_until_altered);
    NB_RUN(test_clear_consumes_the_record);
    NB_RUN(test_seal_caps_depth);
    NB_RUN(test_format_lists_addr2line_pairs);
    NB_RUN(test_format_truncates_without_overflow);
    NB_TEST_MAIN_END();
}
