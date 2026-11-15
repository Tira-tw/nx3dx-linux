/* The versus's client: see net.h and tools/vs_server.py (the protocol). */
#include "net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h> /* (v2.4) */
#include <time.h>
#ifndef _WIN32
#include <netdb.h> /* (v2.4: a relay's name; Windows has getaddrinfo in ws2tcpip.h) */
#endif

#include "car.h"
#include "gk.h"
#include "vroom.h"

enum { HELLO = 1, LOBBY, START, GO, RACE, BYE, DISCOVER, OFFER, CLOSED, GHOST, ROOM, KICK, REFUSED }; /* (v2.3: ROOM..) */
/* (v2.4) a relay's own numbering (vs_server 3), read and sent only while N.relay */
enum { R_LIST = 10, R_ROOMS, R_CREATE, R_JOIN, R_AUTO, R_REFUSED, R_PING, R_PONG, R_NETSTAT, R_START_AT, R_CHECKPOINT, R_FINISH, R_RESULT, R_RACEZ };
#define RELAY_APP_KEY 0x574d4d33u
#define RELAY_APP_BUILD 240
#define RELAY_VS_BUILD 221
/* (1, not 4: 4 makes the relay hold every machine for START_AT till each reports status 70; this
 * client starts at GO, so a 4 left the others waiting up to 60 s. Under 2 the relay asks no sync) */
#define RELAY_PROTO 1
#define RELAY_BROWSING 250 /* HELLO's status while no room is chosen: the relay does not auto-match it */
#define PING_EVERY 1.0
#if defined(_WIN32)
#define RELAY_PLATFORM 0
#elif defined(__ANDROID__)
#define RELAY_PLATFORM 1
#else
#define RELAY_PLATFORM 2
#endif
static void (*ghost_fn)(const unsigned char *, int);
#define SCAN_EVERY 1.0
#define SCAN_GONE 4.0
#define HELLO_EVERY 0.5
#define PACKET_COUNT 0x34 /* SLxPacket: CLinx's frame count (older packets are dropped) */

static struct {
  int fd, gk; /* the server's UDP socket, or the GameKit session's host */
  uint32_t id;
  double last_hello, last_heard, now;
  int last_status;
  int heard;
  net_lobby lobby;
  net_go go;
  int go_new;
  int closed; /* the host's CLOSED came */
  uint32_t race; /* the race this machine is in (0 none) */
  struct sockaddr_in to; /* the server's (v2.2: no connect(), each datagram sent to it) */
  /* the newest race packet from each machine, not yet handed over */
  unsigned char rx[CAR_LINK_MACHINES][CAR_LINK_PACKET];
  int rx_new[CAR_LINK_MACHINES];
  uint32_t rx_count[CAR_LINK_MACHINES];
  int rx_any[CAR_LINK_MACHINES];
  double rx_at[CAR_LINK_MACHINES]; /* (v2.3) when each machine's last came */
  int refused, kicked;             /* (v2.3) the host's REFUSED reason, its KICK */
  /* (v2.4) a relay's connection: its dialect, the room asked for, its rooms, REFUSED, PING */
  int relay, chosen;
  char name[NET_NAME + 1];
  unsigned char profile[NET_PROFILE];
  int plen;
  net_room rooms[NET_ROOMS_MAX];
  int nrooms;
  char refused_msg[49];
  int refused_reason;
  uint32_t ping_nonce;
  double ping_sent;
  int rtt_ms;
  int pend_kind; /* the AUTO/JOIN/CREATE said again each second till a LOBBY (or REFUSED) */
  uint32_t pend_room;
  char pend_name[NET_ROOM_NAME + 1];
  double pend_at;
} N = { .fd = -1 };

