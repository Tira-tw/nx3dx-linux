#include "sound.h"
#include "sounddb.h"

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---- engine model, from nsAudio::engine_calc and the tables it reads ----
 * Each layer is a looped sample with a band (r0..r1 fade in, r2..r3 fade out)
 * and the rpm at which it plays at its recorded pitch. Gain is (w)^0.2778 of
 * the band weight w; pitch is 2^((rpm - base) / 3205). Each car type has its
 * sound set (src/sounddb.h), e.g. the tuned rotaries' rx714-717 engine and
 * rn_m11t2-14t2 muffler. */
typedef sounddb_band band;

static float band_gain(const band *b, float rpm) {
  float w;
  if (rpm < b->r0 || rpm > b->r3) return 0;
  if (rpm < b->r1) w = (rpm - b->r0) / (float)(b->r1 - b->r0);
  else if (rpm <= b->r2) w = 1;
  else w = (rpm - b->r3) / (float)(b->r2 - b->r3);
  return powf(w, 0.2778f);
}

static float band_pitch(const band *b, float rpm) { return exp2f((rpm - b->base) * 0.000312012f); }

/* ---- samples and voices ---- */

typedef struct {
  short *data;
  int n, rate;
} sample;

enum {
  S_ENG0, S_ENG1, S_ENG2, S_ENG3, S_MUF0, S_MUF1, S_MUF2, S_MUF3,
  /* (v2.4 fix) the brake's loop (clAudio::loadTunedLoop loop 2: brake1, brake2 by the handling)
   * and the rotary's rev (loop 5, rx7_rev1) with the other loops */
  S_SLIP, S_WALL, S_TURBIN, S_AIR, S_BRAKE, S_REV, S_CRASH1, S_CRASH4, S_VALVE, S_CURSOR, S_SELECT, S_CANCEL, S_START,
  S_TIME_COUNT, S_TIME_COUNT_LAST, S_TIME_EXTEND, S_FINISH, S_TURN, S_TURN2, S_TURN3, S_START321, S_START_GO, S_FINISH2, S_CUTIN2, S_TUNING_BAR,
  S_RESULT2, S_RESULT_PASS, S_APPROACH2, S_APPROACH3, S_TURN3_CUTIN,
  /* the story movies' shows' others (src/storydemo.h: snd_effect) */
  S_VALB2, S_VALB3, S_GYAP, S_DOOR_OPEN, S_DOOR_CLOSE, S_ENGINE_START, S_PHONE2, S_RYBAL_INTRO,
  /* the dress-up part worn's (menu.c tg_wear_step: SE 0x41, 0x1e), the class-up's and the
   * story tuning work's levelup (SE 0x34); the wheel worn's jack (0x47, 0x46) and wrench
   * (0x4a, 0x4b), the level 63 part's COMPLETE (0x48); the title select's titles coming in (0x30);
   * the versus stars' star landing (0x2c) and big star (0x38) */
  S_PARTS_EQUIP, S_TUNING, S_LEVELUP, S_JACK_UP, S_JACK_DOWN, S_TUNING1, S_TUNING2, S_PARTS_COMP, S_RANK, S_WIN, S_WIN_BIGSTAR,
  /* the turbo's other two turbines (S_TURBIN by the power tune) and the backfire's two */
  S_TURBIN2, S_TURBIN3, S_BACKFIRE1, S_BACKFIRE2,
  /* the wrong way's (clReverseConsole::update, SE 0x10: system_se/reverce.wav) */
  S_REVERCE,
  /* (v2.2) 乱入's 挑戦者参上！ (system_se/challeng.wav; the port's extra folder's if not in sound) */
  S_CHALLENG,
  /* (v2.3) the fireworks' bursts (ONESHOT_HANABI1/2: race_se/hanabi1, 2.wav) */
  S_HANABI1, S_HANABI2,
  /* (v2.3) the mileage screen's counting (SE 0x35) */
  S_ODO_COUNT,
  /* (v2.4 fix) the brake's other loop and the hits' other one-shots (Mc_Req: crash2/3, crash5, crash6) */
  S_BRAKE2, S_CRASH2, S_CRASH3, S_CRASH5, S_CRASH6,
  /* (v2.4 fix) the versus junction prompt's (clJunctionSelectConsole, SE 0x3a: system_se/turning5.wav) */
  S_TURNING5,
  S_COUNT
};
/* every car type's engine and muffler loops, each wave once (S_ENG0..S_MUF3 point into these;
 * v2.4 fix: SOUNDDB_WAVES) */
