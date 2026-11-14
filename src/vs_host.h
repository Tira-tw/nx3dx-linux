/* The versus host in the app (Bluetooth and LAN versus): tools/vs_server.py's lobby and the
 * race packets' relay on UDP NET_PORT (every interface) and on GameKit's peers. Everything
 * runs on the caller's thread: vs_host_poll reads what has arrived, never blocking. */
#ifndef VS_HOST_H
#define VS_HOST_H

#include <stdint.h>

/* open the host (gk: also GameKit's peers, gk_host already advertising); owner: the host's
 * own player's id, the lobby's host whenever it is in; 0 on success */
int vs_host_open(int gk, uint32_t owner);
void vs_host_close(void);
int vs_host_is_open(void);
/* 乱入拒否 (v2.2): the LAN's searches not answered, no new player let in (those in stay) */
void vs_host_refuse(int on);
/* (v2.3) the room's flag as it is now (a player's ROOM may have set it), the players in, and
 * the host's KICK (the player told, dropped, its id banned for the session) */
int vs_host_refused(void);
int vs_host_players(void);
void vs_host_kick(uint32_t id);
/* serve what has arrived; `name`: the game's name in the LAN's answers */
void vs_host_poll(double now, const char *name);

#endif