static void put32(unsigned char *p, uint32_t v) {
  p[0] = (unsigned char)v, p[1] = (unsigned char)(v >> 8), p[2] = (unsigned char)(v >> 16), p[3] = (unsigned char)(v >> 24);
}
static uint32_t get32(const unsigned char *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static void head(unsigned char *p, int kind) { p[0] = 'W', p[1] = 'V', p[2] = 1, p[3] = (unsigned char)kind; }

static void tx(const unsigned char *b, int len) {
  if (N.gk) gk_send(-1, b, len);
  else if (N.fd >= 0) sendto(N.fd, b, (size_t)len, 0, (const struct sockaddr *)&N.to, sizeof N.to);
}

static int rx(unsigned char *b, int max) {
  int peer;
  if (N.gk) return gk_recv(b, max, &peer);
  if (N.fd < 0) return 0;
  /* (from any address: a host with two networks may answer from the other one; the datagrams
   * carry the protocol's own mark) */
  ssize_t n = recvfrom(N.fd, b, (size_t)max, 0, NULL, NULL);
  return n > 0 ? (int)n : 0;
}

/* a dotted IPv4 address (leading zeros too: iOS's inet_pton refuses 192.168.001.005, Allan
 * 11-08: NOT AN ADDRESS on the iPad, the same address taken on Android); 0 ok */
static int parse_ip(const char *s, struct in_addr *out) {
  unsigned v[4];
  int k = 0;
  for (; k < 4; ++k) {
    if (*s < '0' || *s > '9') return -1;
    unsigned n = 0;
    int digits = 0;
    while (*s >= '0' && *s <= '9') {
      n = n * 10 + (unsigned)(*s++ - '0');
      if (++digits > 3 || n > 255) return -1;
    }
    v[k] = n;
    if (k < 3 && *s++ != '.') return -1;
  }
  while (*s == ' ' || *s == '\n' || *s == '\r') s++;
  if (*s) return -1;
  out->s_addr = htonl(v[0] << 24 | v[1] << 16 | v[2] << 8 | v[3]);
  return 0;
}

int net_open(const char *host, int port, uint32_t id) {
  net_close();
  struct sockaddr_in a;
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons((uint16_t)port);
  if (parse_ip(host, &a.sin_addr) != 0) return -1;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) return -2;
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
  /* (no connect(): it could fail on a device for a moment without a route, and the game said
   * the address was wrong; each datagram goes to N.to) */
  memset(&N, 0, sizeof N);
  N.fd = fd;
  N.id = id;
  N.to = a;
  N.last_hello = -1e9;
  return 0;
}

int net_open_gk(uint32_t id) {
  net_close();
  memset(&N, 0, sizeof N);
  N.fd = -1;
  N.gk = 1;
  N.id = id;
  N.last_hello = -1e9;
  return 0;
}

void net_close(void) {
  if (!net_is_open()) return;
  unsigned char b[8];
  head(b, BYE);
  put32(b + 4, N.id);
  tx(b, sizeof b);
  if (N.fd >= 0) close(N.fd);
  memset(&N, 0, sizeof N);
  N.fd = -1;
}

int net_is_open(void) { return N.fd >= 0 || N.gk; }

const net_lobby *net_lobby_now(void) { return &N.lobby; }

double net_silence(double now) { return N.heard ? now - N.last_heard : 1e9; }

/* a player record: u32 id, u8 status (lobby only), char name[16], u8 n, profile[n] */
static int read_player(const unsigned char *p, int len, net_player *pl, int with_status) {
  int need = 4 + with_status + NET_NAME + 1;
  if (len < need) return -1;
  memset(pl, 0, sizeof *pl);
  pl->id = get32(p);
  pl->status = with_status ? p[4] : 1;
  memcpy(pl->name, p + 4 + with_status, NET_NAME);
  int n = p[need - 1];
  if (len < need + n) return -1;
  pl->profile_len = n > NET_PROFILE ? NET_PROFILE : n;
  memcpy(pl->profile, p + need, (size_t)pl->profile_len);
  return need + n;
}

static int relay_got(const unsigned char *b, int len, double now); /* (v2.4) */
static int hello_encode(unsigned char *h, int cap, uint32_t id, int status, const char *name, const unsigned char *profile, int pl, int relay);
static void relay_send_pending(void);

