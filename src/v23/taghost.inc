/* (v2.3) The time attack's ghost: the functions (taghost.h has the state and the file's head).
 * app.c includes this once, just before draw_race_cars (it uses app.c's own statics: G,
 * car_pack, pack_load_now, menu_aero(_want), wheel_model, wheel_on, draw_wheels, vinyl_tex,
 * vinyl_on/off, eye_in). See taghost.HOOKS.md for the calls into it.
 *
 * - Recording: each arcade frame of a time attack (tagh_step after car_update) the player's car
 *   as the draw has it (body matrix, the tyres' steer, hub height and spin, the brake lamps),
 *   every TAGH_STEP frames; counted from car_start, so the rolling start (the arcade pilot's,
 *   the same every run) lines the runs up frame for frame.
 * - Keeping: a run to the goal with the traffic off (the records' rule) faster than the
 *   course's ghost replaces it: save_dir/taghost/courseNN.tgh (gzip: tagh_head, then the
 *   samples), one file a course whatever the car, as the course record is.
 * - Playback: the next time attack on that course draws the ghost's car (its model, paint and
 *   dress-up) at the same frame's pose, interpolated, after the scene's passes: its depth
 *   first (no colour), then its colour blended at a constant alpha (render_const_alpha:
 *   GL_CONSTANT_ALPHA), so it shows its own nearest surface, see-through. The physics never
 *   hears of it: no collision. It fades near the player's car and at its run's end. */

#include <math.h>

#ifndef GL_CONSTANT_ALPHA
#define GL_CONSTANT_ALPHA 0x8003
#define GL_ONE_MINUS_CONSTANT_ALPHA 0x8004
#endif

/* render.c (taghost.HOOKS.md): wpk_draw's batches blend by this constant alpha (0: as they are) */
void render_const_alpha(float a);

static void tagh_path(int course, char *out, size_t n) {
  snprintf(out, n, "%s/taghost", G.save_dir);
  mkdir(out, 0755); /* (win_compat.h: _mkdir) */
  snprintf(out, n, "%s/taghost/course%02d.tgh", G.save_dir, course);
}

static int tagh_color_ok(const tagh_head *h) {
  return h->maker >= 0 && h->maker < CARDB_ALL && h->car >= 0 && h->car < CARDB_NCARS[h->maker] && h->color >= 0 &&
         h->color < CARDB_CARS[h->maker][h->car].ncolors;
}

/* the course's ghost: its head, and with samples its run (malloc'd); 0 ok */
static int tagh_load(int course, tagh_head *h, tagh_sample **samples) {
  char path[1200];
  tagh_path(course, path, sizeof path);
  gzFile g = gzopen(path, "rb");
  if (!g) return -1;
  int ok = gzread(g, h, (unsigned)sizeof *h) == (int)sizeof *h && !memcmp(h->magic, "TAGH", 4) && h->version == TAGH_VERSION &&
           h->course == course && h->course_id == car_course_id(course) && h->step == TAGH_STEP && h->n > 1 &&
           h->n <= TAGH_MAX && h->ms > 0;
  if (ok && samples) {
    size_t want = (size_t)h->n * sizeof **samples;
    *samples = malloc(want);
    ok = *samples && gzread(g, *samples, (unsigned)want) == (int)want;
    if (!ok) free(*samples), *samples = NULL;
  }
  gzclose(g);
  return ok ? 0 : -1;
}

static int tagh_save(void) {
  char path[1200], tmp[1240];
  tagh_path(TAGH.course, path, sizeof path);
  snprintf(tmp, sizeof tmp, "%s.tmp", path);
  gzFile g = gzopen(tmp, "wb6");
  if (!g) return -1;
  size_t body = (size_t)TAGH.rn * sizeof *TAGH.rs;
  int ok = gzwrite(g, &TAGH.rh, (unsigned)sizeof TAGH.rh) == (int)sizeof TAGH.rh && gzwrite(g, TAGH.rs, (unsigned)body) == (int)body;
  ok = gzclose(g) == Z_OK && ok;
  if (ok && rename(tmp, path) != 0) {
    remove(path);
    ok = rename(tmp, path) == 0;
  }
  if (!ok) remove(tmp);
  return ok ? 0 : -1;
}

