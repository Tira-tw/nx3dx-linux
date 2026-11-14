# NX3DX+ Linux v2.4.1 (from Tira-tw/nx3dx)

**Status: Linux release source; Steam Deck community gameplay-tested.**

For the v2.4.1 binary-only package, release details and GPU settings
compatibility, see [RELEASE_NOTES_v2.4.1.md](RELEASE_NOTES_v2.4.1.md).

This port was developed in the original NX3DX+ source repository on
`feature/linux-native-gles2` and is prepared as the
`release/linux-v2.4.1` source branch. It does **not** require the generated header
ZIP or physics export from the unrelated `nx3dx-mac` checkout: this
repository already tracks its own generated `src/phys/phys_rc.c`,
`src/cardb.h`, `src/meterdb.h`, and `src/sounddb.h`.

Its Linux frontend is native, not a Windows binary, Wine or Proton.
The previous development build was tested on Steam Deck by the project
owner, including graphics, multi-touch, collisions and BGM. The v2.4.1
version-stamped binary should be rebuilt and checked separately.

## Work directly on the friend's Steam Deck

Run these inside the existing Ubuntu `nx3dx-jammy` Distrobox
(`deck@nx3dx-jammy` means you are inside it). Do not run `distrobox enter`
a second time.

```bash
sudo apt update
sudo apt install -y git build-essential pkg-config libsdl2-dev libgles2-mesa-dev libegl1-mesa-dev zlib1g-dev ffmpeg zenity

cd ~
git clone --branch feature/linux-native-gles2 --single-branch \
  https://github.com/Tira-tw/nx3dx.git NX3DX-Linux-Reborn
cd ~/NX3DX-Linux-Reborn

bash linux/build_linux.sh --check
bash linux/build_linux.sh 2>&1 | tee ~/nx3dx-reborn-build.log
```

If this directory already exists, use `cd ~/NX3DX-Linux-Reborn` and
`git pull --ff-only` rather than cloning again.

### Run without overwriting the old game

Use the *previously installed* authorized game data, read in place:

```bash
cd ~/NX3DX-Linux-Reborn
bash linux/run_linux.sh "$HOME/NX3DX-Linux/data" --window \
  2>&1 | tee ~/nx3dx-reborn-test.log
```

Alternatively, use `--fullscreen` for Steam Deck's handheld display.
Keyboard: W/A/S/D, Enter, Backspace, Escape, Shift/Ctrl. The first
prototype also maps SDL game controllers / Steam Input to the game's
Android-compatible pad inputs.

**Isolation and data:** The old `~/NX3DX-Linux` install is not modified.
The new binary is `~/NX3DX-Linux-Reborn/build/linux-main/NX3DX`.
Saves use SDL's private per-user preference directory for
`NX3DX / Linux-Reborn` rather than the old installation's saves. No
game assets or user data should be committed or uploaded to GitHub.

### Current limitations / expected follow-ups

- The native renderer uses Mesa-compatible **OpenGL ES 2.0 through SDL2**,
  not Vulkan and not the old nx3dx-mac Linux port. GPU acceleration and
  missing GL extensions must be verified from the launch log.
- SDL audio output invokes the core's `snd_mix` for car engine/effects and
  mixes a separate FFmpeg-decoded AAC/M4A or MP3 music stream through that
  same output device. FFmpeg must be installed **inside the Distrobox**;
  soundtrack playback is still awaiting real-device confirmation.
- Attract/tutorial movies are currently skipped to let the UI proceed.
  Full video/media playback will be integrated after the core build starts.
- A native file picker for custom MP3 is implemented via zenity/kdialog (install one).
- GPU SETTINGS remain useful: Reflection, Sharpen, AO and Back Mirror
  are connected to active engine code. Bloom, SSAO, AA, HD textures and
  wet/paint effects require successful HQ graphics initialization and/or
  relevant OpenGL extensions. The Linux launcher still ignores the MORE
  page's internal RESOLUTION scaling percentage.
- V-Sync and controller bindings may vary across desktop environments;
  additional devices need compatibility testing.
- Source and format mismatch may still exist between data produced for
  the older macOS-derived test port and this newer original repository.
  If `app_init` rejects a pack, do not edit raw binary headers to bypass
  the checks; inspect the exact error and regenerate only authorized
  compatible assets.

### Debugging

If compiling fails, share the lines surrounding the **first** error and:

```bash
tail -n 90 ~/nx3dx-reborn-build.log
```

If the binary runs and fails, share its first error and:

```bash
grep -Ei 'linux:|error|failed|gles|shader|render' ~/nx3dx-reborn-test.log | tail -n 90
```

Do **not** share `data/`, `menu.bin`, exported arcade assets, save data,
GitHub authentication tokens, or private archive files in a bug report.


## Steam Deck touchscreen multi-touch (steering + throttle)

The initial Linux SDL shell processed touchscreen presses through the
single emulated mouse ID, so it could not keep steering and the throttle
held independently. The Linux frontend now handles native
`SDL_FINGERDOWN`, `SDL_FINGERMOTION` and `SDL_FINGERUP` events with
independent finger IDs (up to 16 live fingers) and ignores SDL's duplicate
touch-emulated mouse clicks. Touches are released on focus loss.

