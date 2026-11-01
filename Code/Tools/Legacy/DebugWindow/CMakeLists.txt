# DebugWindow is an MFC DLL (CWinApp derivative, regular/dynamic MFC, see StdAfx.h). Own
# CMakeLists.txt because CMAKE_MFC_FLAG applies per directory and must not affect the MFC-free
# tools. Included via Code/Tools/CMakeLists.txt; target DebugWindow${GAME_SUFFIX}.
set(CMAKE_MFC_FLAG 2)	# 2 = dynamic/regular MFC (matches the project's /MD runtime)
set(TOOL_DIR "${SHARED_TOOLS_DIR}/Legacy/DebugWindow")

add_library(DebugWindow${GAME_SUFFIX} SHARED
	${TOOL_DIR}/DebugWindow.cpp
	${TOOL_DIR}/DebugWindowDialog.cpp
	${TOOL_DIR}/StdAfx.cpp
	${TOOL_DIR}/DebugWindow.rc
)
# Target name per game (DebugWindow/DebugWindowMD); the DLL is DebugWindow.dll in both, the name
# the game loads via LoadLibrary (ScriptEngine::init in ScriptEngineInit.cpp).
set_target_properties(DebugWindow${GAME_SUFFIX} PROPERTIES OUTPUT_NAME "DebugWindow")
target_link_options(DebugWindow${GAME_SUFFIX} PRIVATE "/DEF:${TOOL_DIR}/DebugWindow.def")
target_include_directories(DebugWindow${GAME_SUFFIX} PRIVATE
	# DebugWindow.rc includes "res\..." relatively; the resource compiler finds it via TOOL_DIR.
	${TOOL_DIR}
)
