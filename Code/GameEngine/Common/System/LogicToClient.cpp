/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// LogicToClient.cpp -- see Common/LogicToClient.h.

#include "PreRTS.h"

#include "Common/LogicToClient.h"
#include "Common/Radar.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Display.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/Eva.h"
#include "GameClient/GameText.h"
#include "GameClient/InGameUI.h"
#include "GameClient/View.h"
#include "GameLogic/GameLogic.h"

#include <cstdarg>

static UnicodeString formatMessage( const UnicodeString& format, va_list args )
{
	WideChar buf[ UnicodeString::MAX_FORMAT_BUF_LEN ];
	if( _vsnwprintf( buf, sizeof( buf )/sizeof( WideChar ) - 1, format.str(), args ) < 0 )
		throw ERROR_OUT_OF_MEMORY;
	UnicodeString text;
	text.set( buf );
	return text;
}

LogicToClient::LogicToClient() : m_shroudClear(FALSE), m_postedTotal(0), m_pendingCamera(0), m_cameraSyncTotal(0),
	m_flushing(FALSE)
{
}

void LogicToClient::post( std::function<void()> call )
{
	m_queue.push_back( std::move(call) );
	++m_postedTotal;
}

void LogicToClient::evaSetShouldPlay( EvaMessage message )
{
	post( [message]() { if (TheEva) TheEva->setShouldPlay(message); } );
}

void LogicToClient::evaSetEnabled( Bool enabled )
{
	post( [enabled]() { if (TheEva) TheEva->setEvaEnabled(enabled); } );
}

void LogicToClient::uiMessage( UnicodeString format, ... )
{
	va_list args;
	// the format is passed by value on purpose, as in InGameUI::message
#pragma warning(suppress : 4840)
	va_start( args, format );
	UnicodeString text = formatMessage( format, args );
	va_end( args );
	UnsignedInt frame = TheGameLogic ? TheGameLogic->getFrame() : 0;
	post( [text, frame]() { if (TheInGameUI) TheInGameUI->messageText(text, frame); } );
}

void LogicToClient::uiMessage( AsciiString stringManagerLabel, ... )
{
	UnicodeString format = TheGameText->fetch( stringManagerLabel.str() );
	va_list args;
#pragma warning(suppress : 4840)
	va_start( args, stringManagerLabel );
	UnicodeString text = formatMessage( format, args );
	va_end( args );
	UnsignedInt frame = TheGameLogic ? TheGameLogic->getFrame() : 0;
	post( [text, frame]() { if (TheInGameUI) TheInGameUI->messageText(text, frame); } );
}

void LogicToClient::uiFloatingText( const UnicodeString& text, const Coord3D *pos, Color color )
{
	Coord3D where = *pos;
	UnsignedInt frame = TheGameLogic ? TheGameLogic->getFrame() : 0;
	post( [text, where, color, frame]() { if (TheInGameUI) TheInGameUI->addFloatingTextIssued(text, &where, color, frame); } );
}

void LogicToClient::uiMilitarySubtitle( const AsciiString& label, Int duration )
{
	UnsignedInt frame = TheGameLogic ? TheGameLogic->getFrame() : 0;
	post( [label, duration, frame]() { if (TheInGameUI) TheInGameUI->militarySubtitleIssued(label, duration, frame); } );
}

void LogicToClient::uiAddNamedTimer( const AsciiString& timerName, const UnicodeString& text, Bool isCountdown )
{
	post( [timerName, text, isCountdown]() { if (TheInGameUI) TheInGameUI->addNamedTimer(timerName, text, isCountdown); } );
}

void LogicToClient::uiRemoveNamedTimer( const AsciiString& timerName )
{
	post( [timerName]() { if (TheInGameUI) TheInGameUI->removeNamedTimer(timerName); } );
}

void LogicToClient::uiShowNamedTimerDisplay( Bool show )
{
	post( [show]() { if (TheInGameUI) TheInGameUI->showNamedTimerDisplay(show); } );
}

void LogicToClient::uiSetDisplayedMaxWarning( Bool displayed )
{
	post( [displayed]() { if (TheInGameUI) TheInGameUI->setDisplayedMaxWarning(displayed); } );
}

void LogicToClient::uiAddWorldAnimation( Anim2DTemplate *animTemplate, const Coord3D *pos, Int options,
																				 Real durationInSeconds, Real zRisePerSecond )
{
	if (pos == nullptr)
		return;
	Coord3D where = *pos;
	UnsignedInt frame = TheGameLogic ? TheGameLogic->getFrame() : 0;
	post( [animTemplate, where, options, durationInSeconds, zRisePerSecond, frame]()
	{
		if (TheInGameUI)
			TheInGameUI->addWorldAnimationIssued(animTemplate, &where, static_cast<WorldAnimationOptions>(options),
				durationInSeconds, zRisePerSecond, frame);
	} );
}

void LogicToClient::uiAddSuperweapon( Int playerIndex, const AsciiString& powerName, ObjectID id, const SpecialPowerTemplate *powerTemplate )
{
	post( [playerIndex, powerName, id, powerTemplate]() { if (TheInGameUI) TheInGameUI->addSuperweapon(playerIndex, powerName, id, powerTemplate); } );
}