/* car_start's race is about to run: ta, a time attack (course: menu_state.course) on the
 * player's tune; its ghost loaded (its car's model in now, within the go block's load_pause),
 * the recording set up. Any other race: neither. */
static void tagh_race_start(int ta, int course, int power, int handling) {
  free(TAGH.gs);
  TAGH.gs = NULL;
  TAGH.play = TAGH.rec = TAGH.rn = TAGH.frame = TAGH.best_ms = 0;
  TAGH.pack = -1;
  if (!ta || course < 0 || course >= CAR_COURSES || getenv("WMMT_NO_TAGHOST")) return;
  TAGH.course = course;
  /* the run's head, the car as chosen now */
  tagh_head *h = &TAGH.rh;
  memset(h, 0, sizeof *h);
  memcpy(h->magic, "TAGH", 4);
  h->version = TAGH_VERSION;
  h->course = course;
  h->course_id = car_course_id(course);
  const menu_state *m = &G.menu;
  h->maker = m->maker, h->car = m->car, h->color = m->color;
  h->aero = m->aero[m->maker][m->car], h->mirror = m->mirror[m->maker][m->car], h->wing = m->wing[m->maker][m->car];
  h->bonnet = m->bonnet[m->maker][m->car], h->wheel = m->wheel[m->maker][m->car], h->neon = m->neon[m->maker][m->car];
  h->vinyl = m->vinyl[m->maker][m->car];
  h->power = power, h->handling = handling, h->manual = m->manual;
  h->step = TAGH_STEP;
  if (!TAGH.rs) {
    TAGH.rcap = 60 * 30 * 5; /* (5 min; more as it runs on) */
    TAGH.rs = malloc((size_t)TAGH.rcap * sizeof *TAGH.rs);
    if (!TAGH.rs) TAGH.rcap = 0;
  }
  TAGH.rec = TAGH.rs != NULL;
  /* the course's ghost */
  if (tagh_load(course, &TAGH.gh, &TAGH.gs) != 0) {
    fprintf(stderr, "ta ghost: course %d: none yet\n", course);
    return;
  }
  TAGH.best_ms = TAGH.gh.ms;
  const tagh_head *g = &TAGH.gh;
  if (!tagh_color_ok(g)) {
    fprintf(stderr, "ta ghost: course %d: its car (%d %d %d) unknown, not shown\n", course, g->maker, g->car, g->color);
    return;
  }
  const cardb_color *gc = &CARDB_CARS[g->maker][g->car].colors[g->color];
  TAGH.pack = gc->pack;
  TAGH.paint = gc->paint;
  pack_load_now(gc->pack, gc->paint != 0);
  if (g->aero > 0 && g->aero <= 3) menu_aero_want(gc->pack, g->aero);
  int wset;
  if (g->wheel > 0 && g->wheel < 256 && menu_wheel_pack(g->wheel, &wset)) wheel_model(g->wheel);
  TAGH.play = 1;
  fprintf(stderr, "ta ghost: course %d: %d'%02d\"%03d, %s colour %d, %d samples\n", course, g->ms / 60000, g->ms / 1000 % 60,
          g->ms % 1000, CARDB_CARS[g->maker][g->car].label, g->color, g->n);
}

