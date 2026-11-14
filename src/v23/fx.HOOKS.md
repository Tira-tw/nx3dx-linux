# v2.3 fx hooks: aura rising glow + track fireworks

Files: `src/v23/auraglow.inc`, `src/v23/fireworks.inc`, `tools/stage/pack_fireworks.py`.
Both .inc files are static-only and include nothing project-specific: include them **after**
`gl.h`, `render.h` (and so `mathx.h`), `<zlib.h>`, `<stdio.h>`, `<stdlib.h>`.

## 1. Aura glow (aura.cg) — src/app.c

(a) Next to the other `#include "v23/..."` lines near the top of app.c (after `render.h`/`lights.h`):

```c
#include "v23/auraglow.inc"
```

(b) Above `menu_draw3d` (any file scope spot before it), the silhouette callback:

```c
typedef struct { wpk_scene *scene; mat4 vp, model; const float *eye; render_params *rp; } ag_car;
static void ag_draw_car(void *p) {
  ag_car *c = p;
  for (int pass = WPK_OPAQUE; pass <= WPK_CUTOUT; ++pass) wpk_draw(c->scene, &c->vp, &c->model, c->eye, pass, c->rp);
}
```

(c) In `menu_draw3d`, the aura loop (now app.c ~2931–2942), right **after**
`lights_draw_aura(&aproj, &aview, model.m, s->cars[i].aura - 1, s->cars[i].aura_level, (float)(now_ms() * 1e-3), &rp);`
and still inside the `for (int i ...)` loop, add:

```c
    {
      float lo[3], hi[3];
      if (ag_scene_box(scene[i], lo, hi)) {
        ag_car c = { scene[i], mat4_mul(aproj, aview), model, s->cars[i].cp ? cp_eye : s->eye, &rp };
        float d[3] = { c.eye[0] - model.m[12], c.eye[1] - model.m[13], c.eye[2] - model.m[14] }, k2 = model.m[0] * model.m[0] + model.m[1] * model.m[1] + model.m[2] * model.m[2];
        float eye[3] = { (d[0] * model.m[0] + d[1] * model.m[1] + d[2] * model.m[2]) / k2, (d[0] * model.m[4] + d[1] * model.m[5] + d[2] * model.m[6]) / k2, (d[0] * model.m[8] + d[1] * model.m[9] + d[2] * model.m[10]) / k2 };
        c.eye = eye;
        render_blend_pass(0);
        auraglow_draw(&aproj, &aview, model.m, lo, hi, s->cars[i].aura - 1, s->cars[i].aura_level, (float)(now_ms() * 1e-3), ag_draw_car, &c);
      }
    }
```

(the eye in the car's frame is the same sum the pass loop at ~2893 does; `vp` for a non-cp car
is `mat4_mul(proj, view)` there — use the same `vp` variable if it is still in scope.)

Notes / knobs:
- It saves/restores the bound framebuffer, viewport and depth write mask; leaves blend off,
  depth test on, VAO 0 and buffers 0 bound (as render.c's own composite does). If render.c caches
  GL state across draws, call `render_begin(&rp)` again after it (cheap) or move the call after
  `render_blend_pass(0)` at the end of `menu_draw3d`.
- Faithful aura.cg also tints the car body itself (lower half: alpha 15/8*0.25 ≈ 0.47 inside
  the silhouette). If that looks too strong, set `u_gain` (glUniform1f(AG.u_gain, ...)) lower.
- `AG_COLOUR` (aura colours) and `AG_SCALE` (Scale by level) are approximations: the arcade's
  runtime values were not located (`clMenu3dEnvironment::addAuraRender` 0x89603d8 has no
  direct caller).
- Costs one half-res FBO (RGBA8 + DEPTH16) and a re-draw of the car's opaque/cut-out passes
  with colour writes off. On FBO failure it silently disables itself (the flame shells stay).

## 2. Fireworks — race 3D pass

(a) Pack: `python tools/stage/pack_fireworks.py "<arcade root>" <data dir>/fireworks.bin`
(67 KB; 3 textures: the 256² atlas from fireworks01.igb, the 128² star and soft blob from
fireworks02.igb). Add it to the data packing step next to aura.bin.

(b) Include once in the race renderer's file (where the race's 3D sprites are drawn — the file
that calls `lights_draw*` / `render_sprites` for the race; app.c if unsure):

