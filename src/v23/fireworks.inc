/* (v2.3) the track's fireworks (花火): clFireWorkManager (ctor 0x860c624, 8 shells from
 * clStageMiscObjectManager's ctor 0x8610ab0; models data/effect/fireworks01.igb (the burst) and
 * fireworks02.igb (the rising trail)), update 0x860ecf0 called by clStageMiscObjectManager::update
 * 0x8610804 with a translation to the show's spot, the repeat flag (+0x20, setFireWorkRepeat),
 * the spot and the player car's translation (CSeq::GetInfoMyCarDisp).
 *
 * When (clStageEffectObjectManager::update 0x860adf4, only in clWorkSpace::getPlayState() == 2):
 * yt::CSeq::GetInfoMyCarPlace (area = *machine[0x93c], at = machine[0x7b4+0x1bc] -
 * [0x7b4+0x180], raw: car.c's place / place_at) against the table at 0x93da320, entries
 * { effect, area, from, area_to, to, daytime, kind }; the fireworks are effects 0..3:
 *   0: area 51, 250..2000   1: area 50, 1000..2600   2: area 70, 3200..6000   3: area 72, 7700..9100
 * compared as at >= from * 1000 and at <= to * 1000 (the table times 1000 against the raw
 * distance), daytime 1 = clWorkSpace::getDayTimeSelect() == 1, the night (2 would be either).
 * On: the effect's state 0 -> 1 and clStageMiscObjectManager::awake(detail = effect), which copies
 * the detail's spot (0x8610700's table) to the manager and showFlg(1). Leaving the area clears the
 * state without stopping anything; reaching `to` in the area clears it and setFireWorkRepeat(0):
 * the shells then finish their round and the show is over (the flag is set again only by a new
 * clStageMiscObjectManager, i.e. the next race: fw_reset). A later zone's awake only moves the spot.
 *
 * Each shell (state +0, frame +4, wait +8, sub-state +0xc), a frame = 1/60 s:
 *   0  waits rand() % 200 frames, its offset from the spot (rand()%1600-800, rand()%200,
 *      rand()%1600-800), its colour rand() % 10 of set_firerandomcolor's ten;
 *   1  the launch (every 3rd launch ONESHOT_HANABI1), straight to 2;
 *   2  fireworks02 at the offset - 70 (scale 2), its seven layers fading 0.05 a frame on frames
 *      100..119 (shell 6: 0.025); after frame 120 the burst (every 3rd ONESHOT_HANABI2), 3;
 *   3  fireworks01 at the offset + 500 (scale 14): the spark layer fading 0.1 a frame to frame 10,
 *      then three layers at 1, held to 40, fading 0.025 / 0.00833 / 0.0125 a frame, after 266: 4;
 *   4  with the repeat flag a new round (0), else done.
 * Sound volume (both oneshots, each frame): (10000 - |spot - car|) / 8000 clamped to 0.4..1,
 * times 1.3.
 *
 * Drawn here as billboards from the IGBs' own textures (data/fireworks.bin,
 * tools/stage/pack_fireworks.py): texture 0 is a 2 x 2 atlas (glow, flash disc, rays, dots; the
 * rays and dots a quarter each, mirrored 4 ways about the burst's centre), 1 the star spark,
 * 2 the soft blob. render_sprites: additive, depth tested, not written. The models' own
 * animation (the trail's rise, the burst's spread) is not decoded: the rise and the spread below
 * are eased stand-ins, the burst's radius a guess.
 *
 * Needs: gl.h, render.h, math.h, stdio.h, stdlib.h, zlib.h. Statics, prefix fw_. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>

#define FW_MAX 8 /* clFireWorkManager(8) */
#define FW_SPARKS 48
#define FW_TRAIL 7
#define FW_RADIUS 420.0f /* (the burst model at scale 14: not measured) */

