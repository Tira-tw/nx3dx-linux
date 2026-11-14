/* Native Linux background music playback.
 *
 * The game data's soundtrack is AAC .m4a; players can also supply MP3s.
 * Decode via FFmpeg's command-line program (no shell, no embedded codecs).
 * A worker reads the PCM stream into a bounded ring buffer. The existing SDL
 * audio callback mixes it with snd_mix so there is only ONE audio device.
 *
 * This source runs only in the native Linux frontend.
 */
#include "bgm_linux.h"

#include <SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define BGM_RATE 44100
#define BGM_CHANNELS 2
#define BGM_CAP_SAMPLES (BGM_RATE * BGM_CHANNELS)
#define BGM_PATH_MAX 2048
#define BGM_READ_BYTES 8192

typedef struct {
  SDL_mutex *mutex;
  SDL_Thread *thread;
  int running;
  unsigned generation;
  float level;
  char path[BGM_PATH_MAX];
  int16_t samples[BGM_CAP_SAMPLES];
  size_t read_pos;
  size_t write_pos;
  size_t used;
} bgm_player;

typedef struct {
  pid_t pid;
  int fd;
} ffmpeg_pipe;

static bgm_player player;

static void pipe_close(ffmpeg_pipe *p) {
  if (p->fd >= 0) {
    close(p->fd);
    p->fd = -1;
  }
  if (p->pid > 0) {
    int status = 0;
    pid_t reaped = waitpid(p->pid, &status, WNOHANG);
    if (reaped == 0) {
      kill(p->pid, SIGTERM);
      do {
        reaped = waitpid(p->pid, &status, 0);
      } while (reaped < 0 && errno == EINTR);
    }
    p->pid = 0;
  }
}

/* ffmpeg writes s16le stereo @ 44.1 kHz to a pipe, looping the local file.
 * All arguments are passed directly to exec, never through a shell. */
static int pipe_open(ffmpeg_pipe *p, const char *path) {
  int fds[2];
  if (pipe(fds) != 0) {
    perror("linux BGM: pipe");
    return -1;
  }
  pid_t pid = fork();
  if (pid < 0) {
    perror("linux BGM: fork");
    close(fds[0]);
    close(fds[1]);
    return -1;
  }
  if (pid == 0) {
    close(fds[0]);
    if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(126);
    close(fds[1]);
    int nullfd = open("/dev/null", O_RDONLY);
    if (nullfd >= 0) {
      if (nullfd != STDIN_FILENO) {
        dup2(nullfd, STDIN_FILENO);
        close(nullfd);
      }
    }
    execlp("ffmpeg", "ffmpeg",
           "-nostdin", "-hide_banner", "-loglevel", "error",
           "-stream_loop", "-1", "-i", path,
           "-vn", "-sn", "-f", "s16le",
           "-ac", "2", "-ar", "44100", "pipe:1", (char *)NULL);
    perror("linux BGM: cannot execute ffmpeg");
    _exit(127);
  }
  close(fds[1]);
  int old = fcntl(fds[0], F_GETFL, 0);
  if (old >= 0) fcntl(fds[0], F_SETFL, old | O_NONBLOCK);
  p->pid = pid;
  p->fd = fds[0];
  return 0;
}

