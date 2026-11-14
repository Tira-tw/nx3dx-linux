#include "lights.h"
#include "neondb.h"
#include "tenline.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>

#include "gl.h"

/* nsStreetLightManager::stTextureParam (0x93d9ac0, 72 bytes a colour group: white, yellow,
 * orange, blue), its first half: the lamp's glint from streetlight.tga (image 0x127).
 * uv: left, top, right, bottom (GL's bottom-up v); w, h are HALF extents and ox, oy an offset
 * along the camera's right and up, in world units: clPrimitive::add (0x862366c) puts the
 * corners at p +- right * w +- up * h + right * ox + up * oy, right and up being the view
 * matrix's first two columns (clStreetLight::draw, 0x8623c91..0x8623da0). So the white glint
 * is 73 x 47.5, from 38 below the lamp to 9.4 above, its bright spot (row 63 of its cell) at
 * the lamp itself and its ray hanging down.
 * The record's second half (streettube.tga, image 0xd6, 16 x 16) is never drawn: the manager
 * loads the image (0x85fdcc2) but clStreetLight's ctor (0x8624ff9) sets up one primitive only,
 * from the first half, and draw adds every light to that one. */
typedef struct {
  float uv[4], w, h, ox, oy;
} sprite_param;
static const sprite_param STREET_LAMP[4] = {
  { { 0.0234375f, 0.80859375f, 0.48046875f, 0.51171875f }, 36.5625f, 23.75f, 0, -14.375f },
  { { 0.5234375f, 0.80859375f, 0.98046875f, 0.51171875f }, 36.5625f, 23.75f, 0, -14.375f },
  { { 0.0234375f, 0.30859375f, 0.48046875f, 0.01171875f }, 36.5625f, 23.75f, 0, -14.375f },
  { { 0.5f, 0.5f, 1.0f, 0.0f }, 40.0f, 40.0f, 0, 0 },
};
/* nsBuildlingLightManager's (0x93db680), by group (red1, red2, red3, white1, white2): the red
 * aircraft warning lights from buildinglight.tga, the white ones from bridge_light.tga; and
 * their blink period in frames (0x93db714), lit for its first half */
static const sprite_param BUILDING[5] = {
  { { 0, 1, 1, 0 }, 32.0f, 32.0f, 0, 0 }, { { 0, 1, 1, 0 }, 32.0f, 32.0f, 0, 0 }, { { 0, 1, 1, 0 }, 32.0f, 32.0f, 0, 0 },
  { { 0, 1, 1, 0 }, 48.0f, 48.0f, 0, 0 }, { { 0, 1, 1, 0 }, 48.0f, 48.0f, 0, 0 },
};
static const int BUILDING_BLINK[5] = { 120, 120, 0, 0, 0 };
/* clPrimitive::setup's draw distances: lights further ahead than this are left out */
#define STREET_RANGE 1500.0f
#define BUILDING_RANGE 5000.0f
enum { TEX_LAMP, TEX_TUBE, TEX_BUILDING, TEX_BRIDGE, TEX_GOAL, TEX_TEN_LINE, TEX_SPOT, TEX_SPARK, TEX_TRACK = TEX_SPARK + 2, TEX_BACKFIRE };

/* clGoalLine::clLine::update (0x89778f0): up to 20 lights in a row across the road, from 6
 * units in from one road-edge point to 6 in from the other, (int)((length - 2) * 0.05) + 1 of
 * them, 8.2 units up, scaled 2.5 (preScale) and turned to face the camera (its rotation,
 * transposed). Each is the IGB's glow and flash quad (goal_light_yellow.igb for the goal,
 * _blue for the checkpoint; both from goal_light01.tga: yellow its right half, blue its left,
 * the flashes below the glows), drawn additive (igBlendFunctionAttr SRC_ALPHA, ONE) at the
 * igColorAttr's 0.8 grey, without depth writes. Their igTransformSequence1_5 pulses them
 * (scale keys at these frames of 1/60 s, looped); every other light runs 250 ms ahead
 * (setAnimOffset). */
