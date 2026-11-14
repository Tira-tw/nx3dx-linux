/* (v2.4) The ghost battle 1 vs 3 crowns: up to three ghosts raced at once (app.c includes this
 * once, right after tg_pick; it uses app.c's G, target_load / target_write_trail, model_color).
 *
 * What the arcade has (phys_rc.c, CSeq::Init 0x89895b5): yt::CSeq keeps its other cars' objects
 * in one array, 0x9802898 [0..2] (CLxRival / rival / CTarget, by mode; the vtable calls at
 * 0x8985666.. walk it). In the ghost battle (SConfigPlay mode 8) Init makes ONE yt::CTarget,
 * slot 0 (car 1), from SConfigPlay +0xcc's single trail file (CTarget::Init(this, 1, setting,
 * "/tmp/car0.trail", crown)); there is no second trail field and no loop: CSeq has no 1 vs 3.
 * The CTarget / CSampler / CGhost constructors are inlined in CSeq::Init (no ctor to call), and
 * the trail (CSampler::Load) is the arcade's own packed binary (a header then delta-coded
 * channels), so the extra two cannot simply be made in the emulated memory.
 *
 * The port's: ghost 1 stays the arcade's CTarget (rival 0, car 1, as before). Ghosts 2 and 3
 * are loaded here (tgm_pick), their trails written beside car0.trail (car1.trail, car2.trail)
 * for the replay. Until a decoder of the CSampler format is in (tgm_pose), they are not drawn
 * and their result is the area's level against ghost 1's (tgm_race_over). */

/* the menu's API (helper 16): ghosts 2..3, file path and area; n the count (1..3) */
static void tgm_set(int n, const char *const *paths, const int *areas) {
  G.tg_multi = n < 1 ? 1 : n > 3 ? 3 : n;
  G.tg_multi = 1; /* (v2.4) off: tgm_pose has no decoder yet (menu.c TGM_ENABLED 0) */
  for (int i = 1; i < 3; ++i) {
    G.tgm_path[i][0] = 0;
    G.tgm_area[i] = -1;
    if (i < G.tg_multi && paths && paths[i] && areas) {
      snprintf(G.tgm_path[i], sizeof G.tgm_path[i], "%s", paths[i]);
      G.tgm_area[i] = areas[i];
    }
  }
}

/* tg_pick's variant: after tg_pick (ghost 1), ghosts 2..tg_multi loaded with their trails
 * (save_dir/car<i>.trail); tgm_ok[i] each one's */
static void tgm_pick(void) {
  for (int i = 0; i < 3; ++i) G.tgm_result[i] = -1;
  G.tgm_ok[0] = G.tg_ok;
  for (int i = 1; i < 3; ++i) {
    target_free(&G.tgm_file[i]);
    G.tgm_ok[i] = 0;
    if (i >= G.tg_multi || !G.tgm_path[i][0]) continue;
    int a = G.tgm_area[i];
    if (a < 0 || a >= TARGET_AREAS || target_load(G.tgm_path[i], &G.tgm_file[i]) != 0) continue;
    if (!G.tgm_file[i].area[a].trail_size || !model_color(G.tgm_file[i].area[a].car)) {
      target_free(&G.tgm_file[i]);
      continue;
    }
    char trail[1200];
    snprintf(trail, sizeof trail, "%s/car%d.trail", G.save_dir, i);
    G.tgm_ok[i] = target_write_trail(&G.tgm_file[i], a, trail) == 0;
    fprintf(stderr, "ghost battle 1 vs 3: ghost %d %s area %d trail %u bytes -> %s\n", i + 1, G.tgm_path[i], a,
            G.tgm_file[i].area[a].trail_size, G.tgm_ok[i] ? trail : "NOT WRITTEN");
  }
}

/* ghost i's (1..2) pose at arcade frame `frame`: not yet (the CSampler trail's decoder) */
static int tgm_pose(int i, int frame, float body[16]) {
  (void)i, (void)frame, (void)body;
  return 0;
}

/* the race over: ghost 1's outcome from the arcade (won: beaten); ghosts 2..3 from their
 * levels against ghost 1's while they are not replayed. Returns 1 when all are beaten. */
static int tgm_race_over(int won1) {
  G.tgm_result[0] = won1 ? 1 : 0;
  int all = G.tgm_result[0];
  int l1 = G.tg_ok ? G.tg_file.area[G.tg_area].level : 0;
  for (int i = 1; i < G.tg_multi && i < 3; ++i) {
    if (!G.tgm_ok[i]) {
      G.tgm_result[i] = -1;
      continue;
    }
    int li = G.tgm_file[i].area[G.tgm_area[i]].level;
    G.tgm_result[i] = won1 && li <= l1 ? 1 : 0;
    all = all && G.tgm_result[i] == 1;
  }
  return all;
}
