/* (v2.4) the 2v2 (menu_vs_mode VS_MODE_2V2): two teams (the room's, menu_vs_team_of_pcb), the
 * race's places scored as points, 1st 4, 2nd 3, 3rd 2, 4th 1; the higher sum wins, a tie the team
 * with the first. Each machine's own place stays (the finish's place sprite, LAST RACE); the
 * win (the sound, the car's wins, the stars, the titles) is the team's. 2 to 4 machines; a race
 * with the one team only (or alone at the end): the usual rule. Included in app.c (G's). */
#include "v24/team.h"

int team_on(void) { return G.vs_race && menu_vs_mode(&G.menu) == VS_MODE_2V2; }

int team_of_me(void) { return team_on() ? menu_vs_team_of_pcb(&G.menu, G.vs_go.pcb) : -1; }

int team_of_rival(int i) {
  if (!team_on() || i < 0) return -1;
  /* (car_set_versus's others: the GO's PCBs in order, this machine's left out) */
  int pcb = i < G.vs_go.pcb ? i : i + 1;
  return menu_vs_team_of_pcb(&G.menu, pcb);
}

/* the race over (app.c's versus end): the team's result into m (last_team, finish_story); 1 this
 * machine's team won, 0 lost, -1 not a team race (the usual rule then). The others' places: those
 * still in, by their gaps at this machine's end (the ones ahead of its place first). */
static int team_finish(menu_state *m, const car_race *race, int place, int of) {
  m->vs.last_team = 0;
  if (!team_on() || of < 2) return -1;
  int idx[CAR_RIVALS], n = 0;
  for (int i = 0; i < G.n_rival && i < CAR_RIVALS; ++i)
    if (race->vs_on[i]) idx[n++] = i;
  for (int a = 1; a < n; ++a) /* by the gap, the furthest ahead first */
    for (int b = a; b > 0 && race->vs_gap[idx[b]] < race->vs_gap[idx[b - 1]]; --b) {
      int t = idx[b];
      idx[b] = idx[b - 1];
      idx[b - 1] = t;
    }
  int pts[2] = { 0, 0 }, in[2] = { 0, 0 }, first = -1;
  int me = team_of_me() & 1;
  pts[me] += 5 - place;
  in[me]++;
  if (place == 1) first = me;
  for (int k = 0, p = 1; k < n; ++k, ++p) {
    if (p == place) ++p; /* (this machine's) */
    if (p > 4) break;
    int t = team_of_rival(idx[k]) & 1;
    pts[t] += 5 - p;
    in[t]++;
    if (p == 1) first = t;
  }
  if (!in[0] || !in[1]) return -1; /* (one team only: no team result) */
  /* (the arcade's 2on2: equal points, 1st + 4th against 2nd + 3rd, is a DRAW, not the 1st's win) */
  if (pts[0] == pts[1]) {
    m->vs.last_team = 3;
    m->finish_story = 6;
    fprintf(stderr, "versus 2v2: TEAM A %d pts, TEAM B %d pts: DRAW (place %d of %d)\n", pts[0], pts[1], place, of);
    return 2;
  }
  (void)first;
  int win = pts[1] > pts[0];
  m->vs.last_team = win + 1;
  m->finish_story = win == me ? 5 : 6;
  fprintf(stderr, "versus 2v2: TEAM A %d pts, TEAM B %d pts: TEAM %c WIN (this one TEAM %c, place %d of %d)\n", pts[0], pts[1],
          'A' + win, 'A' + me, place, of);
  return win == me;
}

void team_finish_draw(int team, int t, float screen_w, float screen_h) {
  if (team < 1 || team > 3 || t < 20) return;
  const char *s = team == 1 ? LJ("TEAM A WIN", "チームAの勝利") : team == 2 ? LJ("TEAM B WIN", "チームBの勝利") : LJ("DRAW", "引き分け");
  float px = 6.0f, a = t >= 40 ? 1.0f : (float)(t - 20) / 20.0f;
  float w = hud_ui_text_width(s, px), x = 512.0f - 0.5f * w, y = 600.0f;
  hud_ui_begin(screen_w, screen_h);
  hud_ui_rect(x - 24.0f, y - 14.0f, w + 48.0f, 7.0f * px + 28.0f, 0, 0, 0, 0.55f * a);
  /* (team A blue, B red: as the HUD's and the lobby's) */
  if (team == 1) hud_ui_text(s, x, y, px, 0.35f, 0.65f, 1.0f, a);
  else if (team == 2) hud_ui_text(s, x, y, px, 1.0f, 0.35f, 0.3f, a);
  else hud_ui_text(s, x, y, px, 1.0f, 1.0f, 1.0f, a);
  hud_ui_end();
}
