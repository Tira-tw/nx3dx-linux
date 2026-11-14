/* The players' own ghosts and their exchange: see ghostx.h. */
#include "ghostx.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <zlib.h>

#include "net.h"
#include "target.h"

#define NAME_LEN 48      /* a file's name in the datagrams */
#define CHUNK 1024       /* a DATA's bytes */
#define WINDOW 32        /* the chunks a REQ asks for */
#define MAX_FILE (4 << 20)
#define LIST_EVERY 1.0   /* s: a swap's lists (and its owner's START) */
#define OWN_EVERY 5.0    /* s: the own ghosts' directory read again */
#define REQ_EVERY 0.12   /* s: a download's next request */
#define GIVE_UP 6.0      /* s without a chunk: the download dropped */
#define SWAP_MIN 4.0     /* s: a swap lasts this at least (the others' lists come in) */
#define SWAP_QUIET 2.5   /* s with nothing asked or fetched: the swap over */
#define SWAP_MAX 90.0    /* s: a swap given up */
#define SWAP_LOST 8.0    /* s without its owner's word: another's swap taken as over */
#define OWN_MAX 64
#define WANT_MAX 16
#define SAVES_MAX 8
/* LIST, REQ, DATA: the files; SWAP_START, SWAP_END: one player's swap (the button: one at a
 * time, the lowest id first when two start at once, the other queued till it ends) */
enum { LIST = 1, REQ, DATA, SWAP_START, SWAP_END };

static struct {
  char save[1024], own_dir[1100], got_dir[1100];
  /* the own ghosts (their file names) and when last read */
  char own[OWN_MAX][NAME_LEN];
  int own_n;
  double own_at, list_at, poll_at;
  /* the ghosts wanted (another's, newer than what is here), the one being fetched */
  struct { uint32_t from; char name[NAME_LEN]; } want[WANT_MAX];
  int want_n;
  struct {
    int on;
    uint32_t from;
    char name[NAME_LEN];
    unsigned char *buf, *have;
    uint32_t total;
    int chunks, got;
    double last_data, last_req;
  } dl;
  /* the own file last sent */
  char sent_name[NAME_LEN];
  unsigned char *sent;
  uint32_t sent_len;
  int fresh, received;
  uint32_t ids[SAVES_MAX + 1][2];
  /* the swap under way: its owner (0 none), when it started, when its owner was last heard,
   * when something was last asked or fetched; this one queued for its own; the last swap's
   * result (received in it) and when it ended */
  struct {
    uint32_t owner;
    double since, heard, busy, retry;
    int queued, got;
    uint32_t tail; /* (v2.3) another's swap over, this one's fetches in it still going: its owner */
    int done_got;
    double done_at;
  } sw;
} X;

static void put32(unsigned char *p, uint32_t v) {
  p[0] = (unsigned char)v, p[1] = (unsigned char)(v >> 8), p[2] = (unsigned char)(v >> 16), p[3] = (unsigned char)(v >> 24);
}
static uint32_t get32(const unsigned char *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void put16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v, p[1] = (unsigned char)(v >> 8); }
static unsigned get16(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }

/* a ghost's file name: g-<id1>-<id2>-a<area>-<stamp>.target.gz; 1 if well made (no other) */
static int parse(const char *n, uint32_t *id1, uint32_t *id2, int *area, uint32_t *stamp) {
  unsigned a, b, s;
  int ar;
  if (strlen(n) >= NAME_LEN || sscanf(n, "g-%u-%u-a%d-%u.target.gz", &a, &b, &ar, &s) != 4 || ar < 0 || ar >= TARGET_AREAS)
    return 0;
  char again[NAME_LEN];
  snprintf(again, sizeof again, "g-%u-%u-a%d-%u.target.gz", a, b, ar, s);
  if (strcmp(again, n)) return 0;
  *id1 = a, *id2 = b, *area = ar, *stamp = s;
  return 1;
}

static void ghost_got(const unsigned char *p, int len);

