# v2.3 ghost battle tutorial FLOW — hooks (src/v23/tgtut_flow.inc)

Arcade order (mseqTarget, verified): ghost disc → [first play] BGM sys_03 loop → game_1 → game_2
→ bunsin00 → **the normal ghost battle menus** (search type; level select / course select / day
time are SKIPPED, area forced to clEffAreaType 4, random ghost of the player's rank there) →
**2.8 km tutorial race** (cons_p_tutorial looped in the HUD, meter rival name "チュートリアル") →
result → (no coins / lv up / title / mileage) → bunsin09 (r35) → bunsin00a → BGM sys_02_sel →
**back to the ghost battle's first menu page** for the real game.

## 0. Data
`python -X utf8 tools/tutorial_sprites.py "<arcade root>" data/extra.bin` — now also adds
menu_p_tutorial_bunsin_txt and cons_p_tutorial (tested on a copy: +3 sprites, 2 anims).

## 1. menu.c
a. After `#include "v23/tutorial.inc" /* (v2.3) the first-time tutorials */` add:
```c
#include "v23/tgtut_flow.inc" /* (v2.3) the ghost battle tutorial's flow */
```
b. Ghost disc (the block after `tg_search_enter(m);` ~line 11917): replace
`tut_open(TUT_TG, has(CARDB_CARS[m->maker][m->car].label, "[R35]"))` with
`tgtut_begin(has(CARDB_CARS[m->maker][m->car].label, "[R35]"))` (keep the seen-bit set: set it
only when tgtut_done() fires instead, if you want an aborted tutorial to repeat).

c. step(): replace
```c
  if (tut_active()) { /* (v2.3) a first-time tutorial takes the input */
    tut_step(tap || key);
    return;
  }
```
with
```c
  if (tgtut_step()) { /* (v2.3) the tutorial over: back to the ghost battle's first page */ }
  if (tgtut_done()) { m->screen = MENU_TG_SEARCH; tg_search_enter(m); }
  if (tut_active()) { /* (v2.3) a first-time tutorial takes the input */
    tut_step(tap || key);
    return;
  }
```
(tgtut_step must run every frame, also while a card is up; MENU_TG_SEARCH/tg_search_enter are the
entry the ghost disc uses — use whatever that block does.)

d. menu_bgm(): replace `if (tut_active() && tut_ghost()) return MUSIC_TUTORIAL; /* (v2.3) */` with
```c
  if ((tut_active() && tut_ghost()) || tgtut_music()) return MUSIC_TUTORIAL; /* (v2.3) BGM 0x29 till the End card */
```
(After it the normal `MUSIC_SELECT` = 0x5f sys_02_sel follows by itself.)

e. Menu skips while `tgtut_on()` (seqTgLevelSelect / seqTgCourseSelect / seqStdDayTimeSelect have
no screen in the tutorial): wherever the ghost battle goes from the search page to MENU_TG_LEVEL /
MENU_TG_AREA (the area page) / the day-time cards, when `tgtut_on()` set `m->tg_level` to the
card's level, `m->tg_area = TGTUT_AREA; m->tg_pick = -1;` (app.c tg_pick then takes a random ghost
— ideally of area 4) and go straight on to the rival data / race. Coordinate with
tgtut_menus.inc (it draws bunsin04/07/08 and may draw the bunsin_txt banner itself; if not, add
`tgtut_draw_banner(m->t);` in menu_draw() just before `tut_draw();`).

f. The race starting (where the ghost battle race is launched from the menu, MENU_PRE → racing):
`tgtut_race_start();`

## 2. app.c
a. car_set_target (~line 4781, `car_set_target(&g, G.save_dir);`): before it add
`g.tutorial = menu_tgtut_racing();` — expose from menu.c (after the include):
`int menu_tgtut_racing(void) { return tgtut_racing(); }` plus same-style wrappers
`menu_tgtut_after_race`, `menu_tgtut_draw_race(int t)`, declared in menu.h. car.c then runs 2.8 km.
b. tg_pick(): when `menu_tgtut_racing()`, prefer `with[]` entries whose area == 4 (TGTUT_AREA).
c. After the ghost battle race's result (where the ghost battle goes to the coins / lv up /
title / mileage screens, around the crown_get code ~line 5604): if `menu_tgtut_after_race()`
(menu.c wrapper of tgtut_after_race) skip those screens AND don't count the play (no tg_plays++,
no own-run save at line ~3332 — the arcade's tutorial race is free and jumps back to the menu).

## 3. hud.c (in-race)
In the ghost battle HUD draw, last: `menu_tgtut_draw_race(frame)` (wrapper of tgtut_draw_race) —
cons_p_tutorial looped for the whole race. Meter: when tutorial, the rival name text is
"チュートリアル" (clMeterConsole::setTgTutorial, 0x93d2b8c) and the distance text blank.

## Not done / uncertain
* clEffAreaType 4's name in the port's area table was not checked (the port's G.tg_area indexes
  the same 0..7 enum, so area 4 = the same index).
* clRaceResultConsole::drawTgTutorial (a clMenuFade + 5 anims over the result) not decoded.
* Which of seqTgSearchTypeSelect's options is forced in the tutorial (three flags set 1, 30 s) not decoded.