static sample ENGINE_WAVES[SOUNDDB_NWAVES];
static const sounddb_set *engine_set = &SOUNDDB_SETS[0];
static const char *EFFECT_FILES[S_COUNT - S_SLIP] = { "slip", "wall", "turbin1", "air1", "brake1", "rx7_rev1",
                                                      "crash1", "crash4", "valb1",
                                                      "cursor", "select", "cancel", "gamestart",
                                                      "ti_count", "ti_count2", "turning6", "finish1",
                                                      "turning", "turning2", "turning3", "start321", "start_go",
                                                      "finish2", "cutin2", "tuning_bar", "result2",
                                                      "result_pass", "approach2", "approach3", "turning3_cutin",
                                                      "valb2", "valb3", "gyap", "door_open", "door_close",
                                                      "engine_start", "phone2", "rybal_intro", "parts_equip",
                                                      "tuning", "levelup", "jack_up", "jack_down", "tuning1",
                                                      "tuning2", "parts_comp", "rank", "win", "win_bigstar",
                                                      "turbin2", "turbin3", "backfire1", "backfire2", "reverce",
                                                      "challeng", "hanabi1", "hanabi2", "odo_count",
                                                      "brake2", "crash2", "crash3", "crash5", "crash6", "turning5" };
static sample SAMPLES[S_COUNT];

/* The desktop Linux port can have a partially prepared data/sound folder.
 * Never use a missing crash WAV silently when another authentic crash WAV
 * is available. Android/iOS/Windows keep their original sound routing. */
#ifdef WMMT_LINUX_NATIVE
static int linux_crash_sample(int id) {
  if (SAMPLES[id].n > 1) return id;
  const int alternatives[] = { S_CRASH3, S_CRASH2, S_CRASH1, S_CRASH4, S_CRASH5, S_CRASH6 };
  for (size_t i = 0; i < sizeof alternatives / sizeof alternatives[0]; ++i)
    if (SAMPLES[alternatives[i]].n > 1) return alternatives[i];
  return id;
}
#endif

/* Loop voices map 1:1 to the first fourteen samples; one-shots get their own pool (v2.4 fix:
 * 8, the hits', the valve's and the backfire's together with the consoles') */
#define N_LOOP (S_REV + 1)
#define N_SHOT 8

typedef struct {
  const sample *s;
  double pos;
  volatile float vol, pitch; /* targets, written by the game thread */
  float cur_vol;             /* ramped on the audio thread */
  volatile int seq;          /* one-shots: bumped to (re)start */
  int played_seq;
} voice;

static voice LOOPS[N_LOOP], SHOTS[N_SHOT];
static voice BAR; /* the story result's points gauge counting (SE 0x43, looped till stopped) */
static int shot_next;
static float CAR_GAIN = 0.5f; /* snd_car_volume */