/* the save's player (two numbers, as a cabinet's card's id1_ / id2_), made once */
static void ids_of(int slot, uint32_t *id1, uint32_t *id2) {
  if (slot < 0 || slot > SAVES_MAX) slot = 0;
  if (X.ids[slot][0]) {
    *id1 = X.ids[slot][0], *id2 = X.ids[slot][1];
    return;
  }
  char path[1200];
  snprintf(path, sizeof path, "%s/ghostid%d", X.save, slot);
  unsigned a = 0, b = 0;
  FILE *f = fopen(path, "r");
  if (f) {
    if (fscanf(f, "%u %u", &a, &b) != 2) a = b = 0;
    fclose(f);
  }
  if (!a) {
    srand((unsigned)time(NULL) ^ (unsigned)(slot * 7919) ^ (unsigned)(uintptr_t)&a);
    a = 100000u + (unsigned)(rand() % 899999), b = 100000u + (unsigned)(rand() % 899999);
    f = fopen(path, "w");
    if (f) fprintf(f, "%u %u\n", a, b), fclose(f);
  }
  X.ids[slot][0] = a, X.ids[slot][1] = b;
  *id1 = a, *id2 = b;
}

/* (v2.3) the saves' own runs (save_dir/myghost), and whose a ghost file is: its save 1..,
 * 0 none of this device's */
const char *gx_own_dir(void) { return X.own_dir; }
int gx_slot_of(const char *name) {
  uint32_t a, b, st;
  int ar;
  if (!parse(name, &a, &b, &ar, &st)) return 0;
  for (int s = 1; s <= SAVES_MAX; ++s)
    if (X.ids[s][0] && X.ids[s][0] == a && X.ids[s][1] == b) return s;
  return 0;
}

static int mine(uint32_t id1, uint32_t id2) {
  for (int s = 0; s <= SAVES_MAX; ++s)
    if (X.ids[s][0] == id1 && X.ids[s][1] == id2) return 1;
  return 0;
}

void gx_init(const char *save_dir) {
  memset(&X, 0, sizeof X);
  snprintf(X.save, sizeof X.save, "%s", save_dir);
  snprintf(X.own_dir, sizeof X.own_dir, "%s/myghost", save_dir);
  snprintf(X.got_dir, sizeof X.got_dir, "%s/ghosts", save_dir);
  mkdir(X.own_dir, 0755);
  mkdir(X.got_dir, 0755);
  uint32_t a, b;
  for (int s = 1; s <= SAVES_MAX; ++s) {
    /* (only the saves' ids already made: a new one on its first ghost) */
    char path[1200];
    snprintf(path, sizeof path, "%s/ghostid%d", X.save, s);
    FILE *f = fopen(path, "r");
    if (f) fclose(f), ids_of(s, &a, &b);
  }
  X.own_at = X.list_at = -1e9;
  net_ghost_handler(ghost_got);
}

const char *gx_dir(void) { return X.got_dir; }
int gx_received(void) { return X.received; }

int gx_take_new(void) {
  int f = X.fresh;
  X.fresh = 0;
  return f;
}

/* the newest stamp in dir of the player's area ghost (0 none); with del, the older ones and
 * that one deleted when older than keep */
static uint32_t newest(const char *dir, uint32_t id1, uint32_t id2, int area, uint32_t keep, int del) {
  DIR *d = opendir(dir);
  if (!d) return 0;
  uint32_t best = 0;
  struct dirent *e;
  while ((e = readdir(d)) != NULL) {
    uint32_t a, b, s;
    int ar;
    if (!parse(e->d_name, &a, &b, &ar, &s) || a != id1 || b != id2 || ar != area) continue;
    if (del && s != keep) {
      char p[1200];
      snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
      remove(p);
      continue;
    }
    if (s > best) best = s;
  }
  closedir(d);
  return best;
}

static uint32_t stamp_now(void) {
  time_t t = time(NULL);
  return t > 1577836800 ? (uint32_t)((t - 1577836800) / 60) : 0;
}