/* set_firerandomcolor's ten (r, g, b) */
static const float FW_COLOUR[10][3] = {
  { 0.9f, 0.7f, 0.7f }, { 0.7f, 0.9f, 0.7f }, { 0.7f, 0.7f, 0.9f }, { 1.0f, 0.5f, 0.5f }, { 0.5f, 1.0f, 0.5f },
  { 0.5f, 0.5f, 1.0f }, { 0.2f, 0.6f, 1.0f }, { 1.0f, 0.2f, 0.6f }, { 0.2f, 1.0f, 0.6f }, { 0.6f, 1.0f, 0.2f },
};
/* the arcade's table (0x93da320, effects 0..3): area id, from, to (x 1000 against place_at) */
static const struct {
  int area;
  float from, to;
} FW_ZONE[4] = { { 51, 250, 2000 }, { 50, 1000, 2600 }, { 70, 3200, 6000 }, { 72, 7700, 9100 } };
/* the shows' spots by detail (0x8610700): 0 and 1 the same */
static const float FW_SPOT[4][3] = {
  { -75.2890625f, 462.61014f, 32365.607f },
  { -75.2890625f, 462.61014f, 32365.607f },
  { -23679.770f, 800.0f, -17345.592f },
  { -24746.109f, 900.0f, -12998.360f },
};

typedef struct {
  int state, frame, wait, sub, colour;
  float off[3];             /* from the spot */
  float a[4];               /* the burst's layers: spark, glow, rays, dots */
  float trail[FW_TRAIL];    /* the rising trail's layers */
  float dir[FW_SPARKS][3];  /* the sparks' directions (drawing only) */
} fw_shell;

static struct {
  int tried, ok, n_tex;
  char path[1200];
  GLuint tex[4];
  fw_shell shell[FW_MAX];
  int zstate[4];     /* nsStageEffectObject::effect_state[0..3] */
  int shown, repeat; /* showFlg, setFireWorkRepeat */
  int done;          /* shells done (the manager's +4) */
  int launches, bursts;   /* the manager's +0x88, +0x8c */
  int hanabi1, hanabi2;   /* sounds since fw_sounds */
  float spot[3], vol;
  unsigned rng;
  /* the old fw_zone / fw_update pair */
  int z_area;
  float z_at;
  render_vertex v[4 * (FW_MAX * (FW_SPARKS + 16))];
} FW = { .repeat = 1 };

static void fw_path(const char *path) { snprintf(FW.path, sizeof FW.path, "%s", path); }

static int fw_rand(void) {
  FW.rng = FW.rng * 1103515245u + 12345u;
  return (int)((FW.rng >> 16) & 0x7fff);
}
static float fw_frand(void) { return (float)fw_rand() / 32768.0f; }

/* a new round for shell i (the ctor's and state 4's) */
static void fw_round(int i) {
  fw_shell *s = &FW.shell[i];
  s->frame = 0;
  s->wait = fw_rand() % 200;
  s->off[0] = (float)(fw_rand() % 1600 - 800);
  s->off[1] = (float)(fw_rand() % 200);
  s->off[2] = (float)(fw_rand() % 1600 - 800);
  s->state = s->sub = 0;
  s->colour = fw_rand() % 10;
  s->a[0] = 1, s->a[1] = s->a[2] = s->a[3] = 0;
  for (int k = 0; k < FW_TRAIL; ++k) s->trail[k] = k == FW_TRAIL - 1 ? 0.5f : 1.0f;
  for (int k = 0; k < FW_SPARKS; ++k) {
    float z = 2.0f * fw_frand() - 1.0f, an = 6.2831853f * fw_frand(), r = sqrtf(1.0f - z * z);
    s->dir[k][0] = r * cosf(an), s->dir[k][1] = z, s->dir[k][2] = r * sinf(an);
  }
}

/* a new race (a new clStageMiscObjectManager): nothing shown, the repeat flag on */
static void fw_reset(void) {
  FW.shown = 0, FW.repeat = 1, FW.done = 0, FW.launches = FW.bursts = 0, FW.hanabi1 = FW.hanabi2 = 0;
  for (int e = 0; e < 4; ++e) FW.zstate[e] = 0;
  for (int i = 0; i < FW_MAX; ++i) fw_round(i);
}

