# WW3D2 render library, one target shared by both games.
# animatedsoundmgr_stub.cpp provides no-op AnimatedSoundMgrClass statics, because
# Animatable3DObjClass and WW3D reference them unconditionally.
add_library(WW3D2 STATIC
	Animation/animatedsoundmgr_stub.cpp
	Mesh/aabtree.cpp
	Mesh/aabtreebuilder.cpp
	Animation/agg_def.cpp
	Animation/animobj.cpp
	System/assetmgr.cpp
	System/assetstatus.cpp
	Material/bitmaphandler.cpp
	Effects/bmp2d.cpp
	Effects/boxrobj.cpp
	Scene/bwrender.cpp
	Scene/camera.cpp
	Mesh/collect.cpp
	Scene/coltest.cpp
	Mesh/composite.cpp
	Effects/dazzle.cpp
	Material/ddsfile.cpp
	Mesh/decalmsh.cpp
	Scene/decalsys.cpp
	Mesh/distlod.cpp
	Common/rendercaps.cpp
	Common/renderfvf.cpp
	Buffers/indexbuffer.cpp
	Buffers/polygonrenderer.cpp
	Mesh/meshrenderer.cpp
	Mesh/meshrendererdebugger.cpp
	Material/texturemanager.cpp
	# texture copy and mipmap filtering via DirectXTex
	Material/texutil.cpp
	Buffers/vertexbuffer.cpp
	Common/renderdevice.cpp
	# Direct3D 11 backend (the device-facing part)
	Common/rendercore.cpp
	Common/rendercore_resources.cpp
	Common/rendercore_effects.cpp
	Common/rendercore_shadows.cpp
	Common/rendercore_gtao.cpp
	Common/rendercore_taa.cpp
	Common/rendercore_terrain.cpp
	Common/rendercore_boden.cpp
	Common/rendercore_instancing.cpp
	Common/rendercore_diagnosis.cpp
	Common/renderformat.cpp
	Common/renderstates.cpp
	# device layer: neutral interface, Direct3D 11 implementation
	Common/gpudevice.cpp
	Common/gpudisplay.cpp
	Common/gpulayers.cpp
	D3D11/d3d11device.cpp
	# Vulkan backend: device requirements (no Vulkan headers), loader (volk), probe and factory, the
	# device layer, SPIR-V reflection, the runtime shader compiler and the memory allocator
	Vulkan/vulkanrequirements.cpp
	Vulkan/vulkanloader.cpp
	Vulkan/vulkanbackend.cpp
	Vulkan/vulkandevice.cpp
	Vulkan/vulkandevice_resources.cpp
	Vulkan/vulkandevice_pipeline.cpp
	Vulkan/vulkandevice_context.cpp
	Vulkan/vulkanspirv.cpp
	Vulkan/vulkandxc.cpp
	Vulkan/vulkanmemory.cpp
	Mesh/dynamesh.cpp
	Render2D/font3d.cpp
	Material/formconv.cpp
	Animation/hanim.cpp
	Animation/hanimmgr.cpp
	Animation/hcanim.cpp
	Mesh/hlod.cpp
	Animation/hmdldef.cpp
	Animation/hmorphanim.cpp
	Animation/hrawanim.cpp
	Animation/htree.cpp
	Animation/htreemgr.cpp
	Scene/intersec.cpp
	Scene/layer.cpp
	Scene/light.cpp
	Scene/lightenvironment.cpp
	Effects/line3d.cpp
	Effects/linegrp.cpp
	Material/mapper.cpp
	Material/matinfo.cpp
	Material/matpass.cpp
	Material/matrixmapper.cpp
	Mesh/mesh.cpp
	Mesh/meshbuild.cpp
	Mesh/meshgeometry.cpp
	Material/meshmatdesc.cpp
	Mesh/meshmdl.cpp
	Mesh/meshmdlio.cpp
	Material/metalmap.cpp
	Material/missingtexture.cpp
	Animation/motchan.cpp
	Mesh/nullrobj.cpp
	Effects/part_buf.cpp
	Effects/part_emt.cpp
	Effects/part_ldr.cpp
	Animation/pivot.cpp
	Effects/pointgr.cpp
	Mesh/polyinfo.cpp
	Mesh/predlod.cpp
	Animation/prim_anim.cpp
	Scene/projector.cpp
	Mesh/proto.cpp
	Render2D/render2d.cpp
	Render2D/render2dsentence.cpp
	Mesh/renderobjectrecycler.cpp
	Mesh/rendobj.cpp
	Scene/rinfo.cpp
	Effects/ringobj.cpp
	Scene/scene.cpp
	Effects/segline.cpp
	Effects/seglinerenderer.cpp
	Material/shader.cpp
	Effects/shattersystem.cpp
	Mesh/snappts.cpp
	Buffers/sortingrenderer.cpp
	Effects/sphereobj.cpp
	Buffers/static_sort_list.cpp
	System/statistics.cpp
	Effects/streak.cpp
	Effects/streakRender.cpp
	Mesh/stripoptimizer.cpp
	Material/surfaceclass.cpp
	Scene/texproject.cpp
	Material/texture.cpp
	Material/texturefilter.cpp
	Material/textureloader.cpp
	Material/vertmaterial.cpp
	Scene/visrasterizer.cpp
	Mesh/w3d_dep.cpp
	Mesh/w3d_util.cpp
	Mesh/w3dexclusionlist.cpp
	System/ww3d.cpp
	Material/ww3dformat.cpp
	# matrix and vector helpers used in place of D3DX math
	System/wwd3dmath.cpp
)