int gx_save_own(const gx_run *r, const char *trail_path) {
  FILE *f = fopen(trail_path, "rb");
  if (!f) return -1;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n <= 0 || n > MAX_FILE) {
    fclose(f);
    return -1;
  }
  unsigned char *trail = malloc((size_t)n);
  size_t got = trail ? fread(trail, 1, (size_t)n, f) : 0;
  fclose(f);
  if (got != (size_t)n) {
    free(trail);
    return -1;
  }
  uint32_t id1, id2, st = stamp_now();
  ids_of(r->slot, &id1, &id2);
  char name[NAME_LEN], path[1200], tmp[1200];
  snprintf(name, sizeof name, "g-%u-%u-a%d-%u.target.gz", id1, id2, r->area, st);
  snprintf(path, sizeof path, "%s/%s", X.own_dir, name);
  snprintf(tmp, sizeof tmp, "%s/new.tmp", X.own_dir);
  gzFile g = gzopen(tmp, "wb6");
  if (!g) {
    free(trail);
    return -1;
  }
  time_t now = time(NULL);
  struct tm tm = *localtime(&now);
  int a = r->area;
  gzprintf(g, "-- %u-%u\nver_ = 7\nid1_ = %u\nid2_ = %u\nid_devel_ = 1\n", id1, id2, id1, id2);
  for (int i = 0; i < 5; ++i) gzprintf(g, "name%d_ = %u\n", i, r->name[i]);
  gzprintf(g, "original_ = 1\n\n-- area %d level %d\n", a, r->level);
  gzprintf(g, "area%d_crown_ = %d\n", a, r->crown ? 1 : 0);
  gzprintf(g, "area%d_year_ = %d\narea%d_month_ = %d\narea%d_day_ = %d\narea%d_hour_ = %d\narea%d_minute_ = %d\narea%d_second_ = %d\n", a,
           tm.tm_year - 100, a, tm.tm_mon + 1, a, tm.tm_mday, a, tm.tm_hour, a, tm.tm_min, a, tm.tm_sec);
  gzprintf(g, "area%d_class_ = 0\narea%d_title_ = %d\n", a, a, r->title);
  gzprintf(g, "area%d_car_ = %d\narea%d_custom_color_ = 0\narea%d_vinyl_ = %d\narea%d_bonnet_ = %d\narea%d_wing_ = %d\n", a, r->car, a, a,
           r->vinyl, a, r->bonnet, a, r->wing);
  gzprintf(g, "area%d_aero_ = %d\narea%d_aero_mirror_ = %d\narea%d_wheel_ = %d\narea%d_neon_ = %d\n", a, r->aero, a, r->mirror, a,
           r->wheel ? r->wheel : 143, a, r->neon);
  gzprintf(g, "area%d_tune_power_ = %d\narea%d_tune_handling_ = %d\narea%d_dress_ = 0\narea%d_ramp_ = %d\narea%d_route_ = %d\n", a, r->power, a,
           r->handling, a, a, r->ramp, a, r->route);
  /* (v2.3) the card's mileage and play style (the aura of its car as the crown holder) */
  gzprintf(g, "area%d_odo_count_ = %d\narea%d_cool_wild_ = %d\narea%d_smooth_rough_ = %d\n", a, r->odo, a, r->cool_wild, a, r->smooth_rough);
  gzprintf(g, "area%d_trail_size_ = %ld\n\n-- trail %d size %ld\n--{{\n", a, n, a, n);
  int ok = gzwrite(g, trail, (unsigned)n) == (int)n;
  gzprintf(g, "\n--}}\n");
  ok &= gzclose(g) == Z_OK;
  free(trail);
  if (!ok || rename(tmp, path) != 0) {
    remove(tmp);
    return -1;
  }
  newest(X.own_dir, id1, id2, a, st, 1); /* (the older runs of the area gone) */
  X.own_at = -1e9;
  fprintf(stderr, "ghost: own run saved, %s (%ld bytes of trail)\n", name, n);
  return 0;
}