#define GOAL_MAX 20
#define GOAL_INSET 6.0f
#define GOAL_LIFT 8.2f
#define GOAL_SCALE 2.5f
#define GLOW_HALF 5.0f
#define FLASH_HALF 4.37167f
#define GOAL_GREY 204
static const float GOAL_KEY[12] = { 0, 2, 5, 11, 13, 14, 15, 17, 23, 26, 28, 29 };
static const float GLOW_SCALE[12] = { 0.99380f, 0.92648f, 0.70817f, 0.19941f, 0.11223f, 0.1f,
                                      0.11147f, 0.19360f, 0.68320f, 0.90640f, 0.98853f, 1.0f };
static const float FLASH_SCALE[12] = { 0.6051f, 0.6416f, 0.7408f, 0.9584f, 0.9949f, 1.0f,
                                       0.9949f, 0.9584f, 0.7408f, 0.6416f, 0.6051f, 0.6f };
/* by row (goal, checkpoint), glow then flash: left, top, right, bottom (GL's bottom-up v) */
static const float GOAL_UV[2][2][4] = {
  { { 0.5f, 1.0f, 1.0f, 0.5f }, { 0.5f, 0.5f, 1.0f, 0.0f } },
  { { 0.0f, 1.0f, 0.5f, 0.5f }, { 0.0f, 0.5f, 0.5f, 0.0f } },
};

typedef struct {
  float pos[3];
  uint32_t type;
} light_point;

static struct {
  wpk_scene sprites;
  /* the under neons' glows (neon.wpk, loaded with the rest; neon_tried once it was tried) */
  wpk_scene neon;
  int neon_tried;
  char neon_path[1200];
  light_point *pts; /* street lights, then building lights */
  int n_street, n_building;
  render_vertex v[2][4 * RENDER_MAX_SPRITES];
} L;

/* the lights of a city's file (lights.bin Tokyo's, lights_<city>.bin the others'); none
 * when it is missing */
int lights_points(const char *path) {
  free(L.pts);
  L.pts = NULL;
  L.n_street = L.n_building = 0;
  FILE *f = fopen(path, "rb");
  if (!f) return -1;
  char magic[4];
  uint32_t n[2];
  int ok = fread(magic, 4, 1, f) == 1 && fread(n, 4, 2, f) == 2 && magic[0] == 'L' && magic[3] == '1';
  if (ok) {
    L.pts = malloc(sizeof *L.pts * (n[0] + n[1] + 1));
    ok = L.pts && fread(L.pts, sizeof *L.pts, n[0] + n[1], f) == n[0] + n[1];
  }
  fclose(f);
  if (!ok) {
    free(L.pts);
    L.pts = NULL;
    return -1;
  }
  L.n_street = (int)n[0];
  L.n_building = (int)n[1];
  return 0;
}

int lights_load(const char *data_dir) {
  char path[1200];
  snprintf(L.neon_path, sizeof L.neon_path, "%s/neon.wpk", data_dir);
  snprintf(path, sizeof path, "%s/lights.wpk", data_dir);
  if (wpk_load(&L.sprites, path) != 0 || L.sprites.n_tex < 4) return -1;
  /* the neons now too: on first use their 12 uploads hitched the frame they first showed in */
  L.neon_tried = 1;
  if (wpk_load(&L.neon, L.neon_path) != 0) fprintf(stderr, "no %s, no under neons\n", L.neon_path);
  snprintf(path, sizeof path, "%s/lights.bin", data_dir);
  return lights_points(path);
}

/* a camera-facing quad of p at view-space centre c, ext times w by ext times h each side of
 * it (1 for the street lights' half extents, 0.5 for sizes given whole) */
static void quad(render_vertex *q, const float *c, const sprite_param *p, float ext) {
  float x0 = c[0] + p->ox - p->w * ext, x1 = c[0] + p->ox + p->w * ext;
  float y1 = c[1] + p->oy + p->h * ext, y0 = c[1] + p->oy - p->h * ext;
  const float xs[4] = { x0, x1, x1, x0 }, ys[4] = { y1, y1, y0, y0 };
  const float us[4] = { p->uv[0], p->uv[2], p->uv[2], p->uv[0] }, vs[4] = { p->uv[1], p->uv[1], p->uv[3], p->uv[3] };
  for (int k = 0; k < 4; ++k) {
    q[k].pos[0] = xs[k];
    q[k].pos[1] = ys[k];
    q[k].pos[2] = c[2];
    q[k].uv[0] = us[k];
    q[k].uv[1] = vs[k];
    q[k].rgba[0] = q[k].rgba[1] = q[k].rgba[2] = q[k].rgba[3] = 255;
  }
}

