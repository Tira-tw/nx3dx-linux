#include "pad.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* (all on the game's thread: the platforms queue their events to it) */
#define CODES 400
/* (v2.3) down[]: a bit for each device holding the key (one device's release no longer lets go
 * of another's hold); nonzero = held */
static unsigned char down[CODES], presses[CODES], latched_press[CODES];
static float axes[PAD_AXES];
/* (v2.3, several controllers at once: a wheel and a pedal / shifter set, Allan's friend's PXN V3
 * Pro + A7) each device's own last axes; axes[] their merge (one device: its own values, as
 * before), a device's own axes reached by a binding index of tag << 4 | axis (pad.h) */
#define DEVS 8
typedef struct {
  int id, used, has_axes;
  unsigned tag;   /* (its identity, stable across runs; 0 none: the merge only) */
  unsigned seen;  /* (when it last sent anything: the oldest makes way) */
  float v[PAD_AXES], first[PAD_AXES], rest[PAD_AXES];
  unsigned char moved[PAD_AXES];
} pad_dev;
static pad_dev devs[DEVS];
static unsigned dev_clock;
/* (the device tags in the order seen this run: the J1, J2 ... of the names) */
static unsigned tags_seen[16];
static pad_bind bind[PAD_ACTIONS][PAD_SLOTS];
static int deadzone = 15, idle = 1 << 20, learn_action = -1, learn_slot;
static int any_input;
/* (v2.3) a key mapping just taken: nothing counts as pressed till every key and button is up and
 * every stick back near rest (Allan 11-09: Esc's own back, sent with it, left the mapping page;
 * a pad's other buttons pressed with it did their actions) */
static int hush;
static float learn_rest[PAD_AXES]; /* (the sticks and triggers where they were as learning began) */
static float act_prev[PAD_ACTIONS], act_now[PAD_ACTIONS];
static unsigned char act_pressed[PAD_ACTIONS], act_key[PAD_ACTIONS];
static int sens[PAD_SENS_N] = { 5, 5, 0 };
/* (v2.2) KEYBOARD AS WHEEL: on, STEER LEFT and RIGHT are two inputs (keys); off, one: the
 * wheel's or stick's axis, both its ways (pad_steer_split) */
static int steer_split = 1;
int pad_steer_split(void) { return steer_split; }
void pad_set_steer_split(int on) { steer_split = on != 0; }

/* Android's KeyEvent codes the defaults and the names use */
enum {
  K_DPAD_UP = 19, K_DPAD_DOWN = 20, K_DPAD_LEFT = 21, K_DPAD_RIGHT = 22, K_DPAD_CENTER = 23,
  K_A = 29, K_D = 32, K_E = 33, K_Q = 45, K_R = 46, K_S = 47, K_W = 51, K_C = 31, K_V = 50, K_T = 48, K_I = 37,
  K_1 = 8, K_SPACE = 62, K_ENTER = 66, K_ESCAPE = 111, K_BACK = 4, K_SHIFT_L = 59, K_CTRL_L = 113,
  K_BUTTON_A = 96, K_BUTTON_B = 97, K_BUTTON_X = 99, K_BUTTON_Y = 100, K_BUTTON_L1 = 102, K_BUTTON_R1 = 103,
  K_BUTTON_L2 = 104, K_BUTTON_R2 = 105, K_BUTTON_THUMBL = 106, K_BUTTON_THUMBR = 107, K_BUTTON_START = 108,
  K_BUTTON_SELECT = 109, K_BUTTON_MODE = 110
};

