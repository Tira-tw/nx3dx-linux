/* The players' own ghosts and their exchange over the versus (v2.1, a port extra; the arcade
 * sent its cabinets' ghosts to its server):
 * - a ghost battle run to the goal is saved as the card's ghost for that area, the arcade's own
 *   .target.gz (its header, then the area's card and trail: CSampler::Save's), one file an
 *   area: save_dir/myghost/g-<id1>-<id2>-a<area>-<stamp>.target.gz, the newest run kept;
 * - in the versus's lobby the SWAP GHOSTS button (gx_swap) swaps them, one player's swap at a
 *   time: its owner lists its own ones to everyone, everyone lists theirs to it (GHOST datagrams
 *   through the host, net.h), and each fetches the other's it has not got or has older (the
 *   stamp, minutes since 2020): save_dir/ghosts/, which the ghost battle's lists read beside
 *   data/target. Two tapped at once: the lowest id's first, the other's queued after it. */
#ifndef GHOSTX_H
#define GHOSTX_H

#include <stdint.h>

typedef struct {
  int slot;                    /* the save (1..): its player's ids */
  unsigned short name[5];      /* the card's name, UCS-2 */
  int title;
  int area, level, crown;      /* clEffAreaType, 1..10, the area's crown held */
  int car, power, handling;    /* emCarModelID, the tune */
  int aero, mirror, wing, bonnet, wheel, neon, vinyl;
  int ramp, route;             /* the race's (the ghost raced's) */
  int odo, cool_wild, smooth_rough; /* (v2.3) the card's mileage (km) and play style: its aura */
} gx_run;

void gx_init(const char *save_dir);
/* the run's trail (CSampler::Save's file) saved as the card's ghost for its area; 0 ok */
int gx_save_own(const gx_run *r, const char *trail_path);
/* the exchange, each frame the versus's lobby is up (net open) */
void gx_poll(double now);
/* the lobby's SWAP GHOSTS button: this one's swap started (0), (v2.3) another's on already (2:
 * it swaps this one's too), -1 not connected. (v2.3) One press by anyone swaps both ways for
 * everyone: once a swap's START comes every player lists its own ghosts to all and fetches the
 * others' it lacks, no press of its own; two started at once are one swap (the lowest id's) */
int gx_swap(void);
/* the swap now: 0 none, 1 this one's, 2 another's (owner; (v2.3) or its tail: this one's
 * fetches in it still going), 3 queued (a v2.2 leftover: not any more); got: received in it so
 * far; the last one's received and how long ago */
int gx_swap_state(uint32_t *owner, int *got, int *done_got, double *done_ago);
/* a ghost came since the last call (the lists to be read again) */
int gx_take_new(void);
/* the ghosts received, and how many this session */
const char *gx_dir(void);
/* (v2.3) the saves' own runs' folder (save_dir/myghost), and the save (1..) a ghost file there
 * is the run of, 0 none: another card's own ghost can be raced */
const char *gx_own_dir(void);
int gx_slot_of(const char *name);
int gx_received(void);

#endif
