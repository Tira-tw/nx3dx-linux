#include "anm.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "gl.h"

typedef struct {
  uint16_t sprite, blend; /* 0 alpha, 1 added, 2 subtracted (clDrawBuffer::draw's modes 1, 3) */
  float a, b, c, d, x, y;   /* screen = (x, y) + [a c; b d] * (u, v) on 640x480 */
  int16_t mul[4], add[4];   /* colour transform: c * mul / 256 + add / 255 */
} anm_rec;

typedef struct {
  char name[48];
  uint32_t nframes, nrecs;
  uint32_t (*frames)[2]; /* start, count */
  anm_rec *recs;
} anm_anim;

static struct {
  /* Sprites are uploaded when first drawn and dropped when unused for a while, so only
   * the screens on show take texture memory (menu.bin holds about 60 MB of them). */
  GLuint *tex;
  long *offset;      /* of each sprite's pixels in menu.bin */
  uint32_t *zsize;   /* their zlib-packed size, 0 = raw */
  uint32_t *used;    /* clock of the last draw */
  uint32_t clock;
  FILE *file;
  FILE *lang;        /* anm_load_lang's: the language's sprites over menu.bin's */
  FILE *extra;       /* anm_load_extra's: the port's own sprites added */
  uint8_t *in_lang;  /* the sprite's pixels are lang's (1) or extra's (2) */
  float (*size)[2];
  int *ids; /* the arcade's sprite id of each packed sprite */
  uint16_t (*cols)[2]; /* on a wide screen: its first and past-last columns with any opacity, once up */
  uint32_t nsprites, nanims;
  anm_anim *anims;
  GLuint prog;
  GLint u_mul, u_add, u_tex, u_view, u_off;
  float edge; /* anm_view */
  float off[2]; /* anm_offset */
  float alpha;  /* anm_alpha */
  uint32_t wide_clock; /* the clock when a frame last drew out to the screen's sides */
  int lite;            /* anm_lite */
} A;

void anm_lite(int on) { A.lite = on != 0; }

static const float SCALE = 1024.0f / 640.0f;

/* the program, with the 1024-wide layout in the middle of a screen anm_view widens */
static void use_prog(void) {
  glUseProgram(A.prog);
  glUniform2f(A.u_view, 1.0f / (512.0f + A.edge), A.edge / (512.0f + A.edge) - 1.0f);
  glUniform2f(A.u_off, A.off[0], A.off[1]);
}

void anm_view(float edge) { A.edge = edge; }
void anm_offset(float dx, float dy) { A.off[0] = dx, A.off[1] = dy; }
void anm_alpha(float a) { A.alpha = a < 0 ? 0.0f : a > 1 ? 1.0f : a; }
float anm_edge(void) { return A.edge; }

int anm_wide_drawn(void) { return A.edge > 0.0f && A.wide_clock == A.clock + 1; }

static int read_all(FILE *f, void *p, size_t n) { return fread(p, 1, n, f) == n; }

