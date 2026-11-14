/* (v2.3) The first ghost battle's (分身対戦 tutorial) message windows inside the ghost battle's
 * own menus. #included into src/menu.c after src/v23/tutorial.inc (anm_find, anm_draw_ex,
 * anm_sprite, anm_fill, snd_menu known). The integrator sets tgtut_on() while the first ghost
 * battle tutorial runs (clWorkSpace::isTargetFirstTutorial, workspace +0xa7) and clears it after.
 * Animations / sprites from data/extra.bin (tools/tutorial_sprites.py); none there = nothing.
 *
 * The arcade (main):
 *  clSeqTgSearchTypeSelectThread::run (0x884949c), when [0x97ff180]+0xd (the tutorial):
 *    ANM 0x304 menu_p_tutorial_bunsin04 (95 frames) at its authored place, the menu's dim
 *    clMenuFade to 0.5 (0x9771190) behind it, menu input not read:
 *      setAnime(ANM 4, 0, 0x18)  frames 0..0x18 once, then (isBound)
 *      setAnime(ANM 6, 0x18, 0x3c) frames 0x18..0x54 looped;
 *      counter 0x708 (1800 frames) or the accelerator (chkSW 0x10, at any time) ->
 *      setAnime(ANM 4, 0x55, 0xa) frames 0x55..0x5f once, 9 more frames, the dim off.
 *    Then the hint window per spin index k (tables 0x94082e0..): ANM 0x302 bunsin01_07 for
 *    k 0 (Lv), 0x303 bunsin01_07a for 1..3 with sprite 0x18d5 replaced by
 *    {0x18d5, 0x18d6, 0x18e2, 0x18d7}[k]: in 0..4, loop 4 + {0x32, 0xf}, out {0x37, 0x14} + 5;
 *    the spin moved: the old one's out, then the new one's in; decided: out.
 *    In the tutorial only k 0 decides; any other is refused (SNDOneshot 0x52, the buzzer).
 *  clSeqTgRivalSelectThread::run (0x885b5a4), [0x97ff198]+0x1e: the same 0x304 window and
 *    timings, sprite 0x18d4 replaced by 0x18d8.
 *  clseqTgCourseSelectThread ctor (0x88932cd): 0x304, 0x18d4 -> 0x18e3 (same window; its
 *    run's timing not traced, taken as the others').
 *  clSeqTgRivalDataDisp::run (0x8866098): ANM 0x305 menu_p_tutorial_bunsin07 (105 frames),
 *    0..0x18 once, 0x18 + 0x46 looped, out 0x5f + 0xa; 0x6b replaced by a clSpriteFont string
 *    (the rival's name; here hidden). Offered as tgtut_menu_show(TGT_DATA) for the integrator.
 *  (0x306 / 0x309 are clSeqTgMatchingInformation's, in the race's loading: not here.)
 */


enum { TGT_SEARCH, TGT_RIVAL, TGT_AREA, TGT_DATA, TGT_N };
enum { TGT_IN_LEN = 0x18, TGT_LOOP_LEN = 0x3c, TGT_OUT = 0x55, TGT_OUT_LEN = 0xa, TGT_WAIT = 9,
       TGT_TIMEOUT = 0x708 };
enum { TGT_PH_OFF, TGT_PH_IN, TGT_PH_LOOP, TGT_PH_OUT, TGT_PH_WAIT };

static struct {
  int shown;          /* bit per TGT_*: shown once this tutorial */
  int which, phase, t, timer, anim;
  float dim;          /* the clMenuFade behind it, 0..0.5 */
  /* the search's hint window */
  int hint_k, hint_ph, hint_t, hint_dec;
} TGM = { 0, -1 };

static const char *const TGT_ANIM[TGT_N] = { "menu_p_tutorial_bunsin04", "menu_p_tutorial_bunsin04",
                                             "menu_p_tutorial_bunsin04", "menu_p_tutorial_bunsin07" };
static const int TGT_TEXT[TGT_N] = { 0x18d4, 0x18d8, 0x18e3, -1 }; /* (0x18d4 replaced by) */