void pad_defaults(void) {
  memset(bind, 0, sizeof bind);
#define B(a, s, t, i) bind[a][s] = (pad_bind){ t, i }
  /* (v2.2, Allan 11-08: the keyboard's WASD for the car and the menus, Enter OK, Backspace back,
   * V view, I 乱入, Shift / Ctrl the gears up and down, 1..6 the gears) */
  B(PAD_STEER_L, 0, PAD_KEY, K_A), B(PAD_STEER_L, 1, PAD_AXIS_NEG, PAD_AX_X);
  B(PAD_STEER_R, 0, PAD_KEY, K_D), B(PAD_STEER_R, 1, PAD_AXIS_POS, PAD_AX_X);
  B(PAD_THROTTLE, 0, PAD_KEY, K_W), B(PAD_THROTTLE, 1, PAD_AXIS_POS, PAD_AX_RT);
  B(PAD_BRAKE, 0, PAD_KEY, K_S), B(PAD_BRAKE, 1, PAD_AXIS_POS, PAD_AX_LT);
  B(PAD_SHIFT_UP, 0, PAD_KEY, K_SHIFT_L), B(PAD_SHIFT_UP, 1, PAD_KEY, K_BUTTON_R1);
  B(PAD_SHIFT_DOWN, 0, PAD_KEY, K_CTRL_L), B(PAD_SHIFT_DOWN, 1, PAD_KEY, K_BUTTON_L1);
  for (int g = 0; g < 6; ++g) B(PAD_GEAR1 + g, 0, PAD_KEY, K_1 + g);
  B(PAD_NEUTRAL, 0, PAD_KEY, K_1 - 1); /* 0 */
  B(PAD_VIEW, 0, PAD_KEY, K_V), B(PAD_VIEW, 1, PAD_KEY, K_BUTTON_Y);
  /* (v2.3, Allan 11-09: a pad's B OK, X back, A 乱入, Y view; the reset to R3) */
  B(PAD_RESET, 0, PAD_KEY, K_R), B(PAD_RESET, 1, PAD_KEY, K_BUTTON_THUMBR);
  B(PAD_TRANS, 0, PAD_KEY, K_T), B(PAD_TRANS, 1, PAD_KEY, K_BUTTON_SELECT);
  B(PAD_PAUSE, 0, PAD_KEY, K_ESCAPE), B(PAD_PAUSE, 1, PAD_KEY, K_BUTTON_START);
  B(PAD_UP, 0, PAD_KEY, K_W), B(PAD_UP, 1, PAD_AXIS_NEG, PAD_AX_HAT_Y);
  B(PAD_DOWN, 0, PAD_KEY, K_S), B(PAD_DOWN, 1, PAD_AXIS_POS, PAD_AX_HAT_Y);
  B(PAD_LEFT, 0, PAD_KEY, K_A), B(PAD_LEFT, 1, PAD_AXIS_NEG, PAD_AX_HAT_X);
  B(PAD_RIGHT, 0, PAD_KEY, K_D), B(PAD_RIGHT, 1, PAD_AXIS_POS, PAD_AX_HAT_X);
  B(PAD_OK, 0, PAD_KEY, K_ENTER), B(PAD_OK, 1, PAD_KEY, K_BUTTON_B);
  B(PAD_BACK, 0, PAD_KEY, K_BACK), B(PAD_BACK, 1, PAD_KEY, K_BUTTON_X);
  B(PAD_INTRUDE, 0, PAD_KEY, K_I), B(PAD_INTRUDE, 1, PAD_KEY, K_BUTTON_A);
#undef B
  deadzone = 15;
  sens[PAD_SENS_STEER] = sens[PAD_SENS_PEDAL] = 5;
  sens[PAD_SENS_KEY] = 0;
  steer_split = 1;
}

int pad_sens(int which) { return which >= 0 && which < PAD_SENS_N ? sens[which] : 5; }
void pad_set_sens(int which, int v) {
  if (which < 0 || which >= PAD_SENS_N) return;
  int lo = which == PAD_SENS_KEY ? 0 : 1;
  sens[which] = v < lo ? lo : v > 10 ? 10 : v;
}
int pad_from_key(int a) { return a >= 0 && a < PAD_ACTIONS && act_key[a]; }