/* The lights of [first, first + n) with a type in [lo, hi] up to range ahead, as quads of
 * sprite a[type] (ext: quad's) into out0; blink gives the types' blink periods (NULL:
 * steady). Returns how many. */
static int gather(const mat4 *view, const float *eye, int first, int n, int lo, int hi, float range,
                  const sprite_param *a, float ext, const int *blink, int frame, render_vertex *out0) {
  const float *m = view->m;
  int count = 0;
  for (int i = first; i < first + n && count < RENDER_MAX_SPRITES; ++i) {
    const light_point *l = &L.pts[i];
    int t = (int)l->type;
    if (t < lo || t > hi) continue;
    float dx = l->pos[0] - eye[0], dz = l->pos[2] - eye[2];
    if (dx > range || dx < -range || dz > range || dz < -range) continue;
    if (blink && blink[t] && frame % blink[t] >= blink[t] / 2) continue;
    float c[3];
    for (int r = 0; r < 3; ++r) c[r] = m[r] * l->pos[0] + m[4 + r] * l->pos[1] + m[8 + r] * l->pos[2] + m[12 + r];
    if (c[2] > -1.0f || c[2] < -range) continue;
    quad(out0 + 4 * count, c, &a[t], ext);
    count++;
  }
  return count;
}

void lights_draw(const mat4 *proj, const mat4 *view, int night, float time, render_params *rp) {
  if (!L.pts) return;
  /* the camera's position: the view matrix is a rotation R and a translation t, eye = -R^T t */
  const float *m = view->m;
  float eye[3];
  for (int k = 0; k < 3; ++k) eye[k] = -(m[4 * k] * m[12] + m[4 * k + 1] * m[13] + m[4 * k + 2] * m[14]);
  int frame = (int)(time * 60.0f);
  int n = gather(view, eye, 0, L.n_street, 0, 3, STREET_RANGE, STREET_LAMP, 1.0f, NULL, frame, L.v[0]);
  render_sprites(proj, L.v[0], n, L.sprites.textures[TEX_LAMP], rp);
  if (!night) return;
  n = gather(view, eye, L.n_street, L.n_building, 0, 2, BUILDING_RANGE, BUILDING, 0.5f, BUILDING_BLINK, frame, L.v[0]);
  render_sprites(proj, L.v[0], n, L.sprites.textures[TEX_BUILDING], rp);
  n = gather(view, eye, L.n_street, L.n_building, 3, 4, BUILDING_RANGE, BUILDING, 0.5f, BUILDING_BLINK, frame, L.v[0]);
  render_sprites(proj, L.v[0], n, L.sprites.textures[TEX_BRIDGE], rp);
}

/* a key of the lights' pulse at frame f (0..29) */
static float goal_key(const float *v, float f) {
  for (int k = 1; k < 12; ++k)
    if (f <= GOAL_KEY[k]) return v[k - 1] + (v[k] - v[k - 1]) * (f - GOAL_KEY[k - 1]) / (GOAL_KEY[k] - GOAL_KEY[k - 1]);
  return v[11];
}

/* a camera-facing square of half size h at view-space centre c, texture rect uv */
static void goal_quad(render_vertex *q, const float *c, float h, const float *uv) {
  const float xs[4] = { c[0] - h, c[0] + h, c[0] + h, c[0] - h }, ys[4] = { c[1] + h, c[1] + h, c[1] - h, c[1] - h };
  const float us[4] = { uv[0], uv[2], uv[2], uv[0] }, vs[4] = { uv[1], uv[1], uv[3], uv[3] };
  for (int k = 0; k < 4; ++k) {
    q[k].pos[0] = xs[k];
    q[k].pos[1] = ys[k];
    q[k].pos[2] = c[2];
    q[k].uv[0] = us[k];
    q[k].uv[1] = vs[k];
    q[k].rgba[0] = q[k].rgba[1] = q[k].rgba[2] = GOAL_GREY;
    q[k].rgba[3] = 255;
  }
}

