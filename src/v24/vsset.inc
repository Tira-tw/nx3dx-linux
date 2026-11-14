/* (v2.4) The versus' SETTINGS (MENU_VS_SET, a port page in the MORE OPTIONS page's style, the
 * HUD's letters in the 1024 x 768 layout), from the versus select (MENU_VS_SEL) and the lobby
 * (MENU_VS) by their gear key (SETTINGS; the keyboard's O / Tab, a pad's Select or VIEW). Two
 * tabs:
 *   CONNECTION: LAN / ONLINE (vs.online; ONLINE: the relays, relays.h's, one chosen: vs.relay;
 *               both saved in option2), and in a lobby the room's INTRUDE switch (v2.3's
 *               vs_room_toggle by vs.room_tap)
 *   GAMEPLAY:   the race's rule (vs.mode: NORMAL, 2V2, EXTREME, HP BATTLE, EVENT), each with a
 *               line saying what it does and how one wins; 2V2's teams (vs.team[], the host's in
 *               its lobby); greyed while vs.mode_locked (a player on an older build)
 * Touch, the keyboard's / a pad's arrows (up / down the rows, above the first the tabs; left /
 * right a row's value), OK, back. Included into menu.c after the versus screen. */

/* ---- every word on the page (helper 06: the JA / EN table) ---- */
enum {
  VST_TITLE, VST_TAB_CONN, VST_TAB_GAME, VST_NEXT_PAGE, VST_DONE, VST_SETTINGS,
  VST_NETWORK, VST_LAN, VST_ONLINE, VST_NOTE_LAN, VST_NOTE_ONLINE, VST_NOTE_NEXT,
  VST_RELAY, VST_RELAY_ON, VST_RELAY_OFF, VST_RELAY_ASK, VST_RELAY_NONE, VST_MS, VST_CHOSEN,
  VST_INTRUDE, VST_OPEN, VST_CLOSED, VST_NOTE_INTRUDE, VST_NOTE_FULL, VST_NOTE_NO_ROOM,
  VST_NORMAL, VST_2V2, VST_EXTREME, VST_HP, VST_EVENT,
  VST_EX_NORMAL, VST_EX_2V2, VST_EX_EXTREME, VST_EX_HP, VST_EX_EVENT,
  VST_TEAMS, VST_TEAM_A, VST_TEAM_B, VST_NOTE_TEAMS, VST_LOCKED, VST_HOST_CHOOSES,
  VST_N
};
static const char *const VSSET_TXT[VST_N] = {
  "VERSUS SETTINGS", "CONNECTION", "GAMEPLAY", "< VIEW / TAB: NEXT PAGE >", "DONE", "SETTINGS",
  "NETWORK", "LAN", "ONLINE", "THE SAME WI-FI: GAMES NEARBY", "OVER THE INTERNET BY A RELAY SERVER",
  "FROM THE NEXT CONNECTION",
  "RELAY", "ONLINE", "OFFLINE", "CHECKING...", "NO RELAYS - CHECKING...", "MS", "CHOSEN",
  "INTRUDE", "OPEN", "CLOSED", "OTHERS MAY JOIN THIS ROOM", "THE ROOM IS FULL", "IN A LOBBY ONLY",
  "NORMAL", "2V2", "EXTREME", "HP BATTLE", "EVENT",
  "A STRAIGHT RACE. THE FIRST ACROSS THE GOAL WINS",
  "TWO TEAMS. PLACES SCORE 4-3-2-1 POINTS: THE TEAM WITH MORE WINS (1ST+4TH VS 2ND+3RD: A DRAW)",
  "C1 OR OSAKA ONLY, THE DISTANCE LEFT HIDDEN. THE FIRST ACROSS THE GOAL WINS",
  "FALLING BEHIND THE LEADER AND HITS DRAIN YOUR GAUGE; AT 0 YOU ARE OUT. THE LAST ONE IN WINS",
  "ALL TOGETHER AGAINST A CPU RIVAL: ANY ONE OF YOU AT THE GOAL BEFORE IT CLEARS THE EVENT",
  "TEAMS", "A", "B", "THE HOST'S: TAP A PLAYER TO SWAP TEAM", "A PLAYER IS ON AN OLDER VERSION",
  "THE HOST CHOOSES THE RULES",
};
/* (v2.4) the same in Japanese (the language's: lang_is_en) */
static const char *const VSSET_TXT_JA[VST_N] = {
  "対戦設定", "通信設定", "ゲーム設定", "＜ 視点切替／TAB：次のページ ＞", "決定", "設定",
  "通信", "LAN", "オンライン", "同じWi-Fiの近くのゲーム", "中継サーバーでインターネット対戦",
  "次の接続から有効",
  "中継", "オンライン", "オフライン", "確認中…", "中継なし－確認中…", "MS", "選択中",
  "乱入", "受付中", "締切", "他のプレイヤーが乱入できます", "満員です", "ロビーでのみ",
  "ノーマル", "2ON2", "エクストリーム", "HPバトル", "イベント",
  "通常のレース。先にゴールした方の勝ち",
  "2チーム対戦。順位ポイント4・3・2・1の合計が多いチームの勝ち（1位＋4位と2位＋3位は引き分け）",
  "C1・大阪のみ。残り距離は表示されない。先にゴールした方の勝ち",
  "先頭から離されたり接触するとゲージが減り、0で脱落。最後まで残った方の勝ち",
  "全員でCPUのライバルに挑む。誰か1人が先にゴールすればイベントクリア",
  "チーム", "A", "B", "ホストのみ：プレイヤーをタップでチーム変更", "古いバージョンのプレイヤーがいます",
  "ルールはホストが決めます",
};
#define VSSET(k) (lang_is_en() ? VSSET_TXT[k] : VSSET_TXT_JA[k])

