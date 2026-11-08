# Building and setting up

How to build both games, set up the game data and start them: Generals (`GZHNeo.exe`) and Zero Hour (`GZHNeoZH.exe`),
64 bit (x64) only, with the current MSVC Build Tools. Written for version 2.0-alpha-2.

**To just play,** use the Release build (`cmake --build --preset x64-release`); Debug builds are slower and stop at
assertions meant for development.

**Layout.** All source code lives once under `Code/`, ordered in layers (`Core`, `Renderer`, `GameEngine`, `Platform`,
`App`, `Games`, `Tools`, `Tests`). The root `CMakeLists.txt` builds the shared libraries and the engine
(`GameEngineCommon`, `GameEngineGameClient`, `GameEngineGameLogic`, `GameEngineGameNetwork`, `GameEngineDevice`) once;
both games link the same targets. Per game only a small folder remains, `Code/Games/Generals` or `Code/Games/ZeroHour`,
with `CMakeLists.txt` (parameters such as `GAME_SUFFIX`, `GAME_EXE_NAME` and the asset folder), `GameEdition.cpp`
(selects the game) and `Generals.ico` (program icon). `Code/Games/GameTargets.cmake` builds what stays per game: the
object library `GameEdition`/`GameEditionMD`, the program and the tools (`ImagePacker`/`ImagePackerMD`, ...). The binary
folder per game is still called `<Build>/Generals/Code` or `<Build>/GeneralsMD/Code`, so the program paths stay the same.
The project's own game data for both games is under `Data/` (shaders, `GameLODOverride.ini`, project texts, free
content).

## Prerequisites

- **Visual Studio Build Tools 2026** (MSVC 14.51.36231 or newer) with:
  - "Desktop development with C++"
  - Windows 10/11 SDK (tested with 10.0.26100.0)
  - the CMake component ("C++ CMake tools for Windows")
  - MFC ("C++ MFC for latest build tools", x64 libraries) — **only** for the tool DebugWindow, not for the games.
- **vcpkg** in manifest mode (`vcpkg.json`): FFmpeg (videos and MP3 music), zlib and DirectXTex; Tracy in the profile
  build, Qt 6 for the editors. The VS Build Tools 2026 ship a `vcpkg.exe` (`<VS install path>\VC\vcpkg\vcpkg.exe`);
  alternatively use your own vcpkg installation and set `VCPKG_ROOT` to its root folder.
- **Python 3** for asset setup, replay and image tests (standard library only).
- **Target architecture is x64.** Configuring for Win32 or with an x86 triplet stops with a message.

The C++ standard is C++20 (`ENGINE_CXX_STANDARD`).

## Configuring

`CMakePresets.json` in the repository root:

| Configure preset | Build folder | Purpose | Build presets |
|---|---|---|---|
| `x64-debug` | `build-x64/` | **default:** both games and all tests | `x64-debug`, `x64-release` |
| `x64-tools` | `build-x64-tools/` | like `x64-debug`, plus all tools (`BUILD_TOOLS=ON`) | `x64-tools-debug`, `x64-tools-release` |
| `x64-editor` | `build-x64-editor/` | like `x64-debug`, plus the map editor and GUI editor (Qt 6, vcpkg feature `editor`) | `x64-editor-debug`, `x64-editor-release` |
| `x64-profile` | `build-x64-profile/` | Tracy profiler (`ENABLE_TRACY=ON`, vcpkg feature `profiling`) | `x64-profile` (Release) |
| `x64-asan` | `build-x64-asan/` | AddressSanitizer (`ENABLE_ASAN=ON`); the ASan runtime is copied next to the game | `x64-asan` |
| `x64-vulkan` | `build-x64-vulkan/` | like `x64-debug`, plus the `ctest` `VulkanShaders` (all shader variants through `dxc.exe` to SPIR-V); the Vulkan back end itself is in every build | `x64-vulkan` |
| `x64-d3d11-only` | `build-x64-d3d11-only/` | like `x64-debug`, without the Vulkan device layer (`GZH_RENDER_VULKAN=OFF`) | `x64-d3d11-only` |
| `x64-vulkan-only` | `build-x64-vulkan-only/` | like `x64-debug`, without the Direct3D 11 device layer (`GZH_RENDER_D3D11=OFF`) | `x64-vulkan-only` |

```
cmake --preset x64-debug
```

