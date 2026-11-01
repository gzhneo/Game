/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FILE: TestScript.cpp ///////////////////////////////////////////////////////////////////////////
// Test script: drives the game without input through menus, games, campaign missions, save/load and
// up to exit (-testablauf <file>).
//
// File format: one command per line, words separated by spaces, '#' starts a comment. Window names
// as in the .wnd files, e.g. MainMenu.wnd:ButtonSinglePlayer. Waiting commands take a timeout in main
// loop passes as last word (default 9000); when it expires, the game ends with exit code 4
// (GAME_EXIT_TEST_SCRIPT) and the line "TESTABLAUF ERROR".
//
//   warte <n>                 wait n passes
//   logik <n> [timeout]       wait for n logic frames of the running game (network games run in real
//                             time, runner passes are much faster; default timeout 2000000)
//   fenster <name> [timeout]  wait until the window is visible and enabled (parents visible too, menu
//                             transitions finished) for five passes in a row
//   klick <name> [timeout]    like fenster, then a left click (mouse down/up on the window)
//   esc <name> [timeout]      like fenster, then the Escape key to the window (e.g. credits)
//   klickFalls <name> [t]     like klick, but no error if the window does not become usable within
//                             the timeout (default 120), for menus that return differently by path
//   auswahl <name> <index>    select an entry of a combo box or list box, like fenster; a negative index
//                             counts from the end of a combo box (-1: last entry)
//   auswahlText <name> <text> [t] [spalte]
//                             select the first entry of a list box whose text contains <text> ("_" stands for a
//                             space, e.g. "(4)" or "Tournament_Desert"); without <spalte> every column is searched
//   liste <name>              write the entries of a combo box or list box to the log, all columns separated
//                             by " | " (for writing scripts)
//   partie [timeout]          wait until a game (not the shell map) is loaded and running
//   keinePartie [timeout]     wait until no game is running (the shell map does not count)
//   hauptmenue [timeout]      wait until the main menu is on top and no transition runs (after a game
//                             it opens with a submenu depending on the path)
//   spielmenue                Escape in a game: open or close the quit menu
//   sieg                      Ctrl+W: win the mission/game immediately (debug only)
//   siegBisEnde [timeout]     Ctrl+W every 300 passes until the game ends (cutscenes at mission start
//                             swallow the key; debug only)
//   gegnerVernichten          Ctrl+Shift+Num/: destroy all enemies (debug only)
//   niederlage                destroy all own objects, like a loss (debug only)
//   speichern <file>          create a save game in the save folder
//   laden <file>              load a save game (like the load menu)
//   merke                     memory checkpoint: count live blocks per group (debug only, GameMemory)
//   wachstum <name>           write groups (pool, tag, call stack) that grew since the checkpoint to
//                             the log (lines "WACHSTUM,..."; debug only)
//   schliessen                close the window (WM_CLOSE, like Alt+F4 or the title bar X)
//   beenden                   exit like -quitAfter (clean up a running game first)
//   log <text>                write a line to the log
//   meta <name>               trigger a meta command like a hotkey; name as in CommandMap.ini
//                             (e.g. DEMO_LOD_DECREASE, DEMO_TOGGLE_METRICS)
//   testhilfe <name> [target] trigger a test helper like the -testhilfenAblauf script (names there,
//                             target = player index, none = local player); needs -testhilfen
//   markierung <x> <y>        place a beacon of the local player at the world position, like the beacon
//                             button; markierung basis [dx dy]: at the first own building plus (dx, dy)
//   spezial <power> [dx dy]   fire a special power of the local player (name as in SpecialPower.ini)
//                             from the first own object that has it, at its position plus (dx, dy)
//                             (default 200 200), like a click with the power's target cursor
//   spezialUmlenken <power> [dx dy]  move the target of a running power with overridable destination
//                             (Spectre gunship, particle cannon) by (dx, dy), like a click with it
//   bildlauf <art> <dir> <ticks> [pixels]  scroll the tactical view for <ticks> logic frames with injected
//                             input, then log camera travel in world units and screen fractions (line
//                             "TESTABLAUF BILDLAUF ..."); art = taste (Shift+arrow key: the German
//                             command map binds the plain arrows to unit selection), rand (cursor on the
//                             screen edge, also in a window), ziehen (right button dragged by <pixels>,
//                             default 200); dir = rechts, links, hoch, runter
//   bildrate <n>              cap render-only frames at n per second (0 = none) for this run, to measure
//                             behaviour at a low frame rate
//   bildlaufregler <n>        set the scroll speed like the options slider (ScrollFactor n/100) for this run
//                             only; nothing is saved
//   rad <n>                   mouse wheel over the screen centre, one step per pass: n > 0 zooms in, n < 0
//                             out; then logs zoom and height above ground
//   ziehenEcht <dir> [n] [px] drag with the right button through the real input path: window messages
//                             (WM_RBUTTONDOWN, a WM_MOUSEMOVE of px pixels from where the cursor stands every
//                             pass for n logic frames, WM_RBUTTONUP; default 150 and 40), the real cursor put on the window centre for
//                             the command and back afterwards, the camera on the map centre first; logs "TESTABLAUF ZIEHENECHT ..." with the camera
//                             travel, the visible ground across, whether the cursor was hidden while dragging
//                             and stands on the starting point again
//   radEcht <delta> [n]       n WM_MOUSEWHEEL messages of <delta> (120 = one notch, less = a fine wheel);
//                             logs "TESTABLAUF RADECHT ..." with the zoom before and after
//   tasteEcht <dir> [n]       Shift+arrow key as WM_KEYDOWN/WM_KEYUP held for n passes (default 60); logs
//                             "TESTABLAUF TASTEECHT ..." with the camera travel
//                             (the three Echt commands let real input through for their duration; with
//                             -echteEingabeAus it stays out)
//   klickEigenes              left click on the first own selectable unit on screen (structures only if
//                             there is no unit), like a real click; logs what is selected afterwards
//   rahmen [margin] [edge]    drag a selection box with the left button over all own selectable units
//                             on screen (their projected centres plus margin pixels, default 8), like a
//                             real drag; logs how many of them are selected afterwards. With edge (oben,
//                             unten, links, rechts) the box reaches 200 pixels further on the other sides,
//                             so the units stand near that edge of a large box but not on its line
//   streifen [mitte]          widescreen only: move the camera so the first own unit stands in the 3D view
//                             beside the control bar (mitte: behind the bar), then select all units on
//                             screen like the select-all key; the unit must be selected beside the bar and
//                             not behind it
//   zeichenbereich            check that the ground under points along the edges of the whole 3D viewport
//                             (with a widescreen layout down to the bottom of the screen) lies in the region
//                             whose drawables are drawn each frame; logs the largest distance outside
//   schliessenNach <ms>       WM_CLOSE after ms milliseconds from a timer; also arrives while the main
//                             loop is blocked and only the load screen pumps messages
//
// Log: DebugLogFileD.txt (debug) and, with -crcLog, the runner log. Every line starts with
// "TESTABLAUF". The commands act through the message stream or window input like the hotkeys; a
// replay of the game is no longer equivalent after sieg/niederlage/speichern.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine
#include "formatinto.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include "Engine/Clock.h"
#include "Common/GameEngine.h"
#include "Common/GameMemory.h"
#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Recorder.h"
#include "Common/SpecialPower.h"
#include "Common/TestAids.h"
#include "Common/ThingTemplate.h"
#include "GameClient/Display.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/InGameUI.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/Mouse.h"
#include "GameClient/MetaEvent.h"
#include "GameClient/Shell.h"
#include "GameClient/View.h"
#include "GameClient/TestScript.h"
#include "GameClient/WindowLayout.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/TerrainLogic.h"

extern HWND ApplicationHWnd;

