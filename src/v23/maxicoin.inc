/* (v2.3) The MAXI coin screens. menu.c includes this once, after draw_crown (it uses menu.c's own
 * statics: go, spr, cf_text, snd_effect, tg_due, game_slot, FADE). See maxicoin.HOOKS.md.
 *
 * The arcade (main, 32-bit ELF):
 * - Coins given: clRaceThread::finiTarget (0x88ff7dc..; setMaxiCoin call 0x8900cd0): a ghost
 *   battle (the arcade's ターゲット, clTargetData) WON and not retired adds n to the card's
 *   getMaxiCoin, clamped to 0..999999 (0xf423f): n = 1 (0x8900bae / 0x8901510); a revenge
 *   (clTargetData::hasRevenge with points, 0x89021e6) 2; the beaten ghost holds the crown of the
 *   race's area (hasCrown(area), 0x8901596 -> 0x890229d) 3, and holding all eight areas'
 *   crowns (hasCrown 0..7, 0x89023c4..0x8902478) 10. A loss 0 (0x8901f86).
 * - Shown: mseqTarget (0x88ce758), after seqRaceTarget and the crown taken (hasNewCrown ->
 *   seqTgCrownGet), not retired: getCard().getMaxiCoin() != getBeforeRaceCard().getMaxiCoin()
 *   (0x88cefd9..0x88cf010) -> seqTgMaxiCoinGet (0x888e9d0), before the dress-up GETs.
 * - clSeqTgMaxiCoinGetThread (ctor 0x888d3b0, run 0x888ace8, yield 0x8889dd0, drawHaveCoin
 *   0x888950c): setAnime ANM 0xd8 menu_g_mx_coin_finish_1 (the coin: menu_p_mx_coin_L_1 with
 *   menu_p_flash, 160 frames), 0x201 menu_layout_mx_coin_logo (the car's logo:
 *   nsMenuCheck::getCarLogoSprite over 0x1595), 0x203 menu_layout_name_bar (the card's name,
 *   class and title in clSpriteFonts hung on the templates 0x58b / 0x37d / 0x476 / 0x477), the
 *   car (clMenuCar with its aura) and clMenuCoinManager's 3D coins; drawHaveCoin splits the
 *   count by 10000 / 1000 / 100 / 1 into small coins (table 0x940a360: menu_p_mx_coin_10000 ..
 *   _1 = 3227, 2828, 2320, 1559); the run ends at 600 frames (0x258) or a key after it settles;
 *   SE 0x38 at the end. (ANM table: 0x954fb68, 8 bytes an entry, name pointer first.)
 * - seqMaxiCoinProud (0x87af23c, clSeqMaxiCoinProudThread ctor 0x87acff0, drawCoin 0x87ac98c):
 *   the attract's "maxi coin 自慢": mseqDemo (0x88c9f60) runs it only with clNet::getConnectCount
 *   > 1 (a linked cabinet: 0x88c9f53..0x88c9f59), a card's car (clMaxiCoinRankingDataManager,
 *   the store's maxi coin ranking) with menu_layout_vs_star_title (ANM 0x2bf), the coin
 *   (0xd8), the name bar (0x203) and the logo (0x201). A port with no link: shown in the attract
 *   with the save's car that holds the most coins (maxicoin.HOOKS.md). */

#define MXC_LEN 600       /* the run's end (0x258) */
#define MXC_COIN_AT 30    /* the coin's animation from here (its 160 frames) */
#define MXC_HAVE_AT 150   /* the count of coins held from here, counting up MXC_COUNT frames */
#define MXC_COUNT 40
#define MXC_TAP_AT 200    /* a tap from here on: on at once */
#define MXP_LEN 420       /* the attract's page */

/* drawHaveCoin's coins by denomination: 10000, 1000, 100, 1 */
static const int MXC_SMALL[4] = { 3227, 2828, 2320, 1559 };
static const int MXC_DENOM[4] = { 10000, 1000, 100, 1 };
static struct {
  int bg, maxi_bg, finish[4], keta, logo, title, car, name_bar, star_title;
} MXA = { -2 };

