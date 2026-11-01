/*
**	Command & Conquer Generals(tm) / Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**	Modified by GZH Neo, Copyright (C) 2026 GZH Neo
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: Win32Mouse.cpp ///////////////////////////////////////////////////////////////////////////
// Created:    Colin Day, July 2001
// Desc:       Interface for the mouse using only the Win32 messages
///////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <vector>

#include "Common/Debug.h"
#include "formatinto.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/file.h"
#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/LocalFileSystem.h"
#include "GameClient/GameClient.h"
#include "Win32Device/Common/Win32Monitors.h"
#include "Win32Device/GameClient/RealInputFilter.h"
#include "Win32Device/GameClient/Win32Mouse.h"
#include "Win32Device/Common/Win32AppWindow.h"
#include "WW3D2/Common/renderdevice.h"
#include "formatinto.h"

// EXTERN /////////////////////////////////////////////////////////////////////////////////////////
#include "Engine/EngineContext.h"
#include "Engine/UserDataFiles.h"
#include "Engine/InputQueue.h"
#include "Common/UserSettings.h"
#include <algorithm>

HCURSOR cursorResources[Mouse::NUM_MOUSE_CURSORS][MAX_2D_CURSOR_DIRECTIONS];
///////////////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
/** Get a mouse event from the buffer if available, we need to translate
	* from the windows message meanings to our own internal mouse 
	* structure */
//-------------------------------------------------------------------------------------------------
UnsignedByte Win32Mouse::getMouseEvent( MouseIO *result, Bool flush )
{

	// if there is nothing here there is no event data to do
	if( m_eventBuffer[ m_nextGetIndex ].msg == 0 )
		return MOUSE_NONE;

	// translate the win32 mouse message to our own system
	translateEvent( m_nextGetIndex, result );

	// remove this event from the buffer by setting msg to zero
	m_eventBuffer[ m_nextGetIndex ].msg = 0;

	//
	// our next get index will now be advanced to the next index, wrapping at
	// the mad
	//
	m_nextGetIndex++;
	if( m_nextGetIndex >= Mouse::NUM_MOUSE_EVENTS )
		m_nextGetIndex = 0;

	// got event OK and all done with this one
	return MOUSE_OK;

}  // end getMouseEvent

