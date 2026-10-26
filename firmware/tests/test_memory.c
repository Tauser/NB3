#include <math.h>
#include <string.h>

#include "nb_memory.h"
#include "nb_store.h"
#include "nb_test.h"

// --- A card in RAM that can lose power at any write -----------------------

#define FILE_CAP (NB_MEM_SCRATCH_SIZE + 64)
#define LOG_CAP (128 * 1024)

typedef struct {
    uint8_t slot[2][FILE_CAP];
    size_t slot_len[2];
    uint8_t log[LOG_CAP];
    size_t log_len;
    int writes_left;  // -1 = no cut planned; the write that finds 0 is torn
    bool dead;        // power is gone until the next boot
    int writes;       // writes done since the last reset of the counter
} card_t;

static card_t card;
static uint8_t frame_buf[FILE_CAP];

static void card_reset(void) {
    memset(&card, 0, sizeof(card));
    card.writes_left = -1;
}

// Counts a write and decides whether power fails during it.
static bool power_cut_now(void) {
    if (card.dead) {
        return true;
    }
    card.writes++;
    if (card.writes_left == 0) {
        card.dead = true;
        return true;
    }
    if (card.writes_left > 0) {
        card.writes_left--;
    }
    return false;
}

static void slot_infos(bool valid[2], nb_slot_info_t info[2]) {
    for (int i = 0; i < 2; i++) {
        valid[i] = nb_slot_decode(card.slot[i], card.slot_len[i], &info[i], NULL);
    }
}

static bool io_read_slot(void *ctx, nb_mem_visit_t visit, void *arg, bool *found) {
    (void)ctx;
    bool valid[2];
    nb_slot_info_t info[2];
    slot_infos(valid, info);
    const nb_slot_choice_t c = nb_slot_choose(valid[0], &info[0], valid[1], &info[1]);
    *found = c.load >= 0;
    if (c.load >= 0) {
        const uint8_t *payload;
        nb_slot_decode(card.slot[c.load], card.slot_len[c.load], &info[c.load], &payload);
        visit(payload, info[c.load].length, info[c.load].max_seq, arg);
    }
    return true;
}

static bool io_write_slot(void *ctx, const void *payload, uint32_t length, uint32_t max_seq) {
    (void)ctx;
    bool valid[2];
    nb_slot_info_t info[2];
    slot_infos(valid, info);
    const nb_slot_choice_t c = nb_slot_choose(valid[0], &info[0], valid[1], &info[1]);
    const size_t n = nb_slot_encode(c.next_gen, max_seq, payload, length, frame_buf,
                                    sizeof(frame_buf));
    if (n == 0) {
        return false;
    }
    if (power_cut_now()) {
        if (card.writes_left == 0 && card.dead) {
            memcpy(card.slot[c.write], frame_buf, n / 2);  // torn halfway
            card.slot_len[c.write] = n / 2;
            card.writes_left = -1;
        }
        return false;
    }
    memcpy(card.slot[c.write], frame_buf, n);
    card.slot_len[c.write] = n;
    return true;
}

static bool io_append(void *ctx, uint32_t seq, const void *payload, uint32_t length) {
    (void)ctx;
    const size_t n = nb_frame_encode(seq, payload, length, frame_buf, sizeof(frame_buf));
    // A torn tail from an earlier cut is dropped first, as the store does.
    card.log_len = nb_log_scan(card.log, card.log_len, NULL, NULL).valid_bytes;
    if (n == 0 || card.log_len + n > LOG_CAP) {
        return false;
    }
    if (power_cut_now()) {
        if (card.writes_left == 0 && card.dead) {
            memcpy(card.log + card.log_len, frame_buf, n / 2);
            card.log_len += n / 2;
            card.writes_left = -1;
        }
        return false;
    }
    memcpy(card.log + card.log_len, frame_buf, n);
    card.log_len += n;
    return true;
}

typedef struct {
    nb_mem_visit_t visit;
    void *arg;
} log_visit_t;