int anm_load(const char *data_dir) {
  static const char *vs =
    "attribute vec2 aPos; attribute vec2 aUV; varying vec2 vUV;\n"
    "uniform vec2 uView, uOff;\n"
    "void main() { vUV = aUV; vec2 p = aPos + uOff;\n"
    "  gl_Position = vec4(p.x * uView.x + uView.y, 1.0 - p.y / 384.0, 0.0, 1.0); }\n";
  static const char *fs =
    "precision mediump float; uniform sampler2D uTex; uniform vec4 uMul, uAdd; varying vec2 vUV;\n"
    "void main() { gl_FragColor = clamp(texture2D(uTex, vUV) * uMul + uAdd, 0.0, 1.0); }\n";
  const char *attribs[] = { "aPos", "aUV" };
  A.prog = gl_compile_program(vs, fs, attribs, 2);
  if (!A.prog) return -1;
  A.u_mul = glGetUniformLocation(A.prog, "uMul");
  A.u_add = glGetUniformLocation(A.prog, "uAdd");
  A.u_tex = glGetUniformLocation(A.prog, "uTex");
  A.u_view = glGetUniformLocation(A.prog, "uView");
  A.u_off = glGetUniformLocation(A.prog, "uOff");
  A.alpha = 1.0f;

  char path[1024];
  snprintf(path, sizeof path, "%s/menu.bin", data_dir);
  FILE *f = fopen(path, "rb");
  if (!f) return -1;
  char magic[4];
  if (!read_all(f, magic, 4) || memcmp(magic, "WMN3", 4) || !read_all(f, &A.nsprites, 4) || A.nsprites > 65535) goto fail;
  A.tex = calloc(A.nsprites, sizeof *A.tex);
  A.offset = calloc(A.nsprites, sizeof *A.offset);
  A.zsize = calloc(A.nsprites, sizeof *A.zsize);
  A.used = calloc(A.nsprites, sizeof *A.used);
  A.size = calloc(A.nsprites, sizeof *A.size);
  A.ids = calloc(A.nsprites, sizeof *A.ids);
  A.cols = calloc(A.nsprites, sizeof *A.cols);
  A.in_lang = calloc(A.nsprites, 1);
  for (uint32_t i = 0; i < A.nsprites; ++i) {
    uint16_t wh[2];
    if (!read_all(f, &A.ids[i], 4) || !read_all(f, wh, 4) || wh[0] > 2048 || wh[1] > 2048) goto fail;
    if (!read_all(f, &A.zsize[i], 4)) goto fail;
    A.offset[i] = ftell(f);
    if (fseek(f, A.zsize[i] ? (long)A.zsize[i] : (long)wh[0] * wh[1] * 4, SEEK_CUR) != 0) goto fail;
    A.size[i][0] = wh[0];
    A.size[i][1] = wh[1];
  }
  if (!read_all(f, &A.nanims, 4) || A.nanims > 4096) goto fail;
  A.anims = calloc(A.nanims, sizeof *A.anims);
  for (uint32_t i = 0; i < A.nanims; ++i) {
    anm_anim *an = &A.anims[i];
    if (!read_all(f, an->name, 48) || !read_all(f, &an->nframes, 4) || !read_all(f, &an->nrecs, 4)) goto fail;
    an->name[47] = 0;
    an->frames = malloc(an->nframes * sizeof *an->frames);
    an->recs = malloc(an->nrecs * sizeof *an->recs);
    if (!an->frames || !an->recs || !read_all(f, an->frames, an->nframes * sizeof *an->frames) ||
        !read_all(f, an->recs, an->nrecs * sizeof *an->recs))
      goto fail;
    for (uint32_t k = 0; k < an->nframes; ++k)
      if (an->frames[k][0] + an->frames[k][1] > an->nrecs) goto fail;
  }
  A.file = f;
  return 0;
fail:
  fprintf(stderr, "menu.bin is damaged\n");
  fclose(f);
  A.nanims = 0;
  return -1;
}

static int by_id(const void *a, const void *b) {
  int x = ((const int *)a)[0], y = ((const int *)b)[0];
  return x < y ? -1 : x > y;
}

int anm_load_lang(const char *path) {
  if (!A.file || A.lang) return -1;
  FILE *f = fopen(path, "rb");
  if (!f) return -1;
  char magic[4];
  uint32_t n;
  if (!read_all(f, magic, 4) || memcmp(magic, "WMN3", 4) || !read_all(f, &n, 4) || n > 65535) {
    fclose(f);
    return -1;
  }
  /* menu.bin's sprites by id, to find each one the language replaces */
  int (*order)[2] = malloc(A.nsprites * sizeof *order);
  if (!order) {
    fclose(f);
    return -1;
  }
  for (uint32_t i = 0; i < A.nsprites; ++i) order[i][0] = A.ids[i], order[i][1] = (int)i;
  qsort(order, A.nsprites, sizeof *order, by_id);
  int taken = 0, ok = 1;
  for (uint32_t k = 0; k < n && ok; ++k) {
    int id;
    uint16_t wh[2];
    uint32_t z;
    ok = read_all(f, &id, 4) && read_all(f, wh, 4) && wh[0] <= 2048 && wh[1] <= 2048 && read_all(f, &z, 4);
    if (!ok) break;
    long at = ftell(f);
    ok = fseek(f, z ? (long)z : (long)wh[0] * wh[1] * 4, SEEK_CUR) == 0;
    int key[2] = { id, 0 };
    const int(*hit)[2] = bsearch(key, order, A.nsprites, sizeof *order, by_id);
    if (!ok || !hit) continue;
    int i = (*hit)[1];
    if (A.tex[i]) glDeleteTextures(1, &A.tex[i]), A.tex[i] = 0;
    A.offset[i] = at, A.zsize[i] = z, A.in_lang[i] = 1;
    A.size[i][0] = wh[0], A.size[i][1] = wh[1];
    A.cols[i][0] = A.cols[i][1] = 0;
    taken++;
  }
  free(order);
  A.lang = f;
  fprintf(stderr, "%s: %d sprites%s\n", path, taken, ok ? "" : " (damaged after them)");
  return 0;
}

