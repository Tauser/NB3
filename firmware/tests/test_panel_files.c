#include "nb_panel_files.h"
#include "nb_test.h"

static void test_plain_paths_are_valid(void) {
    NB_CHECK(nb_panel_path_valid("personalidade.ini"));
    NB_CHECK(nb_panel_path_valid("sons/lembrete.ogg"));
    NB_CHECK(nb_panel_path_valid("memoria/slot_a.bin"));
    NB_CHECK(nb_panel_path_valid("a-b_c.1"));
}

static void test_escapes_and_odd_paths_are_refused(void) {
    NB_CHECK(!nb_panel_path_valid(NULL));
    NB_CHECK(!nb_panel_path_valid(""));
    NB_CHECK(!nb_panel_path_valid("/sons/a.ogg"));
    NB_CHECK(!nb_panel_path_valid("../a.ogg"));
    NB_CHECK(!nb_panel_path_valid("sons/../a.ogg"));
    NB_CHECK(!nb_panel_path_valid("sons/./a.ogg"));
    NB_CHECK(!nb_panel_path_valid("sons//a.ogg"));
    NB_CHECK(!nb_panel_path_valid("sons/"));
    NB_CHECK(!nb_panel_path_valid("."));
    NB_CHECK(!nb_panel_path_valid(".."));
    NB_CHECK(!nb_panel_path_valid("a/b/c.ogg"));       // two segments at most
    NB_CHECK(!nb_panel_path_valid("sons\\a.ogg"));
    NB_CHECK(!nb_panel_path_valid("Sons/a.ogg"));      // lowercase only
    NB_CHECK(!nb_panel_path_valid("sons/a b.ogg"));
    NB_CHECK(!nb_panel_path_valid("sons/a%2e.ogg"));
    NB_CHECK(!nb_panel_path_valid("sons/a\n.ogg"));
}

static void test_a_path_over_the_limit_is_refused(void) {
    char path[NB_PANEL_PATH_MAX + 8];
    for (unsigned i = 0; i < sizeof(path) - 1; i++) {
        path[i] = 'a';
    }
    path[sizeof(path) - 1] = '\0';
    NB_CHECK(!nb_panel_path_valid(path));
    path[NB_PANEL_PATH_MAX - 1] = '\0';
    NB_CHECK(nb_panel_path_valid(path));
}

static void test_everything_valid_can_be_read(void) {
    NB_CHECK(nb_panel_allowed("sons/lembrete.ogg", NB_PANEL_READ));
    NB_CHECK(nb_panel_allowed("memoria/slot_a.bin", NB_PANEL_READ));
    NB_CHECK(!nb_panel_allowed("../x", NB_PANEL_READ));
}

static void test_only_sounds_can_be_written(void) {
    NB_CHECK(nb_panel_allowed("sons/novo.ogg", NB_PANEL_WRITE));
    NB_CHECK(!nb_panel_allowed("personalidade.ini", NB_PANEL_WRITE));  // read only on purpose
    NB_CHECK(!nb_panel_allowed("sons/novo.mp3", NB_PANEL_WRITE));
    NB_CHECK(!nb_panel_allowed("sons/.ogg", NB_PANEL_WRITE));
    NB_CHECK(!nb_panel_allowed("memoria/slot_a.bin", NB_PANEL_WRITE));
    NB_CHECK(!nb_panel_allowed("outros/novo.ogg", NB_PANEL_WRITE));
}

static void test_only_sounds_can_be_deleted(void) {
    NB_CHECK(nb_panel_allowed("sons/novo.ogg", NB_PANEL_DELETE));
    NB_CHECK(!nb_panel_allowed("personalidade.ini", NB_PANEL_DELETE));
    NB_CHECK(!nb_panel_allowed("memoria/slot_a.bin", NB_PANEL_DELETE));
}

static void test_write_limits_follow_the_kind_of_file(void) {
    NB_CHECK_EQ_INT(NB_PANEL_SOUND_MAX_BYTES, nb_panel_max_write_bytes("sons/a.ogg"));
    NB_CHECK_EQ_INT(0, nb_panel_max_write_bytes("personalidade.ini"));
    NB_CHECK_EQ_INT(0, nb_panel_max_write_bytes("memoria/slot_a.bin"));
    NB_CHECK_EQ_INT(0, nb_panel_max_write_bytes(NULL));
}

int main(void) {
    NB_RUN(test_plain_paths_are_valid);
    NB_RUN(test_escapes_and_odd_paths_are_refused);
    NB_RUN(test_a_path_over_the_limit_is_refused);
    NB_RUN(test_everything_valid_can_be_read);
    NB_RUN(test_only_sounds_can_be_written);
    NB_RUN(test_only_sounds_can_be_deleted);
    NB_RUN(test_write_limits_follow_the_kind_of_file);
    NB_TEST_MAIN_END();
}
