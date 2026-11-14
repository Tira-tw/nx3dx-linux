# maxicoin (v2.3): hooks into menu.h / menu.c / app.c

Files: `src/v23/maxicoin.inc` (screens), `tools/maxicoin_sprites.py` (data). Arcade evidence is in
the head comment of maxicoin.inc.

## data

    python tools/maxicoin_sprites.py "<arcade root>" data/extra.bin

Adds sprites 1559 2320 2828 3227 (small coins 1/100/1000/10000), 2321 3228 3868 4729 (big ones),
1556, and every sprite of the 13 anims (menu_g_mx_coin_bg, menu_g_maxi_bg,
menu_g_mx_coin_finish_1/100/1000/10000, menu_g_mx_coin_keta_b_1(_a), menu_layout_mx_coin_logo /
_title / _car, menu_layout_name_bar, menu_layout_vs_star_title). Tested on a copy: 1098 -> 1124
sprites, 71 -> 84 anims; a second run adds nothing. All ids are the arcade's own (no collisions).

## menu.h

1. enum: append `MENU_MXC_GET, MENU_MXC_PROUD` to the MENU_* list (after `MENU_TG_CROWN`, line ~572).
2. menu_state, next to `crown_get / crown_held` (line ~238):

       /* (v2.3) MAXI coins (the card's getMaxiCoin, per car; saved), the ones the last ghost
        * battle gave (MENU_MXC_GET: seqTgMaxiCoinGet, 0 none) and the attract's 自慢 page's count */
       int maxi_coin[MENU_GARAGE_MAKERS][MENU_GARAGE_CARS];
       int mxc_add, mxp_coins;

3. prototype: `int menu_mxc_award(int won, int revenge, int area_crown, int all_crowns);`

## menu.c

1. `#include "v23/maxicoin.inc"` right after `draw_crown` (line ~10331; it needs go, spr,
   cf_text, snd_effect, tg_due, game_slot, FADE, all defined above it).
2. Forward declaration near `static void crown_open(menu_state *m);` (line ~7474):
   `static void mxc_open(menu_state *m);`
3. `tg_due` (line ~7475): the arcade's order is crown -> maxi coin -> dress-up GETs. After the
   crown block, before `m->crown_get = -1;`:

       m->crown_get = -1;
       if (m->mxc_add > 0) {
         mxc_open(m);
         m->tap = m->key = m->key_held = m->back = m->leaving = 0;
         return 1;
       }

   (mxc_step zeroes mxc_add before calling tg_due again, so there is no loop.)
4. step dispatch, next to `if (m->screen == MENU_TG_CROWN) { crown_step(m, tap); return; }` (~11478):

       if (m->screen == MENU_MXC_GET) { mxc_step(m, tap); return; }
       if (m->screen == MENU_MXC_PROUD) { if (mxp_step(m, tap)) { /* on with the attract: the
                                           page after it, as seqMaxiCoinProud returns */ } return; }

5. draw dispatch, next to `draw_crown(m)` (~12148):

       if (m->screen == MENU_MXC_GET) { draw_mxc_get(m); return; }
       if (m->screen == MENU_MXC_PROUD) { draw_mxc_proud(m); return; }

6. `aura_screen` (line 640): optional, add `|| m->screen == MENU_MXC_GET` only if the car is ever
   drawn there (the arcade shows the clMenuCar with its aura; the .inc draws 2D only).
7. card page 3 (line 4354, `cf_text(5, "0", ...)  /* (no maxi coins in the port) */`): show
   the car's count instead:

       char mc[16];
       snprintf(mc, sizeof mc, "%d", m->maker >= 0 && m->car >= 0 ? m->maxi_coin[m->maker][m->car] : 0);
       cf_text(5, mc, x, y, fade);

8. The attract (MENU_MXC_PROUD): the arcade runs seqMaxiCoinProud in mseqDemo (0x88c9f60) right
   after seqRanking and before seqChampionDisp, but only with clNet::getConnectCount() > 1 (a
   linked cabinet). Strictly faithful = never in a standalone port. If wanted: in the attract
   loop, before the champion page (MENU_CHAMP), `if (m->mxp_coins > 0) mxp_open(m);` and go on to
   MENU_CHAMP when mxp_step returns 1.

## app.c

1. The award, in the ghost battle's result (line ~5509, `} else if (G.tg_race) {`), after
   `tg_history_add` and before/around the crown block (the arcade: clRaceThread::finiTarget):

       G.menu.mxc_add = 0;
       if (won && G.tg_ok && G.tg_area >= 0 && G.tg_area < TARGET_AREAS && G.menu.maker >= 0 && G.menu.car >= 0) {
         int area_crown = G.tg_file.area[G.tg_area].crown || G.tg_champ, all = 1;
         for (int a = 0; a < TARGET_AREAS; ++a) all &= G.tg_file.area[a].crown != 0;
         int n = menu_mxc_award(1, 0 /* revenge: the port has none yet */, area_crown, all);
         int *mc = &G.menu.maxi_coin[G.menu.maker][G.menu.car];
         int to = *mc + n > 999999 ? 999999 : *mc + n; /* (teBound 0..999999) */
         G.menu.mxc_add = to - *mc;
         *mc = to;
         fprintf(stderr, "ghost battle: maxi coin +%d -> %d\n", G.menu.mxc_add, to);
       }

   and call `garage_save()` after it (the existing `garage_save()` at ~5508 is before; move it
   below or call again). The screen comes up by itself via tg_due (shown only when the count
   changed, as mseqTarget's getMaxiCoin != getBeforeRaceCard().getMaxiCoin()). Not on retire:
   `won` already needs `goal`.
2. Save/load, beside `cclass` (save ~1232, load ~1125):

       fprintf(f, "cmaxi %d %s\n", G.menu.maxi_coin[o->maker][o->car], label);

       if (sscanf(line, "cmaxi %d %199[^\n]", &a, label) == 2) {
         int mk, c;
         if (car_by_label(label, &mk, &c) && a >= 0 && a <= 999999) G.menu.maxi_coin[mk][c] = a;
         continue;
       }

   (zero the array wherever the other per-car arrays are cleared on a new save.)
3. The attract's 自慢 (only if menu.c step 8 is wired): before the attract reaches that page set
   `G.menu.mxp_coins` to the save's highest `maxi_coin[mk][c]` (0 skips the page).
