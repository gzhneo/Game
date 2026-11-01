# E-8.4: Fuzzing-Ziele.
#
# Standardmaessig AUS. Mit -DENABLE_FUZZERS=ON entstehen die Ziele; unter MSVC zusaetzlich mit
# /fsanitize=fuzzer (libFuzzer) und /fsanitize=address, sonst als normale Programme, die ihre
# Eingabedateien als Argumente nehmen (so arbeitet tools/fuzz/dumbfuzz.py).
#
#   cmake --preset x64-debug -DENABLE_FUZZERS=ON
#   cmake --build build-x64 --target FuzzCompression
#   build-x64\tools\fuzz\Debug\FuzzCompression.exe -max_total_time=3600 tools\fuzz\seeds\compression

add_executable(FuzzCompression
	FuzzCompression.cpp
)

target_include_directories(FuzzCompression PRIVATE
	${SHARED_CODE_DIR}/Core/Compression
	${SHARED_LIB_INCLUDE}
)

target_link_libraries(FuzzCompression PRIVATE Compression)

if(MSVC)
	# libFuzzer bringt sein eigenes main() mit; die Datei schaltet ihres dann ab.
	target_compile_definitions(FuzzCompression PRIVATE FUZZ_LIBFUZZER_MAIN)
	target_compile_options(FuzzCompression PRIVATE /fsanitize=fuzzer /fsanitize=address)
	target_link_options(FuzzCompression PRIVATE /INCREMENTAL:NO)
endif()

# Ein kurzer Lauf ueber die Saatdateien gehoert in jeden ctest-Durchlauf: er prueft, dass die
# Ziele ueberhaupt bauen und dass die bekannten Eingaben sauber durchlaufen (Regression zu den
# Funden aus E-8.4).
file(GLOB FUZZ_COMPRESSION_SEEDS "${CMAKE_CURRENT_SOURCE_DIR}/seeds/compression/*.bin")
if(FUZZ_COMPRESSION_SEEDS AND NOT MSVC)
	add_test(NAME FuzzCompressionSeeds COMMAND FuzzCompression ${FUZZ_COMPRESSION_SEEDS})
endif()

# Map model of the new map editor (Code/Tools/Editors/MapEditor/Model): chunk container and all map chunks.
# The sources are compiled in (not the MapEditorModel library) so that all of them carry the same
# sanitizer settings; MSVC refuses to link STL code built with and without /fsanitize=address.
add_executable(FuzzMapReader
	FuzzMapReader.cpp
	${SHARED_TOOLS_DIR}/Editors/MapEditor/Model/ByteIO.cpp
	${SHARED_TOOLS_DIR}/Editors/MapEditor/Model/Chunk.cpp
	${SHARED_TOOLS_DIR}/Editors/MapEditor/Model/MapIO.cpp
)
target_include_directories(FuzzMapReader PRIVATE ${SHARED_TOOLS_DIR}/Editors/MapEditor)
if(MSVC)
	target_compile_definitions(FuzzMapReader PRIVATE FUZZ_LIBFUZZER_MAIN)
	target_compile_options(FuzzMapReader PRIVATE /fsanitize=fuzzer /fsanitize=address)
	target_link_options(FuzzMapReader PRIVATE /INCREMENTAL:NO)
endif()
file(GLOB FUZZ_MAP_SEEDS "${CMAKE_CURRENT_SOURCE_DIR}/seeds/map/*.map")
if(FUZZ_MAP_SEEDS AND NOT MSVC)
	add_test(NAME FuzzMapReaderSeeds COMMAND FuzzMapReader ${FUZZ_MAP_SEEDS})
endif()

