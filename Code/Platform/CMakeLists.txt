# GameEngineDevice: W3DDevice, Win32Device, XAudio2Device, VideoDevice -- one target, built once and
# linked by both games. Included from the root CMakeLists.txt.

add_library(GameEngineDevice STATIC
	${SHARED_PLATFORM_DIR}/VideoDevice/FFmpeg/FFmpegFile.cpp
	${SHARED_PLATFORM_DIR}/VideoDevice/FFmpeg/FFmpegVideoPlayer.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/Common/System/W3DFunctionLexicon.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/Common/System/W3DRadar.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/Common/Thing/W3DModuleFactory.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/Common/Thing/W3DThingFactory.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/Common/W3DConvert.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Terrain/BaseHeightMap.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Drawable/Draw/W3DDebrisDraw.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Drawable/Draw/W3DDefaultDraw.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Drawable/Draw/W3DDependencyModelDraw.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Drawable/Draw/W3DLaserDraw.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Drawable/Draw/W3DModelDrawXfer.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Drawable/Draw/W3DModelDrawSubObjects.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Drawable/Draw/W3DModelDrawBones.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Drawable/Draw/W3DModelDrawAnim.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Drawable/Draw/W3DModelDrawConditionInfo.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Drawable/Draw/W3DModelDrawModuleData.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Drawable/Draw/W3DOverlordAircraftDraw.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Drawable/Draw/W3DOverlordTankDraw.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Drawable/Draw/W3DOverlordTruckDraw.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Drawable/Draw/W3DPoliceCarDraw.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Drawable/Draw/W3DProjectileStreamDraw.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Drawable/Draw/W3DPropDraw.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Drawable/Draw/W3DRopeDraw.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Drawable/Draw/W3DScienceModelDraw.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Drawable/Draw/W3DSupplyDraw.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Drawable/Draw/W3DTankDraw.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Drawable/Draw/W3DTankTruckDraw.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Drawable/Draw/W3DTracerDraw.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Drawable/Draw/W3DTreeDraw.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Drawable/Draw/W3DTruckDraw.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/GUI/GUICallbacks/W3DControlBar.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/GUI/GUICallbacks/W3DMOTD.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/GUI/GUICallbacks/W3DMainMenu.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/GUI/Gadget/W3DCheckBox.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/GUI/Gadget/W3DComboBox.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/GUI/Gadget/W3DHorizontalSlider.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/GUI/Gadget/W3DListBox.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/GUI/Gadget/W3DProgressBar.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/GUI/Gadget/W3DPushButton.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/GUI/Gadget/W3DRadioButton.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/GUI/Gadget/W3DStaticText.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/GUI/Gadget/W3DTabControl.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/GUI/Gadget/W3DTextEntry.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/GUI/Gadget/W3DVerticalSlider.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/GUI/W3DGameFont.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/GUI/W3DGameWindow.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/GUI/W3DGameWindowManager.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Terrain/HeightMap.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Shadow/W3DBufferManager.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Shadow/W3DProjectedShadow.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Shadow/W3DShadow.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Shadow/W3DVolumetricShadow.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Terrain/TerrainTessMask.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Terrain/TerrainTex.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Terrain/TerrainTileImage.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Terrain/TileData.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/System/W3DAssetManager.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/System/W3DAssetManagerExposed.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Terrain/W3DBibBuffer.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Terrain/W3DBridgeBuffer.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Terrain/W3DCustomEdging.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Display/W3DDebugDisplay.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Display/W3DDebugIcons.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Display/W3DDisplay.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Display/W3DDisplay2D.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Display/W3DDisplayStats.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Display/W3DDisplayAssets.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/GUI/W3DDisplayString.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/GUI/W3DDisplayStringManager.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Display/W3DDynamicLight.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/System/W3DFileSystem.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/System/W3DGameClient.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/GUI/W3DInGameUI.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/GUI/W3DMouse.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Display/W3DParticleSys.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Display/W3DPoly.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Terrain/W3DPropBuffer.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Terrain/W3DRoadBuffer.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Display/W3DScene.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Display/W3DShaderManager.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Terrain/W3DShroud.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Terrain/W3DSmudge.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Terrain/W3DSnow.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Display/W3DStatusCircle.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Terrain/W3DTerrainTracks.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Terrain/W3DTerrainVisual.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Terrain/W3DTreeBuffer.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Display/W3DVideoBuffer.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Display/W3DView.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Display/W3dWaypointBuffer.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Water/W3DWater.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Water/W3DWaterXfer.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Water/W3DWaterSky.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Water/W3DWaterGrid.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Water/W3DWaterAreas.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Water/W3DWaterReal.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Water/W3DWaterRender.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Water/W3DWaterTracks.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Terrain/WorldHeightMap.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Display/camerashakesystem.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameLogic/W3DGameLogic.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameLogic/W3DGhostObject.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameLogic/W3DTerrainLogic.cpp
	${SHARED_PLATFORM_DIR}/Win32Device/Common/Win32BIGFileSystem.cpp
	${SHARED_PLATFORM_DIR}/Win32Device/Common/Win32GameEngine.cpp
	${SHARED_PLATFORM_DIR}/Win32Device/Common/Win32LocalFile.cpp
	${SHARED_PLATFORM_DIR}/Win32Device/Common/Win32LocalFileSystem.cpp
	${SHARED_PLATFORM_DIR}/Win32Device/Common/Win32Monitors.cpp
	${SHARED_PLATFORM_DIR}/Win32Device/Common/Win32OSDisplay.cpp
	${SHARED_PLATFORM_DIR}/Win32Device/GameClient/Win32Keyboard.cpp
	${SHARED_PLATFORM_DIR}/Win32Device/GameClient/Win32Mouse.cpp
	${SHARED_PLATFORM_DIR}/XAudio2Device/Mp3Parser.cpp
	${SHARED_PLATFORM_DIR}/XAudio2Device/WavParser.cpp
	${SHARED_PLATFORM_DIR}/XAudio2Device/XAudio2AudioManager.cpp
)

