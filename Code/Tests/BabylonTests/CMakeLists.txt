# Tests of the translation tool Babylon (Code/Tools/Editors/Babylon/Model and Core). Need no game data, no GPU
# and no office program; no "assets" label, so they also run with `ctest -LE assets`. On Linux the same
# test runs in tools/gate.sh (tools/babylon/linux_build.sh, also under AddressSanitizer and UBSan).
add_executable(BabylonTests BabylonTests.cpp)
target_link_libraries(BabylonTests PRIVATE BabylonModel)
target_compile_options(BabylonTests PRIVATE ${ENGINE_STRICT_OPTIONS} $<$<CXX_COMPILER_ID:MSVC>:/WX>)
add_test(NAME BabylonTests COMMAND BabylonTests)
