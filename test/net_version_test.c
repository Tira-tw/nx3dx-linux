/* src/net.c's relay side of the rooms by game version: the HELLO's 'V' label, the ROOMS' 'V' '1'
 * trailer (with and without it, cut short, after an unknown trailer), CREATE's label, and a
 * version REFUSED (the reason, its words, back to browsing) against a fake relay on the loopback. */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "net.h"
#include "test.h"
#include "vroom.h"

/* (the game's, not linked here) */
void car_link_deliver(int pcb, const unsigned char *packet, int len) { (void)pcb, (void)packet, (void)len; }
int gk_send(int peer, const void *b, int len) { (void)peer, (void)b, (void)len; return -1; }
int gk_recv(void *b, int max, int *peer) { (void)b, (void)max, (void)peer; return 0; }

#define HEAD 4
#define REC (4 + 16 + 3)

static void test_hello(void) {
  unsigned char profile[21] = { 1, 2, 3 };
  unsigned char h[256];
  int n = net_relay_hello_encode(h, sizeof h, 0x11223344u, 250, "TUX", profile, sizeof profile);
  /* WV1 1 | id | status | name 16 | n | profile 21 | key 4 | X1 plat build4 vs tlen | N proto | V len label */
  int at = 4 + 4 + 1 + 16 + 1 + 21;
  CHECK(n == at + 4 + 9 + 2 + 2 + 5);
  CHECK(h[0] == 'W' && h[1] == 'V' && h[2] == 1 && h[3] == 1 && h[8] == 250 && !memcmp(h + 9, "TUX", 4));
  CHECK(h[at] == 0x33 && h[at + 1] == 0x4d && h[at + 2] == 0x4d && h[at + 3] == 0x57); /* the app key */
  at += 4;
  CHECK(h[at] == 'X' && h[at + 1] == '1' && (h[at + 3] | h[at + 4] << 8) == 240 && h[at + 8] == 0);
  at += 9;
  CHECK(h[at] == 'N');
  at += 2;
  CHECK(h[at] == 'V' && h[at + 1] == 5 && !memcmp(h + at + 2, "2.4.1", 5));
  /* a buffer too small: nothing written past it */
  CHECK(net_relay_hello_encode(h, 40, 1, 0, "TUX", profile, sizeof profile) == -1);
  /* the longest profile still fits the client's own buffer size */
  unsigned char big[NET_PROFILE] = { 0 };
  CHECK(net_relay_hello_encode(h, 4 + 4 + 1 + NET_NAME + 1 + NET_PROFILE + 16 + 2 + VROOM_LABEL_MAX, 1, 0, "X", big, NET_PROFILE) > 0);
}

/* a ROOMS body: n rooms, then the trailers asked for (each a string of tags: X Q V, or Z an unknown one) */
static int rooms_body(unsigned char *p, int n, const char *tags, const char *const *labels) {
  int at = 0;
  p[at++] = (unsigned char)n;
  for (int i = 0; i < n; ++i) {
    unsigned id = 0xc0000001u + (unsigned)i;
    memcpy(p + at, &id, 4);
    memset(p + at + 4, 0, 16);
    p[at + 4] = (unsigned char)('A' + i);
    p[at + 20] = 1, p[at + 21] = 4, p[at + 22] = 0;
    at += REC;
  }
  for (const char *t = tags; *t; ++t) {
    if (*t == 'X') {
      p[at++] = 'X', p[at++] = '1';
      for (int i = 0; i < n; ++i) {
        memset(p + at, 0, 22);
        p[at] = 221, p[at + 1] = 240, p[at + 5] = 2;
        at += 22;
      }
    } else if (*t == 'Q') {
      p[at++] = 'Q', p[at++] = '1';
      for (int i = 0; i < n; ++i) p[at++] = (unsigned char)(10 + i), p[at++] = 0, p[at++] = 0;
    } else if (*t == 'V') {
      p[at++] = 'V', p[at++] = '1';
      for (int i = 0; i < n; ++i) {
        int l = (int)strlen(labels[i]);
        p[at++] = (unsigned char)l;
        memcpy(p + at, labels[i], (size_t)l), at += l;
      }
    } else {
      p[at++] = 'Z', p[at++] = '9', p[at++] = 7;
    }
  }
  return at;
}

