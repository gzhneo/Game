# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
#
# gzh-rendezvous: rendezvous server for network games (README.md next to this file). Only engine
# building blocks without engine headers, so it also builds on Linux (tools/rendezvous/build_linux.sh).

set(RENDEZVOUS_ENGINE ${SHARED_ENGINE_DIR}/Engine)
add_executable(gzh-rendezvous
	main.cpp
	${RENDEZVOUS_ENGINE}/Wire.cpp
	${RENDEZVOUS_ENGINE}/Rendezvous.cpp
	${RENDEZVOUS_ENGINE}/RendezvousServer.cpp
	${RENDEZVOUS_ENGINE}/InviteCode.cpp
	${RENDEZVOUS_ENGINE}/NetAddress.cpp
	${RENDEZVOUS_ENGINE}/RateLimiter.cpp
	${RENDEZVOUS_ENGINE}/Monocypher.cpp
)
target_include_directories(gzh-rendezvous PRIVATE ${SHARED_GAMEENGINE_DIR} ${SHARED_ENGINE_DIR} ${SHARED_CORE_DIR})
target_compile_options(gzh-rendezvous PRIVATE ${ENGINE_STRICT_OPTIONS} $<$<CXX_COMPILER_ID:MSVC>:/WX>)
if(WIN32)
	target_link_libraries(gzh-rendezvous PRIVATE ws2_32)
endif()
add_test(NAME RendezvousSelftest COMMAND gzh-rendezvous --selftest)
