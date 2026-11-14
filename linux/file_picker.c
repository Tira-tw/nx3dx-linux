/* Desktop Linux: native "CHOOSE MP3" dialog.
 * Use zenity (GTK) or kdialog (KDE); no SDL3, GUI framework, shell command or
 * extra link-time dependency required. Both run inside the current Distrobox.
 */
#include "file_picker.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#define PICK_PATH_MAX 4096

/* Spawn picker and capture its selected filename. Return -2 if not installed,
 * -1 on error, 0 if cancelled, 1 if a filename was chosen. */
static int run_picker(const char *name, char *const args[], char *out, size_t size) {
  int fds[2];
  if (pipe(fds) != 0) {
    fprintf(stderr, "linux picker: pipe failed: %s\n", strerror(errno));
    return -1;
  }
  posix_spawn_file_actions_t actions;
  int err = posix_spawn_file_actions_init(&actions);
  if (err != 0) {
    close(fds[0]);
    close(fds[1]);
    return -1;
  }
  posix_spawn_file_actions_addclose(&actions, fds[0]);
  posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
  posix_spawn_file_actions_addclose(&actions, fds[1]);
  pid_t pid = -1;
  err = posix_spawnp(&pid, name, &actions, NULL, args, environ);
  posix_spawn_file_actions_destroy(&actions);
  close(fds[1]);
  if (err != 0) {
    close(fds[0]);
    if (err != ENOENT) fprintf(stderr, "linux picker: %s: %s\n", name, strerror(err));
    return err == ENOENT ? -2 : -1;
  }

  size_t len = 0;
  int truncated = 0;
  for (;;) {
    char chunk[512];
    ssize_t got = read(fds[0], chunk, sizeof chunk);
    if (got < 0 && errno == EINTR) continue;
    if (got <= 0) break;
    for (ssize_t i = 0; i < got; ++i) {
      if (len + 1 < size) out[len++] = chunk[i];
      else truncated = 1;
    }
  }
  close(fds[0]);
  int status = 0;
  pid_t waited;
  do {
    waited = waitpid(pid, &status, 0);
  } while (waited < 0 && errno == EINTR);
  if (waited < 0 || truncated) {
    fprintf(stderr, "linux picker: failed to read selected filename\n");
    return -1;
  }
  out[len] = 0;
  while (len && (out[len - 1] == '\n' || out[len - 1] == '\r')) out[--len] = 0;
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || !len) {
    /* Exit status 1 with no filename is normal for Cancel. */
    return 0;
  }
  return 1;
}

static int supported_audio(const char *name) {
  const char *dot = strrchr(name, '.');
  return dot && (!strcasecmp(dot, ".mp3") || !strcasecmp(dot, ".m4a") ||
                 !strcasecmp(dot, ".wav") || !strcasecmp(dot, ".aac") ||
                 !strcasecmp(dot, ".ogg") || !strcasecmp(dot, ".flac"));
}

static int copy_music(const char *source, const char *dest) {
  struct stat st;
  if (stat(source, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) {
    fprintf(stderr, "linux picker: selected audio file is missing, empty or not regular\n");
    return -1;
  }
  if (!supported_audio(source)) {
    fprintf(stderr, "linux picker: unsupported file extension (select an audio file): %s\n", source);
    return -1;
  }
  FILE *in = fopen(source, "rb");
  if (!in) {
    fprintf(stderr, "linux picker: cannot read %s: %s\n", source, strerror(errno));
    return -1;
  }
  char temp[PICK_PATH_MAX];
  int n = snprintf(temp, sizeof temp, "%s.tmp-XXXXXX", dest);
  if (n <= 0 || n >= (int)sizeof temp) {
    fclose(in);
    return -1;
  }
  int fd = mkstemp(temp);
  if (fd < 0) {
    fprintf(stderr, "linux picker: cannot create save file: %s\n", strerror(errno));
    fclose(in);
    return -1;
  }
  unsigned char buffer[65536];
  int ok = 1;
  size_t got;
  while ((got = fread(buffer, 1, sizeof buffer, in)) != 0) {
    size_t off = 0;
    while (off < got) {
      ssize_t wrote = write(fd, buffer + off, got - off);
      if (wrote < 0 && errno == EINTR) continue;
      if (wrote <= 0) {
        ok = 0;
        break;
      }
      off += (size_t)wrote;
    }
    if (!ok) break;
  }
  if (ferror(in)) ok = 0;
  fclose(in);
  if (close(fd) != 0) ok = 0;
  if (!ok || rename(temp, dest) != 0) {
    fprintf(stderr, "linux picker: failed to copy MP3 into save directory: %s\n", strerror(errno));
    unlink(temp);
    return -1;
  }
  fprintf(stderr, "linux picker: custom music saved to the Linux-Reborn save folder\n");
  return 0;
}

int linux_pick_bgm(const char *destination) {
  if (!destination || !*destination) return -1;
  char selected[PICK_PATH_MAX] = {0};
  char *zenity[] = {
    "zenity", "--file-selection", "--title=NX3DX+ - Choose custom music",
    "--file-filter=Audio files | *.mp3 *.m4a *.wav *.aac *.ogg *.flac",
    "--file-filter=All files | *", NULL
  };
  int result = run_picker("zenity", zenity, selected, sizeof selected);
  if (result == -2) {
    const char *home = getenv("HOME");
    char *kdialog[] = {
      "kdialog", "--getopenfilename", (char *)(home ? home : "."),
      "Audio files (*.mp3 *.m4a *.wav *.aac *.ogg *.flac)", NULL
    };
    result = run_picker("kdialog", kdialog, selected, sizeof selected);
  }
  if (result == -2) {
    fprintf(stderr, "linux picker: no GUI file picker found: sudo apt install zenity\n");
    return -1;
  }
  if (result != 1) {
    if (result < 0) fprintf(stderr, "linux picker: file dialog failed\n");
    return result;
  }
  return copy_music(selected, destination) == 0 ? 1 : -1;
}