static void got(const unsigned char *b, int len, double now) {
  if (len < 4 || b[0] != 'W' || b[1] != 'V' || b[2] != 1) return;
  if (N.relay && relay_got(b, len, now)) return; /* (v2.4) the relay's own kinds */
  const unsigned char *p = b + 4;
  len -= 4;
  if (b[3] == RACE) {
    if (len < 5 + CAR_LINK_PACKET) return;
    uint32_t race = get32(p);
    int pcb = p[4];
    if (race != N.race || pcb < 0 || pcb >= CAR_LINK_MACHINES) return;
    uint32_t count = get32(p + 5 + PACKET_COUNT);
    if (N.rx_any[pcb] && (int32_t)(count - N.rx_count[pcb]) < 0) return; /* came in late */
    memcpy(N.rx[pcb], p + 5, CAR_LINK_PACKET);
    N.rx_count[pcb] = count;
    N.rx_any[pcb] = N.rx_new[pcb] = 1;
    N.rx_at[pcb] = now;
    N.last_heard = now, N.heard = 1;
    return;
  }
  if (b[3] == CLOSED) {
    N.closed = 1;
    return;
  }
  /* (v2.3) KICK: u32 host, u32 the player taken out (this one: out of the versus); REFUSED: u8
   * the reason this one is not let in */
  if (b[3] == KICK) {
    if (len >= 8 && get32(p + 4) == N.id) N.kicked = 1;
    return;
  }
  if (b[3] == REFUSED) {
    if (len >= 1) N.refused = p[0] ? p[0] : NET_REFUSE_CLOSED;
    return;
  }
  if (b[3] == GHOST) {
    if (len >= 8 && ghost_fn && get32(p) != N.id) ghost_fn(p, len);
    return;
  }
  if (b[3] == LOBBY) {
    if (len < 9) return;
    net_lobby l;
    memset(&l, 0, sizeof l);
    l.connected = 1;
    l.n = p[0] > NET_PLAYERS ? NET_PLAYERS : p[0];
    l.me = p[1];
    l.host = p[2];
    int at = 9;
    for (int i = 0; i < l.n; ++i) {
      if (at + 1 > len) return;
      int k = read_player(p + at + 1, len - at - 1, &l.player[i], 1);
      if (k < 0) return;
      at += 1 + k;
    }
    /* (v2.3) the room's flag after the players (none from a v2.2 host or server: unknown) */
    l.room = at < len && (p[at] & 0xf0) == NET_ROOM_MARK ? p[at] & (NET_ROOM_CLOSED | NET_ROOM_FULL) : -1;
    N.lobby = l;
    N.refused = 0; /* (in: let in after all) */
    N.last_heard = now, N.heard = 1;
    return;
  }
  if (b[3] == GO) {
    if (len < 12) return;
    net_go g;
    memset(&g, 0, sizeof g);
    g.race = get32(p);
    g.seed = get32(p + 4);
    g.ramp = p[8];
    g.km = p[9];
    g.n = p[10] > NET_PLAYERS ? NET_PLAYERS : p[10];
    g.pcb = p[11];
    if (g.race == N.go.race || g.race == N.race) return; /* said again */
    int at = 12;
    for (int i = 0; i < g.n; ++i) {
      if (at + 1 > len) return;
      int pcb = p[at];
      net_player pl;
      int k = read_player(p + at + 1, len - at - 1, &pl, N.relay); /* (v2.4: a relay's carry a status) */
      if (k < 0 || pcb >= NET_PLAYERS) return;
      g.player[pcb] = pl;
      at += 1 + k;
    }
    if (at < len) g.song = p[at]; /* after the machines: a newer host's */
    N.go = g;
    N.go_new = 1;
    N.last_heard = now, N.heard = 1;
  }
}