static int tgt_loop_len(void) { return TGM.which == TGT_DATA ? 0x46 : TGT_LOOP_LEN; }
static int tgt_out(void) { return TGM.which == TGT_DATA ? 0x5f : TGT_OUT; }

/* (the integrator: at the tutorial's start, so every window shows once again) */
static void tgtut_menu_reset(void) {
  TGM.shown = 0;
  TGM.phase = TGT_PH_OFF;
  TGM.hint_ph = -1;
}

static void tgtut_menu_show(int which) {
  if (!tgtut_on() || which < 0 || which >= TGT_N || (TGM.shown >> which & 1)) return;
  int a = anm_find(TGT_ANIM[which]);
  TGM.shown |= 1 << which;
  if (a < 0 || anm_frames(a) <= 0) return; /* (extra.bin without it) */
  TGM.which = which, TGM.anim = a, TGM.phase = TGT_PH_IN, TGM.t = 0, TGM.timer = TGT_TIMEOUT;
  TGM.hint_ph = -1;
}

static int tgt_screen_which(const menu_state *m) {
  if (m->screen == MENU_TG_SEARCH) return TGT_SEARCH;
  if (m->screen == MENU_TG_RIVAL) return TGT_RIVAL;
  if (m->screen == MENU_TG_AREA) return TGT_AREA;
  return -1;
}

/* the search's hint: the phase lengths of 0x302 (k 0) / 0x303 */
static void tgt_hint_ranges(int k, int *loop_len, int *out) {
  *loop_len = k == 0 ? 0x32 : 0xf;
  *out = k == 0 ? 0x37 : 0x14;
}

static void tgt_hint_step(const menu_state *m) {
  if (m->screen != MENU_TG_SEARCH) {
    TGM.hint_ph = -1;
    return;
  }
  int k = m->tgc_move ? -1 : m->tg_search; /* (moving: the old one goes out) */
  if (TGM.hint_ph < 0) { /* (start) */
    if (k < 0) return;
    TGM.hint_k = k, TGM.hint_ph = 0, TGM.hint_t = 0;
    return;
  }
  TGM.hint_t++;
  int loop_len, out;
  tgt_hint_ranges(TGM.hint_k, &loop_len, &out);
  if (TGM.hint_ph == 0 && TGM.hint_t >= 4) TGM.hint_ph = 1, TGM.hint_t = 0;
  if (TGM.hint_ph < 2 && (k != TGM.hint_k || m->tgc_dec)) TGM.hint_ph = 2, TGM.hint_t = 0;
  if (TGM.hint_ph == 2 && TGM.hint_t >= 5) {
    if (m->tgc_dec || k < 0) TGM.hint_ph = 3, TGM.hint_t = 0; /* (gone, till a new k) */
    else TGM.hint_k = k, TGM.hint_ph = 0, TGM.hint_t = 0;
  }
  if (TGM.hint_ph == 3 && k >= 0 && !m->tgc_dec) TGM.hint_k = k, TGM.hint_ph = 0, TGM.hint_t = 0;
}

/* Every frame before the screen's own step (MENU_TG_*): 1 = the window has the input (the
 * screen's step not run). */
