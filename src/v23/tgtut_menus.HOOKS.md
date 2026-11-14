# tgtut_menus.inc hooks (src/menu.c)

Data: run `python -X utf8 tools/tutorial_sprites.py "<arcade root>" data/extra.bin` (tested on a copy:
adds anims menu_p_tutorial_bunsin04 / 01_07 / 01_07a / 07 and sprites 0x18d4 0x18d5 0x18da 0x69..0x6d
0x18d8 0x18e3 0x18d6 0x18e2 0x18d7).

1. Include, right after the line that includes `v23/tutorial.inc`:

```c
#include "v23/tgtut_menus.inc"
```

2. Tutorial flag (integrator): set `tgtut_on = 1;` and call `tgtut_menu_reset();` when the first ghost
battle tutorial starts (isTargetFirstTutorial), `tgtut_on = 0;` when it ends.

3. Step dispatcher. Anchor (around line 11523):

```c
  if (m->screen == MENU_TG_SEARCH || m->screen == MENU_TG_LEVEL || m->screen == MENU_TG_AREA || m->screen == MENU_TG_RIVAL) {
    tga_init();
    if (m->screen == MENU_TG_SEARCH) tg_search_step(m, tap, key, back);
```

insert after `tga_init();` (this first occurrence, the step one, not the draw one):

```c
    if (tgtut_menu_step(m, tap, key, back)) return;
```

4. Draw. Anchor (around line 12215):

```c
      draw_tg_rival(m, clock, bars, 1, screen_w, screen_h);
      if (draw3d) draw3d(ctx);
    }
    if (!m->tgc_dec) hud_draw_menu_keys(screen_w, screen_h, m->key_held, 1, 1);
```

insert before the `if (!m->tgc_dec) hud_draw_menu_keys(...)` line:

```c
    tgtut_menu_draw(m);
```

5. Search: only Lv decides in the tutorial. In `tg_search_step`, anchor:

```c
    if (hit == m->tg_search) {
      /* the chosen one (the big plate): revenge and the name's need the arcade's server: a
```

insert right after `if (hit == m->tg_search) {`:

```c
      if (tgtut_search_refuse(m)) return;
```

(and the same line before any other decide path of tg_search_step, if there is a key one).

6. Optional: clSeqTgRivalDataDisp's window (0x305) has no port screen mapped; call
`tgtut_menu_show(TGT_DATA);` where the rival's data is shown (it is then stepped/drawn only while the
screen is one of MENU_TG_*; elsewhere call tgtut_menu_step/draw there too).