//-------------------------------------------------------------------------------------------------
/** Translate a win32 mouse event to our own event info */
//-------------------------------------------------------------------------------------------------
void Win32Mouse::translateEvent( UnsignedInt eventIndex, MouseIO *result )
{
	UINT msg = m_eventBuffer[ eventIndex ].msg;
	WPARAM wParam = m_eventBuffer[ eventIndex ].wParam;
	LPARAM lParam = m_eventBuffer[ eventIndex ].lParam;
	UnsignedInt frame;

	//
	// get the current input frame from the client, if we don't have
	// a client (like in the GUI editor) we just use frame 1 so it
	// registers with the system
	//
	if( TheGameClient )
		frame = TheGameClient->getFrame();
	else
		frame = 1;

	// set these to defaults
	result->leftState = result->middleState = result->rightState = MBS_Up;
	result->leftFrame = result->middleFrame = result->rightFrame = 0;
	result->pos.x = result->pos.y = result->wheelPos = 0;
	result->hWheelPos = 0;

	// Time is the same for all events
	result->time = m_eventBuffer[ eventIndex ].time;
	
	switch( msg )
	{

		// ------------------------------------------------------------------------
		case WM_LBUTTONDOWN:
		{
			
			result->leftState = MBS_Down;
			result->leftFrame = frame;
			result->pos.x = static_cast<Short>(LOWORD( lParam ));
			result->pos.y = static_cast<Short>(HIWORD( lParam ));
			break;

		}  // end left button down

		// ------------------------------------------------------------------------
		case WM_LBUTTONUP:
		{

			result->leftState = MBS_Up;
			result->leftFrame = frame;
			result->pos.x = static_cast<Short>(LOWORD( lParam ));
			result->pos.y = static_cast<Short>(HIWORD( lParam ));
			break;

		}  // end left button up

		// ------------------------------------------------------------------------
		case WM_LBUTTONDBLCLK:
		{

			result->leftState = MBS_DoubleClick;
			result->leftFrame = frame;
			result->pos.x = static_cast<Short>(LOWORD( lParam ));
			result->pos.y = static_cast<Short>(HIWORD( lParam ));
			break;

		}  // end left button double click

		// ------------------------------------------------------------------------
		case WM_MBUTTONDOWN:
		{

			result->middleState = MBS_Down;
			result->middleFrame = frame;
			result->pos.x = static_cast<Short>(LOWORD( lParam ));
			result->pos.y = static_cast<Short>(HIWORD( lParam ));
			break;

		}  // end middle button down

		// ------------------------------------------------------------------------
		case WM_MBUTTONUP:
		{

			result->middleState = MBS_Up;
			result->middleFrame = frame;
			result->pos.x = static_cast<Short>(LOWORD( lParam ));
			result->pos.y = static_cast<Short>(HIWORD( lParam ));
			break;

		}  // end middle button up

		// ------------------------------------------------------------------------
		case WM_MBUTTONDBLCLK:
		{

			result->middleState = MBS_DoubleClick;
			result->middleFrame = frame;
			result->pos.x = static_cast<Short>(LOWORD( lParam ));
			result->pos.y = static_cast<Short>(HIWORD( lParam ));
			break;

		}  // end middle button double click

		// ------------------------------------------------------------------------
		case WM_RBUTTONDOWN:
		{

			result->rightState = MBS_Down;
			result->rightFrame = frame;
			result->pos.x = static_cast<Short>(LOWORD( lParam ));
			result->pos.y = static_cast<Short>(HIWORD( lParam ));
			break;

		}  // end right button down

		// ------------------------------------------------------------------------
		case WM_RBUTTONUP:
		{

			result->rightState = MBS_Up;
			result->rightFrame = frame;
			result->pos.x = static_cast<Short>(LOWORD( lParam ));
			result->pos.y = static_cast<Short>(HIWORD( lParam ));
			break;

		}  // end right button up

		// ------------------------------------------------------------------------
		case WM_RBUTTONDBLCLK:
		{

			result->rightState = MBS_DoubleClick;
			result->rightFrame = frame;
			result->pos.x = static_cast<Short>(LOWORD( lParam ));
			result->pos.y = static_cast<Short>(HIWORD( lParam ));
			break;

		}  // end right button double click

		// ------------------------------------------------------------------------
		case WM_MOUSEMOVE:
		{

			result->pos.x = static_cast<Short>(LOWORD( lParam ));
			result->pos.y = static_cast<Short>(HIWORD( lParam ));
			break;

		}  // end mouse move

		// ------------------------------------------------------------------------
		case 0x020E:	// WM_MOUSEHWHEEL, tilting the wheel; + is to the right
		{
			POINT p;
			p.x = static_cast<Short>(LOWORD( lParam ));
			p.y = static_cast<Short>(HIWORD( lParam ));
			ScreenToClient( ApplicationHWnd, &p );
			result->hWheelPos = static_cast<Short>(HIWORD( wParam ));
			result->pos.x = p.x;
			result->pos.y = p.y;
			break;
		}

		case 0x020A:	// WM_MOUSEWHEEL
		{
			POINT p;

			// translate the screen mouse position to be relative to the application window; the
			// coordinates are signed (negative on monitors left of or above the primary one)
			p.x = static_cast<Short>(LOWORD( lParam ));
			p.y = static_cast<Short>(HIWORD( lParam ));
			ScreenToClient( ApplicationHWnd, &p );
	
			// note the short cast here to keep signed information in tact
			result->wheelPos = static_cast<Short>(HIWORD( wParam ));
			result->pos.x = p.x;
			result->pos.y = p.y;
			break;

		}  // end mouse wheel

		// ------------------------------------------------------------------------
		default:
		{

			DEBUG_CRASH(( "translateEvent: Unknown Win32 mouse event [%d,%lld,%lld]\n",
							 msg, static_cast<long long>(wParam), static_cast<long long>(lParam) ));
			return;

		}  // end default

	}  // end switch on message at event index in buffer

	// Window client coordinates to display coordinates: in fullscreen the image is scaled into a
	// monitor-sized window. In windowed mode both are the same and nothing changes.
	int x = result->pos.x;
	int y = result->pos.y;
	RenderDevice::Window_To_Render( x, y );
	result->pos.x = x;
	result->pos.y = y;

}  // end translateEvent

