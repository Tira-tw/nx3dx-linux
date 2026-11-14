/* (v2.4) the versus' EXTREME rule (menu_state.vs.mode == VS_MODE_EXTREME), after Maximum Tune
 * 5's / 5DX+'s エクストリーム対戦 (Extreme VS: the official FAQ E4 Q2, Q7, Q8): the familiar
 * courses run against their usual direction, the "remaining distance" hidden, the areas limited
 * to C1 and Osaka (the start ramp the cabinet's pick in the area, as in the normal vote). This
 * engine's roads, traffic and goal only run one way, so the reverse run itself is not here: the
 * rule keeps the area limit (the host's vote: every machine gets its ramp in the GO) and hides
 * the distance on every machine's HUD (race_mode, fixed at the GO). Code: extreme.inc (in hud.c). */
#ifndef V24_EXTREME_H
#define V24_EXTREME_H

/* the arcade's areas (app.c AREA_RAMP's order) Extreme races in: 0 C1, 5 Osaka */
#define EXTREME_AREA_C1 0
#define EXTREME_AREA_OSAKA 5

/* 1 if area a (0..7) is one Extreme races in */
int extreme_area_ok(int a);

/* the HUD's rule this race (set with the versus' start: 1 Extreme, 0 not) */
void hud_set_extreme(int on);
int hud_extreme(void);

#endif
