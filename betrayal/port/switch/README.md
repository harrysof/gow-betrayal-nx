# God of War: Betrayal -- Nintendo Switch port

Switch port by **Harrysof**.

A homebrew `.nro` built from the same translated C++ game as the PC port. The Win32 window,
keyboard/XInput layer, Microsoft MIDI mapper and GDI presenter are replaced by a libnx
framebuffer, Switch pad input and a small software MIDI/WAV mixer. The game code, renderer and
resource loaders are the shared sources in `../src/`.

This is an unofficial fan port and is not affiliated with Sony, Santa Monica Studio, Glu Mobile,
Metaflow or the original rights holders. **No game data is included.** You must supply your own
Sony Ericsson K800i `.jar`.

## Requirements

* A Switch with homebrew support (Atmosphère + hbmenu).
* Your own **K800i build** of *God of War: Betrayal* (MIDlet 1.4.20). The K800i data has 22 px
  tiles, parallax backgrounds, two resources in `RP19` and a **176x352** `RP2[10]` tile atlas.
  Other builds are rejected; the common generic 0.1.95 jar and the other Sony Ericsson v1.4.20
  variants (128x256 art) are **not** supported.
* To build: devkitPro with the `switch-dev` group (`devkitA64`, `libnx`, `switch-tools`).

## Build

From a devkitPro MSYS shell:

```sh
cd betrayal/port/switch
make -j8
```

Or from Windows:

```bat
betrayal\port\switch\build.bat
```

`build.bat` looks for devkitPro in `%DEVKITPRO%`, `%USERPROFILE%\devkitPro`,
`C:\devkitPro` and `%ProgramFiles%\devkitPro`. The output is:

```
betrayal/port/switch/gow_switch.nro
```

`make clean` / `build.bat clean` removes the build directory and the `.nro`.

## Install

1. Copy `gow_switch.nro` to:
   ```
   sdmc:/switch/gow-betrayal/gow_switch.nro
   ```
2. Copy your K800i jar next to it:
   ```
   sdmc:/switch/gow-betrayal/game.jar
   ```
   (`God-of-War-Betrayal_K800i_v1420.jar` and `god_of_war_240x320.jar` are also searched, but
   the latter name is usually the unsupported generic build.)
3. Launch **God of War - Betrayal** from hbmenu.

The first launch extracts the jar's `RP*` resource banks into
`sdmc:/switch/gow-betrayal/data/`. Later launches use that folder directly, so the jar can be
removed after the first run. The folder name `gow-betrayal` is fixed: the port uses absolute SD
paths.

Saves and settings live in:

```
sdmc:/switch/gow-betrayal/saves/       record-store files
sdmc:/switch/gow-betrayal/display.cfg  aspect / scaling / FPS
sdmc:/switch/gow-betrayal/audio.log    audio backend diagnostics
sdmc:/switch/gow-betrayal/saves/exceptions.log
```

PC saves are compatible: copy the `rec*.bin` files from
`%LOCALAPPDATA%\gow-betrayal-port\saves\` into the Switch `saves/` folder.

## Controls

The game still speaks the phone's MIDP key codes; the pad is mapped to them.

| Switch | In a level | In menus |
|---|---|---|
| D-pad / left stick | move | move cursor |
| **B** (south) | jump / up | — |
| **A** (east) | grab / block / down | confirm |
| **Y** (west) | attack / fire | — |
| **X** (north) / **R** | change weapon/magic (left soft key) | — |
| **Plus** | pause (right soft key) | back |
| **Minus**, **L**, **ZL** | upgrade screen (`#`) | upgrade screen |

The original phone's soft-key labels are the two captions in the bottom corners; those are the
left/right soft keys above. The A/B convention follows the console standard: A confirms and B
backs out in menus, while the physical layout mirrors the PS2 game in a level (south = jump,
west = attack, east = grab, north = magic/weapon).

## Options

The three extra rows in **Options** and the pause menu are:

* **Resolution** -- `Original` (240 px), `Auto` (follows the 16:9 screen, giving a 568x320
  logical canvas), `4:3`, `16:10`, `16:9`, `21:9`. Widescreen shows more of the level; sprites
  and the HUD are not stretched.
* **Scaling** -- `Fit` (largest aspect-correct image) or `Integer` (whole-pixel multiples).
* **FPS** -- `Original` (25 logic steps/s) or `60` (interpolated display frames; the logic is
  still 25 steps/s).

The settings are saved to `display.cfg` on the SD card. They do not touch the game's own save
records.

## Audio

The Switch has no General MIDI synthesizer, so the port includes a small software synthesizer:
MIDI sequences are parsed with the same reader as the PC port and rendered with a simple
GM-style instrument set (piano, organ, strings, brass, bass, drums, etc.). WAV resources are
decoded directly (8/16/24/32-bit PCM, mono/stereo) and resampled to 48 kHz. The result is
recognisably the same music but not identical to the Windows MIDI mapper or the phone's synth.
Turn **Sound** off in Options if you prefer silence.

## Differences from the PC port

* No launcher/updater: the `.nro` reads the jar itself.
* Pad only; no keyboard or mouse.
* No Fullscreen row (the console is always fullscreen); the row is **Scaling** instead.
* FPS choices are Original and 60, not 90/120/144/165/240.
* Display settings are stored on the SD card, not in `%LOCALAPPDATA%`.
* Rendering is a 1280x720 nearest-neighbour framebuffer; docked mode is upscaled by the system.
* Java exceptions are logged to `saves/exceptions.log` without dbghelp symbol names (the
  backtrace is not captured on AArch64).

## Troubleshooting

* **"No game data found"** -- `game.jar` is missing or in the wrong folder. It must be at
  `sdmc:/switch/gow-betrayal/game.jar`.
* **"not the K800i 22 px build"** -- the jar is not the K800i 1.4.20 build. The port checks that
  `RP19` contains two resources *and* that the `RP2[10]` tile atlas is the K800i 176x352 PNG.
  The generic 0.1.95 jar and the other v1.4.20 Sony Ericsson variants (128x256 art) will not
  work. If you have several v1.4.20 files, use the one whose `RP2[10]` atlas is 176x352.
* **No sound** -- check `audio.log`; the mixer still runs but the game starts with Sound off.
  Enable it in Options.
* **A crash before the first frame** -- remove `data/` and let the jar extract again; if it still
  fails, `saves/exceptions.log` records the Java-level throw site.

## Legal

This project is not affiliated with, endorsed by, or supported by Sony Interactive
Entertainment, Santa Monica Studio, Glu Mobile, Metaflow, or the game's original developers and
publishers. *God of War* is a trademark of Sony Interactive Entertainment LLC. See the
repository's `NOTICE.md` for attribution and third-party licences. The `.nro` contains no game
data and must not be distributed with any.