static int bgm_worker(void *arg) {
  (void)arg;
  ffmpeg_pipe proc = { .pid = 0, .fd = -1 };
  unsigned active_gen = 0;
  unsigned failed_gen = 0;
  int decoded_any = 0;
  union {
    int16_t aligned[BGM_READ_BYTES / 2 + 4];
    unsigned char bytes[BGM_READ_BYTES + 8];
  } incoming;
  size_t pending = 0;

  for (;;) {
    char desired[BGM_PATH_MAX];
    unsigned requested_gen;
    int running;
    size_t free_samples;

    SDL_LockMutex(player.mutex);
    running = player.running;
    requested_gen = player.generation;
    snprintf(desired, sizeof desired, "%s", player.path);
    free_samples = BGM_CAP_SAMPLES - player.used;
    SDL_UnlockMutex(player.mutex);
    if (!running) break;

    if (active_gen != requested_gen) {
      pipe_close(&proc);
      active_gen = requested_gen;
      decoded_any = 0;
      pending = 0;
    }
    if (!desired[0] || failed_gen == active_gen) {
      SDL_Delay(20);
      continue;
    }

    if (proc.fd < 0) {
      if (pipe_open(&proc, desired) != 0) {
        fprintf(stderr, "linux BGM: could not start decoder: %s\n", desired);
        failed_gen = active_gen;
        SDL_Delay(100);
        continue;
      }
      fprintf(stderr, "linux BGM: now playing %s\n", desired);
    }

    if (free_samples < (BGM_READ_BYTES / (int)sizeof(int16_t))) {
      SDL_Delay(8);
      continue;
    }

    struct pollfd pollfd = { .fd = proc.fd, .events = POLLIN | POLLHUP };
    int ready = poll(&pollfd, 1, 40);
    if (ready < 0 && errno == EINTR) continue;
    if (ready < 0) {
      perror("linux BGM: poll");
      pipe_close(&proc);
      failed_gen = active_gen;
      continue;
    }
    if (!ready) continue;
    ssize_t n = read(proc.fd, incoming.bytes + pending, BGM_READ_BYTES - pending);
    if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
    if (n <= 0) {
      /* -stream_loop normally prevents EOF; restarting also supports files
       * whose particular FFmpeg demuxer does not implement input looping. */
      if (!decoded_any) {
        fprintf(stderr, "linux BGM: decoder produced no audio: %s\n", desired);
        failed_gen = active_gen;
      }
      pipe_close(&proc);
      pending = 0;
      SDL_Delay(20);
      continue;
    }

    decoded_any = 1;
    size_t bytes = pending + (size_t)n;
    size_t whole_bytes = bytes & ~(size_t)3; /* whole stereo s16 frames */
    pending = bytes - whole_bytes;
    if (whole_bytes) {
      size_t count = whole_bytes / sizeof(int16_t);
      SDL_LockMutex(player.mutex);
      if (player.running && player.generation == active_gen &&
          count <= BGM_CAP_SAMPLES - player.used) {
        const int16_t *pcm = incoming.aligned;
        for (size_t i = 0; i < count; ++i) {
          player.samples[player.write_pos] = pcm[i];
          if (++player.write_pos == BGM_CAP_SAMPLES) player.write_pos = 0;
        }
        player.used += count;
      }
      SDL_UnlockMutex(player.mutex);
    }
    if (pending) memmove(incoming.bytes, incoming.bytes + whole_bytes, pending);
  }
  pipe_close(&proc);
  return 0;
}

int linux_bgm_init(void) {
  memset(&player, 0, sizeof player);
  player.mutex = SDL_CreateMutex();
  if (!player.mutex) {
    fprintf(stderr, "linux BGM: SDL_CreateMutex: %s\n", SDL_GetError());
    return -1;
  }
  player.running = 1;
  player.level = 1.0f;
  player.thread = SDL_CreateThread(bgm_worker, "nx3dx-bgm", NULL);
  if (!player.thread) {
    fprintf(stderr, "linux BGM: SDL_CreateThread: %s\n", SDL_GetError());
    SDL_DestroyMutex(player.mutex);
    player.mutex = NULL;
    return -1;
  }
  fprintf(stderr, "linux BGM: FFmpeg AAC/M4A/MP3 streaming enabled\n");
  return 0;
}

void linux_bgm_set_file(const char *path) {
  if (!player.mutex) return;
  if (!path) path = "";
  SDL_LockMutex(player.mutex);
  if (strcmp(player.path, path)) {
    snprintf(player.path, sizeof player.path, "%s", path);
    ++player.generation;
    player.read_pos = player.write_pos = player.used = 0;
    if (!path[0]) fprintf(stderr, "linux BGM: stopped\n");
  }
  SDL_UnlockMutex(player.mutex);
}

void linux_bgm_set_volume(float volume) {
  if (!player.mutex) return;
  if (!(volume >= 0.0f)) volume = 0.0f; /* also handles NaN */
  if (volume > 1.0f) volume = 1.0f;
  SDL_LockMutex(player.mutex);
  player.level = volume;
  SDL_UnlockMutex(player.mutex);
}

/* Called by the same SDL callback that fills the car/FX audio. */
void linux_bgm_mix(int16_t *stereo, int frames) {
  if (!player.mutex || frames <= 0) return;
  size_t count = (size_t)frames * BGM_CHANNELS;
  SDL_LockMutex(player.mutex);
  float volume = player.level;
  size_t n = count < player.used ? count : player.used;
  /* Even when muted, consume the stream to preserve music position. */
  for (size_t i = 0; i < n; ++i) {
    int16_t bgm = player.samples[player.read_pos];
    if (++player.read_pos == BGM_CAP_SAMPLES) player.read_pos = 0;
    if (volume > 0.0f) {
      int32_t summed = (int32_t)stereo[i] + (int32_t)((float)bgm * volume);
      stereo[i] = summed > 32767 ? 32767 : summed < -32768 ? -32768 : (int16_t)summed;
    }
  }
  player.used -= n;
  SDL_UnlockMutex(player.mutex);
}

void linux_bgm_shutdown(void) {
  if (!player.mutex) return;
  SDL_LockMutex(player.mutex);
  player.running = 0;
  ++player.generation;
  SDL_UnlockMutex(player.mutex);
  if (player.thread) SDL_WaitThread(player.thread, NULL);
  SDL_DestroyMutex(player.mutex);
  player.mutex = NULL;
  player.thread = NULL;
}