static void fw_load(void) {
  FW.tried = 1;
  FW.rng = 0x2468aceu;
  fw_reset();
  FILE *f = FW.path[0] ? fopen(FW.path, "rb") : NULL;
  if (!f) {
    fprintf(stderr, "no %s, no fireworks\n", FW.path[0] ? FW.path : "fireworks.bin");
    return;
  }
  char magic[4];
  uint32_t n = 0;
  int ok = fread(magic, 1, 4, f) == 4 && magic[0] == 'W' && magic[1] == 'F' && magic[2] == 'W' && fread(&n, 4, 1, f) == 1 && n >= 3 && n <= 4;
  for (uint32_t t = 0; ok && t < n; ++t) {
    uint16_t wh[2];
    uint32_t zl;
    ok = fread(wh, 2, 2, f) == 2 && fread(&zl, 4, 1, f) == 1 && wh[0] <= 1024 && wh[1] <= 1024;
    unsigned char *z = ok ? malloc(zl) : NULL, *px = ok ? malloc((size_t)wh[0] * wh[1] * 4) : NULL;
    uLongf got = (uLongf)wh[0] * wh[1] * 4;
    ok = ok && z && px && fread(z, 1, zl, f) == zl && uncompress(px, &got, z, zl) == Z_OK;
    if (ok) {
      glGenTextures(1, &FW.tex[t]);
      glBindTexture(GL_TEXTURE_2D, FW.tex[t]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, wh[0], wh[1], 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    }
    free(z);
    free(px);
  }
  fclose(f);
  FW.n_tex = ok ? (int)n : 0;
  FW.ok = ok;
  fprintf(stderr, "fireworks: %s\n", ok ? "loaded" : "unreadable");
}

/* clStageEffectObjectManager::update's part for effects 0..3: area and at raw from
 * GetInfoMyCarPlace (car.c's race.place, race.place_at), night = getDayTimeSelect() == 1 */
static void fw_zones(int area, float at, int night) {
  for (int e = 0; e < 4; ++e) {
    if (area != FW_ZONE[e].area) {
      FW.zstate[e] = 0;
      continue;
    }
    if (at >= FW_ZONE[e].from * 1000.0f && at <= FW_ZONE[e].to * 1000.0f && !FW.zstate[e] && night) {
      FW.zstate[e] = 1;
      /* awake(detail e): the spot, showFlg(1) */
      for (int j = 0; j < 3; ++j) FW.spot[j] = FW_SPOT[e][j];
      FW.shown = 1;
    }
  }
  int ended = 0;
  for (int e = 0; e < 4; ++e)
    if (area == FW_ZONE[e].area && FW.zstate[e] == 1 && at >= FW_ZONE[e].to * 1000.0f) FW.zstate[e] = 0, ended = 1;
  if (ended) FW.repeat = 0;
}

/* clFireWorkManager::update, one frame (car: the player car's world translation) */
static void fw_shells(const float car[3]) {
  if (!FW.shown) return;
  if (FW.done >= FW_MAX && !FW.repeat) return;
  FW.done = 0;
  for (int i = 0; i < FW_MAX; ++i) {
    fw_shell *s = &FW.shell[i];
    switch (s->state) {
    case 0:
      if (--s->wait < 0) s->state = 1;
      break;
    case 1:
      s->state = 2;
      if (++FW.launches % 3 == 0) FW.hanabi1++;
      break;
    case 2:
      s->frame++;
      if (s->frame >= 100 && s->frame < 120) {
        for (int k = 0; k < FW_TRAIL; ++k) s->trail[k] -= i == 6 ? 0.025f : 0.05f;
      } else if (s->frame > 120) {
        if (++FW.bursts % 3 == 0) FW.hanabi2++;
        s->state = 3, s->frame = 0;
      }
      break;
    case 3:
      s->frame++;
      if (s->sub == 0) {
        if (s->frame < 10) s->a[0] = s->a[0] > 0 ? s->a[0] - 0.1f : 0.0f;
        else if (s->frame == 10) s->sub = 1, s->a[0] = 0, s->a[1] = s->a[2] = s->a[3] = 1;
      } else if (s->sub == 1) {
        if (s->frame >= 40) s->sub = 2;
      } else if (s->sub == 2) {
        s->a[1] = s->a[1] > 0 ? s->a[1] - 0.025f : 0.0f;
        s->a[2] = s->a[2] > 0 ? s->a[2] - 0.0083333f : 0.0f;
        s->a[3] = s->a[3] > 0 ? s->a[3] - 0.0125f : 0.0f;
        if (s->frame >= 160) s->sub = 3;
      }
      if (s->frame > 266) s->state = 4;
      break;
    default:
      if (FW.repeat) fw_round(i);
      else FW.done++;
    }
  }
  float d[3] = { FW.spot[0] - car[0], FW.spot[1] - car[1], FW.spot[2] - car[2] };
  float v = (10000.0f - sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2])) * 0.000125f;
  FW.vol = (v > 1.0f ? 1.0f : v < 0.4f ? 0.4f : v) * 1.3f;
}

