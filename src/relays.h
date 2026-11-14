/* (v2.4) The online versus relays: the list from GET https://nova-net.work/api/v1/relays (online
 * ones only) fetched on a background thread, each relay's round trip timed over UDP (PING kind 16
 * -> PONG kind 17). No answer from the website: the three built-in relays (Singapore / US East /
 * EU Central), kept XOR'd in the binary and decoded at run time.
 * Ported from the friend's hand-out relays.c (wmmt-server-protocol). */
#ifndef WMMT_RELAYS_H
#define WMMT_RELAYS_H

#define NET_RELAYS_MAX 8
#define NET_RELAYS_URL "https://nova-net.work/api/v1/relays"

/* the platform's HTTPS GET (main_windows.c: WinHTTP; Android / iOS: helper 03's, a weak stub
 * returning -1 in relays.c until then). Blocking, called from the background thread only: the
 * body into out (NUL-terminated, at most max-1 bytes), its length; -1 on any failure / non-200. */
int platform_https_get(const char *url, char *out, int max);

/* start a fetch + ping round on a background thread (nothing if one is running) */
void net_relays_refresh(void);

/* 0 nothing yet, 1 busy, 2 the website's list, 3 the built-in fallback */
int net_relays_state(void);

int net_relays_count(void);
const char *net_relay_name(int i);
/* the dotted IPv4 address to give net_open() (resolved from the host name when needed) */
const char *net_relay_host(int i);
/* the host name ("apse1.relay.nova-net.work"), the address if the website gave none */
const char *net_relay_hostname(int i);
int net_relay_port(int i);
/* best round trip of the last round in ms, -1 none answered (yet). (Named net_relays_rtt_ms, not
 * net_relay_rtt_ms: net.h's net_relay_rtt_ms(void) is the connected relay's, helper 01) */
int net_relays_rtt_ms(int i);
/* the website's "online" (only online ones are listed); a fallback relay: 1 once it answered a PING */
int net_relay_online(int i);

/* the website's min_build for this platform (android / ios / other), 0 none */
int net_relays_min_build(void);

#endif
