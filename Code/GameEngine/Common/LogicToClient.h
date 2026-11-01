/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// LogicToClient.h -- calls from the game logic into the client, run after the logic tick.
//
// The logic (Source/GameLogic) does not change client state directly but queues the change here.
// GameEngine::update() runs the queue right after the logic tick, in call order, as it does with
// TheLogicAudio. This keeps the client on the main thread, which a separate simulation thread requires.
//
// Only calls whose result the logic does not use and that draw no logic random numbers belong here;
// the client reads the effect in its next update either way, so the timing does not change.

#pragma once

#include "Lib/BaseType.h"
#include <functional>
#include <vector>

class AsciiString;
class Anim2DTemplate;
class Drawable;
class Object;
class SpecialPowerTemplate;
class UnicodeString;
enum EvaMessage : int;
enum class CellShroudStatus;
using Color = Int;

class LogicToClient
{
public:
	LogicToClient();

	/// Queue any call; runs in flush().
	void post( std::function<void()> call );

	// --- Eva announcements ----------------------------------------------------------------------
	void evaSetShouldPlay( EvaMessage message );
	void evaSetEnabled( Bool enabled );

	// --- in-game UI display ---------------------------------------------------------------------
	// Texts are fetched and formatted at call time; varargs are passed by value as in InGameUI.
	void uiMessage( UnicodeString format, ... );
	void uiMessage( AsciiString stringManagerLabel, ... );
	void uiFloatingText( const UnicodeString& text, const Coord3D *pos, Color color );
	void uiMilitarySubtitle( const AsciiString& label, Int duration );
	void uiAddNamedTimer( const AsciiString& timerName, const UnicodeString& text, Bool isCountdown );
	void uiRemoveNamedTimer( const AsciiString& timerName );
	void uiShowNamedTimerDisplay( Bool show );
	void uiSetDisplayedMaxWarning( Bool displayed );
	/// options: a WorldAnimationOptions value
	void uiAddWorldAnimation( Anim2DTemplate *animTemplate, const Coord3D *pos, Int options,
														Real durationInSeconds, Real zRisePerSecond );

	// Superweapon timers; templates are INI data and outlive the queue.
	void uiAddSuperweapon( Int playerIndex, const AsciiString& powerName, ObjectID id, const SpecialPowerTemplate *powerTemplate );
	void uiRemoveSuperweapon( Int playerIndex, const AsciiString& powerName, ObjectID id, const SpecialPowerTemplate *powerTemplate );
	void uiSetSuperweaponDisplayEnabledByScript( Bool enable );
	void uiSetObjectSuperweaponHiddenByScript( ObjectID id, Bool hidden );

	// Idle workers: added by ID (the object may die before the queue runs); removed by address,
	// which the UI only compares and never dereferences.
	void uiAddIdleWorker( ObjectID id );
	void uiRemoveIdleWorker( Object *obj, Int playerNumber );

	// --- shroud of the local player (terrain and radar) -----------------------------------------
	// Many cells change per tick, so these use a plain list instead of queued calls. A clear drops
	// the changes queued before it.
	void shroudSetLevel( Int cellX, Int cellY, CellShroudStatus status );
	void shroudClear( void );

	// --- control bar ----------------------------------------------------------------------------
	void controlBarMarkUIDirty( void );

	// --- tactical view (camera) -----------------------------------------------------------------
	/// Queue a call on TheTacticalView; runs in flush() in call order with the other calls.
	void camera( std::function<void()> call );
	/// The logic is about to read camera state (movement finished, time frozen). Queued camera calls
	/// must reach the view first, so a waiting queue runs now, as the calls did before they were queued.
	void syncCameraForLogic( void );
	UnsignedInt getCameraSyncTotal( void ) const { return m_cameraSyncTotal; }

	// --- drawables ------------------------------------------------------------------------------
	// Display calls on a drawable (tint, flash, decal, shadow, fade, indicator colour). The drawable is
	// looked up by ID when the call runs and skipped if it is gone by then; values are taken at call
	// time, captured in the lambda. Calls whose state the logic reads back stay direct.
	void drawable( Drawable *draw, std::function<void(Drawable *)> call );
	/// flashAsSelected; without a colour the object's colour is taken now, as the drawable would.
	void drawableFlashAsSelected( Drawable *draw, const RGBColor *color = nullptr );
	/// setEmoticon, its duration counted from the frame of the call.
	void drawableSetEmoticon( Drawable *draw, const AsciiString &name, Int duration );

	// --- engine side --------------------------------------------------------------------------
	/// Run the queued calls in order (GameEngine::update, after the logic tick).
	void flush( void );
	/// Discard the queued calls (GameEngine::reset).
	void clear( void );
	UnsignedInt getPostedTotal( void ) const { return m_postedTotal; }

private:
	struct ShroudChange
	{
		Short x, y;
		Int status;
	};
	void flushShroud( void );

	std::vector<ShroudChange> m_shroudChanges;
	Bool m_shroudClear;
	std::vector<std::function<void()>> m_queue;
	std::vector<std::function<void()>> m_running;
	UnsignedInt m_postedTotal;
	UnsignedInt m_pendingCamera;		///< camera calls in m_queue
	UnsignedInt m_cameraSyncTotal;	///< flushes forced by syncCameraForLogic
	Bool m_flushing;
};

#include "Engine/EngineContext.h"
