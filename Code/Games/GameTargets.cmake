# Per-game build: what is built once for each game.
#
# Included by Code/Games/Generals/CMakeLists.txt and Code/Games/ZeroHour/CMakeLists.txt after they have set
# their parameters (GAME_SPIEL_DIR, GAME_SUFFIX, GAME_EXE_NAME, GAME_EDITION_NAME, GAME_ASSETS_DIR).
# The current source and binary folders are those of the game.
#
# The component lists live under Code/ and are included once per game, each with its own binary
# folder under <Build>/<Game>/Code/..., which keeps the executable paths
# (build-x64/Generals/Code/Main/Debug/GZHNeo.exe, ...) stable for test tools and asset junctions.
# Targets and source file properties are separate per game; the parameters reach the lists as
# ordinary variables.

foreach(_param GAME_SPIEL_DIR GAME_EXE_NAME GAME_EDITION_NAME)
	if(NOT DEFINED ${_param} OR "${${_param}}" STREQUAL "")
		message(FATAL_ERROR "Code/Games/GameTargets.cmake: parameter ${_param} missing (see Code/Games/<game>/CMakeLists.txt)")
	endif()
endforeach()

# The engine (GameEngineCommon, -GameClient, -GameLogic, -GameNetwork, GameEngineDevice) is built
# once by the root CMakeLists.txt. Per game only GameEdition.cpp (getGameEdition()) remains, as the
# object library GameEdition${GAME_SUFFIX}. Every executable that links the engine also links this
# library: the game and the tools ImagePacker and MapCacheBuilder; if it is missing, the linker
# reports getGameEdition as unresolved.
# Compiled with the same include paths in the same order as GameEngineCommon: its PUBLIC and PRIVATE
# paths (repeated here, defined in Code/GameEngine/CMakeLists.txt), then via $<COMPILE_ONLY:...>
# (CMake >= 3.27) those of the libraries GameEngineCommon links. COMPILE_ONLY instead of a normal
# link, because a link would change the library order on every executable's linker line and with it
# possibly the static initialisation order.
add_library(GameEdition${GAME_SUFFIX} OBJECT
	${GAME_SPIEL_DIR}/GameEdition.cpp
)
target_include_directories(GameEdition${GAME_SUFFIX} PRIVATE
	${SHARED_GAMEENGINE_DIR}
	${SHARED_ENGINE_DIR}
	${SHARED_CODE_DIR}/Core/Compression
	${SHARED_CORE_DIR}
	${SHARED_RENDERER_DIR}
	${SHARED_CORE_DIR}/WWLib
	${SHARED_LIB_INCLUDE}
)
target_link_libraries(GameEdition${GAME_SUFFIX} PRIVATE $<COMPILE_ONLY:GameEngineCommon>)
target_compile_options(GameEdition${GAME_SUFFIX} PRIVATE ${ENGINE_STRICT_OPTIONS} $<$<CXX_COMPILER_ID:MSVC>:/WX>)


# Main: the game executable (GZHNeo.exe or GZHNeoZH.exe).
add_subdirectory("${SHARED_CODE_DIR}/App" Main)

# Tools, behind BUILD_TOOLS (root CMakeLists.txt).
if(BUILD_TOOLS)
	add_subdirectory("${SHARED_TOOLS_DIR}" Tools)
endif()