/* ---- the exchange ---- */

static void own_read(double now) {
  if (now - X.own_at < OWN_EVERY) return;
  X.own_at = now;
  X.own_n = 0;
  DIR *d = opendir(X.own_dir);
  if (!d) return;
  struct dirent *e;
  while ((e = readdir(d)) != NULL && X.own_n < OWN_MAX) {
    uint32_t a, b, s;
    int ar;
    if (parse(e->d_name, &a, &b, &ar, &s)) snprintf(X.own[X.own_n++], NAME_LEN, "%s", e->d_name);
  }
  closedir(d);
}

static int head(unsigned char *b, uint32_t to, int sub) {
  put32(b, net_my_id());
  put32(b + 4, to);
  b[8] = (unsigned char)sub;
  return 9;
}

static void send_list(uint32_t to) {
  for (int k = 0; k < X.own_n || k == 0; k += 20) {
    unsigned char b[NET_GHOST_MAX];
    int at = head(b, to, LIST), n = X.own_n - k > 20 ? 20 : X.own_n - k;
    if (n <= 0) return;
    b[at++] = (unsigned char)n;
    for (int i = 0; i < n; ++i) {
      memset(b + at, 0, NAME_LEN);
      memcpy(b + at, X.own[k + i], strlen(X.own[k + i]));
      at += NAME_LEN;
    }
    net_ghost_send(b, at);
  }
}

static void name_of(const unsigned char *p, char out[NAME_LEN]) {
  memcpy(out, p, NAME_LEN);
  out[NAME_LEN - 1] = 0;
}

static void want(uint32_t from, const char *name) {
  if (X.dl.on && !strcmp(X.dl.name, name)) return;
  for (int i = 0; i < X.want_n; ++i)
    if (!strcmp(X.want[i].name, name)) return;
  if (X.want_n >= WANT_MAX) return;
  X.want[X.want_n].from = from;
  snprintf(X.want[X.want_n].name, NAME_LEN, "%s", name);
  X.want_n++;
  X.sw.busy = X.poll_at;
}

static void dl_end(void) {
  free(X.dl.buf);
  free(X.dl.have);
  memset(&X.dl, 0, sizeof X.dl);
}

/* a whole file come: checked as a ghost file, kept (the player's older one of the area gone) */
static void dl_done(void) {
  uint32_t id1, id2, st;
  int area;
  if (!parse(X.dl.name, &id1, &id2, &area, &st)) return;
  char tmp[1200], path[1200];
  snprintf(tmp, sizeof tmp, "%s/in.tmp", X.got_dir);
  snprintf(path, sizeof path, "%s/%s", X.got_dir, X.dl.name);
  FILE *f = fopen(tmp, "wb");
  if (!f) return;
  int ok = fwrite(X.dl.buf, 1, X.dl.total, f) == X.dl.total;
  ok &= fclose(f) == 0;
  target_file t;
  ok = ok && target_load(tmp, &t) == 0;
  if (ok) {
    ok = t.area[area].trail_size > 0 && t.id1 == (int)id1 && t.id2 == (int)id2;
    target_free(&t);
  }
  if (!ok || rename(tmp, path) != 0) {
    remove(tmp);
    fprintf(stderr, "ghost: %s came, not a ghost file: dropped\n", X.dl.name);
    return;
  }
  newest(X.got_dir, id1, id2, area, st, 1);
  X.fresh = 1;
  X.received++;
  X.sw.got++;
  fprintf(stderr, "ghost: %s received (%u bytes)\n", X.dl.name, X.dl.total);
}