///////////////////////////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Win32Mouse::Win32Mouse( void )
{

	// zero our event list
	memset( &m_eventBuffer, 0, sizeof( m_eventBuffer ) );

	m_nextFreeIndex = 0;
	m_nextGetIndex = 0;
	m_currentWin32Cursor = NONE;
	for (Int i=0; i<NUM_MOUSE_CURSORS; i++)
		for (Int j=0; j<MAX_2D_CURSOR_DIRECTIONS; j++)
			cursorResources[i][j]=nullptr;
	m_directionFrame=0; //points up.
	m_lostFocus = FALSE;
	m_dragAnchor.x = m_dragAnchor.y = 0;
	m_dragPosition.x = m_dragPosition.y = 0;
	m_dragWasVisible = TRUE;
}  // end Win32Mouse

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Win32Mouse::~Win32Mouse( void )
{

	// remove our global reference that was for the WndProc() only
	::Engine::context().win32Mouse = nullptr;

}  // end ~Win32Mouse

//-------------------------------------------------------------------------------------------------
/** Initialize our device */
//-------------------------------------------------------------------------------------------------
void Win32Mouse::init( void )
{

	// extending functionality
	Mouse::init();

	//
	// when we receive messages from a Windows message procedure, the mouse
	// moves report the current cursor position and not deltas, our mouse
	// needs to process those positions as absolute and not relative
	//
	m_inputMovesAbsolute = TRUE;

}  // end int

//-------------------------------------------------------------------------------------------------
/** Reset */
//-------------------------------------------------------------------------------------------------
void Win32Mouse::reset( void )
{

	// extend
	Mouse::reset();

}  // end reset

//-------------------------------------------------------------------------------------------------
/** Update, called once per frame */
//-------------------------------------------------------------------------------------------------
void Win32Mouse::update( void )
{

	// extend 
	Mouse::update();

}  // end update

//-------------------------------------------------------------------------------------------------
/** Add a window message event along with its WPARAM and LPARAM parameters
	* to our input storage buffer */
//-------------------------------------------------------------------------------------------------
void Win32Mouse::addWin32Event( UINT msg, WPARAM wParam, LPARAM lParam, DWORD time )
{

	// Image comparison runs (-shotAfter, -shotEvery, -cameraPath) ignore the real mouse. A cursor over the test
	// window would trigger hover effects (tooltips, highlighted buttons, health bars, edge scrolling) and
	// LookAtTranslator::hasMouseMovedRecently() would block the recorded replay camera (MSG_SET_REPLAY_CAMERA),
	// making the images depend on where the mouse happens to be.
	if( TheGlobalData && !::Engine::acceptsRealMouse( realInputFilter() ) )
		return;

	if( m_dragScroll && !relocateDragEvent( msg, lParam ) )
		return;

	// a move after an unread move with the same buttons replaces it
	const UnsignedInt previous = ( m_nextFreeIndex + Mouse::NUM_MOUSE_EVENTS - 1 ) % Mouse::NUM_MOUSE_EVENTS;
	if( m_eventBuffer[ previous ].msg != 0 &&
			::Engine::replacesPreviousMouseEvent( m_eventBuffer[ previous ].msg, m_eventBuffer[ previous ].wParam, msg, wParam ) )
	{
		m_eventBuffer[ previous ].lParam = lParam;
		m_eventBuffer[ previous ].time = time;
		return;
	}

	//
	// we can only add this event if our next free index does not already
	// have an event in it, if it does ... our buffer is full and this input
	// event will be lost
	//
	if( m_eventBuffer[ m_nextFreeIndex ].msg != 0 )
		return;

	// add to this index
	m_eventBuffer[ m_nextFreeIndex ].msg = msg;
	m_eventBuffer[ m_nextFreeIndex ].wParam = wParam;
	m_eventBuffer[ m_nextFreeIndex ].lParam = lParam;
	m_eventBuffer[ m_nextFreeIndex ].time = time;

	// wrap index at max
	m_nextFreeIndex++;
	if( m_nextFreeIndex >= Mouse::NUM_MOUSE_EVENTS )
		m_nextFreeIndex = 0;

}  // end addWin32Event