/* RIFF reader: integer PCM, mono or stereo, 8-bit or 16-bit; stereo is downmixed. */
static int load_wav(sample *s, const char *path) {
  FILE *f = fopen(path, "rb");
  unsigned char hdr[12], ck[8], fmt[40] = { 0 };
  unsigned char *raw = NULL;
  long data_pos = -1;
  uint32_t data_size = 0;
  unsigned fmt_size = 0;
  unsigned format = 0, chans = 0, rate = 0, bits = 0, align = 0;
  const char *why = "invalid or incomplete RIFF/WAVE file";
  if (!f) {
    fprintf(stderr, "sound: cannot open %s\n", path);
    return -1;
  }
  memset(s, 0, sizeof *s);
  if (fread(hdr, 1, sizeof hdr, f) != sizeof hdr ||
      memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) goto fail;

  while (fread(ck, 1, sizeof ck, f) == sizeof ck) {
    uint32_t size = (uint32_t)ck[4] | (uint32_t)ck[5] << 8 |
                    (uint32_t)ck[6] << 16 | (uint32_t)ck[7] << 24;
    long pos = ftell(f);
    if (pos < 0) goto fail;
    if (!memcmp(ck, "fmt ", 4)) {
      unsigned take = size < sizeof fmt ? size : (unsigned)sizeof fmt;
      if (size < 16 || fread(fmt, 1, take, f) != take) goto fail;
      fmt_size = size;
      format = (unsigned)fmt[0] | (unsigned)fmt[1] << 8;
      chans = (unsigned)fmt[2] | (unsigned)fmt[3] << 8;
      rate = (unsigned)fmt[4] | (unsigned)fmt[5] << 8 |
             (unsigned)fmt[6] << 16 | (unsigned)fmt[7] << 24;
      align = (unsigned)fmt[12] | (unsigned)fmt[13] << 8;
      bits = (unsigned)fmt[14] | (unsigned)fmt[15] << 8;
      if (format == 0xfffeu && fmt_size >= 40) {
        /* WAVE_FORMAT_EXTENSIBLE: the first WORD of SubFormat is PCM (1). */
        format = (unsigned)fmt[24] | (unsigned)fmt[25] << 8;
      }
      if (fseek(f, (long)(size - take + (size & 1u)), SEEK_CUR) != 0) goto fail;
    } else if (!memcmp(ck, "data", 4)) {
      if (data_pos < 0) {
        data_pos = pos;
        data_size = size;
      }
      if (fseek(f, (long)(size + (size & 1u)), SEEK_CUR) != 0) goto fail;
    } else {
      if (fseek(f, (long)(size + (size & 1u)), SEEK_CUR) != 0) goto fail;
    }
  }

  if (!fmt_size || data_pos < 0 || !data_size) goto fail;
  if (format != 1u) { why = "only integer PCM WAV is supported"; goto fail; }
  if (chans != 1u && chans != 2u) { why = "WAV must be mono or stereo"; goto fail; }
  if (bits != 8u && bits != 16u) { why = "WAV must be 8-bit or 16-bit PCM"; goto fail; }
  if (rate < 8000u || rate > 192000u) { why = "invalid WAV sample rate"; goto fail; }
  if (align != chans * (bits / 8u) || !align || data_size % align) {
    why = "invalid WAV block alignment or data size";
    goto fail;
  }

  size_t frames = data_size / align;
  if (!frames || frames > 0x7fffffffu) { why = "WAV sample is too large"; goto fail; }
  raw = malloc(data_size);
  s->data = malloc(sizeof(short) * frames);
  if (!raw || !s->data) { why = "out of memory loading WAV"; goto fail; }
  if (fseek(f, data_pos, SEEK_SET) != 0 || fread(raw, 1, data_size, f) != data_size) {
    why = "truncated WAV data";
    goto fail;
  }

  for (size_t i = 0; i < frames; ++i) {
    size_t o = i * align;
    int left, right;
    if (bits == 8u) {
      left = ((int)raw[o] - 128) << 8;
      right = chans == 2u ? ((int)raw[o + 1] - 128) << 8 : left;
    } else {
      left = (int)(int16_t)((unsigned)raw[o] | (unsigned)raw[o + 1] << 8);
      right = chans == 2u
          ? (int)(int16_t)((unsigned)raw[o + 2] | (unsigned)raw[o + 3] << 8)
          : left;
    }
    s->data[i] = (short)((left + right) / 2);
  }
  s->n = (int)frames;
  s->rate = (int)rate;
  free(raw);
  fclose(f);
  return 0;

fail:
  fprintf(stderr, "sound: rejected %s: %s\n", path, why);
  free(raw);
  free(s->data);
  memset(s, 0, sizeof *s);
  fclose(f);
  return -1;
}

