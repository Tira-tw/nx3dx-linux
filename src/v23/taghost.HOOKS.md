# Time attack ghost (v2.3): hooks

Files: `src/v23/taghost.h` (state, file head, prototypes) and `src/v23/taghost.inc` (code).
Neither is compiled on its own: app.c includes both, so build.sh / build_android.sh / Xcode
need no new source file. Line numbers are those of app.c / render.c / vkgl.c as of 2026-11-09;
the anchor text is what to match.

How it works: during a time attack, every 2nd arcade frame (counted from `car_start`, so the
arcade pilot's rolling start lines the runs up frame for frame) the player's body matrix, the
tyres' steer, hub height and spin and the brake lamps are sampled. A goal with the traffic off
that is faster than the course's ghost replaces it. In the next time attack on that course the
ghost car (model, paint, aero, wing, mirror, bonnet, wheel and vinyl as it was raced) is drawn at
the same frame's pose after the scene's passes: a depth-only pass, then colour blended at
constant alpha 0.45 (fainter within 10 m of the player, since both start on the same spot,
and over its last 0.5 s). The physics never knows about it, so there is no collision. Glass and
lamp glows (the blend pass) are left out. Set the env var `WMMT_NO_TAGHOST` to turn it off.

The arcade had a TA ghost too (`clSeqTaGhostSelectThread`, `seqTaGhostSelect.cpp`,
`menu_p_ta_ghost_*.png`, "TA-Ghost info"), but its ghosts came from the server's ranking. The
arcade's own `CSampler` trail (`car_record_save`) only runs in the ghost battle mode (EGameMode 8,
`P.tg_race`), and `car_set_target` turns the race into a 12 km ramp battle. So this
feature records the port's own `car_state`, not the arcade's trail.

## Save file

`<save_dir>/taghost/courseNN.tgh` (NN = `menu_state.course`, 00..11). There is one file per course,
whatever the car, like the course record. It is gzip (zlib `gzopen "wb6"`), written to `.tmp`, then
renamed. Contents: `tagh_head` (taghost.h: "TAGH", version 1, course, `car_course_id`, ms, maker/car/
color, aero mirror wing bonnet wheel neon vinyl, power handling manual, step 2, n, time()),
then n x `tagh_sample` (100 bytes: 12 floats body, 4 steer, 4 hub, 4 rot, brake + 3 pad), raw
little-endian. That is about 3 KB/s before gzip, so a 5-minute run is about 0.9 MB raw. Loading
rejects any mismatch: magic, version, course, course id, step, or n outside 2..36000.

## app.c (8 hooks)

1. **State**, right after the G struct closes (line 369 `} G;`):
   ```c
   #include "v23/taghost.h"
   ```
2. **car_packs_trim** (line ~2585), after
   `for (int i = 0; (G.vs_race || G.tg_race) && i < G.n_rival; ++i) want[G.rival_pack[i]] = 1;`:
   ```c
     if (TAGH.pack >= 0) want[TAGH.pack] = 1; /* (v2.3) the time attack's ghost's car */
   ```
3. **car_paints_trim** (line ~2622), after
   `for (int i = 0; (G.vs_race || G.tg_race) && i < G.n_rival; ++i) keep[G.rival_pack[i]] = 1;`:
   ```c
     if (TAGH.pack >= 0) keep[TAGH.pack] = 1; /* (v2.3) the time attack's ghost's car */
   ```
4. **Race start**: in the `if (go) {` block (line ~4740), just BEFORE
   `    load_sync(NULL);` / `    /* the time attack's traffic (the course select's TRAFFIC key) */`.
   It must run inside the block's `load_pause()`, after the `tg` variable exists:
   ```c
       /* (v2.3) the time attack's ghost: its run loaded, its car in; this run recorded */
       tagh_race_start(!ten && !vs && !tg && G.menu.game == MENU_GAME_TA, course, power, handling);
   ```
   (Every race calls it. A race that is not a time attack clears the ghost and the recording.)
5. **Each arcade frame**: in the step loop (line 5413), right after `    car_update(&G.car, &in);`:
   ```c
       tagh_step(&G.car); /* (v2.3) */
   ```
6. **Race over**: in the time attack's `else` branch (line 5579), right after
   `      if (goal) records_goal(G.menu.course, race, &res, !G.race_traffic);`:
   ```c
         tagh_race_over(goal, race, G.race_traffic); /* (v2.3) the course's ghost */
   ```
7. **The functions**: just before `static void draw_race_cars(const car_state *c, ...` (line 4951),
   after `shadow_of` / `near_eye`:
   ```c
   #include "v23/taghost.inc"
   ```
