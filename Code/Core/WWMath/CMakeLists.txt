# Shared library for both games (GZHNeo and GZHNeoZH link the same target).
add_library(WWMath STATIC
	aabox.cpp
	colmath.cpp
	colmathaabox.cpp
	colmathaabtri.cpp
	colmathfrustum.cpp
	colmathline.cpp
	colmathobbobb.cpp
	colmathobbox.cpp
	colmathobbtri.cpp
	colmathplane.cpp
	colmathsphere.cpp
	cullsys.cpp
	frustum.cpp
	lineseg.cpp
	matrix3.cpp
	matrix3d.cpp
	matrix4.cpp
	obbox.cpp
	pot.cpp
	quat.cpp
	tri.cpp
	v3_rnd.cpp
	vp.cpp
	wwmath.cpp
)

target_include_directories(WWMath PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(WWMath PRIVATE G_CODE_BASE)
target_link_libraries(WWMath PUBLIC WWLib WWDebug WWSaveLoad)