int lights_warm(void) {
  static int done;
  if (done || !L.sprites.n_tex) return 0;
  done = 1;
  for (uint32_t k = 0; k < L.sprites.n_tex; ++k) render_warm_texture(L.sprites.textures[k]);
  return (int)L.sprites.n_tex;
}

void lights_draw_goal(const mat4 *proj, const mat4 *view, const car_race *r, float time, render_params *rp) {
  if (L.sprites.n_tex <= TEX_GOAL || !r->lines) return;
  const float *m = view->m;
  int n = 0;
  for (int row = 0; row < 2; ++row) {
    if (!(r->lines & (1 << row))) continue;
    const float *a = r->line[row][0], *b = r->line[row][1];
    float d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
    float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    int count = (int)((len - 2.0f) * 0.05f) + 1;
    if (len <= 0.0f || count < 1) continue;
    if (count > GOAL_MAX) count = GOAL_MAX;
    float s[3], e[3];
    for (int k = 0; k < 3; ++k) {
      s[k] = a[k] + GOAL_INSET * d[k] / len;
      e[k] = b[k] - GOAL_INSET * d[k] / len;
    }
    for (int i = 0; i < count; ++i) {
      float f = count > 1 ? (float)i / (float)(count - 1) : 0.0f;
      float p[3] = { s[0] + (e[0] - s[0]) * f, s[1] + (e[1] - s[1]) * f + GOAL_LIFT, s[2] + (e[2] - s[2]) * f };
      float c[3];
      for (int k = 0; k < 3; ++k) c[k] = m[k] * p[0] + m[4 + k] * p[1] + m[8 + k] * p[2] + m[12 + k];
      if (c[2] > -1.0f) continue; /* behind the camera */
      float t = fmodf(time + (i & 1 ? 0.0f : 0.25f), 29.0f / 60.0f) * 60.0f;
      goal_quad(L.v[0] + 4 * n++, c, GLOW_HALF * GOAL_SCALE * goal_key(GLOW_SCALE, t), GOAL_UV[row][0]);
      goal_quad(L.v[0] + 4 * n++, c, FLASH_HALF * GOAL_SCALE * goal_key(FLASH_SCALE, t), GOAL_UV[row][1]);
    }
  }
  render_sprites(proj, L.v[0], n, L.sprites.textures[TEX_GOAL], rp);
}

/* clTenRivalLine::update (0x861d8c8), the 10-in-a-row's blue line across the road at the
 * rival: rival_line_blue.igb's mesh (tenline.h) posed from the two points a, b as x along
 * b - a its full length, y up (0, 1, 0) made square to it times TEN_RIVAL_LINE_HEIGHT
 * (effect_config.lua: 14), z their cross, at the middle; additive, its vertex colours. */
#define TEN_LINE_HEIGHT 14.0f

void lights_draw_ten_line(const mat4 *proj, const mat4 *view, const car_race *r, render_params *rp) {
  if (L.sprites.n_tex <= TEX_TEN_LINE || !r->ten_line_on) return;
  const float *a = r->ten_line[0], *b = r->ten_line[1], *m = view->m;
  float d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
  float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
  if (len <= 0.0f) return;
  float x[3] = { d[0] / len, d[1] / len, d[2] / len };
  float z[3] = { -x[2], 0.0f, x[0] }; /* x cross (0, 1, 0) */
  float zl = sqrtf(z[0] * z[0] + z[2] * z[2]);
  if (zl <= 0.0f) return;
  z[0] /= zl, z[2] /= zl;
  float y[3] = { z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0] };
  int n = 0;
  for (int t = 0; t < TENLINE_VERTS; t += 3, ++n) {
    render_vertex *q = L.v[0] + 4 * n;
    for (int k = 0; k < 4; ++k) {
      int i = t + (k < 3 ? k : 2); /* the triangle as a quad with its last corner twice */
      const float *v = TENLINE_POS[i];
      float p[3];
      for (int j = 0; j < 3; ++j)
        p[j] = (a[j] + b[j]) * 0.5f + v[0] * len * x[j] + v[1] * TEN_LINE_HEIGHT * y[j] + v[2] * z[j];
      for (int j = 0; j < 3; ++j) q[k].pos[j] = m[j] * p[0] + m[4 + j] * p[1] + m[8 + j] * p[2] + m[12 + j];
      q[k].uv[0] = v[3];
      q[k].uv[1] = v[4];
      memcpy(q[k].rgba, TENLINE_RGBA[i], 4);
    }
  }
  render_sprites(proj, L.v[0], n, L.sprites.textures[TEX_TEN_LINE], rp);
}