int snd_init(const char *data_dir) {
  char path[1024];
  int missing = 0;
  for (int i = S_SLIP; i < S_COUNT; ++i) {
    snprintf(path, sizeof path, "%s/sound/%s.wav", data_dir, EFFECT_FILES[i - S_SLIP]);
    if (load_wav(&SAMPLES[i], path) != 0) missing++;
  }
  for (int k = 0; k < SOUNDDB_NWAVES; ++k) {
    snprintf(path, sizeof path, "%s/sound/engine/%s.wav", data_dir, SOUNDDB_WAVES[k]);
    if (load_wav(&ENGINE_WAVES[k], path) != 0) missing++;
  }
  for (int i = 0; i < N_LOOP; ++i) {
    LOOPS[i].s = &SAMPLES[i];
    LOOPS[i].pitch = 1;
  }
  snd_set_car(5, 24); /* the FD3S, tuned */
  if (missing) fprintf(stderr, "sound: %d WAV file(s) missing or unsupported under %s\n", missing, data_dir);
  else fprintf(stderr, "sound: all effect and engine WAV files loaded\n");
#ifdef WMMT_LINUX_NATIVE
  fprintf(stderr, "linux sound: collision WAVs wall=%s crash1=%s crash2=%s crash3=%s crash4=%s crash5=%s crash6=%s\n",
          SAMPLES[S_WALL].n > 1 ? "OK" : "MISSING",
          SAMPLES[S_CRASH1].n > 1 ? "OK" : "MISSING",
          SAMPLES[S_CRASH2].n > 1 ? "OK" : "MISSING",
          SAMPLES[S_CRASH3].n > 1 ? "OK" : "MISSING",
          SAMPLES[S_CRASH4].n > 1 ? "OK" : "MISSING",
          SAMPLES[S_CRASH5].n > 1 ? "OK" : "MISSING",
          SAMPLES[S_CRASH6].n > 1 ? "OK" : "MISSING");
#endif
  return missing ? -1 : 0;
}

/* nsAudio's per car: the blow-off valve (clAudio::playLoop 4: the one-shot valb1..3 by the car's
 * ECarType, table 0x931be80) and the turbine (clAudio::loadTunedLoop: loop 1 turbin1..3 by the
 * power tune, at least 4 and at most 10: rows of 0x931bf40) */
static const unsigned char VALB_BY_TYPE[48] = {
  0, 1, 1, 0, 0, 0, 2, 1, 0, 0, 0, 2, 0, 0, 0, 0, 0, 1, 0, 0, 1, 2, 2, 0,
  2, 0, 1, 0, 0, 1, 2, 0, 0, 2, 2, 0, 0, 2, 0, 0, 1, 2, 2, 0, 2, 2, 0, 0,
};
static int valb_sample = S_VALVE;

/* (v2.4 fix) CSE::Mc_Load's (car_se load_*): clAudio::loadTunedEngine's set by the ECarType at
 * the power tune's stage, loadTunedLoop's turbine by the power and brake by the handling, the
 * tunes capped at 10 (sounddb.h); what is loaded, to load it again only when it changes */
static int loaded_type = -1, loaded_power, loaded_handling;

static void load_car(unsigned type, int power, int handling) {
  int v = type < 48 ? VALB_BY_TYPE[type] : 0;
  valb_sample = v == 0 ? S_VALVE : v == 1 ? S_VALB2 : S_VALB3;
  int p = power < 0 ? 0 : power > 10 ? 10 : power;
  int h = handling < 0 ? 0 : handling > 10 ? 10 : handling;
  int t = SOUNDDB_TURBIN[p];
  int k = type < SOUNDDB_NTYPES ? SOUNDDB_BY_TYPE[SOUNDDB_STAGE[p]][type] : 0;
  /* the audio thread reads each voice's sample pointer once per mix */
  LOOPS[S_TURBIN].s = &SAMPLES[t <= 1 ? S_TURBIN : t == 2 ? S_TURBIN2 : S_TURBIN3];
  LOOPS[S_BRAKE].s = &SAMPLES[SOUNDDB_BRAKE[h] == 7 ? S_BRAKE2 : S_BRAKE];
  for (int i = 0; i < 4; ++i) {
    LOOPS[S_ENG0 + i].s = &ENGINE_WAVES[SOUNDDB_SETS[k].engine[i]];
    LOOPS[S_MUF0 + i].s = &ENGINE_WAVES[SOUNDDB_SETS[k].muffler[i]];
  }
  engine_set = &SOUNDDB_SETS[k];
  loaded_type = (int)type, loaded_power = power, loaded_handling = handling;
  fprintf(stderr, "sound: car type %u power %d handling %d: engine %s.. muffler %s.. (stage %d) turbin%d %s %s\n",
          type, power, handling, SOUNDDB_WAVES[engine_set->engine[0]], SOUNDDB_WAVES[engine_set->muffler[0]],
          SOUNDDB_STAGE[p], t, SOUNDDB_BRAKE[h] == 7 ? "brake2" : "brake1", EFFECT_FILES[valb_sample - S_SLIP]);
}

