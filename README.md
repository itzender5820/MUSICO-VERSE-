# ✦ MUSICO VERSE ✦

> A fully-featured, terminal-based music player built for **Android Termux** — with real-time braille visualizer, synced lyrics, multi-format decoding, and a fully customizable color system. All running inside your terminal, no GUI required.

```
╔══════════════════════════════════════════════════════════════════════════════╗
║  ▶  NAME     : Lily                  │  Lily was a little girl               ║
║     ARTIST   : Alan Walker           │  Afraid of the big, wide world        ║
║     ALBUM    : Different World       │  She grew up within her castle walls  ║
║     FORMAT   : mp3                   │       Now and then she tried to run   ║
║     DURATION : 195s                  │  She went in the woods away           ║
║     YEAR     : 2018                  │  So afraid, all alone                 ║
╠══════════════════════════════════════╧═══════════════════════════════════════╣
║  [########################################-----------]  [ 67%] [02:10/03:15] ║
╠══════════════════════════════════════════════════════════════════════════════╣
║   ···⣀⣀⣤⣤⣶⣶⣿⣿⣿⣶⣶⣤⣤⣀···       ···⣀⣤⣶⣿⣿⣿⣶⣤⣀···                                 ║
║  ⣀⣤⣤⣶⣶⣿⣿⣿⣿⣿⣿⣿⣿⣿⣶⣶⣤⣤⣀   ⣀⣤⣶⣿⣿⣿⣿⣿⣿⣿⣿⣶⣤⣀                                        ║
╠══════════════════════════════════════════════════════════════════════════════╣
║ > Alan Walker - Lily.mp3                                                     ║
║ > Alan Walker - Faded.mp3                                                    ║
╠══════════════════════════════════════════════════════════════════════════════╣
║  P=play/pause  R=repeat  1/2=vol  /=search  I=import  S=settings  Q=quit     ║
╚══════════════════════════════════════════════════════════════════════════════╝
```

---

## Table of Contents

