/* (v2.3) the aura's rising glow: data/shader/aura.cg (the shader table 0x9735ea8: aura.cg/.vp/.fp,
 * uniforms Time c8, Scale c9, Color c10) over a silhouette of the car drawn into a render target.
 *
 * aura.cg, as the arcade has it: the target holds the car's silhouette, anything not exactly
 * white (r != 1.0) being the car. A quad over the car samples it 8 times, and 7 more between
 * them; in the quad's upper half (uv.y > 0.5) each sample i is pulled toward the quad's centre
 * by (1, 1) - (0.01, 0.035) * i * ss, ss = (sin(Time * pi) + 1) * Scale, so the silhouette is
 * smeared outward and upward (light rising off the roof), the lower half is left as is.
 * power = hits / 8 (0 .. 15/8); out colour = Color, alpha = power * 0.25; added on (SRC_ALPHA, ONE).
 *
 * Here: the car is drawn (by the caller's callback, the usual wpk_draw passes) with its colour
 * off into a half-size target's depth, then a black quad at the far plane with GL_GREATER marks
 * every pixel the car covers, over a white one: the white-background silhouette aura.cg reads.
 * The quad: the screen rect of the car's box (lo/hi in its own space under frame), 2.5x wide and
 * 4x tall about its centre (room for the glow to rise); aura.cg's sampling done per fragment.
 *
 * Needs: gl.h, render.h (mat4 via mathx.h), math.h. Self-contained statics, prefix ag_.
 *
 * WHAT THE ARCADE ACTUALLY DOES (checked in main, 3DX+): aura.cg is never drawn. It is shader
 * table entry 13 (nsShaderDef table 0x9735ca0, 0x28 a row: entry 13 = 0x9735ea8, params Color
 * 0x9735ed0 / Time 0x9735ee0 / Scale 0x9735ef0), but
 *  - clShaderObject's ctors (by id 0x8512c90 / 0x8511f90, by header 0x85112a0 / 0x85105bc) are
 *    only ever built by clBloom, clRadialBlur, the movie player (4), clLightTrail (6, 7), clFlare
 *    (10, 11) and clGraphicFrame (12, neg) - never 13, and no data holds a pointer to them or to
 *    entry 13;
 *  - clShaderObject::setFloat3 0x850f90c (the only setter for Color, a float3) and setFloat2/4
 *    have no callers at all; setFloat1 only from clLightTrail / clGraphicFrame / clRadialBlur;
 *  - clMenu3dEnvironment::addAuraRender 0x89603d8 (pushes a clRenderScene on the vector at
 *    +0xb0) has no callers, direct or through a pointer; update/update_ (0x8961b64 / 0x8960600)
 *    begin/end that vector, but draw 0x8960734 only draws the +0xa4 one (addRender's).
 * So no Color / Scale value exists: the glow pass is a leftover of development. The arcade's
 * aura is car_aura_<colour>.igb plus car_aura_glow_<colour>.igb ("aura_glow", placed in
 * clMenuCar::setMatrix 0x86405dc / clMenuCarSep::setMatrix 0x86418da), i.e. lights_draw_aura.
 * Faithful default: auraglow_draw draws nothing. -DAURAGLOW_ENABLE keeps the reconstruction
 * (its colours / scales below are this port's own guesses, not the arcade's). */

#include <math.h>
#include <string.h>

#ifdef AURAGLOW_ENABLE

#ifndef GL_DEPTH_COMPONENT16
#define GL_DEPTH_COMPONENT16 0x81A5
#endif

/* nsMenuCheck::checkAura's colour order (0 green, 1 purple, 2 red, 3 blue); the arcade's Color
 * constant is set at run time (not found in the data), these follow the glow textures' tints */
static const float AG_COLOUR[4][3] = {
  { 0.30f, 1.00f, 0.40f }, { 0.75f, 0.35f, 1.00f }, { 1.00f, 0.25f, 0.15f }, { 0.25f, 0.55f, 1.00f },
};
/* aura.cg's Scale (c9) by level 0..4 (its caller was not found: tuned so the top sample's pull
 * reaches 1 - 0.035 * 7 * 2 * Scale, i.e. 0.75 .. 0.31 of the way at the pulse's peak) */
static const float AG_SCALE[5] = { 0.5f, 0.75f, 1.0f, 1.25f, 1.4f };

static struct {
  int tried, ok;
  GLuint prog, fill, fbo, tex, depth;
  int w, h;
  GLint u_sil, u_c, u_h, u_ss, u_col, u_gain, u_fill_col;
} AG;

static const char *const AG_VS =
  "attribute vec2 a_pos;\n"
  "attribute vec2 a_q;\n"
  "varying vec2 v_q;\n"
  "void main() { v_q = a_q; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";

