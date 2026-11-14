/* The versus's client (a port extra): UDP to the versus server (tools/vs_server.py, or a LAN
 * game's host, vs_host.c), or GameKit to a Bluetooth game's host (gk.h); its lobby, and the
 * race packets of the arcade's cabinet link (car_set_versus) through it. And the LAN's games
 * found. Everything runs on the caller's thread: net_poll reads what has arrived, never
 * blocking. */
#ifndef NET_H
#define NET_H

#include <stdint.h>

#define NET_PORT 27015
#define NET_PLAYERS 4
#define NET_NAME 16
#define NET_PROFILE 64
#define NET_FOUND 4

/* a player as the lobby shows it: its name and its own description of its car (net_profile) */
typedef struct {
  uint32_t id;
  int status; /* 0 in the lobby, 1 racing */
  char name[NET_NAME + 1];
  unsigned char profile[NET_PROFILE];
  int profile_len;
} net_player;

typedef struct {
  int connected;   /* the server has answered */
  int n, me, host; /* players; this one's and the host's place among them */
  net_player player[NET_PLAYERS];
  /* (v2.3) the room's 乱入 flag, the byte after the players (a v2.2 host sends none: -1):
   * NET_ROOM_CLOSED the players closed it, NET_ROOM_FULL every place taken (locked closed) */
  int room;
} net_lobby;
#define NET_ROOM_CLOSED 1
#define NET_ROOM_FULL 2
#define NET_ROOM_MARK 0xa0 /* (the byte's top bits: a v2.3 host's) */
/* (v2.3) why a host turned this one away: REFUSED's reason (net_refused), or KICK (net_kicked) */
#define NET_REFUSE_CLOSED 1
#define NET_REFUSE_FULL 2
#define NET_REFUSE_KICKED 3
#define NET_REFUSE_MODE 4 /* (v2.4) the room plays a v2.4 mode; the one joining has no v2.4 mark */

/* a race the host started: the machines in it (this one's PCB id `pcb`), its start ramp, km and
 * random number */
typedef struct {
  uint32_t race;
  uint32_t seed;
  int ramp, km;
  int n, pcb;
  net_player player[NET_PLAYERS]; /* by PCB id */
  int song; /* the host's song + 2 for everyone's race (0: a host from before it) */
} net_go;

/* open the client to the server at `host` (dotted IPv4); 0 on success */
int net_open(const char *host, int port, uint32_t id);
/* open the client to the Bluetooth game joined (gk_join, gk_joined) */
int net_open_gk(uint32_t id);
void net_close(void);
int net_is_open(void);
/* read everything that has arrived; in the lobby say hello twice a second (status 0 lobby,
 * 1 racing) as `name` with `profile` */
void net_poll(double now, int status, const char *name, const unsigned char *profile, int profile_len);
const net_lobby *net_lobby_now(void);
/* seconds since the server last answered (large before it ever has) */
double net_silence(double now);
/* the host's start: the lobby races on `ramp` for `km`, everyone's BGM `song` (the GO's) */
void net_start(int ramp, int km, int song);
/* the host (an app's, vs_host) has closed its game: CLOSED came */
int net_host_closed(void);
/* a GO the server sent since the last call: 1 and *go filled in */
int net_take_go(net_go *go);
/* the race's packets: send this frame's; hand the newest received from each machine of the race to
 * car_link_deliver (the car_link_io's receive) */
void net_race_send(const unsigned char *packet, int len);
void net_race_receive(void);
/* (v2.3) seconds since the race's machine `pcb` was last heard (its race packets), large if never */
double net_race_silence(int pcb, double now);

/* (v2.3) the room (v2.2 hosts ignore these; the host enforces them): ROOM asks the host for the
 * 乱入 flag (closed 1 / open 0); KICK, the lobby's host's, takes a player out (the host bans its
 * id for the session); REFUSED / KICK come back: the reason this one is not let in, 0 none */
#define net_room(closed) net_room_flag(closed) /* (v2.4: the name net_room is the room listing's type) */
void net_room(int closed);
void net_kick(uint32_t id);
int net_refused(void);
int net_kicked(void);

/* (v2.4) ONLINE: a relay (the friend's vs_server 3, RELAYS.md), its own kind numbers (LIST 10,
 * ROOMS 11, CREATE 12, JOIN 13, AUTO 14, REFUSED 15, PING 16, PONG 17, START_AT 19, RESULT 22,
 * RACEZ 23) on this connection only; HELLO carries the app key, 'X' '1' and 'N' 4. The lobby, GO
 * and RACE are the same calls as above (net_poll, net_lobby_now, net_take_go, net_race_*). While
 * no room is chosen (AUTO/CREATE/JOIN) the HELLOs say 250 (browsing: not auto-matched).
 * (net_room is the room listing's type; the 乱入 call net_room(closed) above is a macro, no-op
 * on a relay, as is net_kick) */
#define NET_ROOM_NAME 16
#define NET_ROOMS_MAX 32
typedef struct {
  uint32_t id;
  char name[NET_ROOM_NAME + 1];
  int players, max, racing;
  int has_ext, vs_build, app_build, platform; /* the ROOMS' 'X' '1' trailer (has_ext 0: none) */
  char host[NET_NAME + 1];
  int host_rtt; /* the 'Q' '1' trailer: ms, -1 unknown */
} net_room;
/* host: a dotted IPv4 or a name (resolved here, once); 0 ok */
int net_relay_open(const char *host, int port, const char *name, const unsigned char *profile, int plen);
int net_is_relay(void);
void net_relay_auto(void);
void net_relay_list(void);
int net_relay_rooms(net_room *out, int max); /* the last ROOMS' rooms (their count) */
void net_relay_join(unsigned id);
void net_relay_create(const char *name);
const char *net_relay_refused(void); /* the relay's REFUSED message, NULL none */
int net_relay_rtt_ms(void);          /* the last PING's round trip, -1 none yet */

/* The ghosts' exchange (v2.1, ghostx.c): GHOST datagrams, the host (vs_host.c, vs_server.py)
 * passing each on to the player its u32 `to` names (0: everyone else). body: u32 from, u32 to,
 * then ghostx's own. A host from before relays none (nothing is exchanged). */
#define NET_GHOST_MAX 1200
void net_ghost_send(const unsigned char *body, int len);
void net_ghost_handler(void (*fn)(const unsigned char *body, int len));
uint32_t net_my_id(void);

/* The LAN's games: while on, ask the Wi-Fi who hosts (a broadcast a second) and keep the
 * answers; net_found_list copies them (their count) */
typedef struct {
  char ip[16];
  char name[NET_NAME + 1];
  uint32_t id; /* its host's player */
  int n;       /* its players */
  double seen;
} net_found;
void net_scan(double now, int on);
int net_found_list(net_found *out, int max);

#endif
