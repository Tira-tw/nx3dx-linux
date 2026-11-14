/* Platform-independent game loop; main.m forwards lifecycle, touches and frames. */
#ifndef APP_H
#define APP_H

/* data_dir holds c1.wpk, c1.wcl and fd3s.wpk. Returns 0 on success. */
/* Before app_init: a writable folder for the player's files (records, garage, the last time
 * zone, perf.log) when data_dir is read-only, as the app bundle of an .ipa install. */
void app_save_dir(const char *dir);
/* Before app_init on a screen wider than 4:3 (an iPhone's 3:2): the 1024x768 layout in its
 * middle, edge layout px more on each side; the race's touch keys key_k times as big. It also
 * turns on the iPhone's cuts. Touches then come in the layout's px (x from -edge). */
void app_wide(float edge, float key_k);
/* Before app_wide on a fast device (Android): no iPhone cuts though the screen is wide, the
 * maps drawn further. */
void app_console(void);
int app_init(const char *data_dir, int fb_width, int fb_height, float screen_w, float screen_h);
/* The drawing surface resized while running (Windows' Alt+Enter; app_wide again first). */
void app_resize(int fb_width, int fb_height);
/* The MORE page's ASPECT (v2.2): 0 the whole screen, 1 16:9, 2 4:3 (black bars round it). */
int app_aspect(void);
void app_touch(void *id, int phase, float x, float y);
void app_frame(float dt);
/* The iPad's turn about the screen's normal from level (radians, clockwise as the player
 * sees it positive), for steering by tilt (the OPTION screen's GYRO); main.m each frame. */
void app_tilt(float radians);
/* Steering by tilt is on (main.m reads the gravity only then). */
int app_wants_tilt(void);
/* A game controller or keyboard (on the game's thread, before app_frame): a key or button as
 * Android's KeyEvent code (iOS: its GCController buttons as those codes) going down or up, and
 * the axes in src/pad.h PAD_AXES' order (sticks -1..1, triggers 0..1, the hat -1, 0, 1). */
void app_pad_key(int code, int down);
void app_pad_axes(const float *v, int n);
/* The MORE page's: the refresh rate to ask the screen for (30, 60, 90, 120 Hz), and the 3D
 * scene's resolution in percent of the screen's (50..100), which the platforms size their
 * drawing surface by (read after app_init; it takes effect from the next start). */
int app_refresh_rate(void);
/* 1 once the MORE page's restart box was answered OK (the options saved): the platform starts
 * the app again (Android: MainActivity.restartApp; iOS: the app quits, to be opened again) */
int app_wants_restart(void);
int app_render_scale(void);
int app_renderer_choice_available(void); /* (v2.3) the MORE page's RENDERER shown */
int app_renderer_vulkan(void);           /* (v2.3) this build draws through Vulkan */
/* The start button (a tap on the attract movie): leaves the attract loop if it is up. */
void app_press_start(void);
/* The attract movie to show now (0 atruct, 1 playdemo, 2 tutorial; see main.m), or -1. */
int app_movie(void);
/* The movie shown for app_movie() ended, or its file is missing. */
void app_movie_done(void);
/* The menu or race BGM that should play: a file name in data_dir/sound/bgm (without
 * .m4a), or NULL for none (the attract loop has its own sound). */
const char *app_bgm(void);
/* (a name with a '/' or '\' is a full path: the player's own MP3 for the entry music, the MORE
 * page's ENTRY BGM; else a track of data_dir/sound/bgm, .m4a) */
/* The MORE page's CHOOSE MP3: the path the platform copies the player's chosen MP3 to (once per
 * tap; NULL none asked), then app_bgm_picked(1) once it is there, or (0, a note) */
const char *app_wants_bgm_pick(void);
void app_bgm_picked(int ok, const char *note);
/* the BGM's level over its own (1; the story ending's fade out): main.m and main_android.c */
float app_bgm_level(void);
/* Loading on a thread of its own, so the menus keep their frames: after app_init main.m
 * runs app_loader on a new thread with a GL context sharing the app's current there; it
 * never returns. Without it the menu frames load in their spare time. */
void app_loader(void);
/* main.m after each frame's presentRenderbuffer, with the time it waited there: the loading
 * thread runs till the next frame (app_loader) */
void app_frame_done(double present_ms);
/* The app goes to the background (1) or comes back (0): the loading thread stops while it
 * is away, where iOS allows no GL. */
void app_suspend(int away);
/* iOS is short of memory: free what can be loaded again. */
void app_memory_warning(void);
const char *app_error(void);

#endif