/* (v2.4) id `from` drawn as id `to`'s picture (src/v24/langspr.h LANGSPR_ALIAS) */
void anm_lang_alias(int from, int to) {
  int i = anm_sprite(from), j = anm_sprite(to);
  if (i < 0 || j < 0 || i == j) return;
  if (A.tex[i]) glDeleteTextures(1, &A.tex[i]), A.tex[i] = 0;
  A.offset[i] = A.offset[j], A.zsize[i] = A.zsize[j], A.in_lang[i] = A.in_lang[j];
  A.size[i][0] = A.size[j][0], A.size[i][1] = A.size[j][1];
  A.cols[i][0] = A.cols[i][1] = 0;
}

#define GROW(p, n) do { void *q_ = realloc((p), (size_t)(n) * sizeof *(p)); if (!q_) return -1; (p) = q_; } while (0)

int anm_load_extra(const char *path) {
  if (!A.file || A.extra) return -1;
  FILE *f = fopen(path, "rb");
  if (!f) return -1;
  char magic[4];
  uint32_t n;
  if (!read_all(f, magic, 4) || memcmp(magic, "WMN3", 4) || !read_all(f, &n, 4) || n > 4096) {
    fclose(f);
    return -1;
  }
  uint32_t total = A.nsprites + n;
  GROW(A.tex, total);
  GROW(A.offset, total);
  GROW(A.zsize, total);
  GROW(A.used, total);
  GROW(A.size, total);
  GROW(A.ids, total);
  GROW(A.cols, total);
  GROW(A.in_lang, total);
  int added = 0;
  for (uint32_t k = 0; k < n; ++k) {
    int id;
    uint16_t wh[2];
    uint32_t z;
    if (!read_all(f, &id, 4) || !read_all(f, wh, 4) || wh[0] > 2048 || wh[1] > 2048 || !read_all(f, &z, 4)) break;
    long at = ftell(f);
    if (fseek(f, z ? (long)z : (long)wh[0] * wh[1] * 4, SEEK_CUR) != 0) break;
    int i = anm_sprite(id);
    if (i >= 0 && id < 107000) continue; /* (menu.bin's own, an HD one maybe, kept) */
    if (i < 0) i = (int)A.nsprites++;
    else if (A.tex[i]) glDeleteTextures(1, &A.tex[i]);
    A.tex[i] = 0, A.used[i] = 0, A.ids[i] = id;
    A.offset[i] = at, A.zsize[i] = z, A.in_lang[i] = 2;
    A.size[i][0] = wh[0], A.size[i][1] = wh[1];
    A.cols[i][0] = A.cols[i][1] = 0;
    added++;
  }
  /* then its animations (tools/card_pages.py: the card check's pages), their records naming
   * the arcade's sprite ids: i32 sprite, u16 blend, f32 a, b, c, d, x, y, i16 mul[4], add[4];
   * a name menu.bin has already is skipped (anm_find finds menu.bin's) */
  uint32_t na = 0;
  int got = 0;
  if (read_all(f, &na, 4) && na > 0 && na < 1024) {
    GROW(A.anims, A.nanims + na);
    for (uint32_t k = 0; k < na; ++k) {
      anm_anim an;
      memset(&an, 0, sizeof an);
      if (!read_all(f, an.name, 48) || !read_all(f, &an.nframes, 4) || !read_all(f, &an.nrecs, 4) || an.nframes > 4096 ||
          an.nrecs > 65536)
        break;
      an.name[47] = 0;
      an.frames = malloc(an.nframes * sizeof *an.frames + 1);
      an.recs = malloc(an.nrecs * sizeof *an.recs + 1);
      int ok = an.frames && an.recs && read_all(f, an.frames, an.nframes * sizeof *an.frames);
      for (uint32_t r = 0; ok && r < an.nrecs; ++r) {
        unsigned char b[46];
        if (!read_all(f, b, 46)) { ok = 0; break; }
        int32_t sid;
        uint16_t blend;
        memcpy(&sid, b, 4);
        memcpy(&blend, b + 4, 2);
        anm_rec *rc = &an.recs[r];
        int ix = sid >= 0 ? anm_sprite(sid) : -1;
        rc->sprite = ix >= 0 ? (uint16_t)ix : 0xffff;
        rc->blend = blend;
        memcpy(&rc->a, b + 6, 24);
        memcpy(rc->mul, b + 30, 8);
        memcpy(rc->add, b + 38, 8);
      }
      for (uint32_t q = 0; ok && q < an.nframes; ++q)
        if (an.frames[q][0] + an.frames[q][1] > an.nrecs) ok = 0;
      if (!ok) {
        free(an.frames);
        free(an.recs);
        break;
      }
      if (anm_find(an.name) >= 0) {
        free(an.frames);
        free(an.recs);
        continue;
      }
      A.anims[A.nanims++] = an;
      got++;
    }
  }
  A.extra = f;
  fprintf(stderr, "%s: %d sprites, %d animations\n", path, added, got);
  return 0;
}

