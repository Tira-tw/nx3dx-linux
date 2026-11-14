/* (v2.3) The first-time tutorials: the ghost battle's (分身対戦チュートリアル) and the dress-up's
 * (ドレスアップチュートリアル). #included into src/menu.c (after anm.h and snd_menu are known);
 * everything here is static. The animations are the arcade's own, added to data/extra.bin by
 * tools/tutorial_sprites.py (anm_find by name; none there = no tutorial, nothing drawn).
 *
 * The arcade (main):
 *  mseqTarget: on entering the ghost battle, when the card's before-mode data +0x1a4 is 0 (no
 *  ghost battle played yet) and clWorkSpace::isTargetFirstPlay: setTargetFirstTutorial(1),
 *  BGM 0x29 looped, then
 *   seqTgTutorial (clSeqTgTutorialThread): fade in; the scene table at 0x940a2d8 (the same for
 *     both of its rows): ANM 0x2b7 menu_layout_tutorial_game_1, ANM 0x2b8 _game_2, each
 *     setAnime(id, 4, 0, 0) = once, whole; clMenuInput::chkSW(0x10) (the accelerator: "アクセル
 *     でスキップ") ends the scene at once, on to the next; after the last one a fade out.
 *   seqTgTutorialStart (clTgTutorialStart): fade in; ANM 0x300 menu_p_tutorial_bunsin00
 *     frames 0..0x77 once, then 0x77 + 0x3c looped; a counter of 0x708 (1800 frames, 30 s)
 *     ends it, or the accelerator once past frame 0x77; fade out.
 *   ... the tutorial race (SConfigPlay +0xd1, 2.8 km: car.c's P.tg.tutorial) ...
 *  after it (isTargetFirstTutorial):
 *   seqTgTutorialDressupInfo (clTgTutorialDressupInfo): the screen goes black (fade at twice
 *     the speed); ANM 0x307 menu_p_tutorial_bunsin09 (0x308 _bunsin09_r35 when the card's car
 *     isR35) frames 0..0x18 once, then 0x18 + 0x46 looped; 1800 frames end it, or the
 *     accelerator once past 0x18, which plays 0x5f + 10 (the card leaving) first; fade.
 *   seqTgTutorialEnd (clTgTutorialEnd): ANM 0x301 menu_p_tutorial_bunsin00a 0..0x3b once,
 *     then 0x3b + 0x46 looped, 1800 frames or the accelerator past 0x3b; fade out.
 *   then setTargetFirstPlay(0), setTargetFirstTutorial(0), the area select reset to 7, BGM 0x5f.
 *  seqTgTutorialDressUp (clSeqTgTutorialDressUpThread): fade in; ANM 0x2b6
 *   menu_layout_tutorial_dressup once, whole (600 frames), the accelerator ends it; fade out.
 *   (In the retail flow nothing calls it: only a pointer to it, the debug menu's; the port
 *   shows it the first time the dress-up menu opens.)
 *
 * The port has no tutorial race on the way (it may: car.c's P.tg.tutorial), so TUT_TG plays
 * the intro (game_1, game_2), then the parts card (bunsin09) and the closing words (bunsin00a,
 * "では分身対戦を始めてみましょう"), and the ghost battle's menus follow. TUT_TG_START (bunsin00,
 * "ここからはあなたが操作します") is there for a tutorial race before it.
 *
 * Use: tut_open(kind, r35) -> 1 if it is on; then each 60 Hz tick tut_step(tap || key) (it takes
 * all the input while tut_active()) until it returns 1 (done: go on to what was due), and
 * tut_draw() last, over the screen. The per-save "seen" bits (TUT_SEEN_*) are the app's to keep. */

enum { TUT_TG, TUT_TG_INTRO, TUT_TG_START, TUT_TG_INFO, TUT_TG_END, TUT_DRESS };
#define TUT_SEEN_TG 1u
#define TUT_SEEN_DRESS 2u