/* an axis' travel on the sensitivity's curve: 5 straight, under it softer round the middle (a
 * power up to 2), over it sharper (down to 0.42); full travel stays full */
static float sens_curve(float v, int s) {
  if (s == 5 || v <= 0.0f || v >= 1.0f) return v;
  return powf(v, exp2f((float)(5 - s) * 0.25f));
}

static int inited;
static void init(void) {
  if (inited) return;
  inited = 1;
  pad_defaults();
}

static int learn_take(pad_bind b) {
  if (learn_action < 0) return 0;
  if (!steer_split && (learn_action == PAD_STEER_L || learn_action == PAD_STEER_R)) {
    /* the one STEER: an axis both its ways (right +, left -), a key the right alone */
    if (b.type == PAD_AXIS_POS || b.type == PAD_AXIS_NEG) {
      bind[PAD_STEER_R][learn_slot] = (pad_bind){ PAD_AXIS_POS, b.index };
      bind[PAD_STEER_L][learn_slot] = (pad_bind){ PAD_AXIS_NEG, b.index };
    } else {
      bind[PAD_STEER_R][learn_slot] = b;
    }
    learn_action = -1;
    hush = 1;
    return 1;
  }
  bind[learn_action][learn_slot] = b;
  learn_action = -1;
  hush = 1;
  return 1;
}

/* (v2.3) a device's slot (made for a new one: a free slot, else the longest quiet one's, its keys
 * let go); -1 none */
static void dev_free(int s) {
  unsigned char bit = (unsigned char)(1u << s);
  for (int c = 0; c < CODES; ++c) down[c] &= (unsigned char)~bit;
  memset(&devs[s], 0, sizeof devs[s]);
}
static int dev_slot(int id, int make) {
  int s, freeslot = -1, oldest = 0;
  for (s = 0; s < DEVS; ++s) {
    if (devs[s].used && devs[s].id == id) {
      devs[s].seen = ++dev_clock;
      return s;
    }
    if (!devs[s].used && freeslot < 0) freeslot = s;
    if (devs[s].seen < devs[oldest].seen) oldest = s;
  }
  if (!make) return -1;
  s = freeslot >= 0 ? freeslot : oldest;
  if (freeslot < 0) dev_free(s);
  devs[s].used = 1, devs[s].id = id, devs[s].seen = ++dev_clock;
  return s;
}

/* (v2.3) the merge, an axis at a time: one device: its value; several: the one pushed farthest
 * from its rest (an axis that never moved on a device, an unused one, left out); none pushed: the
 * value nearest 0 (an idle pedal's -1 never steers) */
static void merge_axes(void) {
  for (int i = 0; i < PAD_AXES; ++i) {
    int best = -1, n = 0, calm = -1;
    float bd = -1.0f;
    for (int s = 0; s < DEVS; ++s) {
      if (!devs[s].used || !devs[s].has_axes) continue;
      n++;
      if (calm < 0 || fabsf(devs[s].v[i]) < fabsf(devs[calm].v[i])) calm = s;
      if (!devs[s].moved[i]) continue;
      float d = fabsf(devs[s].v[i] - devs[s].rest[i]);
      if (d > bd) bd = d, best = s;
    }
    if (n == 0) axes[i] = 0.0f; /* (the last one unplugged: nothing held) */
    else if (n == 1) axes[i] = devs[calm].v[i];
    else axes[i] = best >= 0 && bd >= 0.1f ? devs[best].v[i] : devs[calm].v[i];
  }
}

/* (v2.3) a device's own axis (a binding's index tag << 4 | axis): the one pushed farthest of the
 * devices with that tag (two of the same kind), 0 none connected */
static float dev_axis(unsigned tag, int i) {
  int best = -1;
  float bd = -1.0f;
  for (int s = 0; s < DEVS; ++s) {
    if (!devs[s].used || !devs[s].has_axes || devs[s].tag != tag) continue;
    float d = fabsf(devs[s].v[i] - devs[s].rest[i]);
    if (d > bd) bd = d, best = s;
  }
  return best >= 0 ? devs[best].v[i] : 0.0f;
}

