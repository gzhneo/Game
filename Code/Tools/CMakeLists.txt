# Tools for both games. Included via Code/Games/GameTargets.cmake behind BUILD_TOOLS (root
# CMakeLists.txt) so the game build stays lean; each game gets its own targets with ${GAME_SUFFIX}
# (ImagePacker/ImagePackerMD, ...), binary folder <Build>/<Game>/Code/Tools.
#
# Only the MFC-free tools are defined here; the MFC tools have their own lists in their
# subfolders. The source lists follow the original .dsp files.

# ImagePacker and MapCacheBuilder have a single source tree under Code/Tools
# (SHARED_TOOLS_DIR); each game gets its own target because it links that game's engine.

# --- ImagePacker -----------------------------------------------------------------------------------
# Packs several textures into texture pages (asset pipeline). No MFC, but needs WWLib (Targa.h)
# and the GameMemory/debug infrastructure from GameEngineCommon.
add_executable(ImagePacker${GAME_SUFFIX} WIN32
	"${SHARED_TOOLS_DIR}/Legacy/ImagePacker/Source/WindowProcedures/DirectorySelect.cpp"
	"${SHARED_TOOLS_DIR}/Legacy/ImagePacker/Source/WindowProcedures/ImageErrorProc.cpp"
	"${SHARED_TOOLS_DIR}/Legacy/ImagePacker/Source/WindowProcedures/ImagePackerProc.cpp"
	"${SHARED_TOOLS_DIR}/Legacy/ImagePacker/Source/WindowProcedures/PageErrorProc.cpp"
	"${SHARED_TOOLS_DIR}/Legacy/ImagePacker/Source/WindowProcedures/PreviewProc.cpp"
	${SHARED_TOOLS_DIR}/Legacy/ImagePacker/Source/ImageInfo.cpp
	${SHARED_TOOLS_DIR}/Legacy/ImagePacker/Source/ImagePacker.cpp
	${SHARED_TOOLS_DIR}/Legacy/ImagePacker/Source/TexturePage.cpp
	${SHARED_TOOLS_DIR}/Legacy/ImagePacker/Source/WinMain.cpp
)
target_include_directories(ImagePacker${GAME_SUFFIX} PRIVATE
	${SHARED_TOOLS_DIR}/Legacy/ImagePacker/Include
	${SHARED_TOOLS_DIR}/Legacy/ImagePacker/Resource
	${SHARED_LIB_INCLUDE}
	${SHARED_GAMEENGINE_DIR}
	${SHARED_ENGINE_DIR}
)
# GameEngineCommon cannot be linked on its own (symbols like ParticleSystemManager/WaterSetting
# live in GameEngineGameClient); like the game, the tool needs the full GameEngineDevice chain,
# which links GameEngineCommon/GameClient/Logic/Network/WW3D2 as PUBLIC.
target_link_libraries(ImagePacker${GAME_SUFFIX} PRIVATE WWLib GameEngineDevice GameEdition${GAME_SUFFIX} winmm imm32)
target_compile_options(ImagePacker${GAME_SUFFIX} PRIVATE ${ENGINE_STRICT_OPTIONS} $<$<CXX_COMPILER_ID:MSVC>:/WX>)
# Same include order as GameEngineDevice (windows.h before Lib/BaseType.h, see ForceOrder.h).
target_compile_options(ImagePacker${GAME_SUFFIX} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/FI"${SHARED_PLATFORM_DIR}/ForceOrder.h">)

# --- MapCacheBuilder -------------------------------------------------------------------------------
# Builds the map cache (preview and metadata for the map lists in skirmish/multiplayer); part of
# the asset pipeline, no MFC. Needs much of the GameEngine infrastructure
# (INI/ThingFactory/ArchiveFileSystem/...).
add_executable(MapCacheBuilder${GAME_SUFFIX} WIN32 ${SHARED_TOOLS_DIR}/Legacy/MapCacheBuilder/Source/WinMain.cpp)
target_include_directories(MapCacheBuilder${GAME_SUFFIX} PRIVATE
	${SHARED_TOOLS_DIR}/Legacy/MapCacheBuilder/Include
	${SHARED_TOOLS_DIR}/Legacy/MapCacheBuilder/Resource
	${SHARED_LIB_INCLUDE}
	${SHARED_GAMEENGINE_DIR}
	${SHARED_ENGINE_DIR}
	${SHARED_PLATFORM_DIR}
)
target_link_libraries(MapCacheBuilder${GAME_SUFFIX} PRIVATE GameEngineDevice GameEdition${GAME_SUFFIX} WWLib winmm imm32)
target_compile_options(MapCacheBuilder${GAME_SUFFIX} PRIVATE ${ENGINE_STRICT_OPTIONS} $<$<CXX_COMPILER_ID:MSVC>:/WX>)

# --- DebugWindow (MFC) ----------------------------------------------------------------------------
# Own CMakeLists.txt under Code/Tools/<tool> because of CMAKE_MFC_FLAG; binary folder
# <Build>/<Game>/Code/Tools/<tool>. Maps and particle systems are edited with the map editor
# (Code/Tools/Editors/MapEditor), window layouts with the GUI editor (Code/Tools/Editors/GuiEditor), translations with
# Babylon (Code/Tools/Editors/Babylon, built once from the root CMakeLists.txt).
add_subdirectory(Legacy/DebugWindow DebugWindow)
