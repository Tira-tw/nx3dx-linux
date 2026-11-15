#!/usr/bin/env bash
# NX3DX+ main-repository Linux x86_64 SDL2/GLES2 experimental native build.
# Uses only files checked into this branch; no private archive import step.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
if [[ "$(uname -m)" != "x86_64" ]]; then
  echo "ERROR: This first Linux native target requires x86_64." >&2
  exit 1
fi
for cmd in gcc pkg-config; do
  command -v "$cmd" >/dev/null 2>&1 || { echo "ERROR: missing $cmd" >&2; exit 1; }
done
for pkg in sdl2 glesv2 egl zlib; do
  pkg-config --exists "$pkg" || {
    echo "ERROR: missing $pkg development package." >&2
    echo "Ubuntu/Distrobox: sudo apt install build-essential pkg-config libsdl2-dev libgles2-mesa-dev libegl1-mesa-dev zlib1g-dev" >&2
    exit 1
  }
done
for required in src/phys/phys_rc.c src/cardb.h src/meterdb.h src/sounddb.h \
                src/phys/rc_runtime.c src/gl.h src/app.h linux/main_linux.c \
                linux/bgm_linux.c linux/bgm_linux.h linux/file_picker.c linux/file_picker.h \
                linux/version.h linux/RELEASE_NOTES_v2.4.1.md linux/package_release.sh; do
  [[ -s "$required" ]] || { echo "ERROR: missing $required" >&2; exit 1; }
done
echo "[Linux main GLES2] source preflight OK; no private header ZIP needed"
if ! command -v zenity >/dev/null 2>&1 && ! command -v kdialog >/dev/null 2>&1; then
  echo "[Linux main GLES2] WARNING: no Linux GUI file picker installed; CHOOSE MP3 requires zenity or kdialog" >&2
  echo "[Linux main GLES2] Install it inside Distrobox: sudo apt install zenity" >&2
fi
if ! command -v ffmpeg >/dev/null 2>&1; then
  echo "[Linux main GLES2] WARNING: ffmpeg runtime missing; soundtrack playback will be silent" >&2
  echo "[Linux main GLES2] Install it inside Distrobox: sudo apt install ffmpeg" >&2
fi
if [[ "${1:-}" == "--check" ]]; then
  echo "[Linux main GLES2] toolchain and checked-in inputs found"
  exit 0
fi
if [[ $# -ne 0 ]]; then
  echo "Usage: bash linux/build_linux.sh [--check]" >&2
  exit 2
fi

OUT="$ROOT/build/linux-main"
OBJ="$OUT/obj"
mkdir -p "$OBJ"
read -r -a PKG_CFLAGS <<< "$(pkg-config --cflags sdl2 glesv2 egl zlib)"
read -r -a PKG_LIBS <<< "$(pkg-config --libs sdl2 glesv2 egl zlib)"
COMMON=(-std=gnu11 -DRC_FPT=double -DWMMT_EGL=1 -DWMMT_LINUX_NATIVE=1 -D_GNU_SOURCE=1
        -Isrc -Isrc/phys -fno-strict-aliasing
        -Wall -Wno-unused-function -Wno-unused-variable
        -Wno-unused-but-set-variable -Wno-incompatible-pointer-types
        -Wno-pointer-sign -Wno-int-conversion
        "${PKG_CFLAGS[@]}")
# The generated physics is much larger than ordinary sources. Reuse its object
# if the generated file and rc_runtime.h have not changed.
PHYS_OBJ="$OBJ/phys_rc.o"
if [[ ! -s "$PHYS_OBJ" || src/phys/phys_rc.c -nt "$PHYS_OBJ" || src/phys/rc_runtime.h -nt "$PHYS_OBJ" ]]; then
  echo "[Linux main GLES2] compiling generated physics"
  gcc -O1 -ffp-contract=off -w -DRC_FPT=double -Isrc/phys \
      -c src/phys/phys_rc.c -o "$PHYS_OBJ"
fi
OBJECTS=("$PHYS_OBJ")
SOURCES=(
  src/phys/rc_runtime.c
  src/app.c src/render.c src/hud.c src/car.c src/collide.c
  src/sound.c src/anm.c src/menu.c src/lights.c src/spark.c
  src/net.c src/vs_host.c src/ghostx.c src/title.c src/target.c
  src/pad.c src/gk.c src/relays.c src/vroom.c
  linux/main_linux.c linux/bgm_linux.c linux/file_picker.c
)
for src in "${SOURCES[@]}"; do
  obj="$OBJ/${src//\//_}.o"
  echo "[Linux main GLES2] compiling $src"
  gcc "${COMMON[@]}" -O2 -c "$src" -o "$obj"
  OBJECTS+=("$obj")
done
echo "[Linux main GLES2] linking"
gcc -o "$OUT/NX3DX" "${OBJECTS[@]}" "${PKG_LIBS[@]}" -lm -ldl -pthread
chmod +x "$OUT/NX3DX"
echo "SUCCESS: $OUT/NX3DX"
"$OUT/NX3DX" --version
echo "Release packaging: bash linux/package_release.sh"
echo "Next: bash linux/run_linux.sh /path/to/your/existing/data --window"
echo "BGM uses the system ffmpeg executable to play local AAC/M4A and MP3 files."
echo "CHOOSE MP3 uses zenity or kdialog (install one in Distrobox)."
echo "Attract movies remain unimplemented in this prototype."