static void mxc_anims(void) {
  if (MXA.bg != -2) return;
  static const char *FIN[4] = { "menu_g_mx_coin_finish_10000", "menu_g_mx_coin_finish_1000", "menu_g_mx_coin_finish_100",
                                "menu_g_mx_coin_finish_1" };
  MXA.bg = anm_find("menu_g_mx_coin_bg"), MXA.maxi_bg = anm_find("menu_g_maxi_bg");
  for (int k = 0; k < 4; ++k) MXA.finish[k] = anm_find(FIN[k]);
  MXA.keta = anm_find("menu_g_mx_coin_keta_b_1");
  MXA.logo = anm_find("menu_layout_mx_coin_logo"), MXA.title = anm_find("menu_layout_mx_coin_title");
  MXA.car = anm_find("menu_layout_mx_coin_car"), MXA.name_bar = anm_find("menu_layout_name_bar");
  MXA.star_title = anm_find("menu_layout_vs_star_title");
}

/* the anim's frame t, the last held after its end */
static int mxc_frame(int anim, int t) {
  int n = anim >= 0 ? anm_frames(anim) : 0;
  if (n <= 0) return 0;
  return t < 0 ? 0 : t < n ? t : n - 1;
}

/* the biggest denomination in n (the coin the finish shows): 0 10000 .. 3 1 */
static int mxc_denom(int n) {
  for (int k = 0; k < 3; ++k)
    if (n >= MXC_DENOM[k]) return k;
  return 3;
}

/* the count as drawHaveCoin has it: per denomination its small coins in a row, the rows from
 * the 10000s down, at (x, y) on the 640x480 screen; then the number under them */
static void mxc_draw_have(int count, float x, float y, float a) {
  int left = count < 0 ? 0 : count > 999999 ? 999999 : count;
  float yy = y;
  for (int k = 0; k < 4; ++k) {
    int n = left / MXC_DENOM[k]; /* (the 1s: the hundreds' remainder, < 100) */
    left -= n * MXC_DENOM[k];
    if (!n) continue;
    int s = spr(MXC_SMALL[k]);
    float w = anm_sprite_width(s), h = anm_sprite_height(s);
    if (w <= 0) continue;
    /* a stack every ten, side by side */
    int shown = n > 50 ? 50 : n;
    for (int i = 0; i < shown; ++i) anm_draw_sprite(s, x + (i / 10) * (w + 2), yy - (i % 10) * h * 0.18f, a);
    x += ((shown + 9) / 10) * (w + 2) + 6;
  }
  char num[32];
  snprintf(num, sizeof num, "%d", count < 0 ? 0 : count);
  cf_text(1, num, 400, y + 40, a);
}

static int mxc_held(const menu_state *m) {
  if (m->maker < 0 || m->car < 0) return 0;
  return m->maxi_coin[m->maker][m->car];
}

/* ---- the coins got (MENU_MXC_GET, seqTgMaxiCoinGet) ---- */
static void mxc_open(menu_state *m) {
  mxc_anims();
  go(m, MENU_MXC_GET);
  m->bars = 0;
  m->music = 1;
}

static void mxc_step(menu_state *m, int tap) {
  int t = m->t;
  if (t == MXC_COIN_AT) snd_effect("win_bigstar", 1.0f);
  if (t == MXC_HAVE_AT) snd_effect("parts_equip", 1.0f);
  if (t >= MXC_LEN + FADE || (tap && t > MXC_TAP_AT)) {
    snd_effect("levelup", 1.0f); /* (SE 0x38 at the end) */
    m->mxc_add = 0;
    if (!tg_due(m)) {
      go(m, MENU_GAME);
      m->sel = game_slot(MENU_GAME_TG);
      m->slide = m->card_t = 0;
      m->bars = 1;
    }
  }
}

/* (v2.3) the car's logo (0x201: setImageReplace(0x1595, nsMenuCheck::getCarLogoSprite)) and the
 * name bar (0x203) at its frame f filled as the ctor (0x888d5c1..0x888dd25) fills its
 * clSpriteFontCluster: [0] font 8 on 0x58b the card's getName; [1] font 3 on 0x37d getClassStr
 * (made full width) then the title (clEffTitleType); [2] on 0x476 L"" (nothing); [3] on 0x477
 * the power (an R35's getStepHandleStr, else getPowerHandleStr). Here in the card's glyphs as
 * the stars screen (draw_vs_star) has the same templates: the name in font 5's, the class's
 * "B7級　/" and the title's glyphs after it, the horsepower; the templates themselves hidden
 * (they showed as their placeholder pictures: the name wrong) */