# The scene classes live in subfolders by task; each is on the public include path so that short includes
# ("mesh.h") keep working. The parent folder too, so includes of the form "WW3D2/x.h" resolve for every user.
set(WW3D2_SCENE_DIRS
	${CMAKE_CURRENT_SOURCE_DIR}/Animation
	${CMAKE_CURRENT_SOURCE_DIR}/Buffers
	${CMAKE_CURRENT_SOURCE_DIR}/Effects
	${CMAKE_CURRENT_SOURCE_DIR}/Material
	${CMAKE_CURRENT_SOURCE_DIR}/Mesh
	${CMAKE_CURRENT_SOURCE_DIR}/Render2D
	${CMAKE_CURRENT_SOURCE_DIR}/Scene
	${CMAKE_CURRENT_SOURCE_DIR}/System
)
set(WW3D2_SCENE_DIRS ${WW3D2_SCENE_DIRS} PARENT_SCOPE)
target_include_directories(WW3D2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${WW3D2_SCENE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(WW3D2 PUBLIC WWLib WWMath WWDebug WWSaveLoad)
# DirectXTex (from the vcpkg manifest) handles texture conversion on the CPU, in every build.
find_package(directxtex CONFIG REQUIRED)
target_link_libraries(WW3D2 PRIVATE Microsoft::DirectXTex)

# Device layers (root CMakeLists.txt: GZH_RENDER_D3D11, GZH_RENDER_VULKAN). A layer that is off loses its
# folder; gpulayers.cpp registers the layers that are on.
get_target_property(WW3D2_SOURCES WW3D2 SOURCES)
if(NOT GZH_RENDER_D3D11)
	list(FILTER WW3D2_SOURCES EXCLUDE REGEX "^D3D11/")
endif()
if(NOT GZH_RENDER_VULKAN)
	list(FILTER WW3D2_SOURCES EXCLUDE REGEX "^Vulkan/")
endif()
set_property(TARGET WW3D2 PROPERTY SOURCES ${WW3D2_SOURCES})
set(WW3D2_LAYER_D3D11 0)
set(WW3D2_LAYER_VULKAN 0)
if(GZH_RENDER_D3D11)
	set(WW3D2_LAYER_D3D11 1)
endif()
if(GZH_RENDER_VULKAN)
	set(WW3D2_LAYER_VULKAN 1)
endif()
set_property(SOURCE Common/gpulayers.cpp APPEND PROPERTY COMPILE_DEFINITIONS
	GZH_RENDER_D3D11=${WW3D2_LAYER_D3D11} GZH_RENDER_VULKAN=${WW3D2_LAYER_VULKAN})
# Direct3D 11: d3dcompiler compiles the shaders at runtime.
if(GZH_RENDER_D3D11)
	target_link_libraries(WW3D2 PRIVATE d3d11.lib dxgi.lib d3dcompiler.lib)
endif()
# Vulkan headers, volk and the memory allocator as headers only: volk loads vulkan-1.dll at runtime
# (vulkanloader.cpp), so nothing links against vulkan-1.lib and the game starts without a Vulkan driver.
# The shader compiler is loaded at runtime too (vulkandxc.cpp), its header is not needed.
if(GZH_RENDER_VULKAN)
	find_package(VulkanHeaders CONFIG REQUIRED)
	find_package(volk CONFIG REQUIRED)
	find_package(VulkanMemoryAllocator CONFIG REQUIRED)
	target_link_libraries(WW3D2 PRIVATE Vulkan::Headers volk::volk_headers GPUOpen::VulkanMemoryAllocator)
endif()