static bool log_adapter(uint32_t seq, const uint8_t *payload, uint32_t length, void *ctx) {
    log_visit_t *v = ctx;
    return v->visit(payload, length, seq, v->arg);
}

static bool io_read_log(void *ctx, nb_mem_visit_t visit, void *arg) {
    (void)ctx;
    log_visit_t v = {visit, arg};
    nb_log_scan(card.log, card.log_len, log_adapter, &v);
    return true;
}

static bool io_clear_log(void *ctx) {
    (void)ctx;
    if (power_cut_now()) {
        if (card.writes_left == 0) {
            card.writes_left = -1;  // truncation either happened or not: say not
        }
        return false;
    }
    card.log_len = 0;
    return true;
}

static const nb_mem_io_t io = {NULL, io_read_slot, io_write_slot, io_append, io_read_log,
                               io_clear_log};

// Whether a text survives anywhere on the card, in any valid slot or frame.
static bool card_holds(const char *text) {
    const size_t n = strlen(text);
    for (int s = 0; s < 2; s++) {
        nb_slot_info_t info;
        const uint8_t *payload;
        if (nb_slot_decode(card.slot[s], card.slot_len[s], &info, &payload)) {
            for (size_t i = 0; i + n <= info.length; i++) {
                if (memcmp(payload + i, text, n) == 0) {
                    return true;
                }
            }
        }
    }
    const size_t valid = nb_log_scan(card.log, card.log_len, NULL, NULL).valid_bytes;
    for (size_t i = 0; i + n <= valid; i++) {
        if (memcmp(card.log + i, text, n) == 0) {
            return true;
        }
    }
    return false;
}

// --- The robot -------------------------------------------------------------

#define FLOOR 1790000000LL
#define REAL 1792929600LL  // 2026-10-26 12:00
#define HOUR 3600LL

static nb_memory_t mem;
static char scratch[NB_MEM_SCRATCH_SIZE];
static nb_json_tok_t toks[NB_MEM_TOKENS];
static nb_clock_t clk;
static uint32_t boot_id = 0;

// A boot whose clock syncs to `wall` at 2 s; returns the load status.
static nb_mem_status_t boot_at(int64_t wall) {
    card.dead = false;
    card.writes_left = -1;
    boot_id++;
    nb_clock_init(&clk, FLOOR, 0, boot_id);
    nb_clock_sample(&clk, 10, 1000);
    nb_clock_sample(&clk, wall, 2000);
    nb_memory_init(&mem, scratch, sizeof(scratch), toks, NB_MEM_TOKENS);
    return nb_memory_load(&mem, &io);
}

static nb_stamp_t now_stamp(void) { return nb_clock_stamp(&clk, 3000); }

static int recall(const char *words, int kind, nb_mem_hit_t *hits, int max) {
    nb_mem_query_t q = {words, kind, 0};
    return nb_memory_recall(&mem, &q, &clk, 3000, hits, max);
}

static bool finds(const char *words, const char *text) {
    nb_mem_hit_t hits[16];
    const int n = recall(words, -1, hits, 16);
    for (int i = 0; i < n; i++) {
        if (strcmp(hits[i].record->text, text) == 0) {
            return true;
        }
    }
    return false;
}

static uint32_t remember_fact(const char *text) {
    uint32_t id = 0;
    NB_CHECK_EQ_INT(NB_MEM_OK, nb_memory_remember(&mem, &io, NB_MEM_FACT, NULL, text,
                                                  now_stamp(), &id));
    return id;
}

static uint32_t add_episode(nb_ep_type_t type, float valence, uint32_t about, const char *text) {
    nb_mem_episode_t ep = {type, valence, 0.0f, 0.0f, about, text};
    uint32_t id = 0;
    NB_CHECK_EQ_INT(NB_MEM_OK,
                    nb_memory_episode(&mem, &io, &ep, now_stamp(), &clk, 3000, &id));
    return id;
}

// --- Tests -----------------------------------------------------------------