/* (v2.3) a binding's axis (0..PAD_AXES-1, a device's own too), -1 not an axis */
static int bind_axis(pad_bind b) {
  if (b.type != PAD_AXIS_POS && b.type != PAD_AXIS_NEG) return -1;
  int i = b.index >= PAD_AX_DEV ? (b.index & 15) : b.index;
  return i >= 0 && i < PAD_AXES ? i : -1;
}

void pad_key_dev(int dev, int code, int is_down) {
  init();
  if (code <= 0 || code >= CODES) return;
  int s = dev_slot(dev, is_down);
  if (s < 0) return; /* (a release from a device never seen: nothing it holds) */
  unsigned char bit = (unsigned char)(1u << s);
  if (is_down && !down[code]) {
    /* (learning: the key is the mapping's and nothing else's; just after it: none count) */
    int learning = learn_action >= 0;
    presses[code] = 1;
    if (learn_take((pad_bind){ PAD_KEY, code }) || learning || hush) presses[code] = 0;
  }
  if (is_down) down[code] |= bit;
  else down[code] &= (unsigned char)~bit;
  any_input = 1;
}
void pad_key(int code, int is_down) { pad_key_dev(0, code, is_down); }

void pad_axes_dev(int dev, unsigned tag, const float *v, int n) {
  init();
  int s = dev_slot(dev, 1);
  pad_dev *d = &devs[s];
  d->tag = tag & PAD_TAG_MASK;
  if (d->tag) {
    int k;
    for (k = 0; k < 16 && tags_seen[k] && tags_seen[k] != d->tag; ++k) {}
    if (k < 16 && !tags_seen[k]) tags_seen[k] = d->tag;
  }
  /* (learning: a device's own axis once two send axes, the merged one with one, as before) */
  int others = 0;
  for (int k = 0; k < DEVS; ++k) others += k != s && devs[k].used && devs[k].has_axes;
  for (int i = 0; i < n && i < PAD_AXES; ++i) {
    float a = v[i] != v[i] ? 0.0f : v[i] < -1 ? -1.0f : v[i] > 1 ? 1.0f : v[i];
    if (!d->has_axes) {
      /* (its rest: the nearest of -1, 0, 1 to the first value: a stick's centre, a pedal's end) */
      d->first[i] = a;
      d->rest[i] = a < -0.5f ? -1.0f : a > 0.5f ? 1.0f : 0.0f;
    }
    if (fabsf(a - d->first[i]) > 0.15f) d->moved[i] = 1;
    if (fabsf(a - d->v[i]) > 0.05f && fabsf(a) > 0.2f) any_input = 1;
    /* learning: a stick or trigger pushed past half from rest */
    if (learn_action >= 0 && fabsf(a) > 0.5f && fabsf(d->v[i]) <= 0.5f) {
      int index = others > 0 && d->tag ? (int)(d->tag << 4) | i : i;
      learn_take((pad_bind){ a > 0 ? PAD_AXIS_POS : PAD_AXIS_NEG, index });
    }
    d->v[i] = a;
  }
  d->has_axes = 1;
  merge_axes();
}
void pad_axes(const float *v, int n) { pad_axes_dev(0, 0, v, n); }

void pad_dev_gone(int dev) {
  init();
  int s = dev_slot(dev, 0);
  if (s < 0) return;
  dev_free(s);
  merge_axes();
}

static float bind_value(pad_bind b) {
  if (b.type == PAD_KEY) return b.index > 0 && b.index < CODES && down[b.index] ? 1.0f : 0.0f;
  int i = bind_axis(b);
  if (i < 0) return 0.0f;
  float a = b.index >= PAD_AX_DEV ? dev_axis((unsigned)b.index >> 4, i) : axes[i];
  float v = b.type == PAD_AXIS_POS ? a : -a, dz = (float)deadzone * 0.01f;
  /* (a hat is all or nothing) */
  if (i == PAD_AX_HAT_X || i == PAD_AX_HAT_Y) return v > 0.5f ? 1.0f : 0.0f;
  return v <= dz ? 0.0f : fminf(1.0f, (v - dz) / (1.0f - dz));
}

