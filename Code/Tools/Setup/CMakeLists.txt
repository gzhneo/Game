# GZHNeoEinrichtung: sets up the game data from the player's installation of Generals and Zero Hour (Steam, EA
# app, Origin, retail discs). Needs no engine; GameDataFinder.cpp and SelfTest.cpp are portable, only
# SystemProbeWin32.cpp, SetupWindow.cpp and main.cpp use Windows. Built once from the root CMakeLists.txt.
add_executable(GZHNeoEinrichtung WIN32
	main.cpp
	SetupWindow.cpp
	GameDataFinder.cpp
	DataCopy.cpp
	SelfTest.cpp
	SystemProbeWin32.cpp
)
target_compile_options(GZHNeoEinrichtung PRIVATE ${ENGINE_STRICT_OPTIONS} $<$<CXX_COMPILER_ID:MSVC>:/WX>)
target_compile_definitions(GZHNeoEinrichtung PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(GZHNeoEinrichtung PRIVATE advapi32 comctl32 ole32 shell32)
# Static C++ runtime: the setup runs before anything else of the package is in place.
if(MSVC AND NOT ENABLE_ASAN)
	set_property(TARGET GZHNeoEinrichtung PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
add_test(NAME SetupSelftest COMMAND GZHNeoEinrichtung --selftest)
install(TARGETS GZHNeoEinrichtung RUNTIME DESTINATION .)

# Licence texts of the libraries shipped as DLLs (vcpkg's copyright file per port) and an overview with the FFmpeg
# version and the vcpkg recipe it was built from (from vcpkg's SPDX file), in <prefix>/Licenses.
set(_gzh_vcpkg_share "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share")
foreach(_gzh_port ffmpeg directxtex zlib directx-dxc)
	if(EXISTS "${_gzh_vcpkg_share}/${_gzh_port}/copyright")
		install(FILES "${_gzh_vcpkg_share}/${_gzh_port}/copyright" DESTINATION Licenses RENAME "${_gzh_port}.txt")
	else()
		message(WARNING "Licence text of ${_gzh_port} not found in ${_gzh_vcpkg_share}; the package lacks it")
	endif()
endforeach()
set(GZH_FFMPEG_SOURCE "unknown")
set(GZH_FFMPEG_PORT "unknown")
set(GZH_FFMPEG_RECIPE "unknown")
if(EXISTS "${_gzh_vcpkg_share}/ffmpeg/vcpkg.spdx.json")
	file(READ "${_gzh_vcpkg_share}/ffmpeg/vcpkg.spdx.json" _gzh_spdx)
	string(JSON _gzh_count LENGTH "${_gzh_spdx}" packages)
	math(EXPR _gzh_last "${_gzh_count} - 1")
	foreach(_gzh_i RANGE ${_gzh_last})
		string(JSON _gzh_name GET "${_gzh_spdx}" packages ${_gzh_i} name)
		string(JSON _gzh_location ERROR_VARIABLE _gzh_error GET "${_gzh_spdx}" packages ${_gzh_i} downloadLocation)
		if(_gzh_name STREQUAL "ffmpeg")
			string(JSON GZH_FFMPEG_PORT GET "${_gzh_spdx}" packages ${_gzh_i} versionInfo)
			set(GZH_FFMPEG_RECIPE "${_gzh_location}")
		elseif(_gzh_name STREQUAL "ffmpeg/ffmpeg")
			set(GZH_FFMPEG_SOURCE "${_gzh_location}")
		endif()
	endforeach()
endif()
configure_file(ThirdPartyLicenses.txt.in "${CMAKE_CURRENT_BINARY_DIR}/ThirdPartyLicenses.txt" @ONLY NEWLINE_STYLE CRLF)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ThirdPartyLicenses.txt" DESTINATION Licenses RENAME README.txt)

# Player package: GZHNeoSetup fills a fresh staging folder with cmake --install and builds the setup with Inno Setup
# (GZHNeo.iss): <Build>/installer/GZHNeo-<version>-Setup.exe. Build it in Release:
#   cmake --build <Build> --config Release --target GZHNeoSetup
# Only defined if ISCC.exe is found (Inno Setup 6, per-user or per-machine installation).
find_program(GZH_ISCC ISCC
	HINTS "$ENV{LOCALAPPDATA}/Programs/Inno Setup 6" "$ENV{ProgramFiles\(x86\)}/Inno Setup 6" "$ENV{ProgramFiles}/Inno Setup 6")
if(GZH_ISCC)
	file(STRINGS "${SHARED_CODE_DIR}/App/BuildVersion.h" _gzh_version_line REGEX "^#define VERSION_TEXT ")
	string(REGEX REPLACE "^#define VERSION_TEXT \"([^\"]*)\".*" "\\1" GZH_VERSION_TEXT "${_gzh_version_line}")
	set(_gzh_stage "${CMAKE_BINARY_DIR}/installer/stage")
	set(_gzh_suffix "")
	if(NOT GZH_INSTALL_FREE_CONTENT)
		set(_gzh_suffix "-NoFreeContent")
	endif()
	add_custom_target(GZHNeoSetup
		COMMAND ${CMAKE_COMMAND} -E rm -rf "${_gzh_stage}"
		COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}" --config $<CONFIG> --prefix "${_gzh_stage}"
		COMMAND "${GZH_ISCC}" /Q "/DStage=${_gzh_stage}" "/DVersion=${GZH_VERSION_TEXT}" "/DSuffix=${_gzh_suffix}"
			"/DIcon=${SHARED_CODE_DIR}/Games/ZeroHour/Generals.ico" "/O${CMAKE_BINARY_DIR}/installer"
			"${CMAKE_CURRENT_SOURCE_DIR}/GZHNeo.iss"
		COMMENT "Building GZHNeo-${GZH_VERSION_TEXT}-Setup${_gzh_suffix}.exe"
		VERBATIM)
	add_dependencies(GZHNeoSetup GZHNeoEinrichtung GZHNeo GZHNeoZH)
endif()
