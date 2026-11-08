# The game executable: GZHNeo.exe for Generals, GZHNeoZH.exe for Zero Hour (GAME_EXE_NAME).
# Included per game via Code/Games/GameTargets.cmake, binary folder <Build>/<Game>/Code/Main. Only the
# program icon Generals.ico differs per game (GAME_SPIEL_DIR). Apart from WinMain.cpp, CpuCheck.cpp
# and RTS.RC all code comes from the engine libraries. WinMain.cpp picks the game mutex by edition.
add_executable(${GAME_EXE_NAME} WIN32
	WinMain.cpp
	CpuCheck.cpp
	RTS.RC
)

# CpuCheck.cpp checks for AVX2 via init_seg(compiler) before all other global constructors and
# exits with a message otherwise. The file itself must not contain AVX2 code; /arch:SSE2 comes
# after the global /arch:AVX2 on the command line and therefore wins.
set_source_files_properties(CpuCheck.cpp PROPERTIES COMPILE_OPTIONS "/arch:SSE2")
# Game name in the title of the AVX2 message in CpuCheck.cpp, per game (the file must not include
# project headers, not even Common/GameEdition.h).
target_compile_definitions(${GAME_EXE_NAME} PRIVATE "GAME_EDITION_NAME=\"${GAME_EDITION_NAME}\""
	"GAME_FILE_DESCRIPTION=\"GZH Neo - ${GAME_EDITION_NAME}\"" "GAME_EXE_FILE=\"${GAME_EXE_NAME}.exe\"")

# /EHa for this program only. WinMain.cpp installs _set_se_translator( DumpExceptionInfo ) to turn
# structured exceptions (access violations etc.) into C++ exceptions; under the global /EHsc the
# translator is never called (warning C4535). Target options come after directory options and CMake
# maps both to the ExceptionHandling property, so the .vcxproj ends up with Async only.
# Not for the engine libraries: /EHa adds cleanup code around non-C++ exceptions there for no benefit.
# /EHa for _set_se_translator; /W4 /WX like the engine libraries.
target_compile_options(${GAME_EXE_NAME} PRIVATE /EHa ${ENGINE_STRICT_OPTIONS} /WX)

# No FFmpeg include path here: FFmpegVideoPlayer.h and VideoDevice/FFmpeg/FFmpegFile.h forward-declare the
# FFmpeg types they expose. Platform/CMakeLists.txt has its own find_package(FFMPEG)
# for the .cpp files that use these types.
target_include_directories(${GAME_EXE_NAME} PRIVATE
	${SHARED_GAMEENGINE_DIR}
	${SHARED_ENGINE_DIR}
	${SHARED_LIB_INCLUDE}
	${SHARED_CORE_DIR}
	${SHARED_RENDERER_DIR}
	${SHARED_CORE_DIR}/WWLib
	${SHARED_CORE_DIR}/WWDebug
	${SHARED_CORE_DIR}/WWMath
	${SHARED_RENDERER_DIR}/WW3D2
	${WW3D2_SCENE_DIRS}
	# RTS.RC includes "Generals.ico" from the per-game folder. The resource compiler searches the
	# same paths as #include; there are no C++ headers in that folder.
	${GAME_SPIEL_DIR}
)

# D3D11, FFmpeg and XAudio2 are linked inside WW3D2/GameEngineDevice and come in transitively.
# The remaining Windows system libraries match the original RTS.dsp.
target_link_libraries(${GAME_EXE_NAME} PRIVATE
	GameEngineDevice
	GameEdition${GAME_SUFFIX}	# selects the game of this executable
	winmm
	dsound
	ws2_32	# network (Transport)
	imm32
	ole32
	oleaut32
	uuid
	shell32
	comdlg32
	advapi32
	gdi32
	user32
	kernel32
	winspool
	odbc32
	odbccp32
)

