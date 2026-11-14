# v2.3 first-time tutorials — hooks

`src/v23/tutorial.inc` plays the arcade's first-time tutorials over the menus:

* **Ghost battle (分身対戦チュートリアル)** — `TUT_TG`: menu_layout_tutorial_game_1 → _game_2 →
  menu_p_tutorial_bunsin09 (bunsin09_r35 for an R35) → menu_p_tutorial_bunsin00a. Shown the first
  time a save enters the ghost battle.
* **Dress-up (ドレスアップチュートリアル)** — `TUT_DRESS`: menu_layout_tutorial_dressup (600 frames).
  Shown the first time a save opens the dress-up menu.

A tap / OK (the arcade's accelerator, `chkSW(0x10)`, "アクセルでスキップ") skips: in the two
intro animations and in the dress-up one it ends the scene at once; in the bunsin cards it only
works after the card's intro (frame 0x18 / 0x3b), and the parts card then plays its 10-frame
exit. Each card also ends by itself after 1800 frames (30 s). The arcade evidence is in the
comment at the top of tutorial.inc.

## 0. Data

```
python tools/tutorial_sprites.py "F:\Wangan Midnight Maximum Tune 3DX+ (Japan)" data/extra.bin
```

This adds 7 animations and 48 sprites (+3.7 MB; extra.bin 9.38 → 13.08 MB). It was tested on a
scratch copy, and a second run adds nothing. If extra.bin has no tutorial animations,
`tut_open` returns 0 and nothing happens.

## 1. menu.h — the per-save seen bits

Add this to `menu_state`, next to `tg_hist_n, tg_plays, tg_wins` (line ~267):

```c
  unsigned tut_seen; /* (v2.3) the first-time tutorials seen on this save (tutorial.inc TUT_SEEN_*) */
```

## 2. menu.c

**a. include**: put this right after `#define TG_FADE 10` (line ~566). It has to come before
`tg_select_open` and `step`, and it only needs anm.h, sound.h and string.h, which are already
included:

```c
#include "v23/tutorial.inc" /* (v2.3) the first-time tutorials */
```

**b. step()**: put this right after `if (m->bars && ++m->bars > 11) m->bars = 0;` (line ~11214),
before `if (in_attract(m))`. While a tutorial is up it takes all the input, and the screen under
it waits:

```c
  if (tut_active()) {
    tut_step(tap || key);
    return;
  }
```

**c. Ghost battle entry**: this is the versus select's ghost disc (`m->game = MENU_GAME_TG; ...
tg_search_enter(m);`, line ~11874). Right after that `tg_search_enter(m);` add:

```c
          if (tut_should_show(m->tut_seen, TUT_TG, 0) &&
              tut_open(TUT_TG, has(CARDB_CARS[m->maker][m->car].label, "[R35]")))
            m->tut_seen |= TUT_SEEN_TG;
```

(The arcade's condition is that the card has no ghost battle played yet (before-mode card
+0x1a4 == 0) and `clWorkSpace::isTargetFirstPlay`. `m->tg_plays` is only filled for the card
check's car (app.c card_info), so pass 0, which leaves the seen bit as the only check. If it is
known to be valid there, pass `m->tg_plays`.)

**d. Dress-up menu**: at the end of `static void tg_select_open(menu_state *m)` (line ~8235),
after `m->music = 1;`:

```c
  if (tut_should_show(m->tut_seen, TUT_DRESS, 0) && tut_open(TUT_DRESS, 0)) m->tut_seen |= TUT_SEEN_DRESS;
```

(`tg_select_open` is also called when coming back from the part screens. The seen bit means the
tutorial only shows once.)

**e. menu_draw()**: put this last, after `if (!in_attract(m)) draw_buyin(m, m->t);` (line ~12030):

```c
  tut_draw(); /* (v2.3) a first-time tutorial over everything */
```

## 3. app.c — keep the bits per save

Add the function next to `card_info` (line ~3338) and call it once a frame next to
`card_info();` (line ~4488). It uses one small file per save slot, `<save_dir>/tutorial<slot>`:

```c
/* (v2.3) the first-time tutorials seen (menu_state.tut_seen), one file a save slot */
static void tut_seen_sync(void) {
  static int slot = -1;
  static unsigned written;
  menu_state *m = &G.menu;
  char path[1100];
  if (m->save_slot != slot) {
    slot = m->save_slot;
    m->tut_seen = 0;
    snprintf(path, sizeof path, "%s/tutorial%d", G.save_dir, slot);
    FILE *f = slot > 0 ? fopen(path, "r") : NULL;
    if (f) {
      if (fscanf(f, "%u", &m->tut_seen) != 1) m->tut_seen = 0;
      fclose(f);
    }
    written = m->tut_seen;
  } else if (m->tut_seen != written && slot > 0) {
    snprintf(path, sizeof path, "%s/tutorial%d", G.save_dir, slot);
    FILE *f = fopen(path, "w");
    if (f) fprintf(f, "%u\n", m->tut_seen), fclose(f);
    written = m->tut_seen;
  }
}
```

(With save slot 0, which means no save, `tut_seen` stays 0 and is not written, so the tutorial
shows each time. If the saves are erased, also remove `tutorial<slot>`.)

## Notes / options

* **BGM**: during the ghost battle tutorial the arcade plays BGM 0x29 (looped), and afterwards
  0x5f. This is not hooked here.
* **The tutorial race**: the arcade runs a 2.8 km tutorial race between `seqTgTutorialStart`
  (bunsin00) and `seqTgTutorialDressupInfo` (car.c already has `P.tg.tutorial` /
  SConfigPlay +0xd1). To have it, open `TUT_TG_INTRO` and then `TUT_TG_START` before the race,
  and `TUT_TG_INFO` and then `TUT_TG_END` after it. The race HUD's 分身対戦体験中 overlays
  (menu_p_tutorial_bunsin_txt / _txt2, clTgTutorialExplain, clMeterConsole::setTgTutorial) are
  not packed by tutorial_sprites.py.
* **Language**: these sprites are Japanese only. The arcade has no sprite-us / sprite-cn copies
  of menu_p_tutorial_*.
* **Dress-up tutorial**: `seqTgTutorialDressUp` is not called anywhere in the retail flow; only
  a pointer to it exists (the debug sequence table). Showing it on the first dress-up menu is the
  port's choice.
