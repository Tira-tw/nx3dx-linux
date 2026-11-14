/* (v2.4) The online versus relays: see relays.h. */
#include "relays.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#ifndef _WIN32
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/time.h>
#endif

#include "net.h"

#define BODY_MAX 16384
#define PING_ROUNDS 3
#define PING_WAIT_MS 700

typedef struct {
  char name[24];
  char hostname[64];
  char ip[16]; /* dotted IPv4, "" not resolved */
  int port;
  int online;
  int rtt_ms;
} relay;

static pthread_mutex_t M = PTHREAD_MUTEX_INITIALIZER;
static struct {
  int busy, state, min_build;
  relay r[NET_RELAYS_MAX];
  int n;
} R;

/* (the getters hand out copies: the worker may replace the list meanwhile) */
static char out_name[NET_RELAYS_MAX][24], out_ip[NET_RELAYS_MAX][16], out_hostname[NET_RELAYS_MAX][64];

#ifndef _WIN32
/* Android / iOS: until the platform's own (helper 03) is linked, no list (the fallback is used) */
__attribute__((weak)) int platform_https_get(const char *url, char *out, int max) {
  (void)url;
  if (out && max > 0) out[0] = 0;
  return -1;
}
#endif

/* ---- the built-in relays: "name\0host\0ip\0" x3, byte i XOR'd with 0xA7 + 29 i ---- */

static const unsigned char FALLBACK[145] = {
  0xf4, 0xad, 0x8f, 0x99, 0x7a, 0x48, 0x3a, 0x00, 0xea, 0xac, 0xa8, 0x96, 0x70, 0x45, 0x0c, 0x74,
  0x05, 0xf1, 0xdd, 0xaf, 0x92, 0x26, 0x4b, 0x2d, 0x29, 0x1d, 0xb4, 0xd8, 0xb6, 0x84, 0x23, 0x5d,
  0x28, 0x16, 0xea, 0x9e, 0x8e, 0xec, 0xdb, 0x23, 0x19, 0x75, 0x47, 0xb5, 0x97, 0xee, 0xec, 0xcd,
  0x24, 0x34, 0x04, 0x3d, 0xab, 0xed, 0xa4, 0x91, 0x8b, 0x1c, 0x4c, 0x25, 0x16, 0xa1, 0x83, 0xb8,
  0x82, 0x68, 0x40, 0x47, 0x75, 0x16, 0xfa, 0xc4, 0xae, 0xc1, 0x67, 0x43, 0x37, 0x4e, 0x0a, 0xf5,
  0xc5, 0xbf, 0xf1, 0x37, 0x13, 0x66, 0x5c, 0xb1, 0xb1, 0x88, 0xe9, 0xd8, 0x24, 0x00, 0x4d, 0x2f,
  0xd2, 0x84, 0x82, 0xbb, 0x95, 0x6c, 0x47, 0x33, 0x03, 0x8c, 0xcc, 0xb3, 0x80, 0x31, 0x33, 0x48,
  0x32, 0x18, 0xf0, 0xd7, 0xe5, 0x86, 0x6a, 0x54, 0x5e, 0x71, 0x17, 0xf3, 0xc7, 0xfe, 0x9a, 0x65,
  0x55, 0x2f, 0x61, 0x4d, 0xb5, 0x89, 0xe7, 0xc7, 0x21, 0x1d, 0x7f, 0x56, 0xad, 0x91, 0x8e, 0xe8,
  0xf7,
};

static int fallback_list(relay *out) {
  char s[sizeof FALLBACK];
  for (int i = 0; i < (int)sizeof FALLBACK; ++i) s[i] = (char)(FALLBACK[i] ^ (unsigned char)(0xA7 + i * 29));
  const char *p = s, *end = s + sizeof s;
  int n = 0;
  while (p < end && n < 3) {
    relay *r = &out[n];
    memset(r, 0, sizeof *r);
    snprintf(r->name, sizeof r->name, "%s", p), p += strlen(p) + 1;
    snprintf(r->hostname, sizeof r->hostname, "%s", p), p += strlen(p) + 1;
    snprintf(r->ip, sizeof r->ip, "%s", p), p += strlen(p) + 1;
    r->port = NET_PORT;
    r->online = 0; /* until a PONG */
    r->rtt_ms = -1;
    n++;
  }
  memset(s, 0, sizeof s);
  return n;
}

/* ---- a small reader for the one JSON shape GET /api/v1/relays returns (the hand-out's) ---- */