namespace
{

struct ScriptCommand
{
	Int line;
	std::vector<std::string> words;
};

struct ScriptState
{
	std::vector<ScriptCommand> commands;
	size_t nextIndex = 0;
	UnsignedInt pass = 0;			///< passes since the current command started
	UnsignedInt stable = 0;					///< consecutive passes with the condition met
	UnsignedInt startFrame = 0;			///< logic frame when the current command started (logik)
	Bool done = FALSE;
};

ScriptState *s_state = nullptr;
Bool s_loaded = FALSE;
HANDLE s_closeTimer = nullptr;	///< schliessenNach

VOID CALLBACK closeTimerCallback( PVOID, BOOLEAN )
{
	::PostMessage( ApplicationHWnd, WM_CLOSE, 0, 0 );
}

const UnsignedInt DEFAULT_TIMEOUT = 9000;
const UnsignedInt STABLE_PASSES = 5;

void logLine( const char *fmt, ... )
{
	char line[1024];
	va_list args;
	va_start( args, fmt );
	vsnprintf( line, sizeof( line ), fmt, args );
	va_end( args );
	char full[1100];
	WWFormat::Into(full, "TESTABLAUF {}", line);
	GameEngine_RunnerLog( full );
	DEBUG_LOG(( "%s\n", full ));
}

void loadScript( void )
{
	s_loaded = TRUE;
	if( TheGlobalData == nullptr || TheGlobalData->m_testScriptFile.isEmpty() )
		return;
	FILE *f = fopen( TheGlobalData->m_testScriptFile.str(), "r" );
	if( f == nullptr )
	{
		logLine( "ERROR file %s not readable", TheGlobalData->m_testScriptFile.str() );
		TheGameExitCode = GAME_EXIT_TEST_SCRIPT;
		TheGameEngine->setQuitting( TRUE );
		return;
	}
	s_state = new ScriptState;
	char buffer[1024];
	Int nr = 0;
	while( fgets( buffer, sizeof( buffer ), f ) )
	{
		++nr;
		char *comment = strchr( buffer, '#' );
		if( comment )
			*comment = 0;
		ScriptCommand b;
		b.line = nr;
		for( char *word = strtok( buffer, " \t\r\n" ); word; word = strtok( nullptr, " \t\r\n" ) )
			b.words.push_back( word );
		if( !b.words.empty() )
			s_state->commands.push_back( b );
	}
	fclose( f );
	logLine( "START %s, %d commands", TheGlobalData->m_testScriptFile.str(), static_cast<Int>( s_state->commands.size() ) );
}

GameWindow *findWindow( const std::string &name )
{
	if( TheWindowManager == nullptr || TheNameKeyGenerator == nullptr )
		return nullptr;
	return TheWindowManager->winGetWindowFromId( nullptr, TheNameKeyGenerator->nameToKey( name.c_str() ) );
}

/// nullptr if visible, enabled and no transition is running; otherwise the reason. Some transitions
/// loop (e.g. the campaign difficulty selection), so they no longer count after TRANSITION_PATIENCE
/// passes; a player would click then as well.
const UnsignedInt TRANSITION_PATIENCE = 300;
const char *windowNotReady( GameWindow *win, UnsignedInt pass )
{
	if( win == nullptr )
		return "window not found";
	if( TheWindowManager->isHidden( win ) )
		return "window (or a parent) hidden";
	if( BitTest( win->winGetStatus(), WIN_STATUS_ENABLED ) == FALSE )
		return "window disabled";
	if( pass < TRANSITION_PATIENCE && TheTransitionHandler && !TheTransitionHandler->isFinished() )
		return "menu transition running";
	if( pass < TRANSITION_PATIENCE && TheShell && !TheShell->isAnimFinished() )
		return "shell animation running";
	return nullptr;
}

Bool inMatch( void )
{
	return TheGameLogic && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame()
		&& !TheGameLogic->isLoadingMap() && TheGameLogic->getFrame() > 2;
}

UnsignedInt timeout( const ScriptCommand &b, size_t index )
{
	if( b.words.size() > index )
	{
		Int n = atoi( b.words[index].c_str() );
		if( n > 0 )
			return static_cast<UnsignedInt>( n );
	}
	return DEFAULT_TIMEOUT;
}

void failCommand( const ScriptCommand &b, const char *reason )
{
	WindowLayout *topWindow = TheShell ? TheShell->top() : nullptr;
	logLine( "ERROR line %d (%s): %s (logic frame %u, game %d, shell map %d, shell active %d, top %s)",
		b.line, b.words[0].c_str(), reason,
		TheGameLogic ? TheGameLogic->getFrame() : 0, TheGameLogic ? TheGameLogic->isInGame() : 0,
		TheGameLogic ? TheGameLogic->isInShellGame() : 0, TheShell ? TheShell->isShellActive() : 0,
		topWindow ? topWindow->getFilename().str() : "-" );
	s_state->done = TRUE;
	TheGameExitCode = GAME_EXIT_TEST_SCRIPT;
	if( TheGameLogic && TheGameLogic->isInGame() )
	{
		if( TheRecorder && TheRecorder->getMode() == RecorderModeType::RECORDERMODETYPE_RECORD )
			TheRecorder->stopRecording();
		TheGameLogic->clearGameData( FALSE );
	}
	TheGameEngine->setQuitting( TRUE );
}

#if defined(_DEBUG)
void collectOwn( Object *obj, void *userData )
{
	static_cast<std::vector<ObjectID> *>( userData )->push_back( obj->getID() );
}
#endif

struct SpecialPowerSearch
{
	const SpecialPowerTemplate *power;
	Object *source;
};

void findSpecialPowerSource( Object *obj, void *userData )
{
	SpecialPowerSearch *search = static_cast<SpecialPowerSearch *>( userData );
	if( search->source == nullptr && obj->getSpecialPowerModule( search->power ) != nullptr )
		search->source = obj;
}

void findRedirectable( Object *obj, void *userData )
{
	SpecialPowerSearch *search = static_cast<SpecialPowerSearch *>( userData );
	if( search->source == nullptr && obj->findSpecialPowerWithOverridableDestinationActive( search->power->getSpecialPowerType() ) != nullptr )
		search->source = obj;
}

struct ClickSearch
{
	Object *found;		///< only valid in the pass of the search
	DrawableID drawable;
	char name[64];
	ICoord2D screen;
	Bool structures;	///< also accept structures
	Int maxY;				///< above the control bar
};

void findClickable( Object *obj, void *userData )
{
	ClickSearch *search = static_cast<ClickSearch *>( userData );
	if( search->found != nullptr || !obj->isSelectable() || obj->getDrawable() == nullptr )
		return;
	if( !search->structures && obj->isKindOf( KINDOF_STRUCTURE ) )
		return;
	ICoord2D screen;
	if( !TheTacticalView->worldToScreen( obj->getPosition(), &screen ) || screen.y >= search->maxY || screen.y < 0 )
		return;
	search->found = obj;
	search->drawable = obj->getDrawable()->getID();
	strncpy( search->name, obj->getTemplate()->getName().str(), sizeof( search->name ) - 1 );
	search->name[sizeof( search->name ) - 1] = 0;
	search->screen = screen;
}

/// Own units for the rahmen command and the box over them.
struct BoxSearch
{
	static const Int MAX_UNITS = 64;
	DrawableID drawables[MAX_UNITS];
	Int count;
	IRegion2D view;		///< tactical view on the display
	IRegion2D box;
};

void findBoxUnit( Object *obj, void *userData )
{
	BoxSearch *search = static_cast<BoxSearch *>( userData );
	if( search->count >= BoxSearch::MAX_UNITS || !obj->isSelectable() || obj->getDrawable() == nullptr ||
		obj->isKindOf( KINDOF_STRUCTURE ) )
		return;
	ICoord2D screen;
	if( !TheTacticalView->worldToScreen( obj->getPosition(), &screen ) || screen.x < search->view.lo.x ||
		screen.x >= search->view.hi.x || screen.y < search->view.lo.y || screen.y >= search->view.hi.y )
		return;
	if( search->count == 0 )
	{
		search->box.lo = screen;
		search->box.hi = screen;
	}
	search->box.lo.x = std::min( search->box.lo.x, screen.x );
	search->box.lo.y = std::min( search->box.lo.y, screen.y );
	search->box.hi.x = std::max( search->box.hi.x, screen.x );
	search->box.hi.y = std::max( search->box.hi.y, screen.y );
	search->drawables[search->count++] = obj->getDrawable()->getID();
}

/// State of a running bildlauf command.
struct ScrollRun
{
	std::uint64_t startMicros = 0;
	Coord3D startPos = {};
	Coord3D startGround = {};		///< terrain point under the screen centre at the start
	Real across = 0;						///< visible ground across the centre row at the start
	Real down = 0;							///< visible ground down the centre column at the start
	Real zoom = 0;
	Bool savedEdgeInWindow = FALSE;
};
ScrollRun s_scrollRun;

void appendMousePosition( const ICoord2D &pos )
{
	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_RAW_MOUSE_POSITION );
	msg->appendPixelArgument( pos );
	msg->appendIntegerArgument( 0 );
}

void appendRightButton( GameMessage::Type type, const ICoord2D &pos )
{
	GameMessage *msg = TheMessageStream->appendMessage( type );
	msg->appendPixelArgument( pos );
	msg->appendIntegerArgument( 0 );
	msg->appendIntegerArgument( static_cast<Int>( Engine::Clock::milliseconds() ) );
}

