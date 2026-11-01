# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
#
# VfsDump: resolves the archives of a game folder with the new file system core and prints the list
# the game writes with -dateiIndexAusgeben. Engine building blocks only, so it
# also builds on Linux (tools/dateisystem/pruefen.sh).

set(VFSDUMP_ENGINE ${SHARED_ENGINE_DIR}/Engine)
add_executable(VfsDump
	main.cpp
	${VFSDUMP_ENGINE}/BigArchive.cpp
	${VFSDUMP_ENGINE}/Vfs.cpp
	${VFSDUMP_ENGINE}/ZipArchive.cpp
)
target_include_directories(VfsDump PRIVATE ${SHARED_GAMEENGINE_DIR} ${SHARED_ENGINE_DIR})
find_package(ZLIB REQUIRED)
target_link_libraries(VfsDump PRIVATE ZLIB::ZLIB)
target_compile_options(VfsDump PRIVATE ${ENGINE_STRICT_OPTIONS} $<$<CXX_COMPILER_ID:MSVC>:/WX>)
