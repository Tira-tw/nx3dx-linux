/* (v2.3) The time attack's ghost (a port extra; the arcade's own TA ghost, clSeqTaGhostSelectThread
 * and menu_p_ta_ghost_*.png, raced the server's ranking runs): each course's best run kept, its
 * car on the road again as a translucent ghost (no collision: the race never knows of it) in the
 * next time attack on that course.
 *
 * NOT a header of its own unit: app.c includes it once, after its G struct (`} G;`), for the
 * state and the prototypes the hooks before taghost.inc's include need; taghost.inc (the
 * functions) goes in before draw_race_cars. See taghost.HOOKS.md. */
#ifndef TAGHOST_H
#define TAGHOST_H

#define TAGH_STEP 2              /* a sample every 2 arcade frames (30 Hz) */
#define TAGH_MAX (60 * 30 * 20)  /* 20 min of samples at most */
#define TAGH_ALPHA 0.45f         /* the ghost's opacity */
#define TAGH_VERSION 1

/* one sample of the run: the body's rotation columns and translation (car_state.body's
 * m[0..2], m[4..6], m[8..10], m[12..14]), each tyre's steer (about the body's y), hub height
 * (its posture's translation y) and spin (tyre_rot), and the brake lamps */
typedef struct {
  float body[12];
  float steer[4], hub[4], rot[4];
  unsigned char brake, pad[3];
} tagh_sample;

/* the file's head (little-endian, as every target is), then n samples; gzipped */
typedef struct {
  char magic[4];               /* "TAGH" */
  int version;                 /* TAGH_VERSION */
  int course, course_id;       /* the course card (menu_state.course, 0..CAR_COURSES-1), car_course_id's */
  int ms;                      /* the run's time (as records_goal: race->total * 1000) */
  int maker, car, color;       /* the car select's (cardb.h) */
  int aero, mirror, wing, bonnet, wheel, neon, vinyl; /* its dress-up then (vs_dress_t's order) */
  int power, handling, manual;
  int step;                    /* TAGH_STEP the run was sampled at */
  int n;                       /* samples */
  long long when;              /* time() of the run */
} tagh_head;

static struct {
  /* the ghost raced now (play), loaded as the race started */
  int play;
  tagh_head gh;
  tagh_sample *gs;
  int pack, paint;             /* its car's model (-1 none) */
  /* the run being recorded (rec) */
  int rec;
  int course;
  tagh_head rh;
  tagh_sample *rs;
  int rn, rcap;
  int frame;                   /* arcade frames since car_start */
  int best_ms;                 /* the course's ghost's time when the race started, 0 none */
} TAGH = { .pack = -1 };

static void tagh_race_start(int ta, int course, int power, int handling);
static void tagh_step(const car_state *c);
static void tagh_race_over(int goal, const car_race *race, int traffic);
static void tagh_draw(const car_state *c, const mat4 *vp, const float *eye_pos, render_params *rp);

#endif
