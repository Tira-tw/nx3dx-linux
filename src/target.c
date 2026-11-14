#include "target.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static int starts(const char *s, const char *end, const char *p) {
  while (*p) {
    if (s >= end || *s != *p) return 0;
    s++, p++;
  }
  return 1;
}

/* "key = number" (the keys clTargetData::operator<< writes) */
static void field(target_file *t, const char *key, size_t klen, long v) {
  char k[64];
  if (klen >= sizeof k) return;
  memcpy(k, key, klen);
  k[klen] = 0;
  int n;
  if (klen == 6 && sscanf(k, "name%d_", &n) == 1 && n >= 0 && n < TARGET_NAME && k[5] == '_') {
    t->name[n] = (unsigned short)v;
    return;
  }
  if (!strcmp(k, "id1_")) t->id1 = (int)v;
  else if (!strcmp(k, "id2_")) t->id2 = (int)v;
  else if (!strcmp(k, "original_")) t->original = (int)v;
  if (strncmp(k, "area", 4) || k[4] < '0' || k[4] > '7' || k[5] != '_') return;
  target_area *a = &t->area[k[4] - '0'];
  const char *f = k + 6;
  static const struct { const char *name; size_t at; } F[] = {
    { "car_", offsetof(target_area, car) },
    { "custom_color_", offsetof(target_area, custom_color) },
    { "tune_power_", offsetof(target_area, power) },
    { "tune_handling_", offsetof(target_area, handling) },
    { "aero_", offsetof(target_area, aero) },
    { "aero_mirror_", offsetof(target_area, aero_mirror) },
    { "wing_", offsetof(target_area, wing) },
    { "bonnet_", offsetof(target_area, bonnet) },
    { "wheel_", offsetof(target_area, wheel) },
    { "neon_", offsetof(target_area, neon) },
    { "vinyl_", offsetof(target_area, vinyl) },
    { "dress_", offsetof(target_area, dress) },
    { "ramp_", offsetof(target_area, ramp) },
    { "route_", offsetof(target_area, route) },
    { "crown_", offsetof(target_area, crown) },
    { "class_", offsetof(target_area, cls) },
    { "title_", offsetof(target_area, title) },
    { "year_", offsetof(target_area, year) },
    { "month_", offsetof(target_area, month) },
    { "day_", offsetof(target_area, day) },
    { "hour_", offsetof(target_area, hour) },
    { "minute_", offsetof(target_area, minute) },
    { "trail_size_", offsetof(target_area, trail_size) }, /* (target_load_header's: the trail itself not read) */
    { "odo_count_", offsetof(target_area, odo) },
    { "cool_wild_", offsetof(target_area, cool_wild) },
    { "smooth_rough_", offsetof(target_area, smooth_rough) },
  };
  for (size_t i = 0; i < sizeof F / sizeof F[0]; ++i)
    if (!strcmp(f, F[i].name)) {
      *(int *)((char *)a + F[i].at) = (int)v;
      return;
    }
}

static int load(const char *path, target_file *t, size_t limit);

int target_load(const char *path, target_file *t) { return load(path, t, 0); }

int target_load_header(const char *path, target_file *t) {
  int r = load(path, t, 1 << 16); /* (the trails start after the areas' lines, some 10 KB in) */
  if (r == 0) {
    free(t->text); /* (its trails are not in: none to write) */
    t->text = NULL, t->len = 0;
    for (int a = 0; a < TARGET_AREAS; ++a) t->area[a].trail_at = 0;
  }
  return r;
}

static int load(const char *path, target_file *t, size_t limit) {
  memset(t, 0, sizeof *t);
  for (int a = 0; a < TARGET_AREAS; ++a) t->area[a].cool_wild = t->area[a].smooth_rough = 3; /* (v2.3: none written, the middle) */
  gzFile g = gzopen(path, "rb");
  if (!g) return -1;
  size_t cap = limit ? limit : 1 << 20, len = 0;
  unsigned char *buf = malloc(cap);
  for (;;) {
    if (!buf) break;
    if (len == cap && limit) break;
    if (len == cap) {
      unsigned char *b = realloc(buf, cap * 2);
      if (!b) { free(buf); buf = NULL; break; }
      buf = b, cap *= 2;
    }
    int n = gzread(g, buf + len, (unsigned)(cap - len));
    if (n <= 0) break;
    len += (size_t)n;
  }
  gzclose(g);
  if (!buf || !len) {
    free(buf);
    return -1;
  }
  t->text = buf;
  t->len = len;
  /* line by line; a trail's bytes ("-- trail N size S", "--{{", S bytes, "--}}") skipped whole */
  const char *s = (const char *)buf, *end = s + len;
  while (s < end) {
    const char *nl = memchr(s, '\n', (size_t)(end - s));
    const char *e = nl ? nl : end;
    int area;
    unsigned size;
    if (starts(s, e, "-- trail ") && sscanf(s, "-- trail %d size %u", &area, &size) == 2) {
      const char *b = nl ? nl + 1 : end;
      if (!starts(b, end, "--{{\n")) break;
      b += 5;
      if (size > (size_t)(end - b)) break;
      if (area >= 0 && area < TARGET_AREAS) {
        t->area[area].trail_at = (size_t)(b - (const char *)buf);
        t->area[area].trail_size = size;
      }
      s = b + size;
      if (s < end && *s == '\n') s++;
      continue;
    }
    int lv;
    if (starts(s, e, "-- area ") && sscanf(s, "-- area %d level %d", &area, &lv) == 2 && area >= 0 && area < TARGET_AREAS) {
      t->area[area].level = lv;
    } else if (*s != '-') {
      const char *k = s;
      while (k < e && *k != ' ' && *k != '=') k++;
      const char *v = k;
      while (v < e && (*v == ' ' || *v == '=')) v++;
      if (k > s && v > k && v < e) field(t, s, (size_t)(k - s), strtol(v, NULL, 10));
    }
    s = nl ? nl + 1 : end;
  }
  return 0;
}

void target_free(target_file *t) {
  free(t->text);
  memset(t, 0, sizeof *t);
}

int target_write_trail(const target_file *t, int area, const char *path) {
  if (area < 0 || area >= TARGET_AREAS || !t->text) return -1;
  const target_area *a = &t->area[area];
  if (!a->trail_size || a->trail_at + a->trail_size > t->len) return -1;
  FILE *f = fopen(path, "wb");
  if (!f) return -1;
  size_t w = fwrite(t->text + a->trail_at, 1, a->trail_size, f);
  return fclose(f) == 0 && w == a->trail_size ? 0 : -1;
}

static int by_name(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

int target_scan(const char *dir, char (*names)[64], int max) {
  DIR *d = opendir(dir);
  if (!d) return 0;
  int n = 0;
  struct dirent *e;
  while ((e = readdir(d)) != NULL && n < max) {
    size_t l = strlen(e->d_name);
    /* (.target too: an APK's assets lose their .gz, stored unzipped (aapt), and the full
     * build's ghosts came out as x.target: none found, Allan 11-07; gzread reads them as they are) */
    int gz = l >= 10 && !strcmp(e->d_name + l - 10, ".target.gz"), plain = l >= 7 && !strcmp(e->d_name + l - 7, ".target");
    if ((!gz && !plain) || l >= 64 || e->d_name[0] == '.') continue;
    memcpy(names[n++], e->d_name, l + 1);
  }
  closedir(d);
  qsort(names, (size_t)n, 64, by_name);
  return n;
}
