# Shared library for both games (GZHNeo and GZHNeoZH link the same target).
add_library(WWSaveLoad STATIC
	persistfactory.cpp
	pointerremap.cpp
	saveload.cpp
	saveloadstatus.cpp
	saveloadsubsystem.cpp
)

target_include_directories(WWSaveLoad PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(WWSaveLoad PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../WWMath)
target_link_libraries(WWSaveLoad PUBLIC WWLib WWDebug)
