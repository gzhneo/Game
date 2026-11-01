# Layout model of the GUI editor (Model) and the command line tool WndTool: no engine and no Qt, built once.
# The game's field readers that need no engine (Engine/WndFields.cpp) come in as source. The editor
# program itself is built with the map editor (Code/Tools/Editors/MapEditor/CMakeLists.txt), next to its engine DLL.
add_library(GuiEditorModel STATIC
	Core/Document.cpp
	Core/Document.h
	Core/WndArrange.cpp
	Core/WndArrange.h
	Core/WndDiff.cpp
	Core/WndDiff.h
	Core/WndEdit.cpp
	Core/WndEdit.h
	Core/WndScheme.cpp
	Core/WndScheme.h
	Core/WndSchemeTable.h
	Model/WndCheck.cpp
	Model/WndCheck.h
	Model/WndLayout.cpp
	Model/WndLayout.h
	${SHARED_ENGINE_DIR}/Engine/WndFields.cpp
)
target_include_directories(GuiEditorModel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} PRIVATE ${SHARED_GAMEENGINE_DIR} ${SHARED_ENGINE_DIR})
target_compile_options(GuiEditorModel PRIVATE ${ENGINE_STRICT_OPTIONS} $<$<CXX_COMPILER_ID:MSVC>:/WX>)

add_executable(WndTool Tool/WndTool.cpp)
target_link_libraries(WndTool PRIVATE GuiEditorModel)
target_compile_options(WndTool PRIVATE ${ENGINE_STRICT_OPTIONS} $<$<CXX_COMPILER_ID:MSVC>:/WX>)