static int tgtut_menu_step(menu_state *m, int tap, int key, int back) {
  (void)back;
  if (!tgtut_on()) {
    TGM.phase = TGT_PH_OFF;
    return 0;
  }
  if (TGM.phase == TGT_PH_OFF && m->t > 0) {
    int w = tgt_screen_which(m);
    if (w >= 0) tgtut_menu_show(w);
  }
  if (TGM.phase == TGT_PH_OFF) {
    if (TGM.dim > 0) TGM.dim -= 0.5f / 10;
    if (TGM.dim < 0) TGM.dim = 0;
    if (TGM.shown >> TGT_SEARCH & 1) tgt_hint_step(m);
    return 0;
  }
  if (TGM.dim < 0.5f) TGM.dim += 0.5f / 10;
  if (TGM.dim > 0.5f) TGM.dim = 0.5f;
  TGM.t++;
  switch (TGM.phase) {
  case TGT_PH_IN:
  case TGT_PH_LOOP:
    if (TGM.phase == TGT_PH_IN && TGM.t >= TGT_IN_LEN) TGM.phase = TGT_PH_LOOP, TGM.t = 0;
    /* (the accelerator at any time, or 1800 frames) */
    if (tap || key || --TGM.timer <= 10) {
      snd_menu(SND_SELECT);
      TGM.phase = TGT_PH_OUT, TGM.t = 0;
    }
    break;
  case TGT_PH_OUT:
    if (TGM.t >= TGT_OUT_LEN) TGM.phase = TGT_PH_WAIT, TGM.t = 0;
    break;
  case TGT_PH_WAIT:
    if (TGM.t >= TGT_WAIT) TGM.phase = TGT_PH_OFF, TGM.hint_ph = -1;
    break;
  }
  return 1;
}

/* In tg_search_step, before a decide: the tutorial decides Lv (k 0) only, the others refused
 * (the arcade's buzzer, SNDOneshot 0x52). 1 = refused. */
static int tgtut_search_refuse(const menu_state *m) {
  if (!tgtut_on() || m->tg_search == 0) return 0;
  snd_menu(SND_CANCEL);
  return 1;
}

/* Over the screen (after draw_tg_*, before the keys' HUD). */
static void tgtut_menu_draw(const menu_state *m) {
  if (!tgtut_on()) return;
  /* the search's hint window (after the message, while choosing) */
  if (m->screen == MENU_TG_SEARCH && (TGM.shown >> TGT_SEARCH & 1) && TGM.phase == TGT_PH_OFF && TGM.hint_ph >= 0 &&
      TGM.hint_ph < 3) {
    int k = TGM.hint_k, a = anm_find(k == 0 ? "menu_p_tutorial_bunsin01_07" : "menu_p_tutorial_bunsin01_07a");
    if (a >= 0) {
      static const int TYPE_SPR[4] = { 0x18d5, 0x18d6, 0x18e2, 0x18d7 };
      int loop_len, out, f;
      tgt_hint_ranges(k, &loop_len, &out);
      if (TGM.hint_ph == 0) f = TGM.hint_t;
      else if (TGM.hint_ph == 1) f = 4 + TGM.hint_t % loop_len;
      else f = out + (TGM.hint_t < 4 ? TGM.hint_t : 4);
      anm_swap sw = { anm_sprite(0x18d5), anm_sprite(TYPE_SPR[k & 3]) };
      if (f >= anm_frames(a)) f = anm_frames(a) - 1;
      anm_draw_ex(a, f, 1, 0, 0, &sw, sw.from >= 0 && sw.to >= 0 ? 1 : 0);
    }
  }
  if (TGM.dim > 0) anm_fill(0, 0, 0, TGM.dim);
  if (TGM.phase == TGT_PH_OFF || TGM.phase == TGT_PH_WAIT) return;
  int f;
  if (TGM.phase == TGT_PH_IN) f = TGM.t;
  else if (TGM.phase == TGT_PH_LOOP) f = TGT_IN_LEN + TGM.t % tgt_loop_len();
  else f = tgt_out() + (TGM.t < TGT_OUT_LEN ? TGM.t : TGT_OUT_LEN - 1);
  if (f >= anm_frames(TGM.anim)) f = anm_frames(TGM.anim) - 1;
  anm_swap sw[1];
  int n = 0;
  if (TGM.which == TGT_DATA) {
    sw[0].from = anm_sprite(0x6b), sw[0].to = -1; /* (the rival's name: a font string there) */
    n = sw[0].from >= 0;
  } else if (TGT_TEXT[TGM.which] != 0x18d4) {
    sw[0].from = anm_sprite(0x18d4), sw[0].to = anm_sprite(TGT_TEXT[TGM.which]);
    n = sw[0].from >= 0 && sw[0].to >= 0;
  }
  anm_draw_ex(TGM.anim, f, 1, 0, 0, sw, n);
}