void net_poll(double now, int status, const char *name, const unsigned char *profile, int profile_len) {
  if (!net_is_open()) return;
  N.now = now;
  unsigned char b[2048];
  for (int n; (n = rx(b, sizeof b)) > 0;) got(b, n, now);
  int pl = profile_len < 0 ? 0 : profile_len > NET_PROFILE ? NET_PROFILE : profile_len;
  if (N.relay) { /* (v2.4) the newest name and profile for AUTO/CREATE/JOIN; a PING a second */
    if (name) snprintf(N.name, sizeof N.name, "%s", name);
    if (profile) memcpy(N.profile, profile, (size_t)pl), N.plen = pl;
    if (!N.chosen && status == 0) status = RELAY_BROWSING;
    if (N.pend_kind && now - N.pend_at >= 1.0) relay_send_pending();
    if (now - N.ping_sent >= PING_EVERY) {
      unsigned char q[13];
      head(q, R_PING);
      N.ping_nonce = (uint32_t)(now * 1000.0) | 1u;
      put32(q + 4, N.ping_nonce);
      q[8] = 'N';
      put32(q + 9, N.id);
      N.ping_sent = now;
      tx(q, sizeof q);
    }
  }
  /* twice a second, and at once on a new status (the area vote's card) */
  if (now - N.last_hello < HELLO_EVERY && status == N.last_status) return;
  N.last_hello = now;
  N.last_status = status;
  unsigned char h[4 + 4 + 1 + NET_NAME + 1 + NET_PROFILE + 16 + 2 + VROOM_LABEL_MAX];
  int at = hello_encode(h, sizeof h, N.id, status, name, profile, pl, N.relay);
  if (at > 0) tx(h, at);
}

/* HELLO: u32 id, u8 status, char name[16], u8 n, profile[n]; a relay's adds the app key, 'X' '1'
 * u8 platform u32 app_build u8 vs_build u8 tlen 0, 'N' proto and 'V' u8 len the game version */
static int hello_encode(unsigned char *h, int cap, uint32_t id, int status, const char *name, const unsigned char *profile, int pl, int relay) {
  int vl = (int)strlen(VROOM_OWN);
  pl = pl < 0 ? 0 : pl > NET_PROFILE ? NET_PROFILE : pl;
  if (cap < 10 + NET_NAME + pl + (relay ? 4 + 9 + 2 + 2 + vl : 0)) return -1;
  head(h, HELLO);
  put32(h + 4, id);
  h[8] = (unsigned char)status;
  memset(h + 9, 0, NET_NAME);
  if (name) memcpy(h + 9, name, strlen(name) < NET_NAME ? strlen(name) : NET_NAME);
  h[9 + NET_NAME] = (unsigned char)pl;
  if (pl) memcpy(h + 10 + NET_NAME, profile, (size_t)pl);
  int at = 10 + NET_NAME + pl;
  if (relay) { /* (v2.4) app key, 'X' '1' u8 platform u32 app_build u8 vs_build u8 tlen 0, 'N' proto */
    put32(h + at, RELAY_APP_KEY);
    at += 4;
    h[at] = 'X', h[at + 1] = '1', h[at + 2] = RELAY_PLATFORM;
    put32(h + at + 3, RELAY_APP_BUILD);
    h[at + 7] = RELAY_VS_BUILD, h[at + 8] = 0;
    at += 9;
    h[at] = 'N', h[at + 1] = RELAY_PROTO;
    at += 2;
    /* the game version (vroom.h): rooms of another version are not joined (an older relay stops
     * reading at the 'N') */
    h[at] = 'V', h[at + 1] = (unsigned char)vl;
    memcpy(h + at + 2, VROOM_OWN, (size_t)vl);
    at += 2 + vl;
  }
  return at;
}

int net_relay_hello_encode(unsigned char *h, int cap, uint32_t id, int status, const char *name, const unsigned char *profile, int pl) {
  return hello_encode(h, cap, id, status, name, profile, pl, 1);
}

void net_start(int ramp, int km, int song) {
  if (!net_is_open()) return;
  unsigned char b[7];
  head(b, START);
  b[4] = (unsigned char)ramp;
  b[5] = (unsigned char)km;
  b[6] = (unsigned char)song;
  tx(b, sizeof b);
}

int net_host_closed(void) { return N.closed; }

void net_ghost_handler(void (*fn)(const unsigned char *body, int len)) { ghost_fn = fn; }

uint32_t net_my_id(void) { return N.id; }

void net_ghost_send(const unsigned char *body, int len) {
  if (!net_is_open() || len <= 0 || len > NET_GHOST_MAX || N.relay) return; /* (v2.4: 10 is a relay's LIST) */
  unsigned char b[4 + NET_GHOST_MAX];
  head(b, GHOST);
  memcpy(b + 4, body, (size_t)len);
  tx(b, 4 + len);
}

