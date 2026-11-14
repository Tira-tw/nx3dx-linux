/* Game controllers and keyboards (a port addition: the arcade has its wheel and pedals). The
 * platforms hand in each key or button as Android's KeyEvent code (iOS's GCController buttons
 * mapped to the same codes) and the sticks, triggers and hat as PAD_AXES values; each of the
 * game's actions takes up to two of them (a keyboard's and a pad's), remapped on the OPTION
 * screen's mapping page and kept in save_dir/pad_bindings. */
#ifndef PAD_H
#define PAD_H

enum {
  PAD_STEER_L, PAD_STEER_R, PAD_THROTTLE, PAD_BRAKE, PAD_SHIFT_UP, PAD_SHIFT_DOWN,
  PAD_GEAR1, PAD_GEAR2, PAD_GEAR3, PAD_GEAR4, PAD_GEAR5, PAD_GEAR6, PAD_NEUTRAL,
  PAD_VIEW, PAD_RESET, PAD_TRANS, PAD_PAUSE,
  PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_OK, PAD_BACK,
  PAD_INTRUDE, /* (v2.2) the cabinet's 乱入 button: the menus' middle key, a host's 乱入 on or off */
  PAD_ACTIONS
};
#define PAD_SLOTS 2
/* the axes, in this order (Android MotionEvent's AXIS_X, Y, Z, RZ, LTRIGGER, RTRIGGER, GAS,
 * BRAKE, HAT_X, HAT_Y, RX, RY; iOS's left stick, right stick, triggers and d-pad the same) */
enum { PAD_AX_X, PAD_AX_Y, PAD_AX_Z, PAD_AX_RZ, PAD_AX_LT, PAD_AX_RT, PAD_AX_GAS, PAD_AX_BRAKE,
       PAD_AX_HAT_X, PAD_AX_HAT_Y, PAD_AX_RX, PAD_AX_RY, PAD_AXES };
/* a binding: none, a key (its code), an axis pushed + or - */
enum { PAD_NONE, PAD_KEY, PAD_AXIS_POS, PAD_AXIS_NEG };
typedef struct { int type, index; } pad_bind;

/* the platform's input (any thread: they take a lock-free copy each frame) */
void pad_key(int code, int down);
void pad_axes(const float *v, int n);
/* (v2.3) several controllers at once (a wheel and a pedal / shifter set): each device's keys and
 * axes by its own id (any number the platform keeps for it while connected; pad_key / pad_axes
 * are device 0) and tag (its identity, the same every run: a hash of its vendor, product or
 * descriptor, 1..PAD_TAG_MASK; 0 none). A key is held while any device holds it; an axis bound
 * as PAD_AX_X .. is the merge of all the devices' (one device: its own, as before); with two or
 * more sending axes, learning binds the pushed device's own axis, index tag << 4 | axis
 * (>= PAD_AX_DEV), the mapping page's "J1 LS X +" ... A device unplugged: pad_dev_gone. */
#define PAD_TAG_MASK 0x7FFFu
#define PAD_AX_DEV 16
/* (Windows' joysticks' buttons 17..32: codes past Android's BUTTON_16 (203), unused there) */
#define PAD_CODE_BTN17 300
void pad_key_dev(int dev, int code, int down);
void pad_axes_dev(int dev, unsigned tag, const float *v, int n);
void pad_dev_gone(int dev);
/* (a tag from an identity string) */
static inline unsigned pad_tag_of(const char *s) {
  unsigned h = 2166136261u;
  while (s && *s) h = (h ^ (unsigned char)*s++) * 16777619u;
  return h % PAD_TAG_MASK + 1;
}
/* once a frame, before the reads: latches the inputs, the presses since the last frame */
void pad_frame(void);
/* an action's travel (0..1: a stick's past the dead zone, a trigger's, a key's 1) and whether it
 * was pressed since the last frame */
float pad_value(int action);
int pad_pressed(int action);
/* a key's own press this frame (Android KeyEvent code), whatever it is bound to: the menus'
 * fixed keys (the arrows, Enter, Backspace, the d-pad) */
int pad_key_pressed(int code);
/* frames since any pad or keyboard input (a large number: none yet) */
int pad_idle(void);
/* the dead zone of the sticks, percent (5..50) */
int pad_deadzone(void);
void pad_set_deadzone(int pct);
/* the sensitivities (v2.2), 1..10 (5 linear): a stick's or wheel's steering and the triggers' or
 * pedals' (a curve on the travel, full at full travel still); a key's: how fast the steering and
 * pedals come on, 0 the arcade's own (instant pedals, the car's own steering ramp) */
enum { PAD_SENS_STEER, PAD_SENS_PEDAL, PAD_SENS_KEY, PAD_SENS_N };
int pad_sens(int which);
/* (v2.2) KEYBOARD AS WHEEL: 1 (default) STEER LEFT / RIGHT two inputs, 0 one axis both ways */
int pad_steer_split(void);
void pad_set_steer_split(int on);
void pad_set_sens(int which, int v);
/* whether an action's value this frame is a key's (or a hat's: all or nothing) */
int pad_from_key(int action);
/* the bindings: an action's slot, set or cleared */
pad_bind pad_get(int action, int slot);
void pad_set(int action, int slot, pad_bind b);
void pad_defaults(void);
/* learning: the next key pressed or axis pushed past half becomes the binding (0 when done or
 * none asked) */
void pad_learn(int action, int slot);
int pad_learning(void);
void pad_learn_cancel(void);
/* save_dir/pad_bindings; load returns 0 if it read one */
int pad_load(const char *path);
void pad_save(const char *path);
/* names for the mapping page: an action's, a binding's (KEY 96, A, LS LEFT ...) */
const char *pad_action_name(int action);
void pad_bind_name(pad_bind b, char *out, int n);

#endif