/* ---- the relays (relays.h, helper 02's; no stand-ins). The per-relay round trip is
 * net_relays_rtt_ms(i) (net.h's net_relay_rtt_ms(void) is the connected relay's) ---- */
#include "relays.h"

/* ---- the page ---- */
#define VSSET_TABS 2
#define VSSET_RELAYS 4 /* the relays shown */
#define VSSET_ROW_DY 54.0f
#define VSSET_HALF 22.0f
/* a row's kind */
enum { VR_NET, VR_RELAY, VR_ROOM = VR_RELAY + VSSET_RELAYS, VR_MODE, VR_TEAM = VR_MODE + VS_MODES, VR_KINDS };
#define VSSET_ROWS_MAX 8
static int vsset_from = MENU_VS_SEL;      /* where back goes */
static int vsset_row, vsset_cur_on, vsset_team_cur, vsset_refresh_t;

static int vsset_tab(const menu_state *m) { return m->vs.set_tab == 1 ? 1 : 0; }
static int vsset_relays(void) {
  int n = net_relays_count();
  return n < 0 ? 0 : n > VSSET_RELAYS ? VSSET_RELAYS : n;
}
/* the room's switch here: in the lobby (from MENU_VS, its game not kept behind), a v2.3 host's */
static int vsset_room_here(const menu_state *m) { return m->vs.state == MENU_VS_LOBBY && m->vs.room >= 0 && !m->vs.bg; }
/* the rules: the host's in its lobby (anyone's before a game: its host's then), none while locked */
static int vsset_rules_free(const menu_state *m) {
  if (m->vs.mode_locked) return 0;
  return m->vs.state != MENU_VS_LOBBY || vs_host(m);
}
static int vsset_teams_shown(const menu_state *m) { return m->vs.mode == VS_MODE_2V2 && vs_host(m) && m->vs.n >= 2; }
/* the tab's rows into rows[]: how many */
static int vsset_rows(const menu_state *m, int tab, int rows[VSSET_ROWS_MAX]) {
  int n = 0;
  if (tab == 0) {
    rows[n++] = VR_NET;
    if (m->vs.online)
      for (int i = 0; i < vsset_relays(); ++i) rows[n++] = VR_RELAY + i;
    rows[n++] = VR_ROOM;
  } else {
    for (int i = 0; i < VS_MODES; ++i) rows[n++] = VR_MODE + i;
    if (vsset_teams_shown(m)) rows[n++] = VR_TEAM;
  }
  return n;
}
static float vsset_row_y(int i) { return O2_ROW_Y0 + VSSET_ROW_DY * (float)i; }
/* a row's chips (NETWORK's, INTRUDE's; TEAMS' the players'): how many, their words */
static int vsset_chips(const menu_state *m, int kind, const char *s[MENU_VS_PLAYERS], char buf[MENU_VS_PLAYERS][24]) {
  if (kind == VR_NET) {
    s[0] = VSSET(VST_LAN), s[1] = VSSET(VST_ONLINE);
    return 2;
  }
  if (kind == VR_ROOM) {
    s[0] = VSSET(VST_OPEN), s[1] = VSSET(VST_CLOSED);
    return 2;
  }
  if (kind == VR_TEAM) {
    int n = m->vs.n < MENU_VS_PLAYERS ? m->vs.n : MENU_VS_PLAYERS;
    for (int i = 0; i < n; ++i) {
      snprintf(buf[i], 24, "%d %.6s:%s", i + 1, m->vs.name[i], VSSET(m->vs.team[i] ? VST_TEAM_B : VST_TEAM_A));
      s[i] = buf[i];
    }
    return n;
  }
  return 0;
}
#define VSSET_TEAM_X 260.0f
#define VSSET_TEAM_DX 182.0f
static float vsset_chip_x(const char *const *s, int kind, int k) {
  if (kind == VR_TEAM) return VSSET_TEAM_X + VSSET_TEAM_DX * (float)k;
  float x = O2_CHIP_X;
  for (int i = 0; i < k; ++i) x += hud_ui_text_width(s[i], O2_PX) + O2_GAP;
  return x;
}
static float vsset_chip_px(int kind) { return kind == VR_TEAM ? 2.0f : O2_PX; }
/* the chip lit on a row, -1 none */
static int vsset_on(const menu_state *m, int kind) {
  if (kind == VR_NET) return m->vs.online ? 1 : 0;
  if (kind == VR_ROOM) return vsset_room_here(m) ? (m->vs.room ? 1 : 0) : -1;
  return -1;
}

