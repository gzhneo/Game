// Copyright (C) 2026 GZH Neo
// Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour (Electronic Arts).
// Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
//
// Link stubs for NetPacketTests: engine singletons and module bases that NetPacket/NetCommandMsg
// reference but the packet reader and writer never reach.

#include "PreRTS.h"
#include "Common/AudioEventRTS.h"
#include "Common/DisabledTypes.h"
#include "Common/GameState.h"
#include "Common/INI.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/SubsystemInterface.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/UpdateModule.h"

DisabledMaskType DISABLEDMASK_NONE;

AudioEventRTS::~AudioEventRTS() {}
SubsystemInterface::SubsystemInterface() {}
SubsystemInterface::~SubsystemInterface() {}

AsciiString GameState::portableMapPathToRealMapPath(const AsciiString &in) const { return in; }
AsciiString GameState::realMapPathToPortableMapPath(const AsciiString &in) const { return in; }
const char *INI::getNextToken(const char *) { return nullptr; }
// GameMessage (built when a game command is written) asks ThePlayerList for the local player and
// NetGameCommandMsg::constructGameMessage looks up the sender. Zeroed objects read as player
// index 0, which is all the packet code needs.
alignas(Player) static unsigned char fakePlayer[sizeof(Player)];
alignas(PlayerList) static unsigned char fakePlayerList[sizeof(PlayerList)];

Player *PlayerList::findPlayerWithNameKey(NameKeyType) { return reinterpret_cast<Player *>(fakePlayer); }
void PlayerList::setLocalPlayer(Player *player) { m_local = player; }

static struct FakePlayerListInit
{
	FakePlayerListInit()
	{
		::Engine::context().playerList = reinterpret_cast<PlayerList *>(fakePlayerList);
		ThePlayerList->setLocalPlayer(reinterpret_cast<Player *>(fakePlayer));
	}
} s_fakePlayerListInit;
Player *PlayerList::getNthPlayer(Int) { return nullptr; }

void BehaviorModule::crc(Xfer *) {}
void BehaviorModule::xfer(Xfer *) {}
void BehaviorModule::loadPostProcess() {}
void UpdateModule::crc(Xfer *) {}
void UpdateModule::xfer(Xfer *) {}
void UpdateModule::loadPostProcess() {}