void LogicToClient::uiRemoveSuperweapon( Int playerIndex, const AsciiString& powerName, ObjectID id, const SpecialPowerTemplate *powerTemplate )
{
	post( [playerIndex, powerName, id, powerTemplate]() { if (TheInGameUI) TheInGameUI->removeSuperweapon(playerIndex, powerName, id, powerTemplate); } );
}

void LogicToClient::uiSetSuperweaponDisplayEnabledByScript( Bool enable )
{
	post( [enable]() { if (TheInGameUI) TheInGameUI->setSuperweaponDisplayEnabledByScript(enable); } );
}

void LogicToClient::uiSetObjectSuperweaponHiddenByScript( ObjectID id, Bool hidden )
{
	post( [id, hidden]() { if (TheInGameUI) TheInGameUI->setObjectSuperweaponHiddenByScript(id, hidden); } );
}

void LogicToClient::uiAddIdleWorker( ObjectID id )
{
	post( [id]()
	{
		Object *obj = TheGameLogic ? TheGameLogic->findObjectByID(id) : nullptr;
		if (TheInGameUI && obj)
			TheInGameUI->addIdleWorker(obj);
	} );
}

void LogicToClient::uiRemoveIdleWorker( Object *obj, Int playerNumber )
{
	post( [obj, playerNumber]() { if (TheInGameUI) TheInGameUI->removeIdleWorker(obj, playerNumber); } );
}

void LogicToClient::shroudSetLevel( Int cellX, Int cellY, CellShroudStatus status )
{
	m_shroudChanges.push_back( ShroudChange{ static_cast<Short>(cellX), static_cast<Short>(cellY), static_cast<Int>(status) } );
}

void LogicToClient::shroudClear( void )
{
	m_shroudChanges.clear();
	m_shroudClear = TRUE;
}

void LogicToClient::flushShroud( void )
{
	if (!m_shroudClear && m_shroudChanges.empty())
		return;
	if (TheRadar)
		TheRadar->beginShroudChanges();
	if (m_shroudClear)
	{
		if (TheDisplay)
			TheDisplay->clearShroud();
		if (TheRadar)
			TheRadar->clearShroud();
		m_shroudClear = FALSE;
	}
	for (const ShroudChange &change : m_shroudChanges)
	{
		CellShroudStatus status = static_cast<CellShroudStatus>(change.status);
		if (TheDisplay)
			TheDisplay->setShroudLevel(change.x, change.y, status);
		if (TheRadar)
			TheRadar->setShroudLevel(change.x, change.y, status);
	}
	m_shroudChanges.clear();
	if (TheRadar)
		TheRadar->endShroudChanges();
}

void LogicToClient::controlBarMarkUIDirty( void )
{
	post( []() { if (TheControlBar) TheControlBar->markUIDirty(); } );
}

void LogicToClient::camera( std::function<void()> call )
{
	post( [call = std::move(call)]() { if (TheTacticalView) call(); } );
	++m_pendingCamera;
}

void LogicToClient::drawable( Drawable *draw, std::function<void(Drawable *)> call )
{
	if (draw == nullptr)
		return;
	const DrawableID id = draw->getID();
	post( [id, call = std::move(call)]()
	{
		if (TheGameClient)
			if (Drawable *d = TheGameClient->findDrawableByID( id ))
				call( d );
	} );
}

void LogicToClient::drawableFlashAsSelected( Drawable *draw, const RGBColor *color )
{
	if (draw == nullptr)
		return;
	RGBColor flash;
	if (color)
		flash = *color;
	else if (!draw->getSelectionFlashColor( flash ))
		return;
	drawable( draw, [flash]( Drawable *d ) { d->flashAsSelected( &flash ); } );
}

void LogicToClient::drawableSetEmoticon( Drawable *draw, const AsciiString &name, Int duration )
{
	const UnsignedInt frame = TheGameLogic->getFrame();
	drawable( draw, [name, duration, frame]( Drawable *d ) { d->setEmoticonIssued( name, duration, frame ); } );
}

void LogicToClient::syncCameraForLogic( void )
{
	if (m_pendingCamera == 0)
		return;
	++m_cameraSyncTotal;
	flush();
}

void LogicToClient::flush( void )
{
	// Calls queued while running (none today) wait for the next flush; a camera read from inside a
	// running call finds the queue already running.
	if (m_flushing)
		return;
	struct Running
	{
		Bool &flag;
		explicit Running( Bool &f ) : flag(f) { flag = TRUE; }
		~Running() { flag = FALSE; }
	} running( m_flushing );
	m_pendingCamera = 0;
	flushShroud();
	m_running.swap( m_queue );
	for (std::function<void()> &call : m_running)
		call();
	m_running.clear();
}

void LogicToClient::clear( void )
{
	// Called at the end of a game: the capacity of the busiest frame goes too, so the queues do not keep
	// memory between games.
	m_pendingCamera = 0;
	std::vector<std::function<void()>>().swap( m_queue );
	std::vector<std::function<void()>>().swap( m_running );
	std::vector<ShroudChange>().swap( m_shroudChanges );
	m_shroudClear = FALSE;
}