static const char *const AG_FS =
  "precision mediump float;\n"
  "varying vec2 v_q;\n"
  "uniform sampler2D u_sil;\n"
  "uniform vec2 u_c, u_h;\n"
  "uniform float u_ss, u_gain;\n"
  "uniform vec3 u_col;\n"
  "float hit(vec2 p) { return texture2D(u_sil, u_c + p * 2.0 * u_h).r < 0.996 ? 0.125 : 0.0; }\n"
  "void main() {\n"
  "  vec2 uv[8];\n"
  "  for (int i = 0; i < 8; ++i)\n"
  "    uv[i] = v_q.y > 0.0 ? v_q * (vec2(1.0) - vec2(0.01, 0.035) * float(i) * u_ss) : v_q;\n"
  "  float power = 0.0;\n"
  "  for (int i = 0; i < 8; ++i) power += hit(uv[i]);\n"
  "  for (int i = 0; i < 7; ++i) power += hit(vec2(uv[i].x, (uv[i].y + uv[i + 1].y) * 0.5));\n"
  "  gl_FragColor = vec4(u_col, power * 0.25 * u_gain);\n"
  "}\n";

static const char *const AG_FILL_VS =
  "attribute vec2 a_pos;\n"
  "void main() { gl_Position = vec4(a_pos, 1.0, 1.0); }\n";
static const char *const AG_FILL_FS =
  "precision mediump float;\n"
  "uniform vec4 u_col;\n"
  "void main() { gl_FragColor = u_col; }\n";

static void ag_init(void) {
  AG.tried = 1;
  const char *attrs[] = { "a_pos", "a_q" };
  AG.prog = gl_compile_program(AG_VS, AG_FS, attrs, 2);
  AG.fill = gl_compile_program(AG_FILL_VS, AG_FILL_FS, attrs, 1);
  if (!AG.prog || !AG.fill) return;
  AG.u_sil = glGetUniformLocation(AG.prog, "u_sil");
  AG.u_c = glGetUniformLocation(AG.prog, "u_c");
  AG.u_h = glGetUniformLocation(AG.prog, "u_h");
  AG.u_ss = glGetUniformLocation(AG.prog, "u_ss");
  AG.u_col = glGetUniformLocation(AG.prog, "u_col");
  AG.u_gain = glGetUniformLocation(AG.prog, "u_gain");
  AG.u_fill_col = glGetUniformLocation(AG.fill, "u_col");
  AG.ok = 1;
}

/* the half-size target (remade when the screen's size changes) */
static int ag_target(int w, int h) {
  if (w < 16) w = 16;
  if (h < 16) h = 16;
  if (AG.fbo && AG.w == w && AG.h == h) return 1;
  if (AG.fbo) {
    glDeleteFramebuffers(1, &AG.fbo);
    glDeleteTextures(1, &AG.tex);
    glDeleteRenderbuffers(1, &AG.depth);
    AG.fbo = AG.tex = AG.depth = 0;
  }
  glGenTextures(1, &AG.tex);
  glBindTexture(GL_TEXTURE_2D, AG.tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glGenRenderbuffers(1, &AG.depth);
  glBindRenderbuffer(GL_RENDERBUFFER, AG.depth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, w, h);
  glGenFramebuffers(1, &AG.fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, AG.fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, AG.tex, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, AG.depth);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    AG.ok = 0; /* no glow on this GPU, the flame shells stay */
    return 0;
  }
  AG.w = w, AG.h = h;
  return 1;
}

static void ag_quad(GLuint prog, const float *xy, const float *q) {
  static const GLubyte idx[6] = { 0, 1, 2, 0, 2, 3 };
  glUseProgram(prog);
  for (GLuint a = 1; a < 8; ++a) glDisableVertexAttribArray(a); /* none left on from the scene's */
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, xy);
  if (q) {
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, q);
  }
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, idx);
  if (q) glDisableVertexAttribArray(1);
}

/* the car's box (lo, hi in its own space, under frame) on the screen, in clip space x0 y0 x1 y1;
 * 0 when it is (partly) behind the eye */
static int ag_rect(const mat4 *proj, const mat4 *view, const float *frame, const float lo[3], const float hi[3], float r[4]) {
  r[0] = r[1] = 1e9f, r[2] = r[3] = -1e9f;
  for (int k = 0; k < 8; ++k) {
    float s[3] = { k & 1 ? hi[0] : lo[0], k & 2 ? hi[1] : lo[1], k & 4 ? hi[2] : lo[2] }, p[3], e[3], c[4];
    for (int j = 0; j < 3; ++j) p[j] = frame[j] * s[0] + frame[4 + j] * s[1] + frame[8 + j] * s[2] + frame[12 + j];
    const float *m = view->m, *P = proj->m;
    for (int j = 0; j < 3; ++j) e[j] = m[j] * p[0] + m[4 + j] * p[1] + m[8 + j] * p[2] + m[12 + j];
    for (int j = 0; j < 4; ++j) c[j] = P[j] * e[0] + P[4 + j] * e[1] + P[8 + j] * e[2] + P[12 + j];
    if (c[3] <= 1e-3f) return 0;
    float x = c[0] / c[3], y = c[1] / c[3];
    if (x < r[0]) r[0] = x;
    if (y < r[1]) r[1] = y;
    if (x > r[2]) r[2] = x;
    if (y > r[3]) r[3] = y;
  }
  return r[2] > r[0] && r[3] > r[1];
}

