/* NX3DX+ Linux x86_64 native SDL2 + OpenGL ES 2.0 bootstrap.
 * Reuses the original nx3dx engine and its checked-in generated headers/physics.
 * Experimental: soundtrack playback uses FFmpeg; attract movies are not wired up yet.
 */
#include <SDL.h>
#include <GLES2/gl2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "app.h"
#include "pad.h"
#include "sound.h"
#include "bgm_linux.h"
#include "file_picker.h"
#include "version.h"

#define MAX_PADS 8

typedef struct {
  SDL_GameController *ctl;
  SDL_JoystickID id;
  unsigned tag;
} linux_pad;

static linux_pad pads[MAX_PADS];
static SDL_AudioDeviceID audio_device;
static SDL_Window *window;
/* SDL reports each touchscreen finger separately. Do not route native touch
 * through a single synthetic mouse, or steering and throttle share one ID. */
#define MAX_TOUCHES 16
typedef struct {
  int active;
  SDL_TouchID device;
  SDL_FingerID finger;
  float x, y;
} linux_touch;
static linux_touch fingers[MAX_TOUCHES];
static int mouse_down;
static float mouse_x, mouse_y;
static int fb_width, fb_height;
static float touch_scale = 1.0f, touch_edge;

static float clamp_axis(Sint16 v) {
  return v < 0 ? (float)v / 32768.0f : (float)v / 32767.0f;
}

static void audio_mix(void *userdata, Uint8 *stream, int len) {
  (void)userdata;
  if (len > 0) {
    memset(stream, 0, (size_t)len);
    int frames = len / (int)(2 * sizeof(short));
    snd_mix((short *)stream, frames);
    linux_bgm_mix((int16_t *)stream, frames);
  }
}

static void open_audio(void) {
  SDL_AudioSpec desired, have;
  SDL_zero(desired);
  desired.freq = SND_RATE;
  desired.format = AUDIO_S16SYS;
  desired.channels = 2;
  desired.samples = 1024;
  desired.callback = audio_mix;
  audio_device = SDL_OpenAudioDevice(NULL, 0, &desired, &have, 0);
  if (!audio_device) {
    fprintf(stderr, "linux: no SDL audio device: %s\n", SDL_GetError());
  } else {
    SDL_PauseAudioDevice(audio_device, 0);
    fprintf(stderr, "linux: audio %d Hz stereo S16\n", have.freq);
  }
}

static void open_controller(int index) {
  if (!SDL_IsGameController(index)) return;
  SDL_GameController *c = SDL_GameControllerOpen(index);
  if (!c) return;
  SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c));
  for (int i = 0; i < MAX_PADS; ++i) {
    if (pads[i].ctl && pads[i].id == id) {
      SDL_GameControllerClose(c);
      return;
    }
  }
  for (int i = 0; i < MAX_PADS; ++i) {
    if (pads[i].ctl) continue;
    pads[i].ctl = c;
    pads[i].id = id;
    const char *name = SDL_GameControllerName(c);
    pads[i].tag = pad_tag_of(name ? name : "SDL gamepad");
    fprintf(stderr, "linux: gamepad %d: %s\n", i, name ? name : "unknown");
    return;
  }
  SDL_GameControllerClose(c);
}

static int find_controller(SDL_JoystickID id) {
  for (int i = 0; i < MAX_PADS; ++i)
    if (pads[i].ctl && pads[i].id == id) return i;
  return -1;
}

static void close_controller(SDL_JoystickID id) {
  int i = find_controller(id);
  if (i < 0) return;
  pad_dev_gone(100 + i);
  SDL_GameControllerClose(pads[i].ctl);
  memset(&pads[i], 0, sizeof pads[i]);
}

static int pad_button_code(Uint8 button) {
  switch (button) {
    case SDL_CONTROLLER_BUTTON_A: return 96;
    case SDL_CONTROLLER_BUTTON_B: return 97;
    case SDL_CONTROLLER_BUTTON_X: return 99;
    case SDL_CONTROLLER_BUTTON_Y: return 100;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return 102;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return 103;
    case SDL_CONTROLLER_BUTTON_LEFTSTICK: return 106;
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return 107;
    case SDL_CONTROLLER_BUTTON_START: return 108;
    case SDL_CONTROLLER_BUTTON_BACK: return 109;
    case SDL_CONTROLLER_BUTTON_GUIDE: return 110;
    default: return -1;
  }
}

