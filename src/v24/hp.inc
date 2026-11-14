/* (v2.4) the versus' HP BATTLE (VS_MODE_HP): see hp.h. Included by app.c after G.
 *
 * The rule (each machine the same, from the race's shared positions):
 *  - a car's position: m run, -(m to the goal); this car's from its remain, a link rival's from
 *    remain + vs_gap (its odometer, as the link sends it).
 *  - drain: m behind the leader of the cars still in, past HP_FREE: (gap - HP_FREE) * HP_K per s
 *    (100 m behind: the whole gauge in ~20 s), at most HP_DRAIN_MAX per s.
 *  - hits: the speed over the last 0.1 s below that of the 0.3 s before by HP_HIT_DV (m/s; hard
 *    braking stays well under it): HP_HIT_ONSET at once, then (drop - HP_HIT_DV) * HP_HIT_K per s
 *    while it lasts.
 *  - 0: out, its KO's turn kept. A link rival gone (vs_on 0 mid-race) is out then too.
 *  - the end: this car out (its place: 1 + the others still in then), every rival out (1st), or
 *    the race's own end (the goal, time up: 1 + those still in ahead). */
#include "hp.h"

#define HP_CARS (CAR_RIVALS + 1)
#define HP_FREE 15.0f
#define HP_K (1.0f / (85.0f * 20.0f))
#define HP_DRAIN_MAX 0.25f
#define HP_HIT_DV 8.0f
#define HP_HIT_ONSET 0.03f
#define HP_HIT_K 0.02f
#define HP_RING 64

static struct {
  int on;              /* an HP battle running (started, the countdown over) */
  int over;            /* ended here: this car out, or the last one in */
  int n;               /* cars in the race (this one + the link rivals) */
  float hp[HP_CARS];
  int out[HP_CARS];    /* KO turn, 1 the first; 0 in */
  int ko_n;
  int place;           /* the place once over (0 not yet) */
  int hitting[HP_CARS];
  float t;             /* s since the start */
  float ring_t[HP_RING], ring_p[HP_RING][HP_CARS];
  int ring_i, ring_n;
} HP;

int hp_active(void) { return HP.on; }
float hp_of(int who) {
  if (!HP.on || who < 0 || who >= HP_CARS || who >= HP.n) return 1.0f;
  return HP.hp[who] < 0.0f ? 0.0f : HP.hp[who] > 1.0f ? 1.0f : HP.hp[who];
}
int hp_out(int who) { return HP.on && who >= 0 && who < HP_CARS ? HP.out[who] : 0; }

static void hp_reset(void) {
  memset(&HP, 0, sizeof HP);
  for (int w = 0; w < HP_CARS; ++w) HP.hp[w] = 1.0f;
}

/* the position (m run) HP.ring holds s ago (the newest at least that old) */
static float hp_pos_ago(int w, float ago) {
  int k = HP.ring_i;
  for (int j = 0; j < HP.ring_n; ++j) {
    k = (HP.ring_i - 1 - j + HP_RING) % HP_RING;
    if (HP.t - HP.ring_t[k] >= ago) break;
  }
  return HP.ring_p[k][w];
}

static void hp_ko(int w) {
  if (HP.out[w]) return;
  HP.hp[w] = 0.0f;
  HP.out[w] = ++HP.ko_n;
  fprintf(stderr, "hp battle: car %d out (KO %d) at %.2f s\n", w, HP.out[w], HP.t);
}

/* each frame of the race, dt s */
static void hp_tick(const car_race *r, float dt) {
  if (!G.vs_race || menu_vs_mode(&G.menu) != VS_MODE_HP) {
    HP.on = 0;
    return;
  }
  if (r->countdown > 0.0f || r->total <= 0.0f) { /* before the start: all full */
    hp_reset();
    return;
  }
  if (!HP.on) {
    hp_reset();
    HP.on = 1;
    HP.n = 1 + (G.vs_race < CAR_RIVALS ? G.vs_race : CAR_RIVALS);
  }
  if (HP.over || r->over || dt <= 0.0f) return;
  HP.t += dt;
  float p[HP_CARS] = { 0 };
  p[0] = -r->remain;
  for (int w = 1; w < HP.n; ++w) {
    if (!r->vs_on[w - 1] && r->vs_left) hp_ko(w); /* gone from the link */
    p[w] = -(r->remain + r->vs_gap[w - 1]);
  }
  HP.ring_t[HP.ring_i] = HP.t;
  memcpy(HP.ring_p[HP.ring_i], p, sizeof p);
  HP.ring_i = (HP.ring_i + 1) % HP_RING;
  if (HP.ring_n < HP_RING) HP.ring_n++;
  float lead = -1e30f;
  for (int w = 0; w < HP.n; ++w)
    if (!HP.out[w] && p[w] > lead) lead = p[w];
  for (int w = 0; w < HP.n; ++w) {
    if (HP.out[w]) continue;
    float behind = lead - p[w], drain = 0.0f;
    if (behind > HP_FREE) drain = (behind - HP_FREE) * HP_K;
    if (drain > HP_DRAIN_MAX) drain = HP_DRAIN_MAX;
    float loss = drain * dt;
    if (HP.t > 0.5f) { /* the hits: a sudden loss of speed */
      float p1 = hp_pos_ago(w, 0.1f), p4 = hp_pos_ago(w, 0.4f);
      float v_now = (p[w] - p1) / 0.1f, v_before = (p1 - p4) / 0.3f, dv = v_before - v_now;
      if (v_before > 5.0f && dv > HP_HIT_DV) {
        if (!HP.hitting[w]) loss += HP_HIT_ONSET;
        loss += (dv - HP_HIT_DV) * HP_HIT_K * dt;
        HP.hitting[w] = 1;
      } else {
        HP.hitting[w] = 0;
      }
    }
    HP.hp[w] -= loss;
  }
  /* the KOs: the lowest first when several go at once (the same order on every machine) */
  for (;;) {
    int lo = -1;
    for (int w = 0; w < HP.n; ++w)
      if (!HP.out[w] && HP.hp[w] <= 0.0f && (lo < 0 || HP.hp[w] < HP.hp[lo])) lo = w;
    if (lo < 0) break;
    hp_ko(lo);
  }
  int in = 0;
  for (int w = 1; w < HP.n; ++w) in += !HP.out[w];
  if (HP.out[0]) {
    HP.over = 1, HP.place = 1 + in;
  } else if (!in && HP.n > 1) {
    HP.over = 1, HP.place = 1;
  }
  if (HP.over) fprintf(stderr, "hp battle: over here, place %d of %d\n", HP.place, HP.n);
}

/* this race ended by the HP rule (app_frame's race end) */
static int hp_over(void) { return HP.on && HP.over; }

/* the place at the race's end (the HP rule's: KO order behind the cars still in); *of the cars
 * that started it. 0: not an HP battle (the race's own place stands) */
static int hp_place(const car_race *r, int *of) {
  if (!HP.on) return 0;
  *of = HP.n;
  if (HP.place) return HP.place;
  /* the race's own end (the goal, time up): the cars still in ahead, by position */
  if (HP.out[0]) return HP.n - HP.out[0] + 1;
  int ahead = 0;
  for (int w = 1; w < HP.n; ++w)
    if (!HP.out[w] && r->vs_gap[w - 1] < 0.0f) ahead++;
  if (r->vs_place == 1) ahead = 0; /* first at the goal */
  return 1 + ahead;
}
