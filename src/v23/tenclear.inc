/* (v2.3) ---- the 10-in-a-row's STAGE CLEAR (MENU_TEN_CLEAR: seqTenClearLevel 0x88a9c1c,
 * clSeqTenClearLevelThread). mseqTen runs it after seqRaceTen when the race was neither a
 * retire nor a time up and not the extend mode (all ten beaten at levels 1..10), before
 * seqLvUp, seqTitleSelect and seqMileageResult. Its arg: the card's getTenResult for the course
 * at the ten levels after the race (+0xc..) and before (+0x34..), the level played
 * (getTenLevelSelect, +0x5c), the course (menuOrderFromTenCourseID, +0x60) and the flag +0x6c:
 * ten golds now and nine before (the last gold taken).
 *
 * The screen (stPimpl 0x88ac2e0, yield 0x88aa4ac, clMenuLevel::update 0x88abb90):
 * menu_g_shogo_bg_1 and menu_g_bg_line behind, the card's car (clMenu3DCar: environment 10,
 * makeProjection(73, -1, 4/3, 5, 20000), setInterest((0, 10, -43), (0, 0, 0)), turned 160
 * degrees about y at (-3, -2, 0)), menu_layout_or_result_B (STAGE CLEAR, its bar, the course's
 * name swapped into 0xcbe; once, held at its end), menu_g_or_result_B_icon (the ten medals
 * coming in with their LEVEL n, 110 frames once). When the icon's run is over its medal
 * templates (0x740..0x748 levels 1..9, 0x73f level 10) take each level's medal (0x940db28:
 * menu_p_or_medal_black, _copper, _silver, _gold) and the level played gets its ring
 * (menu_g_or_result_B_ring frames 100 k + 1 .. + 59, k = the level, once) with SE 0x940db38 by
 * its medal (gold 0x30 rank, else 0x41 parts_equip); with the flag, once that ring is over,
 * every level's ring at once and SE 0x4f (parts_comp). run (0x88a9110): fade in, then a tap
 * or 20 s (evtTimeOut), fade out. ---- */
enum { T_TCL_COURSE = 0xcbe };
#define TCL_ICON_LEN 110  /* menu_g_or_result_B_icon's frames */
#define TCL_RING_LEN 59   /* a level's ring: 100 k + 1 .. 100 k + 59 */
#define TCL_TIMEOUT 1200  /* 20 s */
static const int TCL_MEDAL_TEMPLATE[10] = { 0x740, 0x741, 0x742, 0x743, 0x744, 0x745, 0x746, 0x747, 0x748, 0x73f };
static const int TCL_MEDAL[4] = { 0xf29, 0x1281, 0x1282, 0xca3 }; /* none, bronze, silver, gold */

static struct {
  int bg, line, layout, icon, ring; /* -2: not looked up yet */
  int due;
  int level;                        /* the level played (0..9) */
  signed char before[10], after[10];
  int all;                          /* the last gold taken: every ring */
  int t, out, ring_at, all_at;      /* frames: on the screen, fading out, the ring's start, the rings' */
} TCL = { -2, -2, -2, -2, -2 };

static void tenclear_find(void) {
  if (TCL.layout != -2) return;
  TCL.bg = anm_find("menu_g_shogo_bg_1"), TCL.line = anm_find("menu_g_bg_line");
  TCL.layout = anm_find("menu_layout_or_result_B"), TCL.icon = anm_find("menu_g_or_result_B_icon");
  TCL.ring = anm_find("menu_g_or_result_B_ring");
}

/* the race's end (app.c), before the card takes the medal: the course's medals by level as
 * they were (m->ten_result[menu_ten_course(m)], 0 none .. 3 gold); armed only for a level 1..10
 * cleared, all ten beaten */
void menu_tenclear_arm(menu_state *m, const signed char before[11]) {
  if (m->ten_level < 0 || m->ten_level > 9) return;
  TCL.due = 1;
  TCL.level = m->ten_level;
  for (int k = 0; k < 10; ++k) TCL.before[k] = before[k];
}

static void tenclear_open(menu_state *m) {
  tenclear_find();
  go(m, MENU_TEN_CLEAR);
  TCL.due = 0;
  const signed char *now = m->ten_result[menu_ten_course(m)];
  int gold = 0, gold0 = 0;
  for (int k = 0; k < 10; ++k) {
    TCL.after[k] = now[k] < 0 ? 0 : now[k] > 3 ? 3 : now[k];
    gold += TCL.after[k] == 3, gold0 += TCL.before[k] == 3;
  }
  TCL.all = gold == 10 && gold0 == 9;
  TCL.t = TCL.out = 0;
  TCL.ring_at = TCL.all_at = -1;
  m->bars = 0;
  m->music = 1;
  m->tap = m->key = m->key_held = m->back = m->leaving = 0;
}

