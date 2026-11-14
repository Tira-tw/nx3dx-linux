#ifndef NX3DX_LINUX_BGM_H
#define NX3DX_LINUX_BGM_H
#include <stdint.h>

/* Native Linux soundtrack decoder: FFmpeg CLI -> one SDL audio mix stream.
 * These calls do not change the engine/FX audio mixer. */
int linux_bgm_init(void);
void linux_bgm_set_file(const char *path);
void linux_bgm_set_volume(float volume);
void linux_bgm_mix(int16_t *stereo, int frames);
void linux_bgm_shutdown(void);

#endif