The first run on a new system triggers `vcpkg install` in manifest mode (FFmpeg, zlib and DirectXTex for the
`x64-windows` triplet, about 15 minutes). The presets do not set the vcpkg toolchain themselves; the root
`CMakeLists.txt` does: if `VCPKG_ROOT` is set it is used, otherwise the vcpkg path of the default VS Build Tools 2026
installation.

Without a preset (older CMake or a different toolchain) a manual configure still works:

```
cmake -S . -B build-x64 -G "Visual Studio 18 2026" -A x64 ^
  -DVCPKG_TARGET_TRIPLET=x64-windows ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake"
```

Configure options (`-D<name>=...`):

| Option | Default | Effect |
|---|---|---|
| `BUILD_ZERO_HOUR` | `ON` | also build Zero Hour (`GZHNeoZH`, tools with `MD`, `ReplayDeterminismZH`) |
| `BUILD_TOOLS` | `OFF` | tools of both games (section "Tools") |
| `GENERALS_ASSETS_DIR`, `GENERALSZH_ASSETS_DIR` | environment variables `GZH_ASSETS_GENERALS`, `GZH_ASSETS_ZH` | asset folders (section "Game data"); without either, the build links no game data |
| `ENABLE_MEMORYPOOL_STACKTRACE` | `OFF` | call stack per block in the leak report |
| `ENABLE_ANALYZE` | `OFF` | MSVC `/analyze`; use a build folder of its own |
| `ENABLE_FUZZERS` | `OFF` | fuzzing targets (`tools/fuzz/`) |
| `ENGINE_CXX_STANDARD` | `20` | C++ standard for the whole build |

No further SDKs are needed: the Direct3D 11 shaders are compiled at run time by `d3dcompiler_47.dll` (part of Windows)
from the HLSL files the build copies next to the executable; the Vulkan renderer compiles them with `dxcompiler.dll`,
which the build copies as well.

## Building

```
cmake --build --preset x64-debug
cmake --build --preset x64-release
cmake --build build-x64 --config Debug --target GZHNeoZH
```

The programs end up in

- `build-x64/Generals/Code/Main/<configuration>/GZHNeo.exe` (Generals),
- `build-x64/GeneralsMD/Code/Main/<configuration>/GZHNeoZH.exe` (Zero Hour).