static void mxc_name_bar(const menu_state *m, int mk, int cr, int f) {
  if (mk < 0 || mk >= CARDB_ALL || cr < 0 || cr >= CARDB_NCARS[mk]) {
    if (MXA.logo >= 0) anm_draw(MXA.logo, 0, 1);
    if (MXA.name_bar >= 0) anm_draw(MXA.name_bar, f, 1);
    return;
  }
  const cardb_car *c = &CARDB_CARS[mk][cr];
  if (MXA.logo >= 0) {
    anm_swap logo = { anm_sprite(T_STAR_LOGO), anm_sprite(c->logo) };
    anm_draw_ex(MXA.logo, 0, 1, 0, 0, &logo, 1);
  }
  if (MXA.name_bar < 0) return;
  anm_swap sw[4] = { { anm_sprite(T_STAR_NAME), -1 }, { anm_sprite(T_STAR_CLASS), -1 }, { anm_sprite(T_STAR_TITLE), -1 },
                     { anm_sprite(T_STAR_POWER), -1 } };
  anm_draw_ex(MXA.name_bar, f, 1, 0, 0, sw, 4);
  float r[4];
  /* the name (draw_vs_star's: font 5's glyphs at 0.8, 22 px apart) */
  if (anm_sprite_rect(MXA.name_bar, f, anm_sprite(T_STAR_NAME), r) && anm_sprite(NAME_GLYPH5) >= 0) {
    float al = anm_sprite_alpha(MXA.name_bar, f, anm_sprite(T_STAR_NAME));
    for (int i = 0; i < 5 && m->name[i] >= 0; ++i) {
      int s = anm_sprite(NAME_GLYPH5 + m->name[i]);
      if (s < 0) continue;
      float src[4] = { 0, 0, anm_sprite_width(s), anm_sprite_height(s) };
      float dst[4] = { r[0] + 22.0f * (float)i, r[1], r[0] + 22.0f * (float)i + 0.8f * src[2], r[1] + 0.8f * src[3] };
      anm_draw_part(s, src, dst, al);
    }
  }
  /* the class, then the title on the same line (one string in the arcade) */
  if (anm_sprite_rect(MXA.name_bar, f, anm_sprite(T_STAR_CLASS), r)) {
    float al = anm_sprite_alpha(MXA.name_bar, f, anm_sprite(T_STAR_CLASS)), x = r[0];
    int pre = anm_sprite(CARD_CLASSP + m->class_car[mk][cr]);
    if (pre >= 0) {
      float src[4] = { 0, 0, anm_sprite_width(pre), anm_sprite_height(pre) };
      float dst[4] = { r[0], r[1], r[0] + 0.72f * src[2], r[1] + 0.72f * src[3] };
      anm_draw_part(pre, src, dst, al);
      x += 0.72f * (src[2] - 32.0f + 16.0f); /* (draw_card's advance past the '/') */
    }
    int title = m->title_car[mk][cr] > 0 ? m->title_car[mk][cr] : TITLE_FIRST;
    const unsigned short *g;
    int n = title_glyphs(title, &g);
    for (int i = 0; i < n; ++i) {
      int s = anm_sprite(TITLE_GLYPH1 + g[i]);
      float src[4] = { 0, 0, 20, 20 }, dst[4] = { x, r[1], x + 20, r[1] + 20 };
      if (s >= 0) anm_draw_part(s, src, dst, al);
      x += title_glyph_half(g[i]) ? 8.0f : 16.0f;
    }
  }
  if (anm_sprite_rect(MXA.name_bar, f, anm_sprite(T_STAR_POWER), r)) {
    float al = anm_sprite_alpha(MXA.name_bar, f, anm_sprite(T_STAR_POWER));
    char text[16];
    int power = m->tune_full[mk][cr] ? m->tune_power[mk][cr] : m->st_power[mk][cr];
    snprintf(text, sizeof text, "%d", car_tune_hp(c->hp, power));
    float x = card_glyphs(CARD_GLYPH2, "-,", 8.0f, text, r[0], r[1], al);
    anm_draw_sprite(anm_sprite(CARD_TEXT + C_HP), x, r[1], al);
  }
}

