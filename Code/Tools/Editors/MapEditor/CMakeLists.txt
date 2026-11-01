# New map editor: the map model (plain values; reading and writing .map files, compression via
# the game's Compression library), the editor core and the MapTool command line. Game-neutral, no
# engine, no GUI, so they are built once from the root CMakeLists.txt, also without BUILD_TOOLS.
# Linux build of the same sources: tools/mapeditor/linux_build.sh.
add_library(MapEditorModel STATIC
	Model/ByteIO.cpp
	Model/ByteIO.h
	Model/Chunk.cpp
	Model/Chunk.h
	Model/CompressionCodec.cpp
	Model/CompressionCodec.h
	Model/Map.h
	Model/MapDump.cpp
	Model/MapDump.h
	Model/MapFile.cpp
	Model/MapFile.h
	Model/MapIO.cpp
	Model/MapIO.h
)
target_include_directories(MapEditorModel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Compression.h includes Lib/BaseType.h; only CompressionCodec.cpp includes it.
target_include_directories(MapEditorModel PRIVATE ${SHARED_LIB_INCLUDE})
target_link_libraries(MapEditorModel PRIVATE Compression)
target_compile_options(MapEditorModel PRIVATE ${ENGINE_STRICT_OPTIONS} $<$<CXX_COMPILER_ID:MSVC>:/WX>)

# Editor core: commands, undo history, selection. Plain C++ on the map model, tested without GUI.
add_library(MapEditorCore STATIC
	Core/AreaCommands.cpp
	Core/AreaCommands.h
	Core/BuildListCommands.cpp
	Core/BuildListCommands.h
	Core/CliffTexture.cpp
	Core/CliffTexture.h
	Core/Contours.cpp
	Core/Contours.h
	Core/Fairness.cpp
	Core/Fairness.h
	Core/GameLaunch.cpp
	Core/GameLaunch.h
	Core/GraphicsLevels.cpp
	Core/GraphicsLevels.h
	Core/HeightImage.cpp
	Core/HeightImage.h
	Core/HeightShapes.cpp
	Core/HeightShapes.h
	Core/History.cpp
	Core/History.h
	Core/LayerCommands.cpp
	Core/LayerCommands.h
	Core/MapCheck.cpp
	Core/MapCheck.h
	Core/MapCompare.cpp
	Core/MapCompare.h
	Core/MapPreview.cpp
	Core/MapPreview.h
	Core/MapSettingsCommands.cpp
	Core/MapSettingsCommands.h
	Core/MapSize.cpp
	Core/MapSize.h
	Core/ObjectCommands.cpp
	Core/ObjectCommands.h
	Core/ParticleIni.cpp
	Core/ParticleIni.h
	Core/PlacementCommands.cpp
	Core/PlacementCommands.h
	Core/RoadCommands.cpp
	Core/RoadCommands.h
	Core/ScriptCommands.cpp
	Core/ScriptCommands.h
	Core/ScriptExchange.cpp
	Core/ScriptExchange.h
	Core/SelectionTools.cpp
	Core/SelectionTools.h
	Core/SidesCommands.cpp
	Core/SidesCommands.h
	Core/References.cpp
	Core/References.h
	Core/Slope.cpp
	Core/Symmetry.cpp
	Core/Symmetry.h
	Core/Slope.h
	Core/TerrainCommands.cpp
	Core/TerrainCommands.h
	Core/TextureBlend.cpp
	Core/TextureBlend.h
	Core/TextureCommands.cpp
	Core/TextureCommands.h
	Core/WaypointCommands.cpp
	Core/WaypointCommands.h
	Core/WaterLook.cpp
	Core/WaterLook.h
)
target_link_libraries(MapEditorCore PUBLIC MapEditorModel)
target_compile_options(MapEditorCore PRIVATE ${ENGINE_STRICT_OPTIONS} $<$<CXX_COMPILER_ID:MSVC>:/WX>)

add_executable(MapTool Cli/MapTool.cpp)
target_link_libraries(MapTool PRIVATE MapEditorModel)
target_compile_options(MapTool PRIVATE ${ENGINE_STRICT_OPTIONS} $<$<CXX_COMPILER_ID:MSVC>:/WX>)

# The editor application (Qt 6 Widgets and Svg). Off by default so the game build needs no Qt; the
# preset x64-editor turns it on together with the vcpkg manifest feature "editor" that brings Qt.
# Windows build script: tools/mapeditor/bauen_windows.cmd.
option(BUILD_MAPEDITOR_APP "Build the map editor application (needs Qt 6, vcpkg feature 'editor')" OFF)
if(BUILD_MAPEDITOR_APP)
	find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)

	# MapEditorEngine.dll: engine start, terrain and 3D view (Engine/EngineApi.h). A DLL because the
	# engine replaces the global operator new; the editor loads it at runtime.
	add_library(MapEditorEngine SHARED
		Engine/EditorAudio.h
		Engine/EngineApi.h
		Engine/EngineCatalog.cpp
		Engine/EngineInternal.h
		Engine/EngineMolds.cpp
		Engine/EngineScripts.cpp
		Engine/EngineObjects.cpp
		Engine/EngineOverlay.cpp
		Engine/EngineParticles.cpp
		Engine/EnginePreview.cpp
		Engine/EngineTerrain.cpp
		Engine/EngineStart.cpp
		Engine/EngineTextures.cpp
		Engine/EngineView.cpp
		Engine/EngineWindows.cpp
		Engine/MemoryStream.h
	)
	target_compile_definitions(MapEditorEngine PRIVATE MAPEDITOR_ENGINE_BUILD)
	target_include_directories(MapEditorEngine PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${SHARED_LIB_INCLUDE}
		${SHARED_GAMEENGINE_DIR}
		${SHARED_ENGINE_DIR}
		${SHARED_PLATFORM_DIR}
	)
	# Same include order as GameEngineDevice (windows.h before Lib/BaseType.h, see ForceOrder.h).
	target_compile_options(MapEditorEngine PRIVATE /FI"${SHARED_PLATFORM_DIR}/ForceOrder.h")
	# No GameEdition library: the DLL answers getGameEdition() itself (EngineStart.cpp).
	# Texture thumbnails decode TGA and DDS with DirectXTex, as the renderer does.
	find_package(directxtex CONFIG REQUIRED)
	target_link_libraries(MapEditorEngine PRIVATE GameEngineDevice WWLib Microsoft::DirectXTex winmm imm32)

	# Shared by the map editor and the GUI editor: theme, widget helpers, languages, icons, self test lines.
	set(EDITORCOMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../EditorCommon")
	add_library(EditorCommon STATIC
		${EDITORCOMMON_DIR}/Icons.cpp
		${EDITORCOMMON_DIR}/Icons.h
		${EDITORCOMMON_DIR}/Language.cpp
		${EDITORCOMMON_DIR}/Language.h
		${EDITORCOMMON_DIR}/SelfTest.h
		${EDITORCOMMON_DIR}/Theme.cpp
		${EDITORCOMMON_DIR}/Theme.h
		${EDITORCOMMON_DIR}/Widgets.cpp
		${EDITORCOMMON_DIR}/Widgets.h
	)
	target_include_directories(EditorCommon PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
	target_link_libraries(EditorCommon PUBLIC Qt6::Widgets Qt6::Svg)
	target_compile_options(EditorCommon PRIVATE ${ENGINE_STRICT_OPTIONS} $<$<CXX_COMPILER_ID:MSVC>:/WX /utf-8>)

	add_executable(MapEditor WIN32
		App/AppState.cpp
		App/AppState.h
		App/DemoMap.cpp
		App/DemoMap.h
		App/EngineClient.cpp
		App/EngineClient.h
		App/Icons.h
		App/Language.h
		App/main.cpp
		App/MainWindow.cpp
		App/MainWindowCommandNames.cpp
		App/MainWindowFiles.cpp
		App/MainWindowMenus.cpp
		App/MainWindowPreferences.cpp
		App/MainWindowStart.cpp
		App/MainWindowView3D.cpp
		App/MainWindowViewer.cpp
		App/MainWindow.h
		App/MainWindowParts.h
		App/ParticleCurves.h
		App/SunDial.h
		App/MainWindowAreas.cpp
		App/MainWindowBuildList.cpp
		App/MainWindowCamera.cpp
		App/MainWindowCatalog.cpp
		App/MainWindowCompare.cpp
		App/MainWindowDisplay.cpp
		App/MainWindowEdit.cpp
		App/MainWindowProps.cpp
		App/MainWindowLayers.cpp
		App/MainWindowMapSize.cpp
		App/MainWindowPalette.cpp
		App/MainWindowParticles.cpp
		App/MainWindowPlacement.cpp
		App/MainWindowPoster.cpp
		App/MainWindowHistory.cpp
		App/MainWindowSearch.cpp
		App/MainWindowFairness.cpp
		App/MainWindowHelp.cpp
		App/MainWindowSymmetry.cpp
		App/MainWindowPrefabs.cpp
		App/MainWindowProblems.cpp
		App/MainWindowRoads.cpp
		App/MainWindowScripts.cpp
		App/MainWindowSession.cpp
		App/MainWindowSettings.cpp
		App/MainWindowShortcuts.cpp
		App/MainWindowSides.cpp
		App/MainWindowSound.cpp
		App/MainWindowTerrain.cpp
		App/MainWindowTextures.cpp
		App/MainWindowPathing.cpp
		App/MainWindowShapes.cpp
		App/MainWindowTools.cpp
		App/MainWindowWater.cpp
		App/MainWindowWaypoints.cpp
		App/MapImage.cpp
		App/MapImage.h
		App/MapOverview.cpp
		App/MapOverview.h
		App/MiniMap.cpp
		App/MiniMap.h
		App/Overlay3D.cpp
		App/Overlay3D.h
		App/Theme.h
		App/ToolEvent.h
		App/Viewport3D.cpp
		App/Viewport3D.h
		App/Common.cpp
		App/Common.h
		App/EditorContext.h
		App/ScriptCatalog.cpp
		App/ScriptCatalog.h
		App/SelfTest.h
		App/Windows/FairnessWindow.cpp
		App/Windows/FairnessWindow.h
		App/Windows/HelpWindow.cpp
		App/Windows/HelpWindow.h
		App/Windows/HistoryWindow.cpp
		App/Windows/HistoryWindow.h
		App/Windows/MapSettingsWindow.cpp
		App/Windows/MapSettingsWindow.h
		App/Windows/ParticlesWindow.cpp
		App/Windows/ParticlesWindow.h
		App/Windows/ScriptsWindow.cpp
		App/Windows/ScriptsWindow.h
		App/Windows/SearchWindow.cpp
		App/Windows/SearchWindow.h
		App/Windows/SidesWindow.cpp
		App/Windows/SidesWindow.h
		App/Windows/SymmetryDialog.cpp
		App/Windows/SymmetryDialog.h
		App/Windows/TextListModel.h
		App/SelfTests/AreasTests.cpp
		App/SelfTests/BuildListTests.cpp
		App/SelfTests/CameraTests.cpp
		App/SelfTests/CatalogTests.cpp
		App/SelfTests/CompareTests.cpp
		App/SelfTests/DisplayTests.cpp
		App/SelfTests/EditTests.cpp
		App/SelfTests/FairnessTests.cpp
		App/SelfTests/HelpTests.cpp
		App/SelfTests/HistoryTests.cpp
		App/SelfTests/LayersTests.cpp
		App/SelfTests/MapSizeTests.cpp
		App/SelfTests/ParticlesTests.cpp
		App/SelfTests/PlacementTests.cpp
		App/SelfTests/PosterTests.cpp
		App/SelfTests/PrefabsTests.cpp
		App/SelfTests/ProblemsTests.cpp
		App/SelfTests/RoadsTests.cpp
		App/SelfTests/ScriptsTests.cpp
		App/SelfTests/SearchTests.cpp
		App/SelfTests/SessionTests.cpp
		App/SelfTests/SettingsTests.cpp
		App/SelfTests/ShortcutsTests.cpp
		App/SelfTests/SidesTests.cpp
		App/SelfTests/SoundTests.cpp
		App/SelfTests/SymmetryTests.cpp
		App/SelfTests/TerrainTests.cpp
		App/SelfTests/ToolsTests.cpp
		App/SelfTests/WaterTests.cpp
		App/SelfTests/WaypointsTests.cpp
		App/Widgets.h
		App/Resources/MapEditor.qrc
	)
	set_target_properties(MapEditor PROPERTIES AUTORCC ON AUTOMOC ON)
	target_link_libraries(MapEditor PRIVATE MapEditorCore EditorCommon Qt6::Widgets Qt6::Svg $<$<PLATFORM_ID:Windows>:dwmapi>)
	# /utf-8: narrow literals with \u escapes reach T() as UTF-8.
	target_compile_options(MapEditor PRIVATE ${ENGINE_STRICT_OPTIONS} $<$<CXX_COMPILER_ID:MSVC>:/WX /utf-8>)
	# The editor loads the DLL at runtime; it must be built with it and lies in the same folder.
	add_dependencies(MapEditor MapEditorEngine)
	# The engine reads the HLSL shaders from Shaders\ next to the editor (EngineClient.cpp).
	add_custom_command(TARGET MapEditor POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHARED_DATA_DIR}/Shaders" "$<TARGET_FILE_DIR:MapEditor>/Shaders")
	# The shader compiler of the Vulkan renderer (-vulkan, both editors), loaded from the exe's folder.
	if(GZH_RENDER_VULKAN)
		add_custom_command(TARGET MapEditor POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Microsoft::DirectXShaderCompiler>" "$<TARGET_FILE_DIR:MapEditor>")
	endif()
	# The project's textures of the game's detail levels (ground detail, rock, water), which the engine
	# reads from ProjectData\Textures next to the exe, as for the game (Code/App/CMakeLists.txt).
	add_custom_command(TARGET MapEditor POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHARED_DATA_DIR}/Textures" "$<TARGET_FILE_DIR:MapEditor>/ProjectData/Textures")
	# Qt plugins next to the exe, from the imported plugin targets so Debug gets the debug plugins.
	# The Qt DLLs themselves come with vcpkg's app-local copy step.
	set(_mapeditor_plugins
		Qt6::QWindowsIntegrationPlugin platforms
		Qt6::QModernWindowsStylePlugin styles
		Qt6::QWindowsVistaStylePlugin styles
		Qt6::QSvgPlugin imageformats
		Qt6::QSvgIconPlugin iconengines)
	while(_mapeditor_plugins)
		list(POP_FRONT _mapeditor_plugins _plugin _folder)
		if(TARGET ${_plugin})
			add_custom_command(TARGET MapEditor POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:MapEditor>/${_folder}"
				COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${_plugin}>" "$<TARGET_FILE_DIR:MapEditor>/${_folder}/"
				VERBATIM)
		endif()
	endwhile()

	# The GUI editor (Code/Tools/Editors/GuiEditor): window layouts with the same engine DLL. Built here so it
	# lands next to the DLL, the shaders and the Qt plugins of the map editor.
	set(GUIEDITOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../GuiEditor")
	add_executable(GuiEditor WIN32
		${GUIEDITOR_DIR}/App/EditorArrange.cpp
		${GUIEDITOR_DIR}/App/EditorCatalog.cpp
		${GUIEDITOR_DIR}/App/EditorEditing.cpp
		${GUIEDITOR_DIR}/App/EditorProperties.cpp
		${GUIEDITOR_DIR}/App/EditorSchemes.cpp
		${GUIEDITOR_DIR}/App/EditorShared.h
		${GUIEDITOR_DIR}/App/EditorTest.cpp
		${GUIEDITOR_DIR}/App/EditorWindow.cpp
		${GUIEDITOR_DIR}/App/EditorWindow.h
		${GUIEDITOR_DIR}/App/GuiEngine.cpp
		${GUIEDITOR_DIR}/App/GuiEngine.h
		${GUIEDITOR_DIR}/App/LayoutView.cpp
		${GUIEDITOR_DIR}/App/LayoutView.h
		${GUIEDITOR_DIR}/App/main.cpp
		${GUIEDITOR_DIR}/App/Picker.cpp
		${GUIEDITOR_DIR}/App/Picker.h
		${GUIEDITOR_DIR}/App/SelfTest.cpp
		${GUIEDITOR_DIR}/App/SelfTest.h
		${GUIEDITOR_DIR}/App/WindowTree.h
		${GUIEDITOR_DIR}/App/Resources/GuiEditor.qrc
	)
	set_target_properties(GuiEditor PROPERTIES AUTORCC ON)
	# Its own App folder first, then the map editor's for Engine/EngineApi.h.
	target_include_directories(GuiEditor PRIVATE ${GUIEDITOR_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
	target_link_libraries(GuiEditor PRIVATE EditorCommon GuiEditorModel Qt6::Widgets)
	target_compile_options(GuiEditor PRIVATE ${ENGINE_STRICT_OPTIONS} $<$<CXX_COMPILER_ID:MSVC>:/WX /utf-8>)
	add_dependencies(GuiEditor MapEditorEngine MapEditor)

	# The translation tool Babylon (Code/Tools/Editors/Babylon/App): Qt only, no engine. Built here so it lands
	# next to the Qt DLLs and plugins of the map editor.
	set(BABYLON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Babylon")
	add_executable(Babylon WIN32
		${BABYLON_DIR}/App/Dialogs.cpp
		${BABYLON_DIR}/App/Dialogs.h
		${BABYLON_DIR}/App/main.cpp
		${BABYLON_DIR}/App/MainWindow.cpp
		${BABYLON_DIR}/App/MainWindow.h
		${BABYLON_DIR}/App/SelfTest.cpp
		${BABYLON_DIR}/App/SelfTest.h
		${BABYLON_DIR}/App/Resources/Babylon.qrc
		${BABYLON_DIR}/App/Resources/Babylon.rc
	)
	set_target_properties(Babylon PROPERTIES AUTORCC ON)
	target_include_directories(Babylon PRIVATE ${BABYLON_DIR})
	target_link_libraries(Babylon PRIVATE BabylonModel EditorCommon Qt6::Widgets)
	target_compile_options(Babylon PRIVATE ${ENGINE_STRICT_OPTIONS} $<$<CXX_COMPILER_ID:MSVC>:/WX /utf-8>)
	add_dependencies(Babylon MapEditor)
endif()