/* (until the race's car_se says what CSeq::Init loaded: the tune at least 4, as without a card) */
void snd_set_car(unsigned type, int power) { load_car(type, power > 3 ? power : 4, 4); }

static int shot_playing(int sample_id) {
  for (int i = 0; i < N_SHOT; ++i)
    if (SHOTS[i].s == &SAMPLES[sample_id]) return 1;
  return 0;
}

static void play_shot(int sample_id, float vol, float pitch) {
  voice *v = &SHOTS[shot_next];
  shot_next = (shot_next + 1) % N_SHOT;
  v->s = &SAMPLES[sample_id];
  v->vol = vol;
  v->pitch = pitch;
  v->seq++;
}

/* nsAudio's one-shot table gives the countdown's two volume 1.5, approach2 and 3 and the
 * 10-in-a-row's beaten one's cutin2 (SE 0x4c) 2.0, the rest 1.0 */
void snd_menu(int which) {
  int loud = which == SND_APPROACH2 || which == SND_APPROACH3 || which == SND_BEATEN;
  play_shot(which == SND_BEATEN ? S_CUTIN2 : S_CURSOR + which,
            which == SND_START321 || which == SND_START_GO ? 1.2f : loud ? 1.6f : 0.8f, 1.0f);
}

int snd_load_file(const char *wav, const char *path) {
  for (int i = S_SLIP; i < S_COUNT; ++i) {
    const char *a = EFFECT_FILES[i - S_SLIP], *b = wav;
    while (*a && *a == *b) a++, b++;
    if (*a == *b) return SAMPLES[i].n ? 0 : load_wav(&SAMPLES[i], path);
  }
  return -1;
}

void snd_effect(const char *wav, float vol) {
  for (int i = S_SLIP; i < S_COUNT; ++i) {
    /* (the patched iOS SDK has no plain strcmp for armv7) */
    const char *a = EFFECT_FILES[i - S_SLIP], *b = wav;
    while (*a && *a == *b) a++, b++;
    if (*a == *b) {
      play_shot(i, 0.8f * vol, 1.0f);
      return;
    }
  }
}

void snd_menu_loop(int which, int on) {
  if (which != SND_TUNING_BAR) return;
  if (on) {
    BAR.s = &SAMPLES[S_TUNING_BAR];
    BAR.pitch = 1.0f;
    BAR.seq++;
  }
  BAR.vol = on ? 0.8f : 0.0f;
}

/* ---- game-side control ---- */

static struct {
  float rpm;
  float accel, accel_target, accel_step; /* clAudioControl EngAccelOn/Off/ShiftUp/Down, 0..5000 */
  int throttle_on;
  float tick;
  int gyap_on, backfire_k;
  float valb_left; /* the valve's sound playing (Mc_Req: SNDIsPlayingValb) */
  float eng_vol;   /* (v2.4 fix) ms_Engine: Mc_Req's last in 0..1 */
  unsigned n_cnt;  /* (v2.4 fix) CSE::nCnt, counting Mc_Req's frames */
} E = { .rpm = 1000, .accel = 1000, .accel_target = 1000, .accel_step = 1000, .eng_vol = 0.73f };

/* nsAudio's pitch: a one-shot's frequency is its playback rate over the wave's rate (OneshotSndReq
 * 0x853c0ac: freq / wave desc +4, 22050 for every race_se wave); a loop's is an offset from that rate
 * (LoopSndCtrl 0x853bed8: (22050 + freq, clamped 0..179999) / 22050) */
static float shot_pitch(float hz) { return hz > 0 ? hz / 22050.0f : 1.0f; }
static float loop_pitch(float off) {
  float f = 22050.0f + off;
  return (f < 0 ? 0 : f > 179999.0f ? 179999.0f : f) / 22050.0f;
}