void appendMouseWheel( const ICoord2D &pos, Int spin )
{
	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_RAW_MOUSE_WHEEL );
	msg->appendPixelArgument( pos );
	msg->appendIntegerArgument( spin );
	msg->appendIntegerArgument( 0 );
}

void appendKey( GameMessage::Type type, Int key, Int state )
{
	GameMessage *msg = TheMessageStream->appendMessage( type );
	msg->appendIntegerArgument( key );
	msg->appendIntegerArgument( state );
}

/// Ground distance between two screen points (terrain under each).
Real groundDistance( Int x0, Int y0, Int x1, Int y1 )
{
	ICoord2D a = { x0, y0 }, b = { x1, y1 };
	Coord3D wa, wb;
	TheTacticalView->screenToTerrain( &a, &wa );
	TheTacticalView->screenToTerrain( &b, &wb );
	const Real dx = wb.x - wa.x, dy = wb.y - wa.y;
	return static_cast<Real>( sqrt( dx * dx + dy * dy ) );
}

/// bildlauf: TRUE when done.
Bool runScrollCommand( const ScriptCommand &b, UnsignedInt d )
{
	const std::string art = b.words.size() > 1 ? b.words[1] : std::string();
	const std::string dir = b.words.size() > 2 ? b.words[2] : std::string();
	const UnsignedInt ticks = b.words.size() > 3 ? static_cast<UnsignedInt>( atoi( b.words[3].c_str() ) ) : 30u;
	const Int drag = b.words.size() > 4 ? atoi( b.words[4].c_str() ) : 200;
	if( ( art != "taste" && art != "rand" && art != "ziehen" ) || ticks == 0 )
	{
		failCommand( b, "bildlauf <taste|rand|ziehen> <rechts|links|hoch|runter> <ticks> [pixels]" );
		return FALSE;
	}
	Int sx = 0, sy = 0, key = KEY_NONE;
	if( dir == "rechts" ) { sx = 1; key = KEY_RIGHT; }
	else if( dir == "links" ) { sx = -1; key = KEY_LEFT; }
	else if( dir == "hoch" ) { sy = -1; key = KEY_UP; }
	else if( dir == "runter" ) { sy = 1; key = KEY_DOWN; }
	else
	{
		failCommand( b, "bildlauf: unknown direction" );
		return FALSE;
	}
	if( !inMatch() || TheTacticalView == nullptr || TheDisplay == nullptr )
	{
		failCommand( b, "bildlauf needs a running game" );
		return FALSE;
	}

	const Int width = static_cast<Int>( TheDisplay->getWidth() );
	const Int height = static_cast<Int>( TheDisplay->getHeight() );
	const ICoord2D centre = { width / 2, height / 2 };
	ICoord2D edge = centre;
	if( sx > 0 ) edge.x = width - 1;
	if( sx < 0 ) edge.x = 0;
	if( sy > 0 ) edge.y = height - 1;
	if( sy < 0 ) edge.y = 0;
	const ICoord2D dragged = { centre.x + sx * drag, centre.y + sy * drag };

	if( d == 0 )
	{
		s_scrollRun.startMicros = Engine::Clock::microseconds();
		TheTacticalView->getPosition( &s_scrollRun.startPos );
		TheTacticalView->screenToTerrain( &centre, &s_scrollRun.startGround );
		Int ox, oy;
		TheTacticalView->getOrigin( &ox, &oy );
		s_scrollRun.across = groundDistance( 0, centre.y, width - 1, centre.y );
		s_scrollRun.down = groundDistance( centre.x, oy, centre.x, height - 1 );
		s_scrollRun.zoom = TheTacticalView->getZoom();
		GameWindow *focus = TheWindowManager ? TheWindowManager->winGetFocus() : nullptr;
		if( focus != nullptr && art == "taste" )
		{
			// a focused window would take the arrow keys, as with a real keyboard
			logLine( "BILDLAUF focus %s cleared", focus->winGetInstanceData()->m_decoratedNameString.str() );
			TheWindowManager->winSetFocus( nullptr );
		}
		s_scrollRun.savedEdgeInWindow = TheGlobalData->m_edgeScrollInWindow;
		if( art == "rand" )
			TheWritableGlobalData->m_edgeScrollInWindow = TRUE;
		if( art == "taste" )
			appendKey( GameMessage::MSG_RAW_KEY_DOWN, key, KEY_STATE_DOWN | KEY_STATE_SHIFT );
		else if( art == "ziehen" )
		{
			appendMousePosition( centre );
			appendRightButton( GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, centre );
		}
	}
	if( d < ticks )
	{
		// The real cursor position follows every tick after the scroll step; repeat ours before the next one.
		if( art == "rand" )
			appendMousePosition( edge );
		else if( art == "ziehen" )
			appendMousePosition( dragged );
		return FALSE;
	}

	const std::uint64_t micros = Engine::Clock::microseconds() - s_scrollRun.startMicros;
	Coord3D pos;
	TheTacticalView->getPosition( &pos );
	if( art == "taste" )
		appendKey( GameMessage::MSG_RAW_KEY_UP, key, KEY_STATE_UP | KEY_STATE_SHIFT );
	else if( art == "ziehen" )
		appendRightButton( GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP, dragged );
	appendMousePosition( centre );
	TheWritableGlobalData->m_edgeScrollInWindow = s_scrollRun.savedEdgeInWindow;

	const Real seconds = static_cast<Real>( micros ) / 1000000.0f;
	const Real wx = pos.x - s_scrollRun.startPos.x, wy = pos.y - s_scrollRun.startPos.y;
	const Real world = static_cast<Real>( sqrt( wx * wx + wy * wy ) );
	const Real across = s_scrollRun.across, down = s_scrollRun.down;
	// screen travel of the ground point that was under the centre; -1 when it left the view
	ICoord2D moved = centre;
	const Bool inside = TheTacticalView->worldToScreen( &s_scrollRun.startGround, &moved );
	const Int px = moved.x - centre.x, py = moved.y - centre.y;
	const Real screenFraction = !inside ? -1.0f : sx != 0 ? static_cast<Real>( px < 0 ? -px : px ) / static_cast<Real>( width )
		: static_cast<Real>( py < 0 ? -py : py ) / static_cast<Real>( height );
	logLine( "BILDLAUF art=%s dir=%s res=%dx%d ticks=%u ms=%.1f world=%.2f world_s=%.1f across=%.1f down=%.1f "
		"view_s=%.4f screen_px=%d,%d screen_s=%.4f zoom=%.3f zoom_end=%.3f height=%.1f pitch=%.3f drag=%d factor=%.2f",
		art.c_str(), dir.c_str(), width, height, ticks, static_cast<Real>( micros ) / 1000.0f, world, world / seconds,
		across, down, world / seconds / ( sx != 0 ? across : down ), px, py, inside ? screenFraction / seconds : -1.0f,
		s_scrollRun.zoom, TheTacticalView->getZoom(), TheTacticalView->getHeightAboveGround(), TheTacticalView->getPitch(),
		art == "ziehen" ? drag : 0, TheGlobalData->m_keyboardScrollFactor );
	return TRUE;
}

/// State of a running ziehenEcht, radEcht or tasteEcht command.
struct RealInputRun
{
	POINT savedCursor = {};
	POINT centre = {};			///< client coordinates
	Coord3D startPos = {};
	Real across = 0;
	Real zoom = 0;
	Bool focus = FALSE;
	UnsignedInt startFrame = 0;
	UnsignedInt endPass = 0;		///< pass of the release
	Bool dragged = FALSE;
	Bool hidden = TRUE;			///< hidden in every pass of the drag
};
RealInputRun s_realRun;

Int directionOf( const std::string &dir, Int &sx, Int &sy )
{
	sx = sy = 0;
	if( dir == "rechts" ) { sx = 1; return VK_RIGHT; }
	if( dir == "links" ) { sx = -1; return VK_LEFT; }
	if( dir == "hoch" ) { sy = -1; return VK_UP; }
	if( dir == "runter" ) { sy = 1; return VK_DOWN; }
	return 0;
}

LPARAM clientParam( Int x, Int y )
{
	return MAKELPARAM( static_cast<Short>( x ), static_cast<Short>( y ) );
}

