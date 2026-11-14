#!/usr/bin/env bash
# Export Linux-only source into a NEW directory for a standalone GitHub repo.
# Never copies data/, game assets, saves, build products or Git history.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
DEST="${1:-$HOME/NX3DX-Linux-Only}"
case "$DEST" in /*) ;; *) DEST="$PWD/$DEST" ;; esac
DEST="$(realpath -m "$DEST")"
case "$DEST/" in "$ROOT/"*) echo "ERROR: export directory may not be inside source checkout" >&2; exit 2;; esac
if [[ -e "$DEST" || -L "$DEST" ]]; then
  echo "ERROR: export destination already exists: $DEST" >&2
  exit 2
fi
mkdir -p "$DEST/.github/workflows"
cp -a "$ROOT/src" "$DEST/src"
cp -a "$ROOT/linux" "$DEST/linux"
cp "$ROOT/LICENSE" "$DEST/LICENSE"

# Keep the shared engine, generated Linux physics and headers. Omit all other
# platforms' launchers and Vulkan-only implementation.
rm -f "$DEST/src/main.m" "$DEST/src/main_android.c" \
  "$DEST/src/main_windows.c" "$DEST/src/gk_ios.m" \
  "$DEST/src/sound_ios.m" "$DEST/src/sound_ios.h" \
  "$DEST/src/vkgl.c" "$DEST/src/vkgl.h" "$DEST/src/vkgl_spv.c"

cat > "$DEST/README.md" <<'EOF'
# NX3DX+ Linux

**NX3DX+ Linux v2.4.1** — native Linux x86_64 SDL2 / OpenGL ES 2.0,
including Steam Deck and Ubuntu Distrobox.

Only Linux platform entrypoints and the shared C engine required to
compile are included. No Android, iOS, Windows, macOS or Vulkan targets.

## Build

    sudo apt update
    sudo apt install -y build-essential pkg-config libsdl2-dev libgles2-mesa-dev libegl1-mesa-dev zlib1g-dev ffmpeg zenity
    bash linux/build_linux.sh
    ./build/linux-main/NX3DX --version

## Run with your authorized compatible game data

    bash linux/run_linux.sh "$HOME/NX3DX-Linux/data" --fullscreen

## Build an archive without game assets

    bash linux/package_release.sh

Creates build/release/NX3DX-Linux-v2.4.1-x86_64.tar.gz and its SHA256.

## Origin, license and limitations

Ported from https://github.com/Tira-tw/nx3dx/tree/release/linux-v2.4.1
Source is under the included MIT license, with original attribution to
Allannn-sudo. The MIT license does not include protected arcade assets,
music, games or trademarks.

The source repository does not contain copyrighted data, music or saves.
See linux/README.md and linux/RELEASE_NOTES_v2.4.1.md for details and
limitations (including missing attract/tutorial video playback).
EOF

cat > "$DEST/.gitignore" <<'EOF'
/data/
/build/
/local_assets/
/extra/
*.bin
*.wpk
*.mp3
*.m4a
*.wav
*.mp4
*.mov
*.zip
*.tar.gz
*.ipa
*.apk
.env
.env.*
*.log
*.part
__pycache__/
.vscode/
.idea/
EOF

cat > "$DEST/.gitattributes" <<'EOF'
*.sh text eol=lf
*.c text eol=lf
*.h text eol=lf
EOF

cat > "$DEST/.github/workflows/linux-build.yml" <<'EOF'
name: Build Linux v2.4.1
on:
  push:
    branches: [main]
  pull_request:
  workflow_dispatch:
permissions:
  contents: read
jobs:
  build:
    runs-on: ubuntu-22.04
    timeout-minutes: 60
    steps:
      - uses: actions/checkout@v4
      - name: Dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y build-essential pkg-config libsdl2-dev libgles2-mesa-dev libegl1-mesa-dev zlib1g-dev ffmpeg zenity
      - name: Linux source and build
        run: |
          bash linux/build_linux.sh --check
          bash linux/build_linux.sh
      - name: Package Linux binary
        run: |
          bash linux/package_release.sh
          cd build/release
          sha256sum -c NX3DX-Linux-v2.4.1-x86_64.tar.gz.sha256
      - uses: actions/upload-artifact@v4
        with:
          name: nx3dx-linux-v2.4.1-x86_64
          path: |
            build/release/NX3DX-Linux-v2.4.1-x86_64.tar.gz
            build/release/NX3DX-Linux-v2.4.1-x86_64.tar.gz.sha256
EOF

for f in \
  src/phys/phys_rc.c src/phys/rc_runtime.c src/app.c src/menu.c \
  src/render.c src/cardb.h src/meterdb.h src/sounddb.h \
  linux/build_linux.sh linux/main_linux.c linux/version.h LICENSE; do
  if [[ ! -s "$DEST/$f" ]]; then
    echo "ERROR: Linux required file missing: $f" >&2
    exit 1
  fi
done
echo "Linux-only repository files created at $DEST"
echo "No .git history, data/, saves, or binaries were exported."
echo "Run git init -b main, git add ., git commit, then push to your new repo."