/* (v2.4 fix) an OpenAL source's gain stops at 1 (AL_MAX_GAIN): nsAudio's volume times the
 * wave's own (the loop and one-shot tables' +0xc: slip 1.55, brake 1.6, valb 1.5, gyap 1.8,
 * backfire 2.5, crash 3) */
static float al_gain(float v) { return v < 0 ? 0.0f : v > 1 ? 1.0f : v; }

void snd_update(car_state *car, const car_input *in, float dt) {
  car_se *se = &car->se;
  (void)in;
  /* (v2.4 fix) the sounds CSeq::Init's Mc_Load loaded for this car (its type and tunes) */
  if (se->load_type >= 0 && (se->load_type != loaded_type || se->load_power != loaded_power ||
                             se->load_handling != loaded_handling))
    load_car((unsigned)se->load_type, se->load_power, se->load_handling);

  /* (v2.4 fix) the muffler's level as Mc_Req drives it: the arcade's own throttle and gear cues
   * (car_se accel, shift) into clAudioControl EngAccelOn/Off, EngShiftUp/Down (0..5000) */
  if (se->accel > 0 && !E.throttle_on) { E.throttle_on = 1; E.accel_target = 5000; E.accel_step = 1000; }
  if (se->accel < 0 && E.throttle_on) { E.throttle_on = 0; E.accel_target = 1000; E.accel_step = 1000; }
  if (se->shift > 0) {
    E.accel = fmaxf(0, E.accel_target - 5000); E.accel_step = 400;
  } else if (se->shift < 0) {
    E.accel = fminf(5000, E.accel_target + 5000); E.accel_step = 8;
  }
  se->accel = se->shift = 0;
  car->shift_event = 0;
  /* muffler_calc runs per 60 Hz tick */
  for (E.tick += dt; E.tick >= 1.0f / 60.0f; E.tick -= 1.0f / 60.0f) {
    if (E.accel > E.accel_target) E.accel = fmaxf(E.accel_target, E.accel - E.accel_step);
    else if (E.accel < E.accel_target) E.accel = fminf(E.accel_target, E.accel + E.accel_step);
  }

  /* (v2.4 fix) the engine at the arcade's sound rpm (car_se rpm: the rev rate's curve, 300..8999,
   * the same span for every car; the physics' rpm passed the bands' 9000 at the top of the revs
   * and left the engine silent there), each layer at its band's gain times the engine's volume
   * (Mc_Req: +0x68 * 0.73, clAudioManage ms_Engine), the muffler's times the throttle's level too
   * (EngSndCtrl 0x853b4b8; LimitVolume: at most 1) */
  E.rpm = se->rpm > 8999.0f ? 8999.0f : se->rpm < 300.0f ? 300.0f : (float)(int)se->rpm;
  float ev = 0.73f * se->eng_vol;
  if (ev >= 0.0f && ev <= 1.0f) E.eng_vol = ev;
  float accel = E.accel / 5000.0f;
  for (int i = 0; i < 4; ++i) {
    LOOPS[S_ENG0 + i].vol = al_gain(band_gain(&engine_set->engine_bands[i], E.rpm) * E.eng_vol);
    LOOPS[S_ENG0 + i].pitch = band_pitch(&engine_set->engine_bands[i], E.rpm);
    LOOPS[S_MUF0 + i].vol = al_gain(band_gain(&engine_set->muffler_bands[i], E.rpm) * accel * E.eng_vol);
    LOOPS[S_MUF0 + i].pitch = band_pitch(&engine_set->muffler_bands[i], E.rpm);
  }

  /* the arcade's own requests (car_se, yt::CSE::Mc_Req 0x89a0678), all at its 0.73: the turbine
   * (a turbo's only) and the wind as loops; (v2.4 fix) the brake, the tyres and the wall as loops
   * too, the rotary's rev at 22050 + 22000 Hz, all from the arcade's own values (they were the
   * port's guesses: the tyres from the sideways speed, the wall from a loss of speed) */
  LOOPS[S_TURBIN].vol = se->turbo ? al_gain(0.73f * se->turbin_vol) : 0.0f;
  LOOPS[S_TURBIN].pitch = loop_pitch(se->turbin_hz);
  LOOPS[S_AIR].vol = al_gain(0.73f * se->air_vol);
  LOOPS[S_AIR].pitch = loop_pitch(se->air_hz);
  LOOPS[S_BRAKE].vol = al_gain(0.73f * se->brake_vol * 1.6f);
  LOOPS[S_BRAKE].pitch = loop_pitch(se->brake_hz);
  LOOPS[S_SLIP].vol = al_gain(0.73f * se->slip_vol * 1.55f);
  LOOPS[S_SLIP].pitch = loop_pitch(se->slip_hz);
#ifdef WMMT_LINUX_NATIVE
  /* Recompiled arcade cues aren't emitted for every glancing impact on
   * some native Linux builds. Only use the already-calculated collision
   * state when the original wall loop request is absent. */
  float wall_volume = se->wall_vol;
  if (wall_volume < 0.02f && car->wall_hit > 0.05f)
    wall_volume = fminf(0.65f, car->wall_hit * 1.8f);
  LOOPS[S_WALL].vol = al_gain(0.73f * wall_volume);
#else
  LOOPS[S_WALL].vol = al_gain(0.73f * se->wall_vol);
#endif
  LOOPS[S_WALL].pitch = loop_pitch(se->wall_hz);
  LOOPS[S_REV].vol = se->rev ? al_gain(0.73f * se->rev_vol) : 0.0f;
  LOOPS[S_REV].pitch = loop_pitch(22000.0f);

  /* the one-shots: the blow-off valve (a turbo's only: v2.4 fix) once it is not playing (the
   * valve's sample by the car: load_car; Allan 10-30: the R32's), the backfire (backfire2 or 1
   * by the frame: Mc_Req's 10 - (nCnt & 1)) and the gyap as they come; (v2.4 fix) the hits:
   * crash2 or crash3 (one in four crash2: nCnt & 3), crash6 for +0xd4 1, crash5 and crash4
   * each while flagged once the last has played, crash1 as it comes */
  E.valb_left -= dt;
  if (se->turbo && se->valb && E.valb_left <= 0) {
    play_shot(valb_sample, CAR_GAIN * 0.73f * 0.73f * se->valb_vol, 1.0f);
    E.valb_left = SAMPLES[valb_sample].rate > 0 ? (float)SAMPLES[valb_sample].n / (float)SAMPLES[valb_sample].rate : 0.5f;
  }
  if (se->backfire) {
    int id = (E.backfire_k++ & 1) ? S_BACKFIRE1 : S_BACKFIRE2;
    play_shot(id, CAR_GAIN * al_gain(0.73f * se->backfire_vol * 2.5f), shot_pitch(se->backfire_hz));
    se->backfire = 0; /* (a pulse: car.c keeps it till now) */
  }
  if (se->gyap && !E.gyap_on)
    play_shot(S_GYAP, CAR_GAIN * al_gain(0.73f * se->gyap_vol * 1.8f), shot_pitch(se->gyap_hz));
  E.gyap_on = se->gyap;
  E.n_cnt++;
  if (se->crash) {
    int id = se->crash6 ? S_CRASH6 : (E.n_cnt & 3) == 0 ? S_CRASH2 : S_CRASH3;
#ifdef WMMT_LINUX_NATIVE
    id = linux_crash_sample(id);
#endif
    if (!shot_playing(id)) play_shot(id, CAR_GAIN * al_gain(0.73f * se->crash_vol * 3.0f), shot_pitch(se->crash_hz));
  }
  if (se->crash5) {
#ifdef WMMT_LINUX_NATIVE
    int id = linux_crash_sample(S_CRASH5);
#else
    int id = S_CRASH5;
#endif
    if (!shot_playing(id))
      play_shot(id, CAR_GAIN * al_gain(0.73f * se->crash5_vol * 3.0f), shot_pitch(se->crash5_hz));
  }
  if (se->crash4) {
#ifdef WMMT_LINUX_NATIVE
    int id = linux_crash_sample(S_CRASH4);
#else
    int id = S_CRASH4;
#endif
    if (!shot_playing(id))
      play_shot(id, CAR_GAIN * al_gain(0.73f * se->crash4_vol * 3.0f), shot_pitch(se->crash4_hz));
  }
  if (se->crash1) {
#ifdef WMMT_LINUX_NATIVE
    play_shot(linux_crash_sample(S_CRASH1), CAR_GAIN * al_gain(0.73f * se->crash1_vol * 3.0f), shot_pitch(se->crash1_hz));
#else
    play_shot(S_CRASH1, CAR_GAIN * al_gain(0.73f * se->crash1_vol * 3.0f), shot_pitch(se->crash1_hz));
#endif
  }
#ifdef WMMT_LINUX_NATIVE
  /* The physics-based impact signal is already computed by car.c.
   * The stock mixer previously cleared it without making a sound.
   * Use it only when the recompiled arcade did NOT request a crash;
   * throttle repeated wall scrapes to keep one collision from chattering. */
  static float impact_cooldown;
  if (impact_cooldown > 0.0f) impact_cooldown -= dt > 0 ? dt : 0.0f;
  if (car->impact >= 7.0f && impact_cooldown <= 0.0f &&
      !se->crash && !se->crash1 && !se->crash4 && !se->crash5) {
    int id = linux_crash_sample(S_CRASH3);
    if (SAMPLES[id].n > 1) {
      float strength = car->impact > 30.0f ? 30.0f : car->impact;
      float volume = CAR_GAIN * al_gain(0.3f + strength * 0.023f);
      play_shot(id, volume, 1.0f);
      impact_cooldown = 0.22f;
      if (getenv("NX3DX_SOUND_DEBUG"))
        fprintf(stderr, "linux sound: collision fallback (impact %.1f, wav %s, gain %.2f)\n",
                car->impact, EFFECT_FILES[id - S_SLIP], volume);
    }
  }
#endif
  se->crash = se->crash5 = se->crash4 = se->crash1 = 0;
  car->impact = 0;

  /* the MORE page's CAR SOUND (snd_car_volume) over all of the car's */
  for (int i = 0; i < N_LOOP; ++i) LOOPS[i].vol = LOOPS[i].vol * CAR_GAIN;
}