/* the sprite's texture, uploaded from menu.bin on first use (0 if it cannot be read) */
static GLuint texture(int i) {
  A.used[i] = A.clock;
  if (A.tex[i]) return A.tex[i];
  size_t n = (size_t)A.size[i][0] * (size_t)A.size[i][1] * 4;
  unsigned char *px = malloc(n), *z = A.zsize[i] ? malloc(A.zsize[i]) : NULL;
  uLongf got = (uLongf)n;
  FILE *f = A.in_lang[i] == 1 ? A.lang : A.in_lang[i] == 2 ? A.extra : A.file;
  int ok = px && fseek(f, A.offset[i], SEEK_SET) == 0 &&
           (A.zsize[i] ? z && read_all(f, z, A.zsize[i]) && uncompress(px, &got, z, A.zsize[i]) == Z_OK && got == n
                       : read_all(f, px, n));
  free(z);
  if (!ok) {
    free(px);
    return 0;
  }
  if (A.edge > 0.0f && !A.cols[i][1]) {
    int w = (int)A.size[i][0], h = (int)A.size[i][1], c0 = w, c1 = 0;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x)
        if (px[((size_t)y * w + x) * 4 + 3] > 8) {
          if (x < c0) c0 = x;
          if (x + 1 > c1) c1 = x + 1;
        }
    A.cols[i][0] = (uint16_t)(c0 < c1 ? c0 : 0), A.cols[i][1] = (uint16_t)(c0 < c1 ? c1 : w);
  }
  glGenTextures(1, &A.tex[i]);
  glBindTexture(GL_TEXTURE_2D, A.tex[i]);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  int opaque = A.lite && n >= 64 * 64 * 4;
  for (size_t k = 3; opaque && k < n; k += 4) opaque = px[k] == 255;
  if (opaque) {
    /* (RESOLUTION LITE) a sprite with no see-through texel (a screen's background, a
     * photo) as RGB 565: half the memory; the ones with alpha (the texts, the icons) as
     * they are, so the menus stay sharp */
    uint16_t *d = (uint16_t *)px;
    for (size_t k = 0, m = n / 4; k < m; ++k) {
      const unsigned char *p = px + k * 4;
      unsigned r = (p[0] * 31u + 127u) / 255u, g = (p[1] * 63u + 127u) / 255u, b = (p[2] * 31u + 127u) / 255u;
      d[k] = (uint16_t)(r << 11 | g << 5 | b);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, (GLsizei)A.size[i][0], (GLsizei)A.size[i][1], 0, GL_RGB,
                 GL_UNSIGNED_SHORT_5_6_5, px);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  } else
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)A.size[i][0], (GLsizei)A.size[i][1], 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, px);
  free(px);
  return A.tex[i];
}

void anm_trim(void) {
  A.clock++;
  if (A.clock % 60) return;
  for (uint32_t i = 0; i < A.nsprites; ++i)
    if (A.tex[i] && A.clock - A.used[i] > 180) {
      glDeleteTextures(1, &A.tex[i]);
      A.tex[i] = 0;
    }
}

/* the patched iOS SDK has no plain strcmp for armv7 */
static int same_name(const char *a, const char *b) {
  while (*a && *a == *b) a++, b++;
  return *a == *b;
}

void anm_preload(int anim) {
  if (anim < 0 || (uint32_t)anim >= A.nanims) return;
  const anm_anim *an = &A.anims[anim];
  for (uint32_t i = 0; i < an->nrecs; ++i)
    if (an->recs[i].sprite < A.nsprites) texture(an->recs[i].sprite);
}

int anm_warm_sprite(int sprite, int upload) {
  if (sprite < 0 || (uint32_t)sprite >= A.nsprites) return 0;
  if (A.tex[sprite]) {
    A.used[sprite] = A.clock;
    return 0;
  }
  if (!upload) return 0;
  use_prog();
  glUniform1i(A.u_tex, 0);
  glActiveTexture(GL_TEXTURE0);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glDisableVertexAttribArray(2);
  static const float v[16] = { 0, 0, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1, 1 };
  glUniform4f(A.u_mul, 1, 1, 1, 0);
  glUniform4f(A.u_add, 0, 0, 0, 0);
  GLuint tx_ = texture(sprite);
  if (!tx_) return 0; /* (not read: nothing, not texture 0's white on some GPUs) */
  glBindTexture(GL_TEXTURE_2D, tx_);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, v);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, v + 2);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  return 1;
}

int anm_sprite_ready(int sprite) { return sprite >= 0 && (uint32_t)sprite < A.nsprites && texture(sprite) != 0; }