/* clCarSpotLight (init 0x8603978, update 0x8603c84, draw 0x8604248): the headlights' pool,
 * sprite/car_lightspot_a3.tga (image 1334) on two overlapping quads in the car's space, one
 * per lamp, flat on the road (y 0 and 0.05), 12 to 250 ahead, v 0 at the car (the bright
 * edge); vertex colour effect_config.lua's CAR_SPOT_LIGHT_MORNING (0.4, 0.4, 0.5) or _NIGHT
 * (0.7, 0.7, 0.8) by clWorkSpace::getDayTimeSelect; drawn depth tested, not written,
 * glBlendFunc(GL_SRC_COLOR, GL_ONE). */
static const float SPOT_X[2][2] = { { -36.0f, 54.0f }, { -54.0f, 36.0f } };
static const float SPOT_Y[2] = { 0.0f, 0.05f };
#define SPOT_NEAR 12.0f
#define SPOT_FAR 250.0f

void lights_draw_spots(const mat4 *proj, const mat4 *view, const float *const *bodies, int n, int night, render_params *rp) {
  if (L.sprites.n_tex <= TEX_SPOT || n <= 0) return;
  const uint8_t grey = night ? 179 : 102, blue = night ? 204 : 128;
  const float *m = view->m;
  int q = 0;
  for (int i = 0; i < n && q + 2 <= RENDER_MAX_SPRITES; ++i) {
    const float *b = bodies[i];
    for (int s = 0; s < 2; ++s, ++q) {
      render_vertex *v = L.v[0] + 4 * q;
      const float xs[4] = { SPOT_X[s][0], SPOT_X[s][1], SPOT_X[s][1], SPOT_X[s][0] };
      const float zs[4] = { SPOT_FAR, SPOT_FAR, SPOT_NEAR, SPOT_NEAR };
      const float us[4] = { 0, 1, 1, 0 }, vs[4] = { 1, 1, 0, 0 };
      for (int k = 0; k < 4; ++k) {
        float p[3];
        for (int j = 0; j < 3; ++j) p[j] = b[j] * xs[k] + b[4 + j] * SPOT_Y[s] + b[8 + j] * zs[k] + b[12 + j];
        for (int j = 0; j < 3; ++j) v[k].pos[j] = m[j] * p[0] + m[4 + j] * p[1] + m[8 + j] * p[2] + m[12 + j];
        v[k].uv[0] = us[k];
        v[k].uv[1] = vs[k];
        v[k].rgba[0] = v[k].rgba[1] = grey;
        v[k].rgba[2] = blue;
        v[k].rgba[3] = 255;
      }
    }
  }
  render_sprites_by_colour(proj, L.v[0], q, L.sprites.textures[TEX_SPOT], rp);
}

/* clNeon::setup (0x895094c): the model's matrix makeScale(NeonScaleGetFromCarId) with the
 * translation NeonOffsetGetFromCarId, under the car's: the quad's corners (u along x, v
 * along -z, as the IGBs map them) moved by it, then by frame. The IGBs' blend is
 * SRC_ALPHA, ONE; neon.wpk has the alpha multiplied in (pack_neon.py). */
#define NEON_HALF 29.0f