static int bind_pressed(pad_bind b) {
  return b.type == PAD_KEY && b.index > 0 && b.index < CODES && latched_press[b.index];
}

void pad_frame(void) {
  init();
  memcpy(latched_press, presses, sizeof presses);
  memset(presses, 0, sizeof presses);
  for (int a = 0; a < PAD_ACTIONS; ++a) {
    float v = 0;
    int p = 0, key = 0;
    int curve = a == PAD_STEER_L || a == PAD_STEER_R ? sens[PAD_SENS_STEER] : a == PAD_THROTTLE || a == PAD_BRAKE ? sens[PAD_SENS_PEDAL] : 5;
    for (int s = 0; s < PAD_SLOTS; ++s) {
      pad_bind b = bind[a][s];
      float bv = bind_value(b);
      int ax = bind_axis(b); /* (v2.3: a device's own hat too) */
      int digital = b.type == PAD_KEY || ax == PAD_AX_HAT_X || ax == PAD_AX_HAT_Y;
      if (!digital) bv = sens_curve(bv, curve);
      if (bv > v || (bv == v && bv > 0 && digital)) v = bv, key = digital;
      p |= bind_pressed(b);
    }
    act_key[a] = (unsigned char)key;
    act_prev[a] = act_now[a];
    act_now[a] = v;
    /* an axis bound as a key: pressed as it goes past half */
    act_pressed[a] = (unsigned char)(p || (v > 0.5f && act_prev[a] <= 0.5f));
    if (hush || learn_action >= 0) act_pressed[a] = 0; /* (v2.3: learning, or just after it) */
  }
  if (hush || learn_action >= 0) memset(latched_press, 0, sizeof latched_press);
  /* (the hush over once everything is let go) */
  if (hush) {
    int held = 0;
    for (int c = 1; c < CODES && !held; ++c) held = down[c];
    for (int i = 0; i < PAD_AXES && !held; ++i) held = fabsf(axes[i] - learn_rest[i]) > 0.3f;
    /* (2 s at most: a stuck key or an odd axis never keeps the menus deaf) */
    if (!held || ++hush > 120) hush = 0;
  }
  idle = any_input ? 0 : idle < (1 << 20) ? idle + 1 : idle;
  any_input = 0;
}

int pad_key_pressed(int code) { return code > 0 && code < CODES && latched_press[code]; }
float pad_value(int a) { return a >= 0 && a < PAD_ACTIONS ? act_now[a] : 0.0f; }
int pad_pressed(int a) { return a >= 0 && a < PAD_ACTIONS && act_pressed[a]; }
int pad_idle(void) { return idle; }
int pad_deadzone(void) { return deadzone; }
void pad_set_deadzone(int pct) { deadzone = pct < 5 ? 5 : pct > 50 ? 50 : pct; }

pad_bind pad_get(int a, int s) {
  init();
  return a >= 0 && a < PAD_ACTIONS && s >= 0 && s < PAD_SLOTS ? bind[a][s] : (pad_bind){ PAD_NONE, 0 };
}

void pad_set(int a, int s, pad_bind b) {
  init();
  if (a >= 0 && a < PAD_ACTIONS && s >= 0 && s < PAD_SLOTS) bind[a][s] = b;
}

void pad_learn(int a, int s) {
  init();
  if (a < 0 || a >= PAD_ACTIONS || s < 0 || s >= PAD_SLOTS) return;
  learn_action = a, learn_slot = s;
  memcpy(learn_rest, axes, sizeof learn_rest); /* (v2.3: the hush's rest) */
  /* (the axes as they are now are the rest a push is measured from: each device's last values,
   * pad_axes_dev) */
}
int pad_learning(void) { return learn_action >= 0; }
void pad_learn_cancel(void) { learn_action = -1; }