static void draw_mxc_get(const menu_state *m) {
  int t = m->t;
  float a = t < FADE ? (float)(t + 1) / FADE : t > MXC_LEN ? 1.0f - (float)(t - MXC_LEN) / FADE : 1.0f;
  if (a < 0) a = 0;
  anm_fill(0, 0, 0, 1);
  if (MXA.bg >= 0) anm_draw(MXA.bg, 0, 1);
  if (MXA.maxi_bg >= 0) anm_draw(MXA.maxi_bg, 0, 1);
  if (MXA.title >= 0) anm_draw(MXA.title, mxc_frame(MXA.title, t), 1);
  /* (v2.3) the card's car's logo and the name bar filled (they showed their templates) */
  mxc_name_bar(m, m->maker, m->car, MXA.name_bar >= 0 ? mxc_frame(MXA.name_bar, t) : 0);
  /* the coin got: its denomination's finish (the 1 coin's for n < 100) */
  int add = m->mxc_add > 0 ? m->mxc_add : 1;
  int fin = MXA.finish[mxc_denom(add)];
  if (fin < 0) fin = MXA.finish[3];
  if (fin >= 0 && t >= MXC_COIN_AT) anm_draw(fin, mxc_frame(fin, t - MXC_COIN_AT), 1);
  if (t >= MXC_COIN_AT + 20) {
    char s[24];
    snprintf(s, sizeof s, "+%d", add);
    cf_text(1, s, 300, 300, 1);
  }
  /* the coins held, counted up from before the race */
  if (t >= MXC_HAVE_AT) {
    int to = mxc_held(m), from = to - m->mxc_add;
    int k = t - MXC_HAVE_AT;
    int shown = k >= MXC_COUNT ? to : from + (int)((long long)(to - from) * k / MXC_COUNT);
    mxc_draw_have(shown, 80, 380, 1);
  }
  anm_fill(0, 0, 0, 1.0f - a);
}

/* ---- the attract's maxi coin 自慢 (MENU_MXC_PROUD, seqMaxiCoinProud): m->mxp_coins the
 * coins of the car shown (the app fills it with the save's most; 0: the page is skipped) ---- */
static void mxp_open(menu_state *m) {
  mxc_anims();
  go(m, MENU_MXC_PROUD);
  m->bars = 0;
}

/* 1 when the page is over (the attract goes on) */
static int mxp_step(menu_state *m, int tap) {
  return m->t >= MXP_LEN + FADE || tap || m->mxp_coins <= 0;
}

static void draw_mxc_proud(const menu_state *m) {
  int t = m->t;
  float a = t < FADE ? (float)(t + 1) / FADE : t > MXP_LEN ? 1.0f - (float)(t - MXP_LEN) / FADE : 1.0f;
  if (a < 0) a = 0;
  anm_fill(0, 0, 0, 1);
  if (MXA.bg >= 0) anm_draw(MXA.bg, 0, 1);
  if (MXA.maxi_bg >= 0) anm_draw(MXA.maxi_bg, 0, 1);
  if (MXA.star_title >= 0) anm_draw(MXA.star_title, mxc_frame(MXA.star_title, t), 1);
  if (MXA.car >= 0) anm_draw(MXA.car, 0, 1);
  if (MXA.logo >= 0) anm_draw(MXA.logo, 0, 1);
  if (MXA.name_bar >= 0) anm_draw(MXA.name_bar, mxc_frame(MXA.name_bar, t), 1);
  int fin = MXA.finish[mxc_denom(m->mxp_coins)];
  if (fin >= 0) anm_draw(fin, mxc_frame(fin, t), 1);
  if (t >= 60) mxc_draw_have(m->mxp_coins, 80, 380, 1);
  anm_fill(0, 0, 0, 1.0f - a);
}

/* the coins a won ghost battle gives (clRaceThread::finiTarget): revenge 2, the area's crown
 * holder beaten 3, a ghost with all eight crowns 10, else 1 */
int menu_mxc_award(int won, int revenge, int area_crown, int all_crowns) {
  if (!won) return 0;
  if (revenge) return 2;
  if (area_crown) return all_crowns ? 10 : 3;
  return 1;
}
