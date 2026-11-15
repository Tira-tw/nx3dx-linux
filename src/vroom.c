/* Online versus rooms by game version: see vroom.h. */
#include "vroom.h"

#include <stdio.h>
#include <string.h>

static const vroom_choice CHOICES[VROOM_CHOICES] = {
  { "1.7", "NX3DX+" },
  { "2.3", "" },
  { "2.4", "" },
  { "2.4.1", "LINUX/MACOS" },
};

const vroom_choice *vroom_choice_at(int k) { return k >= 0 && k < VROOM_CHOICES ? &CHOICES[k] : NULL; }

int vroom_choice_enabled(int k, const char *own) {
  const vroom_choice *c = vroom_choice_at(k);
  return c && own && !strcmp(c->label, own);
}

int vroom_choice_own(const char *own) {
  for (int k = 0; k < VROOM_CHOICES; ++k)
    if (vroom_choice_enabled(k, own)) return k;
  return -1;
}

int vroom_label_ok(const unsigned char *p, int n, char *out) {
  out[0] = 0;
  if (!p || n < 1 || n > VROOM_LABEL_MAX) return 0;
  for (int i = 0; i < n; ++i)
    if (p[i] < 33 || p[i] > 126) return 0;
  memcpy(out, p, (size_t)n);
  out[n] = 0;
  return 1;
}

int vroom_locked(const char *room, const char *own) { return room && room[0] && own && strcmp(room, own) != 0; }

int vroom_rows(const char *const *labels, int n, int only_own, const char *own, int *idx, int max) {
  int k = 0;
  for (int pass = 0; pass < 2; ++pass) {
    if (pass == 1 && only_own) break;
    for (int i = 0; i < n && k < max; ++i)
      if (vroom_locked(labels ? labels[i] : NULL, own) == pass) idx[k++] = i;
  }
  return k;
}

const char *vroom_chip(const char *label, int wide) {
  static char s[40];
  if (!label || !label[0]) return "?";
  const char *sub = "";
  for (int k = 0; k < VROOM_CHOICES; ++k)
    if (!strcmp(CHOICES[k].label, label)) sub = CHOICES[k].sub;
  if (wide && sub[0]) snprintf(s, sizeof s, "%s %s", label, sub);
  else snprintf(s, sizeof s, "%s", label);
  return s;
}

const char *vroom_filter_text(int only_own, const char *own) {
  static char s[32];
  if (!only_own) return "ALL VERSIONS";
  snprintf(s, sizeof s, "%s ONLY", own ? own : "?");
  return s;
}