static void vsset_enter(menu_state *m) {
  vsset_from = m->screen == MENU_VS ? MENU_VS : MENU_VS_SEL;
  if (m->vs.set_tab < 0 || m->vs.set_tab >= VSSET_TABS) m->vs.set_tab = 0;
  vsset_row = 0, vsset_cur_on = 0, vsset_team_cur = 0, vsset_refresh_t = 0;
  snd_menu(SND_SELECT);
  go(m, MENU_VS_SET);
}
static void vsset_leave(menu_state *m) {
  snd_menu(SND_CANCEL);
  int to = vsset_from;
  go(m, to);
  if (to == MENU_VS_SEL) m->sel = vs_sel_slot(m->vs.kind);
}
static void vsset_flip(menu_state *m) {
  m->vs.set_tab = (vsset_tab(m) + 1) % VSSET_TABS;
  int rows[VSSET_ROWS_MAX], n = vsset_rows(m, m->vs.set_tab, rows);
  if (vsset_row >= n) vsset_row = n - 1;
  vsset_cur_on = 1;
  snd_menu(SND_CURSOR);
}

/* row kind's chip k chosen (a tap, the arrows, OK); k -1 the row itself (a relay, a rule) */
static void vsset_pick(menu_state *m, int kind, int k) {
  if (kind == VR_NET) {
    int was = m->vs.online;
    option_set(m, &m->vs.online, k ? 1 : 0); /* (saved: option2's vsnet) */
    if (m->vs.online && !was) net_relays_refresh(), vsset_refresh_t = 0;
  } else if (kind >= VR_RELAY && kind < VR_RELAY + VSSET_RELAYS) {
    int i = kind - VR_RELAY;
    if (m->vs.relay == i) snd_menu(SND_CURSOR);
    else snd_menu(SND_SELECT), m->vs.relay = i, m->garage_changed = 1;
  } else if (kind == VR_ROOM) {
    if (!vsset_room_here(m) || m->vs.room >= 2) {
      snd_menu(SND_CANCEL);
      return;
    }
    if (k != (m->vs.room ? 1 : 0)) m->vs.room_tap = 1; /* (the app's switch and sound) */
  } else if (kind >= VR_MODE && kind < VR_MODE + VS_MODES) {
    int mode = kind - VR_MODE;
    if (!vsset_rules_free(m)) {
      snd_menu(SND_CANCEL);
      return;
    }
    if (m->vs.mode != mode) snd_menu(SND_SELECT), m->vs.mode = mode;
  } else if (kind == VR_TEAM) {
    if (!vsset_teams_shown(m) || !vsset_rules_free(m) || k < 0 || k >= m->vs.n) {
      snd_menu(SND_CANCEL);
      return;
    }
    m->vs.team[k] = !m->vs.team[k];
    vsset_team_cur = k;
    snd_menu(SND_CURSOR);
  }
}