void snd_car_volume(float v) { CAR_GAIN = v < 0 ? 0.0f : v > 1 ? 1.0f : v; }

void snd_quiet(void) {
  for (int i = 0; i < N_LOOP; ++i) LOOPS[i].vol = 0;
}

/* ---- audio thread ---- */

static void mix_voice(voice *v, float *acc, int frames, int loop) {
  const sample *s = v->s;
  if (!s || !s->data || s->n < 2) return;
  float target = v->vol;
  if (target <= 0.0005f && v->cur_vol <= 0.0005f) { v->cur_vol = 0; return; }
  double step = (double)v->pitch * s->rate / SND_RATE;
  float g = v->cur_vol, dg = (target - g) / (float)frames;
  double pos = v->pos;
  for (int i = 0; i < frames; ++i) {
    int p = (int)pos;
    if (p >= s->n - 1) {
      if (!loop) { v->s = NULL; break; }
      pos = fmod(pos, s->n - 1); /* also after a switch to a shorter sample */
      p = (int)pos;
    }
    float t = (float)(pos - p);
    acc[i] += g * (s->data[p] + (s->data[p + 1] - s->data[p]) * t);
    g += dg;
    pos += step;
  }
  v->pos = pos;
  v->cur_vol = target;
}

void snd_mix(short *out, int frames) {
  static float acc[4096];
  while (frames > 0) {
    int n = frames > 4096 ? 4096 : frames;
    memset(acc, 0, sizeof(float) * (size_t)n);
    for (int i = 0; i < N_LOOP; ++i) mix_voice(&LOOPS[i], acc, n, 1);
    if (BAR.played_seq != BAR.seq) { BAR.played_seq = BAR.seq; BAR.pos = 0; BAR.cur_vol = BAR.vol; }
    mix_voice(&BAR, acc, n, 1);
    for (int i = 0; i < N_SHOT; ++i) {
      voice *v = &SHOTS[i];
      if (v->played_seq != v->seq) { v->played_seq = v->seq; v->pos = 0; v->cur_vol = v->vol; }
      if (v->s) mix_voice(v, acc, n, 0);
    }
    for (int i = 0; i < n; ++i) {
      out[2 * i] = out[2 * i + 1] = (short)(32767.0f * tanhf(acc[i] * (1.0f / 32767.0f))); /* soft clip */
    }
    out += 2 * n;
    frames -= n;
  }
}