int anm_sprites(int anim, int *out, int max) {
  if (anim < 0 || (uint32_t)anim >= A.nanims) return 0;
  const anm_anim *an = &A.anims[anim];
  int n = 0;
  for (uint32_t i = 0; i < an->nrecs; ++i) {
    int s = an->recs[i].sprite, seen = s >= (int)A.nsprites;
    for (int k = 0; k < n && !seen; ++k) seen = out[k] == s;
    if (!seen && n < max) out[n++] = s;
  }
  return n;
}

int anm_sprite_id(int sprite) { return sprite >= 0 && (uint32_t)sprite < A.nsprites ? A.ids[sprite] : -1; }

int anm_find(const char *name) {
  for (uint32_t i = 0; i < A.nanims; ++i)
    if (same_name(A.anims[i].name, name)) return (int)i;
  return -1;
}

int anm_frames(int anim) { return anim >= 0 && (uint32_t)anim < A.nanims ? (int)A.anims[anim].nframes : 0; }

/* a and b the same picture: menu.bin can hold an id twice (a font's sheet and its 32 x 32
 * template, both menu_f_28's 0x49), anm_sprite finding the first */
static int same_sprite(int a, int b) {
  return a == b || (a >= 0 && b >= 0 && (uint32_t)a < A.nsprites && (uint32_t)b < A.nsprites && A.ids[a] == A.ids[b]);
}

int anm_sprite(int arcade_id) {
  for (uint32_t i = 0; i < A.nsprites; ++i)
    if (A.ids[i] == arcade_id) return (int)i;
  return -1;
}

void anm_draw(int anim, int frame, float alpha) { anm_draw_ex(anim, frame, alpha, 0, 0, 0, 0); }

void anm_draw_ex(int anim, int frame, float alpha, float dx, float dy, const anm_swap *swap, int nswap) {
  anm_draw_xf(anim, frame, alpha, dx, dy, 1.0f, swap, nswap);
}

/* A screen wider than 4:3 (anm_view): the menus' bars go to its edges, as a 16:9 layout of the
 * arcade's would have them. The title bar (menu_p_title_bar, its lines all across) runs on from
 * both its ends; the bottom bar (menu_p_bottom_bar, a capsule wider than the screen, its
 * pattern two pixels across) takes its two ends out by the edge and its middle in between;
 * the time plate (menu_p_time_plate, cut by the 4:3's right edge) and what sits on it go
 * out to the screen's right. Records of a frame that turn or skew are drawn as they are. */
enum { WIDE_NONE, WIDE_RIGHT, WIDE_TITLE_BAR, WIDE_BOTTOM_BAR };

static int wide_kind(int sprite) {
  switch (A.ids[sprite]) {
  case 1908: case 1573: case 1905: case 1906: case 1907:
    return WIDE_RIGHT;
  case 1574:
    return WIDE_TITLE_BAR;
  case 1748:
    return WIDE_BOTTOM_BAR;
  default:
    return WIDE_NONE;
  }
}

/* the edge on the 640x480 screen */
static float wide_edge(void) { return A.edge / SCALE; }

/* how far a record of `sprite` (the frame's own, before any swap) moves across */
static float wide_dx(const anm_rec *r, int sprite) {
  if (A.edge <= 0.0f || r->b != 0.0f || r->c != 0.0f || sprite < 0 || (uint32_t)sprite >= A.nsprites) return 0.0f;
  return wide_kind(sprite) == WIDE_RIGHT ? wide_edge() : 0.0f;
}

/* columns u0..u1 of the sprite drawn where its columns d0..d1 would be, at opacity f */
typedef struct { float u0, u1, d0, d1, f; } wide_piece;
#define WIDE_FADE 24 /* steps of a picture's edge fading out, over half a side */
#define WIDE_PIECES (1 + 2 * WIDE_FADE)

/* plain: the frame drawn where it was laid out (no move or scale): one with a picture right
 * up to a 4:3 edge (a full-bleed photo or band) has its edge column drawn on out across the
 * side, fading (a mirror would show its text and lights backwards) */
