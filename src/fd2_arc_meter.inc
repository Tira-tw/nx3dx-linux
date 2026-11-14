/* NX3DX+ FD2 arc HUD V3 (compact 80% tach arc). Real-time gauge, never a screenshot overlay.
 * Requires: hud_sprite, upload(), draw_k(), meter_car, meter_night,
 * CLUSTER_X/Y, HUD_K, meter_input()/car_kmh(), included before draw_meters.
 * Enabled by default for the independent Honda FD2 NORMAL meter.
 * The historic EVO IX preview remains opt-in via environment switches.
 */
#define FD2_ARC_SPRITES 5
static hud_sprite fd2_arc_sprites[FD2_ARC_SPRITES];
static int fd2_arc_load_state; /* 0 not loaded, 1 loaded, -1 missing/invalid */

static int fd2_arc_is_active(void) {
  const char *preview = getenv("WMMT_FD2_PREVIEW");
  const char *base = getenv("WMMT_FD2_BASE");
  const char *enabled = getenv("WMMT_FD2_ARC_METER");
  if (meter_night >= 2) return 0;
  /* Honda official: enabled by default; explicit 0 disables it. */
  if (meter_car == &METERDB[55]) return !enabled || strcmp(enabled, "0") != 0;
  /* Optional old EVO IX test, not the official Honda vehicle. */
  return meter_car == &METERDB[17] &&
         preview && strcmp(preview, "1") == 0 &&
         base && strcmp(base, "evo9") == 0 &&
         enabled && strcmp(enabled, "1") == 0;
}

static int fd2_arc_read(void *p, size_t len, FILE *f) {
  return fread(p, 1, len, f) == len;
}

static int fd2_arc_load(void) {
  if (fd2_arc_load_state) return fd2_arc_load_state == 1;
  FILE *f = fopen(fd2_arc_path, "rb");
  if (!f) {
    fprintf(stderr, "FD2 arc HUD: missing %s\n", fd2_arc_path);
    fd2_arc_load_state = -1;
    return 0;
  }
  char sig[4];
  unsigned count = 0;
  int ok = fd2_arc_read(sig, 4, f) && !memcmp(sig, "FD2H", 4) &&
           fd2_arc_read(&count, 4, f) && count == FD2_ARC_SPRITES;
  for (unsigned i = 0; ok && i < FD2_ARC_SPRITES; ++i) {
    unsigned w = 0, h = 0;
    if (!fd2_arc_read(&w, 4, f) || !fd2_arc_read(&h, 4, f) ||
        w < 1 || h < 1 || w > 512 || h > 512) { ok = 0; break; }
    size_t bytes = (size_t)w * h * 4;
    unsigned char *pixels = (unsigned char *)malloc(bytes);
    if (!pixels) { ok = 0; break; }
    if (!fd2_arc_read(pixels, bytes, f)) { free(pixels); ok = 0; break; }
    fd2_arc_sprites[i].tex = upload(pixels, w, h);
    fd2_arc_sprites[i].w = (float)w;
    fd2_arc_sprites[i].h = (float)h;
    free(pixels);
    if (!fd2_arc_sprites[i].tex) ok = 0;
  }
  fclose(f);
  if (!ok) {
    fprintf(stderr, "FD2 arc HUD: damaged/incompatible %s\n", fd2_arc_path);
    for (int i = 0; i < FD2_ARC_SPRITES; ++i) {
      if (fd2_arc_sprites[i].tex) glDeleteTextures(1, &fd2_arc_sprites[i].tex);
      memset(&fd2_arc_sprites[i], 0, sizeof fd2_arc_sprites[i]);
    }
    fd2_arc_load_state = -1;
    return 0;
  }
  fd2_arc_load_state = 1;
  return 1;
}

static void fd2_arc_draw(const car_state *car) {
  /* Two independent live instruments: a speed dial and a 37-step tach arc.
   * RPM input is the engine's normalized rev rate (0..1), not an invented
   * standalone RPM source. The 0..9 graphics are a visual scale only. */
  const float k = HUD_K; /* keep the live speedometer unchanged */
  const float arc_k = HUD_K * 0.80f; /* shrink only the large RPM arc and its 37 segments */
  const float dial_x = CLUSTER_X;
  const float dial_y = CLUSTER_Y + 7.0f * k;
  const float arc_x = CLUSTER_X - 142.0f * k;
  const float arc_y = CLUSTER_Y + 141.0f * k;
  glEnableVertexAttribArray(1);
  /* Wedge panel, then dynamic neon segments, then live speed dial and needle. */
  draw_k(&fd2_arc_sprites[1], arc_x, arc_y, 0, arc_k);
  float rev = car->rev_rate;
  if (!(rev >= 0.0f)) rev = 0.0f;
  if (rev > 1.0f) rev = 1.0f;
  int count = (int)(rev * 37.0f + 0.5f);
  for (int i = 0; i < count; ++i) {
    float t = ((float)i + 0.5f) / 37.0f;
    float s = sinf(1.35f * t), c = cosf(1.35f * t);
    float dx = 325.0f, dy = -149.85f * c;
    float inv = 1.0f / sqrtf(dx * dx + dy * dy);
    float nx = -dy * inv, ny = dx * inv;
    float px = 29.0f + 325.0f * t + 3.5f * nx;
    float py = 151.0f - 111.0f * s + 3.5f * ny;
    float angle = atan2f(dy, dx) * (180.0f / (float)M_PI);
    const hud_sprite *led = &fd2_arc_sprites[i >= 32 ? 4 : 3];
    draw_k(led, arc_x + (px - 185.0f) * arc_k,
          arc_y + (py - 90.0f) * arc_k, angle, arc_k);
  }
  draw_k(&fd2_arc_sprites[0], dial_x, dial_y, 0, k);
  float kmh = car_kmh(car);
  if (!(kmh >= 0.0f)) kmh = 0.0f;
  if (kmh > 320.0f) kmh = 320.0f;
  draw_k(&fd2_arc_sprites[2], dial_x, dial_y, -130.0f + 260.0f * kmh / 320.0f, k);
  glDisableVertexAttribArray(1);
}