int net_take_go(net_go *go) {
  if (!N.go_new) return 0;
  N.go_new = 0;
  *go = N.go;
  /* the race's packets from now on */
  N.race = N.go.race;
  memset(N.rx_new, 0, sizeof N.rx_new);
  memset(N.rx_any, 0, sizeof N.rx_any);
  memset(N.rx_at, 0, sizeof N.rx_at);
  return 1;
}

/* (v2.3) */
double net_race_silence(int pcb, double now) {
  if (pcb < 0 || pcb >= CAR_LINK_MACHINES || !N.rx_any[pcb]) return 1e9;
  return now - N.rx_at[pcb];
}

/* (v2.3) the room's requests to the host, said three times (UDP; each one sets, not toggles) */
void net_room(int closed) {
  if (!net_is_open() || N.relay) return; /* (v2.4: 11 is a relay's ROOMS) */
  unsigned char b[9];
  head(b, ROOM);
  put32(b + 4, N.id);
  b[8] = (unsigned char)(closed != 0);
  for (int t = 0; t < 3; ++t) tx(b, sizeof b);
}

void net_kick(uint32_t id) {
  if (!net_is_open() || !id || N.relay) return; /* (v2.4: 12 is a relay's CREATE) */
  unsigned char b[12];
  head(b, KICK);
  put32(b + 4, N.id);
  put32(b + 8, id);
  for (int t = 0; t < 3; ++t) tx(b, sizeof b);
}

int net_refused(void) { return N.refused; }
int net_kicked(void) { return N.kicked; }

void net_race_send(const unsigned char *packet, int len) {
  if (!net_is_open() || !N.race || len > CAR_LINK_PACKET) return;
  unsigned char b[4 + 5 + CAR_LINK_PACKET];
  head(b, RACE);
  put32(b + 4, N.race);
  b[8] = (unsigned char)N.go.pcb;
  memcpy(b + 9, packet, (size_t)len);
  tx(b, 9 + len);
}

void net_race_receive(void) {
  if (!net_is_open()) return;
  unsigned char b[2048];
  for (int n; (n = rx(b, sizeof b)) > 0;) got(b, n, N.now);
  for (int k = 0; k < CAR_LINK_MACHINES; ++k)
    if (N.rx_new[k]) {
      car_link_deliver(k, N.rx[k], CAR_LINK_PACKET);
      N.rx_new[k] = 0;
    }
}

/* ---- the LAN's games: a DISCOVER to the Wi-Fi's broadcast address (and the loopback, for a
 * host on this machine: the harness) every SCAN_EVERY s, each host's OFFER kept till it has
 * been quiet SCAN_GONE s */
static struct {
  int fd;
  double last;
  net_found found[NET_FOUND];
  int n;
} S = { .fd = -1 };

void net_scan(double now, int on) {
  if (!on) {
    if (S.fd >= 0) close(S.fd);
    memset(&S, 0, sizeof S);
    S.fd = -1;
    return;
  }
  if (S.fd < 0) {
    S.fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (S.fd < 0) return;
    int one = 1;
    setsockopt(S.fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
    fcntl(S.fd, F_SETFL, fcntl(S.fd, F_GETFL, 0) | O_NONBLOCK);
    S.last = -1e9;
  }
  unsigned char b[256];
  for (;;) {
    struct sockaddr_in from;
    socklen_t al = sizeof from;
    ssize_t n = recvfrom(S.fd, b, sizeof b, 0, (struct sockaddr *)&from, &al);
    if (n <= 0) break;
    if (n < 9 + NET_NAME || b[0] != 'W' || b[1] != 'V' || b[2] != 1 || b[3] != OFFER) continue;
    char ip[16] = { 0 };
    if (!inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip)) continue;
    uint32_t id = get32(b + 5 + NET_NAME);
    int k = 0;
    while (k < S.n && S.found[k].id != id) k++;
    if (k == S.n) {
      if (S.n >= NET_FOUND) continue;
      S.n++;
      memset(&S.found[k], 0, sizeof S.found[k]);
    }
    net_found *f = &S.found[k];
    /* (the loopback's answer only till the Wi-Fi's comes) */
    int loop = from.sin_addr.s_addr == htonl(INADDR_LOOPBACK);
    if (!f->ip[0] || !loop) memcpy(f->ip, ip, sizeof f->ip);
    f->id = id;
    memcpy(f->name, b + 5, NET_NAME);
    f->name[NET_NAME] = 0;
    f->n = b[4];
    f->seen = now;
  }
  for (int k = S.n - 1; k >= 0; --k)
    if (now - S.found[k].seen > SCAN_GONE) memmove(&S.found[k], &S.found[k + 1], sizeof S.found[0] * (size_t)(--S.n - k));
  if (now - S.last < SCAN_EVERY) return;
  S.last = now;
  unsigned char d[4];
  head(d, DISCOVER);
  struct sockaddr_in to;
  memset(&to, 0, sizeof to);
  to.sin_family = AF_INET;
  to.sin_port = htons(NET_PORT);
  to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
  sendto(S.fd, d, sizeof d, 0, (struct sockaddr *)&to, sizeof to);
  to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  sendto(S.fd, d, sizeof d, 0, (struct sockaddr *)&to, sizeof to);
}

