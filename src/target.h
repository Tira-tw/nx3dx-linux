/* The ghost versus battle's ghosts (分身, the arcade's clTargetData): data/target's .target.gz files,
 * the arcade's own files (its data/target/jp, and the ones a cabinet saved under
 * /tmp/data/target). Each is a gzipped Lua text: the player's id and name, then per area
 * (clEffAreaType 0..7) the card it raced that area with (car, tune, dress-up), its ramp and
 * route, crown, and the trail of that run (yt::CSampler's file, between "--{{" and "--}}"). */
#ifndef TARGET_H
#define TARGET_H

#include <stddef.h>

#define TARGET_AREAS 8
#define TARGET_NAME 5   /* name0_ .. name4_: the card's name, UCS-2 */
#define TARGET_MAX 512  /* ghost files the list keeps */

typedef struct {
  int car;              /* emCarModelID (the colour select's model: titledb.h TITLEDB_MODEL) */
  int custom_color;
  int power, handling;  /* tune_power_, tune_handling_ (0..CAR_TUNE_MAX) */
  int aero, aero_mirror, wing, bonnet, wheel, neon, vinyl, dress;
  int ramp, route;      /* clEffRampType (the join ramps' table), the route on it */
  int crown;
  int cls, title;
  int year, month, day; /* the run's date */
  int hour, minute;
  int level;            /* the file's "-- area N level L" (getTargetLevel: 1..10), 0 none */
  /* (v2.3) the card's mileage (km) and its play style, cool_wild_ / smooth_rough_ (3 the middle):
   * the aura's (nsMenuCheck::checkAura) */
  int odo, cool_wild, smooth_rough;
  size_t trail_at;      /* the trail in the file's text, trail_size bytes; 0 size: none */
  unsigned trail_size;
} target_area;

typedef struct {
  int id1, id2;
  unsigned short name[TARGET_NAME];
  int original;         /* original_: a cabinet player's own ghost */
  target_area area[TARGET_AREAS];
  unsigned char *text;  /* the file, unzipped */
  size_t len;
} target_file;

/* reads and parses one ghost file; 0 ok */
int target_load(const char *path, target_file *t);
/* only its head: the name and the areas' cards and trail sizes (trail_size set, no trail to
 * write); fast, for the lists */
int target_load_header(const char *path, target_file *t);
void target_free(target_file *t);
/* writes area's trail to path as the arcade's clTargetData::getTrail does (/tmp/car0.trail);
 * 0 ok, -1 none or not written */
int target_write_trail(const target_file *t, int area, const char *path);
/* the *.target.gz files in dir, sorted by name; their count */
int target_scan(const char *dir, char (*names)[64], int max);

#endif
