/* src/vroom.c: the room list's version logic (labels from the wire, the lock, the rows' order and
 * the filter, the chips) and the CREATE ROOM version picker's choices. */
#include <string.h>

#include "test.h"
#include "vroom.h"

static void test_own_label(void) {
  /* the same label on every platform this code is built for (Linux, macOS) */
  CHECK(!strcmp(VROOM_OWN, "2.4.1"));
  CHECK(strlen(VROOM_OWN) <= VROOM_LABEL_MAX);
}

static void test_picker(void) {
  static const char *const WANT[VROOM_CHOICES][2] = { { "1.7", "NX3DX+" }, { "2.3", "" }, { "2.4", "" }, { "2.4.1", "LINUX/MACOS" } };
  for (int k = 0; k < VROOM_CHOICES; ++k) {
    const vroom_choice *c = vroom_choice_at(k);
    CHECK(c && !strcmp(c->label, WANT[k][0]) && !strcmp(c->sub, WANT[k][1]));
  }
  CHECK(vroom_choice_at(-1) == NULL && vroom_choice_at(VROOM_CHOICES) == NULL);
  /* only this app's own version can be picked, and it is the highlighted one */
  CHECK(vroom_choice_own(VROOM_OWN) == 3);
  int enabled = 0;
  for (int k = 0; k < VROOM_CHOICES; ++k) enabled += vroom_choice_enabled(k, VROOM_OWN);
  CHECK(enabled == 1 && vroom_choice_enabled(3, VROOM_OWN));
  CHECK(!vroom_choice_enabled(0, VROOM_OWN) && !vroom_choice_enabled(1, VROOM_OWN) && !vroom_choice_enabled(2, VROOM_OWN));
  /* (another app's own: its button) */
  CHECK(vroom_choice_own("1.7") == 0 && vroom_choice_own("2.4") == 2);
  CHECK(vroom_choice_own("9.9") == -1 && vroom_choice_own(NULL) == -1);
  CHECK(!vroom_choice_enabled(5, VROOM_OWN));
}

static void test_label_ok(void) {
  char out[VROOM_LABEL_MAX + 1];
  CHECK(vroom_label_ok((const unsigned char *)"2.4.1", 5, out) && !strcmp(out, "2.4.1"));
  CHECK(vroom_label_ok((const unsigned char *)"1.7", 3, out) && !strcmp(out, "1.7"));
  CHECK(!vroom_label_ok((const unsigned char *)"", 0, out) && out[0] == 0);
  CHECK(!vroom_label_ok((const unsigned char *)"2.4 1", 5, out) && out[0] == 0); /* (a space) */
  CHECK(!vroom_label_ok((const unsigned char *)"2.4\x01", 4, out));
  CHECK(!vroom_label_ok((const unsigned char *)"0123456789abcdef", 16, out)); /* (too long) */
  CHECK(vroom_label_ok((const unsigned char *)"0123456789abcde", 15, out) && strlen(out) == 15);
  CHECK(!vroom_label_ok(NULL, 3, out));
}

static void test_lock(void) {
  CHECK(!vroom_locked("2.4.1", "2.4.1"));
  CHECK(vroom_locked("2.4", "2.4.1"));
  CHECK(vroom_locked("1.7", "2.4.1"));
  CHECK(vroom_locked("2.3", "2.4.1"));
  /* unknown (an older relay, no 'V' trailer) or open to every version (an empty listed room): not locked */
  CHECK(!vroom_locked("", "2.4.1"));
  CHECK(!vroom_locked(NULL, "2.4.1"));
}

static void test_rows(void) {
  const char *labels[6] = { "1.7", "2.4.1", "", "2.4", "2.4.1", "2.3" };
  int idx[8];
  /* all versions: this version's (and the unknown) first, in the relay's order, then the locked */
  int n = vroom_rows(labels, 6, 0, "2.4.1", idx, 8);
  CHECK(n == 6);
  CHECK(idx[0] == 1 && idx[1] == 2 && idx[2] == 4 && idx[3] == 0 && idx[4] == 3 && idx[5] == 5);
  /* this version's only */
  n = vroom_rows(labels, 6, 1, "2.4.1", idx, 8);
  CHECK(n == 3 && idx[0] == 1 && idx[1] == 2 && idx[2] == 4);
  /* the screen's 4 rows: the first 4 of that order */
  n = vroom_rows(labels, 6, 0, "2.4.1", idx, 4);
  CHECK(n == 4 && idx[3] == 0);
  /* nothing, and an older relay's rooms (no labels): all shown, none locked */
  CHECK(vroom_rows(labels, 0, 0, "2.4.1", idx, 4) == 0);
  const char *none[3] = { "", "", "" };
  CHECK(vroom_rows(none, 3, 1, "2.4.1", idx, 4) == 3);
  CHECK(vroom_rows(NULL, 2, 1, "2.4.1", idx, 4) == 2);
  /* every room another version's, filtered: an empty list */
  const char *other[2] = { "1.7", "2.4" };
  CHECK(vroom_rows(other, 2, 1, "2.4.1", idx, 4) == 0);
  CHECK(vroom_rows(other, 2, 0, "2.4.1", idx, 4) == 2);
}

static void test_texts(void) {
  CHECK(!strcmp(vroom_chip("2.4.1", 1), "2.4.1 LINUX/MACOS"));
  CHECK(!strcmp(vroom_chip("2.4.1", 0), "2.4.1"));
  CHECK(!strcmp(vroom_chip("1.7", 1), "1.7 NX3DX+"));
  CHECK(!strcmp(vroom_chip("2.3", 1), "2.3"));
  CHECK(!strcmp(vroom_chip("", 1), "?") && !strcmp(vroom_chip(NULL, 0), "?"));
  CHECK(!strcmp(vroom_chip("7.0", 1), "7.0")); /* (a version this build does not know) */
  CHECK(!strcmp(vroom_filter_text(0, VROOM_OWN), "ALL VERSIONS"));
  CHECK(!strcmp(vroom_filter_text(1, VROOM_OWN), "2.4.1 ONLY"));
}

int main(void) {
  test_own_label();
  test_picker();
  test_label_ok();
  test_lock();
  test_rows();
  test_texts();
  return test_done("vroom_test");
}