static void poll_controller_axes(void) {
  for (int i = 0; i < MAX_PADS; ++i) {
    SDL_GameController *c = pads[i].ctl;
    if (!c) continue;
    float v[PAD_AXES] = {0};
    v[PAD_AX_X] = clamp_axis(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX));
    v[PAD_AX_Y] = clamp_axis(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY));
    v[PAD_AX_Z] = clamp_axis(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTX));
    v[PAD_AX_RZ] = clamp_axis(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTY));
    v[PAD_AX_LT] = (float)SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT) / 32767.0f;
    v[PAD_AX_RT] = (float)SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) / 32767.0f;
    v[PAD_AX_HAT_X] = (float)SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)
                    - (float)SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    v[PAD_AX_HAT_Y] = (float)SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_DOWN)
                    - (float)SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_UP);
    v[PAD_AX_RX] = v[PAD_AX_Z];
    v[PAD_AX_RY] = v[PAD_AX_RZ];
    pad_axes_dev(100 + i, pads[i].tag, v, PAD_AXES);
  }
}

static int android_key(SDL_Keycode k) {
  if (k >= SDLK_a && k <= SDLK_z) return 29 + (int)(k - SDLK_a);
  if (k >= SDLK_0 && k <= SDLK_9) return 7 + (int)(k - SDLK_0);
  if (k >= SDLK_KP_0 && k <= SDLK_KP_9) return 144 + (int)(k - SDLK_KP_0);
  switch (k) {
    case SDLK_UP: return 19;
    case SDLK_DOWN: return 20;
    case SDLK_LEFT: return 21;
    case SDLK_RIGHT: return 22;
    case SDLK_RETURN: case SDLK_KP_ENTER: return 66;
    case SDLK_SPACE: return 62;
    case SDLK_BACKSPACE: return 4;
    case SDLK_ESCAPE: return 111;
    case SDLK_TAB: return 61;
    case SDLK_LSHIFT: case SDLK_RSHIFT: return 59;
    case SDLK_LCTRL: case SDLK_RCTRL: return 113;
    case SDLK_LALT: case SDLK_RALT: return 57;
    case SDLK_COMMA: return 55;
    case SDLK_PERIOD: return 56;
    case SDLK_MINUS: return 69;
    case SDLK_EQUALS: return 70;
    default: return -1;
  }
}

static void set_size(void) {
  int w = 0, h = 0;
  SDL_GL_GetDrawableSize(window, &w, &h);
  if (w < 1 || h < 1) return;
  if (w == fb_width && h == fb_height) return;
  fb_width = w;
  fb_height = h;
  touch_scale = 768.0f / (float)fb_height;
  touch_edge = ((float)fb_width * touch_scale - 1024.0f) * 0.5f;
  if (touch_edge < 1.0f) touch_edge = 0.0f;
  app_wide(touch_edge, 1.0f);
  app_resize(fb_width, fb_height);
}

/* SDL mouse and normalized finger coordinates are window coordinates, whereas
 * the game's GL viewport uses drawable pixels (different on HiDPI screens). */
static void touch(int id, int phase, float x, float y) {
  int ww = 0, wh = 0;
  SDL_GetWindowSize(window, &ww, &wh);
  float sx = ww > 0 ? (float)fb_width / (float)ww : 1.0f;
  float sy = wh > 0 ? (float)fb_height / (float)wh : 1.0f;
  app_touch((void *)(intptr_t)(id + 1), phase,
            x * sx * touch_scale - touch_edge, y * sy * touch_scale);
}

static int touch_slot(SDL_TouchID device, SDL_FingerID finger) {
  for (int i = 0; i < MAX_TOUCHES; ++i)
    if (fingers[i].active && fingers[i].device == device &&
        fingers[i].finger == finger) return i;
  return -1;
}

/* One stable pointer-like ID per live finger: mouse=1, touchscreen=2..17.
 * The HUD stores the ID while steering, braking or accelerating is held. */