static void test_remember_and_recall_ignore_case_and_accents(void) {
    card_reset();
    NB_CHECK_EQ_INT(NB_MEM_OK, boot_at(REAL));
    remember_fact("Gosta de café sem açúcar");
    uint32_t name_id = 0;
    NB_CHECK_EQ_INT(NB_MEM_OK, nb_memory_remember(&mem, &io, NB_MEM_PROFILE, "nome", "Tauser",
                                                  now_stamp(), &name_id));
    NB_CHECK(finds("cafe", "Gosta de café sem açúcar"));
    NB_CHECK(finds("ACUCAR", "Gosta de café sem açúcar"));
    NB_CHECK(finds("qual o nome dele", "Tauser"));  // the key counts as text
    NB_CHECK(!finds("cha", "Gosta de café sem açúcar"));  // too short to search
    nb_mem_hit_t hits[4];
    NB_CHECK_EQ_INT(0, recall("chocolate", -1, hits, 4));
    NB_CHECK_EQ_INT(1, recall("", NB_MEM_PROFILE, hits, 4));

    // Same key replaces the profile entry; same text refreshes the fact.
    uint32_t again = 0;
    NB_CHECK_EQ_INT(NB_MEM_UPDATED, nb_memory_remember(&mem, &io, NB_MEM_PROFILE, "Nome",
                                                       "Tau", now_stamp(), &again));
    NB_CHECK_EQ_INT(name_id, again);
    NB_CHECK_EQ_INT(NB_MEM_UPDATED, nb_memory_remember(&mem, &io, NB_MEM_FACT, NULL,
                                                       "gosta de CAFE sem acucar",
                                                       now_stamp(), &again));
    NB_CHECK_EQ_INT(2, mem.long_count);
    NB_CHECK_EQ_INT(NB_MEM_INVALID, nb_memory_remember(&mem, &io, NB_MEM_PROFILE, "",
                                                       "sem chave", now_stamp(), NULL));
    NB_CHECK_EQ_INT(NB_MEM_INVALID, nb_memory_remember(&mem, &io, NB_MEM_FACT, NULL, "  \n ",
                                                       now_stamp(), NULL));
}

static void test_memory_survives_a_reboot(void) {
    card_reset();
    boot_at(REAL);
    const uint32_t fact = remember_fact("O dono trabalha com robôs");
    const uint32_t ep = add_episode(NB_EP_AFFECTION, 0.8f, 0, "carinho na cabeça");
    NB_CHECK_EQ_INT(NB_MEM_OK, boot_at(REAL + HOUR));
    NB_CHECK(nb_memory_get(&mem, fact) != NULL);
    const nb_mem_record_t *e = nb_memory_get(&mem, ep);
    NB_CHECK(e != NULL && e->kind == NB_MEM_EPISODE && e->type == NB_EP_AFFECTION);
    NB_CHECK(e != NULL && fabsf(e->valence - 0.8f) < 1e-3f && e->when.has_wall);
    NB_CHECK_EQ_INT(0, mem.rejected);
    // New ids continue after the loaded ones.
    NB_CHECK(remember_fact("outro fato") > ep);
}

static void test_importance_halves_at_the_half_life(void) {
    card_reset();
    boot_at(REAL);
    add_episode(NB_EP_AFFECTION, 1.0f, 0, NULL);  // half-life 6 h, starts at 0.5
    boot_at(REAL + 6 * HOUR);
    bool known;
    const float imp = nb_memory_importance(&mem.episodes[0], &clk, 2000, &known);
    NB_CHECK(known);
    NB_CHECK(fabsf(imp - 0.25f) < 0.01f);
}