static void ghost_got(const unsigned char *p, int len) {
  if (len < 9) return;
  uint32_t from = get32(p), to = get32(p + 4);
  if (to && to != net_my_id()) return;
  int sub = p[8];
  p += 9, len -= 9;
  uint32_t me = net_my_id();
  if (sub == SWAP_START) {
    /* another's swap: taken unless one started first (the lowest id keeps it); this one's own,
     * started at the same moment, queued behind it */
    if (X.sw.owner == from) {
      X.sw.heard = X.poll_at;
    } else if (!X.sw.owner || from < X.sw.owner) {
      /* (v2.3) one press swaps both ways for everyone: this one joins it at once, its own
       * ghosts read again and listed to all; its own started at the same moment is the same
       * swap (the lower id's kept), nothing queued */
      if (X.sw.owner == me) fprintf(stderr, "ghost: %u's swap started at the same time: one swap, both ways\n", (unsigned)from);
      X.sw.owner = from;
      X.sw.since = X.sw.heard = X.sw.busy = X.poll_at;
      X.sw.queued = 0, X.sw.got = 0, X.sw.tail = 0;
      X.own_at = -1e9, X.list_at = -1e9;
    }
    return;
  }
  if (sub == SWAP_END) {
    if (X.sw.owner == from) {
      /* (another's swap: its result shown when it brought some, or this one asked; a queued one
       * keeps its count for its own swap next) */
      /* (v2.3: shown once this one's own fetches in it are done too, gx_poll: the swap's tail) */
      X.sw.tail = from;
      X.sw.owner = 0;
      X.sw.retry = X.poll_at + 0.3 * (double)(me % 5); /* (two queued: not at the same moment) */
    }
    return;
  }
  /* (a swap's lists only: its owner's to everyone, everyone's to its owner; the chunks asked
   * for and sent go on till they are done, a swap over or not) */
  if (X.sw.owner && from == X.sw.owner) X.sw.heard = X.poll_at;
  /* (busy: chunks asked for or come; a list only when it brings a ghost wanted) */
  if (sub == REQ || sub == DATA) X.sw.busy = X.poll_at;
  if (sub == LIST) {
    /* (v2.3) everyone's list to everyone while a swap is on (or its tail): one press, all ways */
    if (!X.sw.owner && !X.sw.tail) return;
    if (len < 1) return;
    int n = p[0];
    if (len < 1 + n * NAME_LEN) return;
    for (int i = 0; i < n; ++i) {
      char name[NAME_LEN];
      uint32_t id1, id2, st;
      int area;
      name_of(p + 1 + i * NAME_LEN, name);
      if (!parse(name, &id1, &id2, &area, &st) || mine(id1, id2)) continue;
      if (newest(X.got_dir, id1, id2, area, 0, 0) >= st) continue;
      want(from, name);
    }
    return;
  }
  if (sub == REQ) {
    /* another's ask for chunks of an own ghost: sent while this one is in the lobby */
    if (len < NAME_LEN + 6) return;
    char name[NAME_LEN];
    name_of(p, name);
    unsigned first = get16(p + NAME_LEN);
    uint32_t mask = get32(p + NAME_LEN + 2);
    int own = 0;
    for (int i = 0; i < X.own_n && !own; ++i) own = !strcmp(X.own[i], name);
    if (!own) return;
    if (strcmp(X.sent_name, name)) {
      free(X.sent);
      X.sent = NULL, X.sent_len = 0, X.sent_name[0] = 0;
      char path[1200];
      snprintf(path, sizeof path, "%s/%s", X.own_dir, name);
      FILE *f = fopen(path, "rb");
      if (!f) return;
      fseek(f, 0, SEEK_END);
      long n = ftell(f);
      fseek(f, 0, SEEK_SET);
      if (n > 0 && n <= MAX_FILE && (X.sent = malloc((size_t)n)) != NULL && fread(X.sent, 1, (size_t)n, f) == (size_t)n) {
        X.sent_len = (uint32_t)n;
        snprintf(X.sent_name, NAME_LEN, "%s", name);
      } else {
        free(X.sent);
        X.sent = NULL;
      }
      fclose(f);
      if (!X.sent) return;
    }
    unsigned chunks = (X.sent_len + CHUNK - 1) / CHUNK;
    for (int k = 0; k < WINDOW; ++k) {
      unsigned c = first + (unsigned)k;
      if (!(mask >> k & 1) || c >= chunks) continue;
      unsigned char b[NET_GHOST_MAX];
      int at = head(b, from, DATA);
      memset(b + at, 0, NAME_LEN);
      memcpy(b + at, name, strlen(name));
      at += NAME_LEN;
      unsigned l = X.sent_len - c * CHUNK < CHUNK ? X.sent_len - c * CHUNK : CHUNK;
      put32(b + at, X.sent_len), put16(b + at + 4, c), put16(b + at + 6, l);
      memcpy(b + at + 8, X.sent + c * CHUNK, l);
      net_ghost_send(b, at + 8 + (int)l);
    }
    return;
  }
  if (sub == DATA) {
    if (len < NAME_LEN + 8 || !X.dl.on || from != X.dl.from) return;
    char name[NAME_LEN];
    name_of(p, name);
    if (strcmp(name, X.dl.name)) return;
    uint32_t total = get32(p + NAME_LEN);
    unsigned c = get16(p + NAME_LEN + 4), l = get16(p + NAME_LEN + 6);
    if (len < NAME_LEN + 8 + (int)l || !total || total > MAX_FILE || l > CHUNK) return;
    if (!X.dl.buf) {
      X.dl.total = total;
      X.dl.chunks = (int)((total + CHUNK - 1) / CHUNK);
      X.dl.buf = malloc(total);
      X.dl.have = calloc((size_t)X.dl.chunks, 1);
      if (!X.dl.buf || !X.dl.have) {
        dl_end();
        return;
      }
    }
    if (total != X.dl.total || (int)c >= X.dl.chunks || X.dl.have[c]) return;
    if ((uint32_t)c * CHUNK + l > total || (l != CHUNK && (int)c != X.dl.chunks - 1)) return;
    memcpy(X.dl.buf + c * CHUNK, p + NAME_LEN + 8, l);
    X.dl.have[c] = 1;
    X.dl.got++;
    X.dl.last_data = X.poll_at;
    if (X.dl.got == X.dl.chunks) {
      dl_done();
      dl_end();
    }
  }
}

