# Tests of the layout model of the GUI editor (Code/Tools/Editors/GuiEditor/Model). Need no game data and no GPU.
# On Linux the same test runs in tools/gate.sh (tools/guieditor/linux_build.sh, also under AddressSanitizer
# and UBSan, there with the test layouts of tools/wine/testdaten).
add_executable(GuiEditorTests GuiEditorTests.cpp)
target_link_libraries(GuiEditorTests PRIVATE GuiEditorModel)
target_include_directories(GuiEditorTests PRIVATE ${SHARED_GAMEENGINE_DIR} ${SHARED_ENGINE_DIR})
target_compile_options(GuiEditorTests PRIVATE ${ENGINE_STRICT_OPTIONS} $<$<CXX_COMPILER_ID:MSVC>:/WX>)
add_test(NAME GuiEditorTests COMMAND GuiEditorTests)