static void test_bias_is_bounded_and_pure(void) {
    card_reset();
    boot_at(REAL);
    float v, a;
    nb_memory_bias(&mem, &clk, 3000, &v, &a);
    NB_CHECK(v == 0.0f && a == 0.0f);
    for (int i = 0; i < 10; i++) {
        add_episode(NB_EP_AFFECTION, 1.0f, 0, "muito carinho");
    }
    nb_memory_bias(&mem, &clk, 3000, &v, &a);
    NB_CHECK(fabsf(v - NB_MEM_BIAS_MAX) < 1e-6f);  // 10 x 0.5 x 0.05 = 0.25, capped
    nb_mem_hit_t hits[8];
    for (int i = 0; i < 1000; i++) {
        recall("carinho", -1, hits, 8);  // recalling does not feel anything
    }
    float v2, a2;
    nb_memory_bias(&mem, &clk, 3000, &v2, &a2);
    NB_CHECK(v2 == v && a2 == a);

    // Episodes whose age is unknown never move the mood.
    card_reset();
    boot_id++;
    nb_clock_init(&clk, FLOOR, 0, boot_id);
    nb_clock_sample(&clk, 10, 1000);  // no network yet
    nb_memory_init(&mem, scratch, sizeof(scratch), toks, NB_MEM_TOKENS);
    nb_memory_load(&mem, &io);
    add_episode(NB_EP_HURT, -1.0f, 0, "pancada");
    nb_memory_bias(&mem, &clk, 3000, &v, &a);
    NB_CHECK(v == 0.0f);
}

static void test_rebase_dates_episodes_made_before_the_sync(void) {
    card_reset();
    boot_id++;
    nb_clock_init(&clk, FLOOR, 0, boot_id);
    nb_clock_sample(&clk, 10, 1000);
    nb_memory_init(&mem, scratch, sizeof(scratch), toks, NB_MEM_TOKENS);
    nb_memory_load(&mem, &io);
    const uint32_t id = add_episode(NB_EP_CONVERSATION, 0.3f, 0, "papo antes da rede");
    NB_CHECK(!nb_memory_get(&mem, id)->when.has_wall);
    nb_clock_sample(&clk, 11, 2000);
    nb_clock_sample(&clk, REAL, 10000);
    NB_CHECK(nb_memory_rebase(&mem, &clk));
    NB_CHECK(nb_memory_get(&mem, id)->when.has_wall);
    NB_CHECK(mem.dirty);
    NB_CHECK_EQ_INT(NB_MEM_OK, nb_memory_flush(&mem, &io));
    boot_at(REAL + HOUR);
    NB_CHECK(nb_memory_get(&mem, id)->when.has_wall);
}

static void test_compaction_drops_faded_episodes_and_empties_the_log(void) {
    card_reset();
    boot_at(REAL);
    const uint32_t fact = remember_fact("mora em Recife");
    const uint32_t old = add_episode(NB_EP_AFFECTION, 0.5f, 0, "carinho antigo");
    boot_at(REAL + 3 * 24 * HOUR);  // 12 half-lives later
    const uint32_t fresh = add_episode(NB_EP_CONVERSATION, 0.4f, fact, "falamos de Recife");
    NB_CHECK(card.log_len > 0);
    NB_CHECK_EQ_INT(NB_MEM_OK, nb_memory_compact(&mem, &io, &clk, 3000));
    NB_CHECK_EQ_INT(0, card.log_len);
    NB_CHECK(nb_memory_get(&mem, old) == NULL);
    boot_at(REAL + 3 * 24 * HOUR + HOUR);
    NB_CHECK(nb_memory_get(&mem, old) == NULL);
    NB_CHECK(nb_memory_get(&mem, fresh) != NULL);
    NB_CHECK(nb_memory_get(&mem, fact) != NULL);
    NB_CHECK(!card_holds("carinho antigo"));
}

static void test_episodes_full_drop_the_least_important(void) {
    card_reset();
    boot_at(REAL);
    nb_mem_episode_t big = {NB_EP_NOTE, 0.0f, 0.0f, 1.0f, 0, "aniversario"};
    uint32_t keep = 0;
    nb_memory_episode(&mem, &io, &big, now_stamp(), &clk, 3000, &keep);
    const uint32_t first_small = add_episode(NB_EP_CONVERSATION, 0.1f, 0, "papo");
    for (int i = 1; i < NB_MEM_EPISODE_MAX; i++) {
        add_episode(NB_EP_CONVERSATION, 0.1f, 0, "papo");
    }
    NB_CHECK_EQ_INT(NB_MEM_EPISODE_MAX, mem.episode_count);
    NB_CHECK(nb_memory_get(&mem, keep) != NULL);
    NB_CHECK(nb_memory_get(&mem, first_small) == NULL);  // oldest of the least important
}