void lights_draw_neon(const mat4 *proj, const mat4 *view, const float *frame, int pack, int body, int neon, render_params *rp) {
  if (neon < 1 || neon > 12) return;
  if (!L.neon_tried) {
    L.neon_tried = 1;
    if (wpk_load(&L.neon, L.neon_path) != 0) fprintf(stderr, "no %s, no under neons\n", L.neon_path);
  }
  if (L.neon.n_tex < neon) return;
  const float *pl = NEON_PLACE[pack >= 0 && pack < (int)(sizeof NEON_PLACE / sizeof NEON_PLACE[0]) ? pack : 0][body & 3];
  const float *m = view->m, *f = frame;
  const float xs[4] = { -NEON_HALF, NEON_HALF, NEON_HALF, -NEON_HALF }, zs[4] = { NEON_HALF, NEON_HALF, -NEON_HALF, -NEON_HALF };
  const float us[4] = { 0, 1, 1, 0 }, vs[4] = { 0, 0, 1, 1 };
  render_vertex *q = L.v[0];
  for (int k = 0; k < 4; ++k) {
    float l[3] = { xs[k] * pl[3] + pl[0], pl[1], zs[k] * pl[5] + pl[2] }, p[3];
    for (int j = 0; j < 3; ++j) p[j] = f[j] * l[0] + f[4 + j] * l[1] + f[8 + j] * l[2] + f[12 + j];
    for (int j = 0; j < 3; ++j) q[k].pos[j] = m[j] * p[0] + m[4 + j] * p[1] + m[8 + j] * p[2] + m[12 + j];
    q[k].uv[0] = us[k];
    q[k].uv[1] = vs[k];
    q[k].rgba[0] = q[k].rgba[1] = q[k].rgba[2] = q[k].rgba[3] = 255;
  }
  render_sprites(proj, q, 1, L.neon.textures[neon - 1], rp);
}

uint32_t lights_backfire_texture(int k) {
  return k >= 0 && L.sprites.n_tex > TEX_BACKFIRE + k ? L.sprites.textures[TEX_BACKFIRE + k] : 0;
}

uint32_t lights_track_texture(void) { return L.sprites.n_tex > TEX_TRACK ? L.sprites.textures[TEX_TRACK] : 0; }

/* ---- the aura (オーラ, v2.3): clMenuCar::setAura's car_aura_<colour>.igb and its glow, from
 * data/aura.bin (tools/stage/pack_aura.py): colour 0 green, 1 purple, 2 red, 3 blue
 * (nsMenuCheck::checkAura's), level 0..4 its group aura01..aura05; each flame shell's texture
 * running down its v (the IGB's keyed texture transform, a 14.983 s loop), SRC_ALPHA ONE, the
 * glow quad under the car; in the car's own space, under the car's matrix (as the neons). ---- */
#define AURA_COLOURS 4
#define AURA_LOOP 14.983f
typedef struct {
  unsigned char level, tex;
  float run;
  int nv, ni;
  float *v; /* x y z u v */
  uint16_t *idx;
} aura_mesh;
static struct {
  int tried, ok;
  char path[1200];
  int n_tex[AURA_COLOURS], n_mesh[AURA_COLOURS];
  GLuint tex[AURA_COLOURS][4];
  aura_mesh *mesh[AURA_COLOURS];
} AU;

void lights_aura_path(const char *path) { snprintf(AU.path, sizeof AU.path, "%s", path); }

static int aura_read(FILE *f, void *p, size_t n) { return fread(p, 1, n, f) == n; }