/* Each 1/60 s frame of a race: area and at raw from GetInfoMyCarPlace (G.car.race.place,
 * G.car.race.place_at), night (G.track_zone == 1), the player car's world position. */
static void fw_step(int area, float at, int night, const float car[3]) {
  if (!FW.tried) fw_load();
  if (!FW.ok) return;
  fw_zones(area, at, night);
  fw_shells(car);
}

/* the sounds since the last call: hanabi1.wav (launches), hanabi2.wav (bursts); returns their
 * volume (SNDOneshotSetVolPer's: 0.52..1.3 by the distance to the spot) */
static float fw_sounds(int *hanabi1, int *hanabi2) {
  *hanabi1 = FW.hanabi1, *hanabi2 = FW.hanabi2;
  FW.hanabi1 = FW.hanabi2 = 0;
  return FW.vol;
}

/* (the first hook's pair, kept so app.c builds until it moves to fw_step / fw_sounds, see
 * fx.HOOKS.md: the night is taken as on, the distance back to raw) */
static int fw_zone(int area, float dist_m) {
  FW.z_area = area, FW.z_at = dist_m * 1000.0f;
  return 1;
}
static void fw_update(int on, const float car[3], const float fwd[3], float dt) {
  (void)on, (void)fwd, (void)dt;
  fw_step(FW.z_area, FW.z_at, 1, car);
  /* (no fw_reset at a race's start on this path: re-arm once the show is over and the car is
   * out of the four areas, so the next race shows again) */
  if (!FW.repeat && FW.done >= FW_MAX) {
    int in = 0;
    for (int e = 0; e < 4; ++e) in |= FW.z_area == FW_ZONE[e].area;
    if (!in) fw_reset();
  }
}
static int fw_bursts(void) {
  int a, b;
  fw_sounds(&a, &b);
  return a + b;
}

static void fw_set(render_vertex *o, const float c[3], float dx0, float dy0, float dx1, float dy1, float u0, float v0, float u1,
                   float v1, const float rgb[3], float a) {
  const float dx[4] = { dx0, dx1, dx1, dx0 }, dy[4] = { dy0, dy0, dy1, dy1 }, u[4] = { u0, u1, u1, u0 }, v[4] = { v0, v0, v1, v1 };
  if (a < 0) a = 0;
  if (a > 1) a = 1;
  for (int k = 0; k < 4; ++k) {
    o[k].pos[0] = c[0] + dx[k], o[k].pos[1] = c[1] + dy[k], o[k].pos[2] = c[2];
    o[k].uv[0] = u[k], o[k].uv[1] = v[k];
    for (int j = 0; j < 3; ++j) o[k].rgba[j] = (uint8_t)(rgb[j] * 255.0f); /* (render_sprites: SRC_ALPHA, ONE) */
    o[k].rgba[3] = (uint8_t)(a * 255.0f);
  }
}

/* a quarter of the atlas mirrored 4 ways about c: cu, cv the quarter's corner at the centre,
 * ou, ov its far corner */
static int fw_burst4(render_vertex *o, const float c[3], float r, float cu, float cv, float ou, float ov, const float rgb[3], float a) {
  for (int q = 0; q < 4; ++q) {
    float sx = q & 1 ? -r : r, sy = q & 2 ? -r : r;
    fw_set(o + 4 * q, c, 0, 0, sx, sy, cu, cv, ou, ov, rgb, a);
  }
  return 4;
}

static void fw_eye(const float *m, const float w[3], float c[3]) {
  for (int j = 0; j < 3; ++j) c[j] = m[j] * w[0] + m[4 + j] * w[1] + m[8 + j] * w[2] + m[12 + j];
}