static int wide_pieces(const anm_rec *r, int sprite, int plain, wide_piece pc[WIDE_PIECES]) {
  float w = A.size[sprite][0];
  pc[0] = (wide_piece){ 0, w, 0, w, 1 };
  int own = r->sprite;
  if (A.edge <= 0.0f || r->b != 0.0f || r->c != 0.0f || r->a <= 0.0f || own >= (int)A.nsprites || own != sprite)
    return 1;
  float e = wide_edge(), x0 = r->x, x1 = r->x + r->a * w;
  switch (wide_kind(own)) {
  case WIDE_RIGHT:
    pc[0].d0 += e / r->a, pc[0].d1 += e / r->a;
    return 1;
  case WIDE_TITLE_BAR: {
    A.wide_clock = A.clock + 1;
    /* on past each end at the 4:3's edge or beyond: lines from just in from its left end (as
     * bright as there; its middle is a shade darker), drawn out */
    int n = 1;
    if (x0 <= 0.5f) pc[n++] = (wide_piece){ 8, 24, (-e - r->x) / r->a, 4.0f / r->a, 1 }; /* (over its lit end) */
    if (x1 >= 640.0f - 64.0f) pc[n++] = (wide_piece){ 8, 24, w, w + (640.0f + e - x1) / r->a, 1 };
    return n;
  }
  case WIDE_BOTTOM_BAR: {
    A.wide_clock = A.clock + 1;
    /* an even shift keeps the two-pixel pattern unbroken where the pieces meet */
    float sh = 2.0f * floorf(e / 2.0f + 0.5f), c = w * 0.5f;
    if (sh > c * 0.4f) sh = c * 0.4f;
    pc[0] = (wide_piece){ 0, c, -sh, c - sh, 1 };
    pc[1] = (wide_piece){ c - sh, c + sh, c - sh, c + sh, 1 };
    pc[2] = (wide_piece){ c, w, c + sh, w + sh, 1 };
    return 3;
  }
  default: {
    if (!plain) return 1;
    if (r->a >= 4.0f && x0 <= 1.0f && x1 >= 639.0f) {
      /* a few texels stretched over the whole 4:3 (menu_p_bg_line, the attract's white and
       * black): over the whole screen */
      pc[0].d0 = (-e - r->x) / r->a, pc[0].d1 = (640.0f + e - r->x) / r->a;
      A.wide_clock = A.clock + 1;
      return 1;
    }
    /* by where its picture is in it (a 640-wide photo in a 1024 texture) */
    float u0 = A.cols[sprite][0], u1 = A.cols[sprite][1];
    if (r->a * (u1 - u0) < 560.0f) return 1; /* (a photo across the 4:3; a silhouette's would streak) */
    float c0 = r->x + r->a * u0, c1 = r->x + r->a * u1, k = 0.5f * e / r->a / WIDE_FADE;
    int n = 1;
    for (int i = 0; i < WIDE_FADE; ++i) {
      float f = 1.0f - ((float)i + 0.5f) / WIDE_FADE;
      /* a column a few in: the picture's own edge may be a lit line */
      if (fabsf(c0) < 4.0f) pc[n++] = (wide_piece){ u0 + 3.5f, u0 + 3.5f, u0 - k * (i + 1), u0 - k * i, f };
      if (fabsf(c1 - 640.0f) < 4.0f) pc[n++] = (wide_piece){ u1 - 3.5f, u1 - 3.5f, u1 + k * i, u1 + k * (i + 1), f };
    }
    return n;
  }
  }
}

void anm_draw_xf(int anim, int frame, float alpha, float dx, float dy, float zoom, const anm_swap *swap, int nswap) {
  anm_draw_xy(anim, frame, alpha, dx, dy, zoom, zoom, swap, nswap);
}