int net_found_list(net_found *out, int max) {
  int n = S.n < max ? S.n : max;
  memcpy(out, S.found, sizeof out[0] * (size_t)n);
  return n;
}

/* ---- (v2.4) ONLINE: a relay's dialect (the friend's vs_server 3; RELAYS.md). Its kinds 10.. are
 * its own (LIST/ROOMS/CREATE/JOIN/AUTO/REFUSED/PING/PONG/START_AT/RESULT/RACEZ), read only on a
 * relay's connection; HELLO/LOBBY/START/GO/RACE/BYE are the same as above (GO's machines carry a
 * status byte, read_player's). */

/* RACEZ's packet: a zero byte is followed by its run length (1..255), any other byte stands for itself */
static int zrle_decode(const unsigned char *in, int n, unsigned char *out, int want) {
  int o = 0;
  for (int i = 0; i < n;) {
    if (in[i]) {
      if (o >= want) return -1;
      out[o++] = in[i++];
      continue;
    }
    if (i + 1 >= n || !in[i + 1] || o + in[i + 1] > want) return -1;
    memset(out + o, 0, in[i + 1]);
    o += in[i + 1];
    i += 2;
  }
  return o;
}

/* ROOMS: u8 count, per room u32 id, char name[16], u8 players, u8 max, u8 racing; then the
 * trailers, each read only when complete and in this order: 'X' '1' (per room u8 vs_build, u32
 * app_build, u8 platform, char host[16]), 'Q' '1' (per room u16 the host's rtt, u8 flags), 'V' '1'
 * (per room u8 len, the game version); anything else ends the reading (a newer relay's) */
int net_relay_rooms_parse(const unsigned char *p, int n, net_room *out, int max) {
  if (n < 1 || max < 0) return -1;
  int total = p[0], count = total > max ? max : total, at = 1;
  for (int i = 0; i < total; ++i, at += 4 + NET_ROOM_NAME + 3) {
    if (at + 4 + NET_ROOM_NAME + 3 > n) return -1;
    if (i >= count) continue;
    net_room *r = &out[i];
    memset(r, 0, sizeof *r);
    r->id = get32(p + at);
    memcpy(r->name, p + at + 4, NET_ROOM_NAME);
    r->players = p[at + 4 + NET_ROOM_NAME];
    r->max = p[at + 5 + NET_ROOM_NAME];
    r->racing = p[at + 6 + NET_ROOM_NAME];
    r->host_rtt = -1;
  }
  if (at + 2 <= n && p[at] == 'X' && p[at + 1] == '1') {
    at += 2;
    for (int i = 0; i < total && at + 6 + NET_NAME <= n; ++i, at += 6 + NET_NAME) {
      if (i >= count) continue;
      net_room *r = &out[i];
      r->has_ext = 1;
      r->vs_build = p[at];
      r->app_build = (int)get32(p + at + 1);
      r->platform = p[at + 5];
      memcpy(r->host, p + at + 6, NET_NAME);
    }
  }
  if (at + 2 <= n && p[at] == 'Q' && p[at + 1] == '1') {
    at += 2;
    for (int i = 0; i < total && at + 3 <= n; ++i, at += 3)
      if (i < count) {
        int v = p[at] | p[at + 1] << 8;
        out[i].host_rtt = v == 0xffff ? -1 : v;
      }
  }
  if (at + 2 <= n && p[at] == 'V' && p[at + 1] == '1') {
    at += 2;
    for (int i = 0; i < total && at + 1 <= n; ++i) {
      int len = p[at];
      if (at + 1 + len > n) break; /* (cut short: the rest stay unknown) */
      if (i < count) vroom_label_ok(p + at + 1, len, out[i].version);
      at += 1 + len;
    }
  }
  return count;
}

