# LinkAssets.cmake -- provides an exe output folder with the game assets.
#
# Called by the post-build step of GZHNeo.exe / GZHNeoZH.exe (Code/App/CMakeLists.txt):
#   cmake -DASSETS_DIR=<asset folder> -DTARGET_DIR=<exe folder> -P cmake/LinkAssets.cmake
#
# The asset folder (cache variables GENERALS_ASSETS_DIR/GENERALSZH_ASSETS_DIR, default from the
# environment variables GZH_ASSETS_GENERALS/GZH_ASSETS_ZH) is created by tools/assets/setup_assets.py:
# a real copy of the EA installation plus data fixes. The .big archives and extra files are placed
# in the exe folder as hard links (same drive, no extra space; copy on failure), the folders Data,
# ZH_Generals (Zero Hour only) and Texturstufen (if present, created by
# tools/assets/build_texture_tiers.py) as junctions. Existing entries are never touched, so an
# output folder with its own links stays as it is.

if(NOT ASSETS_DIR OR NOT TARGET_DIR)
	message(FATAL_ERROR "LinkAssets.cmake: pass ASSETS_DIR and TARGET_DIR")
endif()
if(NOT IS_DIRECTORY "${ASSETS_DIR}")
	message(WARNING "LinkAssets.cmake: asset folder missing: ${ASSETS_DIR} -- run "
		"'python tools/assets/setup_assets.py' first (see BUILD.md), otherwise the exe does not start.")
	return()
endif()

file(GLOB _assets
	"${ASSETS_DIR}/*.big"
	"${ASSETS_DIR}/Generals.dat"
	"${ASSETS_DIR}/Install_Final.bmp"
	"${ASSETS_DIR}/langdata.dat"
	"${ASSETS_DIR}/0000000?.*")
set(_linked 0)
foreach(_src IN LISTS _assets)
	get_filename_component(_name "${_src}" NAME)
	set(_dst "${TARGET_DIR}/${_name}")
	if(NOT EXISTS "${_dst}")
		file(CREATE_LINK "${_src}" "${_dst}" RESULT _res COPY_ON_ERROR)
		if(NOT _res EQUAL 0)
			message(WARNING "LinkAssets.cmake: ${_name}: ${_res}")
		else()
			math(EXPR _linked "${_linked} + 1")
		endif()
	endif()
endforeach()

foreach(_dir IN ITEMS Data ZH_Generals Texturstufen)
	set(_dir_src "${ASSETS_DIR}/${_dir}")
	set(_dir_dst "${TARGET_DIR}/${_dir}")
	if(IS_DIRECTORY "${_dir_src}" AND NOT EXISTS "${_dir_dst}")
		if(WIN32)
			file(TO_NATIVE_PATH "${_dir_dst}" _dst_native)
			file(TO_NATIVE_PATH "${_dir_src}" _src_native)
			# Junction instead of symlink: needs no developer rights
			execute_process(COMMAND cmd /c mklink /J "${_dst_native}" "${_src_native}"
				RESULT_VARIABLE _res OUTPUT_QUIET ERROR_VARIABLE _err)
		else()
			file(CREATE_LINK "${_dir_src}" "${_dir_dst}" RESULT _res SYMBOLIC)
			set(_err "${_res}")
		endif()
		if(NOT _res EQUAL 0)
			message(WARNING "LinkAssets.cmake: ${_dir}-junction failed: ${_err}")
		else()
			math(EXPR _linked "${_linked} + 1")
		endif()
	endif()
endforeach()

if(_linked GREATER 0)
	message(STATUS "LinkAssets.cmake: ${_linked} asset entries linked into ${TARGET_DIR} (source ${ASSETS_DIR})")
endif()