After each build of a game the post-build step puts next to it: the runtime DLLs from vcpkg (FFmpeg, zlib,
DirectXTex), `dxcompiler.dll`, `Shaders\`, `ProjectData\` (the project's own data, section "Project data") and the
links to the game data (section "Links into the build folders"). XAudio2/X3DAudio and `d3dcompiler_47.dll` need no
copy (part of Windows).

After changing global compiler definitions (for example `WINVER`), rebuild once from scratch instead of incrementally;
CMake does not always notice changed macro definitions on the command line:

```
cmake --build build-x64 --config Debug --target GZHNeo --clean-first
```

**Release is not Debug:** both configurations must compute the same game logic; a difference is a bug (typically
uninitialised stack memory). Before submitting a change, build Release as well and check the replays in it (`ctest` with the label `assets`).

## Tools

The tools of both games depend on `BUILD_TOOLS` (off by default). A configuration with `BUILD_TOOLS=ON` and
`BUILD_ZERO_HOUR=ON` builds them together with the games: DebugWindow, ImagePacker and MapCacheBuilder for Generals,
the same targets with the suffix `MD` for Zero Hour. Sources and build lists live once under `Code/Tools`; each game gets
targets of its own that link against its engine.

```
cmake --preset x64-tools
cmake --build --preset x64-tools-debug
cmake --build --preset x64-tools-release
```

The preset inherits from `x64-debug` and builds into `build-x64-tools/`; `build-x64` stays untouched. The tools end up
in `build-x64-tools/<game>/Code/Tools/<configuration>/` (ImagePacker, MapCacheBuilder) or
`build-x64-tools/<game>/Code/Tools/<tool>/<configuration>/` (DebugWindow), with `<game>` = `Generals` or `GeneralsMD`.
`DebugWindow.dll` is an add-on module of the game (loaded with `-scriptDebug`); the Zero Hour target is called
`DebugWindowMD`, the file is `DebugWindow.dll` as well.

To build the tools without starting them (for example in a worktree), set `-DGENERALS_ASSETS_DIR=` and
`-DGENERALSZH_ASSETS_DIR=` to empty values; the games then get no links to the game data.

**Translation tool Babylon** (`Code/Tools/Editors/Babylon`): needs neither MFC nor Excel.
`BabylonModel`, `BabylonTool` and `BabylonTests` are built in every build folder, the window program `Babylon.exe` with
the editors (preset `x64-editor`).

### Map editor and GUI editor (Qt 6)

The map editor (`Code/Tools/Editors/MapEditor`) is one program for both games; it also
edits particle systems. The GUI editor (`Code/Tools/Editors/GuiEditor`) edits window layouts.
Map model, `MapTool` and `MapEditorTests` need nothing but the compiler and are built in every build folder. The window
programs need Qt 6 and depend on `BUILD_MAPEDITOR_APP`; the preset `x64-editor` sets it and gets Qt through the vcpkg
feature `editor` (build folder `build-x64-editor`):

```
tools\mapeditor\bauen_windows.cmd                   build Release, check the Qt plug-ins, self-test
tools\mapeditor\bauen_windows.cmd -Config Debug     Debug
tools\mapeditor\bauen_windows.cmd -Starten          start the editor afterwards
tools\mapeditor\bauen_windows.cmd -Jobs 16          fewer parallel jobs (little memory)
```

or by hand `cmake --preset x64-editor` and `cmake --build --preset x64-editor-release --target MapEditor`. The first
time, vcpkg builds Qt (Debug and Release), which takes a while; afterwards Qt comes from the vcpkg binary cache
(`%LOCALAPPDATA%\vcpkg\archives`). The executable is in `build-x64-editor/Code/Tools/MapEditor/<configuration>/`, with
the Qt DLLs and plug-ins next to it.

`MapEditor.exe --selftest <folder> [map]` draws the start, map and settings pages in light and dark theme and in German
and English as PNG files into the folder, writes `selftest.log` and exits with 0 when all images were made; the user's
settings stay untouched. The editor stores its settings as an INI file in the user profile (Qt `QSettings`, not in the
registry).

**3D view.** Next to `MapEditor.exe` are `MapEditorEngine.dll` (engine, bridge and view window; loaded only when
switching to the 3D view) and `Shaders\`. The engine reads the game data from the data folder of the chosen game
(Settings → Game data; the default is `GENERALS_ASSETS_DIR` or `GENERALSZH_ASSETS_DIR` of the build). Its log is
`%LOCALAPPDATA%\GZH Neo\MapEditor\engine.log`. The self-test with `--data <data folder>` starts the engine and saves
the 3D view. Without EA data, `python tools/mapeditor/testdaten.py <folder>` creates a small data set for smoke tests.

## Player package

`cmake --install <build folder> --config Release --prefix <folder>` collects what players need, without EA's game data:
`Generals\` and `ZeroHour\` with the game, its DLLs and the VC++ runtime, `Shaders\` and `ProjectData\`;
`GZHNeoEinrichtung.exe` and the licence files at the top. `-DGZH_INSTALL_FREE_CONTENT=OFF` leaves out the free content
(`ProjectData\Inhalt`, about 500 MB per game); the game then uses EA's content only, as with `-freeContent ea`.

Started without arguments, `GZHNeoEinrichtung.exe` opens a window (German or English after the Windows display
language) that sets up the game data for the package it belongs to: pick the installations, check the free space,
copy with progress and cancel; the detailed log goes to `GZHNeoEinrichtung.log` next to it.
`GZHNeoEinrichtung.exe --erkennen` lists the installations of Generals and Zero Hour it finds (Steam, EA app, Origin,
retail discs; `--quelle <folder>` to look in a folder of your choice). `GZHNeoEinrichtung.exe --kopieren --ziel <folder>`
copies their data into `<folder>\Generals` and `<folder>\ZeroHour`, next to the games of the package, and applies the
data fixes; the installation is only read. Only EA's archives and EA's folders below `Data` are copied, so files of
mods in the installation stay out. For development builds `tools/assets/setup_assets.py` remains the reference.

With [Inno Setup 6](https://jrsoftware.org/isinfo.php) installed, `cmake --build <build folder> --config Release --target
GZHNeoSetup` builds the setup `<build folder>\installer\GZHNeo-<version>-Setup.exe` from that folder: per-user installation
without administrator rights, Start menu entries, uninstaller; at the end it opens the setup window above.
Uninstalling removes the program folder including the copied game data; `Documents\GZH Neo` stays.

The workflow `.github/workflows/release.yml` does the same on GitHub Actions: on a tag `v<version>` (or by hand) it builds both
setups, runs the tests that need no game data and creates a draft release with the setups, checksums and the FFmpeg source code
(`tools/release/ffmpeg_quelle.ps1`). The draft is published by hand.

## Game data

This repository contains only source code, **no game data** (`.big` archives, INI data, `.w3d` models, videos, audio).
A working game needs your own, legally acquired installation of C&C Generals and/or Zero Hour. It is **only read and
never changed**; everything else uses an asset folder of its own per game.

### Machine paths (environment variables)

No script or build file names a fixed path of a machine. What differs per machine is kept in one place, looked up in
this order: the environment variable of the same name, then the machine file `%LOCALAPPDATA%\GZHNeo\pfade.json`
(Linux: `~/.config/gzhneo/pfade.json`), then, for the asset copies only, the target of the `Data` junction in an existing
game build folder. Switches such as `--source`, `--dest`, `--dir` or `-DGENERALS_ASSETS_DIR=` take precedence. The file
is shared by all clones and worktrees of the user. Set it up once per machine:

    python tools/pfade.py --einrichten                       # writes the file from what it can detect
    python tools/pfade.py --setzen GZH_EA_ZH "D:\Games\ZH"   # set or change one value
    python tools/pfade.py                                    # show every value and where it comes from

The same lookup is done by `tools/pfade.py` (Python tools), `tools/pfade.ps1` (PowerShell tools: the Windows check and
`tools/git/*_entfernen.ps1`) and `cmake/GzhPfade.cmake` (default of the cache variables at the first configure).

| Variable | Content |
|---|---|
| `GZH_ASSETS_GENERALS`, `GZH_ASSETS_ZH` | asset copy per game (target of `setup_assets.py`, default of `GENERALS_ASSETS_DIR` / `GENERALSZH_ASSETS_DIR`) |
| `GZH_EA_GENERALS`, `GZH_EA_ZH` | EA installation per game, read only (source of `setup_assets.py`) |
| `GZH_WERKZEUGE` | work folder outside the repository for the asset tools (`directxtex\texconv.exe`, `cc0\download`, `cc0\modelle`, Blender work, AI environment); relative paths in the job files under `tools/assets/blender/*.json` are taken below it |
| `BLENDER` | Blender program for the Blender scripts (otherwise `blender` from the search path) |

A build folder remembers the asset folders from its first configure; a variable set later only takes effect in a new
build folder or with `-DGENERALS_ASSETS_DIR=...`.

### Setting up Generals (once)

```powershell
python tools/assets/setup_assets.py --source "<Generals installation>" --dest "<Generals copy>"
# without --source/--dest: GZH_EA_GENERALS and GZH_ASSETS_GENERALS
```

The script can be repeated at any time (it only copies what is missing or changed) and creates:

| Content | Source | Note |
|---|---|---|
| 15 `.big` archives | EA installation | real copy, about 1.2 GB; **without** `PatchData.big`, `PatchWindow.big` and `PatchINI.big` (see "Asset version mismatch") |
| `Generals.dat`, `Install_Final.bmp`, `00000000.016`, `00000000.256` | EA installation | small files the game expects next to the executable |
| `Data\` (cursors, movies, scripts, water plane, language folders) | EA installation | about 70 MB |
| `Data\INI\...` | generated | the project's data fixes, extracted from the archives and patched |
| `Data\INI\CommandMap.ini`, `Data\<language>\CommandMap.ini` | Zero Hour installation | Zero Hour key bindings (see "Data fixes"); `--generals-tastenbelegung` leaves them out |
| `FIXES.md` | generated | list of all fixes with their reasons |

**Why a copy and not links to the installation:** tests no longer depend on an outside folder (EA app updates,
uninstalling, another machine), all worktrees and build folders share the same state, and data fixes have a fixed
place. An installer that turns an original game into this version has to do exactly the steps of this script: take
over archives, extra files and `Data\`, write the fixes as loose files, then put the executable and its DLLs next to
them. `tools/assets/setup_assets.py` is the reference for the asset side of such an installer.

### Setting up Zero Hour (once)

```powershell
python tools/assets/setup_assets.py --game zh --source "<Zero Hour installation>" --dest "<Zero Hour copy>"
# without --source/--dest: GZH_EA_ZH and GZH_ASSETS_ZH
```

| Content | Note |
|---|---|
| ZH `.big` archives of the main folder | without `PatchData.big`, `PatchWindow.big` and `PatchINI.big` (see "Asset version mismatch"); `PatchZH.big` (fixed Avenger models) stays |
| `Generals.dat`, `Install_Final.bmp`, `00000000.*`, `00000001.*`, `langdata.dat` | the Zero Hour versions (they differ from the Generals ones); `langdata.dat` is the word filter |
| `Data\` (cursors, movies, scripts, water plane, language folders) | about 300 MB; **without** `Data\INI\INIZH.big`, an outdated second copy that the game deletes at start (EA's patch 1.01, `GameEngine::init`); the valid one is `INIZH.big` in the main folder |
| `ZH_Generals\*.big` | the Generals base archives that Zero Hour loads in addition (as in the EA app layout) |
| `FIXES.md` | no fixes so far |

**Load order** (`Win32BIGFileSystem::init`): first all `*.big` in the executable's folder are loaded **recursively**,
sorted without regard to case; for duplicate files the archive loaded first wins. So `ZH_Generals\...` comes after all
ZH archives. The Generals base archives come from `-baseGame <folder>` if
given, otherwise from `ZH_Generals` next to the executable. A Zero Hour build without an asset folder therefore needs
`-baseGame` or the folder `ZH_Generals`.

### Language

The game does not read or write the Windows registry. The language decides the paths `Data\<language>\...` (texts,
speech, videos, fonts). Order:

1. `Settings.ini` in the user folder, `[General] Language = german`; while there is no `Settings.ini`, `Options.ini`
   with `Language = german`;
2. otherwise the archive names next to the executable: Generals `<Language>.big` (`German2.big`), Zero Hour
   `<Language>ZH.big` (`GermanZH.big`);
3. otherwise `english`.

The log names the language chosen (`GetGameLanguage: ...`). The version values are fixed to EA's last patches
(Generals 1.08, Zero Hour 1.04).

### Settings files

The game keeps its settings in `Settings.ini` (what the player sets) and `State.ini` (what the game remembers: player
name, last map, hints shown), both in the user folder, UTF-8 with sections. On the first
start without `Settings.ini` it takes over the values of `Options.ini`, `Network.ini` and `Skirmish.ini` once; those
files then stay as they are and are no longer read. A key is called `[Section] Name` in the new file, for example
`Options.ini` `TextureQuality` → `Settings.ini` `[Detail] TextureQuality`; the complete mapping is in
`Code/Core/EngineCore/Engine/SettingsSchema.cpp`.

Only in `Settings.ini` (no menu entry): `[Controls] ScrollSpeedKeyboard`, `ScrollSpeedEdge`, `ScrollSpeedDrag` (0..100)
and `EdgeScrollInWindow` (edge scrolling in a window as well; always on borderless and in full screen),
`[Camera] MaxZoomFactor` (1 to 1.25, default 1: how far the player camera may zoom out, as a multiple of the normal
maximum height), `[Display] RenderFPSLimit` (frame cap), `[Display] RenderScale`,
`[Detail] D3D11Effects` (effect list like `-effects`; entries with a leading `-` remove an effect from the detail
level, for example `-colorgrade`), `WorkerThreads` (like `-workers`), `PrefetchArchives`, `HardwareHint` (hint on CPUs
with fewer than four cores). With a menu entry: `[Display] RenderBackend = d3d11|vulkan` (checkbox "Vulkan renderer" on
the Display page), `Gamma = 0..100` (brightness slider, 50 is neutral, applied as a post effect). `FramesPerSecondLimit`
in `GameData.ini` is the tick rate of the game logic and must not be raised. Test runs do not write the files
(`-einstellungenNurLesen` and the test switches).

### Texture tiers SD, HD, UHD: the texture package

High-resolution textures are generated locally by each player from their own installation; none of it belongs in the
repository or in anything passed on. Two steps: compute the tiers, then build the package from them.

```powershell
python tools/assets/build_texture_tiers.py --game zh --tier hd
python tools/assets/build_texture_tiers.py --game zh --tier uhd
python tools/assets/build_texture_package.py --dir <asset folder>
```

`build_texture_tiers.py` writes `Texturstufen\HD\TexturesHD.big` (or `Texturstufen\UHD\...`) with a manifest into the
asset folder, intermediate results into `Texturstufen\Arbeit\` (a restart continues there; the folder may be deleted
afterwards). The tier folders are **only input** of the package tool; the game does not read them.
`build_texture_package.py` writes `Texturstufen\Paket\TexturesPaket*.big`: every texture once in its highest version
with a full mip chain, plus a manifest per archive with the original size. If the
active tiers are not called `HD` and `UHD`, name the folders with `--hd`/`--uhd` (for example
`--hd Texturstufen\HD-KI2 --uhd Texturstufen\UHD-KI2`). `LinkAssets.cmake` links `Texturstufen` like `Data` as a
**junction** into the executable's folder once it exists in the asset folder, that is on the next build after creating
it. **Deletion trap** as with `Data`: remove it only with `cmd /c rmdir`.

In HD and UHD the game loads the package archives **with precedence** (then free content and `-mod`), and the tier
only decides how many top mip levels per texture are skipped. **SD loads the originals**; **without a package there is
only SD.** A package archive with content other than textures and its manifest is rejected. The tier is chosen with
`[Detail] TextureQuality = Auto|SD|HD|UHD` (default `Auto`) or the switch `-textureQuality auto|sd|hd|uhd`, which takes
precedence; it takes effect on the next start. `Auto` always picks SD on the detail level *Low*, otherwise it decides by
output height and video memory. The line `TEXTURPAKET` (HD, UHD) or `TEXTURESTUFE` (SD) in the
runner log (or `HD_ASSETS:` in the debug log) names wish, choice, reason and the available tiers. The AI tiers in BC7
are made with `--stage`, `--bc7` and `--ai-reuse` (see `python tools/assets/build_texture_tiers.py --help`).

### Links into the build folders (automatic)

After each build of a game a post-build step calls `cmake/LinkAssets.cmake` (cache variable `GENERALS_ASSETS_DIR` or
`GENERALSZH_ASSETS_DIR`). It creates what is missing in the executable's folder
(`<Build>/<game>/Code/Main/<configuration>/`): archives and extra files as **hard links** (same drive, no extra space;
a copy if that fails), the `Data` folder (and `ZH_Generals` for Zero Hour) as a **junction** (no admin rights needed).
Existing entries are never touched, so a folder with links of its own stays as it is. `Debug/` and `Release/` are
separate output folders and both get links. Without an asset folder the build only warns, but the game will not start.

Another target: `cmake -DGENERALS_ASSETS_DIR=<path> ...` when configuring, or by hand
`cmake -DASSETS_DIR=<path> -DTARGET_DIR=<exe folder> -P cmake/LinkAssets.cmake`.

**Junction trap.** `Data` and `ZH_Generals` in the executable's folder are junctions into the shared asset folder.
Deleting a build folder or worktree recursively (`rm -r`, `Remove-Item -Recurse`, `git worktree remove --force`)
deletes the assets of **all** build folders through the junction. Remove the junctions first, one by one, with
`cmd /c rmdir <folder>\Data` (without `/s`, which only removes the link), or use `tools/git/buildordner_entfernen.ps1`
and `tools/git/worktree_entfernen.ps1`. If it happened anyway, `setup_assets.py` restores the asset folder.

### Project data (`ProjectData`)

Data the project brings itself is in the repository under `Data/` and is copied by the build to `ProjectData\` **next
to the executable**, explicitly not into `Data\`, which is a junction into the shared asset folder:

- `Data/INI` → `ProjectData\INI\` (`GameLODOverride.ini` with the detail levels, test aid key bindings);
- `Data/Text/<language>.str` → `ProjectData\Text\`: texts the project adds to the game (identifiers `GZH:...`), in the
  same form as EA's `map.str` and in CP1252. The game loads the file of its language, otherwise `english.str`; a new
  text gets an entry in every language file;
- `Data/Network`, `Data/Textures` and the free content (`Data/FreierBoden`, `Data/FreieStrassen`, `Data/BaeumeFrei`)
  → `ProjectData\Network\`, `ProjectData\Textures\`, `ProjectData\Inhalt\`.

### Data fixes

Loose files under `Data\` take precedence over the `.big` archives in the game. Fixes to EA's data are therefore kept
as complete files under `<asset folder>\Data\...`; `setup_assets.py` extracts them from the archive and patches them,
and the patch functions check strictly that exactly the expected places are hit. The current list is in `FIXES.md` of
the asset folder. So far:

- **A-01** (`Data\INI\Object\FactionUnit.ini` from `INI.big`): `GLARocketBuggyFullDebris` named
  `Show/HideSubObject Turret/TurretUp01` and a `WEAPONSET_PLAYER_UPGRADE` state (copied from `GLARocketBuggy`), but the
  debris model `UVRockBug_d1.W3D` has no turret. The ineffective lines are removed. `tools/assets/check_subobjects.py`
  checks all sub-object references of the object INIs against the W3D models.
- **Zero Hour key bindings** (profile `generals` only): the script reads Zero Hour's effective `CommandMap.ini` from
  `--zh-source` (default `GZH_EA_ZH`) and writes it as a loose file for every path either game has, so both games share
  the same shortcuts. Input only: game logic and replays stay the same. `--generals-tastenbelegung` removes the copies
  again. Without a Zero Hour installation Generals keeps its own bindings, with a warning. The shortcuts of the command
  bar (`&` in the `CONTROLBAR:` texts) are not covered.

### Asset version mismatch

`PatchData.big`, `PatchWindow.big` and `PatchINI.big` come from the digital versions (Origin/EA app, Steam). The retail
discs never had them, and the source code does not know them either. They add an extra single-player menu entry
(`MISSING: 'GUI:CustomMission'`): `PatchWindow.big` replaces `MainMenu.wnd`, `PatchData.big` brings its texts,
`PatchINI.big` its window transitions. `PatchWindow.big` would load before `Window.big` or `WindowZH.big` and cover the
real menus, so `setup_assets.py` leaves all three out and removes them if an earlier copy left one in the target.

## First start

Start the executable from its build folder, for example

```
build-x64\GeneralsMD\Code\Main\Release\GZHNeoZH.exe -win
```

The user folder (settings, saved games, replays, maps, mods) is `Documents\GZH Neo\Generals` or
`Documents\GZH Neo\Zero Hour`, or the folder given with `-benutzerdaten`. It is separate from EA's folder of the
original game (`Documents\Command and Conquer Generals [Zero Hour ]Data`), whose saved games and replays do not work
with GZH Neo; only on the very first start, without `Settings.ini`, the game takes over the settings of EA's
`Options.ini` (read only). Saved games, replays, maps and mods are not copied. After a crash or a hang (watchdog) there is a group of files under
`Crashes\<timestamp>\` in the program folder, next to the executable: minidump, `Info.txt`, the log ring buffer and the
replay of the running game.

### Running on another computer

The programs need the Microsoft Visual C++ runtime: `MSVCP140.dll`, `VCRUNTIME140.dll`, `VCRUNTIME140_1.dll` and
`VCOMP140.dll` (OpenMP, used by `DirectXTex.dll`). **On a computer with Visual Studio or the Visual Studio Build Tools
they are installed automatically** together with the C++ tools, so the machine you build on needs nothing else.

To run a Release build on another computer, install the **Microsoft Visual C++ v14 Redistributable (x64)** there once,
in at least the version of the build tools you built with (14.51 for the Build Tools 2026 named above; an older
"2015–2022" package may be too old). The installer comes with the Build Tools:
`<VS install path>\VC\Redist\MSVC\<version>\vc_redist.x64.exe`; the current one is also available from Microsoft
([latest supported Visual C++ Redistributable](https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist)).
Copy the whole program folder (`build-x64\<game>\Code\Main\Release\` with its DLLs, `Shaders\` and `ProjectData\`) and
set up the game data there as well; the `Data` junction points to a folder on the build machine and does not travel.

Everything else is part of Windows 10 and 11 (the Universal C runtime `api-ms-win-crt-*`, Direct3D 11, DXGI,
`d3dcompiler_47.dll`, XAudio2, DbgHelp) or comes next to the program (FFmpeg, zlib, DirectXTex, `dxcompiler.dll`).
The Vulkan renderer also needs `vulkan-1.dll`, which the graphics driver installs. **Debug builds** use the debug
runtime (`MSVCP140D.dll`, `VCRUNTIME140D.dll`, `ucrtbased.dll`), which only exists with Visual Studio installed and may
not be passed on; they run only on a build machine.

### Start-up switches

Both games accept the same switches (complete list in `Code/GameEngine/Common/Game/CommandLine.cpp`, a few evaluated
early in `Code/App/WinMain.cpp`). The ones for playing and setting up:

| Switch | Effect |
|---|---|
| `-win` | window instead of full screen |
| `-fullscreen` | full screen (default without `-win`): a borderless window over the monitor; the resolution from the settings is scaled into it with the right aspect ratio (black bars if it differs) |
| `-borderless` | borderless window at desktop size and resolution (overrides the settings), implies `-win` |
| `-bildschirm <N>` | start on monitor N (1 = primary monitor, then in Windows' order); without it on the monitor of the last run |
| `-nologo` | skip the logo and intro videos |
| `-quickstart` | like `-nologo`, also without the menu background scene and window animations |
| `-noshellmap` | without the 3D background scene of the main menu |
| `-noaudio` | start without sound |
| `-noVSync` | present without vertical sync, regardless of the settings |
| `-benutzerdaten <folder>` | user folder instead of `Documents\GZH Neo\<game>` (may contain characters outside the ANSI code page); EA's `Options.ini` is then not read |
| `-baseGame <folder>` | (Zero Hour) folder with the Generals base archives |
| `-mod <folder or archive>` | load a mod on top of the game data: a folder (its loose files, all `*.big` and `*.zip` in it, from HD on its texture package `Texturstufen\Paket`) or a single `.big`/`.zip`. Several `-mod`: the last one named is on top. A relative path is relative to the user folder, otherwise to its subfolder `Mods\`. Mods can also be switched on, off and ordered in the options menu (page Game, "Mods ..."). |
| `-freeContent <ea\|free>` | free content (terrain, roads, water from CC0 scans, `ProjectData\Inhalt`): `free` (default) or only EA's data |
| `-vulkan` / `-d3d11` | render back end for this run; the permanent choice is the checkbox "Vulkan renderer" on the Display page (`[Display] RenderBackend`). If Vulkan cannot be used, Direct3D 11 starts and the reason is shown once. Needs `dxcompiler.dll` next to the game (the build copies it) |
| `-effects <list>` | graphics effects instead of the list of the detail level: `fxaa`, `sharpen[=0..1]`, `softparticles`, `ssao`, `gtao`, `colorgrade`, `shadowmap[=size]`, `softshadows`, `contactshadows`, `shadowcascades`, `particlelight`, `particleground`, `terraindetail`, `terrainrock`, `terrainnormal`, `terraintess`, `rocktess`, `terrainlight`, `modellight`, `flatsnow`, `heightfog`, `sunshafts`, `bloom`, `heat`, `haze`, `lights`, `hdr`, `taa`, `waterssr`, `deband`, `all`, `none`, separated by commas. `-d3d11Effects` is the older name |
| `-textureQuality <auto\|sd\|hd\|uhd>` | texture tier for this start |
| `-renderScale <0.5..2>` | internal render resolution of the 3D scene (also in percent, 50 to 200): below 1 drawn smaller and scaled up, above 1 supersampled; interface, text and cursor stay at full resolution. `-renderScaleFilter bilinear\|bicubic\|cas` and `-renderScaleSharpen <0..1>` choose the filter |
| `-msaa <n>` | anti-aliasing with n samples (0 or 1 = off), overrides the settings |
| `-gamma <0..100>` | position of the brightness slider, 50 is neutral |
| `-maxZoom <1..1.25>` | how far the player camera may zoom out (`[Camera] MaxZoomFactor`) |
| `-waterQuality <0\|1\|2>` | water level: 0 = EA's still water, 1 = turbidity and Fresnel, 2 = also refraction |
| `-waterTexture <ea\|free>` | water colour: EA's painted tile or the free tiles from CC0 photos (default) |
| `-treffpunkt host[:port]` | rendezvous server for games over the internet (invite code, relay), several allowed; asked before the lists in `<user folder>\Rendezvous.txt` and `ProjectData\Network\Rendezvous.txt` |
| `-netzOhneTreffpunkt` | no rendezvous servers: no invite code, no relay; LAN and direct connection by address stay |
| `-netzOhnePortfreigabe` | do not ask the router to forward UDP 8086 and 8088 (otherwise through NAT-PMP or UPnP) |
| `-testhilfen` | unlock the test aids (money, power, win mission, ...; single player only) |
| `-mission <campaign> <no>` | start a campaign mission without the menu, for example `-mission USA 3` |
| `-ignoreAsserts` | (debug build) suppress assertion dialogs; recommended for test runs |

## Known limitations

The list for players is in [README.md](README.md) ("Known limitations"); for building and setting up:

- No EA online service (GameSpy was shut down in 2014; the online code is removed). LAN, direct connection, invite codes
  and the relay work between GZH Neo builds.
- The keyboard works through Windows messages instead of DirectInput.
- Direct3D 11 (feature level 11.0) is the default renderer, Vulkan 1.3 the alternative. The graphics effects follow the
  detail level.
- Replays, saved games and network games are not compatible with EA's original game (SSE instead of x87 floating
  point), only between identical GZH Neo builds.
- `setup_assets.py` has so far only been used with the EA app installation; a Steam installation is expected to work
  the same way but has not been tested yet.
