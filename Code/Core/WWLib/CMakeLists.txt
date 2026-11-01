# Shared library for both games (GZHNeo and GZHNeoZH link the same target).
add_library(WWLib STATIC
	TARGA.CPP
	buff.cpp
	bufffile.cpp
	chunkio.cpp
	crc.cpp
	cstraw.cpp
	ffactory.cpp
	gcd_lcm.cpp
	hash.cpp
	ini.cpp
	jshell.cpp
	multilist.cpp
	mutex.cpp
	nstrdup.cpp
	pipe.cpp
	ramfile.cpp
	random.cpp
	rawfile.cpp
	readline.cpp
	realcrc.cpp
	refcount.cpp
	sampler.cpp
	slnode.cpp
	straw.cpp
	strtok_r.cpp
	systimer.cpp
	textfile.cpp
	thread.cpp
	trim.cpp
	vector.cpp
	widestring.cpp
	win.cpp
	wwfile.cpp
	wwstring.cpp
	xpipe.cpp
	xstraw.cpp
)

# The WWVegas folder itself too, so includes of the form "WWLib/x.h" resolve for every user.
target_include_directories(WWLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
# Code/Core for "Lib/SafeString.h" (safe_strcpy/safe_strcat), also for every user
# of the WWLib headers.
target_include_directories(WWLib PUBLIC ${SHARED_LIB_INCLUDE})
target_compile_definitions(WWLib PRIVATE REGEX_MALLOC STDC_HEADERS)
target_link_libraries(WWLib PUBLIC WWDebug)