static void swap_say(int sub) {
  unsigned char b[16];
  int at = head(b, 0, sub);
  net_ghost_send(b, at);
}

static void swap_start(double now) {
  X.sw.owner = net_my_id();
  X.sw.since = X.sw.heard = X.sw.busy = now;
  /* (one queued behind another's: what came in that one counts as its own swap's too) */
  if (!X.sw.queued) X.sw.got = 0;
  X.sw.queued = 0;
  X.list_at = -1e9;
  X.own_at = -1e9; /* (the own ghosts read again: a race since may have saved one) */
  own_read(now);
  fprintf(stderr, "ghost: swap started (%d own ghosts)\n", X.own_n);
}

int gx_swap(void) {
  if (!X.save[0] || !net_is_open() || !net_my_id()) return -1;
  uint32_t me = net_my_id();
  if (X.sw.owner == me) return 0;
  /* (v2.3) another's on: it swaps this one's too, both ways (nothing queued) */
  if (X.sw.owner) return 2;
  X.sw.tail = 0;
  swap_start(X.poll_at);
  return 0;
}

int gx_swap_state(uint32_t *owner, int *got, int *done_got, double *done_ago) {
  if (owner) *owner = X.sw.owner ? X.sw.owner : X.sw.tail;
  if (got) *got = X.sw.got;
  if (done_got) *done_got = X.sw.done_got;
  if (done_ago) *done_ago = X.sw.done_at > 0 ? X.poll_at - X.sw.done_at : 1e9;
  uint32_t me = net_my_id();
  /* (v2.3: another's swap's tail, this one's fetches in it still going, as that swap) */
  if (!X.sw.owner && X.sw.tail) return 2;
  return !X.sw.owner ? (X.sw.queued ? 3 : 0) : X.sw.owner == me ? 1 : X.sw.queued ? 3 : 2;
}