/// Starts a real input command: real input passes, camera state noted. FALSE if no game runs.
Bool beginRealInput( const ScriptCommand &b, const char *usage )
{
	if( !inMatch() || TheTacticalView == nullptr || TheDisplay == nullptr || ApplicationHWnd == nullptr || TheMouse == nullptr )
	{
		failCommand( b, usage );
		return FALSE;
	}
	RECT client;
	GetClientRect( ApplicationHWnd, &client );
	s_realRun = RealInputRun();
	s_realRun.centre.x = ( client.right - client.left ) / 2;
	s_realRun.centre.y = ( client.bottom - client.top ) / 2;
	s_realRun.focus = GetForegroundWindow() == ApplicationHWnd;
	TheTacticalView->getPosition( &s_realRun.startPos );
	s_realRun.across = groundDistance( 0, s_realRun.centre.y, static_cast<Int>( TheDisplay->getWidth() ) - 1, s_realRun.centre.y );
	s_realRun.zoom = TheTacticalView->getZoom();
	s_realRun.startFrame = TheGameLogic ? TheGameLogic->getFrame() : 0;
	TheWritableGlobalData->m_testRealInput = TRUE;
	return TRUE;
}

Real realInputTravel( void )
{
	Coord3D pos;
	TheTacticalView->getPosition( &pos );
	const Real dx = pos.x - s_realRun.startPos.x, dy = pos.y - s_realRun.startPos.y;
	return static_cast<Real>( sqrt( dx * dx + dy * dy ) );
}

/// Runs the command. TRUE = done, continue with the next; FALSE = keep waiting.
Bool executeCommand( const ScriptCommand &b )
{
	const std::string &cmd = b.words[0];
	const std::string arg1 = b.words.size() > 1 ? b.words[1] : std::string();
	const UnsignedInt d = s_state->pass;

	if( cmd == "warte" )
		return d >= static_cast<UnsignedInt>( atoi( arg1.c_str() ) );

	if( cmd == "logik" )
	{
		const UnsignedInt now = TheGameLogic ? TheGameLogic->getFrame() : 0;
		if( d == 0 )
			s_state->startFrame = now;
		if( now >= s_state->startFrame + static_cast<UnsignedInt>( atoi( arg1.c_str() ) ) )
		{
			logLine( "OK line %d logik %s after %u passes, logic frame %u", b.line, arg1.c_str(), d, now );
			return TRUE;
		}
		if( d >= ( b.words.size() > 2 ? timeout( b, 2 ) : 2000000u ) )
			failCommand( b, "logic frames not reached" );
		return FALSE;
	}

	if( cmd == "fenster" || cmd == "klick" || cmd == "klickFalls" || cmd == "esc" || cmd == "auswahl" || cmd == "auswahlText" || cmd == "liste" )
	{
		GameWindow *win = findWindow( arg1 );
		const char *reason = windowNotReady( win, d );
		if( reason == nullptr )
			++s_state->stable;
		else
			s_state->stable = 0;
		if( s_state->stable < STABLE_PASSES )
		{
			if( cmd == "klickFalls" )
			{
				const UnsignedInt f = b.words.size() > 2 ? timeout( b, 2 ) : 120;
				if( d >= f )
				{
					logLine( "OK line %d klickFalls %s skipped (%s)", b.line, arg1.c_str(), reason ? reason : "not stable" );
					return TRUE;
				}
				return FALSE;
			}
			if( d >= timeout( b, ( cmd == "auswahl" || cmd == "auswahlText" ) ? 3 : 2 ) )
				failCommand( b, reason ? reason : "not stable" );
			return FALSE;
		}
		if( cmd == "klick" || cmd == "klickFalls" )
		{
			Int x, y, w, h;
			win->winGetScreenPosition( &x, &y );
			win->winGetSize( &w, &h );
			const UnsignedInt pos = SHORTTOLONG( x + w / 2, y + h / 2 );
			// a check box only takes the release while the mouse is over it
			const Bool checkBox = BitTest( win->winGetStyle(), GWS_CHECK_BOX );
			if( checkBox )
				TheWindowManager->winSendInputMsg( win, GWM_MOUSE_ENTERING, pos, 0 );
			TheWindowManager->winSendInputMsg( win, GWM_LEFT_DOWN, pos, 0 );
			TheWindowManager->winSendInputMsg( win, GWM_LEFT_UP, pos, 0 );
			if( checkBox )
				TheWindowManager->winSendInputMsg( win, GWM_MOUSE_LEAVING, pos, 0 );
		}
		else if( cmd == "esc" )
		{
			TheWindowManager->winSendInputMsg( win, GWM_CHAR, KEY_ESC, KEY_STATE_DOWN );
			TheWindowManager->winSendInputMsg( win, GWM_CHAR, KEY_ESC, KEY_STATE_UP );
		}
		else if( cmd == "auswahl" )
		{
			Int index = b.words.size() > 2 ? atoi( b.words[2].c_str() ) : 0;
			if( BitTest( win->winGetStyle(), GWS_COMBO_BOX ) )
			{
				// a negative index counts from the end (-1: last entry), the lists differ between data sets
				if( index < 0 )
					index += GadgetComboBoxGetLength( win );
				GadgetComboBoxSetSelectedPos( win, index );
			}
			else if( BitTest( win->winGetStyle(), GWS_SCROLL_LISTBOX ) )
				GadgetListBoxSetSelected( win, index );
			else
			{
				failCommand( b, "neither combo box nor list box" );
				return FALSE;
			}
		}
		else if( cmd == "auswahlText" )
		{
			if( !BitTest( win->winGetStyle(), GWS_SCROLL_LISTBOX ) || b.words.size() < 3 )
			{
				failCommand( b, "auswahlText needs a list box and a text" );
				return FALSE;
			}
			std::string wanted = b.words[2];
			std::replace( wanted.begin(), wanted.end(), '_', ' ' );
			const Int columns = GadgetListBoxGetNumColumns( win );
			const Int onlyColumn = b.words.size() > 4 ? atoi( b.words[4].c_str() ) : -1;
			const Int n = GadgetListBoxGetNumEntries( win );
			Int found = -1;
			for( Int i = 0; i < n && found < 0; ++i )
			{
				for( Int c = 0; c < columns && found < 0; ++c )
				{
					if( onlyColumn >= 0 && c != onlyColumn )
						continue;
					AsciiString text;
					text.translate( GadgetListBoxGetText( win, i, c ) );
					if( strstr( text.str(), wanted.c_str() ) != nullptr )
						found = i;
				}
			}
			if( found < 0 )
			{
				failCommand( b, "no list entry contains the text" );
				return FALSE;
			}
			GadgetListBoxSetSelected( win, found );
		}
		else if( cmd == "liste" )
		{
			GameWindow *lb = nullptr;
			if( BitTest( win->winGetStyle(), GWS_COMBO_BOX ) )
				lb = GadgetComboBoxGetListBox( win );
			else if( BitTest( win->winGetStyle(), GWS_SCROLL_LISTBOX ) )
				lb = win;
			const Int n = lb ? GadgetListBoxGetNumEntries( lb ) : 0;
			const Int columns = lb ? GadgetListBoxGetNumColumns( lb ) : 0;
			logLine( "LISTE %s %d entries, %d columns", arg1.c_str(), n, columns );
			for( Int i = 0; i < n; ++i )
			{
				std::string row;
				for( Int c = 0; c < columns; ++c )
				{
					AsciiString text;
					text.translate( GadgetListBoxGetText( lb, i, c ) );
					if( c > 0 )
						row += " | ";
					row += text.str();
				}
				logLine( "LISTE %s %d %s", arg1.c_str(), i, row.c_str() );
			}
		}
		logLine( "OK line %d %s %s", b.line, cmd.c_str(), arg1.c_str() );
		return TRUE;
	}

	if( cmd == "bildlauf" )
		return runScrollCommand( b, d );

	if( cmd == "bildrate" )
	{
		TheGameEngine->setRenderFpsLimit( atoi( arg1.c_str() ) );
		logLine( "OK line %d bildrate %s", b.line, arg1.c_str() );
		return TRUE;
	}

	if( cmd == "bildlaufregler" )
	{
		TheWritableGlobalData->m_keyboardScrollFactor = static_cast<Real>( atoi( arg1.c_str() ) ) / 100.0f;
		logLine( "OK line %d bildlaufregler %s", b.line, arg1.c_str() );
		return TRUE;
	}

	if( cmd == "hauptmenue" )
	{
		WindowLayout *topWindow = TheShell ? TheShell->top() : nullptr;
		const Bool ready = topWindow && topWindow->getFilename().compareNoCase( "Menus/MainMenu.wnd" ) == 0 && !inMatch()
			&& ( TheTransitionHandler == nullptr || TheTransitionHandler->isFinished() )
			&& TheShell->isAnimFinished();
		if( ready )
			++s_state->stable;
		else
			s_state->stable = 0;
		if( s_state->stable < STABLE_PASSES )
		{
			if( d >= timeout( b, 1 ) )
				failCommand( b, "main menu not reached" );
			return FALSE;
		}
		logLine( "OK line %d hauptmenue after %u passes", b.line, d );
		return TRUE;
	}

	if( cmd == "partie" || cmd == "keinePartie" )
	{
		const Bool target = ( cmd == "partie" );
		if( inMatch() == target && ( target || TheGameLogic == nullptr || !TheGameLogic->isLoadingMap() ) )
			++s_state->stable;
		else
			s_state->stable = 0;
		if( s_state->stable < STABLE_PASSES )
		{
			if( d >= timeout( b, 1 ) )
				failCommand( b, target ? "no game" : "game still running" );
			return FALSE;
		}
		logLine( "OK line %d %s after %u passes, logic frame %u", b.line, cmd.c_str(), d,
			TheGameLogic ? TheGameLogic->getFrame() : 0 );
		return TRUE;
	}

	if( cmd == "spielmenue" )
	{
		TheMessageStream->appendMessage( GameMessage::MSG_META_OPTIONS );
		logLine( "OK line %d spielmenue", b.line );
		return TRUE;
	}

#if defined(_DEBUG)
	if( cmd == "sieg" )
	{
		TheMessageStream->appendMessage( GameMessage::MSG_META_DEBUG_WIN );
		logLine( "OK line %d sieg", b.line );
		return TRUE;
	}
	if( cmd == "siegBisEnde" )
	{
		if( !inMatch() && ( TheGameLogic == nullptr || !TheGameLogic->isLoadingMap() ) )
			++s_state->stable;
		else
			s_state->stable = 0;
		if( s_state->stable >= STABLE_PASSES )
		{
			logLine( "OK line %d siegBisEnde after %u passes", b.line, d );
			return TRUE;
		}
		if( inMatch() && d % 300 == 0 )
			TheMessageStream->appendMessage( GameMessage::MSG_META_DEBUG_WIN );
		if( d >= timeout( b, 1 ) )
			failCommand( b, "game does not end" );
		return FALSE;
	}
	if( cmd == "gegnerVernichten" )
	{
		TheMessageStream->appendMessage( GameMessage::MSG_META_DEMO_KILL_ALL_ENEMIES );
		logLine( "OK line %d gegnerVernichten", b.line );
		return TRUE;
	}
	if( cmd == "niederlage" )
	{
		Player *local = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
		if( local == nullptr )
		{
			failCommand( b, "no local player" );
			return FALSE;
		}
		std::vector<ObjectID> ids;
		local->iterateObjects( collectOwn, &ids );
		for( ObjectID id : ids )
		{
			GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_DEBUG_KILL_OBJECT );
			msg->appendObjectIDArgument( id );
		}
		logLine( "OK line %d niederlage, %d objects", b.line, static_cast<Int>( ids.size() ) );
		return TRUE;
	}