/* 1: the datagram was the relay's own (taken here); 0: the shared code reads it */
static int relay_got(const unsigned char *b, int len, double now) {
  const unsigned char *p = b + 4;
  int n = len - 4;
  switch (b[3]) {
  case LOBBY:
    N.chosen = 1; /* (in a room: the HELLOs' own status from now on) */
    N.pend_kind = 0;
    N.refused_msg[0] = 0;
    return 0;
  case RACE: case GO: case CLOSED: case BYE: return 0;
  case R_RACEZ: { /* u32 race, u8 pcb, u8 ver 1, u16 coded length, the coded packet, RACE's trailer */
    if (len < 12) return 1;
    int clen = b[10] | b[11] << 8;
    if (b[9] != 1 || 12 + clen > len) return 1;
    unsigned char r[4 + 5 + CAR_LINK_PACKET];
    memcpy(r, b, 9);
    r[3] = RACE;
    if (zrle_decode(b + 12, clen, r + 9, CAR_LINK_PACKET) == CAR_LINK_PACKET) got(r, sizeof r, now);
    return 1;
  }
  case R_PONG: /* u32 nonce, ... */
    if (n >= 4 && N.ping_nonce && get32(p) == N.ping_nonce) {
      double ms = (N.now - N.ping_sent) * 1000.0;
      N.rtt_ms = ms < 0 ? 0 : (int)(ms + 0.5);
      N.ping_nonce = 0;
      N.last_heard = now, N.heard = 1;
    }
    return 1;
  case R_REFUSED: { /* u8 reason, u32 min_build, char msg[48] */
    if (n < 5) return 1;
    int k = 0;
    for (int i = 5; i < n && i < 5 + 48 && p[i]; ++i)
      if (p[i] >= 32 && p[i] < 127) N.refused_msg[k++] = (char)p[i];
    N.refused_msg[k] = 0;
    if (!k) snprintf(N.refused_msg, sizeof N.refused_msg, "UPDATE REQUIRED: BUILD %u OR NEWER", (unsigned)get32(p + 1));
    N.refused_reason = p[0] ? p[0] : 1;
    N.pend_kind = 0;
    N.chosen = 0; /* (back to the room list: the HELLOs say browsing again, nothing auto-matched) */
    N.last_heard = now, N.heard = 1;
    return 1;
  }
  case R_ROOMS: { /* u8 count, per room ..; then the trailers (net_relay_rooms_parse) */
    net_room rooms[NET_ROOMS_MAX];
    int count = net_relay_rooms_parse(p, n, rooms, NET_ROOMS_MAX);
    if (count < 0) return 1;
    memcpy(N.rooms, rooms, sizeof rooms[0] * (size_t)count);
    N.nrooms = count;
    N.last_heard = now, N.heard = 1;
    return 1;
  }
  case R_START_AT: case R_RESULT: /* (N2's synced start and official result: not used yet) */
    N.last_heard = now, N.heard = 1;
    return 1;
  default: return 1; /* (nothing else of ours: 10.. are the relay's) */
  }
}