- [Features](#features)
- [Requirements](#requirements)
- [Installation](#installation)
- [Controls](#controls)
- [Settings & Color System](#settings--color-system)
- [Lyrics System](#lyrics-system)
- [Visualizer Styles](#visualizer-styles)
- [Audio Engine](#audio-engine)
- [Project Structure](#project-structure)
- [License](#license)

---

## Features

### Playback
- **Multi-format decoding** via FFmpeg — plays FLAC, MP3, M4A/AAC, Opus, OGG, WAV, APE, WMA and more
- **Recursive playlist scanning** — drop all your music into `/sdcard/music/` including sub-folders; every track is found and sorted automatically
- **Alphabetical sorting** — all songs sorted A–Z by filename, case-insensitive
- **Volume control up to 300%** — push beyond 100% with PCM-level amplification when you need extra loudness
- **Repeat mode** — loop the current track indefinitely
- **Seek** — jump forward/backward 10 seconds at a time

### Visualizer
Five distinct real-time visualizer styles, all powered by a 2048-point FFT across 19 frequency bands (60 Hz to 8 kHz), rendered in Unicode braille and block characters:

| Style | Description |
|---|---|
| **BARS** | Braille bars rising from the bottom — smooth cosine-interpolated mountain shape |
| **MIRROR** | Bars grow from the centre upward and downward simultaneously (NCS-style) |
| **WAVE** | Single-pixel sine-wave edge drawn with braille sub-cell precision |
| **FIRE** | Block characters `░ ▒ ▓ █` with intensity brightest at the base |
| **DOTS** | Peak dot `•` floating above a faint trail per frequency column |

### Lyrics
- **Sidecar files** — automatically loads `.lrc`, `.txt`, or `.lyrics` file placed alongside the audio file with the same name
- **Timed LRC sync** — scrolls line-by-line in sync with playback; active line centred and highlighted
- **Online auto-fetch** — uses the `syncedlyrics` Python library to search and download synced lyrics automatically if no local file is found
- **Automatic caching** — fetched lyrics are saved as a `.lrc` file next to the audio for instant loading next time
- **Animated noise fallback** — when no lyrics are available, the right panel shows a continuously shifting organic field of braille characters and Unicode symbols (`⣿ ⣶ ⣤ ⣀ • ✦ ★ ·`) driven by overlapping sine waves

### Search
- Press `/` to open the live search overlay
- Type any part of a song name — results filter in real time
- Navigate with `↑ ↓`, press `Enter` to immediately play the selected result
- Press `Esc` to cancel and return to the normal playlist view

### Color System
Full per-element color customization using **ANSI 256-color codes** with an independent **brightness control (0–100%)**. Colors are applied directly to the terminal hardware color table via `init_color()` — bypassing the terminal's default palette entirely, guaranteeing consistent brightness on any theme.

Every visual element is independently configurable:
- Visualizer bars
- UI border / outline
- Title bar
- Metadata key labels
- Track name / metadata values
- Progress bar
- Playlist text
- Active/selected song (foreground + background)
- Lyrics (dim / active line)
- Animated noise field
- Status bar

### Settings Screen
Press `S` to open the settings overlay. Navigate with `↑ ↓`, switch between the ANSI and Brightness columns with `Tab` or `← →`, press `Enter` to type a new value. Changes apply and save instantly to `~/.config/musico_verse/settings.conf`.

---

## Requirements

**Termux packages:**
```bash
pkg install cmake clang make ffmpeg ncurses python termux-tools
```

**Python lyrics fetcher (optional):**
```bash
pip install syncedlyrics
```

**Terminal requirement:** A 256-color terminal is needed for the full color system. If your terminal only supports 8 colors, the player falls back gracefully to basic color mode with a warning.

```bash
export TERM=xterm-256color   # add to ~/.bashrc if needed
```

---

## Installation

```bash
# 1. Extract
unzip musico_verse_1.2.zip
cd musico_verse_1.2

# 2. Build (installs all dependencies automatically)
bash build.sh

# 3. Run
./build/musico_verse
```

The build script handles everything: installs Termux packages, requests storage permission, and compiles the project with CMake + Clang.

**Put your music here:**
```
/sdcard/music/
├── Artist A - Song 1.flac
├── Artist A - Song 2.mp3
├── Pop/
│   ├── Song A.m4a
│   └── Song B.opus
└── Chill/
    └── Night Drive.wav
```
All sub-folders are scanned recursively.

---

## Controls

| Key | Action |
|---|---|
| `P` | Play / Pause |
| `R` | Toggle repeat (current track) |
| `↑ ↓` | Previous / Next track |
| `← →` | Seek backward / forward 10 seconds |
| `1` | Volume up (+5%) |
| `2` | Volume down (−5%) |
| `/` | Open live search |
| `I` | Import a new folder or file path |
| `S` | Open settings |
| `Q` | Quit |

---

## Settings & Color System

The settings file is saved at:
```
~/.config/musico_verse/settings.conf
```

### Color Format
Each color is defined as `<ANSI code, Brightness>`:
```
color_viz=51,100        # ANSI 51 (bright cyan)  at 100% brightness
color_lyr_hi=226,100    # ANSI 226 (yellow)       at 100% brightness
color_lyr_dim=253,60    # ANSI 253 (white)        at 60%  brightness
color_noise=48,80       # ANSI 48  (green)        at 80%  brightness
```

**ANSI 256 reference:**
- `0–15` — Standard 16 terminal colors
- `16–231` — 6×6×6 RGB color cube
- `232–255` — Greyscale ramp (dark → light)

**Brightness:** `0` = black (fully off), `100` = full color, values in between dim the color proportionally.

### Visualizer Style Codes
```
0 = BARS   (braille, default)
1 = MIRROR (NCS-style)
2 = WAVE   (sine edge)
3 = FIRE   (block chars)
4 = DOTS   (peak dots)
```

---

## Lyrics System

### Priority order on track load:
1. `SongName.lrc` — timed LRC (synced, scrolls automatically)
2. `SongName.txt` — plain text lyrics (displayed statically)
3. `SongName.lyrics` — same as `.txt`
4. **Online fetch** via `syncedlyrics` (async, cached as `.lrc` after download)
5. **Animated noise** — shown while fetching or if no lyrics exist

### LRC format example:
```
[00:12.34]Lily was a little girl
[00:15.80]Afraid of the big, wide world
[00:19.00]She grew up within her castle walls
```

---

## Visualizer Styles

### How it works internally

1. **Ring buffer** — incoming PCM samples are written into a 2048-sample circular buffer (mono, downmixed)
2. **FFT** — a 2048-point Hann-windowed FFT runs on every audio callback via KissFFT
3. **19 bands** — frequency bins are grouped into 10 log-spaced bands (60 Hz → 8 kHz) then mirrored into a symmetric 19-band layout
4. **Smoothing** — exponential smoothing with separate attack (0.25) and decay (0.15) coefficients for natural bounce
5. **Interpolation** — cosine interpolation between band centre columns produces the smooth flowing mountain shape rather than blocky steps
6. **Braille rendering** — each column's height is mapped to a 4-unit sub-cell grid using Unicode braille characters (`⣿ ⣶ ⣤ ⣀`), giving 4× vertical resolution compared to regular block characters

---

## Audio Engine

Musico Verse uses a **fully open-source audio stack** — no proprietary players, no Termux API wrappers:

```
Audio file
    ↓
FFmpeg libavformat   — reads container format (mp3, flac, m4a, opus…)
FFmpeg libavcodec    — decodes compressed audio to raw PCM
FFmpeg libswresample — resamples to 44100 Hz stereo float
    ↓
Player callback      — pulls float PCM frames, feeds visualizer ring buffer
    ↓
AudioEngine          — applies volume gain (PCM-level, supports >100%)
                     — converts float → int16
                     — double-buffer queue for gapless output
    ↓
OpenSL ES            — Khronos open standard, built into Android NDK
(SLAndroidSimpleBufferQueueItf)
    ↓
Android AudioFlinger — kernel audio mixer
    ↓
Hardware DAC → Speaker / Headphones
```

| Component | Type | License |
|---|---|---|
| OpenSL ES | Khronos open standard | Free / open |
| FFmpeg libavformat | Open source | LGPL 2.1+ |
| FFmpeg libavcodec | Open source | LGPL / GPL |
| FFmpeg libswresample | Open source | LGPL 2.1+ |
| KissFFT | Open source | BSD |
| ncurses | Open source | MIT-like |

---

## Project Structure

```
musico_verse/
├── CMakeLists.txt          — CMake build configuration
├── build.sh                — One-command Termux build script
├── README.md               — This file
└── src/
    ├── main.cpp            — ncurses UI, event loop, all panels
    ├── player.h / .cpp     — Playback controller, state machine
    ├── audio_engine.h/.cpp — OpenSL ES output, double-buffer queue
    ├── audio_stub.cpp      — Silent fallback for non-Android builds
    ├── av_decoder.h / .cpp — FFmpeg multi-format decoder wrapper
    ├── visualizer.h / .cpp — FFT engine + 5 render styles
    ├── playlist.h / .cpp   — Recursive directory scanner + sorter
    ├── lyrics.h            — LRC parser + async syncedlyrics fetcher
    ├── settings.h          — Color scheme, viz style, save/load
    ├── cover_art.h         — ASCII art generator (legacy)
    └── kissfft/
        └── kiss_fft.h      — Self-contained FFT implementation
```

---

## License

```
Apache License
Version 2.0, January 2004
http://www.apache.org/licenses/

Copyright (c) 2026 itz-ender

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
implied. See the License for the specific language governing
permissions and limitations under the License.
```

### Third-Party Licenses

| Library | License | Link |
|---|---|---|
| FFmpeg | LGPL 2.1+ / GPL 2+ | https://ffmpeg.org/legal.html |
| OpenSL ES | Khronos free use license | https://www.khronos.org/opensles/ |
| KissFFT | BSD 3-Clause | https://github.com/mborgerding/kissfft |
| ncurses | MIT-style (X11) | https://invisible-island.net/ncurses/ |
| syncedlyrics | MIT | https://github.com/moehmeni/syncedlyrics |

---

*Built with ♥ for the terminal. — itz-ender, 2026*

# MUSICO-VERSE-