/* after each car_update: the frame counted, the player's car sampled */
static void tagh_step(const car_state *c) {
  int f = TAGH.frame++;
  if (!TAGH.rec || f % TAGH_STEP) return;
  if (c->race.state == CAR_RACE_TIME_UP) { /* (no goal: nothing to keep) */
    TAGH.rec = 0;
    return;
  }
  if (TAGH.rn >= TAGH.rcap) {
    int cap = TAGH.rcap * 2 > TAGH_MAX ? TAGH_MAX : TAGH.rcap * 2;
    tagh_sample *s = cap > TAGH.rcap ? realloc(TAGH.rs, (size_t)cap * sizeof *s) : NULL;
    if (!s) { /* (too long a run, or no memory: this one not kept) */
      TAGH.rec = 0;
      return;
    }
    TAGH.rs = s, TAGH.rcap = cap;
  }
  tagh_sample *s = &TAGH.rs[TAGH.rn++];
  memset(s, 0, sizeof *s);
  static const int at[12] = { 0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14 };
  for (int i = 0; i < 12; ++i) s->body[i] = c->body[at[i]];
  for (int k = 0; k < 4; ++k) {
    const float *t = c->tyre[k];
    s->steer[k] = atan2f(t[8], t[10]); /* (its z column's turn about y) */
    s->hub[k] = t[13];
    s->rot[k] = c->tyre_rot[k];
  }
  s->brake = c->brake;
}

/* the race over (the time attack's branch): a run to the goal with the traffic off, faster
 * than the course's ghost, is the course's ghost from now on */
static void tagh_race_over(int goal, const car_race *race, int traffic) {
  if (!TAGH.rec) return;
  TAGH.rec = 0;
  int ms = (int)(race->total * 1000.0f); /* (as records_goal) */
  if (ms <= 0) ms = 1;
  if (!goal || traffic || TAGH.rn < 2) return;
  if (TAGH.best_ms && ms >= TAGH.best_ms) {
    fprintf(stderr, "ta ghost: %d ms, not under the ghost's %d ms\n", ms, TAGH.best_ms);
    return;
  }
  TAGH.rh.ms = ms;
  TAGH.rh.n = TAGH.rn;
  TAGH.rh.when = (long long)time(NULL);
  if (tagh_save() == 0) {
    fprintf(stderr, "ta ghost: course %d: new ghost, %d ms (was %d), %d samples\n", TAGH.course, ms, TAGH.best_ms, TAGH.rn);
    TAGH.best_ms = ms;
  } else {
    fprintf(stderr, "ta ghost: course %d: NOT saved\n", TAGH.course);
  }
}

