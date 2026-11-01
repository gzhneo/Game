# Game folder: all code and build lists live under Code/. What differs per game is set only by the
# parameters here; Code/Games/Generals/CMakeLists.txt and Code/Games/ZeroHour/CMakeLists.txt differ only in
# their values. Code/Games/GameTargets.cmake defines what is built per game. Next to this file there are only
# GameEdition.cpp (selects the game) and the icon Generals.ico; tools/mdsync/treediff.py --overlap
# enforces that.

# Game folder: GameEdition.cpp and Generals.ico come from here.
set(GAME_SPIEL_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
# Suffix of the per-game target names (GameEdition${GAME_SUFFIX}, ImagePacker${GAME_SUFFIX}, ...).
set(GAME_SUFFIX "MD")
# Name of the game executable.
set(GAME_EXE_NAME "GZHNeoZH")
# Game name in the title of the AVX2 message in App/CpuCheck.cpp ("GZH Neo - <name>").
set(GAME_EDITION_NAME "Zero Hour")

# Game assets: tools/assets/setup_assets.py [--game zh] creates a separate asset folder per game from
# the EA installation (copy plus data fixes); after each build cmake/LinkAssets.cmake links missing
# .big archives, extra files, the Data folder and (Zero Hour only) ZH_Generals from there into the
# exe folder. Existing entries are left alone. The default comes from the environment variable
# GZH_ASSETS_ZH or the machine file (cmake/GzhPfade.cmake); without either the build links nothing.
include("${CMAKE_SOURCE_DIR}/cmake/GzhPfade.cmake")
gzh_machine_path(_gzh_assets GZH_ASSETS_ZH)
set(GENERALSZH_ASSETS_DIR "${_gzh_assets}" CACHE PATH
	"Zero Hour asset folder (like GENERALS_ASSETS_DIR), linked into the exe folder")
set(GAME_ASSETS_DIR "${GENERALSZH_ASSETS_DIR}")

include("${SHARED_CODE_DIR}/Games/GameTargets.cmake")
