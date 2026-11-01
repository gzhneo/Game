# Tests of the newer engine building blocks (clock, thread roles, logging, CPU topology, job
# system, watchdog, texture tiers, .big archive directory, virtual file system, string tables).
#
# Deliberately a small separate target: the tested files depend on no engine header, so it needs
# neither GameEngineCommon nor assets and runs in seconds. GenerationTable.h is header-only. It has
# no "assets" label and therefore also runs with `ctest -LE assets`. Built once from the root
# CMakeLists.txt.
add_executable(EngineTests
	EngineTests.cpp
)

target_include_directories(EngineTests PRIVATE
	${SHARED_GAMEENGINE_DIR}
	${SHARED_ENGINE_DIR}
	${SHARED_CORE_DIR}
	${SHARED_CORE_DIR}/WWLib
)

# Same strict options as the tested files.
target_compile_options(EngineTests PRIVATE ${ENGINE_STRICT_OPTIONS} $<$<CXX_COMPILER_ID:MSVC>:/WX>)

find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
target_link_libraries(EngineTests PRIVATE EngineCore Threads::Threads ZLIB::ZLIB)

add_test(NAME EngineTests COMMAND EngineTests)
