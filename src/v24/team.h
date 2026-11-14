/* (v2.4) the 2v2 teams (app.c's, src/v24/team.inc), for the HUD, the lobby and the result */
#ifndef V24_TEAM_H
#define V24_TEAM_H

/* this race a 2v2 (a versus, menu_vs_mode VS_MODE_2V2) */
int team_on(void);
/* the team (0 TEAM A, 1 TEAM B) of link rival i (car_set_versus's others, 0..: the PCBs but
 * this machine's) and this machine's; -1 when not a 2v2 */
int team_of_rival(int i);
int team_of_me(void);
/* the finish screen's TEAM A WIN / TEAM B WIN line (team 1 A, 2 B; 0 nothing), the menu's
 * screen size; t the frames since it came */
void team_finish_draw(int team, int t, float screen_w, float screen_h);

#endif
