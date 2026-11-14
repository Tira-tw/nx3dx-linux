/* (v2.4) The versus' EVENT rule (menu_state.vs.mode == VS_MODE_EVENT): a co-op race. The linked
 * players race together against one CPU pace rival; the TEAM clears the event if any of them
 * reaches the goal before it (EVENT CLEAR), else EVENT FAILED, the same result on every machine.
 *
 * The rival is not a car on the road (the link race, EGameMode 5, has no CPU car beside the link
 * rivals): it is a virtual one running at a steady pace from the start's GO, its character and
 * pace picked from the race's shared seed, ramp and km (car_set_versus') so that every machine
 * has the same one with nothing more sent. The team's lead is the furthest linked car's (the
 * player's run, or a link rival's from Get2DInfoBattle's gaps).
 *
 * Included into app.c (after G); the hooks there are marked "(v2.4)". */

typedef struct {
  int on;          /* this race is an EVENT */
  int done, clear; /* decided (and how) */
  int who;         /* the rival (EV_RIVALS) */
  float kmh;       /* its pace */
  float len;       /* m from the start to the goal (the race's remain at the start) */
  float lead;      /* m the team's furthest car has run */
  float t_done;    /* s since the start when decided */
} ev_state;
static ev_state EV;

static const struct { const char *name, *car; } EV_RIVALS[] = {
  { "TATSUYA SHIMA", "BLACKBIRD" },
  { "REINA AKIKAWA", "R32 GT-R" },
  { "EIJI KUROKI", "FD3S RX-7" },
  { "KOICHI HIRAMOTO", "R34 GT-R" },
  { "MASAKI", "Z33" },
  { "AKIO ASAKURA", "S30Z" },
};
#define EV_N_RIVALS ((int)(sizeof EV_RIVALS / sizeof EV_RIVALS[0]))

static unsigned ev_hash(unsigned x) {
  x ^= x >> 16; x *= 0x7feb352du;
  x ^= x >> 15; x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

/* the race's start (after car_set_versus): the same rival and pace on every machine */
static void ev_start(int mode, unsigned seed, int ramp, int km) {
  memset(&EV, 0, sizeof EV);
  EV.on = mode == VS_MODE_EVENT;
  if (!EV.on) return;
  unsigned h = ev_hash(seed ^ ev_hash((unsigned)ramp * 131u + (unsigned)km));
  EV.who = (int)(h % (unsigned)EV_N_RIVALS);
  /* a steady 175..214 km/h from GO: a fast but beatable run through the traffic (the harder
   * rivals at the end of the table go faster) */
  EV.kmh = 175.0f + (float)(h >> 8 & 15) + 4.0f * (float)EV.who;
  if (EV.kmh > 214.0f) EV.kmh = 214.0f;
  EV.len = 0;
  fprintf(stderr, "event: co-op vs %s (%s) at %.0f km/h (seed %u ramp %d km %d)\n", EV_RIVALS[EV.who].name,
          EV_RIVALS[EV.who].car, EV.kmh, seed, ramp, km);
}

static float ev_rival_m(const car_race *r) { return r->total > 0 ? r->total * EV.kmh / 3.6f : 0.0f; }

static void ev_decide(const car_race *r, int clear) {
  if (EV.done) return;
  EV.done = 1;
  EV.clear = clear;
  EV.t_done = r->total;
  fprintf(stderr, "event: %s at %.2f s (team %.0f m, rival %.0f m of %.0f)\n", clear ? "CLEAR" : "FAILED", r->total, EV.lead,
          ev_rival_m(r), EV.len);
}

/* each race frame (a versus): the team's lead against the rival's run */
static void ev_step(const car_race *r, int n_link) {
  if (!EV.on || !G.vs_race) return;
  if (EV.len <= 0) {
    if (r->remain > 0) EV.len = r->remain;
    return;
  }
  float mine = EV.len - r->remain;
  if (r->state == CAR_RACE_GOAL) mine = EV.len;
  float lead = mine;
  for (int i = 0; i < n_link && i < CAR_RIVALS; ++i)
    if (r->vs_on[i] && mine - r->vs_gap[i] > lead) lead = mine - r->vs_gap[i];
  /* a link rival through the goal (ahead of the player, the race's place taken) */
  if (r->vs_rank > 0 && lead >= EV.len - 2.0f) lead = EV.len;
  if (lead > EV.lead) EV.lead = lead;
  if (EV.done || r->total <= 0) return;
  if (EV.lead >= EV.len) ev_decide(r, ev_rival_m(r) < EV.len);
  else if (ev_rival_m(r) >= EV.len) ev_decide(r, 0);
}

/* the race over on this machine: decided if not yet (the player's own run: the goal before the
 * rival's), then the result screen's WIN / LOSE as the team's */
static void ev_finish(menu_state *m, const car_race *r, int goal) {
  if (!EV.on) return;
  if (!EV.done) ev_decide(r, goal && r->total > 0 && ev_rival_m(r) < EV.len);
  m->finish_story = EV.clear ? 5 : 6;
  m->star_delta = 0; /* (no stars won or lost in an event) */
}

/* over the race and its result */
static void ev_draw(const car_race *r, int finishing) {
  if (!EV.on || !G.vs_race) return;
  char l1[96], l2[96];
  float w = G.hud.screen_w, px;
  hud_ui_begin(G.hud.screen_w, G.hud.screen_h);
  if (finishing || EV.done) {
    const char *s = EV.clear ? "EVENT CLEAR" : "EVENT FAILED";
    px = 6.0f;
    float tw = hud_ui_text_width(s, px);
    hud_ui_rect(0, 150, w, 70, 0, 0, 0, 0.55f);
    if (EV.clear) hud_ui_text(s, 0.5f * (w - tw), 160, px, 1.0f, 0.8f, 0.1f, 1);
    else hud_ui_text(s, 0.5f * (w - tw), 160, px, 0.6f, 0.75f, 1.0f, 1);
    snprintf(l1, sizeof l1, "TEAM VS %s (%s)", EV_RIVALS[EV.who].name, EV_RIVALS[EV.who].car);
    px = 2.0f;
    hud_ui_text(l1, 0.5f * (w - hud_ui_text_width(l1, px)), 204, px, 1, 1, 1, 0.9f);
  } else if (r->state == CAR_RACE_RUN) {
    float gap = EV.lead - ev_rival_m(r);
    snprintf(l1, sizeof l1, "EVENT  TEAM VS %s (%s %.0f KM/H)", EV_RIVALS[EV.who].name, EV_RIVALS[EV.who].car, EV.kmh);
    snprintf(l2, sizeof l2, "TEAM %s%.0f M", gap >= 0 ? "+" : "-", gap >= 0 ? gap : -gap);
    px = 2.0f;
    hud_ui_rect(0, 96, w, 48, 0, 0, 0, 0.45f);
    hud_ui_text(l1, 0.5f * (w - hud_ui_text_width(l1, px)), 100, px, 1, 1, 1, 0.95f);
    if (gap >= 0) hud_ui_text(l2, 0.5f * (w - hud_ui_text_width(l2, 2.6f)), 120, 2.6f, 0.4f, 1.0f, 0.4f, 1);
    else hud_ui_text(l2, 0.5f * (w - hud_ui_text_width(l2, 2.6f)), 120, 2.6f, 1.0f, 0.4f, 0.3f, 1);
  }
  hud_ui_end();
}
