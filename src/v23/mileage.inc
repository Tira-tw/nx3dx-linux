/* (v2.3) ---- the mileage after a race (MENU_MILEAGE: seqMileageResult 0x878920c,
 * clSeqMileageResultThread). mseqStory, mseqTimeAttack, mseqTen and mseqTarget all call it
 * right after seqLvUp and seqTitleSelect (mseqJoin: seqJoinMileageResult, the versus' own);
 * its clArg (0x8794560) takes the card's getOdoCount before the race (odo_total_) and after
 * less before (odo_plus_), in km.
 *
 * The screen (ctor 0x878782c, yield 0x87871dc): menu_layout_milage twice, the first with only
 * its bg (menu_p_milage_bg 0x615) shown, then the card's car turning (clMenuCar, environment 0,
 * setInterest((0, 16, 50), (0, 9, 0)), 0.3 degrees a frame back about y; with its aura,
 * nsMenuCheck::checkAura), then the second with everything else but the bg: the title bar, the
 * name (0x58b), class (0x37d), title (0x476) and power (0x477) as the versus stars' screen has
 * them, the labels 0x59f 今回の走行距離 and 0x5a0 総走行距離 (FONT_ID 4, wide strings 0x93f0f3c,
 * 0x93f0f5c: tools/result_sprites.py RESULT_TEXT_BASE + 0, + 1), the template car.png (0x6) and
 * the password bar (0x1a76, 0x478, 0x1a77: the internet ranking's password, none here) hidden.
 * setNo (0x8786970) puts the numbers in the templates, digits menu_p_milage_0..9 (0x93f0b60):
 * this play's km in 0x4c8 (ones) and 0xb07 (tens, if any); the total right to left in
 * 0x4d2, 0x4d1, 0x4d0, a comma (0xb07's sprite) in 0x4cf, 0x4ce, 0x4cd, 0x4cc, a comma in
 * 0x4cb, 0x4ca, 0x4c9, as far as the number goes.
 * run (0x87895a4): fade in, then every 5 frames one km off this play's onto the total with SE
 * 0x35 (odo_count) till none is left (a tap: all at once), then 300 frames or a tap, fade out.
 * (The card update notice, menu_g_milage_card_update, is the arcade's card renewal: none here.) ---- */
enum { T_MIL_BG = 0x615, T_MIL_BAR = 0x15d0, T_MIL_CAR = 0x6, T_MIL_NAME = 0x58b, T_MIL_CLASS = 0x37d,
       T_MIL_TITLE = 0x476, T_MIL_POWER = 0x477, T_MIL_LABEL_PLUS = 0x59f, T_MIL_LABEL_TOTAL = 0x5a0,
       T_MIL_COMMA = 0xb07, T_MIL_D0 = 0x4c8, T_MIL_A = 0x4d2, T_MIL_KM = 0x616, T_MIL_PASS_BAR = 0x1a76,
       T_MIL_PASS_FONT = 0x478, T_MIL_PASS_MES = 0x1a77 };
#define MIL_TEXT 126800 /* tools/result_sprites.py RESULT_TEXT_BASE: + 0 今回の走行距離, + 1 総走行距離 */
#define MIL_STEP 5      /* frames a km */
#define MIL_WAIT 300    /* frames shown once counted */
#define MIL_SPIN 0.3f   /* degrees a frame */

static struct {
  int anim;                 /* menu_layout_milage (-2: not looked up yet) */
  int due;                  /* armed by the race (menu_mileage_arm), shown on the way back */
  int plus, total;          /* km: this play's still to count, the total so far */
  int t, tick, wait, out;   /* frames on the screen, to the next km, waited, fading out */
  float spin;               /* the car's turn, degrees */
} MIL = { -2 };

/* the race's end (app.c): the card's mileage before the race and now, metres */
void menu_mileage_arm(menu_state *m, double odo0_m, double odo1_m) {
  (void)m;
  int before = odo0_m > 0 ? (int)(odo0_m / 1000.0) : 0, after = odo1_m > 0 ? (int)(odo1_m / 1000.0) : 0;
  if (after > 16777215) after = 16777215; /* (teBound 0..16777215) */
  /* (a story select's second race: title_odo0 is the first race's start already; a leftover arm
   * (a challenger's versus cut in) must not stretch it) */
  if (before > after) before = after;
  MIL.due = 1;
  MIL.total = before;
  MIL.plus = after - before;
}

static void mileage_open(menu_state *m) {
  if (MIL.anim == -2) MIL.anim = anm_find("menu_layout_milage");
  go(m, MENU_MILEAGE);
  MIL.due = 0;
  MIL.t = MIL.wait = MIL.out = 0;
  MIL.tick = 0;
  MIL.spin = 0;
  m->bars = 0;
  m->music = 1;
  m->tap = m->key = m->key_held = m->back = m->leaving = 0;
}