void anm_draw_xy(int anim, int frame, float alpha, float dx, float dy, float sx, float sy, const anm_swap *swap,
                 int nswap) {
  if (anim < 0 || (uint32_t)anim >= A.nanims) return;
  alpha *= A.alpha;
  const anm_anim *an = &A.anims[anim];
  if (frame < 0) frame = 0;
  if ((uint32_t)frame >= an->nframes) frame = (int)an->nframes - 1;
  use_prog();
  glUniform1i(A.u_tex, 0);
  glActiveTexture(GL_TEXTURE0);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  /* the 3D renderer leaves its buffers bound and a third attribute on */
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glDisableVertexAttribArray(2);
  const anm_rec *r = an->recs + an->frames[frame][0];
  int blend = 0, plain = dx == 0.0f && dy == 0.0f && sx == 1.0f && sy == 1.0f;
  for (uint32_t k = 0; k < an->frames[frame][1]; ++k, ++r) {
    int sprite = r->sprite;
    for (int i = 0; i < nswap; ++i)
      if (same_sprite(swap[i].from, sprite)) {
        sprite = swap[i].to; /* once: a template may be another's digit (TOTAL TIME's) */
        break;
      }
    if (sprite < 0 || (uint32_t)sprite >= A.nsprites) continue;
    float a = r->mul[3] / 256.0f * alpha;
    if (a <= 0.0f && r->add[3] <= 0) continue;
    float w = A.size[sprite][0];
    GLuint tx = texture(sprite); /* (first: its columns for wide_pieces) */
    if (!tx) continue; /* (not read: nothing, not texture 0's white on some GPUs) */
    wide_piece pc[WIDE_PIECES];
    int npc = wide_pieces(r, sprite, plain, pc);
    if (r->blend != blend) {
      /* the arcade's ONE, ONE: its fades scale the colour; here the alpha does */
      blend = r->blend;
      glBlendFunc(GL_SRC_ALPHA, blend ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
      glBlendEquation(blend == 2 ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD);
    }
    glUniform4f(A.u_mul, r->mul[0] / 256.0f, r->mul[1] / 256.0f, r->mul[2] / 256.0f, a);
    glUniform4f(A.u_add, r->add[0] / 255.0f, r->add[1] / 255.0f, r->add[2] / 255.0f, r->add[3] / 255.0f * alpha);
    glBindTexture(GL_TEXTURE_2D, tx);
    for (int p = 0; p < npc; ++p) {
      if (p && pc[p].f != pc[p - 1].f) {
        glUniform4f(A.u_mul, r->mul[0] / 256.0f, r->mul[1] / 256.0f, r->mul[2] / 256.0f, a * pc[p].f);
        glUniform4f(A.u_add, r->add[0] / 255.0f, r->add[1] / 255.0f, r->add[2] / 255.0f,
                    r->add[3] / 255.0f * alpha * pc[p].f);
      }
      float h = A.size[sprite][1], v[16];
      static const float corner[4][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } };
      for (int i = 0; i < 4; ++i) {
        float u = pc[p].u0 + corner[i][0] * (pc[p].u1 - pc[p].u0), t = corner[i][1] * h;
        float q = pc[p].d0 + corner[i][0] * (pc[p].d1 - pc[p].d0);
        v[i * 4 + 0] = (dx + sx * (r->x + r->a * q + r->c * t)) * SCALE;
        v[i * 4 + 1] = (dy + sy * (r->y + r->b * q + r->d * t)) * SCALE;
        v[i * 4 + 2] = u / w;
        v[i * 4 + 3] = corner[i][1];
      }
      glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, v);
      glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, v + 2);
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
  }
  if (blend) {
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);
  }
}

int anm_sprite_rect(int anim, int frame, int sprite, float r[4]) {
  float a;
  return anm_sprite_rect_at(anim, frame, sprite, 0, r, &a);
}

int anm_sprite_rect_at(int anim, int frame, int sprite, int nth, float r[4], float *alpha) {
  if (anim < 0 || (uint32_t)anim >= A.nanims || sprite < 0 || (uint32_t)sprite >= A.nsprites) return 0;
  const anm_anim *an = &A.anims[anim];
  if (frame < 0) frame = 0;
  if ((uint32_t)frame >= an->nframes) frame = (int)an->nframes - 1;
  const anm_rec *rec = an->recs + an->frames[frame][0];
  for (uint32_t k = 0; k < an->frames[frame][1]; ++k, ++rec) {
    if (!same_sprite(rec->sprite, sprite) || nth-- > 0) continue;
    *alpha = rec->mul[3] / 256.0f;
    float w = A.size[rec->sprite][0], h = A.size[rec->sprite][1];
    float xs[2] = { rec->x, rec->x + rec->a * w + rec->c * h };
    float ys[2] = { rec->y, rec->y + rec->b * w + rec->d * h };
    float ox = wide_dx(rec, rec->sprite);
    r[0] = fminf(xs[0], xs[1]) + ox;
    r[2] = fmaxf(xs[0], xs[1]) + ox;
    r[1] = fminf(ys[0], ys[1]);
    r[3] = fmaxf(ys[0], ys[1]);
    return 1;
  }
  return 0;
}

int anm_sprite_xf(int anim, int frame, int sprite, float xf[6], float mul[4], float add[4]) {
  if (anim < 0 || (uint32_t)anim >= A.nanims || sprite < 0) return 0;
  const anm_anim *an = &A.anims[anim];
  if (frame < 0) frame = 0;
  if ((uint32_t)frame >= an->nframes) frame = (int)an->nframes - 1;
  const anm_rec *rec = an->recs + an->frames[frame][0];
  for (uint32_t k = 0; k < an->frames[frame][1]; ++k, ++rec) {
    if (!same_sprite(rec->sprite, sprite)) continue;
    const float v[6] = { rec->x, rec->y, rec->a, rec->b, rec->c, rec->d };
    for (int i = 0; i < 6; ++i) xf[i] = v[i];
    for (int i = 0; i < 4; ++i) mul[i] = rec->mul[i] / 256.0f, add[i] = rec->add[i] / 255.0f;
    return 1;
  }
  return 0;
}