# Readers of the translation tool Babylon (main.db, sheets, Generals.str, .csf); sources compiled in
# for the same reason as above.
file(GLOB FUZZ_BABYLON_SOURCES ${SHARED_TOOLS_DIR}/Editors/Babylon/Model/*.cpp ${SHARED_TOOLS_DIR}/Editors/Babylon/Core/*.cpp)
add_executable(FuzzBabylon
	FuzzBabylon.cpp
	${FUZZ_BABYLON_SOURCES}
	${SHARED_ENGINE_DIR}/Engine/TextFiles.cpp
	${SHARED_ENGINE_DIR}/Engine/ZipArchive.cpp
)
target_include_directories(FuzzBabylon PRIVATE ${SHARED_TOOLS_DIR}/Editors/Babylon ${SHARED_ENGINE_DIR})
find_package(ZLIB REQUIRED)
target_link_libraries(FuzzBabylon PRIVATE ZLIB::ZLIB)
if(MSVC)
	target_compile_definitions(FuzzBabylon PRIVATE FUZZ_LIBFUZZER_MAIN)
	target_compile_options(FuzzBabylon PRIVATE /fsanitize=fuzzer /fsanitize=address)
	target_link_options(FuzzBabylon PRIVATE /INCREMENTAL:NO)
endif()
file(GLOB FUZZ_BABYLON_SEEDS "${CMAKE_CURRENT_SOURCE_DIR}/seeds/babylon/*")
if(FUZZ_BABYLON_SEEDS AND NOT MSVC)
	add_test(NAME FuzzBabylonSeeds COMMAND FuzzBabylon ${FUZZ_BABYLON_SEEDS})
endif()

# Verzeichnis eines .big-Archivs (Engine::readBigArchiveDirectory); braucht keinen Engine-Header.
add_executable(FuzzBigArchive
	FuzzBigArchive.cpp
	${SHARED_ENGINE_DIR}/Engine/BigArchive.cpp
)
target_include_directories(FuzzBigArchive PRIVATE ${SHARED_GAMEENGINE_DIR} ${SHARED_ENGINE_DIR})
if(MSVC)
	target_compile_definitions(FuzzBigArchive PRIVATE FUZZ_LIBFUZZER_MAIN)
	target_compile_options(FuzzBigArchive PRIVATE /fsanitize=fuzzer /fsanitize=address)
	target_link_options(FuzzBigArchive PRIVATE /INCREMENTAL:NO)
endif()
file(GLOB FUZZ_BIG_SEEDS "${CMAKE_CURRENT_SOURCE_DIR}/seeds/big/*.big")
if(FUZZ_BIG_SEEDS AND NOT MSVC)
	add_test(NAME FuzzBigArchiveSeeds COMMAND FuzzBigArchive ${FUZZ_BIG_SEEDS})
endif()

# Zentralverzeichnis und Einträge eines .zip-Archivs (Engine::readZipArchiveDirectory, extractZipEntry); braucht zlib.
add_executable(FuzzZipArchive
	FuzzZipArchive.cpp
	${SHARED_ENGINE_DIR}/Engine/ZipArchive.cpp
)
target_include_directories(FuzzZipArchive PRIVATE ${SHARED_GAMEENGINE_DIR} ${SHARED_ENGINE_DIR})
find_package(ZLIB REQUIRED)
target_link_libraries(FuzzZipArchive PRIVATE ZLIB::ZLIB)
if(MSVC)
	target_compile_definitions(FuzzZipArchive PRIVATE FUZZ_LIBFUZZER_MAIN)
	target_compile_options(FuzzZipArchive PRIVATE /fsanitize=fuzzer /fsanitize=address)
	target_link_options(FuzzZipArchive PRIVATE /INCREMENTAL:NO)
endif()
file(GLOB FUZZ_ZIP_SEEDS "${CMAKE_CURRENT_SOURCE_DIR}/seeds/zip/*.zip")
if(FUZZ_ZIP_SEEDS AND NOT MSVC)
	add_test(NAME FuzzZipArchiveSeeds COMMAND FuzzZipArchive ${FUZZ_ZIP_SEEDS})
endif()

# Stringtabellen .csf und .str (Engine::readCsf, Engine::readStr); braucht keinen Engine-Header.
add_executable(FuzzTextFiles
	FuzzTextFiles.cpp
	${SHARED_ENGINE_DIR}/Engine/TextFiles.cpp
)
target_include_directories(FuzzTextFiles PRIVATE ${SHARED_GAMEENGINE_DIR} ${SHARED_ENGINE_DIR})
if(MSVC)
	target_compile_definitions(FuzzTextFiles PRIVATE FUZZ_LIBFUZZER_MAIN)
	target_compile_options(FuzzTextFiles PRIVATE /fsanitize=fuzzer /fsanitize=address)
	target_link_options(FuzzTextFiles PRIVATE /INCREMENTAL:NO)
endif()
file(GLOB FUZZ_TEXT_SEEDS "${CMAKE_CURRENT_SOURCE_DIR}/seeds/text/*")
if(FUZZ_TEXT_SEEDS AND NOT MSVC)
	add_test(NAME FuzzTextFilesSeeds COMMAND FuzzTextFiles ${FUZZ_TEXT_SEEDS})
endif()

# Sitzungs- und Treffpunkt-Nachrichten, Einladungscodes und der Treffpunkt-Server.
add_executable(FuzzSession
	FuzzSession.cpp
	${SHARED_ENGINE_DIR}/Engine/Wire.cpp
	${SHARED_ENGINE_DIR}/Engine/SessionMessage.cpp
	${SHARED_ENGINE_DIR}/Engine/Rendezvous.cpp
	${SHARED_ENGINE_DIR}/Engine/RendezvousServer.cpp
	${SHARED_ENGINE_DIR}/Engine/InviteCode.cpp
	${SHARED_ENGINE_DIR}/Engine/NetAddress.cpp
	${SHARED_ENGINE_DIR}/Engine/RateLimiter.cpp
	${SHARED_ENGINE_DIR}/Engine/Monocypher.cpp
)
target_include_directories(FuzzSession PRIVATE ${SHARED_GAMEENGINE_DIR} ${SHARED_ENGINE_DIR} ${SHARED_CORE_DIR})
if(MSVC)
	target_compile_definitions(FuzzSession PRIVATE FUZZ_LIBFUZZER_MAIN)
	target_compile_options(FuzzSession PRIVATE /fsanitize=fuzzer /fsanitize=address)
	target_link_options(FuzzSession PRIVATE /INCREMENTAL:NO)
endif()
file(GLOB FUZZ_SESSION_SEEDS "${CMAKE_CURRENT_SOURCE_DIR}/seeds/session/*")
if(FUZZ_SESSION_SEEDS AND NOT MSVC)
	add_test(NAME FuzzSessionSeeds COMMAND FuzzSession ${FUZZ_SESSION_SEEDS})
endif()