static void swap_over(double now, int mine) {
  if (mine) {
    for (int t = 0; t < 3; ++t) swap_say(SWAP_END);
    fprintf(stderr, "ghost: swap over, %d received\n", X.sw.got);
    X.sw.done_got = X.sw.got, X.sw.done_at = now;
  } else {
    X.sw.tail = X.sw.owner; /* (v2.3: its result once this one's fetches are done) */
  }
  X.sw.owner = 0;
  X.sw.retry = now;
}

void gx_poll(double now) {
  if (!X.save[0]) return;
  if (!net_is_open()) {
    X.sw.owner = 0, X.sw.queued = 0, X.sw.tail = 0; /* (left the game: no swap) */
    return;
  }
  X.poll_at = now;
  own_read(now);
  uint32_t me = net_my_id();
  if (X.sw.owner == me) {
    /* this one's swap: its START and list to everyone each second, its end once nothing more
     * is asked or fetched */
    if (now - X.list_at >= LIST_EVERY) {
      X.list_at = now;
      swap_say(SWAP_START);
      if (X.own_n) send_list(0);
    }
    int quiet = !X.dl.on && !X.want_n && now - X.sw.busy > SWAP_QUIET;
    if ((now - X.sw.since > SWAP_MIN && quiet) || now - X.sw.since > SWAP_MAX) swap_over(now, 1);
  } else if (X.sw.owner) {
    /* another's: (v2.3) this one's list to everyone each second (one press swaps all ways: the
     * owner's and the others' fetch this one's, this one theirs); it gone quiet, taken as over */
    if (now - X.list_at >= LIST_EVERY) {
      X.list_at = now;
      if (X.own_n) send_list(0);
    }
    if (now - X.sw.heard > SWAP_LOST) swap_over(now, 0);
  } else if (X.sw.tail) {
    /* (v2.3) another's swap over: this one's result once its fetches in it are done */
    if (!X.dl.on && !X.want_n) {
      X.sw.done_got = X.sw.got, X.sw.done_at = now;
      fprintf(stderr, "ghost: swap (another's) over, %d received\n", X.sw.got);
      X.sw.tail = 0;
    }
  } else if (X.sw.queued && now >= X.sw.retry) {
    /* the one before over: this one's queued swap now */
    swap_start(now);
  }
  if (!X.dl.on && X.want_n) {
    /* the next one wanted (still newer than what is here) */
    X.dl.on = 1;
    X.dl.from = X.want[0].from;
    snprintf(X.dl.name, NAME_LEN, "%s", X.want[0].name);
    memmove(&X.want[0], &X.want[1], sizeof X.want[0] * (size_t)--X.want_n);
    X.dl.last_data = now, X.dl.last_req = -1e9;
  }
  if (!X.dl.on) return;
  if (now - X.dl.last_data > GIVE_UP) {
    fprintf(stderr, "ghost: %s not coming: dropped\n", X.dl.name);
    dl_end();
    return;
  }
  if (now - X.dl.last_req < REQ_EVERY) return;
  X.dl.last_req = now;
  unsigned first = 0;
  uint32_t mask = 0xffffffffu;
  if (X.dl.buf) {
    while ((int)first < X.dl.chunks && X.dl.have[first]) first++;
    mask = 0;
    for (int k = 0; k < WINDOW; ++k)
      if ((int)first + k < X.dl.chunks && !X.dl.have[first + k]) mask |= 1u << k;
  }
  unsigned char b[NET_GHOST_MAX];
  int at = head(b, X.dl.from, REQ);
  memset(b + at, 0, NAME_LEN);
  memcpy(b + at, X.dl.name, strlen(X.dl.name));
  at += NAME_LEN;
  put16(b + at, first), put32(b + at + 2, mask);
  net_ghost_send(b, at + 6);
}