#define TUT_FADE 10     /* frames of a fade in or out (the port's TG_FADE; nsMenuFade::speed_ 1/6) */
#define TUT_TIMEOUT 1800 /* the classes' counter, 0x708 */
#define TUT_MAX_SCENES 4

/* one clSpriteAnime phase list: intro (start .. +len once), then loop (+loop_len) till the
 * counter or the accelerator (past the intro), then the exit part (exit_len 0: none); loop_len 0
 * = the whole animation once, the accelerator ending it */
typedef struct {
  const char *name;
  int len, loop_len, exit_start, exit_len;
  int group_end; /* a fade out (and in, before the next) after this scene */
} tut_scene;

static const tut_scene TUT_GAME_1 = { "menu_layout_tutorial_game_1", 0, 0, 0, 0, 0 };
static const tut_scene TUT_GAME_2 = { "menu_layout_tutorial_game_2", 0, 0, 0, 0, 1 };
static const tut_scene TUT_START = { "menu_p_tutorial_bunsin00", 0x77, 0x3c, 0, 0, 1 };
static const tut_scene TUT_INFO = { "menu_p_tutorial_bunsin09", 0x18, 0x46, 0x5f, 10, 1 };
static const tut_scene TUT_INFO_R35 = { "menu_p_tutorial_bunsin09_r35", 0x18, 0x46, 0x5f, 10, 1 };
static const tut_scene TUT_END = { "menu_p_tutorial_bunsin00a", 0x3b, 0x46, 0, 0, 1 };
static const tut_scene TUT_DRESSUP = { "menu_layout_tutorial_dressup", 0, 0, 0, 0, 1 };

enum { TUT_PH_IN, TUT_PH_INTRO, TUT_PH_LOOP, TUT_PH_EXIT, TUT_PH_OUT };

static struct {
  int on;
  int n, k;          /* the scenes, the one up */
  const tut_scene *sc[TUT_MAX_SCENES];
  int anim[TUT_MAX_SCENES];
  int phase, t;      /* the phase and its frame */
  int count;         /* the 1800-frame counter */
  int frame;         /* the frame drawn */
} TUT;

/* show it? the arcade's condition for the ghost battle: no ghost battle played yet on this card
 * (+0x1a4 0: the port's tg_plays) and not seen; the dress-up's: not seen */
static int tut_should_show(unsigned seen, int kind, int tg_plays) {
  if (kind == TUT_DRESS) return !(seen & TUT_SEEN_DRESS);
  return !(seen & TUT_SEEN_TG) && tg_plays == 0;
}

static int tut_active(void) { return TUT.on; }
/* the ghost battle's (not the dress-up's): BGM 0x29 under it */
static int tut_ghost(void) { return TUT.on && TUT.n && TUT.sc[0] != &TUT_DRESSUP; }

static void tut_scene_begin(void) {
  TUT.phase = TUT.k == 0 || TUT.sc[TUT.k - 1]->group_end ? TUT_PH_IN : TUT_PH_INTRO;
  TUT.t = 0;
  TUT.count = TUT_TIMEOUT;
  TUT.frame = 0;
}

static int tut_open(int kind, int r35) {
  const tut_scene *list[TUT_MAX_SCENES];
  int n = 0;
  const tut_scene *info = r35 ? &TUT_INFO_R35 : &TUT_INFO;
  switch (kind) {
  case TUT_TG: list[n++] = &TUT_GAME_1, list[n++] = &TUT_GAME_2, list[n++] = info, list[n++] = &TUT_END; break;
  case TUT_TG_INTRO: list[n++] = &TUT_GAME_1, list[n++] = &TUT_GAME_2; break;
  case TUT_TG_START: list[n++] = &TUT_START; break;
  case TUT_TG_INFO: list[n++] = info; break;
  case TUT_TG_END: list[n++] = &TUT_END; break;
  case TUT_DRESS: list[n++] = &TUT_DRESSUP; break;
  default: return 0;
  }
  memset(&TUT, 0, sizeof TUT);
  for (int i = 0; i < n; ++i) {
    int a = anm_find(list[i]->name);
    if (a < 0 && list[i] == &TUT_INFO_R35) a = anm_find(TUT_INFO.name), list[i] = &TUT_INFO;
    if (a < 0 || anm_frames(a) <= 0) continue; /* (extra.bin without it: left out) */
    TUT.sc[TUT.n] = list[i];
    TUT.anim[TUT.n++] = a;
  }
  if (!TUT.n) return 0;
  TUT.on = 1;
  TUT.k = 0;
  tut_scene_begin();
  return 1;
}