static void finger_event(const SDL_TouchFingerEvent *ev) {
  /* Ignore synthetic touch events generated by the actual mouse. */
  if (ev->touchId == SDL_MOUSE_TOUCHID) return;
  int ww = 0, wh = 0;
  SDL_GetWindowSize(window, &ww, &wh);
  if (ww <= 0 || wh <= 0) return;
  float x = ev->x * (float)ww, y = ev->y * (float)wh;
  int slot = touch_slot(ev->touchId, ev->fingerId);
  if (ev->type == SDL_FINGERDOWN) {
    if (slot >= 0) {
      touch(slot + 1, 2, fingers[slot].x, fingers[slot].y);
      memset(&fingers[slot], 0, sizeof fingers[slot]);
    }
    for (int i = 0; i < MAX_TOUCHES; ++i)
      if (!fingers[i].active) { slot = i; break; }
    if (slot < 0) {
      fprintf(stderr, "linux: ignoring finger: %d active touch slots in use\n", MAX_TOUCHES);
      return;
    }
    fingers[slot] = (linux_touch){ .active = 1, .device = ev->touchId,
                                   .finger = ev->fingerId, .x = x, .y = y };
    if (getenv("NX3DX_TOUCH_DEBUG"))
      fprintf(stderr, "linux: native touchscreen finger DOWN in slot %d\n", slot + 1);
    touch(slot + 1, 0, x, y);
  } else if (slot >= 0) {
    fingers[slot].x = x;
    fingers[slot].y = y;
    touch(slot + 1, ev->type == SDL_FINGERUP ? 2 : 1, x, y);
    if (ev->type == SDL_FINGERUP) {
      if (getenv("NX3DX_TOUCH_DEBUG"))
        fprintf(stderr, "linux: native touchscreen finger UP in slot %d\n", slot + 1);
      memset(&fingers[slot], 0, sizeof fingers[slot]);
    }
  }
}

static void release_all_touches(void) {
  for (int i = 0; i < MAX_TOUCHES; ++i) {
    if (!fingers[i].active) continue;
    touch(i + 1, 2, fingers[i].x, fingers[i].y);
    memset(&fingers[i], 0, sizeof fingers[i]);
  }
  if (mouse_down) {
    touch(0, 2, mouse_x, mouse_y);
    mouse_down = 0;
  }
}

/* app_bgm returns either a system/race track name (without extension) or
 * the player's absolute path to a custom MP3 under the save directory. */
static int bgm_file_path(char *out, size_t outsize, const char *data, const char *name) {
  if (!name || !*name) return 0;
  if (name[0] == '/' || name[0] == '.') {
    int n = snprintf(out, outsize, "%s", name);
    return n > 0 && (size_t)n < outsize && access(out, R_OK) == 0;
  }
  const char *ext[] = { ".m4a", ".mp3", ".wav" };
  const char *dirs[] = { "sound/bgm", "extra", "../extra" };
  for (int d = 0; d < (int)(sizeof dirs / sizeof dirs[0]); ++d) {
    for (int e = 0; e < (int)(sizeof ext / sizeof ext[0]); ++e) {
      int n = snprintf(out, outsize, "%s/%s/%s%s", data, dirs[d], name, ext[e]);
      if (n > 0 && (size_t)n < outsize && access(out, R_OK) == 0) return 1;
    }
  }
  return 0;
}

/* Compare names by content: custom BGM paths are in reusable buffers. */
static void update_bgm(const char *data) {
  static char last_name[2048];
  const char *track = app_bgm();
  if (!track) track = "";
  if (strcmp(track, last_name) != 0) {
    char path[2048];
    snprintf(last_name, sizeof last_name, "%s", track);
    if (!track[0]) linux_bgm_set_file(NULL);
    else if (bgm_file_path(path, sizeof path, data, track))
      linux_bgm_set_file(path);
    else {
      fprintf(stderr, "linux BGM: missing track '%s' in %s/sound/bgm\n", track, data);
      linux_bgm_set_file(NULL);
    }
  }
  linux_bgm_set_volume(app_bgm_level());
}

static int data_ok(const char *dir) {
  char path[2048];
  int n = snprintf(path, sizeof path, "%s/menu.bin", dir);
  return n > 0 && n < (int)sizeof path && access(path, R_OK) == 0;
}