#endif /* AURAGLOW_ENABLE */

/* a car scene's box in its own space (its chunks' boxes, the shadow's left out) */
static int ag_scene_box(const wpk_scene *s, float lo[3], float hi[3]) {
  int any = 0;
  for (uint32_t i = 0; s && i < s->n_chunk; ++i) {
    const wpk_chunk *c = &s->chunks[i];
    if (c->group == WPK_SHADOW || c->n == 0) continue;
    for (int j = 0; j < 3; ++j) {
      if (!any || c->lo[j] < lo[j]) lo[j] = c->lo[j];
      if (!any || c->hi[j] > hi[j]) hi[j] = c->hi[j];
    }
    any = 1;
  }
  return any;
}

/* The glow of one car's aura. Call where lights_draw_aura is called (menu_draw3d), after it, with
 * the same proj/view/frame. draw_car(ctx) draws the car (and its wheels) as the scene does, its
 * opaque and cut-out passes: it runs with the colour writes off, into the glow's own target.
 * colour 0..3, level 0..4 as lights_draw_aura; time in seconds. */
static void auraglow_draw(const mat4 *proj, const mat4 *view, const float *frame, const float lo[3], const float hi[3], int colour,
                          int level, float time, void (*draw_car)(void *ctx), void *ctx) {
#ifndef AURAGLOW_ENABLE
  /* the arcade never draws aura.cg (see the top): nothing */
  (void)proj, (void)view, (void)frame, (void)lo, (void)hi, (void)colour, (void)level, (void)time, (void)draw_car, (void)ctx;
#else
  if (colour < 0 || colour > 3 || level < 0 || level > 4 || !draw_car) return;
  if (!AG.tried) ag_init();
  if (!AG.ok) return;
  float r[4];
  if (!ag_rect(proj, view, frame, lo, hi, r)) return;
  if (r[2] < -1.5f || r[0] > 1.5f || r[3] < -1.5f || r[1] > 1.5f) return;

  GLint fb = 0, vp[4];
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
  glGetIntegerv(GL_VIEWPORT, vp);
  GLboolean depth_mask;
  glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
  if (!ag_target(vp[2] / 2, vp[3] / 2)) {
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fb);
    return;
  }

  /* 1: the silhouette: white, the car's depth (colour off), black where the car is */
  static const float full[8] = { -1, -1, 1, -1, 1, 1, -1, 1 };
  glBindFramebuffer(GL_FRAMEBUFFER, AG.fbo);
  glViewport(0, 0, AG.w, AG.h);
  glDepthMask(GL_TRUE);
  glClear(GL_DEPTH_BUFFER_BIT);
  glBindVertexArrayOES(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glUseProgram(AG.fill);
  glUniform4f(AG.u_fill_col, 1, 1, 1, 1);
  ag_quad(AG.fill, full, NULL);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
  draw_car(ctx);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glBindVertexArrayOES(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glDepthFunc(GL_GREATER); /* the far plane's quad (depth 1) behind the car's pixels alone */
  glUseProgram(AG.fill);
  glUniform4f(AG.u_fill_col, 0, 0, 0, 1);
  ag_quad(AG.fill, full, NULL);
  glDepthFunc(GL_LEQUAL);

  /* 2: aura.cg's quad over the car, added on in the aura's colour */
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fb);
  glViewport(vp[0], vp[1], vp[2], vp[3]);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  float cx = 0.5f * (r[0] + r[2]), cy = 0.5f * (r[1] + r[3]);
  float hx = 0.5f * (r[2] - r[0]) * 2.5f, hy = 0.5f * (r[3] - r[1]) * 4.0f;
  const float xy[8] = { cx - hx, cy - hy, cx + hx, cy - hy, cx + hx, cy + hy, cx - hx, cy + hy };
  static const float q[8] = { -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f };
  glUseProgram(AG.prog);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, AG.tex);
  glUniform1i(AG.u_sil, 0);
  glUniform2f(AG.u_c, 0.5f * cx + 0.5f, 0.5f * cy + 0.5f);
  glUniform2f(AG.u_h, 0.5f * hx, 0.5f * hy);
  glUniform1f(AG.u_ss, (sinf(time * 3.141592f) + 1.0f) * AG_SCALE[level]);
  glUniform3f(AG.u_col, AG_COLOUR[colour][0], AG_COLOUR[colour][1], AG_COLOUR[colour][2]);
  glUniform1f(AG.u_gain, 1.0f);
  ag_quad(AG.prog, xy, q);
  glDisableVertexAttribArray(0);

  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(depth_mask);
#endif
}