/* back_from_race: the STAGE CLEAR now if it is due (out of the black); 1 if it opened */
static int tenclear_due(menu_state *m, int white) {
  if (!TCL.due) return 0;
  tenclear_find();
  if (white || TCL.layout < 0 || TCL.icon < 0 || m->game != MENU_GAME_TEN) {
    TCL.due = 0;
    return 0;
  }
  tenclear_open(m);
  return 1;
}

static void tenclear_step(menu_state *m, int tap, int key) {
  int t = ++TCL.t;
  if (TCL.out) {
    if (++TCL.out > FADE) back_from_race(m, 0);
    return;
  }
  /* the icon's run over: the medals in, the level's ring and its SE */
  if (t == TCL_ICON_LEN) {
    TCL.ring_at = t;
    snd_effect(TCL.after[TCL.level] == 3 ? "rank" : "parts_equip", 1.0f); /* SE 0x30 / 0x41 */
  }
  /* the last gold: every ring once the level's is over (SE 0x4f) */
  if (TCL.all && TCL.ring_at >= 0 && TCL.all_at < 0 && t >= TCL.ring_at + TCL_RING_LEN) {
    TCL.all_at = t;
    snd_effect("parts_comp", 1.0f);
  }
  if (t < FADE) return;
  if (tap || key || t >= FADE + TCL_TIMEOUT) TCL.out = 1;
}

static void draw_tenclear(const menu_state *m, void (*draw3d)(void *), void *ctx) {
  int t = TCL.t;
  float a = TCL.out ? 1.0f - (float)TCL.out / FADE : t < FADE ? (float)(t + 1) / FADE : 1.0f;
  if (a < 0) a = 0;
  anm_fill(0, 0, 0, 1);
  if (TCL.bg >= 0) anm_draw(TCL.bg, 0, 1);
  if (TCL.line >= 0) anm_draw(TCL.line, t % (anm_frames(TCL.line) > 0 ? anm_frames(TCL.line) : 1), 1);
  if (draw3d) draw3d(ctx);
  if (TCL.layout >= 0) {
    int n = anm_frames(TCL.layout), ci = menu_ten_course(m);
    anm_swap sw = { spr(T_TCL_COURSE), ci >= 0 && ci < 10 ? spr(TEN_COURSE_NAME[ci]) : -1 };
    anm_draw_ex(TCL.layout, t < n ? t : n - 1, 1, 0, 0, &sw, 1);
  }
  if (TCL.icon >= 0) {
    int n = anm_frames(TCL.icon);
    anm_swap sw[10];
    int ns = 0;
    if (t >= TCL_ICON_LEN) /* (the medals once the run is over: the placeholders before) */
      for (int k = 0; k < 10; ++k) sw[ns++] = (anm_swap){ spr(TCL_MEDAL_TEMPLATE[k]), spr(TCL_MEDAL[TCL.after[k]]) };
    anm_draw_ex(TCL.icon, t < n ? t : n - 1, 1, 0, 0, sw, ns);
  }
  if (TCL.ring >= 0 && TCL.ring_at >= 0) {
    int n = anm_frames(TCL.ring);
    for (int k = 0; k < 10; ++k) {
      int at = k == TCL.level ? TCL.ring_at : TCL.all_at;
      if (at < 0 || (k != TCL.level && !TCL.all)) continue;
      if (k == TCL.level && TCL.all_at >= 0) at = TCL.all_at; /* (its own again with the rest) */
      int g = t - at, f = 100 * k + 1 + (g < TCL_RING_LEN - 1 ? g : TCL_RING_LEN - 1);
      if (f < n) anm_draw(TCL.ring, f, 1);
    }
  }
  anm_fill(0, 0, 0, 1.0f - a);
}

/* menu_scene3d's: clMenu3DCar, the card's car turned 160 degrees about y at (-3, -2, 0),
 * environment 10, 73 degrees, the eye (0, 10, -43) on the origin */
static int tenclear_scene3d(const menu_state *m, menu_scene *s) {
  s->env = 10;
  s->fov_x = 73.0f * DEG;
  s->eye[0] = 0, s->eye[1] = 10.0f, s->eye[2] = -43.0f;
  s->at[0] = 0, s->at[1] = 0, s->at[2] = 0;
  const float pos[3] = { -3.0f, -2.0f, 0 };
  mat4 mm = tg_rotation(pos, 0, 160.0f, 0);
  const cardb_color *c = &chosen_car(m, m->car)->colors[m->color];
  s->cars[0].pack = c->pack;
  s->cars[0].paint = c->paint;
  s->cars[0].aero = m->aero[m->maker][m->car];
  tg_wear(m, &s->cars[0]);
  for (int e = 0; e < 16; ++e) s->cars[0].model[e] = mm.m[e];
  s->n = 1;
  return 1;
}