static const char *skip_value(const char *s, const char *end) {
  if (s >= end) return end;
  if (*s == '"') {
    for (++s; s < end; ++s) {
      if (*s == '\\') { ++s; continue; }
      if (*s == '"') return s + 1;
    }
    return end;
  }
  if (*s == '{' || *s == '[') {
    int depth = 0;
    for (; s < end; ++s) {
      if (*s == '"') { s = skip_value(s, end) - 1; continue; }
      if (*s == '{' || *s == '[') depth++;
      if (*s == '}' || *s == ']') {
        if (--depth == 0) return s + 1;
      }
    }
    return end;
  }
  while (s < end && *s != ',' && *s != '}' && *s != ']') ++s;
  return s;
}

static const char *ws(const char *s, const char *end) {
  while (s < end && (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t')) ++s;
  return s;
}

static const char *member(const char *obj, const char *end, const char *name) {
  if (!obj || obj >= end || *obj != '{') return NULL;
  const char *s = ws(obj + 1, end);
  size_t nl = strlen(name);
  while (s < end && *s == '"') {
    const char *k = s + 1;
    const char *kend = skip_value(s, end);
    s = ws(kend, end);
    if (s >= end || *s != ':') return NULL;
    const char *v = ws(s + 1, end);
    if ((size_t)(kend - 1 - k) == nl && !memcmp(k, name, nl)) return v;
    s = ws(skip_value(v, end), end);
    if (s < end && *s == ',') s = ws(s + 1, end);
  }
  return NULL;
}

static void str_value(const char *v, const char *end, char *out, int n) {
  int k = 0;
  if (v && v < end && *v == '"') {
    for (const char *s = v + 1; s < end && *s != '"' && k < n - 1; ++s) {
      char c = *s;
      if (c == '\\' && s + 1 < end) {
        c = *++s;
        if (c == 'u') { s += 4 < end - s ? 4 : end - s - 1; c = '?'; }
        else if (c == 'n' || c == 't' || c == 'r') c = ' ';
      }
      out[k++] = c;
    }
  }
  out[k] = 0;
}

static int int_value(const char *v, const char *end, int def) {
  if (!v || v >= end || (*v != '-' && (*v < '0' || *v > '9'))) return def;
  return atoi(v);
}

/* a dotted IPv4 address (leading zeros too, as net.c's parse_ip) */
static int is_ipv4(const char *s, struct in_addr *out) {
  unsigned v[4];
  for (int k = 0; k < 4; ++k) {
    if (*s < '0' || *s > '9') return 0;
    unsigned n = 0;
    int digits = 0;
    while (*s >= '0' && *s <= '9') {
      n = n * 10 + (unsigned)(*s++ - '0');
      if (++digits > 3 || n > 255) return 0;
    }
    v[k] = n;
    if (k < 3 && *s++ != '.') return 0;
  }
  if (*s) return 0;
  if (out) out->s_addr = htonl(v[0] << 24 | v[1] << 16 | v[2] << 8 | v[3]);
  return 1;
}

#if defined(__ANDROID__)
#define MY_PLATFORM "android"
#elif defined(__APPLE__)
#define MY_PLATFORM "ios"
#else
#define MY_PLATFORM "other"
#endif

/* the online relays of a body into out; -1 not the shape */
static int parse(const char *json, relay *out, int max, int *min_build) {
  *min_build = 0;
  const char *end = json + strlen(json);
  const char *top = ws(json, end);
  if (top >= end || *top != '{') return -1;
  const char *top_end = skip_value(top, end);
  const char *mb = member(top, top_end, "min_build");
  if (mb && *mb == '{') {
    const char *mbe = skip_value(mb, top_end);
    *min_build = int_value(member(mb, mbe, MY_PLATFORM), mbe, 0);
  }
  const char *arr = member(top, top_end, "relays");
  if (!arr || *arr != '[') return -1;
  const char *arr_end = skip_value(arr, top_end);
  int n = 0;
  const char *s = ws(arr + 1, arr_end);
  while (s < arr_end && *s == '{' && n < max) {
    const char *oe = skip_value(s, arr_end);
    relay *r = &out[n];
    memset(r, 0, sizeof *r);
    char key[24], status[16], host[64];
    str_value(member(s, oe, "key"), oe, key, sizeof key);
    str_value(member(s, oe, "name"), oe, r->name, sizeof r->name);
    str_value(member(s, oe, "host"), oe, host, sizeof host);
    str_value(member(s, oe, "ip"), oe, r->ip, sizeof r->ip);
    str_value(member(s, oe, "status"), oe, status, sizeof status);
    r->port = int_value(member(s, oe, "port"), oe, NET_PORT);
    r->online = !strcmp(status, "online");
    r->rtt_ms = -1;
    if (!r->name[0]) snprintf(r->name, sizeof r->name, "%s", key);
    snprintf(r->hostname, sizeof r->hostname, "%s", host[0] ? host : r->ip);
    if (!is_ipv4(r->ip, NULL)) r->ip[0] = 0;
    if (!r->ip[0] && is_ipv4(host, NULL)) snprintf(r->ip, sizeof r->ip, "%s", host);
    /* only an online relay with somewhere to send makes a row */
    if (r->online && (r->ip[0] || r->hostname[0]) && r->port > 0 && r->port < 65536) n++;
    s = ws(oe, arr_end);
    if (s < arr_end && *s == ',') s = ws(s + 1, arr_end);
  }
  return n;
}

/* ---- the round trips ---- */

/* (v2.4 fix) iOS before 10 has no clock_gettime (MinimumOSVersion 9.3; app.c now_ms the same) */
#ifdef __APPLE__
#include <mach/mach_time.h>
static double now_s(void) {
  static mach_timebase_info_data_t tb;
  if (!tb.denom) mach_timebase_info(&tb);
  return (double)mach_absolute_time() * tb.numer / tb.denom * 1e-9;
}
#else
static double now_s(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}
#endif

static void put32(unsigned char *p, unsigned v) {
  p[0] = (unsigned char)v, p[1] = (unsigned char)(v >> 8), p[2] = (unsigned char)(v >> 16), p[3] = (unsigned char)(v >> 24);
}
static unsigned get32(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24; }

/* a host name to a dotted IPv4 address (blocking: the worker's thread) */
static int resolve(const char *host, char *ip, int n) {
  struct addrinfo hints, *res = NULL;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  if (!host[0] || getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return 0;
  unsigned a = ntohl(((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr);
  freeaddrinfo(res);
  snprintf(ip, (size_t)n, "%u.%u.%u.%u", a >> 24, (a >> 16) & 255, (a >> 8) & 255, a & 255);
  return 1;
}

/* PING each relay PING_ROUNDS times; rtt[i] the best, -1 none */
static void ping_all(const relay *r, int n, int *rtt) {
  for (int i = 0; i < n; ++i) rtt[i] = -1;
  int fd = (int)socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) return;
  struct sockaddr_in to[NET_RELAYS_MAX];
  int ok[NET_RELAYS_MAX];
  for (int i = 0; i < n; ++i) {
    memset(&to[i], 0, sizeof to[i]);
    to[i].sin_family = AF_INET;
    to[i].sin_port = htons((unsigned short)r[i].port);
    ok[i] = is_ipv4(r[i].ip, &to[i].sin_addr);
  }
  unsigned id = (unsigned)net_my_id();
  if (!id) id = (unsigned)rand() | 0x10000u;
  unsigned base = ((unsigned)rand() << 12) & 0x7fff0000u;
  for (int round = 0; round < PING_ROUNDS; ++round) {
    double sent[NET_RELAYS_MAX];
    int waiting = 0;
    for (int i = 0; i < n; ++i) {
      if (!ok[i]) continue;
      unsigned char p[13] = { 'W', 'V', 1, 16 }; /* PING: nonce, 'N', our id */
      put32(p + 4, base | 0x80000000u | (unsigned)round << 8 | (unsigned)i);
      p[8] = 'N';
      put32(p + 9, id);
      sent[i] = now_s();
      if (sendto(fd, (const char *)p, sizeof p, 0, (struct sockaddr *)&to[i], sizeof to[i]) > 0) waiting |= 1 << i;
    }
    double until = now_s() + PING_WAIT_MS / 1000.0;
    while (waiting) {
      double left = until - now_s();
      if (left <= 0) break;
      fd_set rs;
      FD_ZERO(&rs);
      FD_SET(fd, &rs);
      struct timeval tv;
      tv.tv_sec = (long)left;
      tv.tv_usec = (long)((left - (double)tv.tv_sec) * 1e6);
      if (select(fd + 1, &rs, NULL, NULL, &tv) <= 0) break;
      unsigned char b[64];
      struct sockaddr_in from;
      socklen_t al = sizeof from;
      int len = (int)recvfrom(fd, (char *)b, sizeof b, 0, (struct sockaddr *)&from, &al);
      if (len < 8 || b[0] != 'W' || b[1] != 'V' || b[2] != 1 || b[3] != 17) continue; /* PONG */
      unsigned nonce = get32(b + 4);
      int i = (int)(nonce & 255);
      if (i >= n || (nonce & ~255u) != (base | 0x80000000u | (unsigned)round << 8) || !(waiting & 1 << i) ||
          from.sin_addr.s_addr != to[i].sin_addr.s_addr)
        continue;
      waiting &= ~(1 << i);
      int ms = (int)((now_s() - sent[i]) * 1000.0 + 0.5);
      if (ms < 0) ms = 0;
      if (rtt[i] < 0 || ms < rtt[i]) rtt[i] = ms;
    }
  }
  close(fd);
}

/* ---- the worker ---- */

static void *worker(void *arg) {
  (void)arg;
  relay list[NET_RELAYS_MAX];
  int n = -1, min_build = 0, state = 2;
  char *body = malloc(BODY_MAX);
  if (body) {
    body[0] = 0;
    if (platform_https_get(NET_RELAYS_URL, body, BODY_MAX) > 0) {
      body[BODY_MAX - 1] = 0;
      n = parse(body, list, NET_RELAYS_MAX, &min_build);
    }
    free(body);
  }
  if (n <= 0) {
    n = fallback_list(list);
    state = 3;
  }
  for (int i = 0; i < n; ++i) /* a host name the website gave without an address */
    if (!list[i].ip[0]) resolve(list[i].hostname, list[i].ip, sizeof list[i].ip);
  if (state == 3) /* the built-in addresses may be stale: the names first */
    for (int i = 0; i < n; ++i) {
      char ip[16];
      if (resolve(list[i].hostname, ip, sizeof ip)) snprintf(list[i].ip, sizeof list[i].ip, "%s", ip);
    }
  pthread_mutex_lock(&M);
  for (int i = 0; i < n; ++i) /* the same relay keeps its last round trip until the new one */
    for (int j = 0; j < R.n; ++j)
      if (!strcmp(R.r[j].hostname, list[i].hostname)) list[i].rtt_ms = R.r[j].rtt_ms;
  memcpy(R.r, list, sizeof list);
  R.n = n;
  R.min_build = min_build;
  R.state = state;
  pthread_mutex_unlock(&M);

  int rtt[NET_RELAYS_MAX];
  ping_all(list, n, rtt);
  pthread_mutex_lock(&M);
  for (int i = 0; i < n && i < R.n; ++i) {
    R.r[i].rtt_ms = rtt[i];
    if (state == 3) R.r[i].online = rtt[i] >= 0;
  }
  R.busy = 0;
  pthread_mutex_unlock(&M);
  return NULL;
}

void net_relays_refresh(void) {
  pthread_mutex_lock(&M);
  int start = !R.busy;
  if (start) {
    R.busy = 1;
    if (!R.state) R.state = 1;
  }
  pthread_mutex_unlock(&M);
  if (!start) return;
  pthread_t t;
  if (pthread_create(&t, NULL, worker, NULL) == 0) {
    pthread_detach(t);
  } else {
    pthread_mutex_lock(&M);
    R.busy = 0;
    pthread_mutex_unlock(&M);
  }
}

int net_relays_state(void) {
  pthread_mutex_lock(&M);
  int s = R.busy && !R.n ? 1 : R.state;
  pthread_mutex_unlock(&M);
  return s;
}

int net_relays_count(void) {
  pthread_mutex_lock(&M);
  int n = R.n;
  pthread_mutex_unlock(&M);
  return n;
}

int net_relays_min_build(void) {
  pthread_mutex_lock(&M);
  int m = R.min_build;
  pthread_mutex_unlock(&M);
  return m;
}

#define GET_STR(fn, field, buf)                                     \
  const char *fn(int i) {                                           \
    if (i < 0 || i >= NET_RELAYS_MAX) return "";                    \
    pthread_mutex_lock(&M);                                         \
    if (i < R.n) snprintf(buf[i], sizeof buf[i], "%s", R.r[i].field); \
    else buf[i][0] = 0;                                             \
    pthread_mutex_unlock(&M);                                       \
    return buf[i];                                                  \
  }
GET_STR(net_relay_name, name, out_name)
GET_STR(net_relay_host, ip, out_ip)
GET_STR(net_relay_hostname, hostname, out_hostname)

#define GET_INT(fn, expr, def)       \
  int fn(int i) {                    \
    int v = def;                     \
    pthread_mutex_lock(&M);          \
    if (i >= 0 && i < R.n) v = expr; \
    pthread_mutex_unlock(&M);        \
    return v;                        \
  }
GET_INT(net_relay_port, R.r[i].port, 0)
GET_INT(net_relays_rtt_ms, R.r[i].rtt_ms, -1)
GET_INT(net_relay_online, R.r[i].online, 0)
