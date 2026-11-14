/* The driving style the card keeps (card +0x184 cool / wild, +0x185 smooth / rough, 0..6, 3 the
 * middle), as clRaceThread::finiJoin updates it after a versus (not in event mode). See
 * playstyle.HOOKS.md. Include it in car.c (it needs rc_runtime.h's M8/M32/MF and MACHINE_PTR).
 *
 * The arcade's sources:
 *   yt::CWallLogger (8 bytes, CSeq::pWallLogger 0x98028d0): +0 float sum, +4 int count
 *     Drive(SHitPushBack const &) 0x89b3930, each CSeq::Exec with the player's CMachine +0xd4
 *     GetPlayStyle 0x89b3984 -> SPlayResult +0x18 -> clWorkSpace impl +0x258 -> getSmoothRough
 *   yt::CHitLogger (0x14 bytes, CSeq::pHitLogger 0x98028d4): +0 frame, +4 last hit frame,
 *     +8 this hit's sum, +0xc last hit's kind (0x7fff none), +0x10 total
 *     Drive(SHitPushBack const &, CVector3 const &, CVector3 const &, float) 0x89b3a20, each
 *     CSeq::Exec with CMachine +0x90, +0xab0, a copy of +0xa0c, +0xb04 (the float unused)
 *     GetPlayStyle 0x89b3958 -> SPlayResult +0x1c -> clWorkSpace impl +0x25c -> getCoolWild
 * In phys_rc.c all four are stubs (recomp.py NOT_LOGIC has "yt::CHitLogger", "yt::CWallLogger"),
 * so SPlayResult +0x18 / +0x1c are always 0 there. This file is the C version, fed after each
 * CSeq::Exec from the CMachine's memory. */

#include <math.h>

typedef struct {
  float wall_sum; int wall_count;          /* CWallLogger */
  int frame, last_frame; float hit_acc;    /* CHitLogger +0, +4, +8 */
  int last_kind; float hit_total;          /* CHitLogger +0xc, +0x10 */
} playstyle_t;
static playstyle_t PS;

/* CSeq::Init (0x898cd1e..0x898cd5a): both loggers new and zeroed, the last kind 0x7fff */
static void playstyle_reset(void) {
  memset(&PS, 0, sizeof PS);
  PS.last_kind = 0x7fff;
}

/* CWallLogger::Drive: a frame touching a wall (+0 set, +1 clear) adds the push-back depth */
static void ps_wall_drive(uint32_t pb) {
  if (M8(pb) && !M8(pb + 1)) {
    PS.wall_sum += MF(pb + 0x3c);
    PS.wall_count++;
  }
}

/* CHitLogger::Drive: pb the car-to-car SHitPushBack (+0 hit, +4 its kind, +0x30 the push-back
 * vector, +0x3c its length), vel the CMachine's velocity, head the vector at CMachine +0xa0c */
static void ps_hit_drive(uint32_t pb, const float vel[3], const float head[3]) {
  int f = ++PS.frame;
  if (M8(pb)) {
    int kind = (int32_t)M32(pb + 4);
    if (kind <= 3 && kind <= PS.last_kind) {
      float sp = sqrtf(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);
      if (sp > 10000.0f) {
        float inv = 1.0f / sp;
        float px = MF(pb + 0x30), py = MF(pb + 0x34), pz = MF(pb + 0x38), len = MF(pb + 0x3c);
        float d = inv * vel[0] * px + inv * vel[1] * py + inv * vel[2] * pz;
        float into = 0.0f < d ? 0.0f : -d;     /* the push-back against the motion */
        float nx = 0, ny = 0, nz = 0;
        if (len > 0.01f) { float k = 1.0f / len; nx = px * k; ny = py * k; nz = pz * k; }
        float w = fabsf(nx * head[0] + ny * head[1] + nz * head[2]) * 0.8f + 0.2f;
        PS.last_frame = f;
        PS.last_kind = kind;
        PS.hit_acc += into * w;
      }
    }
  }
  /* 10 frames after a hit's last contact it is over: its sum, at most 2500, into the total */
  if (f - PS.last_frame == 10) {
    float x = PS.hit_acc;
    PS.hit_acc = 0.0f;
    PS.last_kind = 0x7fff;
    PS.hit_total += x <= 2500.0f ? (x > 0.0f ? x : 0.0f) : 2500.0f;
  }
}

/* after each call(f_8983f64, ...) (CSeq::Exec), the player's car only */
static void playstyle_frame(void) {
  uint32_t m = M32(MACHINE_PTR);
  if (!m) return;
  ps_wall_drive(m + 0xd4u);
  float vel[3] = { MF(m + 0xab0u), MF(m + 0xab4u), MF(m + 0xab8u) };
  float head[3] = { MF(m + 0xa0cu), MF(m + 0xa10u), MF(m + 0xa14u) };
  ps_hit_drive(m + 0x90u, vel, head);
}

/* CWallLogger::GetPlayStyle: 1 rough, 0, -1 smooth */
static int playstyle_wall(void) {
  if (PS.wall_sum > 300000.0f || PS.wall_count > 30) return 1;
  if (PS.wall_sum >= 20000.0f || PS.wall_count > 6) return 0;
  return -1;
}
/* CHitLogger::GetPlayStyle: 1 wild, 0, -1 cool */
static int playstyle_hit(void) {
  if (!(PS.hit_total >= 3500.0f)) return -1;
  return PS.hit_total >= 7500.0f;
}

/* clWorkSpace::getCoolWild / getSmoothRough: (1 - x) * 3 as a byte, kept to 0..6 */
static int ps_race_value(int x) {
  signed char r = (signed char)(int)(((float)(-x) + 1.0f) * 3.0f);
  if (r <= 0) r = 0;
  if (r >= 7) r = 6;
  return r;
}
/* clRaceThread::finiJoin: the card's value moved a third of the way toward the race's */
static unsigned char ps_card_mix(int card, int race) {
  int v = (int)(((card / 3.0f - 1.0f) + (race / 3.0f - 1.0f) / 3.0f + 1.0f) * 3.0f);
  return (unsigned char)(v < 0 ? 0 : v > 6 ? 6 : v);
}
/* the card after a versus (not in event mode): cw card +0x184, sr card +0x185 */
static void playstyle_card_update(unsigned char *cw, unsigned char *sr) {
  *cw = ps_card_mix(*cw, ps_race_value(playstyle_hit()));
  *sr = ps_card_mix(*sr, ps_race_value(playstyle_wall()));
}