```c
#include "v23/fireworks.inc"
```

(c) Load path, where aura.bin's is set (app.c ~1516–1519):

```c
    port_path(ap, sizeof ap, "fireworks.bin");
    fw_path(ap);
```

(d) Each race frame (the update, once per frame):

```c
fw_update(fw_zone(area_id, metres_into_area), car_pos_world, car_forward_world, dt_seconds);
int n = fw_bursts(); /* play data/sound/race_se/hanabi1.wav or hanabi2.wav n times (ONESHOT_HANABI1/2) */
```

`area_id` is the arcade's EAreaId number of the player's current area and the distance its
yt::CSeq::GetInfoMyCarPlace gives (SInfoPlace: area, distance). If the port has no such ids,
pass `on` = "player on Tokyo's Shibaura / Tennouzu (Rainbow Bridge) stretch, night" instead —
table 0x93da324 = { 51: 250..2000, 50: 1000..2600, 70: 3200..6000, 72: 7700..9100 }; by the
enum string order 50/51 ≈ TkShibaura_Up / TkTennouzu_Up (unverified mapping).

(e) Draw, in the race's 3D pass after the opaque scene, with the other additive sprites
(same proj/view the street lights use, `render_params *rp` of that frame):

```c
fw_draw(&proj, &view, &rp);
```

Shells live 160 frames (launch 0–10, burst, hold to 40, fade to 160), as clFireWorkManager's
fireworks_Animation_updata; bursts 1500–3500 units ahead of the car, 700–1300 above it.

## auraglow.inc: the arcade never draws aura.cg (2026-11-09)

Checked in main: shader 13 (aura) is never built, clShaderObject::setFloat3 0x850f90c (Color)
has no callers, clMenu3dEnvironment::addAuraRender 0x89603d8 has no callers and draw 0x8960734
never draws its +0xb0 list. So `auraglow_draw` is now a no-op unless built with
`-DAURAGLOW_ENABLE`. The app.c block can stay as is (it only costs ag_scene_box per aura car).
Optional cleanup in `menu_draw3d`: drop the `/* (v2.3) aura.cg's rising glow ... */` block
after `lights_draw_aura(...)` (the `if (ag_scene_box(...)) { ... }`), keeping lights_draw_aura.

## fireworks.inc: checked against main (2026-11-09) — app.c change

The show is not placed by the car: clStageMiscObjectManager::awake puts it at a fixed world
spot per zone (0x8610700's table), shells at spot + (±800, 0..200, ±800), the burst 500 above;
the zones only start on the night (getDayTimeSelect() == 1); the end of a zone stops the
repeating for the rest of the race; the sounds are hanabi1 on every 3rd launch and hanabi2 on
every 3rd burst, at (10000 − distance to the spot)/8000 clamped 0.4..1, ×1.3. The old
`fw_zone` / `fw_update` / `fw_bursts` still build (night taken as on), but replace app.c's
block (~5490) with:

```c
    /* (v2.3) the fireworks (clStageEffectObjectManager / clFireWorkManager) */
    {
      int h1, h2;
      fw_step(G.car.race.place, G.car.race.place_at, G.track_zone == 1, G.car.pos);
      float v = fw_sounds(&h1, &h2);
      while (h1-- > 0) snd_effect("hanabi1", v);
      while (h2-- > 0) snd_effect("hanabi2", v);
    }
```

(`place_at` raw, no `* 0.001f`: fw_step compares it against the table × 1000 as the arcade does.)
And once at each race start (where the race's car / track state is reset), `fw_reset();` — the
arcade gets a new clStageMiscObjectManager (repeat flag on, nothing shown) per race.
