# NX3DX+ Linux v2.4.1

**Platform:** Linux x86_64, native SDL2 + OpenGL ES 2.0 (Mesa/Steam Deck).
**Version:** 2.4.1
**Source:** Tira-tw/nx3dx, release/linux-v2.4.1

## Release overview

Linux v2.4.1 builds directly from the original nx3dx source repository,
without requiring the private generated header archive used by the older
nx3dx-mac-based Linux experiments.

Steam Deck testing by the project owner has confirmed that the major
playability fixes are working: native graphics, multitouch controls,
soundtrack/BGM support, collision sound and custom BGM selection.

Features:
- Native GLES2 rendering (not Wine, Proton or Vulkan)
- SDL2 keyboard, mouse, Steam Deck controller, and simultaneous touch fingers
- Game and engine sound effects, including native Linux impact fallback
- AAC/M4A and MP3 background music via FFmpeg, auto-switching tracks
- CHOOSE MP3 dialog via Zenity/KDialog, isolated per-user save location
- Window/fullscreen and software FPS limit
- Existing graphics options, subject to supported GPU capabilities

## Known limitations

- Attract/tutorial video playback and its embedded audio are still not
  implemented in this Linux frontend.
- The `RENDERER` menu does not offer Vulkan on this build (OpenGL ES only).
- The graphics OPTION menu stays useful: reflection, sharpen, baked AO and
  back mirror are controlled by the game. The optional HQ rows for bloom,
  SSAO, wet/paint reflections and MSAA become available *only if*
  `render_hq()` successfully initializes against the active OpenGL driver.
- HD textures require ASTC support and matching private texture packs.
- The MORE -> DISPLAY -> RESOLUTION percentage is saved, but this Linux
  frontend has not implemented resizing its internal render target based
  on that value. Do not expect it to lower internal GPU resolution yet.
- V-Sync is enabled by default, so 90/120 fps and UNLOCK cannot exceed
  the refresh cap unless the driver/display allow it; `NX3DX_VSYNC=0`
  can disable the requested swap interval when troubleshooting.
- SteamOS/Distrobox GPU/SDL drivers and game assets can vary across
  installations. Final build and gameplay validation remains device-specific.

## Runtime requirements inside Ubuntu Distrobox

```bash
sudo apt update
sudo apt install -y libsdl2-2.0-0 libgles2 libegl1 zlib1g ffmpeg zenity
```

Compiler packages are required only to build the executable from source.

### Use the binary-only archive

The v2.4.1 archive intentionally **does not include** copyrighted game
assets, any `data/` directory or save files. After extracting it:

```bash
cd NX3DX-Linux-v2.4.1-x86_64
./NX3DX --version
./launch.sh "$HOME/NX3DX-Linux/data" --window
```

Use `--fullscreen` instead of `--window` to take over the screen.
`launch.sh` reads the already existing, locally authorized game data.
Linux-Reborn saves remain in the user's SDL preference directory and
are not bundled.

### Build and package from source

```bash
git clone -b release/linux-v2.4.1 --single-branch \
  https://github.com/Tira-tw/nx3dx.git NX3DX-Linux-v2.4.1-source
cd NX3DX-Linux-v2.4.1-source
sudo apt install -y build-essential pkg-config libsdl2-dev \
  libgles2-mesa-dev libegl1-mesa-dev zlib1g-dev ffmpeg zenity
bash linux/build_linux.sh
bash linux/package_release.sh
```

Results:
- `build/release/NX3DX-Linux-v2.4.1-x86_64.tar.gz`
- `build/release/NX3DX-Linux-v2.4.1-x86_64.tar.gz.sha256`

Verify the archive before distributing:
`cd build/release && sha256sum -c NX3DX-Linux-v2.4.1-x86_64.tar.gz.sha256`.

The archive contains the compiled binary, executable launcher and
README, not a full game installer. Only distribute assets that you have
the rights and authorization to distribute.