static void test_rooms_parse(void) {
  unsigned char p[512];
  net_room r[NET_ROOMS_MAX];
  const char *labels[3] = { "2.4.1", "1.7", "" };
  /* a new relay: X, Q, V */
  int n = rooms_body(p, 3, "XQV", labels);
  CHECK(net_relay_rooms_parse(p, n, r, NET_ROOMS_MAX) == 3);
  CHECK(!strcmp(r[0].version, "2.4.1") && !strcmp(r[1].version, "1.7") && r[2].version[0] == 0);
  CHECK(r[0].has_ext && r[0].app_build == 240 && r[1].host_rtt == 11 && !strcmp(r[1].name, "B"));
  /* a new relay's room list taken in part (max 2): the V trailer still read for those */
  CHECK(net_relay_rooms_parse(p, n, r, 2) == 2 && !strcmp(r[1].version, "1.7"));
  /* an older relay (no V): versions unknown, the rest as before */
  n = rooms_body(p, 2, "XQ", labels);
  CHECK(net_relay_rooms_parse(p, n, r, NET_ROOMS_MAX) == 2 && r[0].version[0] == 0 && r[1].host_rtt == 11);
  /* the oldest (no trailer at all) */
  n = rooms_body(p, 2, "", labels);
  CHECK(net_relay_rooms_parse(p, n, r, NET_ROOMS_MAX) == 2 && !r[0].has_ext && r[0].host_rtt == -1 && r[0].version[0] == 0);
  /* V cut inside the second label: the first kept, the second unknown */
  n = rooms_body(p, 2, "XQV", labels);
  CHECK(net_relay_rooms_parse(p, n - 2, r, NET_ROOMS_MAX) == 2 && !strcmp(r[0].version, "2.4.1") && r[1].version[0] == 0);
  /* V cut right after its tag */
  int cut = rooms_body(p, 2, "XQ", labels);
  p[cut] = 'V', p[cut + 1] = '1';
  CHECK(net_relay_rooms_parse(p, cut + 2, r, NET_ROOMS_MAX) == 2 && r[0].version[0] == 0);
  /* a newer relay's unknown trailer after V: ignored */
  n = rooms_body(p, 3, "XQVZ", labels);
  CHECK(net_relay_rooms_parse(p, n, r, NET_ROOMS_MAX) == 3 && !strcmp(r[0].version, "2.4.1"));
  /* an unknown trailer before V: the reading stops there (versions unknown, nothing locked) */
  n = rooms_body(p, 2, "XQZV", labels);
  CHECK(net_relay_rooms_parse(p, n, r, NET_ROOMS_MAX) == 2 && r[0].version[0] == 0 && r[0].host_rtt == 10);
  /* a label that is not one (a space, too long): unknown */
  const char *bad[2] = { "2.4 1", "0123456789abcdef" };
  n = rooms_body(p, 2, "XQV", bad);
  CHECK(net_relay_rooms_parse(p, n, r, NET_ROOMS_MAX) == 2 && r[0].version[0] == 0 && r[1].version[0] == 0);
  /* a cut room record: nothing taken */
  n = rooms_body(p, 2, "", labels);
  CHECK(net_relay_rooms_parse(p, n - 1, r, NET_ROOMS_MAX) == -1);
  CHECK(net_relay_rooms_parse(p, 0, r, NET_ROOMS_MAX) == -1);
  /* no rooms, with trailers */
  n = rooms_body(p, 0, "XQV", labels);
  CHECK(net_relay_rooms_parse(p, n, r, NET_ROOMS_MAX) == 0);
}

static int udp(int *port) {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in a;
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bind(fd, (struct sockaddr *)&a, sizeof a);
  socklen_t l = sizeof a;
  getsockname(fd, (struct sockaddr *)&a, &l);
  *port = ntohs(a.sin_port);
  struct timeval tv = { 0, 200000 };
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  return fd;
}

