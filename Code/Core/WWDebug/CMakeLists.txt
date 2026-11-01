# Shared library for both games (GZHNeo and GZHNeoZH link the same target).
add_library(WWDebug STATIC
	wwdebug.cpp
	wwtracy.cpp
)

target_include_directories(WWDebug PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# WWDebug is the lowest library; from here the Tracy client and TRACY_ENABLE reach every target
# that links WWDebug directly or transitively.
if(ENABLE_TRACY)
	target_link_libraries(WWDebug PUBLIC Tracy::TracyClient)
	target_compile_definitions(WWDebug PUBLIC TRACY_ENABLE)
endif()
# Circular header dependency WWDebug<->WWLib: wwdebug.h needs WWLib headers, WWLib links WWDebug.
target_include_directories(WWDebug PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../WWLib)
# Common/debug.h and Engine/FormatCheck.h via wwdebug.h: PUBLIC so that targets without their own
# GameEngine (WW3D2, tools) find the headers too.
target_include_directories(WWDebug PUBLIC ${SHARED_GAMEENGINE_DIR} ${SHARED_ENGINE_DIR})
