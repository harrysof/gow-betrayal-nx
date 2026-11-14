# God of War: Betrayal -- Nintendo Switch port

![God of War: Betrayal](.github/social-preview.jpg)

A Nintendo Switch homebrew port of *God of War: Betrayal*, the 2007
side-scrolling *God of War* made for J2ME (mobile Java) phones. This fork builds
on [samioan/gow-betrayal-remastered](https://github.com/samioan/gow-betrayal-remastered),
the native Windows remaster: the game code is the same decompiled-and-translated
C++, with the Win32 window, input and audio layer replaced by libnx.

**Switch port by Harrysof.** The Windows port is by samioan and the upstream
project contributors.

| Platform | Download | You supply |
|---|---|---|
| Nintendo Switch (this fork) | `gow_switch.nro` from [Releases](https://github.com/harrysof/gow-betrayal-nx/releases) | K800i jar |
| Windows (upstream) | [samioan's Releases](https://github.com/samioan/gow-betrayal-remastered/releases) | K800i jar |

## Download and play

**The game is not included.** It belongs to its rights holders, so you need
your own copy of the Sony Ericsson K800i `.jar`. Other versions are not
supported. The port checks for the K800i layout (MIDlet 1.4.20, 22 px tiles,
176x352 tile atlas) and rejects the generic build and other phone variants.

### Nintendo Switch

1. Copy `gow_switch.nro` to:
   ```
   sdmc:/switch/gow-betrayal/gow_switch.nro
   ```
2. Put your K800i jar beside it as:
   ```
   sdmc:/switch/gow-betrayal/game.jar
   ```
   It may also keep its original name if it is the only `.jar` in that folder.
3. Launch **God of War - Betrayal** from hbmenu.

The first launch extracts the jar's `RP*` resource banks into
`sdmc:/switch/gow-betrayal/data/`; later launches use that folder directly. The
folder name is fixed. Saves, settings and logs live under the same folder. See
[betrayal/port/switch/README.md](betrayal/port/switch/README.md) for controls,
settings and troubleshooting.

### Windows (upstream)

1. Download the zip from the
   [upstream Releases page](https://github.com/samioan/gow-betrayal-remastered/releases)
   and unzip it anywhere.
2. Run **GodOfWar.exe** and click **Choose file...** to pick your `.jar`. The
   launcher unpacks it for you.
3. Choose **Fullscreen** or **Windowed** and press **Play**.

There is no installer, and no Visual C++ redistributable is needed. The
launcher checks GitHub for newer upstream builds on start-up, offers an
**Update** button when there is one, and keeps your saves.

## What the Switch port adds

| | |
|---|---|
| Presentation | 1280x720 linear framebuffer, nearest-neighbour scaling, 16:9 pillarboxing |
| Resolution | Original, Auto, 4:3, 16:10, 16:9, 21:9 (HUD stays anchored, sprites are not stretched) |
| Frame rate | Original (25 logic steps/s) or 60 interpolated display frames |
| Audio | Built-in software MIDI synth and WAV decoder mixed through libnx `audout` |
| Controls | Switch pad: D-pad/stick, A/B confirm-back, PS2-style face layout in a level |
| Setup | Reads the user's K800i jar from the SD card on first run; no PC launcher needed |
| Saves | `sdmc:/switch/gow-betrayal/saves/` (PC save records are compatible) |

The Switch build uses the same translated game, renderer, resource loaders and
menus as the PC port. The Resolution / Scaling / FPS rows appear in **Options**
and the pause menu.

## The PC port (upstream)

The upstream project is a native Windows port of *God of War: Betrayal*, rebuilt
from the original game's own decompiled code, not emulated. It adds widescreen,
high frame rates and modern controls.

**Status: complete.** The upstream game is finished, released as the stable
1.0.0, and has been played from start to finish.

| | |
|---|---|
| Widescreen | Original, Auto, 4:3, 16:10, 16:9, 21:9 |
| Frame rate | Original, 60, 90, 120, 144, 165, 240, Unlimited, with interpolated frames |
| Game speed | Stays at the original 25 logic steps a second |
| Fullscreen | Borderless by default; F11 or Alt+Enter toggles it |
| Controls | Keyboard and XInput gamepad |

Widescreen shows more of the level and never stretches sprites or the HUD. The
options are in **Main menu > Options** and in the pause menu.

### Windows troubleshooting

The PC music and sound effects are MIDI, played through the Windows synthesizer,
so they sound different from an emulator or the phone; that is expected. Sound
problems are logged to `%LOCALAPPDATA%\gow-betrayal-port\audio.log`.

PC game files stay in `data\` next to the launcher. Saves and display settings
live in `%LOCALAPPDATA%\gow-betrayal-port\`.

## How it was made

The game is plain JVM bytecode with minifier-style obfuscation, so
[Vineflower](https://github.com/Vineflower/vineflower) recovers close-to-original
Java directly. From there the code was read through and renamed, and the
game's custom binary formats (resource banks, sprite atlases, animation sets,
bit-packed levels) were parsed and verified byte for byte.

The port is translated mechanically: `tools/java2cpp.py` turns the renamed
Java into C++, and a hand-written engine and platform layer sit underneath.
Patches for widescreen, settings and interpolation are applied during
translation (`tools/gow_port_patches.py`), so the renamed Java stays a plain
decompile. The PC build is C++17 on raw Win32 and GDI; the Switch build swaps
that shell for libnx while sharing the game and renderer.

The project started on the generic v1.4.8 jar and moved to the Sony Ericsson
K800i build, which has larger 22 px tiles, a parallax background in every
level, better art and all eleven MIDI tracks in use.

## Repository layout

| Path | Contents |
|---|---|
| `betrayal/decompiled/` | Raw Vineflower output |
| `betrayal/src/` | Renamed, compile-checked Java (generated from `names.map`) |
| `betrayal/docs/` | Roadmaps, class map, asset formats and `names.map` |
| `betrayal/port/` | The C++ port, launcher and packaging scripts |
| `betrayal/port/switch/` | Nintendo Switch homebrew (`.nro`) build and libnx backend |
| `tools/` | Extraction, decompilation, renaming, parsing and translation scripts |
| `roms/`, `betrayal/extracted/` | Your original jar and its contents (gitignored, never commit) |

### Documentation

| Topic | File |
|---|---|
| Reverse-engineering record | [ROADMAP](betrayal/docs/ROADMAP.md) |
| Port development log | [PORT_ROADMAP](betrayal/docs/PORT_ROADMAP.md) |
| Classes and members | [CLASS_MAP](betrayal/docs/CLASS_MAP.md) |
| Binary formats | [ASSET_FORMATS](betrayal/docs/ASSET_FORMATS.md) |
| Store page copy | [ITCH_PAGE](betrayal/docs/ITCH_PAGE.md) |
| Switch port | [switch/README](betrayal/port/switch/README.md) |

The scripts are described in [tools/README.md](tools/README.md).

## Building from source

### Windows (upstream)

You need Windows, Visual Studio or the VS Build Tools with the C++ workload,
CMake and Ninja. Regenerating sources also needs Python 3.10+ (`pip install
javalang`) and a Java 17+ JDK (see [tools/README.md](tools/README.md)).

```
python tools\extract_jar.py          & rem unpack your jar from roms\
python tools\decompile.py
python tools\rename_gow.py           & rem renamed Java -> betrayal\src + compile check
python tools\java2cpp.py             & rem -> betrayal\port\src\gen
betrayal\port\build.bat              & rem builds betrayal\port\build\gow_port.exe
```

To build the same package that gets released, run
`betrayal\port\build_dist.bat <version>` and then
`betrayal\port\check_dist.ps1`, which confirms that no game data or VC runtime
slipped in. The output is in `betrayal\port\dist\`.

### Nintendo Switch

Install devkitPro with the `switch-dev` group (`devkitA64`, `libnx` and
`switch-tools`). Then, from a devkitPro MSYS shell:

```sh
cd betrayal/port/switch
make -j8
```

Or from Windows:

```bat
betrayal\port\switch\build.bat
```

The output is `betrayal/port/switch/gow_switch.nro`. The `.nro` contains no
game data. `build.bat` looks for devkitPro in `%DEVKITPRO%`,
`%USERPROFILE%\devkitPro`, `C:\devkitPro` and `%ProgramFiles%\devkitPro`.

## Releases

The Switch port has no auto-updater. Download `gow_switch.nro` from this fork's
[Releases page](https://github.com/harrysof/gow-betrayal-nx/releases) or build it
with `betrayal\port\switch\build.bat`.

The inherited Windows release workflow (`.github/workflows/release.yml`) still
builds the upstream PC package and is left unchanged. The PC launcher's update
owner/repository are compiled as `samioan/gow-betrayal-remastered`, so Windows
builds from this fork continue to update from the upstream project.

## Legal

This project is not affiliated with, endorsed by, or supported by Sony
Interactive Entertainment, Santa Monica Studio, Glu Mobile, Metaflow, or the
game's original developers and publishers. *God of War* is a trademark of Sony
Interactive Entertainment LLC. See [NOTICE.md](NOTICE.md) for attribution and
third-party licences. The `.nro` contains no game data and must not be
distributed with any.