// Dragging with the right button. Windows keeps the cursor on the screen, so a drag towards an edge
// used to stop there. Now the cursor is hidden and put back where the drag began after every move;
// the events report a position that keeps going (up to half the window from there).
void Win32Mouse::setDragScroll( Bool on )
{
	if( on == m_dragScroll )
		return;
	if( !on )
	{
		endDrag();
		return;
	}
	if( m_lostFocus || ApplicationHWnd == nullptr || UserSettings::get( "Controls.HideCursorWhileDragging" ).compareNoCase( "no" ) == 0 )
		return;
	POINT cursor;
	if( !GetCursorPos( &cursor ) || !ScreenToClient( ApplicationHWnd, &cursor ) )
		return;
	m_dragAnchor = cursor;
	m_dragPosition = cursor;
	m_dragWasVisible = m_visible;
	Mouse::setDragScroll( TRUE );
	setVisibility( FALSE );
}

void Win32Mouse::endDrag( void )
{
	if( !m_dragScroll )
		return;
	Mouse::setDragScroll( FALSE );
	POINT screen = m_dragAnchor;
	if( ApplicationHWnd != nullptr && ClientToScreen( ApplicationHWnd, &screen ) )
		SetCursorPos( screen.x, screen.y );
	setVisibility( m_dragWasVisible );
}

// False drops the event: the move Windows reports for setting the cursor back.
Bool Win32Mouse::relocateDragEvent( UINT msg, LPARAM &lParam )
{
	switch( msg )
	{
		case WM_MOUSEMOVE:
		{
			const Int dx = GET_X_LPARAM( lParam ) - m_dragAnchor.x;
			const Int dy = GET_Y_LPARAM( lParam ) - m_dragAnchor.y;
			if( dx == 0 && dy == 0 )
				return FALSE;
			RECT client;
			GetClientRect( ApplicationHWnd, &client );
			const Int halfWidth = ( client.right - client.left ) / 2;
			const Int halfHeight = ( client.bottom - client.top ) / 2;
			m_dragPosition.x = std::clamp<Int>( m_dragPosition.x + dx, m_dragAnchor.x - halfWidth, m_dragAnchor.x + halfWidth );
			m_dragPosition.y = std::clamp<Int>( m_dragPosition.y + dy, m_dragAnchor.y - halfHeight, m_dragAnchor.y + halfHeight );
			POINT screen = m_dragAnchor;
			if( ClientToScreen( ApplicationHWnd, &screen ) )
				SetCursorPos( screen.x, screen.y );
			break;
		}
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_LBUTTONDBLCLK:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
		case WM_MBUTTONDBLCLK:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONDBLCLK:
			break;
		case WM_RBUTTONUP:
			lParam = MAKELPARAM( static_cast<Short>( m_dragPosition.x ), static_cast<Short>( m_dragPosition.y ) );
			endDrag();
			return TRUE;
		default:
			return TRUE;	// the wheels report screen coordinates
	}
	lParam = MAKELPARAM( static_cast<Short>( m_dragPosition.x ), static_cast<Short>( m_dragPosition.y ) );
	return TRUE;
}

extern HINSTANCE ApplicationHInstance;

void Win32Mouse::setVisibility(Bool visible)
{
	//Extend
	Mouse::setVisibility(visible);
	//Maybe need to set cursor to force hiding of some cursors.
	Win32Mouse::setCursor(getMouseCursor());
}

// Cursor size: Controls.CursorScale in percent, 0 by the DPI of the window's monitor (the .ani files are
// 32 pixels; up to 150 % scaling they stay at the system size, above they grow with it).
static Int cursorPixels( void )
{
	Int percent = atoi( UserSettings::get( "Controls.CursorScale" ).str() );
	if( percent <= 0 )
	{
		const UnsignedInt dpi = WindowDpi( ApplicationHWnd );
		percent = dpi <= 144 ? 100 : static_cast<Int>( dpi * 100 / 96 );
	}
	return percent == 100 ? 0 : 32 * percent / 100;
}

// Wide file API: a narrow path under the user data folder may be UTF-8 (see Engine/UserDataFiles.h).
static HCURSOR loadCursorFile( const char *path )
{
	static const Int s_pixels = cursorPixels();
	const std::filesystem::path file = ::Engine::pathFromNarrow( path );
	if( s_pixels <= 0 )
		return LoadCursorFromFileW( file.c_str() );
	return static_cast<HCURSOR>( LoadImageW( nullptr, file.c_str(), IMAGE_CURSOR, s_pixels, s_pixels, LR_LOADFROMFILE ) );
}