// docs/VIDA.md §8, against the core with a simulated card.
static void test_acceptance_script(void) {
    card_reset();
    boot_at(REAL);
    const uint32_t fact = remember_fact("A cor favorita do Tauser é verde");
    add_episode(NB_EP_AFFECTION, 0.9f, fact, "elogiou o verde dos meus olhos");

    // 1. remember -> reboot -> recall
    boot_at(REAL + HOUR);
    NB_CHECK(finds("cor favorita", "A cor favorita do Tauser é verde"));

    // 2. correct -> reboot -> only the new version
    NB_CHECK_EQ_INT(NB_MEM_OK, nb_memory_correct(&mem, &io, fact,
                                                 "A cor favorita do Tauser é azul"));
    boot_at(REAL + 2 * HOUR);
    NB_CHECK(finds("cor favorita", "A cor favorita do Tauser é azul"));
    NB_CHECK(!finds("verde", "A cor favorita do Tauser é verde"));
    NB_CHECK(!card_holds("Tauser é verde"));

    // 3. forget -> reboot -> gone from recall, episodes and mood bias
    float v_before, a;
    nb_memory_bias(&mem, &clk, 3000, &v_before, &a);
    NB_CHECK(v_before > 0.0f);
    nb_mem_forgot_t gone;
    NB_CHECK_EQ_INT(NB_MEM_OK, nb_memory_forget_id(&mem, &io, fact, &gone));
    NB_CHECK_EQ_INT(1, gone.count[NB_MEM_FACT]);
    NB_CHECK_EQ_INT(1, gone.count[NB_MEM_EPISODE]);
    NB_CHECK_EQ_INT(0, mem.pending_count);  // the mark is gone once it succeeds
    NB_CHECK(!mem.dirty);
    boot_at(REAL + 3 * HOUR);
    NB_CHECK(!finds("cor favorita", "A cor favorita do Tauser é azul"));
    NB_CHECK_EQ_INT(0, mem.episode_count);
    float v;
    nb_memory_bias(&mem, &clk, 3000, &v, &a);
    NB_CHECK(v == 0.0f);
    NB_CHECK(!card_holds("cor favorita"));
    NB_CHECK(!card_holds("verde dos meus olhos"));
    NB_CHECK_EQ_INT(0, mem.pending_count);
    // Ids are never handed out again.
    NB_CHECK(remember_fact("novo fato") > fact);
}

// Builds a memory with a fact, an episode about it in the snapshot and
// another still in the log.
static uint32_t setup_for_forget(void) {
    card_reset();
    boot_at(REAL);
    remember_fact("fica");
    const uint32_t fact = remember_fact("segredo do Noise");
    add_episode(NB_EP_NOTE, 0.2f, fact, "sobre o segredo");
    nb_memory_compact(&mem, &io, &clk, 3000);
    add_episode(NB_EP_NOTE, 0.2f, fact, "de novo o segredo");
    return fact;
}

