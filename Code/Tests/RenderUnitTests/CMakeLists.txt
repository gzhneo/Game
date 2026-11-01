# Unit tests for the device-independent parts of the render backend: format mapping and
# conversion, state mapping, FVF declarations, Vulkan device requirements, SPIR-V reflection; decoding of terrain
# tile files (TGA, DDS).
#
# Deliberately not linked against WW3D2: the files compiled in here are pure conversion code
# without device access or engine singletons. Linking WW3D2 would pull in the whole renderer with
# file system, asset manager and WinMain globals (same reasoning as for UnitTests).
#
# d3d11.lib is not needed: <d3d11.h> only provides enums and structs, no function calls.
#
# Built once from the root CMakeLists.txt (<Build>/Code/Tests/RenderUnitTests); not tied to a game.
add_executable(RenderUnitTests
	RenderUnitTests.cpp
	RenderUnitTestStubs.cpp	# memory pool glue and DebugCrash without GameEngine
	${SHARED_RENDERER_DIR}/WW3D2/Common/renderformat.cpp
	${SHARED_RENDERER_DIR}/WW3D2/Common/renderstates.cpp
	${SHARED_RENDERER_DIR}/WW3D2/Common/renderfvf.cpp
	${SHARED_RENDERER_DIR}/WW3D2/Vulkan/vulkanrequirements.cpp
	${SHARED_RENDERER_DIR}/WW3D2/Vulkan/vulkanspirv.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Terrain/TerrainTileImage.cpp
)

target_include_directories(RenderUnitTests PRIVATE
	${SHARED_CORE_DIR}
	${SHARED_RENDERER_DIR}
	${SHARED_RENDERER_DIR}/WW3D2
	${WW3D2_SCENE_DIRS}
	${SHARED_CORE_DIR}/WWLib
	${SHARED_CORE_DIR}/WWMath
	${SHARED_CORE_DIR}/WWDebug
	${SHARED_LIB_INCLUDE}
	${SHARED_PLATFORM_DIR}
)

# renderfvf.cpp uses StringClass from WWLib.
# winmm: timeGetTime/timeBeginPeriod from WWLib (systimer/thread).
# DirectXTex: TerrainTileImage.cpp reads DDS and decompresses BC1/BC7; the tests encode with it.
find_package(directxtex CONFIG REQUIRED)
target_link_libraries(RenderUnitTests PRIVATE WWLib WWDebug winmm Microsoft::DirectXTex)

# /W4 /WX for the whole target: the WW3D2 and device sources compiled in are already clean in their
# libraries.
target_compile_options(RenderUnitTests PRIVATE ${ENGINE_STRICT_OPTIONS} /WX)

add_test(NAME RenderUnitTests COMMAND RenderUnitTests)
