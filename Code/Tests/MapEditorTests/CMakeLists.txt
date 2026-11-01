# Tests of the map model of the new map editor (Code/Tools/Editors/MapEditor/Model). Need no game data and
# no GPU; no "assets" label, so they also run with `ctest -LE assets`. On Linux the same test runs in
# tools/gate.sh (tools/mapeditor/linux_build.sh, also under AddressSanitizer and UBSan).
add_executable(MapEditorTests MapEditorTests.cpp)
target_link_libraries(MapEditorTests PRIVATE MapEditorCore)
target_compile_options(MapEditorTests PRIVATE ${ENGINE_STRICT_OPTIONS} $<$<CXX_COMPILER_ID:MSVC>:/WX>)
add_test(NAME MapEditorTests COMMAND MapEditorTests)