#endif

	if( cmd == "speichern" )
	{
		const SaveCode code = TheGameState->saveGame( AsciiString( arg1.c_str() ), UnicodeString( L"Test script" ), SAVE_FILE_TYPE_NORMAL );
		if( code != SaveCode::SC_OK )
		{
			failCommand( b, "save failed" );
			return FALSE;
		}
		logLine( "OK line %d speichern %s", b.line, arg1.c_str() );
		return TRUE;
	}

	if( cmd == "laden" )
	{
		// like doLoadGame (PopupSaveLoad.cpp): prepare a single player game first when in the main menu
		AvailableGameInfo info;
		info.filename = arg1.c_str();
		TheGameState->getSaveGameInfoFromFile( TheGameState->getFilePathInSaveDirectory( info.filename ), &info.saveGameInfo );
		info.next = nullptr;
		info.prev = nullptr;
		if( TheShell && TheShell->isShellActive() )
			TheGameLogic->prepareNewGame( GAME_SINGLE_PLAYER, DIFFICULTY_NORMAL, 0 );
		if( TheGameState->loadGame( info ) != SaveCode::SC_OK )
		{
			failCommand( b, "load failed" );
			return FALSE;
		}
		logLine( "OK line %d laden %s", b.line, arg1.c_str() );
		return TRUE;
	}

	if( cmd == "merke" )
	{
#ifdef MEMORYPOOL_DEBUG
		debugMemorySetMark();
#endif
		logLine( "OK line %d merke", b.line );
		return TRUE;
	}

	if( cmd == "wachstum" )
	{
#ifdef MEMORYPOOL_DEBUG
		debugMemoryReportGrowth( arg1.empty() ? "unnamed" : arg1.c_str() );
#endif
		logLine( "OK line %d wachstum %s", b.line, arg1.c_str() );
		return TRUE;
	}

	if( cmd == "schliessen" )
	{
		logLine( "OK line %d schliessen", b.line );
		::PostMessage( ApplicationHWnd, WM_CLOSE, 0, 0 );
		return TRUE;
	}

	if( cmd == "beenden" )
	{
		logLine( "OK line %d beenden", b.line );
		if( TheGameLogic && TheGameLogic->isInGame() )
		{
			if( TheRecorder && TheRecorder->getMode() == RecorderModeType::RECORDERMODETYPE_RECORD )
				TheRecorder->stopRecording();
			TheGameLogic->clearGameData( FALSE );
		}
		TheGameEngine->setQuitting( TRUE );
		return TRUE;
	}

	// Hotkeys, test helpers and exit during the load screen, for the leak check
	if( cmd == "meta" )
	{
		const GameMessage::Type typ = MetaMap::findMetaTypeByName( arg1.c_str() );
		if( typ == GameMessage::MSG_INVALID )
		{
			failCommand( b, "unknown meta command" );
			return FALSE;
		}
		TheMessageStream->appendMessage( typ );
		logLine( "OK line %d meta %s", b.line, arg1.c_str() );
		return TRUE;
	}

	if( cmd == "rad" )
	{
		const Int steps = atoi( arg1.c_str() );
		if( !inMatch() || TheTacticalView == nullptr || TheDisplay == nullptr || steps == 0 )
		{
			failCommand( b, "rad <n> needs a running game and n != 0" );
			return FALSE;
		}
		const UnsignedInt count = static_cast<UnsignedInt>( steps < 0 ? -steps : steps );
		const ICoord2D centre = { static_cast<Int>( TheDisplay->getWidth() ) / 2, static_cast<Int>( TheDisplay->getHeight() ) / 3 };
		if( d < count )
		{
			appendMouseWheel( centre, steps > 0 ? 1 : -1 );
			return FALSE;
		}
		if( d < count + 60 )
			return FALSE;	// let the height settle
		logLine( "OK line %d rad %d zoom %.3f height %.1f current %.1f", b.line, steps, TheTacticalView->getZoom(),
			TheTacticalView->getHeightAboveGround(), TheTacticalView->getCurrentHeightAboveGround() );
		return TRUE;
	}

	if( cmd == "ziehenEcht" )
	{
		Int sx, sy;
		const UnsignedInt frames = b.words.size() > 2 ? static_cast<UnsignedInt>( atoi( b.words[2].c_str() ) ) : 150u;
		const Int step = b.words.size() > 3 ? atoi( b.words[3].c_str() ) : 40;
		if( directionOf( arg1, sx, sy ) == 0 || frames == 0 )
		{
			failCommand( b, "ziehenEcht <rechts|links|hoch|runter> [frames] [pixels]" );
			return FALSE;
		}
		if( d == 0 )
		{
			// from the map centre, so that the map edge does not end the drag
			if( inMatch() && TheTerrainLogic != nullptr && TheTacticalView != nullptr )
			{
				Region3D extent;
				TheTerrainLogic->getExtent( &extent );
				Coord3D middle;
				middle.x = ( extent.lo.x + extent.hi.x ) * 0.5f;
				middle.y = ( extent.lo.y + extent.hi.y ) * 0.5f;
				middle.z = 0.0f;
				TheTacticalView->lookAt( &middle );
			}
			if( !beginRealInput( b, "ziehenEcht needs a running game" ) )
				return FALSE;
			GetCursorPos( &s_realRun.savedCursor );
			POINT screen = s_realRun.centre;
			ClientToScreen( ApplicationHWnd, &screen );
			SetCursorPos( screen.x, screen.y );
			const LPARAM at = clientParam( s_realRun.centre.x, s_realRun.centre.y );
			::PostMessage( ApplicationHWnd, WM_MOUSEMOVE, 0, at );
			::PostMessage( ApplicationHWnd, WM_RBUTTONDOWN, MK_RBUTTON, at );
			return FALSE;
		}
		POINT cursor;
		GetCursorPos( &cursor );
		ScreenToClient( ApplicationHWnd, &cursor );
		// another window taking the focus (someone using the mouse) ends the drag; the run then says so
		if( GetForegroundWindow() != ApplicationHWnd )
			s_realRun.focus = FALSE;
		if( s_realRun.endPass == 0 && TheGameLogic->getFrame() < s_realRun.startFrame + frames )
		{
			// the drag puts the cursor back where it began after every move: each move goes one step from there
			::PostMessage( ApplicationHWnd, WM_MOUSEMOVE, MK_RBUTTON, clientParam( cursor.x + sx * step, cursor.y + sy * step ) );
			if( TheMouse->isDragScrolling() )
			{
				s_realRun.dragged = TRUE;
				if( TheMouse->getVisibility() )
					s_realRun.hidden = FALSE;
			}
			return FALSE;
		}
		if( s_realRun.endPass == 0 )
		{
			::PostMessage( ApplicationHWnd, WM_RBUTTONUP, 0, clientParam( cursor.x + sx * step, cursor.y + sy * step ) );
			s_realRun.endPass = d;
			return FALSE;
		}
		if( d < s_realRun.endPass + 30 )
			return FALSE;	// let the release and the camera settle
		const Int offX = cursor.x - s_realRun.centre.x, offY = cursor.y - s_realRun.centre.y;
		const Bool back = offX >= -1 && offX <= 1 && offY >= -1 && offY <= 1;
		logLine( "ZIEHENECHT dir=%s frames=%u step=%d focus=%d dragged=%d hidden=%d back=%d cursor=%d,%d visible=%d "
			"still_dragging=%d world=%.1f across=%.1f", arg1.c_str(), frames, step, s_realRun.focus ? 1 : 0,
			s_realRun.dragged ? 1 : 0, ( s_realRun.dragged && s_realRun.hidden ) ? 1 : 0, back ? 1 : 0, offX, offY,
			TheMouse->getVisibility() ? 1 : 0, TheMouse->isDragScrolling() ? 1 : 0, realInputTravel(), s_realRun.across );
		TheWritableGlobalData->m_testRealInput = FALSE;
		SetCursorPos( s_realRun.savedCursor.x, s_realRun.savedCursor.y );
		return TRUE;
	}

	if( cmd == "radEcht" )
	{
		const Int delta = atoi( arg1.c_str() );
		const UnsignedInt count = b.words.size() > 2 ? static_cast<UnsignedInt>( atoi( b.words[2].c_str() ) ) : 1u;
		if( delta == 0 || count == 0 )
		{
			failCommand( b, "radEcht <delta> [count]" );
			return FALSE;
		}
		if( d == 0 && !beginRealInput( b, "radEcht needs a running game" ) )
			return FALSE;
		if( d < count )
		{
			// the wheel reports screen coordinates; over the upper middle no window takes it
			POINT screen = { s_realRun.centre.x, s_realRun.centre.y * 2 / 3 };
			ClientToScreen( ApplicationHWnd, &screen );
			::PostMessage( ApplicationHWnd, WM_MOUSEWHEEL, MAKEWPARAM( 0, static_cast<Short>( delta ) ), MAKELPARAM( screen.x, screen.y ) );
			return FALSE;
		}
		if( d < count + 60 )
			return FALSE;	// let the height settle
		logLine( "RADECHT delta=%d count=%u focus=%d fine=%d zoom_before=%.4f zoom_after=%.4f height=%.1f", delta, count,
			s_realRun.focus ? 1 : 0, TheMouse->hasFineWheel() ? 1 : 0, s_realRun.zoom, TheTacticalView->getZoom(),
			TheTacticalView->getCurrentHeightAboveGround() );
		TheWritableGlobalData->m_testRealInput = FALSE;
		return TRUE;
	}

	if( cmd == "tasteEcht" )
	{
		Int sx, sy;
		const Int key = directionOf( arg1, sx, sy );
		const UnsignedInt hold = b.words.size() > 2 ? static_cast<UnsignedInt>( atoi( b.words[2].c_str() ) ) : 60u;
		if( key == 0 || hold == 0 )
		{
			failCommand( b, "tasteEcht <rechts|links|hoch|runter> [passes]" );
			return FALSE;
		}
		if( d == 0 )
		{
			if( !beginRealInput( b, "tasteEcht needs a running game" ) )
				return FALSE;
			GameWindow *focus = TheWindowManager ? TheWindowManager->winGetFocus() : nullptr;
			if( focus != nullptr )
				TheWindowManager->winSetFocus( nullptr );	// a focused window would take the arrow keys
			const LPARAM shiftDown = 1 | ( static_cast<LPARAM>( MapVirtualKey( VK_SHIFT, 0 ) ) << 16 );
			const LPARAM arrowDown = 1 | ( static_cast<LPARAM>( MapVirtualKey( key, 0 ) ) << 16 ) | ( 1 << 24 );
			::PostMessage( ApplicationHWnd, WM_KEYDOWN, VK_SHIFT, shiftDown );
			::PostMessage( ApplicationHWnd, WM_KEYDOWN, key, arrowDown );
			return FALSE;
		}
		if( d < hold )
			return FALSE;
		if( d == hold )
		{
			const LPARAM up = ( static_cast<LPARAM>( 1 ) << 30 ) | ( static_cast<LPARAM>( 1 ) << 31 ) | 1;
			::PostMessage( ApplicationHWnd, WM_KEYUP, key, up | ( static_cast<LPARAM>( MapVirtualKey( key, 0 ) ) << 16 ) | ( 1 << 24 ) );
			::PostMessage( ApplicationHWnd, WM_KEYUP, VK_SHIFT, up | ( static_cast<LPARAM>( MapVirtualKey( VK_SHIFT, 0 ) ) << 16 ) );
			return FALSE;
		}
		if( d < hold + 30 )
			return FALSE;
		logLine( "TASTEECHT dir=%s passes=%u focus=%d world=%.1f across=%.1f", arg1.c_str(), hold, s_realRun.focus ? 1 : 0,
			realInputTravel(), s_realRun.across );
		TheWritableGlobalData->m_testRealInput = FALSE;
		return TRUE;
	}

	if( cmd == "klickEigenes" )
	{
		static ClickSearch s_click;
		if( d == 0 )
		{
			Player *localPlayer = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
			if( !inMatch() || localPlayer == nullptr || TheTacticalView == nullptr || TheDisplay == nullptr )
			{
				failCommand( b, "klickEigenes needs a running game" );
				return FALSE;
			}
			s_click = {};
			s_click.maxY = static_cast<Int>( TheDisplay->getHeight() * 0.7f );
			localPlayer->iterateObjects( findClickable, &s_click );
			if( s_click.found == nullptr )
			{
				s_click.structures = TRUE;
				localPlayer->iterateObjects( findClickable, &s_click );
			}
			if( s_click.found == nullptr )
			{
				failCommand( b, "no own selectable object on screen" );
				return FALSE;
			}
			TheInGameUI->deselectAllDrawables();
			appendMousePosition( s_click.screen );
			return FALSE;
		}
		if( d == 2 )
		{
			GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN );
			msg->appendPixelArgument( s_click.screen );
			msg->appendIntegerArgument( 0 );
			msg->appendIntegerArgument( static_cast<Int>( Engine::Clock::milliseconds() ) );
			return FALSE;
		}
		if( d == 3 )
		{
			GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_UP );
			msg->appendPixelArgument( s_click.screen );
			msg->appendIntegerArgument( 0 );
			msg->appendIntegerArgument( static_cast<Int>( Engine::Clock::milliseconds() ) );
			return FALSE;
		}
		if( d < 10 )
			return FALSE;
		const Drawable *selected = TheInGameUI->getFirstSelectedDrawable();
		const Bool hit = selected != nullptr && selected->getID() == s_click.drawable;
		logLine( "%s line %d klickEigenes %s at %d,%d selected %d zoom %.3f height %.1f", hit ? "OK" : "MISS", b.line,
			s_click.name, s_click.screen.x, s_click.screen.y, TheInGameUI->getSelectCount(),
			TheTacticalView->getZoom(), TheTacticalView->getCurrentHeightAboveGround() );
		return TRUE;
	}

	if( cmd == "rahmen" )
	{
		static BoxSearch s_box;
		if( d == 0 )
		{
			Player *localPlayer = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
			if( !inMatch() || localPlayer == nullptr || TheTacticalView == nullptr || TheDisplay == nullptr )
			{
				failCommand( b, "rahmen needs a running game" );
				return FALSE;
			}
			const Int margin = b.words.size() >= 2 ? atoi( arg1.c_str() ) : 8;
			s_box = {};
			TheTacticalView->getOrigin( &s_box.view.lo.x, &s_box.view.lo.y );
			s_box.view.hi.x = s_box.view.lo.x + TheTacticalView->getWidth();
			s_box.view.hi.y = s_box.view.lo.y + TheTacticalView->getHeight();
			localPlayer->iterateObjects( findBoxUnit, &s_box );
			if( s_box.count == 0 )
			{
				failCommand( b, "no own selectable unit on screen" );
				return FALSE;
			}
			const std::string edge = b.words.size() >= 3 ? b.words[2] : std::string();
			const Int SPREAD = 200;
			s_box.box.lo.x -= ( edge == "links" || edge.empty() ) ? 0 : SPREAD;
			s_box.box.lo.y -= ( edge == "oben" || edge.empty() ) ? 0 : SPREAD;
			s_box.box.hi.x += ( edge == "rechts" || edge.empty() ) ? 0 : SPREAD;
			s_box.box.hi.y += ( edge == "unten" || edge.empty() ) ? 0 : SPREAD;
			s_box.box.lo.x = std::max( s_box.view.lo.x, s_box.box.lo.x - margin );
			s_box.box.lo.y = std::max( s_box.view.lo.y, s_box.box.lo.y - margin );
			s_box.box.hi.x = std::min( s_box.view.hi.x - 1, s_box.box.hi.x + margin );
			s_box.box.hi.y = std::min( s_box.view.hi.y - 1, s_box.box.hi.y + margin );
			TheInGameUI->deselectAllDrawables();
			appendMousePosition( s_box.box.lo );
			return FALSE;
		}
		if( d == 2 )
		{
			GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN );
			msg->appendPixelArgument( s_box.box.lo );
			msg->appendIntegerArgument( 0 );
			msg->appendIntegerArgument( static_cast<Int>( Engine::Clock::milliseconds() ) );
			return FALSE;
		}
		if( d == 3 || d == 4 )
		{
			ICoord2D pos = s_box.box.hi;
			if( d == 3 )
			{
				pos.x = ( s_box.box.lo.x + s_box.box.hi.x ) / 2;
				pos.y = ( s_box.box.lo.y + s_box.box.hi.y ) / 2;
			}
			appendMousePosition( pos );
			return FALSE;
		}
		if( d == 5 )
		{
			GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_UP );
			msg->appendPixelArgument( s_box.box.hi );
			msg->appendIntegerArgument( 0 );
			msg->appendIntegerArgument( static_cast<Int>( Engine::Clock::milliseconds() ) );
			return FALSE;
		}
		if( d < 12 )
			return FALSE;
		Int selected = 0;
		for( Int i = 0; i < s_box.count; ++i )
		{
			const Drawable *draw = TheGameClient->findDrawableByID( s_box.drawables[i] );
			if( draw != nullptr && draw->isSelected() )
				++selected;
		}
		logLine( "%s line %d rahmen edge %s box %d,%d-%d,%d units %d selected %d of them, %d in all; display %dx%d view %d,%d %dx%d",
			selected == s_box.count ? "OK" : "MISS", b.line, b.words.size() >= 3 ? b.words[2].c_str() : "-", s_box.box.lo.x, s_box.box.lo.y, s_box.box.hi.x, s_box.box.hi.y,
			s_box.count, selected, TheInGameUI->getSelectCount(), TheDisplay->getWidth(), TheDisplay->getHeight(),
			s_box.view.lo.x, s_box.view.lo.y, TheTacticalView->getWidth(), TheTacticalView->getHeight() );
		return TRUE;
	}

	if( cmd == "streifen" )
	{
		static DrawableID s_unit;
		static ICoord2D s_target;
		static Int s_side;
		static UnsignedInt s_check;
		const Bool behind = arg1 == "mitte";
		if( d == 0 )
		{
			Player *localPlayer = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
			if( !inMatch() || localPlayer == nullptr || TheTacticalView == nullptr || TheDisplay == nullptr )
			{
				failCommand( b, "streifen needs a running game" );
				return FALSE;
			}
			if( !TheDisplay->isUILayoutPillarboxed() )
			{
				logLine( "OK line %d streifen skipped, no widescreen layout", b.line );
				return TRUE;
			}
			BoxSearch units = {};
			TheTacticalView->getOrigin( &units.view.lo.x, &units.view.lo.y );
			units.view.hi.x = units.view.lo.x + TheTacticalView->getWidth();
			units.view.hi.y = static_cast<Int>( TheDisplay->getHeight() );
			localPlayer->iterateObjects( findBoxUnit, &units );
			if( units.count == 0 )
			{
				failCommand( b, "no own selectable unit on screen" );
				return FALSE;
			}
			s_unit = units.drawables[0];
			s_side = 0;
			s_check = 20;
		}
		Drawable *draw = TheGameClient->findDrawableByID( s_unit );
		if( draw == nullptr )
		{
			failCommand( b, "the unit is gone" );
			return FALSE;
		}
		Int originX, originY;
		TheTacticalView->getOrigin( &originX, &originY );
		const Int viewBottom = originY + TheTacticalView->getHeight();
		ICoord2D screen = { -1, -1 };
		const Bool onScreen = TheTacticalView->worldToScreen( draw->getPosition(), &screen );
		Bool covered = FALSE;
		GameWindow *bar = findWindow( "ControlBar.wnd:ControlBarParent" );
		if( onScreen && bar != nullptr && !bar->winIsHidden() )
		{
			Int x, y, width, height;
			bar->winGetScreenPosition( &x, &y );
			bar->winGetSize( &width, &height );
			covered = screen.x >= x && screen.x < x + width && screen.y >= y && screen.y < y + height;
		}
		const Bool placed = onScreen && screen.y >= viewBottom && screen.y < static_cast<Int>( TheDisplay->getHeight() ) && covered == behind;
		if( d == s_check && !placed && s_side == 0 )
		{
			// The map edge can stop the camera; try the other side.
			s_side = 1;
			s_check += 20;
		}
		if( d + 20 == s_check || d + 10 == s_check )
		{
			// Strip width from the UI layout; behind the bar means the middle of the screen.
			Real scaleX, scaleY;
			Int stripX, stripY;
			TheDisplay->getUILayoutTransform( 800, 600, &scaleX, &scaleY, &stripX, &stripY );
			const Int width = TheTacticalView->getWidth();
			s_target.x = behind ? originX + width / 2 + ( s_side ? 200 : -200 ) : s_side ? originX + width - stripX / 2 : originX + stripX / 2;
			s_target.y = static_cast<Int>( TheDisplay->getHeight() ) - 40;
			// Shift the camera by the ground offset between the target point and the unit.
			Coord3D ground, pos;
			TheTacticalView->screenToTerrain( &s_target, &ground );
			TheTacticalView->getPosition( &pos );
			pos.x += draw->getPosition()->x - ground.x;
			pos.y += draw->getPosition()->y - ground.y;
			TheTacticalView->lookAt( &pos );
			return FALSE;
		}
		if( d < s_check )
			return FALSE;
		if( d == s_check )
		{
			TheInGameUI->deselectAllDrawables();
			KindOfMaskType clear = KINDOFMASK_NONE;
			clear.set( KINDOF_STRUCTURE );
			TheInGameUI->selectAllUnitsByTypeAcrossScreen( KINDOFMASK_NONE, clear );
			return FALSE;
		}
		if( d < s_check + 5 )
			return FALSE;
		const Bool ok = placed && draw->isSelected() != behind;
		logLine( "%s line %d streifen%s unit at %d,%d (target %d,%d) placed %d behind bar %d selected %d, %d in all; view height %d display %dx%d",
			ok ? "OK" : "MISS", b.line, behind ? " mitte" : "", screen.x, screen.y, s_target.x, s_target.y, placed ? 1 : 0,
			covered ? 1 : 0, draw->isSelected() ? 1 : 0, TheInGameUI->getSelectCount(), TheTacticalView->getHeight(),
			TheDisplay->getWidth(), TheDisplay->getHeight() );
		return TRUE;
	}

	if( cmd == "zeichenbereich" )
	{
		Region3D region;
		if( !inMatch() || TheTacticalView == nullptr || TheDisplay == nullptr || !TheTacticalView->getDrawRegion( &region ) )
		{
			failCommand( b, "zeichenbereich needs a running game" );
			return FALSE;
		}
		Int x0, y0;
		TheTacticalView->getOrigin( &x0, &y0 );
		const Int w = TheTacticalView->getWidth();
		Int bottom = y0 + TheTacticalView->getHeight() - 1;
		if( TheDisplay->isUILayoutPillarboxed() )
			bottom = static_cast<Int>( TheDisplay->getHeight() ) - 1;
		const Int STEPS = 8;
		Int points = 0, outside = 0;
		Real worst = 0;
		ICoord2D worstAt = { 0, 0 };
		for( Int edge = 0; edge < 4; ++edge )
		{
			for( Int i = 0; i <= STEPS; ++i )
			{
				ICoord2D screen;
				if( edge < 2 )
				{
					screen.x = x0 + ( w - 1 ) * i / STEPS;
					screen.y = edge == 0 ? y0 : bottom;
				}
				else
				{
					screen.x = edge == 2 ? x0 : x0 + w - 1;
					screen.y = y0 + ( bottom - y0 ) * i / STEPS;
				}
				Coord3D ground;
				TheTacticalView->screenToTerrain( &screen, &ground );
				const Real dx = std::max( { region.lo.x - ground.x, ground.x - region.hi.x, 0.0f } );
				const Real dy = std::max( { region.lo.y - ground.y, ground.y - region.hi.y, 0.0f } );
				const Real distance = sqrtf( dx * dx + dy * dy );
				++points;
				if( distance > 0 )
					++outside;
				if( distance > worst )
				{
					worst = distance;
					worstAt = screen;
				}
			}
		}
		logLine( "%s line %d zeichenbereich points %d outside %d worst %.1f at %d,%d; region %.0f,%.0f-%.0f,%.0f view %dx%d bottom %d",
			outside == 0 ? "OK" : "MISS", b.line, points, outside, worst, worstAt.x, worstAt.y, region.lo.x, region.lo.y,
			region.hi.x, region.hi.y, w, TheTacticalView->getHeight(), bottom );
		return TRUE;
	}

	if( cmd == "markierung" )
	{
		Player *localPlayer = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
		if( localPlayer == nullptr || TheGameLogic == nullptr || b.words.size() < 2 || ( arg1 != "basis" && b.words.size() < 3 ) )
		{
			failCommand( b, "needs x y or basis [dx dy], and a local player" );
			return FALSE;
		}
		Coord3D pos;
		pos.zero();
		if( arg1 == "basis" )
		{
			Object *home = nullptr;
			for( Object *obj = TheGameLogic->getFirstObject(); obj && home == nullptr; obj = obj->getNextObject() )
			{
				if( obj->getControllingPlayer() == localPlayer && obj->isKindOf( KINDOF_STRUCTURE ) && !obj->isEffectivelyDead() )
					home = obj;
			}
			if( home == nullptr )
			{
				failCommand( b, "no own building" );
				return FALSE;
			}
			pos = *home->getPosition();
			pos.x += b.words.size() > 2 ? static_cast<Real>( atof( b.words[2].c_str() ) ) : 0.0f;
			pos.y += b.words.size() > 3 ? static_cast<Real>( atof( b.words[3].c_str() ) ) : 0.0f;
		}
		else
		{
			pos.x = static_cast<Real>( atof( arg1.c_str() ) );
			pos.y = static_cast<Real>( atof( b.words[2].c_str() ) );
		}
		pos.z = TheTerrainLogic ? TheTerrainLogic->getGroundHeight( pos.x, pos.y ) : 0.0f;
		GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_PLACE_BEACON );
		msg->appendLocationArgument( pos );
		logLine( "OK line %d markierung at %.0f %.0f", b.line, pos.x, pos.y );
		return TRUE;
	}

	if( cmd == "spezial" )
	{
		Player *localPlayer = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
		SpecialPowerSearch search = { TheSpecialPowerStore ? TheSpecialPowerStore->findSpecialPowerTemplate( AsciiString( arg1.c_str() ) ) : nullptr, nullptr };
		if( localPlayer == nullptr || search.power == nullptr )
		{
			failCommand( b, "unknown special power or no local player" );
			return FALSE;
		}
		localPlayer->iterateObjects( findSpecialPowerSource, &search );
		if( search.source == nullptr )
		{
			failCommand( b, "no own object with this special power" );
			return FALSE;
		}
		Coord3D targetPos = *search.source->getPosition();
		targetPos.x += b.words.size() > 2 ? static_cast<Real>( atof( b.words[2].c_str() ) ) : 200.0f;
		targetPos.y += b.words.size() > 3 ? static_cast<Real>( atof( b.words[3].c_str() ) ) : 200.0f;
		GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_DO_SPECIAL_POWER_AT_LOCATION );
		msg->appendIntegerArgument( static_cast<Int>( search.power->getID() ) );
		msg->appendLocationArgument( targetPos );
		msg->appendRealArgument( 0.0f );
		msg->appendObjectIDArgument( INVALID_ID );
		msg->appendIntegerArgument( 0 );
		msg->appendObjectIDArgument( search.source->getID() );
		logLine( "OK line %d spezial %s from %s at %.0f %.0f", b.line, arg1.c_str(),
			search.source->getTemplate()->getName().str(), targetPos.x, targetPos.y );
		return TRUE;
	}

	if( cmd == "spezialUmlenken" )
	{
		Player *localPlayer = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
		SpecialPowerSearch search = { TheSpecialPowerStore ? TheSpecialPowerStore->findSpecialPowerTemplate( AsciiString( arg1.c_str() ) ) : nullptr, nullptr };
		if( localPlayer == nullptr || search.power == nullptr )
		{
			failCommand( b, "unknown special power or no local player" );
			return FALSE;
		}
		localPlayer->iterateObjects( findRedirectable, &search );
		if( search.source == nullptr )
		{
			failCommand( b, "no running redirectable special power" );
			return FALSE;
		}
		Coord3D targetPos = *search.source->getPosition();
		targetPos.x += b.words.size() > 2 ? static_cast<Real>( atof( b.words[2].c_str() ) ) : 200.0f;
		targetPos.y += b.words.size() > 3 ? static_cast<Real>( atof( b.words[3].c_str() ) ) : 200.0f;
		GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_DO_SPECIAL_POWER_OVERRIDE_DESTINATION );
		msg->appendLocationArgument( targetPos );
		msg->appendIntegerArgument( static_cast<Int>( search.power->getSpecialPowerType() ) );
		msg->appendObjectIDArgument( search.source->getID() );
		logLine( "OK line %d spezialUmlenken %s from %s to %.0f %.0f", b.line, arg1.c_str(),
			search.source->getTemplate()->getName().str(), targetPos.x, targetPos.y );
		return TRUE;
	}

	if( cmd == "testhilfe" )
	{
		const Int command = TestAids_CommandByName( arg1.c_str() );
		if( command == TA_NONE )
		{
			failCommand( b, "unknown test aid" );
			return FALSE;
		}
		if( !TestAids_ClientAllowed() )
		{
			failCommand( b, "test aids not allowed (needs -testhilfen and a single-player game)" );
			return FALSE;
		}
		TestAids_Command( command, b.words.size() > 2 ? atoi( b.words[2].c_str() ) : -1 );
		logLine( "OK line %d testhilfe %s", b.line, arg1.c_str() );
		return TRUE;
	}

	if( cmd == "schliessenNach" )
	{
		const Int ms = atoi( arg1.c_str() );
		if( s_closeTimer != nullptr || ms <= 0
			|| !::CreateTimerQueueTimer( &s_closeTimer, nullptr, closeTimerCallback, nullptr, static_cast<DWORD>( ms ), 0, WT_EXECUTEONLYONCE ) )
		{
			failCommand( b, "timer not created" );
			return FALSE;
		}
		logLine( "OK line %d schliessenNach %d ms", b.line, ms );
		return TRUE;
	}

	if( cmd == "log" )
	{
		std::string text;
		for( size_t i = 1; i < b.words.size(); ++i )
			text += ( i > 1 ? " " : "" ) + b.words[i];
		logLine( "LOG %s", text.c_str() );
		return TRUE;
	}

	failCommand( b, "unknown command" );
	return FALSE;
}

}	// namespace

void TestScript_Update( void )
{
	if( !s_loaded )
		loadScript();
	if( s_state == nullptr || s_state->done || TheGameEngine == nullptr || TheGameEngine->getQuitting() )
		return;
	if( s_state->nextIndex >= s_state->commands.size() )
	{
		logLine( "ENDE" );
		s_state->done = TRUE;
		return;
	}
	const ScriptCommand &b = s_state->commands[s_state->nextIndex];
	if( executeCommand( b ) )
	{
		++s_state->nextIndex;
		s_state->pass = 0;
		s_state->stable = 0;
	}
	else
		++s_state->pass;
}

void TestScript_Release( void )
{
	if( s_closeTimer != nullptr )
	{
		// waits if the callback is running
		::DeleteTimerQueueTimer( nullptr, s_closeTimer, INVALID_HANDLE_VALUE );
		s_closeTimer = nullptr;
	}
	delete s_state;
	s_state = nullptr;
}