/* the next scene, or the end; 1 when it is all done */
static int tut_next(void) {
  if (++TUT.k >= TUT.n) {
    TUT.on = 0;
    return 1;
  }
  tut_scene_begin();
  return 0;
}

/* one 60 Hz tick; skip = the accelerator (a tap or OK). 1 the frame it is done. */
static int tut_step(int skip) {
  if (!TUT.on) return 0;
  const tut_scene *s = TUT.sc[TUT.k];
  int a = TUT.anim[TUT.k], frames = anm_frames(a);
  int whole = s->loop_len == 0;
  int last = TUT.k + 1 >= TUT.n || s->group_end;
  TUT.t++;
  switch (TUT.phase) {
  case TUT_PH_IN: /* (the fade: input off, clMenuInput::setEnable(0)) */
    TUT.frame = 0;
    if (TUT.t >= TUT_FADE) TUT.phase = TUT_PH_INTRO, TUT.t = 0;
    return 0;
  case TUT_PH_INTRO: {
    int len = whole ? frames : s->len;
    TUT.frame = TUT.t < len ? TUT.t : len - 1;
    if (!whole && --TUT.count <= 0) break;
    if (whole && skip) break; /* (clSeqTgTutorialThread: chkSW at once, no wait) */
    if (TUT.t >= len) {
      if (whole) break;
      TUT.phase = TUT_PH_LOOP, TUT.t = 0;
    }
    return 0;
  }
  case TUT_PH_LOOP:
    TUT.frame = s->len + TUT.t % s->loop_len;
    if (TUT.frame >= frames) TUT.frame = frames - 1;
    if (--TUT.count <= 0) break; /* (the counter: no exit part) */
    if (skip) {
      snd_menu(SND_SELECT);
      if (s->exit_len) {
        TUT.phase = TUT_PH_EXIT, TUT.t = 0;
        return 0;
      }
      break;
    }
    return 0;
  case TUT_PH_EXIT:
    TUT.frame = s->exit_start + (TUT.t < s->exit_len ? TUT.t : s->exit_len - 1);
    if (TUT.frame >= frames) TUT.frame = frames - 1;
    if (TUT.t >= s->exit_len) break;
    return 0;
  case TUT_PH_OUT:
    if (TUT.t >= TUT_FADE) return tut_next();
    return 0;
  }
  /* the scene's end: a fade out at a group's end, else straight on to the next */
  if (last) {
    TUT.phase = TUT_PH_OUT, TUT.t = 0;
    return 0;
  }
  return tut_next();
}

/* over everything: black under the animation (the classes' clMenuFade curtain), its frame, the
 * fade */
static void tut_draw(void) {
  if (!TUT.on) return;
  anm_fill(0, 0, 0, 1);
  float shade = 0;
  if (TUT.phase == TUT_PH_IN) shade = 1.0f - (float)TUT.t / TUT_FADE;
  if (TUT.phase == TUT_PH_OUT) shade = (float)TUT.t / TUT_FADE;
  if (shade < 0) shade = 0;
  if (shade > 1) shade = 1;
  anm_draw(TUT.anim[TUT.k], TUT.frame, 1);
  if (shade > 0) anm_fill(0, 0, 0, shade);
}
