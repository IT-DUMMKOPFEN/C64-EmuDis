# C64-EmuDis

**C64-EmuDis 0.1a** is a Commodore 64 emulator for **Windows 11, macOS (Apple Silicon) and Linux**.
It is based on [VICE 3.10](https://vice-emu.sourceforge.io/) and keeps every VICE feature and
setting, and it adds tools for looking inside the running machine.

Author: **IT-Dummkopfen** · License: **GPL-2.0-or-later** (see [Why GPL?](#license))

---

## Features

### Everything from VICE
- Accurate C64 emulation (x64sc core): VIC-II, SID (reSID), CIAs, 1541/1571/1581 drives,
  datasette, cartridges, REU, printers, joysticks, mice, paddles, RS232, ...
- All VICE settings under *Preferences → Settings*
- VICE monitor with breakpoints and the classic line assembler (`a c000`)
- VICE snapshots, event recording, screenshots in many formats

### Added by C64-EmuDis (menu **Tools**)
| Tool | What it does |
|------|--------------|
| **No loading times** | True drive emulation off by default, PRGs injected into RAM, warp while autostarting. Toggle: *Tools → Fast loading* |
| **Live Memory Viewer** | Hex + PETSCII/screen code/ASCII view or 256×256 memory map, updated up to 50×/s while the C64 runs, change highlighting, editing, search, save/load ranges, VIC-II info |
| **Font Finder** | Finds character sets in RAM (scored heuristics), hires/multicolor preview, export .bin/.prg/.png, activate in VIC |
| **Sprite Finder** | Finds sprite data in RAM, shows the 8 hardware sprites, export .bin/.prg/.png/sprite sheet |
| **SID Finder** | Finds music players (SID register writes, init/play detection, IRQ callers, PSID headers), live SID register view, export .sid (PSID v2) / .prg |
| **Assembler** | Two-pass 6502/6510 assembler (ACME/Kick Assembler style, labels, expressions, illegal opcodes), write to RAM, run with SYS, save .prg |
| **Disassembler** | Listing or re-assemblable ACME / Kick Assembler source with automatic and system labels, live CPU view following the PC |
| **Snapshot Manager** | 10 slots with screenshot thumbnails |
| **Screenshot** | PNG in original resolution with border |
| **Video Recorder** | MP4 (H.264 + AAC) in original resolution with border, optionally scaled **2×, 3×, 4×** with sharp pixels |
| **Help / History / About** | Built-in help, the history of the C64 and credits/licenses |

Default hotkeys (Linux/Windows `Alt+Shift`, macOS `Cmd+Shift`): **M** memory viewer, **F** fonts,
**G** sprites, **I** SID finder, **A** assembler, **D** disassembler, **K** snapshots,
**V** start/stop video, **X** screenshot.

---

## Download

Ready-to-run builds for all three platforms are produced by GitHub Actions
(*Actions* tab → latest run → *Artifacts*, or the *Releases* page for tagged versions):

| Platform | File | Notes |
|----------|------|-------|
| Windows 10/11 (x64) | `C64-EmuDis-0.1a-windows-x64.zip` | unzip, start `bin\C64-EmuDis.exe`; ffmpeg included |
| macOS 12+ (Apple Silicon) | `C64-EmuDis-0.1a-macos-arm64.dmg` | drag to Applications; first start: right-click → *Open* (not notarized); ffmpeg included |
| Linux (x86_64) | `C64-EmuDis-0.1a-linux-x86_64.tar.gz` | extract, run `./C64-EmuDis`; needs GTK 3, GLEW and `ffmpeg` (for video) from your distribution |

If the emulator screen stays white or black (typical for virtual machines such as Parallels on a
Mac, VMware or VirtualBox), use the **software renderer**: *Tools → Software renderer*, the option
`-softrender`, or on Windows `Start C64-EmuDis (software renderer).bat`. On Windows it is chosen
automatically when Direct3D 11 is not available.

---

## Building from source

The build system is VICE's autotools build; only the C64 emulator (`x64sc`) is packaged.

### Linux (Debian/Ubuntu)
```sh
sudo apt install build-essential autoconf automake flex bison xa65 dos2unix \
     libgtk-3-dev libglew-dev libpulse-dev libasound2-dev libpng-dev \
     libcurl4-openssl-dev libevdev-dev ffmpeg
./autogen.sh
mkdir _build && cd _build
../configure --enable-gtk3ui --disable-arch --disable-pdf-docs --disable-html-docs
make -j$(nproc)
../packaging/linux/make-dist.sh .. . ../dist
```

### Windows (MSYS2 UCRT64)
See [`.github/workflows/build.yml`](.github/workflows/build.yml) and
[`packaging/windows/make-dist.sh`](packaging/windows/make-dist.sh).

### macOS (Homebrew, Apple Silicon)
See [`.github/workflows/build.yml`](.github/workflows/build.yml) and
[`packaging/macos/make-dist.sh`](packaging/macos/make-dist.sh).

---

## Project layout

| Path | Content |
|------|---------|
| `src/arch/gtk3/emudis_*.c` | the C64-EmuDis tools (memory viewer, finders, assembler, ...) |
| `src/arch/gtk3/actions-emudis.c` | menu/hotkey actions |
| `src/emudis_version.h` | name, version, author |
| `packaging/` | distribution scripts for Linux, Windows and macOS |
| everything else | VICE 3.10 (lightly modified, see [CHANGES-EMUDIS.md](CHANGES-EMUDIS.md)) |

---

## License

C64-EmuDis is free software under the **GNU General Public License v2.0 or later** (`COPYING`).

*Why not MIT?* C64-EmuDis is a modified version of VICE, which is GPL licensed. A program derived
from GPL code must be distributed under the GPL as a whole, so C64-EmuDis - including the new
code © 2026 IT-Dummkopfen - is GPL-2.0-or-later.

The Commodore ROM images in `data/` are © Commodore Business Machines and are distributed as part
of VICE. All used components and their licenses are listed in [CREDITS.md](CREDITS.md) and on the
in-app *Help → About C64-EmuDis* page.
