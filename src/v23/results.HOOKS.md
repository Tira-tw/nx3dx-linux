# v2.3 result screens: MILEAGE and 10-in-a-row STAGE CLEAR — where to hook them in

Files: `src/v23/mileage.inc`, `src/v23/tenclear.inc` (static code + two public arm functions,
compiled inside menu.c's translation unit), `tools/result_sprites.py` (sprites/anims into extra.bin).
Nothing here has been built yet.

## Arcade evidence (main, 32-bit ELF)

| Screen | Seq / class | Called from | Condition |
|---|---|---|---|
| Mileage | `seqMileageResult` 0x878920c, `clSeqMileageResultThread` (ctor 0x878782c, run 0x87895a4, yield 0x87871dc, setNo 0x8786970), clArg 0x8794560 | mseqStory 0x88ccb19, mseqTimeAttack 0x88cd7f3, mseqTen 0x88cddfc, mseqTarget 0x88cf1d9 (mseqJoin: `seqJoinMileageResult`) | every play, after seqLvUp + seqTitleSelect, before the story's seqStTune / poem / continue |
| STAGE CLEAR | `seqTenClearLevel` 0x88a9c1c, `clSeqTenClearLevelThread` (stPimpl 0x88ac2e0, run 0x88a9110, yield 0x88aa4ac, clMenuLevel::update 0x88abb90, clMenu3DCar 0x88ac13c) | mseqTen 0x88ce1d8 | after seqRaceTen when `!isRetire && !isTimeUp && !isTenExtendMode` (all ten beaten, level 1..10); BEFORE seqLvUp |

- Mileage anims: `menu_layout_milage` (ANM 0x1f7), `menu_g_milage_card_update` (0xd7, card renewal: not used).
  Labels: wide strings 0x93f0f3c 今回の走行距離 (template 0x59f), 0x93f0f5c 総走行距離 (0x5a0), FONT_ID 4.
  Values: odo_plus_ = getOdoCount(after) − getOdoCount(before), odo_total_ = before (km). One km is moved
  from plus to total every 5 frames with SE 0x35 `odo_count.wav`; a tap moves all; then 300 frames or a tap.
  Camera: env 0, setInterest((0,16,50),(0,9,0)), car turning −0.3°/frame, with aura (checkAura).
- STAGE CLEAR anims: `menu_g_shogo_bg_1`, `menu_g_bg_line`, `menu_layout_or_result_B` (ANM 0x214), `menu_g_or_result_B_icon`
  (0xec), `menu_g_or_result_B_ring` (0xed, 100 frames a level). Medal sprites table 0x940db28 = black/copper/silver/gold
  (0xf29, 0x1281, 0x1282, 0xca3) swapped into templates 0x740..0x748, 0x73f after the icon's 110 frames.
  SE table 0x940db38 by medal: gold 0x30 `rank`, else 0x41 `parts_equip`; the last gold (10 golds now, 9 before): every ring + SE 0x4f `parts_comp`.
  Exit: a tap or 20 s. Camera: env 10, 73°, eye (0,10,−43) at origin, car rotated 160° about y at (−3,−2,0).
  (SE names: nsAudio one-shot table 0x931EE08, 20 bytes each.)

## 1. Data (do once; data may be updated)

```
python tools/result_sprites.py "<arcade root>" data/extra.bin          # menu.bin beside it is read to skip its sprites
cp "<arcade root>/data/sound/system_se/odo_count.wav" data/sound/
```
Also add `odo_count` to the `for f in ...` list in `prepare_sound.sh` (line 11).
New sprite ids: **126800** (今回の走行距離) and **126801** (総走行距離) = `RESULT_TEXT_BASE` in result_sprites.py,
`MIL_TEXT` in mileage.inc. Tested on a copy: +46 sprites, +4 anims; running it again adds nothing.
(extra.bin's loader caps the file at 4096 sprites; it now has about 1220.)

## 2. src/sound.c — the counting SE

- enum (before `S_COUNT`): add `S_ODO_COUNT,`
- `EFFECT_FILES` (the same place at the end of the list): add `"odo_count"`

## 3. src/menu.h

- Add `MENU_MILEAGE, MENU_TEN_CLEAR` to the end of the `enum { MENU_MOVIE, ... }` screen list.
- Prototypes (anywhere after `menu_state`):
```c
/* (v2.3) the race's end: the card's mileage at the race's start and now (metres): the mileage
 * screen on the way back (src/v23/mileage.inc) */
void menu_mileage_arm(menu_state *m, double odo0_m, double odo1_m);
/* (v2.3) a 10-in-a-row level 1..10 cleared (all ten beaten), before the card takes its medal: the
 * course's medals as they were (STAGE CLEAR on the way back, src/v23/tenclear.inc) */
void menu_tenclear_arm(menu_state *m, const signed char before[11]);
```

## 4. src/menu.c

a) **Forward declarations**: put these next to `static int match_scene3d(...)` / `tg_scene3d(...)` (just above `int menu_scene3d`, about line 743):
```c
static int mileage_scene3d(const menu_state *m, menu_scene *s);
static int tenclear_scene3d(const menu_state *m, menu_scene *s);
```
and these in the block of forward declarations just before `back_from_race` (`static void tune_open(...)`, `lvup_open`, ... about line 4663):
```c
static int tenclear_due(menu_state *m, int white);
static int mileage_due(menu_state *m, int white);
```

b) **Include the code**: right after `draw_crown`'s closing brace (the MENU_TG_CROWN block, about line 10345). Everything they use
(`go`, `spr`, `FADE`, `DEG`, `back_from_race`, `tg_wear`, `tg_rotation`, `chosen_car`, `card_glyphs`, `NAME_GLYPH5`, `CARD_CLASSP`,
`TITLE_GLYPH1`, `CARD_GLYPH2`, `CARD_TEXT`/`C_HP`, `TEN_COURSE_NAME`, `menu_ten_course`) is defined before that:
```c
#include "v23/mileage.inc"
#include "v23/tenclear.inc"
```