static void test_forget_interrupted_at_every_write(void) {
    // Writes of the forget: snapshot with the mark, empty log, clean snapshot.
    for (int cut = 0; cut < 3; cut++) {
        const uint32_t fact = setup_for_forget();
        card.writes = 0;
        card.writes_left = cut;
        nb_mem_forgot_t gone;
        NB_CHECK_EQ_INT(NB_MEM_IO, nb_memory_forget_id(&mem, &io, fact, &gone));

        NB_CHECK_EQ_INT(NB_MEM_OK, boot_at(REAL + HOUR));
        NB_CHECK_EQ_INT(0, mem.pending_count);
        NB_CHECK(finds("fica", "fica"));
        if (cut == 0) {
            // Cut while writing the marked snapshot: success was never
            // reported, so the old data may stay; forgetting again works.
            NB_CHECK(finds("segredo", "segredo do Noise"));
            NB_CHECK_EQ_INT(NB_MEM_OK, nb_memory_forget_id(&mem, &io, fact, &gone));
            boot_at(REAL + 2 * HOUR);
        }
        NB_CHECK(nb_memory_get(&mem, fact) == NULL);
        NB_CHECK(!finds("segredo", "segredo do Noise"));
        NB_CHECK_EQ_INT(0, mem.episode_count);
        NB_CHECK(!card_holds("segredo"));
    }
}

static void test_boot_interrupted_while_finishing_a_forget(void) {
    for (int cut = 0; cut < 3; cut++) {
        const uint32_t fact = setup_for_forget();
        card.writes_left = 1;  // the mark is on the card, nothing more
        nb_memory_forget_id(&mem, &io, fact, NULL);
        card.dead = false;
        // A boot that loses power again while finishing the job.
        boot_id++;
        nb_clock_init(&clk, FLOOR, 0, boot_id);
        nb_memory_init(&mem, scratch, sizeof(scratch), toks, NB_MEM_TOKENS);
        card.writes_left = cut;
        nb_memory_load(&mem, &io);
        NB_CHECK(nb_memory_get(&mem, fact) == NULL);  // filtered even before finishing
        NB_CHECK_EQ_INT(NB_MEM_OK, boot_at(REAL + HOUR));
        NB_CHECK_EQ_INT(0, mem.pending_count);
        NB_CHECK(!card_holds("segredo"));
        NB_CHECK(finds("fica", "fica"));
    }
}

static void test_forget_by_kind_and_everything(void) {
    card_reset();
    boot_at(REAL);
    remember_fact("fato um");
    nb_memory_remember(&mem, &io, NB_MEM_PREFERENCE, "musica", "gosta de jazz", now_stamp(),
                       NULL);
    add_episode(NB_EP_CONVERSATION, 0.2f, 0, "papo solto");
    nb_mem_forgot_t gone;
    NB_CHECK_EQ_INT(NB_MEM_OK, nb_memory_forget_kind(&mem, &io, NB_MEM_EPISODE, &gone));
    NB_CHECK_EQ_INT(1, gone.count[NB_MEM_EPISODE]);
    NB_CHECK_EQ_INT(2, mem.long_count);
    NB_CHECK_EQ_INT(NB_MEM_NOT_FOUND, nb_memory_forget_kind(&mem, &io, NB_MEM_EPISODE, &gone));
    NB_CHECK_EQ_INT(NB_MEM_OK, nb_memory_forget_all(&mem, &io, &gone));
    NB_CHECK_EQ_INT(1, gone.count[NB_MEM_FACT]);
    NB_CHECK_EQ_INT(1, gone.count[NB_MEM_PREFERENCE]);
    boot_at(REAL + HOUR);
    NB_CHECK_EQ_INT(0, mem.long_count + mem.episode_count);
    NB_CHECK(!card_holds("jazz") && !card_holds("papo solto"));
    NB_CHECK_EQ_INT(NB_MEM_NOT_FOUND, nb_memory_forget_id(&mem, &io, 9999, &gone));
}

static void test_torn_log_tail_loses_only_that_episode(void) {
    card_reset();
    boot_at(REAL);
    const uint32_t a = add_episode(NB_EP_CONVERSATION, 0.2f, 0, "primeiro");
    card.writes_left = 0;
    nb_mem_episode_t ep = {NB_EP_CONVERSATION, 0.2f, 0.0f, 0.0f, 0, "rasgado"};
    NB_CHECK_EQ_INT(NB_MEM_IO, nb_memory_episode(&mem, &io, &ep, now_stamp(), &clk, 3000, NULL));
    boot_at(REAL + HOUR);
    NB_CHECK(nb_memory_get(&mem, a) != NULL);
    NB_CHECK_EQ_INT(1, mem.episode_count);
    const uint32_t c = add_episode(NB_EP_CONVERSATION, 0.2f, 0, "depois");
    boot_at(REAL + 2 * HOUR);
    NB_CHECK(nb_memory_get(&mem, c) != NULL);
    NB_CHECK_EQ_INT(2, mem.episode_count);
}

