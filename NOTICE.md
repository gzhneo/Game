# NOTICE

GZH Neo is a modified version of the Command & Conquer Generals / Zero Hour source code released by
Electronic Arts under the GNU GPL v3 ([LICENSE.md](LICENSE.md)) with additional terms (section
"Additional terms of Electronic Arts" below).

## Trademarks

"Command & Conquer" and "Generals" are trademarks of Electronic Arts Inc. They are used here only to
describe which game this code belongs to. GZH Neo is not affiliated with, endorsed by or sponsored by
Electronic Arts.

The program icon (`Code/Games/*/Generals.ico`) is the one from EA's source release; GZH Neo uses it
to show which game it is based on.

## Source file headers

- Files from EA's source release carry EA's copyright notice and GPL header. Files that GZH Neo has
  changed additionally carry the line `Modified by GZH Neo, Copyright (C) 2026 GZH Neo`.
- Files written for GZH Neo carry `Copyright (C) 2026 GZH Neo` and state that they are part of a
  modified version of Command & Conquer Generals / Zero Hour, licensed under the GNU GPL v3 or later.
- The tool and test scripts under `tools/` and `tests/` carry the same GZH Neo header. Build scripts
  (CMake) and data files carry no header; they are covered by the same license.

## Changes since EA's release

This notice belongs to GZH Neo 2.0-alpha-2 (the first public version was 2.0-alpha-1). Modifications since 2026, in broad
terms:

- Builds with a current MSVC toolchain as conforming C++20 (`/std:c++20 /permissive-`); 64-bit (x64)
  only.
- Generals and Zero Hour share one merged code base; the engine is built once and linked by both
  game executables.
- Direct3D 11 renderer behind a render device interface, replacing Direct3D 8/9 and D3DX; HLSL shaders
  compiled at runtime.
- Optional Vulkan renderer (`-vulkan`) next to Direct3D 11, sharing one render core; HLSL shaders are
  compiled to SPIR-V at build time.
- Graphics additions: shadow maps, ambient occlusion, anti-aliasing, new water, terrain detail and
  rock layers, widescreen support, frame rate decoupled from the logic tick, higher-resolution texture
  tiers.
- Audio through XAudio2 instead of the Miles Sound System; video through FFmpeg instead of Bink.
- Standard C++ library instead of STLport; no Windows registry; keyboard input without DirectInput.
- Online services removed (GameSpy, patch download, copy protection). Network play through a new
  session layer (LAN lobby, direct connection, invite codes, relay) with encrypted and authenticated
  packets, and a small rendezvous server (`Code/Tools/Server/Rendezvous`) that anyone can run.
- Mods load as folders, `.big` or `.zip` archives and can be chosen in the options menu; settings
  are kept in `Settings.ini` and `State.ini` in a user folder of GZH Neo's own
  (`Documents\GZH Neo\<game>`), not in the registry.
- Free content from CC0 photo scans (terrain, roads, water, trees), used by default next to EA's
  game data ([ASSET_CREDITS.md](ASSET_CREDITS.md)).
- New map editor and GUI editor (Qt 6) instead of WorldBuilder, ParticleEditor and GUIEdit; the
  translation tool Babylon without Excel.
- Program names `GZHNeo.exe` and `GZHNeoZH.exe`, window titles and version 2.0-alpha-2 mark the
  program as GZH Neo, not as EA's original game.
- A Windows setup (Inno Setup) with a setup program that finds the player's installation of Generals and
  Zero Hour (Steam, EA app, Origin, disc) and copies the game data, reading the installation only.
- Engine building blocks: central clock, thread-safe log, job system, logic access guard, crash
  reports with minidump, hang watchdog.
- A new, selectable computer AI next to the classic one; developer test helpers.
- Numerous bug fixes in the original code; unit tests, replay determinism checks and image
  comparison tests.

## Third-party components

These libraries are fetched at build time through vcpkg (see `vcpkg.json`) and shipped as DLLs next
to the executables. They are not part of this repository. The Windows setup puts their licence texts and an overview
(with the exact FFmpeg version and the vcpkg recipe it was built from) into its folder `Licenses`.

