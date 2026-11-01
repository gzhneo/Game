/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// DesyncReport.cpp -- see DesyncReport.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Common/LogicToClient.h"

#include "GameLogic/DesyncReport.h"

#include "Common/GlobalData.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/version.h"
#include "GameClient/GameText.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/GameLobby.h"

#include <cstdio>
#include "Engine/UserDataFiles.h"

namespace
{
	AsciiString s_reportBase;

	AsciiString playerName( Int index )
	{
		Player *player = ThePlayerList ? ThePlayerList->getNthPlayer( index ) : nullptr;
		AsciiString name;
		if ( player )
			name.translate( player->getPlayerDisplayName() );
		return name.isEmpty() ? AsciiString( "?" ) : name;
	}
}

void WriteDesyncReport( const std::map<Int, UnsignedInt> &crcs, Int connectedPlayers )
{
	if ( s_reportBase.isNotEmpty() || TheGameLogic == nullptr || TheGlobalData == nullptr )
		return;	// one report per game: later frames only repeat the first difference

	AsciiString dir = TheGlobalData->getPath_UserData();
	dir.concat( "Desync\\" );
	::Engine::UserDataFiles::createDirectories( ::Engine::pathFromNarrow( dir.str() ) );

	SYSTEMTIME now;
	GetLocalTime( &now );
	const UnsignedInt frame = TheGameLogic->getFrame();
	Int localIndex = -1;
	if ( ThePlayerList && ThePlayerList->getLocalPlayer() )
		localIndex = ThePlayerList->getLocalPlayer()->getPlayerIndex();
	AsciiString base;
	base.format( "%sdesync_%04d%02d%02d_%02d%02d%02d_f%u_p%d", dir.str(), now.wYear, now.wMonth, now.wDay,
		now.wHour, now.wMinute, now.wSecond, frame, localIndex );
	if ( TheGlobalData->m_netLocalInstance > 0 )
	{
		AsciiString instance;
		instance.format( "_local%d", TheGlobalData->m_netLocalInstance );
		base.concat( instance );
	}
	s_reportBase = base;

	// Full object state (as saved), not only the short crc() part: shows which field drifted.
	AsciiString deepName;
	deepName.format( "%s.deep", base.str() );
	const Bool wasFull = TheGlobalData->m_deepCRCFull;
	TheWritableGlobalData->m_deepCRCFull = TRUE;
	const UnsignedInt stateCRC = TheGameLogic->getCRC( CRC_RECALC, deepName );
	TheWritableGlobalData->m_deepCRCFull = wasFull;

	AsciiString textName;
	textName.format( "%s.txt", base.str() );
	FILE *f = ::Engine::openNarrowFile( textName.str(), "w" );
	if ( f == nullptr )
	{
		DEBUG_LOG(( "WriteDesyncReport - cannot write %s\n", textName.str() ));
		return;
	}
	fprintf( f, "GZH Neo desync report\n\n" );
	fprintf( f, "Program:        %s (%s), network protocol %u\n",
		TheVersion ? TheVersion->getAsciiVersion().str() : "?", TheVersion ? TheVersion->getAsciiBuildTime().str() : "?",
		LAN_PROTOCOL_VERSION );
	fprintf( f, "Program CRC:    %08X\nGame data CRC:  %08X\n", TheGlobalData->m_exeCRC, TheGlobalData->m_iniCRC );
	if ( TheGameInfo )
	{
		fprintf( f, "Map:            %s (CRC %08X)\nSeed:           %d\nCRC interval:   %d frames\n",
			TheGameInfo->getMap().str(), TheGameInfo->getMapCRC(), TheGameInfo->getSeed(), TheGameInfo->getCRCInterval() );
	}
	fprintf( f, "Logic frame:    %u (report and dump taken at this frame on every machine)\n", frame );
	fprintf( f, "Local player:   %d (%s)\n\n", localIndex, playerName( localIndex ).str() );

	fprintf( f, "Logic CRCs of the checked frame, %d players connected:\n", connectedPlayers );
	const UnsignedInt first = crcs.empty() ? 0 : crcs.begin()->second;
	for ( std::map<Int, UnsignedInt>::const_iterator it = crcs.begin(); it != crcs.end(); ++it )
	{
		fprintf( f, "  player %d (%s): %08X%s\n", it->first, playerName( it->first ).str(), it->second,
			it->second != first ? "   <-- differs" : "" );
	}
	if ( static_cast<Int>(crcs.size()) < connectedPlayers )
		fprintf( f, "  (only %d of %d players reported a CRC)\n", static_cast<Int>(crcs.size()), connectedPlayers );

	fprintf( f, "\nFiles of this report:\n" );
	fprintf( f, "  %s.deep and .deep.idx   state of every object at frame %u (CRC %08X)\n", base.str(), frame, stateCRC );
	fprintf( f, "  %s.rep                  replay of the game, copied when it ends\n\n", base.str() );
	fprintf( f, "Every player of the game writes such a report. Compare the dumps of two players:\n" );
	fprintf( f, "  python tools/determinism/deepcrc_diff.py <report of A>.deep <report of B>.deep\n" );
	fclose( f );
	DEBUG_LOG(( "WriteDesyncReport - report written to %s\n", textName.str() ));
	if ( TheInGameUI && TheGameText )
	{
		UnicodeString where;
		where.translate( textName );
		TheLogicToClient->uiMessage( TheGameText->fetch( "GZH:DesyncReport" ), where.str() );
	}
}

AsciiString GetDesyncReportBase( void )
{
	return s_reportBase;
}

void ClearDesyncReport( void )
{
	s_reportBase.clear();
}