static void test_unreadable_snapshot_is_never_overwritten(void) {
    static const char *const payloads[] = {
        "{}",                                     // not our layout
        "{\"schema\":2,\"next_id\":5,\"records\":[]}",  // a newer firmware's
        "nao e json",
    };
    for (size_t k = 0; k < sizeof(payloads) / sizeof(payloads[0]); k++) {
        card_reset();
        card.slot_len[0] = nb_slot_encode(1, 0, payloads[k], (uint32_t)strlen(payloads[k]),
                                          card.slot[0], FILE_CAP);
        NB_CHECK_EQ_INT(NB_MEM_UNREADABLE, boot_at(REAL));
        NB_CHECK_EQ_INT(NB_MEM_IO, nb_memory_remember(&mem, &io, NB_MEM_FACT, NULL, "x",
                                                      now_stamp(), NULL));
        NB_CHECK_EQ_INT(0, card.writes);
        NB_CHECK_EQ_INT(0, card.slot_len[1]);
    }
}

static void test_malformed_records_are_skipped(void) {
    card_reset();
    const char *p =
        "{\"schema\":1,\"next_id\":9,\"records\":["
        "{\"id\":1,\"kind\":\"fact\",\"text\":\"bom\",\"boot\":1,\"mono_ms\":5},"
        "{\"id\":2,\"kind\":\"alien\",\"text\":\"x\",\"boot\":1,\"mono_ms\":5},"
        "{\"id\":3,\"kind\":\"profile\",\"text\":\"sem chave\",\"boot\":1,\"mono_ms\":5},"
        "{\"id\":4,\"kind\":\"episode\",\"text\":\"\",\"boot\":1,\"mono_ms\":5,"
        "\"type\":\"hurt\",\"valence\":-5,\"arousal\":0.2,\"importance\":0.7}]}";
    card.slot_len[0] = nb_slot_encode(1, 0, p, (uint32_t)strlen(p), card.slot[0], FILE_CAP);
    NB_CHECK_EQ_INT(NB_MEM_OK, boot_at(REAL));
    NB_CHECK_EQ_INT(2, mem.rejected);
    NB_CHECK(nb_memory_get(&mem, 1) != NULL);
    const nb_mem_record_t *e = nb_memory_get(&mem, 4);
    NB_CHECK(e != NULL && e->valence == -1.0f);  // clamped
    NB_CHECK(!e->when.has_wall);                 // unknown date stays unknown
    NB_CHECK_EQ_INT(9, remember_fact("novo"));   // next_id from the snapshot
}

int main(void) {
    NB_RUN(test_remember_and_recall_ignore_case_and_accents);
    NB_RUN(test_memory_survives_a_reboot);
    NB_RUN(test_importance_halves_at_the_half_life);
    NB_RUN(test_bias_is_bounded_and_pure);
    NB_RUN(test_rebase_dates_episodes_made_before_the_sync);
    NB_RUN(test_compaction_drops_faded_episodes_and_empties_the_log);
    NB_RUN(test_episodes_full_drop_the_least_important);
    NB_RUN(test_acceptance_script);
    NB_RUN(test_forget_interrupted_at_every_write);
    NB_RUN(test_boot_interrupted_while_finishing_a_forget);
    NB_RUN(test_forget_by_kind_and_everything);
    NB_RUN(test_torn_log_tail_loses_only_that_episode);
    NB_RUN(test_unreadable_snapshot_is_never_overwritten);
    NB_RUN(test_malformed_records_are_skipped);
    NB_TEST_MAIN_END();
}