| Component | Use | License |
|---|---|---|
| [FFmpeg](https://ffmpeg.org) | video playback | LGPL 2.1 or later (default vcpkg build, no GPL-only parts enabled) |
| [DirectXTex](https://github.com/microsoft/DirectXTex) | texture loading and conversion | MIT |
| [zlib](https://zlib.net) | data compression | zlib License |
| [Tracy](https://github.com/wolfpld/tracy) | profiler, only in profiling builds (preset `x64-profile`, vcpkg feature `profiling`) | BSD 3-Clause |
| [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | Vulkan API declarations (optional Vulkan renderer) | Apache 2.0 (headers only, compiled in) |
| [volk](https://github.com/zeux/volk) | Vulkan function loader (loads `vulkan-1.dll` at runtime) | MIT |
| [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | GPU memory management of the Vulkan renderer | MIT (headers only, compiled in) |
| [DirectX Shader Compiler](https://github.com/microsoft/DirectXShaderCompiler) | compiles the HLSL shaders to SPIR-V; `dxcompiler.dll` is loaded at runtime by the Vulkan renderer and shipped next to the game | University of Illinois/NCSA Open Source License (LLVM Release License) |
| [Qt 6](https://www.qt.io) (`qtbase`, `qtsvg`) | map editor `Code/Tools/Editors/MapEditor/App`, only with the vcpkg feature `editor`; Qt DLLs and plugins shipped next to the editor | LGPL 3.0 only, used under the LGPL (Qt is dual-licensed LGPL-3.0 / GPL-2.0 / GPL-3.0 / commercial); dynamically linked |

Included in this repository (one cast added in `monocypher.c` for a compiler warning, see its README):

| Component | Use | License |
|---|---|---|
| [Monocypher](https://monocypher.org) 4.0.2 (`Code/Core/Monocypher`) | key exchange and authenticated encryption of network game packets | BSD 2-Clause or CC0 1.0, at your choice (`LICENCE.md` next to the source) |

Some shaders implement published rendering methods in code of their own; no third-party source is
included: FXAA (T. Lottes), contrast adaptive sharpening after AMD FidelityFX CAS, and ground truth
ambient occlusion (Jimenez et al. 2016).

The following system components are used from the Windows SDK and Windows itself and are not
redistributed: Direct3D 11, DXGI, `d3dcompiler_47`, XAudio2, DbgHelp. The legacy Debug Window tool uses
MFC from Visual Studio.

The Windows setup ships the Microsoft Visual C++ runtime (`msvcp140*.dll`, `vcruntime140*.dll`, `concrt140.dll`) next
to the games as distributable code of Microsoft Visual Studio, under its terms; the setup program
`GZHNeoEinrichtung.exe` links it statically. The setup itself is made with
[Inno Setup](https://jrsoftware.org/isinfo.php).

Non-EA art assets (models, textures) and their licenses and attributions are listed in
[ASSET_CREDITS.md](ASSET_CREDITS.md).

## Game data

EA's game data (models, textures, sounds, maps, videos) is not included. Running the game requires
your own legally obtained copy of Command & Conquer Generals and/or Zero Hour.

## Additional terms of Electronic Arts (GPLv3 section 7)

These are EA's additional terms, verbatim from EA's source release; "the GPL V3 license below" in EA's text is the
license in [LICENSE.md](LICENSE.md), kept there as the plain GPL text.

Electronic Arts Inc. has released the source code for Command & Conquer Generals
and Command & Conquer Generals Zero Hour under the GPL V3 license below, with
additional terms at the bottom.

No trademark or publicity rights are granted. This license does NOT give you
any right, title or interest in "Command & Conquer" or any other Electronic Arts
trademark. You may not distribute any modification of this program using any
Electronic Arts trademark or claim any affiliation or association with
Electronic Arts Inc. or its affiliates or their employees.

Any propagation or conveyance of this program must include this copyright
notice and these terms.

If you convey this program (or any modifications of it) and assume
contractual liability for the program to recipients of it, you agree to
indemnify Electronic Arts for any liability that those contractual
assumptions impose on Electronic Arts.

You may not misrepresent the origins of this program; modified versions of
the program must be marked as such and not identified as the original program.

This disclaimer supplements the one included in the General Public License.
TO THE FULLEST EXTENT PERMISSIBLE UNDER APPLICABLE LAW, THIS PROGRAM IS
PROVIDED TO YOU "AS IS," WITH ALL FAULTS, WITHOUT WARRANTY OF ANY KIND, AND
YOUR USE IS AT YOUR SOLE RISK. THE ENTIRE RISK OF SATISFACTORY QUALITY AND
PERFORMANCE RESIDES WITH YOU. ELECTRONIC ARTS DISCLAIMS ANY AND ALL EXPRESS,
IMPLIED OR STATUTORY WARRANTIES, INCLUDING IMPLIED WARRANTIES OF
MERCHANTABILITY, SATISFACTORY QUALITY, FITNESS FOR A PARTICULAR PURPOSE,
NONINFRINGEMENT OF THIRD PARTY RIGHTS, AND WARRANTIES (IF ANY) ARISING FROM A
COURSE OF DEALING, USAGE, OR TRADE PRACTICE. ELECTRONIC ARTS DOES NOT WARRANT
AGAINST INTERFERENCE WITH YOUR ENJOYMENT OF THE PROGRAM; THAT THE PROGRAM WILL
MEET YOUR REQUIREMENTS; THAT OPERATION OF THE PROGRAM WILL BE UNINTERRUPTED OR
ERROR-FREE, OR THAT THE PROGRAM WILL BE COMPATIBLE WITH THIRD PARTY SOFTWARE
OR THAT ANY ERRORS IN THE PROGRAM WILL BE CORRECTED. NO ORAL OR WRITTEN ADVICE
PROVIDED BY ELECTRONIC ARTS OR ANY AUTHORIZED REPRESENTATIVE SHALL CREATE A
WARRANTY. SOME JURISDICTIONS DO NOT ALLOW THE EXCLUSION OF OR LIMITATIONS ON
IMPLIED WARRANTIES OR THE LIMITATIONS ON THE APPLICABLE STATUTORY RIGHTS OF A
CONSUMER, SO SOME OR ALL OF THE ABOVE EXCLUSIONS AND LIMITATIONS MAY NOT APPLY
TO YOU.

## Additional term for GZH Neo code (GPLv3 section 7 b)

As an additional term under section 7 b of the GNU GPL v3, the following applies to the parts of this
program authored by GZH Neo:

> Modified versions and forks must name GZH Neo as their origin in a reasonable place, such as the
> README, an about/info dialog, or a NOTICE file.

This term applies only to the GZH Neo portions. EA's original code remains under the terms set by
Electronic Arts (previous section), which already require that modified versions be marked
as such and not be identified as the original program.