// LoadCursorFromFile needs a file on disk: a cursor from a mod archive is copied to the user data folder first.
static HCURSOR loadArchivedModCursor(const char *resourcePath)
{
	File *source = TheArchiveFileSystem->openModFile(resourcePath);
	if (source == nullptr)
		return nullptr;
	const char *fileName = strrchr(resourcePath, '\\');
	AsciiString copy;
	copy.format("%sCache\\Cursors\\%s", TheGlobalData->getPath_UserData().str(), fileName ? fileName + 1 : resourcePath);
	File *target = TheLocalFileSystem->openFile(copy.str(), File::WRITE | File::BINARY | File::CREATE | File::TRUNCATE);
	Bool copied = FALSE;
	if (target != nullptr)
	{
		const Int size = source->size();
		std::vector<char> data(size > 0 ? static_cast<std::size_t>(size) : 0);
		copied = size > 0 && source->read(data.data(), size) == size && target->write(data.data(), size) == size;
		target->close();
	}
	source->close();
	return copied ? loadCursorFile(copy.str()) : nullptr;
}

/**Preload all the cursors we may need during the game.  This must be done before the D3D device
is created to avoid cursor corruption on buggy ATI Radeon cards. */
void Win32Mouse::initCursorResources(void)
{
	for (Int cursor=FIRST_CURSOR; cursor<NUM_MOUSE_CURSORS; cursor++)
	{
		for (Int direction=0; direction<m_cursorInfo[cursor].numDirections; direction++)
		{	if (!cursorResources[cursor][direction] && !m_cursorInfo[cursor].textureName.isEmpty())
			{	//this cursor has never been loaded before.
				char resourcePath[256];
				//Check if this is a directional cursor
				if (m_cursorInfo[cursor].numDirections > 1)
					WWFormat::Into(resourcePath, "data\\cursors\\{}{:d}.ANI", m_cursorInfo[cursor].textureName.str(), direction);
				else
					WWFormat::Into(resourcePath, "data\\cursors\\{}.ANI", m_cursorInfo[cursor].textureName.str());
				
				// A mod's cursor comes first, also from its archives.
				Bool loaded = FALSE;
				AsciiString modDiskPath;
				if (TheArchiveFileSystem != nullptr && TheArchiveFileSystem->isModFile(AsciiString(resourcePath), &modDiskPath))
				{
					cursorResources[cursor][direction] = modDiskPath.isNotEmpty() ? loadCursorFile(modDiskPath.str())
						: loadArchivedModCursor(resourcePath);
					loaded = (cursorResources[cursor][direction] != nullptr);
					char line[512];
					WWFormat::Into(line, "MOD zeiger {} {}", resourcePath, loaded ? "geladen" : "nicht ladbar");
					line[sizeof(line) - 1] = 0;
					GameEngine_RunnerLog(line);
				}

				if (!loaded)
					cursorResources[cursor][direction]=loadCursorFile(resourcePath);
				DEBUG_ASSERTCRASH(cursorResources[cursor][direction], ("MissingCursor %s\n",resourcePath));
			}
		}
//		SetCursor(cursorResources[cursor][m_directionFrame]);
	}
}

//-------------------------------------------------------------------------------------------------
/** Super basic simplistic cursor */
//-------------------------------------------------------------------------------------------------
void Win32Mouse::setCursor( MouseCursor cursor )
{
	// extend
	Mouse::setCursor( cursor );

	if (m_lostFocus)
		return;	//stop messing with mouse cursor if we don't have focus.

	if (cursor == NONE || !m_visible || m_dragScroll)
		SetCursor( nullptr );
	else
	{
		SetCursor(cursorResources[cursor][m_directionFrame]);
	}  // end switch

	// save current cursor
	m_currentWin32Cursor=m_currentCursor = cursor;
	
}  // end setCursor

//-------------------------------------------------------------------------------------------------
/** Capture the mouse to our application */
//-------------------------------------------------------------------------------------------------
void Win32Mouse::capture( void )
{

//	SetCapture( ApplicationHWnd );

}  // end capture

//-------------------------------------------------------------------------------------------------
/** Release the mouse capture for our app window */
//-------------------------------------------------------------------------------------------------
void Win32Mouse::releaseCapture( void )
{

//	ReleaseCapture();

}  // end releaseCapture