int pad_load(const char *path) {
  init();
  FILE *f = fopen(path, "r");
  if (!f) return -1;
  char line[128];
  int got = 0;
  /* (an action a file from an older build has no line for keeps its default: INTRUDE's) */
  pad_bind defaults[PAD_ACTIONS][PAD_SLOTS];
  unsigned char seen[PAD_ACTIONS] = { 0 };
  memcpy(defaults, bind, sizeof bind);
  /* (a file from before the v2.2 keyboard (keys 2: WASD ...) has the old arrows' layout: the
   * new defaults instead, its dead zone and sensitivities kept) */
  int keys = 0;
  while (fgets(line, sizeof line, f)) {
    int k;
    if (sscanf(line, "keys %d", &k) == 1) keys = k;
  }
  rewind(f);
  while (fgets(line, sizeof line, f)) {
    int a, s, t, i, dz;
    if (keys < 2 && sscanf(line, "bind %d", &a) == 1) continue;
    if (sscanf(line, "deadzone %d", &dz) == 1) pad_set_deadzone(dz);
    else if (sscanf(line, "sens %d %d", &a, &s) == 2) pad_set_sens(a, s);
    else if (sscanf(line, "keyaxis %d", &a) == 1) pad_set_steer_split(a);
    else if (sscanf(line, "bind %d %d %d %d", &a, &s, &t, &i) == 4 && a >= 0 && a < PAD_ACTIONS && s >= 0 && s < PAD_SLOTS &&
             t >= PAD_NONE && t <= PAD_AXIS_NEG) {
      if (!got) memset(bind, 0, sizeof bind); /* the file's, not the defaults' */
      bind[a][s] = (pad_bind){ t, i };
      seen[a] = 1;
      got = 1;
    }
  }
  fclose(f);
  for (int a = 0; got && a < PAD_ACTIONS; ++a)
    if (!seen[a]) memcpy(bind[a], defaults[a], sizeof bind[a]);
  /* (v2.3, keys 3: a pad's new OK B, back X, 乱入 A, reset R3; a file from before: those four's
   * pad slot the new defaults, the keyboard's and the rest as the player had them) */
  if (got && keys < 3) {
    static const int NEW_PAD[4] = { PAD_OK, PAD_BACK, PAD_INTRUDE, PAD_RESET };
    for (int k = 0; k < 4; ++k) bind[NEW_PAD[k]][1] = defaults[NEW_PAD[k]][1];
  }
  return got ? 0 : -1;
}

void pad_save(const char *path) {
  init();
  FILE *f = fopen(path, "w");
  if (!f) return;
  fprintf(f, "# WMMT3DX+ port controller bindings: bind action slot type index\n");
  fprintf(f, "keys 3\ndeadzone %d\nkeyaxis %d\n", deadzone, steer_split);
  for (int k = 0; k < PAD_SENS_N; ++k) fprintf(f, "sens %d %d\n", k, sens[k]);
  for (int a = 0; a < PAD_ACTIONS; ++a)
    for (int s = 0; s < PAD_SLOTS; ++s) fprintf(f, "bind %d %d %d %d\n", a, s, bind[a][s].type, bind[a][s].index);
  fclose(f);
}

const char *pad_action_name(int a) {
  static const char *const N[PAD_ACTIONS] = {
    "STEER LEFT", "STEER RIGHT", "THROTTLE", "BRAKE", "SHIFT UP", "SHIFT DOWN",
    "GEAR 1", "GEAR 2", "GEAR 3", "GEAR 4", "GEAR 5", "GEAR 6", "NEUTRAL",
    "VIEW", "RESET", "AT / MT", "PAUSE",
    "MENU UP", "MENU DOWN", "MENU LEFT", "MENU RIGHT", "MENU OK", "MENU BACK", "INTRUDE" };
  return a >= 0 && a < PAD_ACTIONS ? N[a] : "";
}