/* the shells, in the race's 3D pass after the scene (with the other added sprites) */
static void fw_draw(const mat4 *proj, const mat4 *view, render_params *rp) {
  if (!FW.ok || !FW.shown) return;
  const float *m = view->m;
  int nq[3] = { 0, 0, 0 };
  /* by texture, each its own run in FW.v */
  render_vertex *run[3] = { FW.v, FW.v + 4 * FW_MAX * 16, FW.v + 4 * FW_MAX * 20 };
  static const float white[3] = { 1, 1, 1 };
  for (int i = 0; i < FW_MAX; ++i) {
    const fw_shell *s = &FW.shell[i];
    const float *rgb = FW_COLOUR[s->colour];
    float base[3] = { FW.spot[0] + s->off[0], FW.spot[1] + s->off[1], FW.spot[2] + s->off[2] }, c[3];
    if (s->state == 2) {
      /* fireworks02 from 70 below the offset up toward the burst at 500 above it (eased) */
      float t = s->frame / 100.0f, a = s->trail[0];
      if (t > 1) t = 1;
      float w[3] = { base[0], base[1] - 70.0f + 570.0f * (1.0f - (1.0f - t) * (1.0f - t)), base[2] };
      fw_eye(m, w, c);
      if (c[2] > -10.0f || a <= 0) continue;
      if (nq[1] < FW_MAX * 4) fw_set(run[1] + 4 * nq[1]++, c, -30, -30, 30, 30, 0, 0, 1, 1, white, a);
      for (int k = 1; k <= 3 && nq[1] < FW_MAX * 4; ++k) { /* its trail */
        float wk[3] = { w[0], w[1] - 40.0f * k * (1.0f - t + 0.2f), w[2] }, ck[3];
        fw_eye(m, wk, ck);
        fw_set(run[1] + 4 * nq[1]++, ck, -18, -18, 18, 18, 0, 0, 1, 1, white, s->trail[k] * (1.0f - 0.25f * k));
      }
      continue;
    }
    if (s->state != 3) continue;
    float f = (float)s->frame;
    float drop = f > 10 ? 0.012f * (f - 10) * (f - 10) : 0.0f;
    float w[3] = { base[0], base[1] + 500.0f - drop, base[2] };
    fw_eye(m, w, c);
    if (c[2] > -10.0f) continue; /* behind the eye */
    if (s->sub == 0) {
      /* the spark layer, fading 0.1 a frame toward the burst */
      if (nq[1] < FW_MAX * 4) fw_set(run[1] + 4 * nq[1]++, c, -30, -30, 30, 30, 0, 0, 1, 1, white, s->a[0] < 0.3f ? 0.3f : s->a[0]);
      continue;
    }
    float t = f - 10, g = 1.0f - expf(-t / 10.0f), r = FW_RADIUS * g;
    float ag = s->a[1], ar = s->a[2], ad = s->a[3];
    if (nq[0] + 16 <= FW_MAX * 16) {
      render_vertex *o = run[0] + 4 * nq[0];
      int k = 0;
      if (ag > 0) k += fw_burst4(o + 4 * k, c, r * 0.6f, 0.5f, 0.0f, 0.0f, 0.5f, rgb, ag); /* glow */
      if (ar > 0) k += fw_burst4(o + 4 * k, c, r, 0.5f, 0.5f, 0.0f, 1.0f, rgb, ar);        /* rays */
      if (ad > 0) k += fw_burst4(o + 4 * k, c, r * 0.85f, 0.5f, 0.5f, 1.0f, 1.0f, rgb, ad); /* dots */
      if (ag > 0 && t < 12.0f) {                                                           /* the flash disc */
        float fr = FW_RADIUS * 0.35f, a = 1.0f - t / 12.0f;
        fw_set(o + 4 * k, c, -fr, -fr, fr, fr, 0.5f, 0.0f, 1.0f, 0.5f, white, a);
        k++;
      }
      nq[0] += k;
    }
    /* the sparks flying out (the star), falling and fading with the rays */
    float as = ar > ad ? ar : ad;
    for (int k = 0; k < FW_SPARKS && as > 0 && nq[2] < FW_MAX * FW_SPARKS / 2; k += 2) {
      float p[3], e[3];
      for (int j = 0; j < 3; ++j) p[j] = w[j] + s->dir[k][j] * r * 1.05f;
      fw_eye(m, p, e);
      fw_set(run[2] + 4 * nq[2]++, e, -14, -14, 14, 14, 0, 0, 1, 1, rgb, as * 0.9f);
    }
  }
  if (nq[0]) render_sprites(proj, run[0], nq[0], FW.tex[0], rp);
  if (nq[1]) render_sprites(proj, run[1], nq[1], FW.tex[1], rp);
  if (nq[2]) render_sprites(proj, run[2], nq[2], FW.tex[1], rp);
}
