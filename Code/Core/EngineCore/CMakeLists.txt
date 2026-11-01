# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
#
# EngineCore: the engine building blocks (clock, logging, job system, file system, archives, network
# messages, settings, ...). They depend on no engine header (no PreRTS.h, no singletons, no WWLib), so
# GameEngineCommon, the tests and the small tools can use them alone. Headers: #include "Engine/X.h".

add_library(EngineCore STATIC
	Engine/BigArchive.cpp
	Engine/Clock.cpp
	Engine/CpuTopology.cpp
	Engine/EngineContext.cpp
	Engine/EngineLog.cpp
	Engine/InviteCode.cpp
	Engine/JobSystem.cpp
	Engine/LogicAccessGuard.cpp
	Engine/ModCatalog.cpp
	Engine/NetAddress.cpp
	Engine/NetAddressBook.cpp
	Engine/NetEmulator.cpp
	Engine/PacketSeal.cpp
	Engine/PeerLink.cpp
	Engine/PositionalFile.cpp
	Engine/PortMapping.cpp
	Engine/PreferenceFile.cpp
	Engine/RateLimiter.cpp
	Engine/Rendezvous.cpp
	Engine/RendezvousServer.cpp
	Engine/RendezvousServerList.cpp
	Engine/RunAhead.cpp
	Engine/SessionHandshake.cpp
	Engine/SessionMessage.cpp
	Engine/Settings.cpp
	Engine/SettingsSchema.cpp
	Engine/SimThread.cpp
	Engine/TextFiles.cpp
	Engine/TexturePackage.cpp
	Engine/TextureTier.cpp
	Engine/ThreadRole.cpp
	Engine/UserDataFiles.cpp
	Engine/Vfs.cpp
	Engine/WatchStream.cpp
	Engine/Watchdog.cpp
	Engine/Wire.cpp
	Engine/WndFields.cpp
	Engine/ZipArchive.cpp
	# Monocypher (Code/Core/Monocypher) as C++
	Engine/Monocypher.cpp
)
target_include_directories(EngineCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Code/Core for "Monocypher/monocypher.h".
target_include_directories(EngineCore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_options(EngineCore PRIVATE ${ENGINE_STRICT_OPTIONS} $<$<CXX_COMPILER_ID:MSVC>:/WX>)
find_package(Threads REQUIRED)
# ZipArchive.cpp unpacks deflate entries of mod archives.
find_package(ZLIB REQUIRED)
target_link_libraries(EngineCore PUBLIC Threads::Threads PRIVATE ZLIB::ZLIB)
