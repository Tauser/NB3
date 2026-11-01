#include "nb_pcm_ring.h"
#include "nb_test.h"

#define CAP 8

static int16_t storage[CAP];

static void test_pops_oldest_first(void) {
    nb_pcm_ring_t r;
    nb_pcm_ring_init(&r, storage, CAP);
    const int16_t in[] = {1, 2, 3};
    nb_pcm_ring_push(&r, in, 3);
    NB_CHECK(nb_pcm_ring_count(&r) == 3);
    int16_t out[4];
    NB_CHECK(nb_pcm_ring_pop(&r, out, 2) == 2);
    NB_CHECK(out[0] == 1 && out[1] == 2);
    NB_CHECK(nb_pcm_ring_pop(&r, out, 4) == 1);
    NB_CHECK(out[0] == 3);
    NB_CHECK(nb_pcm_ring_count(&r) == 0);
}

static void test_overwrites_oldest_when_full(void) {
    nb_pcm_ring_t r;
    nb_pcm_ring_init(&r, storage, CAP);
    int16_t in[12];
    for (int i = 0; i < 12; i++) in[i] = (int16_t)(i + 1);
    nb_pcm_ring_push(&r, in, 6);
    nb_pcm_ring_push(&r, in + 6, 6);
    NB_CHECK(nb_pcm_ring_count(&r) == CAP);
    int16_t out[CAP];
    NB_CHECK(nb_pcm_ring_pop(&r, out, CAP) == CAP);
    for (int i = 0; i < CAP; i++) NB_CHECK(out[i] == i + 5);
}

static void test_oversized_push_keeps_newest(void) {
    nb_pcm_ring_t r;
    nb_pcm_ring_init(&r, storage, CAP);
    int16_t in[20];
    for (int i = 0; i < 20; i++) in[i] = (int16_t)i;
    nb_pcm_ring_push(&r, in, 20);
    int16_t out[CAP];
    NB_CHECK(nb_pcm_ring_pop(&r, out, CAP) == CAP);
    NB_CHECK(out[0] == 12 && out[CAP - 1] == 19);
}

static void test_wraps_after_partial_pop(void) {
    nb_pcm_ring_t r;
    nb_pcm_ring_init(&r, storage, CAP);
    int16_t in[6] = {1, 2, 3, 4, 5, 6};
    int16_t out[CAP];
    nb_pcm_ring_push(&r, in, 6);
    nb_pcm_ring_pop(&r, out, 4);
    nb_pcm_ring_push(&r, in, 6);
    NB_CHECK(nb_pcm_ring_count(&r) == 8);
    NB_CHECK(nb_pcm_ring_pop(&r, out, CAP) == CAP);
    NB_CHECK(out[0] == 5 && out[1] == 6 && out[2] == 1 && out[7] == 6);
}

static void test_clear_and_missing_storage(void) {
    nb_pcm_ring_t r;
    nb_pcm_ring_init(&r, storage, CAP);
    int16_t in[2] = {7, 8};
    nb_pcm_ring_push(&r, in, 2);
    nb_pcm_ring_clear(&r);
    NB_CHECK(nb_pcm_ring_count(&r) == 0);

    nb_pcm_ring_init(&r, NULL, CAP);
    nb_pcm_ring_push(&r, in, 2);
    NB_CHECK(nb_pcm_ring_count(&r) == 0);
}

int main(void) {
    NB_RUN(test_pops_oldest_first);
    NB_RUN(test_overwrites_oldest_when_full);
    NB_RUN(test_oversized_push_keeps_newest);
    NB_RUN(test_wraps_after_partial_pop);
    NB_RUN(test_clear_and_missing_storage);
    NB_TEST_MAIN_END();
}
