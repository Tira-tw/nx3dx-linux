/* (v2.4) the HP BATTLE's gauges (VS_MODE_HP, v24/hp.h): this machine's on top, then each link
 * rival's (who 1..3, the versus boxes' order), at the bottom centre of the screen, clear of the
 * consoles (the meters right, the shifter left, the time and the boxes up top). Each row: the
 * player's name (the plate's name font: the menus' font 5 glyphs, PLATE_NAME_GLYPH) and ten
 * cells of the car setting's tune gauge (menu_g_setting_gage_1, lit as the menus light them:
 * frames 1..59 looping; its waku_2 empty), the cells' 200-frame blocks by how much is left;
 * under a quarter the lit cells flash; out: the cells empty and greyed, a red "KO" over them.
 * Included by hud.c (after the versus boxes); hud_draw calls draw_hp_gauges. */
#include "v24/hp.h"

enum { HPG_CELLS = 10 };

static struct {
  int init, gage, waku;
  float ox, oy, cw;  /* the empty cell's top-left and width in its anime (the menus' 640 units) */
  int glyph_k, glyph_o; /* NAMEDB indices of K and O (-1 none) */
  unsigned tick;
} HPG;

static int hpg_glyph(unsigned short a, unsigned short full) {
  for (int i = 0; i < NAMEDB_N; ++i)
    if (NAMEDB_WCHAR[i] == a) return i;
  for (int i = 0; i < NAMEDB_N; ++i)
    if (NAMEDB_WCHAR[i] == full) return i;
  return -1;
}

static void hpg_init(void) {
  HPG.init = 1;
  HPG.gage = anm_find("menu_g_setting_gage_1");
  HPG.waku = anm_find("menu_g_setting_gage_waku_2");
  HPG.cw = 19.0f; /* (the menus' cell step) */
  int s[4];
  float r[4];
  if (HPG.waku >= 0 && anm_sprites(HPG.waku, s, 4) > 0 && anm_sprite_rect(HPG.waku, 0, s[0], r))
    HPG.ox = r[0], HPG.oy = r[1], HPG.cw = r[2] - r[0] > 4.0f ? r[2] - r[0] : 19.0f;
  else HPG.waku = -1;
  HPG.glyph_k = hpg_glyph('K', 0xff2b);
  HPG.glyph_o = hpg_glyph('O', 0xff2f);
}

/* name-font glyphs (cells of 32 px) drawn `out` points high from (x, y) points, coloured */
static float hpg_text(const short *name, int n, float x, float y, float out, const float col[4]) {
  const float k = 1.0f / 1.6f, adv_h = out * 0.4f, adv_f = out * 0.8f;
  static const float add[4] = { 0, 0, 0, 0 };
  float shadow[4] = { 0, 0, 0, 0.7f * col[3] * CUR_A };
  float mul[4] = { col[0], col[1], col[2], col[3] * CUR_A };
  for (int pass = 0; pass < 2; ++pass) {
    float cx = x, o = pass ? 0.0f : 1.5f;
    for (int i = 0; i < n; ++i) {
      if (name[i] < 0 || name[i] >= NAMEDB_N) continue;
      int s = anm_sprite(PLATE_NAME_GLYPH + name[i]);
      float src[4] = { 0, 0, 32, 32 };
      float dst[4] = { (cx + o) * k, (y + o) * k, (cx + o + out) * k, (y + o + out) * k };
      if (s >= 0) anm_draw_part_col(s, src, dst, pass ? mul : shadow, add);
      cx += NAMEDB_WCHAR[name[i]] <= 0x7f ? adv_h : adv_f;
    }
    if (pass) return cx;
  }
  return x;
}

