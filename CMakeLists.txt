# --- vcpkg toolchain (FFmpeg and other dependencies) --------------------------
# Must be set before project(). vcpkg.json in the repo root drives manifest mode.
# The presets (CMakePresets.json) set the toolchain from $env{VCPKG_ROOT}; this
# fallback only applies to a plain `cmake -S . -B build` without a preset.
# -----------------------------------------------------------------------------
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
	if(DEFINED ENV{VCPKG_ROOT} AND NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
		set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "vcpkg toolchain file (for FFmpeg)")
	else()
		# Fallback: the vcpkg instance shipped with the VS 2026 Build Tools.
		set(CMAKE_TOOLCHAIN_FILE "C:/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/VC/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "vcpkg toolchain file (for FFmpeg) -- falls back to the default VS 2026 Build Tools installation because VCPKG_ROOT is not set")
	endif()
endif()
# Only x64 is supported. Configuring for Win32 or with an x86 triplet stops with an error.
if(CMAKE_GENERATOR_PLATFORM MATCHES "^[Ww][Ii][Nn]32$")
	message(FATAL_ERROR "32-bit build (Win32) is no longer supported. "
		"Use an x64 preset, e.g. cmake --preset x64-debug. Last x86 state: tag x86_Letzter_Stand.")
endif()
if(NOT DEFINED VCPKG_TARGET_TRIPLET)
	set(VCPKG_TARGET_TRIPLET "x64-windows" CACHE STRING "vcpkg target triplet (x64 only)")
endif()
if(VCPKG_TARGET_TRIPLET MATCHES "^x86-")
	message(FATAL_ERROR "vcpkg triplet ${VCPKG_TARGET_TRIPLET}: x86 is no longer supported. "
		"Use x64-windows (an x64 preset).")
endif()

cmake_minimum_required(VERSION 3.20)
project(CnC_Generals_ZeroHour_Modernization CXX)
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
	message(FATAL_ERROR "Only 64-bit (x64) builds are supported, but the compiler produces "
		"${CMAKE_SIZEOF_VOID_P}-byte pointers. Use an x64 preset, e.g. cmake --preset x64-debug.")
endif()

# enable_testing() must be in the root CMakeLists.txt so that ctest run from the
# build root finds every add_test().
enable_testing()

# --- C++20 -------------------------------------------------------------------
# Newer engine parts (job system, clock, logging, thread roles) use std::atomic,
# std::jthread, std::span and <bit>. For cross-checking, the standard can be set
# back to 17: cmake --preset ... -DENGINE_CXX_STANDARD=17
# -----------------------------------------------------------------------------
set(ENGINE_CXX_STANDARD 20 CACHE STRING "C++ standard for the whole build")
set_property(CACHE ENGINE_CXX_STANDARD PROPERTY STRINGS 17 20)
set(CMAKE_CXX_STANDARD ${ENGINE_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
	add_compile_options(/EHsc /W3)
	# Compile the sources of one project in parallel. MSBuild /m only parallelises whole
	# projects, so large targets like GameEngine would otherwise build on one core.
	add_compile_options(/MP)
	# Report the real __cplusplus value (MSVC reports 199711L otherwise, which sends library
	# headers down their C++98 paths). /std:c++20 implies /permissive-, so the whole code base
	# builds in conformance mode.
	add_compile_options(/Zc:__cplusplus)
	# AVX2 code generation for the whole project (minimum CPU is Haswell). No /fp:fast: it
	# would allow FMA contraction and change rounding; with /fp:precise the floating-point
	# results stay identical to SSE2. App/CpuCheck.cpp is built without AVX2 and exits with
	# a message on older CPUs.
	add_compile_options(/arch:AVX2)
	# Leak hunting: ENABLE_MEMORYPOOL_STACKTRACE=ON drops DISABLE_MEMORYPOOL_STACKTRACE, so the leak
# report shows the allocation call stack of every block (about twice the run time, Debug only).
# Use a separate build folder.
option(ENABLE_MEMORYPOOL_STACKTRACE "Allocation call stacks in the leak report (debug diagnostics)" OFF)
add_compile_definitions(
		WIN32
		_WINDOWS
		WIN32_LEAN_AND_MEAN
		NOMINMAX
		# Target Windows 10 (0x0A00), matching the SDK default and the supported
		# platforms. xaudio2.h requires at least _WIN32_WINNT_WIN8.
		_WIN32_WINNT=0x0A00
		WINVER=_WIN32_WINNT
		_CRT_SECURE_NO_WARNINGS
		# Do not flag the POSIX names (stricmp, strdup, getpid, ...) as deprecated (C4996).
		# Affects the warning only, not the generated code.
		_CRT_NONSTDC_NO_DEPRECATE
		# MEMORYPOOL_STACKTRACE (Debug builds, Common/GameMemory.h) records the call stack
		# of every memory block allocation and prints it in the leak report. It costs about
		# twice the run time, so it is off unless ENABLE_MEMORYPOOL_STACKTRACE is set.
		# Debug diagnostics only, no effect on game logic.
		$<$<NOT:$<BOOL:${ENABLE_MEMORYPOOL_STACKTRACE}>>:DISABLE_MEMORYPOOL_STACKTRACE>
	)
	# StackDump.cpp (GetFunctionDetails, DumpExceptionInfo, FillStackAddresses,
	# StackDumpFromAddresses, g_LastErrorDump) is only compiled with _DEBUG or
	# IG_DEBUG_STACKTRACE, but WinMain.cpp and Debug.cpp call these symbols
	# unconditionally. Release therefore defines IG_DEBUG_STACKTRACE, as the original
	# Release configuration did; crash diagnostics stay available in Release.
	add_compile_definitions($<$<CONFIG:Release>:IG_DEBUG_STACKTRACE>)
	# Release also writes a PDB (as the original RTS.dsp did), so minidumps and cdb
	# traces of a Release crash resolve to function names. No effect on codegen.
	# No /OPT:ICF: folding identical functions with different exception handler
	# chains triggered FAST_FAIL_INVALID_EXCEPTION_CHAIN (0xC0000409) on the first
	# C++ exception unwind (INI::prepFile) with 32-bit SEH.
	string(APPEND CMAKE_CXX_FLAGS_RELEASE " /Zi")
	string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE " /DEBUG /OPT:REF")
	# Debug links without the incremental linker: its .ilk file got corrupted repeatedly (LNK1136 or an
	# executable that jumps to address 0 at startup) and only deleting it helped.
	string(APPEND CMAKE_EXE_LINKER_FLAGS_DEBUG " /INCREMENTAL:NO")
	string(APPEND CMAKE_SHARED_LINKER_FLAGS_DEBUG " /INCREMENTAL:NO")

	# --- AddressSanitizer build --------------------------------------------------
	# The "x64-asan" preset (CMakePresets.json) sets ENABLE_ASAN=ON in a separate
	# build folder. /fsanitize=address is incompatible with MSVC runtime checks
	# (/RTC1, default in CMAKE_CXX_FLAGS_DEBUG), so /RTC1 is removed.
	option(ENABLE_ASAN "Build with AddressSanitizer" OFF)
	if(ENABLE_ASAN)
		add_compile_options(/fsanitize=address)
		# MEMORYPOOL_ASAN_BYPASS (Common/System/GameMemory.cpp) disables the memory
		# pool: it carves objects out of GlobalAlloc blobs, where ASan cannot place
		# redzones around single blocks. With the bypass every allocation goes through
		# malloc()/free(), which ASan intercepts. Requires MEMORYPOOL_OVERRIDE_MALLOC
		# to be off (otherwise recursion, see the comment there).
		add_compile_definitions(MEMORYPOOL_ASAN_BYPASS)
		string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
	endif()

	# --- Static analysis ---------------------------------------------------------
	# Optional /analyze build in its own build folder (preset x64-analyze), it takes several
	# times as long as a normal build:
	#     cmake --preset x64-analyze
	#     cmake --build --preset x64-analyze > analyze.log
	# /analyze:WX- keeps analysis warnings as warnings (otherwise the build stops),
	# /analyze:stacksize limits large stack frame reports to real outliers.
	# -----------------------------------------------------------------------------
	option(ENABLE_ANALYZE "Run MSVC /analyze during the build" OFF)
	if(ENABLE_ANALYZE)
		add_compile_options(/analyze /analyze:WX- /analyze:stacksize 100000)
	endif()

	# Warning level per target: /W3 here, /W4 /WX for every target that compiles clean at /W4
	# (ENGINE_STRICT_OPTIONS below). Still at /W3: the MFC tool DebugWindow.
endif()

# --- Tracy profiler -------------------------------------------------------------
# Profile build only (preset x64-profile: ENABLE_TRACY=ON plus the vcpkg manifest
# feature "profiling"). WWDebug passes Tracy::TracyClient and TRACY_ENABLE on as PUBLIC;
# without the option the PROF_* macros from WWDebug/wwtracy.h are empty.
# -----------------------------------------------------------------------------
option(ENABLE_TRACY "Build with the Tracy profiler (preset x64-profile)" OFF)
if(ENABLE_TRACY)
	find_package(Tracy CONFIG REQUIRED)
endif()

# --- Device layers of the renderer -------------------------------------------------
# The render core runs on the device layers built in (Code/Renderer/WW3D2/D3D11, .../Vulkan; gpulayers.h).
# Without a layer its sources, libraries and packages drop out; at least one must be on.
# The Vulkan layer needs Vulkan headers, volk and the Vulkan Memory Allocator as headers and the DirectX
# Shader Compiler as dxcompiler.dll next to the game (loaded at runtime, see Code/App/CMakeLists.txt).
# Found here so the imported targets are visible in every subfolder.
# Preset x64-vulkan (ENABLE_VULKAN=ON) adds the test VulkanShaders, which compiles every shader variant
# the backend builds to SPIR-V with the dxc.exe of the directx-dxc port (tools/vulkan/shader_spirv.py).
# -----------------------------------------------------------------------------
option(GZH_RENDER_D3D11 "Build the Direct3D 11 device layer" ON)
option(GZH_RENDER_VULKAN "Build the Vulkan device layer" ON)
if(NOT GZH_RENDER_D3D11 AND NOT GZH_RENDER_VULKAN)
	message(FATAL_ERROR "GZH_RENDER_D3D11 and GZH_RENDER_VULKAN are both OFF: at least one device layer must be built")
endif()
if(WIN32 AND GZH_RENDER_VULKAN)
	find_package(VulkanHeaders CONFIG REQUIRED)
	find_package(VulkanMemoryAllocator CONFIG REQUIRED)
	find_package(directx-dxc CONFIG REQUIRED)
endif()
option(ENABLE_VULKAN "Register the Vulkan shader test (preset x64-vulkan)" OFF)
if(ENABLE_VULKAN AND NOT GZH_RENDER_VULKAN)
	message(FATAL_ERROR "ENABLE_VULKAN needs GZH_RENDER_VULKAN")
endif()
if(ENABLE_VULKAN)
	find_package(Python3 COMPONENTS Interpreter)
	if(Python3_Interpreter_FOUND)
		# dxc validates its SPIR-V output itself, so no separate spirv-val here.
		add_test(NAME VulkanShaders
			COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/vulkan/shader_spirv.py"
				--dxc "${DIRECTX_DXC_TOOL}" --ohne-val)
	else()
		message(WARNING "Python 3 not found: test VulkanShaders is not registered")
	endif()
endif()

# Warning options of the targets that compile clean at /W4 (with /WX added per target: the
# libraries below, the engine, the game executables, tests and the newer tools). /permissive- is
# listed for clarity only, /std:c++20 implies it for all files.
if(MSVC)
	set(ENGINE_STRICT_OPTIONS "/permissive-;/W4")
else()
	set(ENGINE_STRICT_OPTIONS "-Wall;-Wextra")
endif()

# Each game has a small folder with its parameters, GameEdition.cpp and icon.
set(GENERALS_SPIEL_DIR "${CMAKE_SOURCE_DIR}/Code/Games/Generals")

# Both games build the same engine from Code/; Zero Hour is built by default.
option(BUILD_ZERO_HOUR "Build the Zero Hour (GeneralsMD) targets" ON)
set(GENERALSMD_SPIEL_DIR "${CMAKE_SOURCE_DIR}/Code/Games/ZeroHour")
# Tools for both games (Code/Tools/CMakeLists.txt, included per game via Code/Games/GameTargets.cmake).
# Off by default to keep the game build lean. Defined before the games because both read it.
option(BUILD_TOOLS "Build the tools (Code/Tools) for both games" OFF)

# Install layout for the player setup: cmake --install <build> --config Release --prefix <folder> gives
# Generals/ and ZeroHour/, each with the game, its DLLs, Shaders and ProjectData (Code/App/CMakeLists.txt), and
# the licence files at the top. EA's game data is not part of it. The VC++ runtime is installed next to each
# executable (app-local), so the setup needs no administrator rights for a redistributable.
set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
include(InstallRequiredSystemLibraries)
install(FILES LICENSE.md NOTICE.md ASSET_CREDITS.md README.md DESTINATION .)
# Free content (ground, roads, trees; about 500 MB per game) in the install. Without it the package is small and
# the game uses EA's content only, as with -freeContent ea.
option(GZH_INSTALL_FREE_CONTENT "Install the free content packages (ProjectData/Inhalt)" ON)

# --- Shared libraries ---------------------------------------------------------------------------
# Code/Libraries: WWDebug, WWLib, WWMath, WWSaveLoad, WW3D2, Compression (ZLib via vcpkg plus EA's
# BTree/Huff/RefPack codecs) and Core (Lib/BaseType.h, Lib/trig.h). One target per
# library; GZHNeo and GZHNeoZH link the same targets.
# -----------------------------------------------------------------------------------------------
set(SHARED_CODE_DIR "${CMAKE_SOURCE_DIR}/Code")
set(SHARED_CORE_DIR "${SHARED_CODE_DIR}/Core")
set(SHARED_RENDERER_DIR "${SHARED_CODE_DIR}/Renderer")
set(SHARED_LIB_INCLUDE "${SHARED_CODE_DIR}/Core")

# --- Engine and tool folders ----------------------------------------------------------------------
# GameEngine and GameEngineDevice are built once (below); the tools are built per game.
set(SHARED_ENGINE_DIR "${SHARED_CODE_DIR}/Core/EngineCore")
set(SHARED_GAMEENGINE_DIR "${SHARED_CODE_DIR}/GameEngine")
set(SHARED_PLATFORM_DIR "${SHARED_CODE_DIR}/Platform")
set(SHARED_TOOLS_DIR "${SHARED_CODE_DIR}/Tools")
# Project game data (HLSL shaders, GameLODOverride.ini), shared by both games. Copied next to the
# executables by post-build steps (Code/App, Tools).
set(SHARED_DATA_DIR "${CMAKE_SOURCE_DIR}/Data")

add_subdirectory("${SHARED_CORE_DIR}/WWDebug")
add_subdirectory("${SHARED_CORE_DIR}/WWLib")
add_subdirectory("${SHARED_CORE_DIR}/WWMath")
add_subdirectory("${SHARED_CORE_DIR}/WWSaveLoad")
add_subdirectory("${SHARED_CODE_DIR}/Core/Compression")

# Rendering (WW3D2, Direct3D 11).
add_subdirectory("${SHARED_RENDERER_DIR}/WW3D2")

# --- /W4 /WX per library --------------------------------------------------------------------------
# Libraries that compile warning-free at /W4 get /W4 /WX so they stay clean. Deliberate exceptions
# are documented at the spot (#pragma warning) or in the library's CMakeLists.txt. Everything else
# stays at /W3.
set(ENGINE_STRICT_LIBRARIES
	WWLib
	WWMath
	WWDebug
	WWSaveLoad
	Compression
	WW3D2
)
if(MSVC)
	foreach(_strict_ziel IN LISTS ENGINE_STRICT_LIBRARIES)
		target_compile_options(${_strict_ziel} PRIVATE ${ENGINE_STRICT_OPTIONS} /WX)
	endforeach()
endif()

# Unit tests of the render backend that need no GPU. Not tied to a game, so added once here.
add_subdirectory("${SHARED_CODE_DIR}/Tests/RenderUnitTests")

# --- Engine ---------------------------------------------------------------------------------------
# EngineCore, GameEngineCommon, GameEngineGameClient, GameEngineGameLogic, GameEngineGameNetwork and
# GameEngineDevice are game-neutral (GameEdition.cpp is linked into each executable) and built once;
# GZHNeo, GZHNeoZH and the tools of both games link the same targets.
# EngineCore: the engine building blocks without engine headers (Code/Core/EngineCore), used by the engine,
# the tests and the small tools.
add_subdirectory("${SHARED_ENGINE_DIR}" Code/Engine)
add_subdirectory("${SHARED_GAMEENGINE_DIR}" Code/GameEngine)
add_subdirectory("${SHARED_PLATFORM_DIR}" Code/GameEngineDevice)

# Engine libraries that compile warning-free at /W4 get /W4 /WX as well (same rule as above, the targets
# only exist from here on). The rest of the engine stays at /W3 until its warnings are worked off.
set(ENGINE_STRICT_ENGINE_LIBRARIES
	GameEngineGameNetwork
	GameEngineDevice
	GameEngineGameClient
	GameEngineCommon
	GameEngineGameLogic
)
if(MSVC)
	foreach(_strict_ziel IN LISTS ENGINE_STRICT_ENGINE_LIBRARIES)
		target_compile_options(${_strict_ziel} PRIVATE ${ENGINE_STRICT_OPTIONS} /WX)
	endforeach()
endif()

# Tests are game-neutral and built once.
add_subdirectory("${SHARED_CODE_DIR}/Tests/UnitTests" Code/Tests/UnitTests)
add_subdirectory("${SHARED_CODE_DIR}/Tests/EngineTests" Code/Tests/EngineTests)

# Map model of the new map editor and its tests: game-neutral, no engine, built once.
add_subdirectory("${SHARED_TOOLS_DIR}/Editors/MapEditor" Code/Tools/MapEditor)
add_subdirectory("${SHARED_CODE_DIR}/Tests/MapEditorTests" Code/Tests/MapEditorTests)

# Layout model of the GUI editor and its tests: game-neutral, no engine, built once.
add_subdirectory("${SHARED_TOOLS_DIR}/Editors/GuiEditor" Code/Tools/GuiEditor)
add_subdirectory("${SHARED_CODE_DIR}/Tests/GuiEditorTests" Code/Tests/GuiEditorTests)

# Translation tool Babylon (database, sheets, game files) and its tests: game-neutral, no engine, built
# once. The Babylon application comes with the map editor's application build (Qt).
add_subdirectory("${SHARED_TOOLS_DIR}/Editors/Babylon" Code/Tools/Babylon)
add_subdirectory("${SHARED_CODE_DIR}/Tests/BabylonTests" Code/Tests/BabylonTests)

# Rendezvous server for network games: engine building blocks only, built once.
add_subdirectory("${SHARED_TOOLS_DIR}/Server/Rendezvous" Code/Tools/Rendezvous)

# File system comparison tool (-dateiIndexAusgeben): engine building blocks only, built once.
add_subdirectory("${SHARED_TOOLS_DIR}/Diagnostics/VfsDump" Code/Tools/VfsDump)

# Setup program of the player package: finds the installed games and copies their data. Windows only.
if(WIN32)
	add_subdirectory("${SHARED_TOOLS_DIR}/Setup" Code/Tools/Setup)
endif()

# --- The games ----------------------------------------------------------------------------------
# Each game has a CMakeLists.txt in <Game>/Spiel that only sets parameters (target suffix, exe name,
# assets, ...) and includes Code/Games/GameTargets.cmake, which builds GameEdition, the game and the tools.
# The build subfolder is given explicitly as <Build>/Generals/Code or <Build>/GeneralsMD/Code, which
# test tools and the asset junctions in the exe folders rely on. Zero Hour targets carry the suffix
# "MD" (GameEditionMD, ImagePackerMD, ...); the game executable is GZHNeoZH.
# -----------------------------------------------------------------------------------------------
add_subdirectory("${GENERALS_SPIEL_DIR}" Generals/Code)

# --- Replay determinism ---------------------------------------------------------
# Plays every reference replay in tests/replays/<arch> (each .rep with a matching .crc) with -runner
# and compares the CRC history. Needs the game assets in the exe folder and a free game mutex (no
# GZHNeo.exe running in parallel), hence the label "assets": ctest -L assets, or ctest -LE assets to skip.
# Debug builds get -ignoreAsserts, otherwise assert dialogs block. Replays are bound to the
# architecture, so the recordings live under tests/replays/x64.
# -----------------------------------------------------------------------------
find_package(Python3 COMPONENTS Interpreter)
set(REFERENCE_REPLAY_ARCH "x64")
if(Python3_Interpreter_FOUND)
	# Generous time limits: a long replay (Twilight Flame, 7 AIs, 36000 frames) can take well over an
	# hour in a Debug build on a loaded machine.
	add_test(NAME ReplayDeterminism
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/determinism/runner.py check
			--exe $<TARGET_FILE:GZHNeo> --all ${CMAKE_SOURCE_DIR}/tests/replays/${REFERENCE_REPLAY_ARCH}
			--timeout 9000 $<$<CONFIG:Debug>:--extra> $<$<CONFIG:Debug>:-ignoreAsserts>)
	set_tests_properties(ReplayDeterminism PROPERTIES LABELS "assets" TIMEOUT 18000)
endif()
# Reference replays with the new AI (AICommanderPlayer), kept apart from the classic ones: the classic
# replays must never change, these are re-recorded on every intended change of the new AI.
file(GLOB NEUEKI_REFERENCE_REPLAYS "${CMAKE_SOURCE_DIR}/tests/replays/neueki/${REFERENCE_REPLAY_ARCH}/*.rep")
if(Python3_Interpreter_FOUND AND NEUEKI_REFERENCE_REPLAYS)
	add_test(NAME ReplayNeueKI
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/determinism/runner.py check
			--exe $<TARGET_FILE:GZHNeo> --all ${CMAKE_SOURCE_DIR}/tests/replays/neueki/${REFERENCE_REPLAY_ARCH}
			--timeout 3600 $<$<CONFIG:Debug>:--extra> $<$<CONFIG:Debug>:-ignoreAsserts>)
	set_tests_properties(ReplayNeueKI PROPERTIES LABELS "assets" TIMEOUT 7200)
endif()

# --- Zero Hour (option BUILD_ZERO_HOUR, on by default) ------------------------
if(BUILD_ZERO_HOUR)
	add_subdirectory("${GENERALSMD_SPIEL_DIR}" GeneralsMD/Code)

	# --- Fuzzing targets --------------------------------------------------------
	# Off by default; -DENABLE_FUZZERS=ON builds them (under MSVC with libFuzzer and ASan).
	# -----------------------------------------------------------------------
	option(ENABLE_FUZZERS "Build the fuzzing targets" OFF)
	if(ENABLE_FUZZERS)
		add_subdirectory("${CMAKE_SOURCE_DIR}/tools/fuzz" fuzz)
	endif()

	# --- Replay determinism (Zero Hour) --------------------------------------------
	# Like ReplayDeterminism above, with GZHNeoZH.exe and the reference replays in
	# tests/replays/zh/<arch>. The test only exists if recordings are present there.
	# -----------------------------------------------------------------------
	file(GLOB GENERALSZH_REFERENCE_REPLAYS "${CMAKE_SOURCE_DIR}/tests/replays/zh/${REFERENCE_REPLAY_ARCH}/*.rep")
	if(Python3_Interpreter_FOUND AND GENERALSZH_REFERENCE_REPLAYS)
		add_test(NAME ReplayDeterminismZH
			COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/determinism/runner.py check
				--exe $<TARGET_FILE:GZHNeoZH> --all ${CMAKE_SOURCE_DIR}/tests/replays/zh/${REFERENCE_REPLAY_ARCH}
				--timeout 9000 $<$<CONFIG:Debug>:--extra> $<$<CONFIG:Debug>:-ignoreAsserts>)
		set_tests_properties(ReplayDeterminismZH PROPERTIES LABELS "assets" TIMEOUT 18000)
	endif()
	# Reference replays with the new AI for Zero Hour (see ReplayNeueKI above).
	file(GLOB NEUEKI_ZH_REFERENCE_REPLAYS "${CMAKE_SOURCE_DIR}/tests/replays/neueki/zh/${REFERENCE_REPLAY_ARCH}/*.rep")
	if(Python3_Interpreter_FOUND AND NEUEKI_ZH_REFERENCE_REPLAYS)
		add_test(NAME ReplayNeueKIZH
			COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/determinism/runner.py check
				--exe $<TARGET_FILE:GZHNeoZH> --all ${CMAKE_SOURCE_DIR}/tests/replays/neueki/zh/${REFERENCE_REPLAY_ARCH}
				--timeout 3600 $<$<CONFIG:Debug>:--extra> $<$<CONFIG:Debug>:-ignoreAsserts>)
		set_tests_properties(ReplayNeueKIZH PROPERTIES LABELS "assets" TIMEOUT 7200)
	endif()
endif()

# --- Map editor defaults ------------------------------------------------------------------------
# The editor offers the asset folders and game programs of this build as defaults; they are known
# only after the game folders above, so the definitions are set here.
if(TARGET MapEditor)
	target_compile_definitions(MapEditor PRIVATE
		MAPEDITOR_DEFAULT_DATA_GENERALS="${GENERALS_ASSETS_DIR}"
		MAPEDITOR_DEFAULT_DATA_ZERO_HOUR="${GENERALSZH_ASSETS_DIR}")
	# "Test in game" starts the build's own game programs unless the settings name others.
	if(TARGET GZHNeo)
		target_compile_definitions(MapEditor PRIVATE MAPEDITOR_DEFAULT_GAME_GENERALS="$<TARGET_FILE:GZHNeo>")
	endif()
	if(TARGET GZHNeoZH)
		target_compile_definitions(MapEditor PRIVATE MAPEDITOR_DEFAULT_GAME_ZERO_HOUR="$<TARGET_FILE:GZHNeoZH>")
	endif()
endif()