c) **menu_scene3d** (after the `MENU_VS_STAR` case):
```c
  if (m->screen == MENU_MILEAGE) return mileage_scene3d(m, s);
  if (m->screen == MENU_TEN_CLEAR) return tenclear_scene3d(m, s);
```

d) **aura_screen** (about line 641): add `|| m->screen == MENU_MILEAGE` (the arcade's ctor calls nsMenuCheck::checkAura + clMenuCar::setAura).

e) **music**: in the `MUSIC_RESULT` screen list (about line 1218, `m->screen == MENU_LVUP || m->screen == MENU_SHOGO || ...`) add
`|| m->screen == MENU_MILEAGE || m->screen == MENU_TEN_CLEAR` (the result music plays on, as for seqLvUp).

f) **back_from_race** (about line 4675) — the trigger points:
   - just BEFORE the class-up block (`/* the class risen: ... */ if (m->lvup_due && !white)`), i.e. after the online-versus `return`:
```c
  /* (v2.3) a 10-in-a-row level cleared: STAGE CLEAR first (mseqTen: seqTenClearLevel before seqLvUp) */
  if (tenclear_due(m, white)) return;
```
   - just AFTER the titles block (`m->title_n = 0;`) and BEFORE the story tuning block (`if (m->game == MENU_GAME_STORY && m->st_tune_due && !white)`):
```c
  /* (v2.3) the mileage (every mode's seqMileageResult, after seqLvUp and seqTitleSelect) */
  if (mileage_due(m, white)) return;
```
   Both screens end by calling `back_from_race(m, 0)` themselves (their own fade-out; they never set `m->deciding`),
   and both drop themselves when `white` (the pause menu's leave).

g) **step dispatch** (next to `if (m->screen == MENU_LVUP) { lvup_step(m, tap, key); return; }`):
```c
  if (m->screen == MENU_MILEAGE) {
    mileage_step(m, tap, key);
    return;
  }
  if (m->screen == MENU_TEN_CLEAR) {
    tenclear_step(m, tap, key);
    return;
  }
```

h) **draw dispatch** in `menu_draw_screen` (next to `if (m->screen == MENU_LVUP) { draw_lvup(m); return; }`):
```c
  if (m->screen == MENU_MILEAGE) {
    draw_mileage(m, draw3d, ctx);
    return;
  }
  if (m->screen == MENU_TEN_CLEAR) {
    draw_tenclear(m, draw3d, ctx);
    return;
  }
```
   (Both draw their own black base, fade in/out and car; no menu bars/keys.)

## 5. src/app.c — arming at the race's end (the `if (race->over && !G.menu.finish && !movie)` block, about line 5484)

a) Mileage — right after the first `garage_save(); /* the mileage */`:
```c
    /* (v2.3) the mileage screen on the way back (not a versus: back to its lobby) */
    if (!G.vs_race && G.menu.own_sel >= 0 && G.menu.own_sel < G.menu.n_own)
      menu_mileage_arm(&G.menu, G.title_odo0, G.menu.own[G.menu.own_sel].odo);
```
   (`G.title_odo0` is set at the race's start, line ~4870; `own[].odo` grows during the race, line ~5478. A story select's
   second race re-arms; the first race's start is kept, so the screen shows both races' km.)

b) STAGE CLEAR — in the `else if (G.n_ten)` branch, BEFORE `signed char *best = ...` updates the card's medal:
```c
      /* (v2.3) all ten beaten at a level 1..10: STAGE CLEAR on the way back (the medals before) */
      if (race->won && G.menu.ten_level < 10) menu_tenclear_arm(&G.menu, G.menu.ten_result[menu_ten_course(&G.menu)]);
```
   (The screen reads the medals after from `m->ten_result` when it opens, so the `*best` update must stay where it is.)

## Not done / notes
- The versus' `seqJoinMileageResult` is not hooked (back_from_race returns to the lobby before; app.c doesn't arm for `G.vs_race`).
- The card-renewal notice (`menu_g_milage_card_update`, 次回はカード更新になります) and the internet-ranking password bar are hidden (no card renewal / passwords in the port).
- Ring timing: `clSpriteAnime::isBound` is bit 14 of +0x20, cleared by setAnime → taken as "the run reached its end"; so the medals swap and the
  played level's ring + SE come at frame 110 (end of the icon's run) and the all-gold rings after that ring's 59 frames. If it looks off in the game,
  `TCL_ICON_LEN` is the one knob.
- With `WMMT_TEN_RES` etc. test env vars: STAGE CLEAR needs `race->won`; the mileage needs a save slot car (`own_sel`).