int net_relay_open(const char *host, int port, const char *name, const unsigned char *profile, int plen) {
  if (!host) return -1;
  char ip[16];
  struct in_addr a;
  if (parse_ip(host, &a) == 0) {
    snprintf(ip, sizeof ip, "%s", host);
  } else { /* a name: resolved once (blocking) */
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return -1;
    struct sockaddr_in *sa = (struct sockaddr_in *)res->ai_addr;
    const char *ok = inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof ip);
    freeaddrinfo(res);
    if (!ok) return -1;
  }
  static uint32_t salt;
  /* (two machines started the same second must not share an id: the relay knows players by it) */
  uint32_t id = (uint32_t)time(NULL) * 2654435761u ^ (uint32_t)clock() * 40503u ^ (uint32_t)(uintptr_t)&a ^ ++salt * 0x9e3779b9u ^
                ((uint32_t)rand() << 16 ^ (uint32_t)rand() ^ (uint32_t)(uintptr_t)&salt * 0x85ebca6bu);
  if (!id) id = 1;
  int e = net_open(ip, port, id);
  if (e) return e;
  N.relay = 1;
  N.rtt_ms = -1;
  N.ping_sent = -1e9;
  if (name) snprintf(N.name, sizeof N.name, "%s", name);
  N.plen = plen < 0 ? 0 : plen > NET_PROFILE ? NET_PROFILE : plen;
  if (N.plen && profile) memcpy(N.profile, profile, (size_t)N.plen);
  else N.plen = 0;
  return 0;
}

int net_is_relay(void) { return net_is_open() && N.relay; }

static void relay_send_pending(void) {
  if (!net_is_relay() || !N.pend_kind) return;
  unsigned char b[4 + 4 + 4 + NET_ROOM_NAME + 1 + NET_PROFILE + 2 + VROOM_LABEL_MAX];
  head(b, N.pend_kind);
  put32(b + 4, N.id);
  int at = 8;
  if (N.pend_kind == R_JOIN) put32(b + at, N.pend_room), at += 4;
  if (N.pend_kind == R_CREATE) memcpy(b + at, N.pend_name, NET_ROOM_NAME), at += NET_ROOM_NAME;
  b[at] = (unsigned char)N.plen;
  if (N.plen) memcpy(b + at + 1, N.profile, (size_t)N.plen);
  at += 1 + N.plen;
  if (N.pend_kind == R_CREATE) { /* the room's game version (vroom.h; an older relay reads no further) */
    int vl = (int)strlen(VROOM_OWN);
    b[at] = 'V', b[at + 1] = (unsigned char)vl;
    memcpy(b + at + 2, VROOM_OWN, (size_t)vl);
    at += 2 + vl;
  }
  tx(b, at);
  N.pend_at = N.now;
}

static void relay_choose(int kind, uint32_t room, const char *name) {
  if (!net_is_relay()) return;
  N.chosen = 1;
  N.refused_msg[0] = 0;
  N.refused_reason = 0;
  N.pend_kind = kind;
  N.pend_room = room;
  memset(N.pend_name, 0, sizeof N.pend_name);
  if (name) memcpy(N.pend_name, name, strlen(name) < NET_ROOM_NAME ? strlen(name) : NET_ROOM_NAME);
  relay_send_pending();
}

void net_relay_auto(void) { relay_choose(R_AUTO, 0, NULL); }
void net_relay_join(unsigned id) { relay_choose(R_JOIN, id, NULL); }
void net_relay_create(const char *name) { relay_choose(R_CREATE, 0, name); }

void net_relay_list(void) {
  if (!net_is_relay()) return;
  unsigned char b[8];
  head(b, R_LIST);
  put32(b + 4, N.id);
  tx(b, sizeof b);
}

int net_relay_rooms(net_room *out, int max) {
  if (!N.relay) return 0;
  int n = N.nrooms < max ? N.nrooms : max;
  if (n > 0) memcpy(out, N.rooms, sizeof out[0] * (size_t)n);
  return n < 0 ? 0 : n;
}

const char *net_relay_refused(void) { return N.relay && N.refused_msg[0] ? N.refused_msg : NULL; }

int net_relay_refused_reason(void) { return N.relay && N.refused_msg[0] ? N.refused_reason : 0; }

int net_relay_rtt_ms(void) { return N.relay ? N.rtt_ms : -1; }