static double now_seconds(void) {
  return (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
}

int main(int argc, char **argv) {
  const char *data = "data";
  int fullscreen = 0;
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) data = argv[++i];
    else if (strcmp(argv[i], "--fullscreen") == 0) fullscreen = 1;
    else if (strcmp(argv[i], "--window") == 0) fullscreen = 0;
    else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
      puts(NX3DX_LINUX_NAME " (x86_64, OpenGL ES 2.0)");
      return 0;
    } else if (strcmp(argv[i], "--help") == 0) {
      puts(NX3DX_LINUX_NAME);
      puts("Usage: NX3DX [--data DIR] [--fullscreen|--window] [--version]");
      return 0;
    } else {
      fprintf(stderr, "Unknown argument: %s\n", argv[i]);
      return 2;
    }
  }
  if (!data_ok(data)) {
    fprintf(stderr, "linux: %s/menu.bin not found (use --data DIR)\n", data);
    return 1;
  }
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
    fprintf(stderr, "linux: SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
  Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
  if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  window = SDL_CreateWindow(NX3DX_LINUX_NAME,
                            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                            1280, 800, flags);
  if (!window) {
    fprintf(stderr, "linux: SDL_CreateWindow: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  SDL_GLContext gl = SDL_GL_CreateContext(window);
  if (!gl) {
    fprintf(stderr, "linux: SDL_GL_CreateContext (GLES2): %s\n", SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  SDL_GL_MakeCurrent(window, gl);
  int vsync = 1;
  const char *sync = getenv("NX3DX_VSYNC");
  if (sync && strcmp(sync, "0") == 0) vsync = 0;
  if (SDL_GL_SetSwapInterval(vsync) != 0)
    fprintf(stderr, "linux: swap interval unavailable: %s\n", SDL_GetError());
  fprintf(stderr, "linux: GL renderer %s\n", glGetString(GL_RENDERER));
  fprintf(stderr, "linux: GL version %s\n", glGetString(GL_VERSION));
  fprintf(stderr, "linux: %s (x86_64 native SDL2/GLES2)\n", NX3DX_LINUX_NAME);
  fprintf(stderr, "linux: data %s\n", data);
  int w = 0, h = 0;
  SDL_GL_GetDrawableSize(window, &w, &h);
  if (w <= 0 || h <= 0) {
    fprintf(stderr, "linux: invalid drawable size %dx%d\n", w, h);
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  fb_width = w;
  fb_height = h;
  touch_scale = 768.0f / (float)h;
  touch_edge = ((float)w * touch_scale - 1024.0f) * 0.5f;
  if (touch_edge < 1.0f) touch_edge = 0.0f;

  char *save = SDL_GetPrefPath("NX3DX", "Linux-Reborn");
  if (!save) {
    fprintf(stderr, "linux: cannot create save directory: %s\n", SDL_GetError());
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  app_save_dir(save);
  fprintf(stderr, "linux: saves %s\n", save);
  SDL_free(save);
  app_console();
  app_wide(touch_edge, 1.0f);
  if (app_init(data, w, h, 1024, 768) != 0) {
    fprintf(stderr, "linux: app_init failed: %s\n", app_error());
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  /* First native milestone: no shared GL loading context. Menu loading is
   * incremental on the game thread as in the Windows no-loader fallback. */
  fprintf(stderr, "linux: v2.4.1 soundtrack enabled (FFmpeg), attract video playback not implemented yet\n");
  /* Initialise the music mix lock BEFORE starting SDL's audio callback. */
  int music_ready = linux_bgm_init() == 0;
  open_audio();
  if (!audio_device) {
    if (music_ready) linux_bgm_shutdown();
    fprintf(stderr, "linux BGM: disabled because SDL audio output failed\n");
  }
  for (int i = 0; i < SDL_NumJoysticks(); ++i) open_controller(i);
  int running = 1, previous_movie = -2;
  double last = now_seconds();
  while (running) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      switch (ev.type) {
        case SDL_QUIT: running = 0; break;
        case SDL_WINDOWEVENT:
          if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
              ev.window.event == SDL_WINDOWEVENT_RESIZED) set_size();
          if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
            release_all_touches();
            app_suspend(1);
          }
          if (ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) app_suspend(0);
          break;
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
          if (ev.key.repeat) break;
          if (ev.type == SDL_KEYDOWN &&
              (ev.key.keysym.mod & KMOD_ALT) && ev.key.keysym.sym == SDLK_RETURN) {
            Uint32 f = SDL_GetWindowFlags(window);
            SDL_SetWindowFullscreen(window, (f & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
            set_size();
            break;
          }
          int key = android_key(ev.key.keysym.sym);
          if (key >= 0) app_pad_key(key, ev.type == SDL_KEYDOWN);
          break;
        }
        case SDL_FINGERDOWN:
        case SDL_FINGERMOTION:
        case SDL_FINGERUP:
          finger_event(&ev.tfinger);
          break;
        case SDL_MOUSEBUTTONDOWN:
          /* SDL also synthesizes a mouse click for a finger. Ignore that
           * duplicate; native finger events carry independent touch IDs. */
          if (ev.button.which == SDL_TOUCH_MOUSEID) break;
          if (ev.button.button == SDL_BUTTON_LEFT) {
            mouse_down = 1;
            mouse_x = (float)ev.button.x;
            mouse_y = (float)ev.button.y;
            touch(0, 0, mouse_x, mouse_y);
          } else if (ev.button.button == SDL_BUTTON_RIGHT) {
            app_pad_key(4, 1);
          }
          break;
        case SDL_MOUSEBUTTONUP:
          if (ev.button.which == SDL_TOUCH_MOUSEID) break;
          if (ev.button.button == SDL_BUTTON_LEFT) {
            mouse_x = (float)ev.button.x;
            mouse_y = (float)ev.button.y;
            if (mouse_down) touch(0, 2, mouse_x, mouse_y);
            mouse_down = 0;
          } else if (ev.button.button == SDL_BUTTON_RIGHT) {
            app_pad_key(4, 0);
          }
          break;
        case SDL_MOUSEMOTION:
          if (ev.motion.which == SDL_TOUCH_MOUSEID) break;
          if (mouse_down) {
            mouse_x = (float)ev.motion.x;
            mouse_y = (float)ev.motion.y;
            touch(0, 1, mouse_x, mouse_y);
          }
          break;
        case SDL_CONTROLLERDEVICEADDED: open_controller(ev.cdevice.which); break;
        case SDL_CONTROLLERDEVICEREMOVED: close_controller(ev.cdevice.which); break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP: {
          int i = find_controller(ev.cbutton.which);
          int key = pad_button_code(ev.cbutton.button);
          if (i >= 0 && key >= 0)
            pad_key_dev(100 + i, key, ev.type == SDL_CONTROLLERBUTTONDOWN);
          break;
        }
        default: break;
      }
    }
    if (!running) break;
    poll_controller_axes();
    /* Keep the attract loop progressing until video playback is implemented. */
    int movie = app_movie();
    if (movie >= 0 && movie != previous_movie) {
      app_movie_done();
    }
    previous_movie = movie;
    const char *pick = app_wants_bgm_pick();
    if (pick) {
      int result = linux_pick_bgm(pick);
      app_bgm_picked(result == 1,
                     result == 1 ? "(YOUR MP3 IS ON)" :
                     result == 0 ? "(NO MP3 CHOSEN)" :
                     "(FILE PICKER FAILED - SEE LOG)");
      /* GUI file selection can take minutes. Do not advance the game's
       * simulation by the time the dialog was open when we resume. */
      last = now_seconds();
    }
    set_size();
    double start = now_seconds();
    float dt = (float)(start - last);
    last = start;
    app_frame(dt);
    update_bgm(data);
    double before_swap = now_seconds();
    SDL_GL_SwapWindow(window);
    app_frame_done((now_seconds() - before_swap) * 1000.0);
    int fps = app_refresh_rate();
    if (fps > 0 && fps <= 240) {
      double remaining = 1.0 / (double)fps - (now_seconds() - start);
      if (remaining > 0.002) SDL_Delay((Uint32)(remaining * 1000.0));
    }
    if (app_wants_restart()) {
      fprintf(stderr, "linux: settings saved; restart this launcher to apply them\n");
      running = 0;
    }
  }
  release_all_touches();
  for (int i = 0; i < MAX_PADS; ++i)
    if (pads[i].ctl) close_controller(pads[i].id);
  if (audio_device) SDL_CloseAudioDevice(audio_device);
  linux_bgm_shutdown();
  SDL_GL_DeleteContext(gl);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
