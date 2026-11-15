/* Online versus rooms by game version: the relay keeps players of different game versions apart
 * (they cannot race each other). This client says its version in HELLO ('V' u8 len label) and
 * with CREATE; the relay lists each room's version (ROOMS' 'V' '1' trailer: per room u8 len +
 * label, 0 = open to every version), lets only the same version JOIN (REFUSED otherwise) and
 * QUICK MATCH only into the same version's rooms. Here: the pure logic the room list and the
 * CREATE ROOM version picker use (no network, no drawing), so it is unit tested on its own. */
#ifndef VROOM_H
#define VROOM_H

/* this app's game version label, the same on every platform it is built for (Linux, macOS) */
#define VROOM_OWN "2.4.1"
#define VROOM_LABEL_MAX 15

/* the CREATE ROOM pop-up's versions, in its order: the label sent / shown, and a small line
 * under it ("" none) */
typedef struct {
  const char *label, *sub;
} vroom_choice;
#define VROOM_CHOICES 4
const vroom_choice *vroom_choice_at(int k); /* NULL out of range */
/* a choice can be picked only when it is this app's own version (the others are shown, disabled) */
int vroom_choice_enabled(int k, const char *own);
/* the choice that is this app's own (the highlighted one), -1 none */
int vroom_choice_own(const char *own);

/* a label from the wire: printable ASCII without spaces, 1..VROOM_LABEL_MAX; copied into out
 * (VROOM_LABEL_MAX + 1 bytes) and 1, or 0 and out "" */
int vroom_label_ok(const unsigned char *p, int n, char *out);

/* a room of version `room` (NULL or "": unknown, an older relay's, or open to all) is locked for
 * this app (version `own`): another version's */
int vroom_locked(const char *room, const char *own);

/* the room list's rows: the indexes of `labels` (n rooms) to show, this version's rooms first
 * (then the locked ones, each group in the relay's order); with only_own the locked ones left
 * out. Writes at most max indexes into idx, returns their count */
int vroom_rows(const char *const *labels, int n, int only_own, const char *own, int *idx, int max);

/* the chip's words for a room's version: "2.4.1" with its platforms when wide ("2.4.1
 * LINUX/MACOS"), "NX3DX+" after the 1.7 family's, "?" unknown */
const char *vroom_chip(const char *label, int wide);

/* the version filter's button: "ALL VERSIONS" or "<own> ONLY" */
const char *vroom_filter_text(int only_own, const char *own);

#endif