static float tagh_dot(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void tagh_unit(float *a) {
  float n = sqrtf(tagh_dot(a, a));
  if (n > 1e-6f) a[0] /= n, a[1] /= n, a[2] /= n;
}

/* the ghost at arcade frame `frame`: its body matrix, tyres' postures and spins, brake lamps;
 * how far it is from its run's end (frames); 0 past it */
static int tagh_pose(int frame, float body[16], float tyre[4][16], float rot[4], int *brake, int *left) {
  int n = TAGH.gh.n;
  float s = (float)frame / (float)TAGH_STEP;
  int i = (int)s;
  if (!TAGH.gs || i >= n - 1) return 0;
  float t = s - (float)i;
  const tagh_sample *a = &TAGH.gs[i], *b = &TAGH.gs[i + 1];
  float v[12];
  for (int k = 0; k < 12; ++k) v[k] = a->body[k] + (b->body[k] - a->body[k]) * t;
  /* the rotation's columns made orthonormal again (Gram-Schmidt from z: its handedness kept) */
  float *x = v, *y = v + 3, *z = v + 6;
  tagh_unit(z);
  float yz = tagh_dot(y, z);
  for (int k = 0; k < 3; ++k) y[k] -= yz * z[k];
  tagh_unit(y);
  float xz = tagh_dot(x, z), xy = tagh_dot(x, y);
  for (int k = 0; k < 3; ++k) x[k] -= xz * z[k] + xy * y[k];
  tagh_unit(x);
  memset(body, 0, 16 * sizeof *body);
  for (int k = 0; k < 3; ++k) body[k] = x[k], body[4 + k] = y[k], body[8 + k] = z[k], body[12 + k] = v[9 + k];
  body[15] = 1.0f;
  for (int k = 0; k < 4; ++k) {
    float d = b->steer[k] - a->steer[k];
    if (d > 3.14159265f) d -= 6.28318531f;
    if (d < -3.14159265f) d += 6.28318531f;
    float st = a->steer[k] + d * t, sn = sinf(st), cs = cosf(st);
    float *w = tyre[k];
    memset(w, 0, 16 * sizeof *w);
    /* its posture: the steer about y, the hub's height (x and z: wpk_draw takes the model's) */
    w[0] = cs, w[2] = -sn, w[5] = 1.0f, w[8] = sn, w[10] = cs;
    w[13] = a->hub[k] + (b->hub[k] - a->hub[k]) * t;
    w[15] = 1.0f;
    rot[k] = t < 0.5f ? a->rot[k] : b->rot[k]; /* (a spin is too fast to blend at 30 Hz) */
  }
  *brake = t < 0.5f ? a->brake : b->brake;
  *left = (n - 1) * TAGH_STEP - frame;
  return 1;
}

/* the ghost's car, after the scene's passes (blending on, depth not written, as the blend pass
 * leaves them; so again after it) */
static void tagh_draw(const car_state *c, const mat4 *vp, const float *eye_pos, render_params *rp) {
  if (!TAGH.play || TAGH.pack < 0) return;
  float body[16], tyre[4][16], rot[4];
  int brake, left;
  /* (the player shows the pose after update N-1's sample: the same frame of the ghost) */
  if (TAGH.frame < 1 || !tagh_pose(TAGH.frame - 1, body, tyre, rot, &brake, &left)) return;
  wpk_scene *model = car_pack(TAGH.pack);
  if (!model) return;
  /* fainter close to the player's car (both start on the same spot) and at the run's end */
  float d[3] = { body[12] - c->body[12], body[13] - c->body[13], body[14] - c->body[14] };
  float dist = sqrtf(tagh_dot(d, d)), apart = (dist - 25.0f) / 75.0f; /* (display units: 2.5 .. 10 m) */
  float a = TAGH_ALPHA * (apart < 0.3f ? 0.3f : apart > 1.0f ? 1.0f : apart);
  if (left < 30) a *= (float)left / 30.0f;
  a = floorf(a * 16.0f + 0.5f) / 16.0f; /* (a few steps: vkgl bakes it into its pipelines) */
  if (a <= 0.0f) return;
  mat4 m;
  memcpy(m.m, body, sizeof m.m);
  float eye[3];
  eye_in(&m, eye_pos, eye);
  const tagh_head *h = &TAGH.gh;
  render_params q = *rp;
  q.wheels = (const float (*)[16])tyre;
  q.wheel_rot = rot;
  q.braking = brake;
  q.over = 0;
  wpk_paint(model, TAGH.paint); /* (the player's draw sets its own each frame: a shared model) */
  wpk_scene *aero = h->aero > 0 && h->aero <= 3 ? menu_aero(TAGH.pack, h->aero) : NULL;
  if (aero) wpk_share_sync(aero), model = aero;
  int set = aero ? h->aero : 0;
  q.mirror = h->mirror, q.wing = h->wing, q.bonnet = h->bonnet;
  q.wheel_custom = wheel_on(h->wheel);
  const char *page1 = "";
  uint32_t vt = vinyl_tex(TAGH.pack, set, TAGH.paint, h->vinyl, h->bonnet, &page1), vold = 0;
  int vi = vinyl_on(model, vt, page1, &vold);
  /* its depth alone first (its nearest surface), then its colour over what is there */
  for (int k = 0; k < 2; ++k) {
    if (!k) {
      glDisable(GL_BLEND);
      glDepthMask(GL_TRUE);
      glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    } else {
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      glDepthMask(GL_FALSE);
      glEnable(GL_BLEND);
      render_const_alpha(a);
    }
    /* (its glass and lamp glows, the blend pass, left out: a ghost's windows are open) */
    for (int pass = WPK_OPAQUE; pass <= WPK_CUTOUT; ++pass) {
      wpk_draw(model, vp, &m, eye, pass, &q);
      draw_wheels(h->wheel, TAGH.pack, set, vp, &m, eye, pass, &q);
    }
  }
  render_const_alpha(0.0f);
  vinyl_off(model, vi, vold);
  /* as the blend pass had it */
  glEnable(GL_BLEND);
  glDepthMask(GL_FALSE);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  rp->draws = q.draws, rp->triangles = q.triangles, rp->new_chunks = q.new_chunks;
}