/* where OK taps with the arrows' cursor on (1024 x 768): the next tab, a row's other chip, a row;
 * 0 none (DONE) */
static int vsset_ok_point(const menu_state *m, float *x, float *y) {
  if (!vsset_cur_on) return 0;
  int rows[VSSET_ROWS_MAX], n = vsset_rows(m, vsset_tab(m), rows);
  if (vsset_row < 0) {
    float r[4];
    o2_tab_rect((vsset_tab(m) + 1) % VSSET_TABS, r);
    *x = r[0] + 0.5f * r[2], *y = r[1] + 0.5f * r[3];
    return 1;
  }
  if (vsset_row >= n) return 0;
  int kind = rows[vsset_row];
  const char *s[MENU_VS_PLAYERS];
  char buf[MENU_VS_PLAYERS][24];
  int c = vsset_chips(m, kind, s, buf);
  *y = vsset_row_y(vsset_row);
  if (!c) {
    *x = 512.0f;
    return 1;
  }
  int k = kind == VR_TEAM ? (vsset_team_cur < c ? vsset_team_cur : 0) : (vsset_on(m, kind) + 1) % c;
  if (k < 0) k = 0;
  *x = vsset_chip_x(s, kind, k) + 0.5f * hud_ui_text_width(s[k], vsset_chip_px(kind));
  return 1;
}

static void vsset_step(menu_state *m, int tap, int back) {
  float x = m->tap_x * 1.6f, y = m->tap_y * 1.6f;
  int kh = m->key_h, kv = m->key_v;
  m->key_h = m->key_v = 0;
  /* the relays' answers asked again now and then while they are shown */
  if (m->vs.online && vsset_tab(m) == 0 && vsset_refresh_t-- <= 0) net_relays_refresh(), vsset_refresh_t = 300;
  /* the lobby's race on its way (the host's START): back to it */
  if (vsset_from == MENU_VS && (m->vs.state >= MENU_VS_LOADING || m->vs.area_go)) {
    go(m, MENU_VS);
    return;
  }
  if (back || (tap && o2_in(O2_DONE, x, y))) {
    vsset_leave(m);
    return;
  }
  int tab = vsset_tab(m), rows[VSSET_ROWS_MAX], n = vsset_rows(m, tab, rows);
  if (vsset_row >= n) vsset_row = n - 1;
  if (kh || kv) {
    if (!vsset_cur_on) {
      vsset_cur_on = 1;
      snd_menu(SND_CURSOR);
      return;
    }
    if (kv) {
      int to = vsset_row + kv;
      if (to >= -1 && to < n) vsset_row = to, snd_menu(SND_CURSOR);
      return;
    }
    if (vsset_row < 0) {
      vsset_flip(m);
      return;
    }
    int kind = rows[vsset_row];
    if (kind == VR_TEAM) {
      int to = vsset_team_cur + kh;
      if (to >= 0 && to < m->vs.n && to < MENU_VS_PLAYERS) vsset_team_cur = to, snd_menu(SND_CURSOR);
      return;
    }
    if (kind == VR_NET || kind == VR_ROOM) {
      int k = vsset_on(m, kind) + kh;
      if (k < 0 || k > 1) {
        snd_menu(SND_CANCEL);
        return;
      }
      vsset_pick(m, kind, k);
    }
    return;
  }
  if (!tap) return;
  for (int t = 0; t < VSSET_TABS; ++t) {
    float r[4];
    o2_tab_rect(t, r);
    if (x < r[0] || x >= r[0] + r[2] || y < r[1] - 4 || y >= r[1] + r[3] + 4) continue;
    if (t != tab) {
      m->vs.set_tab = t;
      int rows2[VSSET_ROWS_MAX], n2 = vsset_rows(m, t, rows2);
      if (vsset_row >= n2) vsset_row = n2 - 1;
      snd_menu(SND_CURSOR);
    }
    return;
  }
  for (int i = 0; i < n; ++i) {
    float yc = vsset_row_y(i);
    if (y < yc - VSSET_HALF - 4 || y >= yc + VSSET_HALF + 4 || x < 40 || x >= 984) continue;
    int kind = rows[i];
    vsset_row = i;
    const char *s[MENU_VS_PLAYERS];
    char buf[MENU_VS_PLAYERS][24];
    int c = vsset_chips(m, kind, s, buf);
    if (!c) {
      vsset_pick(m, kind, -1);
      return;
    }
    for (int k = 0; k < c; ++k) {
      float cx = vsset_chip_x(s, kind, k), w = hud_ui_text_width(s[k], vsset_chip_px(kind));
      if (x < cx - O2_GAP * 0.5f || x >= cx + w + O2_GAP * 0.5f) continue;
      vsset_pick(m, kind, k);
      return;
    }
    return;
  }
}

