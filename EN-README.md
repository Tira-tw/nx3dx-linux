<div align="center">

<img width="680" alt="image" src="https://github.com/user-attachments/assets/01fb6a63-58f1-4be5-b3d2-59d5d013e417" />

# NX3DX+ Linux

**Linux v2.4.1 · Native x86_64 · SDL2 · OpenGL ES**

**[繁體中文](README.md) | [English](EN-README.md)**

![Linux](https://img.shields.io/badge/Linux-x86__64-1793D1?logo=linux&logoColor=white)
![OpenGL ES](https://img.shields.io/badge/Graphics-OpenGL%20ES%202.0-0b86c4)
![Steam Deck](https://img.shields.io/badge/Steam%20Deck-Tested-1b9e77)
![Version](https://img.shields.io/badge/Version-2.4.1-cc2e9a)

</div>

> **NX3DX+ Linux is an unofficial Linux port developed from the Re3DX+ codebase.** This repository focuses on Linux and Steam Deck. No original game assets are included.

## 📖 Overview

NX3DX+ Linux is a **native Linux x86_64** port built with **SDL2 and OpenGL ES through EGL**. It does **not** require Wine, Proton, or Vulkan. This repository contains the Linux frontend and the shared C engine required to build it, not launchers for Windows, Android, iOS, or macOS.

**Current features**
- Native Linux windowed/fullscreen execution and Steam Deck controller support
- Keyboard, mouse, and simultaneous multi-finger touchscreen input
- Engine and collision sound effects; MP3/M4A soundtrack playback via FFmpeg
- Custom BGM selection using Zenity or KDialog
- Graphics settings for reflection, sharpening, ambient occlusion, rear-view mirror, and supported HQ effects
- Separate per-user saved games, without overwriting existing game installations

## 🖥️ Requirements

| Component | Requirement |
|---|---|
| Platform | 64-bit Linux x86_64; Ubuntu 22.04+ recommended |
| Steam Deck | Ubuntu Distrobox supported |
| Graphics | GPU and drivers supporting OpenGL ES 2.0 with EGL |
| Libraries | SDL2, GLES2, EGL, zlib |
| Music decoder | FFmpeg |
| Custom BGM file dialog | Zenity or KDialog |
| Game data | Your own lawfully obtained, compatible 'data/' directory |

> ⚠️ This repository **does not distribute** ROMs, original game files (BIN/WPK), music, videos, protected assets, or user saves.

## 📦 Installation A — Prebuilt Linux package

When available, download 'NX3DX-Linux-v2.4.1-x86_64.tar.gz' from [GitHub Releases](https://github.com/Tira-tw/nx3dx-linux/releases).

Install runtime dependencies in Ubuntu or your Steam Deck's Ubuntu Distrobox:

~~~bash
sudo apt update
sudo apt install -y libsdl2-2.0-0 libgles2 libegl1 zlib1g ffmpeg zenity
~~~

Extract the archive and provide the path to your own local game data:

~~~bash
tar -xzf NX3DX-Linux-v2.4.1-x86_64.tar.gz
cd NX3DX-Linux-v2.4.1-x86_64
./NX3DX --version
./launch.sh "$HOME/NX3DX-Linux/data" --fullscreen
~~~

If a prebuilt release has not yet been uploaded, compile from source instead.

## 🛠️ Installation B — Build from source

### 1. Install build dependencies

Run these commands **inside Ubuntu or the Ubuntu Distrobox on Steam Deck**:

~~~bash
sudo apt update
sudo apt install -y git build-essential pkg-config \
  libsdl2-dev libgles2-mesa-dev libegl1-mesa-dev zlib1g-dev \
  ffmpeg zenity
~~~

### 2. Clone this Linux-only repository

~~~bash
git clone https://github.com/Tira-tw/nx3dx-linux.git
cd nx3dx-linux
~~~

### 3. Build and verify v2.4.1

~~~bash
bash linux/build_linux.sh --check
bash linux/build_linux.sh
./build/linux-main/NX3DX --version
~~~

Expected: **NX3DX+ Linux v2.4.1 (x86_64, OpenGL ES 2.0)**.

### 4. Launch the game

~~~bash
bash linux/run_linux.sh "$HOME/NX3DX-Linux/data" --window
~~~

Replace the path with your own authorized game data directory. A compatible 'menu.bin' file is required, along with the rest of the required game assets. Use '--fullscreen' instead of '--window' for fullscreen mode.

### 5. Build a distribution archive

~~~bash
bash linux/package_release.sh
~~~

Outputs under 'build/release/' include the x86_64 binary package and its SHA256 checksum. **No copyrighted game assets or save files are included.**

## 🎮 Steam Deck

Enter your Ubuntu Distrobox first, then run the build or launch commands above. If your prompt already indicates the Distrobox, do not enter it again.

Multi-touch input supports holding steering and acceleration with separate fingers. User settings and saves are stored in SDL's per-user preference directory, not in the game's original data folder.

## 🎨 GPU Settings & known limitations

- **Working engine options:** Reflection, Sharpen, AO, Back Mirror.
- **Driver-dependent HQ options:** Bloom, SSAO, MSAA, wet/paint reflections, ASTC HD textures.
- The Linux backend currently supports **OpenGL ES only**, not Vulkan.
- The MORE menu's internal resolution percentage is not fully connected to the Linux render target yet.
- Attract/tutorial video playback and embedded movie audio are not implemented yet.

**NVIDIA GeForce 940MX compatibility:** Some NVIDIA PRIME/driver environments crash while loading car models with HQ rendering enabled. A tester successfully reached vehicle selection with this workaround:

~~~bash
WMMT_NO_HQ=1 prime-run bash linux/run_linux.sh \
  "$HOME/NX3DX-Linux/data" --window
~~~

Only use 'prime-run' on systems where that utility is installed; omit it otherwise. The exact HQ rendering failure is still under investigation. This does **not** mean all NVIDIA GPUs are affected.

## 🐛 Bug reports

Use [GitHub Issues](https://github.com/Tira-tw/nx3dx-linux/issues). Include your Linux distribution, GPU, driver version, launch command, and relevant terminal errors. **Never upload game data, personal saves, credentials, or secrets.**

## ❤️ Credits, origin & licensing

- **Port origin:** Developed from the Re3DX+ codebase and continued through the [NX3DX+ project](https://github.com/Tira-tw/nx3dx).
- **Original source attribution:** Allannn-sudo (高橋涼介のFC). The original copyright notice is preserved in the [MIT License](LICENSE).
- **Linux maintenance:** NX3DX+ Project / [Tira-tw](https://github.com/Tira-tw), together with Linux testers and contributors.
- The MIT license applies only to software and tools the repository is authorized to license. It **does not license** the underlying commercial game, its copyrighted content, soundtrack, art, or trademarks.

This is an unofficial community project, **not affiliated with Bandai Namco Entertainment or other game rights holders**.

---

<div align="center">

**NX3DX+ PROJECT EDITION — LINUX v2.4.1**

[繁體中文 README](README.md) · [Detailed Linux guide](linux/README.md) · [Release notes](linux/RELEASE_NOTES_v2.4.1.md)

</div>