void pad_bind_name(pad_bind b, char *out, int n) {
  static const char *const AX[PAD_AXES] = { "LS X", "LS Y", "RS X", "RS Y", "LT", "RT", "GAS", "BRAKE", "HAT X", "HAT Y",
                                            "RS2 X", "RS2 Y" };
  if (b.type == PAD_KEY) {
    int c = b.index;
    const char *s = NULL;
    switch (c) {
    case K_DPAD_UP: s = "UP"; break;
    case K_DPAD_DOWN: s = "DOWN"; break;
    case K_DPAD_LEFT: s = "LEFT"; break;
    case K_DPAD_RIGHT: s = "RIGHT"; break;
    case K_DPAD_CENTER: s = "CENTER"; break;
    case K_SPACE: s = "SPACE"; break;
    case K_ENTER: s = "ENTER"; break;
    case K_ESCAPE: s = "ESC"; break;
    case K_BACK: s = "BACK"; break;
    case K_SHIFT_L: s = "L SHIFT"; break;
    case K_CTRL_L: s = "L CTRL"; break;
    case K_BUTTON_A: s = "PAD A"; break;
    case K_BUTTON_B: s = "PAD B"; break;
    case K_BUTTON_X: s = "PAD X"; break;
    case K_BUTTON_Y: s = "PAD Y"; break;
    case K_BUTTON_L1: s = "PAD L1"; break;
    case K_BUTTON_R1: s = "PAD R1"; break;
    case K_BUTTON_L2: s = "PAD L2"; break;
    case K_BUTTON_R2: s = "PAD R2"; break;
    case K_BUTTON_THUMBL: s = "PAD L3"; break;
    case K_BUTTON_THUMBR: s = "PAD R3"; break;
    case K_BUTTON_START: s = "PAD START"; break;
    case K_BUTTON_SELECT: s = "PAD SELECT"; break;
    case K_BUTTON_MODE: s = "PAD HOME"; break;
    }
    if (s) snprintf(out, (size_t)n, "%s", s);
    else if (c >= 7 && c <= 16) snprintf(out, (size_t)n, "KEY %c", '0' + (c - 7));
    else if (c >= 29 && c <= 54) snprintf(out, (size_t)n, "KEY %c", 'A' + (c - 29));
    else if (c >= 188 && c <= 203) snprintf(out, (size_t)n, "PAD %d", c - 187); /* BUTTON_1..16 */
    else if (c >= PAD_CODE_BTN17 && c < PAD_CODE_BTN17 + 16) snprintf(out, (size_t)n, "PAD %d", c - PAD_CODE_BTN17 + 17); /* (v2.3: Windows' joysticks' 17..32) */
    else snprintf(out, (size_t)n, "CODE %d", c);
  } else if ((b.type == PAD_AXIS_POS || b.type == PAD_AXIS_NEG) && b.index >= 0 && b.index < PAD_AXES) {
    snprintf(out, (size_t)n, "%s %s", AX[b.index], b.type == PAD_AXIS_POS ? "+" : "-");
  } else if (b.index >= PAD_AX_DEV && bind_axis(b) >= 0) {
    /* (v2.3) a device's own axis: J1, J2 ... in the order the devices were seen this run */
    unsigned tag = (unsigned)b.index >> 4;
    int k;
    for (k = 0; k < 16 && tags_seen[k] != tag; ++k) {}
    char j[8];
    if (k < 16) snprintf(j, sizeof j, "J%d", k + 1);
    else snprintf(j, sizeof j, "J?");
    snprintf(out, (size_t)n, "%s %s %s", j, AX[bind_axis(b)], b.type == PAD_AXIS_POS ? "+" : "-");
  } else {
    snprintf(out, (size_t)n, "-");
  }
}
