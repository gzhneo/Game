# GZH Neo

**A modernised Command & Conquer Generals and Zero Hour, built from EA's GPL source release.**

GZH Neo is a modernised version of the source code of *Command & Conquer Generals* and its expansion *Zero Hour*
(both 2003), which Electronic Arts released under the GNU GPL v3 in 2025
([electronicarts/CnC_Generals_Zero_Hour](https://github.com/electronicarts/CnC_Generals_Zero_Hour)). Both games build
with a current Visual Studio toolchain and run well on today's Windows PCs. The outdated third-party libraries of the
original (DirectX 8, STLport, Miles Sound System, Bink Video, GameSpy, …) are replaced, the code is conforming C++20
for 64 bit, and the renderer is Direct3D 11 with an optional Vulkan back end.

> **Not affiliated with EA.** GZH Neo is an independent hobby project. It is not affiliated with, endorsed by or
> sponsored by Electronic Arts. "Command & Conquer" and "Generals" are trademarks of Electronic Arts Inc. and are used
> here only to say which game this code belongs to. For EA's unmodified source release see
> [electronicarts/CnC_Generals_Zero_Hour](https://github.com/electronicarts/CnC_Generals_Zero_Hour).

**You need your own copy of the game.** This repository contains source code only — no game data (models, textures,
sounds, maps, videos) and no ready-made programs. To play, you need a legally acquired copy of Generals and/or Zero
Hour, for example from *The Ultimate Collection* on the
[EA app](https://www.ea.com/en-gb/games/command-and-conquer/command-and-conquer-the-ultimate-collection/buy/pc) or
[Steam](https://store.steampowered.com/bundle/39394/Command__Conquer_The_Ultimate_Collection/).

## Status: 2.0-alpha-2

This is the second public version of GZH Neo, **2.0-alpha-2**, after 2.0-alpha-1 (November 2026, source code
only). The "2" marks the step from EA's last patches, Generals
1.08 and Zero Hour 1.04. *Alpha* means: both games build, run and are playable — skirmish, campaign and network games —
but not everything is finished. Both games and all automated tests have passed on several different computers, but not
yet on the wide range of hardware that players have. Expect rough edges and please
report them (see "Reporting problems" below).

The programs are `GZHNeo.exe` (Generals) and `GZHNeoZH.exe` (Zero Hour); the version is shown at the bottom left of the
main menu and on the info page of the options menu (page *Game*, *Info ...*). This version is published as **source
code** and as a **Windows setup** on the releases page, which installs both games and copies the game data from your own
installation (see "Quick start"). You can also build the programs or the setup yourself.

This repository, [github.com/gzhneo/Game](https://github.com/gzhneo/Game), holds the game itself. Further projects
around it, such as mods, may follow as repositories of their own under [github.com/gzhneo](https://github.com/gzhneo).

## Features

### Foundation

- **One code base for both games.** Generals and Zero Hour share one source tree and one engine; Generals keeps its own
  rules where it differs from Zero Hour.
- **Conforming C++20, 64 bit only**, with strict MSVC settings (`/std:c++20 /permissive-`, warning level 4 as errors).
- **Modern replacements** for the libraries of 2003: FFmpeg instead of Bink for videos, XAudio2 instead of Miles for
  sound (music straight from MP3), the standard library instead of STLport, no D3DX, no DirectInput, no registry.
- **Settings files** (`Settings.ini`, `State.ini`) in the user folder instead of registry entries; an existing
  `Options.ini` is taken over on the first start.
- **Mods** load as folders, `.big` or `.zip` archives (`-mod`) and can be switched on, off and ordered in the options
  menu. Lobby, replays and saved games check that everybody uses the same mods and game data.
- **Network play without GameSpy:** a new session layer with LAN lobby, direct connection, invite codes and a relay;
  packets are encrypted and authenticated. Games over the internet use a small rendezvous server (`gzh-rendezvous`,
  part of the source code) that anyone can run for themselves or their community
  ([Code/Tools/Server/Rendezvous/README.md](Code/Tools/Server/Rendezvous/README.md)); the game is told about it with `-treffpunkt` or
  `<user folder>\Rendezvous.txt`.
- **Many bugs of the original fixed:** memory errors and remotely triggerable faults in the network code, memory leaks,
  crashes on exit, a 1997 typo in the vector maths, broken German speech, errors in the game data.

### Graphics

- **Direct3D 11** is the default renderer; **Vulkan 1.3** is available as an alternative (`-vulkan` or a checkbox in
  the options menu). Both share one render core; the shaders are HLSL source in the repository.
- **Effects by detail level:** from *Medium* FXAA, sharpening, soft particles, colour grading, real **shadow maps**,
  a detail layer for the terrain and the **new water** (turbidity, Fresnel reflection, waves, sky reflection, foam,
  flowing rivers); from *High* ambient occlusion, a **rock layer on steep mountain slopes**, bloom, heat haze, dynamic
  lights and **HDR rendering with tone mapping**; *Very high* adds relief mapping and tessellation. The *Low* level is
  tailored to the minimum hardware: no post-processing, 75 % render resolution. MSAA is available on every level.
- **More effects behind switches** (`-effects`, settings file): supersampling, soft and contact shadows, shadow
  cascades, GTAO, temporal anti-aliasing, screen-space reflections on water, de-banding, per-pixel lighting for terrain
  and models (with normal maps), height fog and sun shafts, snow on flat ground.
- **Modern displays:** correct 16:9, 21:9 and 32:9; the frame rate is decoupled from the 30 Hz game logic (with
  interpolation and an optional frame cap); full screen as a borderless window; zoom out further on large monitors.
- **Higher-resolution textures:** HD and UHD tiers that every player upscales locally from their own installation and
  compresses as BC7, either as tier folders or as one texture package; team colours are applied in the shader.
- **Free content:** terrain, road and water textures from CC0 photo scans are in the repository and used by default;
  rocks and trees are being replaced step by step by freely licensed models (`ASSET_CREDITS.md`). `-freeContent ea`
  returns to EA's originals.

### Game

- **A new computer AI**, selectable next to the classic one in skirmish ("Brutal (new AI)" and the easier levels): it
  cooperates with allies, attacks in a coordinated way, defends its base and expands its economy. In Zero Hour it wins
  about 70 % of games against the classic Brutal AI.
- **Spectating** AI-only games, a **grace period** as a game option, a reworked options menu.

### Tools and tests

- A new **map editor** (Qt 6) and a new **GUI editor** replace WorldBuilder, ParticleEditor and GUIEdit; the
  translation tool **Babylon** works with `.xlsx`, `.ods` or `.csv` instead of Excel; Image Packer, Map Cache Builder
  and Debug Window build again.
- Unit tests, replay determinism checks, image comparisons along camera paths, leak search, AddressSanitizer,
  `/analyze` and fuzzers for archives, maps, INI files, network sessions, models and textures.

## System requirements

| | Minimum | Notes |
|---|---|---|
| Operating system | Windows 10 (64-bit) or Windows 11 | XAudio2 and `d3dcompiler_47` are part of Windows. |
| Processor | x86-64 with **AVX2**, four cores | Reference: Intel Core i7-4770 (2013); AMD from Excavator (2015) or any Ryzen. Without AVX2 the game exits with a message. |
| Memory | 4 GB | The game itself needs less than 1 GB. |
| Graphics | **Direct3D 11** (feature level 11.0); optionally Vulkan 1.3 | Reference: Intel HD Graphics 4600. Any graphics card of the last ten years will do, integrated ones too. |
| Display | 800x600 and up | 4:3, 16:10, 16:9, 21:9 and 32:9. |
| Disk | about 3 to 4 GB | Copy of the game data (2.7 GB) plus programs (0.1 GB, with the free textures 1.1 GB); HD/UHD textures need about 7 to 10 GB more per game. |
| Runtime | Microsoft Visual C++ v14 runtime (x64) | The setup brings it along next to the games. Self-built programs need the Redistributable, at least the version of the build tools used; it is already installed on every computer with Visual Studio or its Build Tools, see [BUILD.md](BUILD.md), "Running on another computer". FFmpeg, DirectXTex and zlib come as DLLs next to the program. |
| Game data | your own copy of Generals and/or Zero Hour | EA app or Steam (*The Ultimate Collection*). German is tested, English by spot checks, other languages are untested. |

## Quick start

### With the setup

`GZHNeo-<version>-Setup.exe` installs GZH Neo for the current user, without administrator rights, to
`%LOCALAPPDATA%\Programs\GZH Neo`. If the releases page offers it, download it; otherwise build it from the source
(below, then `cmake --build build-x64 --config Release --target GZHNeoSetup` with [Inno Setup 6](https://jrsoftware.org/isinfo.php)
installed; details in [BUILD.md](BUILD.md), "Player package").

Releases are built by GitHub Actions from the tagged commit (`.github/workflows/release.yml`). Besides the two setups each
release carries `SHA256SUMS.txt` to check the downloads and the source code of the FFmpeg DLLs in the setup with the
vcpkg port that built them (FFmpeg is LGPL 2.1).

1. **Run the setup.** At the end it opens *Set up game data*: it finds your installation of Generals and Zero Hour
   (Steam, EA app, Origin or disc), lets you pick another folder, and copies the game data (about 2.7 GB) into the
   program folder. Your installation is only read, never changed. You can repeat this at any time from the Start menu
   (*GZH Neo*, *Set up game data*).
2. **Start** *GZH Neo - Generals* or *GZH Neo - Zero Hour* from the Start menu.

The setup comes in two sizes: about 550 MB with the free ground, road and tree textures, and about 50 MB without them
(`-NoFreeContent`; the games then use EA's textures only). Uninstalling removes the program folder including the copied
game data; settings and saved games in `Documents\GZH Neo` stay.

### Building from source

1. **Install the build tools:** Visual Studio 2026 Build Tools with "Desktop development with C++", the Windows SDK,
   CMake and vcpkg (all part of the Build Tools), and Python 3.
2. **Copy the game data once** into a folder of its own per game; your installation is only read, never changed:

   ```
   python tools/assets/setup_assets.py --game generals --source "<Generals folder>" --dest "<Generals copy>"
   python tools/assets/setup_assets.py --game zh --source "<Zero Hour folder>" --dest "<Zero Hour copy>"
   ```

3. **Configure and build** in the repository folder, naming the two copies (the first configure fetches FFmpeg,
   DirectXTex and zlib through vcpkg, which takes about 15 minutes); the build links the game data next to the
   programs:

   ```
   cmake --preset x64-debug -DGENERALS_ASSETS_DIR="<Generals copy>" -DGENERALSZH_ASSETS_DIR="<Zero Hour copy>"
   cmake --build --preset x64-release
   ```

4. **Start**, for example `build-x64\GeneralsMD\Code\Main\Release\GZHNeoZH.exe -win`.

Details, the start-up switches and the texture tiers: [BUILD.md](BUILD.md).

## Known limitations

- **No central online service** (EA's GameSpy was shut down in 2014, and GZH Neo does not host a replacement). Instead
  you play over a LAN (also over a VPN), by direct connection, with an invite code, or through a relay on a
  rendezvous server that you or your community run yourselves. This works between GZH Neo builds only. LAN games are
  tested well; direct connections and the relay have seen little testing on real internet connections.
- **Not compatible with the original game:** GZH Neo computes with SSE floating point instead of the x87 arithmetic of
  the 2003 compiler, so network games, replays and saved games only work between identical GZH Neo builds.
- **HD/UHD textures are not shipped;** every player generates them from their own installation
  (`tools/assets/build_texture_tiers.py`).
- **The setup is new:** it has been tested on the developer's computer with the EA app, Steam and the English discs as
  source, not yet on a fresh computer. It is not code-signed, so Windows SmartScreen warns when you start it the
  first time ("More info", "Run anyway").
- **Only 64-bit Windows** with a processor that supports AVX2.
- **Languages:** German is tested, English by spot checks, other languages are untested.
- **Testing is still thin in places:** the campaigns and saving and loading have not yet been played through completely
  for every faction. The game data has been set up from the German EA app and Steam versions and from the English
  discs; the English EA app and Steam versions are untested.
- Rocks, trees and other nature objects are only partly replaced by free models; where none exists yet, EA's model is
  used.

## Reporting problems

Please report bugs as an issue at [github.com/gzhneo/Game](https://github.com/gzhneo/Game/issues). Helpful are the version
(main menu, bottom left), which game, the detail level and renderer (Direct3D 11 or Vulkan), your graphics card and what
you did before the problem. After a crash or a hang the game writes a minidump, the log and the replay of the running game
to `Crashes\<timestamp>\` in the program folder (next to `GZHNeo.exe` or `GZHNeoZH.exe`); please attach them. Replays
and saved games only work with the same build, so name the commit you built from.

## Plans

Ideas for later versions, in no fixed order and without a promise:

- **Linux:** support through the Vulkan renderer, first as Wine/Proton, later perhaps natively.
- **Better computer opponent:** further work on the new AI next to the classic one.
- **Better graphics on discrete graphics cards:** use more of what Direct3D 11 and Vulkan offer on the high and very high
  detail levels; the low level stays the minimum option.
- **Internet play:** more testing of direct connections and the relay on real networks, and community rendezvous servers.
- **More languages:** support for further languages is planned.
- **Performance:** further optimisations, also for weaker computers.

## Dependencies

Fetched automatically through vcpkg (`vcpkg.json`), no manual installation: FFmpeg, DirectXTex, zlib, and for the
Vulkan renderer Vulkan-Headers, volk, Vulkan Memory Allocator and the DirectX Shader Compiler; Qt 6 for the map editor.
Monocypher is included in the source tree. Direct3D 11, DXGI and XAudio2 come with Windows and the Windows SDK. Licences
of all components: [NOTICE.md](NOTICE.md); the setup puts the licence texts of the DLLs it ships into `Licenses\`.

None of the SDKs that EA's original build asked for (DirectX 8 SDK, STLport, Bink, Miles Sound System, GameSpy SDK,
SafeDisc API, NVASM, BYTEmark, 3ds Max SDK, LZH-Light) is needed.

## Licence

GZH Neo is licensed like [EA's source release](https://github.com/electronicarts/CnC_Generals_Zero_Hour) under the
**GNU GPL v3** ([LICENSE.md](LICENSE.md)) **with additional terms** ([NOTICE.md](NOTICE.md)). NOTICE.md holds EA's
additional terms, the changes since EA's release, the file headers, the third-party components and an additional term
for the GZH Neo code: modified versions and forks name GZH Neo as their
origin. Third-party art assets (CC0, CC BY) with origin and licence are listed in
[ASSET_CREDITS.md](ASSET_CREDITS.md). EA's game data is not part of this repository.
