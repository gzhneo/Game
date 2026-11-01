# Compression library shared by both games (GZHNeo and GZHNeoZH link the same target).
# NoxCompress.cpp is excluded because the proprietary LZHCompress/CompLibHeader SDK is missing.
add_library(Compression STATIC
	CompressionManager.cpp
	EAC/btreeabout.cpp
	EAC/btreedecode.cpp
	EAC/btreeencode.cpp
	EAC/huffabout.cpp
	EAC/huffdecode.cpp
	EAC/huffencode.cpp
	EAC/refabout.cpp
	EAC/refdecode.cpp
	EAC/refencode.cpp
)

target_include_directories(Compression PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}
)
target_include_directories(Compression PRIVATE
	${SHARED_LIB_INCLUDE}
)

find_package(ZLIB REQUIRED)
target_link_libraries(Compression PRIVATE ZLIB::ZLIB)