/* one row: the name right-aligned to x_gauge - 8, the cells from x_gauge (points) */
static void hpg_row(const short *name, int n, int who, float xg, float y, float zoom) {
  float hp = hp_of(who);
  int out = hp_out(who);
  if (hp < 0) hp = 0;
  if (hp > 1) hp = 1;
  /* the name: its width first (half / full advances), white, this machine's a touch yellow */
  const float th = 16.0f;
  float w = 0;
  for (int i = 0; i < n; ++i)
    if (name[i] >= 0 && name[i] < NAMEDB_N) w += NAMEDB_WCHAR[name[i]] <= 0x7f ? th * 0.4f : th * 0.8f;
  const float white[4] = { 1, 1, 1, 1 }, mine[4] = { 1, 0.92f, 0.45f, 1 }, grey[4] = { 0.55f, 0.55f, 0.55f, 1 };
  hpg_text(name, n, xg - 8.0f - w, y + 1.0f, th, out ? grey : who ? white : mine);
  /* the cells: lit count, colour block by what's left, flashing when low */
  int lit = out ? 0 : (int)ceilf(hp * HPG_CELLS - 0.001f);
  if (!out && hp > 0 && lit < 1) lit = 1;
  int nf = anm_frames(HPG.gage), base = hp > 0.5f ? 0 : hp > 0.25f ? 200 : 400;
  if (base + 60 > nf) base = nf >= 260 ? 200 : 0;
  int frame = base + 1 + (int)(HPG.tick % 59u);
  if (frame >= nf) frame = nf - 1;
  float lit_a = 1.0f;
  if (!out && hp < 0.25f) lit_a = (HPG.tick / 8u) % 2u ? 0.3f : 1.0f; /* low: flashing */
  const float step = HPG.cw * zoom, gx = xg / 1.6f - HPG.ox * zoom, gy = y / 1.6f - HPG.oy * zoom;
  for (int i = 0; i < HPG_CELLS; ++i) {
    float dx = gx + step * (float)i;
    if (i < lit && HPG.gage >= 0) anm_draw_xf(HPG.gage, frame, lit_a, dx, gy, zoom, NULL, 0);
    else anm_draw_xf(HPG.waku, 0, out ? 0.45f : 1.0f, dx, gy, zoom, NULL, 0);
  }
  /* out: a red KO over the middle of the gauge, bigger for its first second */
  if (out && HPG.glyph_k >= 0 && HPG.glyph_o >= 0) {
    short ko[2] = { (short)HPG.glyph_k, (short)HPG.glyph_o };
    float kh = 22.0f, kw = 2.0f * (NAMEDB_WCHAR[HPG.glyph_k] <= 0x7f ? kh * 0.4f : kh * 0.8f);
    float cx = xg + step * 1.6f * HPG_CELLS / 2.0f, cy = y + step * 1.6f * 0.5f;
    const float red[4] = { 1, 0.18f, 0.12f, 1 };
    hpg_text(ko, 2, cx - kw / 2.0f, cy - kh / 2.0f - 2.0f, kh, red);
  }
}

static void draw_hp_gauges(const hud_state *h, const car_state *car) {
  if (!hp_active()) return;
  if (!HPG.init) hpg_init();
  if (HPG.waku < 0) return;
  HPG.tick++;
  const car_race *r = &car->race;
  const float zoom = HUD_K / 1.6f * 0.9f, cell_pt = HPG.cw * zoom * 1.6f, row_h = cell_pt + 8.0f;
  int who[1 + CAR_RIVALS], n = 0;
  who[n++] = 0;
  for (int i = 0; i < vs_n && i < CAR_RIVALS; ++i)
    if (r->vs_on[i] || hp_out(i + 1)) who[n++] = i + 1;
  /* the block centred under the screen's middle (the names left of the gauges), its last row
   * 12 points over the bottom */
  float gauge_w = cell_pt * HPG_CELLS, xg = 512.0f - gauge_w / 2.0f + 40.0f;
  float y0 = h->screen_h - 12.0f - row_h * (float)n;
  for (int k = 0; k < n; ++k) {
    int w = who[k];
    if (w == 0) hpg_row(plate_name, plate_name_n, 0, xg, y0 + row_h * (float)k, zoom);
    else {
      short nm[5];
      int len = 0;
      for (int j = 0; j < 5 && vs_box[w - 1].name[j] >= 0 && vs_box[w - 1].name[j] < NAMEDB_N; ++j) nm[len++] = vs_box[w - 1].name[j];
      hpg_row(nm, len, w, xg, y0 + row_h * (float)k, zoom);
    }
  }
}
