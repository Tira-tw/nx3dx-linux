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