# The ASan build (preset x64-asan) needs the ASan runtime next to the exe.
if(ENABLE_ASAN)
	get_filename_component(_asanBinDir "${CMAKE_CXX_COMPILER}" DIRECTORY)
	set(_asanDll "${_asanBinDir}/clang_rt.asan_dynamic-x86_64.dll")
	if(EXISTS "${_asanDll}")
		add_custom_command(TARGET ${GAME_EXE_NAME} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_asanDll}" "$<TARGET_FILE_DIR:${GAME_EXE_NAME}>")
	else()
		message(WARNING "ASan runtime not found: ${_asanDll} -- copy it next to the executable before starting.")
	endif()
endif()

# RTS.RC embeds a complete manifest (RTS.manifest: DPI awareness, supportedOS up to Windows 11,
# longPathAware, asInvoker). /MANIFEST:NO stops the linker from generating its own, which would
# share resource ID 1 (CREATEPROCESS_MANIFEST_RESOURCE_ID) and fail as a duplicate.
target_link_options(${GAME_EXE_NAME} PRIVATE /MANIFEST:NO)

# vcpkg copies its runtime DLLs (FFmpeg, ZLib, DirectXTex) next to the exe itself with the
# Visual Studio generator (VCPKG_APPLOCAL_DEPS).
#
# The Vulkan renderer loads the shader compiler at runtime (vulkandxc.cpp), so vcpkg does not see it
# as a dependency: dxcompiler.dll is copied explicitly. dxil.dll only signs DXIL and is not needed for
# SPIR-V.
if(GZH_RENDER_VULKAN)
	add_custom_command(TARGET ${GAME_EXE_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Microsoft::DirectXShaderCompiler>" "$<TARGET_FILE_DIR:${GAME_EXE_NAME}>"
	)
endif()
#
# The HLSL shaders from Data/Shaders are copied to Shaders/ next to the exe; the D3D11 backend
# loads "shaders\\sm4\\<name>.ps.hlsl" from the local file system.
add_custom_command(TARGET ${GAME_EXE_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHARED_DATA_DIR}/Shaders" "$<TARGET_FILE_DIR:${GAME_EXE_NAME}>/Shaders"
)

# Project game data (detail levels in GameLODOverride.ini, see GameLOD.cpp) goes to its own folder
# ProjectData next to the exe, apart from EA's Data: Data is a junction into the shared asset
# folder, so writing there would modify the assets, and creating it as a real folder would keep
# LinkAssets from placing the junction.
add_custom_command(TARGET ${GAME_EXE_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHARED_DATA_DIR}/INI" "$<TARGET_FILE_DIR:${GAME_EXE_NAME}>/ProjectData/INI"
)
# Texts the project adds to the game (Data/Text/<language>.str, see GameTextManager::loadProjectStrings).
add_custom_command(TARGET ${GAME_EXE_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHARED_DATA_DIR}/Text" "$<TARGET_FILE_DIR:${GAME_EXE_NAME}>/ProjectData/Text"
)
# Rendezvous servers shipped with the game (Data/Network/Rendezvous.txt, see GameLobby::loadServers).
add_custom_command(TARGET ${GAME_EXE_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHARED_DATA_DIR}/Network" "$<TARGET_FILE_DIR:${GAME_EXE_NAME}>/ProjectData/Network"
)
# Terrain detail textures from CC0 photo scans (Data/Textures/BodenDetail), loaded by the D3D11
# backend from ProjectData/Textures/BodenDetail.
add_custom_command(TARGET ${GAME_EXE_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHARED_DATA_DIR}/Textures" "$<TARGET_FILE_DIR:${GAME_EXE_NAME}>/ProjectData/Textures"
)

# Free content packages (Data/FreierBoden, Data/FreieStrassen, Data/BaeumeFrei, CC0) go to ProjectData/Inhalt; the
# game loads every folder there above the texture package (ArchiveFileSystem::loadFreeContent, -freeContent ea switches it off). Only
# changed files are copied, the packages are large; the tier folder of an older copy is removed (the package now
# holds its highest version with a manifest).
add_custom_command(TARGET ${GAME_EXE_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:${GAME_EXE_NAME}>/ProjectData/Inhalt/FreieStrassen/Texturstufen"
	COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${SHARED_DATA_DIR}/FreierBoden" "$<TARGET_FILE_DIR:${GAME_EXE_NAME}>/ProjectData/Inhalt/FreierBoden"
	COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${SHARED_DATA_DIR}/FreieStrassen" "$<TARGET_FILE_DIR:${GAME_EXE_NAME}>/ProjectData/Inhalt/FreieStrassen"
	COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${SHARED_DATA_DIR}/BaeumeFrei" "$<TARGET_FILE_DIR:${GAME_EXE_NAME}>/ProjectData/Inhalt/BaeumeFrei"
)

# Install (root CMakeLists.txt): <prefix>/Generals or <prefix>/ZeroHour with everything the game needs next to
# the exe except EA's game data.
string(REPLACE " " "" GAME_INSTALL_DIR "${GAME_EDITION_NAME}")
install(TARGETS ${GAME_EXE_NAME} RUNTIME DESTINATION ${GAME_INSTALL_DIR})
# The DLLs next to the built exe (FFmpeg, ZLib, DirectXTex from vcpkg, dxcompiler.dll). Collected at install time
# with a flat glob: install(DIRECTORY) would walk into the Data and ZH_Generals junctions.
install(CODE "
	file(GLOB _gzh_dlls \"$<TARGET_FILE_DIR:${GAME_EXE_NAME}>/*.dll\")
	if(NOT _gzh_dlls)
		message(FATAL_ERROR \"no DLLs next to ${GAME_EXE_NAME} -- build the configuration first\")
	endif()
	file(INSTALL \${_gzh_dlls} DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${GAME_INSTALL_DIR}\")
")
install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION ${GAME_INSTALL_DIR})
install(DIRECTORY "${SHARED_DATA_DIR}/Shaders" DESTINATION ${GAME_INSTALL_DIR})
install(DIRECTORY "${SHARED_DATA_DIR}/INI" "${SHARED_DATA_DIR}/Text" "${SHARED_DATA_DIR}/Network" "${SHARED_DATA_DIR}/Textures"
	DESTINATION ${GAME_INSTALL_DIR}/ProjectData)
if(GZH_INSTALL_FREE_CONTENT)
	install(DIRECTORY "${SHARED_DATA_DIR}/FreierBoden" "${SHARED_DATA_DIR}/FreieStrassen" "${SHARED_DATA_DIR}/BaeumeFrei"
		DESTINATION ${GAME_INSTALL_DIR}/ProjectData/Inhalt)
endif()

# Game assets: after each build cmake/LinkAssets.cmake links missing .big archives, extra files,
# Data and (Zero Hour only) ZH_Generals from GAME_ASSETS_DIR into the exe folder (cache variable
# GENERALS_ASSETS_DIR or GENERALSZH_ASSETS_DIR). Without a folder the step is left out and the exe
# needs the game data by other means; a folder that does not exist yet only warns at build time.
if(GAME_ASSETS_DIR)
	add_custom_command(TARGET ${GAME_EXE_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} "-DASSETS_DIR=${GAME_ASSETS_DIR}" -DTARGET_DIR=$<TARGET_FILE_DIR:${GAME_EXE_NAME}> -P ${CMAKE_SOURCE_DIR}/cmake/LinkAssets.cmake
		VERBATIM
	)
else()
	message(WARNING "${GAME_EXE_NAME}: no asset folder set (GENERALS_ASSETS_DIR / GENERALSZH_ASSETS_DIR or the "
		"environment variables GZH_ASSETS_GENERALS / GZH_ASSETS_ZH); the build does not link game data next to the exe.")
endif()