Rebuild after pulling the latest `feature/linux-native-gles2`:

```bash
cd ~/NX3DX-Linux-Reborn
git pull --ff-only
bash linux/build_linux.sh
NX3DX_TOUCH_DEBUG=1 bash linux/run_linux.sh "$HOME/NX3DX-Linux/data" --window \
  2>&1 | tee ~/nx3dx-touch-test.log
```

While touching steering and throttle with two fingers, the terminal should
show **different slots**, e.g. `finger DOWN in slot 1` and
`finger DOWN in slot 2`, followed by their UP lines.

If only mouse clicks are received and **no** native finger DOWN messages
are logged, the desktop environment or SDL/Distrobox input layer may be
converting the physical touchscreen to single-pointer events. That must be
resolved on the host/SDL side; the C event handler cannot synthesize a
second independent finger from one mouse pointer.


## Soundtrack/BGM support (Steam Deck)

The Linux frontend now calls `app_bgm()` to select the menu/race track,
honours `app_bgm_level()`, and loops the song. The original soundtracks
live under the locally authorized `data/sound/bgm/*.m4a`; custom BGM
tracks can also be `*.mp3`. This uses an external `ffmpeg` executable
for decoding and mixes with engine sound through one SDL audio device.

Inside the Steam Deck's **Ubuntu Distrobox** terminal:

```bash
sudo apt update
sudo apt install -y ffmpeg

cd ~/NX3DX-Linux-Reborn
git pull --ff-only
bash linux/build_linux.sh

bash linux/run_linux.sh "$HOME/NX3DX-Linux/data" --window \
  2>&1 | tee ~/nx3dx-bgm-test.log
```

Before launching, check that a soundtrack is present:

```bash
find "$HOME/NX3DX-Linux/data/sound/bgm" -maxdepth 1 -type f \
  \( -iname '*.m4a' -o -iname '*.mp3' \) | head
```

Expected runtime messages:

- `linux BGM: FFmpeg AAC/M4A/MP3 streaming enabled` — the mixer started
- `linux BGM: now playing /.../sound/bgm/NAME.m4a` — a track was found
- `linux BGM: missing track` — a selected song file isn't present
- `linux BGM: cannot execute ffmpeg` — install `ffmpeg` in Distrobox
- `linux BGM: decoder produced no audio` — inspect FFmpeg error in log

The game must be past the attract/movie-only screens for `app_bgm()` to
select menu tracks; video/audio embedded in attract movies is **not**
implemented yet. Do not upload copyrighted music or arcade data to GitHub.


## Steam Deck: collision audio and CHOOSE MP3 (Linux-native)

The Linux branch now addresses two issues reported during real-device testing:

- **Collision audio.** `src/sound.c` now reports whether local
  `wall.wav` and `crash1.wav` through `crash6.wav` are loaded. On
  Linux only, when the recompiled arcade doesn't emit a crash request,
  the already-calculated car impact can trigger an authentic loaded crash
  WAV with a brief cooldown; wall scrapes can use the already-calculated
  wall-hit amount when the arcade loop gain is absent. When a requested
  crash WAV is missing but another supplied crash WAV exists, the Linux
  mixer falls back to that other sample. It cannot synthesize or supply
  copyrighted WAV files not present in local authorized game data.
- **BGM SETTINGS → CHOOSE MP3.** This now opens a native desktop file
  picker via `zenity` (or `kdialog`, if installed), copies the selected
  MP3/M4A/WAV/etc. atomically into the Linux-Reborn **save directory**
  and activates that choice. RESET switches back to the original track.
  The frontend must be running with access to the desktop's graphical
  session. The chooser is not a BGM volume slider.

Install the picker in the existing Ubuntu `nx3dx-jammy` Distrobox;
do not install it on SteamOS's read-only host:

```bash
sudo apt update
sudo apt install -y zenity

cd ~/NX3DX-Linux-Reborn
git pull --ff-only
bash linux/build_linux.sh

NX3DX_SOUND_DEBUG=1 bash linux/run_linux.sh "$HOME/NX3DX-Linux/data" --window \
  2>&1 | tee ~/nx3dx-audio-test.log
```

In game: test an actual wall collision, then go to OPTION → MORE →
SOUND → BGM SETTINGS → MENUS → CHOOSE MP3 and choose a local audio file.
Watch for a filename picker, and afterwards for `YOUR MP3`. Its RESET
button activates only after the custom track is installed.

If collision sounds are silent, inspect:

```bash
grep -Ei 'linux sound:|sound: cannot open .*sound/(wall|crash[1-6])\\.wav|collision fallback' \
  ~/nx3dx-audio-test.log | tail -n 70
for s in wall crash1 crash2 crash3 crash4 crash5 crash6; do
  test -s "$HOME/NX3DX-Linux/data/sound/$s.wav" && echo "[OK] $s.wav" \
    || echo "[MISSING] $s.wav"
done
```

All WAV files and converted arcade data must remain private and be
sourced only from material the player is authorized to use.

If the custom picker does not open, inspect:

```bash
command -v zenity || command -v kdialog
grep -Ei 'linux picker:|entry BGM:' ~/nx3dx-audio-test.log | tail -n 35
```