static void aura_load(void) {
  AU.tried = 1;
  FILE *f = AU.path[0] ? fopen(AU.path, "rb") : NULL;
  if (!f) {
    fprintf(stderr, "no %s, no auras\n", AU.path[0] ? AU.path : "aura.bin");
    return;
  }
  char magic[4];
  uint32_t nc;
  int ok = aura_read(f, magic, 4) && magic[0] == 'W' && magic[1] == 'A' && magic[2] == 'U' && aura_read(f, &nc, 4) && nc == AURA_COLOURS;
  for (int c = 0; ok && c < AURA_COLOURS; ++c) {
    uint32_t nt, nm;
    ok = aura_read(f, &nt, 4) && nt <= 4;
    for (uint32_t t = 0; ok && t < nt; ++t) {
      uint16_t wh[2];
      uint32_t zl;
      ok = aura_read(f, wh, 4) && aura_read(f, &zl, 4) && wh[0] <= 1024 && wh[1] <= 1024;
      unsigned char *z = ok ? malloc(zl) : NULL, *px = ok ? malloc((size_t)wh[0] * wh[1] * 4) : NULL;
      uLongf got = (uLongf)wh[0] * wh[1] * 4;
      ok = ok && z && px && aura_read(f, z, zl) && uncompress(px, &got, z, zl) == Z_OK;
      if (ok) {
        glGenTextures(1, &AU.tex[c][t]);
        glBindTexture(GL_TEXTURE_2D, AU.tex[c][t]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, wh[0], wh[1], 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
      }
      free(z);
      free(px);
    }
    AU.n_tex[c] = (int)nt;
    ok = ok && aura_read(f, &nm, 4) && nm < 64;
    AU.mesh[c] = ok ? calloc(nm, sizeof(aura_mesh)) : NULL;
    ok = ok && AU.mesh[c];
    for (uint32_t i = 0; ok && i < nm; ++i) {
      aura_mesh *m = &AU.mesh[c][i];
      unsigned char h[4];
      uint32_t n[2];
      ok = aura_read(f, h, 4) && aura_read(f, &m->run, 4) && aura_read(f, n, 8) && n[0] < 65536 && n[1] < 65536 * 3;
      m->level = h[0], m->tex = h[1], m->nv = (int)n[0], m->ni = (int)n[1];
      m->v = ok ? malloc(sizeof(float) * 5 * n[0]) : NULL;
      m->idx = ok ? malloc(sizeof(uint16_t) * n[1]) : NULL;
      ok = ok && m->v && m->idx && aura_read(f, m->v, sizeof(float) * 5 * n[0]) && aura_read(f, m->idx, sizeof(uint16_t) * n[1]);
    }
    AU.n_mesh[c] = ok ? (int)nm : 0;
  }
  fclose(f);
  AU.ok = ok;
  fprintf(stderr, "auras: %s\n", ok ? "loaded" : "unreadable");
}

void lights_draw_aura(const mat4 *proj, const mat4 *view, const float *frame, int colour, int level, float time, render_params *rp) {
  if (colour < 0 || colour >= AURA_COLOURS || level < 0 || level > 4) return;
  if (!AU.tried) aura_load();
  if (!AU.ok) return;
  const float *m = view->m, *f = frame;
  float t = fmodf(time, AURA_LOOP);
  /* by texture: the flame shells of the level, then the glow */
  for (int tx = 0; tx < AU.n_tex[colour]; ++tx) {
    int q = 0;
    for (int i = 0; i < AU.n_mesh[colour]; ++i) {
      const aura_mesh *a = &AU.mesh[colour][i];
      if (a->tex != tx || (a->level != level && a->level != 255)) continue;
      float dv = a->run * t;
      for (int k = 0; k + 2 < a->ni && q < RENDER_MAX_SPRITES; k += 3, ++q) {
        render_vertex *o = L.v[0] + 4 * q;
        for (int c = 0; c < 4; ++c) {
          const float *s = a->v + 5 * a->idx[k + (c < 3 ? c : 2)];
          float p[3];
          for (int j = 0; j < 3; ++j) p[j] = f[j] * s[0] + f[4 + j] * s[1] + f[8 + j] * s[2] + f[12 + j];
          for (int j = 0; j < 3; ++j) o[c].pos[j] = m[j] * p[0] + m[4 + j] * p[1] + m[8 + j] * p[2] + m[12 + j];
          o[c].uv[0] = s[3];
          o[c].uv[1] = s[4] + dv;
          o[c].rgba[0] = o[c].rgba[1] = o[c].rgba[2] = o[c].rgba[3] = 255;
        }
      }
    }
    if (q) render_sprites(proj, L.v[0], q, AU.tex[colour][tx], rp);
  }
}

uint32_t lights_spark_texture(int k) {
  return k >= 0 && k < 2 && L.sprites.n_tex > TEX_SPARK + k ? L.sprites.textures[TEX_SPARK + k] : 0;
}
