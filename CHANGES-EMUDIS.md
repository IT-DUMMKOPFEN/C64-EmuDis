# C64-EmuDis changes to VICE 3.10

C64-EmuDis is a modified version of VICE 3.10 (GPL-2.0-or-later). As required by the GPL, this file
lists all files that were changed or added and why. All changes: 2026-10-24, IT-Dummkopfen.

## New files
| File | Purpose |
|------|---------|
| `src/emudis_version.h` | product name, version 0.1a, author |
| `src/arch/gtk3/emudis.h`, `emudis_common.c` | shared helpers (memory access, VIC-II state, colours, dialogs, heuristics) |
| `src/arch/gtk3/emudis_memview.c` | live memory viewer |
| `src/arch/gtk3/emudis_fontsearch.c` | font finder |
| `src/arch/gtk3/emudis_spritesearch.c` | sprite finder |
| `src/arch/gtk3/emudis_sidsearch.c` | SID finder / PSID export / live SID registers |
| `src/arch/gtk3/emudis_6502.c/.h` | 6502 opcode table, two-pass assembler, disassembler |
| `src/arch/gtk3/emudis_asmwin.c` | assembler window |
| `src/arch/gtk3/emudis_disasmwin.c` | disassembler window with live CPU view |
| `src/arch/gtk3/emudis_snapshots.c` | snapshot manager (10 slots with thumbnails) |
| `src/arch/gtk3/emudis_video.c` | MP4 video recorder dialog |
| `src/arch/gtk3/emudis_pages.c` | Help, History and About pages |
| `src/arch/gtk3/actions-emudis.c/.h` | UI actions for the Tools menu |
| `src/arch/gtk3/software_renderer.c/.h` | Cairo software renderer (no GPU needed), resource `EmuDisRenderer`, options `-softrender`/`-gpurender`, choice saved in `renderer.txt` |
| `data/hotkeys/hotkeys-emudis.vhk` | default hotkeys for the tools |
| `packaging/*` | distribution scripts (Linux, Windows, macOS; Windows/macOS based on VICE's bindist scripts) |
| `.github/workflows/build.yml` | CI builds for all three platforms |

## Changed VICE files
| File | Change |
|------|--------|
| `src/arch/gtk3/uimachinemenu.c` | new **Tools** menu, C64-EmuDis entries in the Help menu |
| `src/arch/gtk3/ui.c`, `actions-joystick.c` | window title "C64-EmuDis 0.1a", register the new actions |
| `src/arch/gtk3/uimon.c` | monitor window title |
| `src/arch/gtk3/widgets/ffmpegwidget.c` | "video scale" option (1x-4x) in the media recording dialog |
| `src/arch/shared/uiactions.c/.h` | new action IDs `emudis-*` |
| `src/arch/shared/archdep_user_*_path.c`, `archdep_default_sysfile_pathlist.c` | own settings/data folder `c64-emudis` (does not touch an installed VICE) |
| `src/arch/shared/coproc.c` | Windows: quote the whole command for `cmd.exe /C` so paths with spaces work |
| `src/arch/shared/macOS-launcher.sh` | `C64-EmuDis.app` starts x64sc |
| `src/arch/shared/archdep_is_macos_bindist.c` | detect any `*.app` bundle, not only `VICE.app` (ROMs were not found in `C64-EmuDis.app`) |
| `src/arch/shared/macOS-launcher.c` | missing `<mach-o/dyld.h>` include (build error with newer Xcode) |
| `src/drive/drive-resources.c` | true drive emulation off by default (no loading times) |
| `src/autostart.c`, `src/autostart-prg.h` | no random autostart delay, PRGs injected into RAM by default |
| `src/gfxoutputdrv/ffmpegexedrv.c` | resource `FFMPEGVideoScale` (nearest neighbour 1x-4x), `-pix_fmt yuv420p`, prefer an ffmpeg next to the emulator, quoted output file name, wait up to 5 s for ffmpeg, robust ffmpeg detection, fixes for recording without audio (crash and dropped frames), frames before ffmpeg is connected are skipped |
| `src/gfxoutput.h` | maximum video bitrate raised to 50 Mbit/s (for 4x videos) |
| `src/arch/gtk3/uimachinewindow.c` | renderer selection: probe Direct3D on Windows, use the software renderer if it fails or when selected |
| `src/arch/gtk3/directx_renderer_impl.cc/.h`, `directx_renderer.c` | WARP fallback, retry without feature level 11.1, HRESULTs in the log, `vice_directx_impl_probe()`, dialog offering the software renderer when DirectX fails |
| `src/arch/shared/sounddrv/soundcoreaudio.c` | macOS: reopen the sound device when the audio device changes or stops playing (e.g. Bluetooth headset connected) instead of freezing the emulation |
| `src/mainlock.c/.h` | `mainlock_get_depth()` so tool windows know whether they hold the VICE lock |
| `src/Makefile.am`, `src/arch/gtk3/Makefile.am`, `data/hotkeys/Makefile.am` (+ generated `.in`) | new files |
| `data/hotkeys/hotkeys.vhk` | include `hotkeys-emudis.vhk` |