8. **Draw**: in the race draw, right after the main pass loop closes (line 5711):
   ```c
       if (pass == WPK_CUTOUT) draw_shadows(&vp, &car_m);
       SEG_END(SEG_OPAQUE + pass);
     }
     if (!movie) tagh_draw(c, &vp, c->cam_pos, &G.rp); /* (v2.3) the time attack's ghost */   <-- add
   ```
   It is not drawn in the back mirror (`draw_backmirror`) and casts no shadow. tagh_draw
   leaves GL as the blend pass left it: blend on, depth mask off, SRC_ALPHA / ONE_MINUS_SRC_ALPHA.

## render.c / render.h (the alpha path)

wpk_draw sets its own `glBlendFunc` on every batch-flag change, so an outside `glBlendFunc`
does not survive it. The new path is a constant alpha:

- render.c, just above `void render_blend_pass(int on) {` (line 2076):
  ```c
  /* (v2.3 taghost) wpk_draw's batches blended by a constant alpha (the time attack's ghost car):
   * a, 0 < a < 1; 0 back to their own blending */
  static int const_alpha;
  void render_const_alpha(float a) {
    const_alpha = a > 0.0f && a < 1.0f;
    if (const_alpha) glBlendColor(0.0f, 0.0f, 0.0f, a);
  }
  ```
- render.c, in `wpk_draw` (line 2461), make the batch's blend func:
  ```c
          if (const_alpha) glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA); /* (v2.3 taghost) */
          else if (blend == 2) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
          else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  ```
- render.h, after `void render_blend_pass(int on);`:
  ```c
  /* (v2.3) wpk_draw's batches blended by constant alpha a (0 < a < 1; 0: as they are) */
  void render_const_alpha(float a);
  ```
  (taghost.inc declares the prototype itself too, which is harmless.)

Platforms: iOS GLES2 and Mesa EGL have `glBlendColor` / `GL_CONSTANT_ALPHA` natively. Windows already
loads `wmmt_glBlendColor` (win/gles_wgl.c:263). **Android Vulkan (vkgl) needs the four edits below.**
`test/glstub.h` has no glBlendColor, so add `static inline void glBlendColor(float r, float g, float b, float a) {}`
there if the syntax-check stub build includes render.c.

## vkgl.h / vkgl.c (Android, -DWMMT_VK)

- vkgl.h, in `VKGL_FUNCS` before the glBlendEquation line:
  `VKGL_FN(void, glBlendColor, (GLfloat, GLfloat, GLfloat, GLfloat))  \`
  and in the `#ifndef VKGL_IMPL` defines: `#define glBlendColor vkgl_glBlendColor`
- vkgl.c `vctx` (line ~609, after `int unpack, pack;`): `float balpha; /* glBlendColor's alpha */`
- vkgl.c after `vkgl_glBlendEquation` (line 1683):
  ```c
  void vkgl_glBlendColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    (void)r, (void)g, (void)b;
    ctx()->balpha = a < 0.0f ? 0.0f : a > 1.0f ? 1.0f : a;
  }
  ```
- vkgl.c `blend_factor` (line 1967), two more cases:
  ```c
    case GL_CONSTANT_ALPHA: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
    case GL_ONE_MINUS_CONSTANT_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
  ```
- vkgl.c `key_state` (line 2359), inside `if (c->blend) {` after the `k->beq = ...;` statement
  (the key's spare `pad[0]` carries the constant, so the pipeline cache keys on it):
  ```c
        if (c->bsrc == GL_CONSTANT_ALPHA || c->bdst == GL_ONE_MINUS_CONSTANT_ALPHA) k->pad[0] = (uint8_t)(c->balpha * 255.0f + 0.5f);
  ```
- vkgl.c `pipeline_make` (line 2040), after `bs.pAttachments = &cb;`:
  ```c
    for (int i = 0; i < 4; ++i) bs.blendConstants[i] = (float)k->pad[0] / 255.0f; /* (v2.3) */
  ```
  taghost.inc quantises the alpha to 1/16 steps, so the ghost makes only a handful of pipelines
  (each step x the car's programs). They are made on first use, which may cost a short hitch the
  first time the ghost fades.

## Checked

Copies of app.c (all 8 hooks applied), render.c/render.h and vkgl.c/vkgl.h with the hooks above
pass `clang -fsyntax-only -std=gnu99 -Wall` (NDK 27 clang, aarch64-android24) with
`-DWMMT_EGL -DWMMT_VK` and with `-DWMMT_EGL` alone. They gave no warnings. Nothing was built and nothing was run.

## Not done / ideas

- There is no menu switch (env var only) and no HUD gap to the ghost. A "GHOST ON/OFF" key on the course
  select could set a flag that `tagh_race_start`'s `ta` argument also requires.
- The ghost is machine-wide per course. To keep it per card, put `G.menu.save_slot` in `tagh_path`.