target_include_directories(GameEngineDevice PUBLIC
	${SHARED_PLATFORM_DIR}
)
target_include_directories(GameEngineDevice PRIVATE
	${SHARED_GAMEENGINE_DIR}
	${SHARED_ENGINE_DIR}
	${SHARED_LIB_INCLUDE}
	${SHARED_CORE_DIR}
	${SHARED_RENDERER_DIR}
	${SHARED_RENDERER_DIR}/WW3D2
	${WW3D2_SCENE_DIRS}
	${SHARED_CORE_DIR}/WWLib
	${SHARED_CORE_DIR}/WWDebug
	${SHARED_CORE_DIR}/WWMath
	${SHARED_CORE_DIR}/WWSaveLoad
)
# Forces windows.h ahead of Lib/BaseType.h in every .cpp file (BitTest macro collision,
# see ForceOrder.h).
target_compile_options(GameEngineDevice PRIVATE /FI"${SHARED_PLATFORM_DIR}/ForceOrder.h")

# Newly written code (XAudio2/FFmpeg backends) builds with /W4 /WX; the legacy rest of
# GameEngineDevice stays at /W3 (root CMakeLists.txt) because it is full of harmless old idioms.
# /wd4005 (WIN32_LEAN_AND_MEAN/BitTest/NOMINMAX), /wd4091 (GlobalData.h typedef) and /wd4018
# (signed/unsigned compares in GameClient.h/GameLogic.h) come from headers every file includes;
# suppressing them lets /WX catch only real problems in these files.
set_source_files_properties(
	${SHARED_PLATFORM_DIR}/XAudio2Device/XAudio2AudioManager.cpp
	${SHARED_PLATFORM_DIR}/XAudio2Device/WavParser.cpp
	${SHARED_PLATFORM_DIR}/VideoDevice/FFmpeg/FFmpegVideoPlayer.cpp
	${SHARED_PLATFORM_DIR}/W3DDevice/GameClient/Terrain/TerrainTileImage.cpp
	PROPERTIES COMPILE_OPTIONS "/W4;/WX;/wd4005;/wd4091;/wd4018"
)
# Mp3Parser.cpp/FFmpegFile.cpp additionally need /wd4244: depending on include order (xaudio2.h/
# x3daudio.h ahead of the avformat/avcodec headers here, unlike in FFmpegVideoPlayer.cpp) MSVC
# emits C4244 (int->uint8_t/int8_t/uint16_t/int16_t narrowing) inside libavutil/common.h's own
# av_clip_uint8_c()-style helpers -- third-party header code we don't control, not our narrowing.
set_source_files_properties(
	${SHARED_PLATFORM_DIR}/XAudio2Device/Mp3Parser.cpp
	${SHARED_PLATFORM_DIR}/VideoDevice/FFmpeg/FFmpegFile.cpp
	PROPERTIES COMPILE_OPTIONS "/W4;/WX;/wd4005;/wd4091;/wd4244;/wd4018"
)

# FFmpeg comes from the vcpkg manifest; its port provides a module-mode FindFFMPEG.cmake with
# ${FFMPEG_LIBRARIES}/${FFMPEG_INCLUDE_DIRS}.
find_package(FFMPEG REQUIRED)
target_include_directories(GameEngineDevice PRIVATE ${FFMPEG_INCLUDE_DIRS})
# xaudio2.lib is needed only for the X3DAudioInitialize/X3DAudioCalculate imports
# (X3DAudio lives in xaudio2.lib; XAudio2Create() itself needs no import lib).
target_link_libraries(GameEngineDevice PUBLIC GameEngineGameNetwork WW3D2 ${FFMPEG_LIBRARIES} xaudio2)
# DirectXTex reads DDS terrain tiles and decompresses BC1/BC7 (TerrainTileImage.cpp).
find_package(directxtex CONFIG REQUIRED)
target_link_libraries(GameEngineDevice PRIVATE Microsoft::DirectXTex)