float anm_sprite_alpha(int anim, int frame, int sprite) {
  if (anim < 0 || (uint32_t)anim >= A.nanims || sprite < 0) return 0.0f;
  const anm_anim *an = &A.anims[anim];
  if (frame < 0) frame = 0;
  if ((uint32_t)frame >= an->nframes) frame = (int)an->nframes - 1;
  const anm_rec *rec = an->recs + an->frames[frame][0];
  for (uint32_t k = 0; k < an->frames[frame][1]; ++k, ++rec)
    if (same_sprite(rec->sprite, sprite)) return rec->mul[3] / 256.0f;
  return 0.0f;
}

float anm_sprite_width(int sprite) {
  return sprite >= 0 && (uint32_t)sprite < A.nsprites ? A.size[sprite][0] : 0.0f;
}

float anm_sprite_height(int sprite) {
  return sprite >= 0 && (uint32_t)sprite < A.nsprites ? A.size[sprite][1] : 0.0f;
}

void anm_draw_sprite(int sprite, float x, float y, float alpha) {
  if (sprite < 0 || (uint32_t)sprite >= A.nsprites || alpha <= 0.0f) return;
  use_prog();
  glUniform1i(A.u_tex, 0);
  glActiveTexture(GL_TEXTURE0);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glDisableVertexAttribArray(2);
  float x1 = x + A.size[sprite][0], y1 = y + A.size[sprite][1];
  const float v[16] = { x * SCALE, y * SCALE, 0, 0, x1 * SCALE, y * SCALE, 1, 0,
                        x * SCALE, y1 * SCALE, 0, 1, x1 * SCALE, y1 * SCALE, 1, 1 };
  glUniform4f(A.u_mul, 1, 1, 1, alpha * A.alpha);
  glUniform4f(A.u_add, 0, 0, 0, 0);
  GLuint tx_ = texture(sprite);
  if (!tx_) return; /* (not read: nothing, not texture 0's white on some GPUs) */
  glBindTexture(GL_TEXTURE_2D, tx_);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, v);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, v + 2);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void anm_draw_part(int sprite, const float src[4], const float dst[4], float alpha) {
  static const float add[4] = { 0, 0, 0, 0 };
  const float mul[4] = { 1, 1, 1, alpha };
  anm_draw_part_col(sprite, src, dst, mul, add);
}

void anm_draw_part_col(int sprite, const float src[4], const float dst[4], const float mul[4], const float add[4]) {
  if (sprite < 0 || (uint32_t)sprite >= A.nsprites || (mul[3] <= 0.0f && add[3] <= 0.0f)) return;
  use_prog();
  glUniform1i(A.u_tex, 0);
  glActiveTexture(GL_TEXTURE0);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glDisableVertexAttribArray(2);
  float u0 = src[0] / A.size[sprite][0], v0 = src[1] / A.size[sprite][1];
  float u1 = src[2] / A.size[sprite][0], v1 = src[3] / A.size[sprite][1];
  float x0 = dst[0] * SCALE, y0 = dst[1] * SCALE, x1 = dst[2] * SCALE, y1 = dst[3] * SCALE;
  const float v[16] = { x0, y0, u0, v0, x1, y0, u1, v0, x0, y1, u0, v1, x1, y1, u1, v1 };
  glUniform4f(A.u_mul, mul[0], mul[1], mul[2], mul[3] * A.alpha);
  glUniform4f(A.u_add, add[0], add[1], add[2], add[3]);
  GLuint tx_ = texture(sprite);
  if (!tx_) return; /* (not read: nothing, not texture 0's white on some GPUs) */
  glBindTexture(GL_TEXTURE_2D, tx_);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, v);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, v + 2);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void anm_fill(float r, float g, float b, float a) {
  static int white = -2;
  if (white == -2) white = anm_sprite(841); /* atract_p_white */
  if (white < 0 || a <= 0.0f) return;
  use_prog();
  glUniform1i(A.u_tex, 0);
  glActiveTexture(GL_TEXTURE0);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glDisableVertexAttribArray(2);
  /* sample the middle of the white sprite so its edges don't show */
  const float x0 = -A.edge, x1 = 1024.0f + A.edge;
  const float v[16] = { x0, 0, 0.5f, 0.5f, x1, 0, 0.5f, 0.5f, x0, 768, 0.5f, 0.5f, x1, 768, 0.5f, 0.5f };
  glUniform4f(A.u_mul, r, g, b, a);
  glUniform4f(A.u_add, 0, 0, 0, 0);
  glBindTexture(GL_TEXTURE_2D, texture(white));
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, v);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, v + 2);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}
