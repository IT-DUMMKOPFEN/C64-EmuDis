# Credits and licenses

## C64-EmuDis
- Version 0.3b, © 2026 **IT-Dummkopfen** - GPL-2.0-or-later
- https://github.com/IT-DUMMKOPFEN/C64-EmuDis

## Used components

| Component | Author / origin | License |
|-----------|-----------------|---------|
| VICE 3.10 | The VICE Team, https://vice-emu.sourceforge.io | GPL-2.0-or-later |
| reSID (SID emulation, part of VICE) | Dag Lem | GPL-2.0-or-later |
| Commodore ROM images (KERNAL, BASIC, character ROM, drive DOS) | © Commodore Business Machines, distributed with VICE | proprietary, redistributed as part of VICE |
| GTK 3, GLib, Pango, GdkPixbuf, ATK | The GNOME Project | LGPL-2.1-or-later |
| Cairo | cairographics.org | LGPL-2.1 or MPL-1.1 |
| GLEW | glew.sourceforge.net | Modified BSD / MIT |
| libpng | libpng.org | PNG Reference Library License v2 |
| zlib | Jean-loup Gailly, Mark Adler | zlib license |
| linenoise-ng (VICE monitor) | arangodb / Salvatore Sanfilippo | BSD-2-Clause |
| FFmpeg (external program, video recording) | ffmpeg.org | LGPL-2.1-or-later / GPL-2.0-or-later (depending on build) |
| C64 Pro Mono font | Style, style64.org | Style free font license |
| PetMe fonts | Kreative Software | Relay Fonts Free Use License (see `data/common/PetMe-FreeLicense.txt`) |
| Colodore palette (tool previews) | Philip "Pepto" Timmermann | free to use |

Windows builds additionally contain the MSYS2 runtime DLLs of the libraries above; macOS builds
contain the Homebrew builds of the same libraries.

## Inspiration (ideas only - no code was copied)

| Project | License |
|---------|---------|
| [emudore](https://github.com/marioballano/emudore) by Mario Ballano | Apache-2.0 |
| [Emu64](https://github.com/thkattanek/emu64) by Thorsten Kattanek | GPL-2.0 |
| [BMC64](https://github.com/randyrossi/bmc64) by Randy Rossi | GPL-3.0 |
| [c64](https://github.com/mohrt/c64) by mohrt | MIT |
| [Dis64](https://github.com/smnjameson/Dis64) by smnjameson | not stated |

Dis64 inspired the disassembler's automatic labels and Kick Assembler output.
