# Babylon, the translation tool: database model and core (Model, Core) and the command line tool
# BabylonTool. Game-neutral, no engine, no GUI and no office program, so they are built once from the
# root CMakeLists.txt, also without BUILD_TOOLS. The application (App, Qt 6) is built with the map
# editor (Code/Tools/Editors/MapEditor/CMakeLists.txt, option BUILD_MAPEDITOR_APP), next to its Qt DLLs.
# Linux build of the same sources: tools/babylon/linux_build.sh, the application: tools/babylon/gui_linux.sh.
find_package(ZLIB REQUIRED)

add_library(BabylonModel STATIC
	Model/CsfFile.cpp
	Model/CsfFile.h
	Model/Database.cpp
	Model/Database.h
	Model/DbFile.cpp
	Model/DbFile.h
	Model/Ods.cpp
	Model/Sheet.cpp
	Model/Sheet.h
	Model/StrFile.cpp
	Model/StrFile.h
	Model/Text.cpp
	Model/Text.h
	Model/Xlsx.cpp
	Model/Xml.cpp
	Model/Xml.h
	Model/Zip.cpp
	Model/Zip.h
	Core/Checks.cpp
	Core/Checks.h
	Core/Exchange.cpp
	Core/Exchange.h
	Core/GameImport.cpp
	Core/GameImport.h
	Core/Generate.cpp
	Core/Generate.h
	Core/Project.cpp
	Core/Project.h
	Core/Reports.cpp
	Core/Reports.h
	Core/Update.cpp
	Core/Update.h
	# The engine's ZIP reader and its .csf/.str readers (tests and self test check the generated
	# files with them); both need no engine header.
	${SHARED_ENGINE_DIR}/Engine/TextFiles.cpp
	${SHARED_ENGINE_DIR}/Engine/ZipArchive.cpp
)
target_include_directories(BabylonModel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${SHARED_ENGINE_DIR})
target_link_libraries(BabylonModel PRIVATE ZLIB::ZLIB)
target_compile_options(BabylonModel PRIVATE ${ENGINE_STRICT_OPTIONS} $<$<CXX_COMPILER_ID:MSVC>:/WX>)

add_executable(BabylonTool Cli/BabylonTool.cpp)
target_link_libraries(BabylonTool PRIVATE BabylonModel)
target_compile_options(BabylonTool PRIVATE ${ENGINE_STRICT_OPTIONS} $<$<CXX_COMPILER_ID:MSVC>:/WX>)
