# Nuked SC-55/8820 JUCE

> **This is a fork** of [depixeled-chris/nuked-sc55-juce](https://github.com/depixeled-chris/nuked-sc55-juce).
> It is what I use to play MIDI files on Linux (Arch / CachyOS, Hyprland) through hardware-accurate
> Roland Sound Canvas emulation instead of a soundfont. On top of upstream it adds:
>
> - **SC-8820 emulation**, through [88emu](https://github.com/dsp56300/gearmulator/blob/main/doc/88emu_readme.md)
>   (The Usual Suspects, part of gearmulator). It runs the real SC-8820 firmware on emulated SH-2 / XP6 / LSP chips.
>   This is not a soundfont or an approximation.
> - **An SC-55 / SC-8820 switch** in the main window. The choice is saved. The exact SC-55 variant is still picked in
>   Preferences, and the switch goes back to it.
> - **A clear error when ROMs are missing.** You get a dialog listing the files the selected model needs, not a crash or silence.
> - Linux niceties: settings in `~/.config/NukedSC55JUCE/`, uppercase `.MID` files show in the file picker,
>   drag-and-drop a MIDI file onto the window to play it, 96 kHz default output with windowed-sinc resampling,
>   and the audio device choice is remembered.
>
> **No ROMs are included, and none are embedded.** You supply dumps of your own hardware; see [ROM situation](#rom-situation--read-this-too).
> **Source only, no binaries:** see [License situation](#license-situation--read-this-before-building),
> which matters even more here because 88emu is GPL-3.0.

A JUCE-based standalone host for the [Nuked-SC55](https://github.com/nukeykt/Nuked-SC55) Roland Sound Canvas and JV-880 hardware emulator.

This project gives you a clean GUI to play MIDI files or stream live MIDI input through the cycle-accurate Nuked-SC55 emulator core, with a system-aware MIDI input picker that does **not** truncate the device list (unlike many older WinMM-based players).

> **Source only.** Because of the license terms attached to Nuked-SC55, no precompiled binaries are distributed from this repository. Clone and build it yourself. See **License** below for the full explanation.

---

## What's in the box

### `NukedSC55JUCE` — the main standalone player

A Winamp/WMP-style player UI:

- **Menu bar** (File / Edit / Help) with Open MIDI, Preferences, About.
- **Now Playing** label showing the current file.
- **Seek bar** with current/total time labels. Drag to scrub, release to seek; sustained notes are silenced automatically.
- **Transport**: Play/Pause toggle, Stop (rewinds to 0), master Volume slider.
- **Preferences dialog** (Edit → Preferences) for all configuration:
  - Emulator **Model** selector covering every Romset Nuked-SC55 supports (SC-55mk2, SC-55, SC-55st, CM-300/SCC-1, SCB-55, RLP-3237, SC-155, SC-155mk2, JV-880).
  - **ROM directory** picker.
  - **MIDI input device** dropdown listing every system MIDI port (no fixed-size limit, unlike WinMM-based players).
  - Full embedded `AudioDeviceSelectorComponent` for output device, sample rate, buffer size, channel mapping.
- **Auto-loads ROMs on startup** when the saved path is valid. No clicks required for the common case.
- **Settings persistence** across launches (ROM directory, model, MIDI input, last-opened MIDI file, master gain).
- **Lenient SMF parser** that handles real-world MIDI files with trailing bytes (JUCE's strict parser rejects these).
- **Status bar** showing live engine diagnostics: native sample rate, output rate, peak level, audio FIFO fullness.

### `NukedTestRunner` — headless renderer for validation

A separate console executable that reads the same saved settings as the GUI app and renders a MIDI file offline through the emulator directly. Writes a `test-output.wav` and detailed `test-runner.log`. Useful for:

- Validating that ROMs load correctly without firing up the GUI.
- Sanity-checking emulator output on a known-good MIDI.
- CI / automated testing where no audio device is available.
- Diagnosing GUI vs. core problems by comparing the two outputs.

Override settings at the command line:

```
NukedTestRunner.exe --rom-dir="C:/path/to/roms" --model=0 --midi="C:/song.mid"
```

### What this project does *not* (yet) do

- VST3 / AU / CLAP plugin variant.
- Multi-instance polyphony stacking (Nuked has a 24-voice cap per instance; jcmoyer's fork supports stacking).
- Reverb / chorus parameter controls.
- LCD display recreation (would be cute, not on roadmap).

---

## License situation — read this before building

This is the part everyone skips and then regrets.

### Nuked-SC55 is *not* MIT or BSD-without-strings

Despite being called "open source" in casual conversation, Nuked-SC55's per-file license header reads:

> *Redistributions may not be sold, nor may they be used in a commercial product or activity.*

That clause makes it incompatible with **GPL** (which forbids adding restrictions like non-commercial), incompatible with **JUCE's commercial license** (no sale allowed), and incompatible with shipping a precompiled binary in any commercial context. It does *not* prevent personal use, modification, or non-commercial source distribution, which is what this project sticks to.

### Practical consequences

| You want to... | OK under Nuked's license? |
|---|---|
| Clone, build, and use this app personally | Yes |
| Fork this repo and publish your changes (source) | Yes |
| Distribute compiled binaries you built yourself | **Murky.** The "may not be sold" clause is about selling, but the "commercial activity" clause is broad. We don't do this from the official repo. |
| Sell binaries on a store, bundle in commercial software | **No** |
| Use it for music you write and sell | Yes. Your music is your work; this app is just a renderer. |

### Why this matters for the repo

Because of the combined-work license conflict (Nuked-SC55's non-commercial clause + JUCE's GPLv3-or-paid licensing), this repository ships **source only**. Building binaries is the user's responsibility. Files in `src/` and the `CMakeLists.txt` itself are MIT-licensed (see `LICENSE`); the combined binary inherits Nuked-SC55's restrictions in practice.

### This fork adds a GPL-3.0 core (88emu)

The SC-8820 core, 88emu, comes from [gearmulator](https://github.com/dsp56300/gearmulator), which is **GPL-3.0**. GPL-3.0 does not allow extra restrictions like Nuked-SC55's non-commercial clause, so **a binary containing both cores cannot be legally distributed by anyone**. Building and using it yourself is fine; GPL-3.0 §2 puts no conditions on private use. This repo only references gearmulator as a git submodule (it does not copy its code), and it ships no binaries.

If you need a binary you can share, build with `-DNUKED_WITH_88EMU=OFF` (Nuked-SC55 only, upstream's situation), or use 88emu's own player for the SC-8820.

If you want to push for binary distribution, the right path is asking nukeykt (Nuked-SC55's author) for written permission to redistribute non-commercial binaries. He's responsive on his issue tracker.

---

## ROM situation — read this too

The Nuked-SC55 emulator is a CPU + DSP simulator. It needs the original Roland firmware ROMs to actually do anything. **Those ROMs are not in this repo and never will be.** They're copyrighted Roland firmware and we don't have permission to redistribute them.

Legitimate ways to obtain the ROMs:

1. Buy an SC-55 / SC-55mk2 / CM-300 / etc. and dump the chips yourself. Used units run $80–$200 on eBay; chip dumpers (e.g. TL866II Plus) are ~$50.
2. Some Roland soft-synth installations have shipped equivalent ROM data in encrypted form, but extracting and using it outside that product is on you, legally.

The "everyone just downloads them from archive.org" path is *technically* infringing. The community largely doesn't care, but this project does not point you at any specific source.

### ROM filename convention

The app expects ROMs to follow Nuked-SC55's "legacy" filename scheme. Place them in a single directory and point the app at it.

| Model | Files expected |
|---|---|
| **SC-55mk2 / SC-155mk2** (v1.01) | `rom1.bin`, `rom2.bin`, `rom_sm.bin`, `waverom1.bin`, `waverom2.bin` |
| **SC-55** (v1.00 / v1.21 / v2.0) | `sc55_rom1.bin`, `sc55_rom2.bin`, `sc55_waverom1.bin`, `sc55_waverom2.bin`, `sc55_waverom3.bin` |
| **SC-55st** (v1.01) | `rom1.bin`, `rom2_st.bin`, `rom_sm.bin`, `waverom1.bin`, `waverom2.bin` |
| **CM-300 / SCC-1** (v1.10 / v1.20) | `cm300_rom1.bin`, `cm300_rom2.bin`, `cm300_waverom1.bin`, `cm300_waverom2.bin`, `cm300_waverom3.bin` |
| **SCB-55** | `scb55_rom1.bin`, `scb55_rom2.bin`, `scb55_waverom1.bin`, `scb55_waverom2.bin` |
| **RLP-3237** | `rlp3237_rom1.bin`, `rlp3237_rom2.bin`, `rlp3237_waverom1.bin` |
| **SC-155** | `sc155_rom1.bin`, `sc155_rom2.bin`, `sc155_waverom1.bin`, `sc155_waverom2.bin`, `sc155_waverom3.bin` |
| **JV-880** (v1.0.0 / v1.0.1) | `jv880_rom1.bin`, `jv880_rom2.bin`, `jv880_waverom1.bin`, `jv880_waverom2.bin` |

### SC-8820 ROMs (88emu)

88emu finds ROMs by **content** (MD5 + size), searching the ROM folder **and its subfolders**, so you can keep them apart, e.g. `<ROM folder>/SC-8820/`. If a dump isn't recognized by hash, these standard names are used as a fallback:

| File | Size | What it is |
|---|---|---|
| `sc8820_internal.bin` | 64 KiB | SH7016 CPU internal ROM |
| `sc8820_program.bin` | 2 MiB | program flash |
| `sc8820_wave0.bin` | 16 MiB | wave ROM |
| `sc8820_wave1.bin` | 8 MiB | wave ROM |

All four are required. If any are missing, switching to SC-8820 opens a dialog listing what 88emu expects (names, sizes, known hashes), and the app stays usable. Switch back to SC-55 to keep playing. 88emu's [ROM documentation](https://github.com/dsp56300/gearmulator/blob/main/doc/88emu_readme.md#rom-loading) covers alternative layouts.

Example layout:

```
~/.local/share/nuked-sc55/roms/          <- set this as the ROM directory in Preferences
├── rom1.bin, rom2.bin, rom_sm.bin, waverom1.bin, waverom2.bin   (SC-55mk2, Nuked names)
├── sc55_rom1.bin, ...                                            (SC-55, Nuked names)
└── SC-8820/
    ├── sc8820_internal.bin
    ├── sc8820_program.bin
    ├── sc8820_wave0.bin
    └── sc8820_wave1.bin
```

Full list and exact chip-to-filename mappings are in [`external/Nuked-SC55/doc/USAGE.md`](https://github.com/jcmoyer/Nuked-SC55/blob/master/doc/USAGE.md). Your local checkout pins a specific submodule commit, so check `documentation/USAGE.md` inside the submodule for that exact version.

---

## Prerequisites

| Tool | Version | Notes |
|---|---|---|
| **CMake** | 3.22 or newer | https://cmake.org/download |
| **A C++20 compiler** | MSVC 2022, Apple Clang 14+, GCC 11+, or Clang 14+ | C++20 is required because Nuked-SC55 uses `std::span` and `std::filesystem` |
| **JUCE** | 8.0 or newer (8.0.12 is the FetchContent fallback) | Either install locally or let CMake fetch it |
| **Git** with submodule support | any modern version | This project uses a submodule for the Nuked-SC55 backend |

### Optional but recommended: local JUCE install

This project follows the convention of looking for JUCE at:

- **Windows**: `C:/JUCE`
- **macOS / Linux**: `~/JUCE`

If JUCE is present at that path, CMake uses it. If not, CMake will `FetchContent` JUCE 8.0.12 from GitHub on first configure. Both work; local is faster on rebuilds.

To install JUCE locally:

```bash
# Windows (PowerShell, run as admin):
git clone --depth 1 --branch 8.0.12 https://github.com/juce-framework/JUCE.git C:/JUCE

# macOS / Linux:
git clone --depth 1 --branch 8.0.12 https://github.com/juce-framework/JUCE.git ~/JUCE
```

### Override the JUCE path

If your JUCE is somewhere else, pass `-DJUCE_LOCAL_PATH=/path/to/JUCE` at CMake configure time.

---

## Build

### Clone with submodules

```bash
git clone https://github.com/milk4sale/nuked-sc55-juce.git
cd nuked-sc55-juce
git submodule update --init --depth 1 external/Nuked-SC55 external/gearmulator
# gearmulator's own submodules, minus its JUCE copy (not needed here):
git -C external/gearmulator submodule update --init --depth 1 --recursive \
    source/cpu/dsp56300 source/cpu/mc68k source/3rdparty/cpp-terminal \
    source/3rdparty/RmlUi source/3rdparty/freetype source/3rdparty/portmidi-latest \
    source/3rdparty/lunasvg
```

Don't want SC-8820? Skip the gearmulator lines and configure with `-DNUKED_WITH_88EMU=OFF`.

88emu builds as its own CMake sub-build with only 88emu switched on (no JUCE, no plugins), producing one static `lib88emu.a`. The first build takes a few extra minutes. The 88emu part currently builds with GCC/Clang only, so Windows/MSVC builds default to `NUKED_WITH_88EMU=OFF`.

### Windows (Visual Studio 2022)

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The executable lands in `build/NukedSC55JUCE_artefacts/Release/nuked-sc55-sc8820.exe`.

### Windows (Ninja + MSVC, faster)

```powershell
# From a "Developer PowerShell for VS 2022" prompt
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### macOS

```bash
cmake -B build -G Xcode
cmake --build build --config Release
```

Output: `build/NukedSC55JUCE_artefacts/Release/nuked-sc55-sc8820.app`

### Linux

Install the JUCE Linux dependencies first.

Arch / CachyOS / Manjaro:

```bash
sudo pacman -S --needed base-devel cmake ninja git alsa-lib jack2 freetype2 fontconfig \
    libx11 libxcomposite libxcursor libxext libxinerama libxrandr libxrender mesa glu
```

Debian / Ubuntu:

```bash
sudo apt-get install libasound2-dev libjack-jackd2-dev \
    libcurl4-openssl-dev libfreetype-dev libfontconfig1-dev \
    libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
    libxinerama-dev libxrandr-dev libxrender-dev \
    libwebkit2gtk-4.1-dev libglu1-mesa-dev mesa-common-dev
```

Then:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Output: `build/NukedSC55JUCE_artefacts/Release/nuked-sc55-sc8820` (and `NukedTestRunner` next to it under `NukedTestRunner_artefacts/Release/`).

---

## First run

1. Launch the built executable.
2. Go to **Edit → Preferences...**
3. **Model:** pick the unit you have ROMs for (defaults to SC-55mk2).
4. **ROM directory:** click **Browse...** and pick the directory containing your dumped ROM files (e.g. `rom1.bin`, `waverom1.bin`, …).
5. *(Optional)* **MIDI input:** pick a system MIDI port if you want to drive the emulator from a DAW or external player (e.g. a loopMIDI virtual port).
6. *(Optional)* **Audio device:** verify your output device is correct (the picker is at the bottom of the Preferences dialog).
7. Close Preferences. The engine auto-loads as soon as a valid ROM directory is set; the status bar will read "Engine: ... @ 66207 Hz".
8. Use the **SC-55 / SC-8820** switch under the transport to change cores. The choice is saved.
9. **File → Open MIDI...**, pick a `.mid` file (or drag one onto the window).
10. Click **Play**.

On subsequent launches, ROMs auto-load from the saved path and the last-opened MIDI file is restored. Just hit Play.

If you hear nothing:
- Check the status bar at the bottom: it shows engine state and live diagnostics (`peak` should rise above zero when audio is being produced; `fifo` should be at a healthy non-zero percentage).
- Verify the output device in **Edit → Preferences → Audio device** — pick the right one if JUCE picked a virtual/silent default.
- Try the headless **`NukedTestRunner`** (`--model=9` is SC-8820, `--model=0` SC-55mk2, `--model=1` SC-55) to verify the emulator works in isolation; if it produces a non-silent WAV, the problem is in the GUI's audio path, not the core.

---

## Project layout

```
nuked-sc55-juce/
├── CMakeLists.txt              # Top-level build script
├── LICENSE                     # MIT for wrapper code
├── NOTICE                      # Third-party license notices
├── README.md                   # You are here
├── .gitignore                  # Excludes build artifacts AND *.bin (ROMs)
├── external/
│   └── Nuked-SC55/             # Git submodule — jcmoyer's fork of nukeykt/Nuked-SC55
└── src/
    ├── Main.cpp                # JUCEApplication entry, DocumentWindow
    ├── MainComponent.h/.cpp    # Top-level UI host: menu bar, layout, audio callback
    ├── NukedEngine.h/.cpp      # Pimpl facade over the Nuked Emulator class:
    │                           #   - dedicated emulator worker thread
    │                           #   - lock-free MIDI input + audio output FIFOs
    │                           #   - atomic master gain, peak meter, diagnostics
    ├── MidiFilePlayer.h/.cpp   # SMF playback: play/pause/stop/seek, lenient
    │                           #   parser, atomic loadFile semantics
    ├── SeekBar.h/.cpp          # Position slider + current/total time labels
    ├── PlayerControls.h/.cpp   # Transport buttons + master volume
    ├── PreferencesDialog.h/.cpp# Modal config: model, ROMs, MIDI input, audio
    └── TestRunner.cpp          # Headless validation executable (separate target)
```

The `external/Nuked-SC55` submodule is pinned to a specific commit. To update it:

```bash
cd external/Nuked-SC55
git fetch
git checkout <new-commit-or-tag>
cd ../..
git add external/Nuked-SC55
git commit -m "Bump Nuked-SC55 submodule"
```

---

## Threading model

This is the part that breaks the first time someone reaches for the emulator in a tight loop, so it's worth documenting.

```
┌────────────────┐                    ┌──────────────────┐
│  MIDI device   │  raw bytes ─────▶ │ Lock-free MIDI    │
│  callback      │                    │ FIFO (uint8 x N)  │
└────────────────┘                    └──────────────────┘
                                              │
                                              ▼
                                      ┌──────────────────┐
                                      │  Emu worker      │
                                      │  thread          │
                                      │                  │
                                      │  - drain MIDI    │
                                      │  - call Step()   │
                                      │  - back off when │
                                      │    audio FIFO    │
                                      │    is full       │
                                      └──────────────────┘
                                              │
                              sample callback ▼
                                      ┌──────────────────┐
                                      │ Lock-free audio  │
                                      │ FIFO (float x N) │
                                      └──────────────────┘
                                              │
                                              ▼
                                      ┌──────────────────┐
                                      │ Audio thread     │
                                      │ (JUCE)           │
                                      │                  │
                                      │ - pull samples   │
                                      │ - resample to    │
                                      │   device rate    │
                                      └──────────────────┘
```

The emulator runs at a fixed native rate (66207 Hz for SC-55mk2 / SC-55st / SC-155mk2, 64000 Hz for SC-55mk1 / CM-300 / SCB-55 / RLP-3237 / SC-155 / JV-880). These are *the actual hardware DAC rates* derived from the MCU's clock divider. They look weird because they aren't musical-standard rates, but that's the chip.

Sample-rate conversion to the audio device rate is done with `juce::LagrangeInterpolator` in the audio callback, with **leftover-sample tracking** between blocks so no input frames get dropped (which caused audible pitch jitter in earlier versions). A polyphase resampler would be a quality upgrade but is not strictly required for normal listening.

---

## Roadmap

### Done

- [x] Standalone JUCE GUI with Winamp/WMP-style player layout
- [x] All Romsets supported by Nuked-SC55 (mk1, mk2, ST, CM-300, SCB-55, RLP-3237, SC-155, SC-155mk2, JV-880)
- [x] Live MIDI input + Standard MIDI File playback with seek / pause / stop
- [x] Auto-load ROMs on startup; settings persistence
- [x] Master volume control
- [x] Headless `NukedTestRunner` for offline WAV rendering
- [x] Lenient SMF parser (tolerates files with trailing bytes that JUCE's parser rejects)
- [x] Leftover-aware resampler (kills the periodic jitter / pitch drift)

### Next

- [ ] VST3 / AU / CLAP plugin variants
- [ ] Multi-instance polyphony stacking (jcmoyer's fork supports it)
- [ ] Render-to-WAV in the GUI app (the core path is already in `NukedTestRunner`)
- [ ] Higher-quality polyphase resampler
- [ ] Reverb / chorus parameter controls (Nuked's MCU exposes these)
- [ ] Playlist support
- [ ] LCD display recreation
- [ ] DAW-friendly tempo sync (when running as a plugin)
- [ ] Preset / SysEx bank loader

If you want any of these, open an issue or send a PR.

---

## Contributing

Contributions welcome under the same license terms. Please:

- Keep new code MIT-licensed (in `src/`).
- Don't add files that bundle ROM data or Roland firmware in any form.
- If you bump the Nuked-SC55 submodule, mention which commit in your PR.

Code style: roughly JUCE-flavored C++ (4-space indent, K&R braces, no `using namespace` in headers).

---

## Acknowledgments

- **nukeykt** for the original [Nuked-SC55](https://github.com/nukeykt/Nuked-SC55) emulator. That's the real achievement; the wrapper is trivial. The emulator is years of reverse engineering.
- **jcmoyer** for the [maintained fork](https://github.com/jcmoyer/Nuked-SC55) that exposes the emulator as a library with a clean backend API. Without that work, this wrapper would be much harder.
- **linoshkmalayil** for the [GUI-Float fork](https://github.com/linoshkmalayil/Nuked-SC55-GUI-Float) that informed a lot of the UX decisions here.
- **Raw Material Software** for JUCE.
- **Roland** for designing the SC-55 in 1991 and shipping the synth that all this work exists to preserve.