/* the next datagram of `kind` the client sent, its length (0 none) */
static int next_of(int fd, int kind, unsigned char *b, int max, struct sockaddr_in *from) {
  for (int i = 0; i < 40; ++i) {
    socklen_t l = sizeof *from;
    ssize_t n = recvfrom(fd, b, (size_t)max, 0, (struct sockaddr *)from, &l);
    if (n <= 0) return 0;
    if (n >= 4 && b[3] == kind) return (int)n;
  }
  return 0;
}

static void test_fake_relay(void) {
  int port;
  int fd = udp(&port);
  unsigned char profile[21] = { 0 };
  CHECK(net_relay_open("127.0.0.1", port, "TUX", profile, sizeof profile) == 0);
  unsigned char b[512];
  struct sockaddr_in from;
  double t = 10.0;
  net_poll(t, 0, "TUX", profile, sizeof profile);
  int n = next_of(fd, 1, b, sizeof b, &from);
  CHECK(n > 0 && b[8] == 250); /* browsing: no room chosen */
  CHECK(n > 7 && b[n - 7] == 'V' && b[n - 6] == 5 && !memcmp(b + n - 5, "2.4.1", 5));

  /* CREATE ROOM (the picker's own version): CREATE ends with 'V' 5 "2.4.1" */
  net_relay_create("TUX ROOM");
  n = next_of(fd, 12, b, sizeof b, &from);
  CHECK(n == 4 + 4 + 16 + 1 + 21 + 2 + 5);
  CHECK(n > 7 && !memcmp(b + 8, "TUX ROOM", 8) && b[n - 7] == 'V' && b[n - 6] == 5 && !memcmp(b + n - 5, "2.4.1", 5));

  /* JOIN carries no label (the relay knows the player's from its HELLO) */
  net_relay_join(0xc0000002u);
  n = next_of(fd, 13, b, sizeof b, &from);
  CHECK(n == 4 + 4 + 4 + 1 + 21);

  /* the relay's version refusal: reason 2, its words; the client back to browsing */
  unsigned char ref[4 + 5 + 48] = { 'W', 'V', 1, 15, 2, 0, 0, 0, 0 };
  memcpy(ref + 9, "VERSION 1.7 ROOM, YOU HAVE 2.4.1", 32);
  sendto(fd, ref, sizeof ref, 0, (struct sockaddr *)&from, sizeof from);
  usleep(20000);
  t += 1.0;
  net_poll(t, 0, "TUX", profile, sizeof profile);
  CHECK(net_relay_refused() && !strcmp(net_relay_refused(), "VERSION 1.7 ROOM, YOU HAVE 2.4.1"));
  CHECK(net_relay_refused_reason() == 2);
  n = next_of(fd, 1, b, sizeof b, &from);
  CHECK(n > 0 && b[8] == 250); /* (an older client said 0 here: auto-matched by an older relay) */
  /* the JOIN is not said again after the refusal */
  t += 1.5;
  net_poll(t, 0, "TUX", profile, sizeof profile);
  CHECK(next_of(fd, 13, b, sizeof b, &from) == 0);

  /* ROOMS through the client: the versions reach net_relay_rooms */
  const char *labels[2] = { "1.7", "2.4.1" };
  unsigned char rb[256] = { 'W', 'V', 1, 11 };
  int rn = 4 + rooms_body(rb + 4, 2, "XQV", labels);
  sendto(fd, rb, (size_t)rn, 0, (struct sockaddr *)&from, sizeof from);
  usleep(20000);
  t += 0.1;
  net_poll(t, 0, "TUX", profile, sizeof profile);
  net_room r[NET_ROOMS_MAX];
  CHECK(net_relay_rooms(r, NET_ROOMS_MAX) == 2 && !strcmp(r[0].version, "1.7") && !strcmp(r[1].version, "2.4.1"));

  /* a new ask clears the refusal */
  net_relay_auto();
  CHECK(net_relay_refused() == NULL && net_relay_refused_reason() == 0);
  net_close();
  close(fd);
}

int main(void) {
  test_hello();
  test_rooms_parse();
  test_fake_relay();
  return test_done("net_version_test");
}