static void draw_vsset(const menu_state *m, int clock, int bars, float screen_w, float screen_h) {
  float fade = m->t < FADE ? (float)(m->t + 1) / FADE : 1.0f;
  draw_back(ID.bg, clock % 600, 1);
  anm_draw(ID.line, clock % anm_frames(ID.line), 1);
  anm_fill(0, 0, 0, 0.45f);
  anm_swap title = { anm_sprite(T_TOPBAR_MES), anm_sprite(VS_TEXT) };
  draw_topbar(bars, 1, &title);
  anm_draw(ID.bottom, bars, 1);
  hud_ui_begin(screen_w, screen_h);
  layout_text_mid(VSSET(VST_TITLE), 136, 2.6f, 1, 1, 1);
  int tab = vsset_tab(m), rows[VSSET_ROWS_MAX], n = vsset_rows(m, tab, rows);
  /* the tabs (the MORE page's: the one shown orange; the cursor on them a bar under) */
  for (int t = 0; t < VSSET_TABS; ++t) {
    float r[4];
    o2_tab_rect(t, r);
    int on = t == tab;
    hud_ui_rect(r[0], r[1], r[2], r[3], on ? 0.16f : 0.03f, on ? 0.09f : 0.04f, on ? 0.02f : 0.06f, on ? 0.85f : 0.6f);
    if (on) hud_ui_rect(r[0], r[1], r[2], 3, 1.0f, 0.42f, 0.05f, 0.95f);
    if (on && vsset_cur_on && vsset_row < 0) hud_ui_rect(r[0], r[1] + r[3] - 3, r[2], 3, 1.0f, 0.55f, 0.1f, 0.95f);
    const char *s = VSSET(t ? VST_TAB_GAME : VST_TAB_CONN);
    hud_ui_text(s, r[0] + 0.5f * (r[2] - hud_ui_text_width(s, 2.6f)), r[1] + 0.5f * (r[3] - 7.0f * 2.6f), 2.6f, 1, on ? 0.55f : 1,
                on ? 0.15f : 1, on ? 1.0f : 0.6f);
  }
  {
    const char *s = VSSET(VST_NEXT_PAGE);
    hud_ui_text(s, 984 - hud_ui_text_width(s, 1.3f), O2_TAB_Y + O2_TAB_H + 6, 1.3f, 1, 1, 1, 0.4f);
  }
  /* GAMEPLAY's why the rules are not this one's: an older build in the room, or the host's */
  int rfree = vsset_rules_free(m);
  if (tab == 1 && m->vs.mode_locked)
    hud_ui_text(VSSET(VST_LOCKED), 40, O2_TAB_Y + O2_TAB_H + 6, 1.8f, 1.0f, 0.3f, 0.3f, 1);
  else if (tab == 1 && !rfree)
    hud_ui_text(VSSET(VST_HOST_CHOOSES), 40, O2_TAB_Y + O2_TAB_H + 6, 1.8f, 1.0f, 0.55f, 0.15f, 1);
  if (tab == 0 && m->vs.online && !vsset_relays()) {
    const char *s = VSSET(VST_RELAY_NONE);
    hud_ui_text(s, 64, vsset_row_y(1) - 3.5f * 2.0f, 2.0f, 1, 1, 1, (clock / 30) % 2 ? 0.4f : 0.7f);
  }
  char buf2[96];
  for (int i = 0; i < n; ++i) {
    int kind = rows[i];
    float yc = vsset_row_y(i);
    int cur = vsset_cur_on && vsset_row == i;
    /* the row's own lit (the relay chosen, the rule on) */
    int lit = (kind >= VR_RELAY && kind < VR_RELAY + VSSET_RELAYS && m->vs.relay == kind - VR_RELAY) ||
              (kind >= VR_MODE && kind < VR_MODE + VS_MODES && m->vs.mode == kind - VR_MODE);
    int dim = (kind >= VR_MODE && !rfree) || (kind == VR_ROOM && !vsset_room_here(m));
    float a = dim ? 0.4f : 1.0f;
    hud_ui_rect(40, yc - VSSET_HALF - 3, 944, 2 * VSSET_HALF + 6, cur ? 0.12f : lit ? 0.10f : 0.03f, cur ? 0.07f : lit ? 0.06f : 0.04f,
                cur ? 0.02f : lit ? 0.02f : 0.06f, 0.55f);
    if (cur || lit) hud_ui_rect(40, yc - VSSET_HALF - 3, 4, 2 * VSSET_HALF + 6, 1.0f, 0.55f, 0.1f, cur ? 0.9f : 0.5f);
    const char *label = "", *note = "";
    float nr = 1, ng = 1, nb = 1, na = 0.5f;
    if (kind == VR_NET) {
      label = VSSET(VST_NETWORK);
      note = VSSET(m->vs.online ? VST_NOTE_ONLINE : VST_NOTE_LAN);
      if (m->vs.state != MENU_VS_OFF) {
        snprintf(buf2, sizeof buf2, "%s - %s", note, VSSET(VST_NOTE_NEXT));
        note = buf2;
      }
    } else if (kind >= VR_RELAY && kind < VR_RELAY + VSSET_RELAYS) {
      int r = kind - VR_RELAY, on = net_relay_online(r), ms = net_relays_rtt_ms(r); /* (v2.4) */
      const char *nm = net_relay_name(r);
      label = nm ? nm : VSSET(VST_RELAY);
      char st[48];
      if (on && ms >= 0) snprintf(st, sizeof st, "%s  %d %s", VSSET(VST_RELAY_ON), ms, VSSET(VST_MS));
      else snprintf(st, sizeof st, "%s", on ? VSSET(VST_RELAY_ON) : ms < 0 ? VSSET(VST_RELAY_ASK) : VSSET(VST_RELAY_OFF));
      float sr = on ? 0.5f : 1.0f, sg = on ? 1.0f : 0.45f, sb = on ? 0.5f : 0.35f;
      hud_ui_text(st, O2_CHIP_X, yc - 3.5f * 2.4f, 2.4f, sr, sg, sb, 1);
      if (m->vs.relay == r) {
        const char *c = VSSET(VST_CHOSEN);
        hud_ui_text(c, 960 - hud_ui_text_width(c, 2.4f), yc - 3.5f * 2.4f, 2.4f, 1.0f, 0.55f, 0.15f, 1);
      }
      snprintf(buf2, sizeof buf2, "%s %d", VSSET(VST_RELAY), r + 1);
      note = buf2;
    } else if (kind == VR_ROOM) {
      label = VSSET(VST_INTRUDE);
      note = !vsset_room_here(m) ? VSSET(VST_NOTE_NO_ROOM) : m->vs.room >= 2 ? VSSET(VST_NOTE_FULL) : VSSET(VST_NOTE_INTRUDE);
    } else if (kind >= VR_MODE && kind < VR_MODE + VS_MODES) {
      static const int NAME[VS_MODES] = { VST_NORMAL, VST_2V2, VST_EXTREME, VST_HP, VST_EVENT };
      int k = kind - VR_MODE;
      label = VSSET(NAME[k]);
      note = VSSET(VST_EX_NORMAL + k);
      na = lit ? 1.0f : 0.75f;
      if (lit) nr = 1.0f, ng = 0.75f, nb = 0.4f;
    } else if (kind == VR_TEAM) {
      label = VSSET(VST_TEAMS);
      note = VSSET(VST_NOTE_TEAMS);
    }
    float lr = lit ? 1.0f : 1.0f, lg = lit ? 0.55f : 1.0f, lb = lit ? 0.15f : 1.0f;
    hud_ui_text(label, 64, yc - 19, 2.6f, lr, lg, lb, a);
    hud_ui_text(note, 64, yc + 8, 1.4f, nr, ng, nb, na * a);
    /* the chips */
    const char *s[MENU_VS_PLAYERS];
    char buf[MENU_VS_PLAYERS][24];
    int c = vsset_chips(m, kind, s, buf), on = vsset_on(m, kind);
    for (int k = 0; k < c; ++k) {
      float px = vsset_chip_px(kind), cx = vsset_chip_x(s, kind, k);
      if (kind == VR_TEAM) {
        int b = m->vs.team[k] != 0, tc = cur && vsset_team_cur == k;
        float w = VSSET_TEAM_DX - 10;
        hud_ui_rect(cx - 6, yc - 13, w, 26, b ? 0.05f : 0.30f, b ? 0.12f : 0.06f, b ? 0.35f : 0.03f, 0.9f);
        if (tc) hud_ui_rect(cx - 6, yc + 11, w, 2, 1.0f, 0.55f, 0.1f, 1);
        hud_ui_text(s[k], cx, yc - 3.5f * px, px, 1, 1, 1, a);
        continue;
      }
      int l = k == on;
      hud_ui_text(s[k], cx, yc - 3.5f * px, px, 1.0f, l ? 0.55f : 1.0f, l ? 0.15f : 1.0f, (l ? 1.0f : 0.35f) * a);
    }
  }
  const float *d = O2_DONE;
  hud_ui_button(d[0], d[1], d[2], d[3], 0);
  hud_ui_text(VSSET(VST_DONE), d[0] + 0.5f * (d[2] - hud_ui_text_width(VSSET(VST_DONE), 3.5f)), d[1] + 0.5f * (d[3] - 7.0f * 3.5f), 3.5f,
              1, 1, 1, 1);
  hud_ui_end();
  hud_draw_menu_keys(screen_w, screen_h, m->key_held, 0, 1);
  anm_fill(0, 0, 0, 1.0f - fade);
}

/* the versus select's and the lobby's SETTINGS: the gear key with its word under it */
static void vsset_draw_gear(const menu_state *m, float screen_w, float screen_h) {
  hud_draw_menu_option(screen_w, screen_h, m->key_held == HUD_MENU_OPTION);
  hud_ui_begin(screen_w, screen_h);
  const char *s = VSSET(VST_SETTINGS);
  hud_ui_text(s, 670.0f - 0.5f * hud_ui_text_width(s, 1.5f), 108.0f, 1.5f, 1, 1, 1, 0.85f);
  hud_ui_end();
}

/* step()'s: the page, or its gear key on the versus screens; 1 if it took the frame */
static int vsset_hook(menu_state *m, int tap, int back, int option) {
  if (m->screen == MENU_VS_SET) {
    vsset_step(m, tap, back);
    return 1;
  }
  if (option && (m->screen == MENU_VS_SEL || (m->screen == MENU_VS && m->vs.state < MENU_VS_LOADING)) && !m->deciding && !m->vs.ask &&
      !m->vs.area_go) {
    vsset_enter(m);
    return 1;
  }
  return 0;
}