/* back_from_race: the mileage now if it is due (out of the black); 1 if it opened. The pause
 * menu's leave (white) and an extra.bin without the layout drop it. */
static int mileage_due(menu_state *m, int white) {
  if (!MIL.due) return 0;
  if (MIL.anim == -2) MIL.anim = anm_find("menu_layout_milage");
  if (white || MIL.anim < 0) {
    MIL.due = 0;
    return 0;
  }
  mileage_open(m);
  return 1;
}

static void mileage_step(menu_state *m, int tap, int key) {
  MIL.t++;
  MIL.spin -= MIL_SPIN;
  if (MIL.out) {
    if (++MIL.out > FADE) back_from_race(m, 0);
    return;
  }
  if (MIL.t < FADE) return; /* (the fade in: clMenuFade, input off) */
  int hit = tap || key;
  if (MIL.plus > 0) {
    if (hit) {
      MIL.total += MIL.plus, MIL.plus = 0;
      return;
    }
    if (--MIL.tick <= 0) {
      snd_effect("odo_count", 1.0f); /* SE 0x35 */
      MIL.plus--, MIL.total++;
      MIL.tick = MIL_STEP;
    }
    return;
  }
  if (hit || ++MIL.wait >= MIL_WAIT) MIL.out = 1;
}

/* setNo's swaps: n swaps written to sw (at most 12) */
static int mileage_digits(anm_swap *sw) {
  int n = 0, p = MIL.plus < 0 ? 0 : MIL.plus > 99 ? 99 : MIL.plus, v = MIL.total;
  /* this play's: ones in 0x4c8, tens in 0xb07 (hidden under 10) */
  sw[n++] = (anm_swap){ spr(T_MIL_COMMA), p >= 10 ? spr(T_MIL_D0 + p / 10 % 10) : -1 };
  sw[n++] = (anm_swap){ spr(T_MIL_D0), spr(T_MIL_D0 + p % 10) };
  /* the total, right to left in the slots s = 0x4c9 + s: ones 9, tens 8, hundreds 7, a comma
   * 6, thousands 5 .. 3, a comma 2, millions 1, 0 (shown: 1 a digit, 2 the comma) */
  int shown[10] = { 0 }, dig[10] = { 0 };
  for (int k = 0, x = v; k < 8; ++k, x /= 10) {
    if (k && !x) break;
    int s = k < 3 ? 9 - k : k < 6 ? 8 - k : 7 - k; /* ones 9, tens 8, hundreds 7, thousands 5 .. */
    shown[s] = 1, dig[s] = x % 10;
    if (k == 3) shown[6] = 2; /* the comma before the thousands */
    if (k == 6) shown[2] = 2; /* and before the millions */
  }
  for (int s = 0; s < 10; ++s) {
    int from = spr(T_MIL_D0 + 1 + s); /* 0x4c9 + s */
    int to = shown[s] == 1 ? spr(T_MIL_D0 + dig[s]) : shown[s] == 2 ? spr(T_MIL_COMMA) : -1;
    sw[n++] = (anm_swap){ from, to };
  }
  return n;
}

static void draw_mileage(const menu_state *m, void (*draw3d)(void *), void *ctx) {
  int t = MIL.t;
  float a = MIL.out ? 1.0f - (float)MIL.out / FADE : t < FADE ? (float)(t + 1) / FADE : 1.0f;
  if (a < 0) a = 0;
  anm_fill(0, 0, 0, 1);
  if (MIL.anim < 0) return;
  int f = t % (anm_frames(MIL.anim) > 0 ? anm_frames(MIL.anim) : 1);
  /* the first: the bg alone */
  static const int REST[] = { T_MIL_BAR, T_MIL_CAR, T_MIL_NAME, T_MIL_CLASS, T_MIL_TITLE, T_MIL_POWER,
                              T_MIL_LABEL_PLUS, T_MIL_LABEL_TOTAL, T_MIL_COMMA, T_MIL_D0, T_MIL_D0 + 1, T_MIL_D0 + 2,
                              T_MIL_D0 + 3, T_MIL_D0 + 4, T_MIL_D0 + 5, T_MIL_D0 + 6, T_MIL_D0 + 7, T_MIL_D0 + 8,
                              T_MIL_D0 + 9, T_MIL_A, T_MIL_KM, T_MIL_PASS_BAR, T_MIL_PASS_FONT, T_MIL_PASS_MES };
  enum { NREST = sizeof REST / sizeof REST[0] };
  anm_swap sw[NREST + 16];
  for (int i = 0; i < NREST; ++i) sw[i] = (anm_swap){ spr(REST[i]), -1 };
  anm_draw_ex(MIL.anim, f, 1, 0, 0, sw, NREST);
  /* the car */
  if (draw3d) draw3d(ctx);
  /* the second: all but the bg, the template car and the password; the numbers and labels in */
  int n = 0;
  sw[n++] = (anm_swap){ spr(T_MIL_BG), -1 };
  sw[n++] = (anm_swap){ spr(T_MIL_CAR), -1 };
  sw[n++] = (anm_swap){ spr(T_MIL_PASS_BAR), -1 };
  sw[n++] = (anm_swap){ spr(T_MIL_PASS_FONT), -1 };
  sw[n++] = (anm_swap){ spr(T_MIL_PASS_MES), -1 };
  sw[n++] = (anm_swap){ spr(T_MIL_NAME), -1 };
  sw[n++] = (anm_swap){ spr(T_MIL_CLASS), -1 };
  sw[n++] = (anm_swap){ spr(T_MIL_TITLE), -1 };
  sw[n++] = (anm_swap){ spr(T_MIL_POWER), -1 };
  sw[n++] = (anm_swap){ spr(T_MIL_LABEL_PLUS), spr(MIL_TEXT + 0) };
  sw[n++] = (anm_swap){ spr(T_MIL_LABEL_TOTAL), spr(MIL_TEXT + 1) };
  n += mileage_digits(sw + n);
  anm_draw_ex(MIL.anim, f, 1, 0, 0, sw, n);
  /* the name, class, title and power on their templates (the versus stars' way) */
  int mk = m->maker, cr = m->car;
  const cardb_car *c = &CARDB_CARS[mk][cr];
  float r[4];
  if (anm_sprite_rect(MIL.anim, f, spr(T_MIL_NAME), r) && spr(NAME_GLYPH5) >= 0)
    for (int i = 0; i < 5 && m->name[i] >= 0; ++i) {
      int s = spr(NAME_GLYPH5 + m->name[i]);
      if (s < 0) continue;
      float src[4] = { 0, 0, anm_sprite_width(s), anm_sprite_height(s) };
      float dst[4] = { r[0] + 22.0f * (float)i, r[1], r[0] + 22.0f * (float)i + 0.8f * src[2], r[1] + 0.8f * src[3] };
      anm_draw_part(s, src, dst, 1);
    }
  if (anm_sprite_rect(MIL.anim, f, spr(T_MIL_CLASS), r)) {
    int pre = spr(CARD_CLASSP + m->class_car[mk][cr]);
    if (pre >= 0) {
      float src[4] = { 0, 0, anm_sprite_width(pre), anm_sprite_height(pre) };
      float dst[4] = { r[0], r[1], r[0] + 0.72f * src[2], r[1] + 0.72f * src[3] };
      anm_draw_part(pre, src, dst, 1);
    }
  }
  if (anm_sprite_rect(MIL.anim, f, spr(T_MIL_TITLE), r)) {
    int title = m->title_car[mk][cr] > 0 ? m->title_car[mk][cr] : TITLE_FIRST;
    const unsigned short *g;
    int ng = title_glyphs(title, &g);
    float x = r[0];
    for (int i = 0; i < ng; ++i) {
      int s = spr(TITLE_GLYPH1 + g[i]);
      float src[4] = { 0, 0, 20, 20 }, dst[4] = { x, r[1], x + 20, r[1] + 20 };
      if (s >= 0) anm_draw_part(s, src, dst, 1);
      x += title_glyph_half(g[i]) ? 8.0f : 16.0f;
    }
  }
  if (anm_sprite_rect(MIL.anim, f, spr(T_MIL_POWER), r)) {
    char text[16];
    int power = m->tune_full[mk][cr] ? m->tune_power[mk][cr] : m->st_power[mk][cr];
    snprintf(text, sizeof text, "%d", car_tune_hp(c->hp, power));
    float x = card_glyphs(CARD_GLYPH2, "-,", 8.0f, text, r[0], r[1], 1);
    anm_draw_sprite(spr(CARD_TEXT + C_HP), x, r[1], 1);
  }
  anm_fill(0, 0, 0, 1.0f - a);
}

/* menu_scene3d's: the card's car at the origin turning, environment 0, the default projection
 * (73 degrees), setInterest((0, 16, 50), (0, 9, 0)) (the ctor's second, 0x97feabc / 0x97feab0) */
static int mileage_scene3d(const menu_state *m, menu_scene *s) {
  s->env = 0;
  s->fov_x = 73.0f * DEG;
  s->eye[0] = 0, s->eye[1] = 16.0f, s->eye[2] = 50.0f;
  s->at[0] = 0, s->at[1] = 9.0f, s->at[2] = 0;
  const float pos[3] = { 0, 0, 0 };
  mat4 mm = mat4_body(pos, MIL.spin * DEG, 0, 0);
  const cardb_color *c = &chosen_car(m, m->car)->colors[m->color];
  s->cars[0].pack = c->pack;
  s->cars[0].paint = c->paint;
  s->cars[0].aero = m->aero[m->maker][m->car];
  tg_wear(m, &s->cars[0]); /* (its parts and, aura_screen with MENU_MILEAGE, its aura) */
  for (int e = 0; e < 16; ++e) s->cars[0].model[e] = mm.m[e];
  s->n = 1;
  return 1;
}
