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

// FILE: W3DGameEngine.cpp ////////////////////////////////////////////////////////////////////////
// Author: Colin Day, April 2001
// Description:
//   Implementation of the Win32 game engine, this is the highest level of 
//   the game application, it creates all the devices we will use for the game
///////////////////////////////////////////////////////////////////////////////////////////////////

#include <windows.h>
#include "Win32Device/Common/Win32GameEngine.h"
#include "Common/GlobalData.h"
#include "GameClient/Display.h"
#include "Common/PerfTimer.h"

#include "GameNetwork/GameLobbyCallbacks.h"

extern DWORD TheMessageTime;

//-------------------------------------------------------------------------------------------------
/** Constructor for Win32GameEngine */
//-------------------------------------------------------------------------------------------------
Win32GameEngine::Win32GameEngine()
{
	// Stop blue screen
	m_previousErrorMode = SetErrorMode( SEM_FAILCRITICALERRORS );
}

//-------------------------------------------------------------------------------------------------
/** Destructor for Win32GameEngine */
//-------------------------------------------------------------------------------------------------
Win32GameEngine::~Win32GameEngine()
{
	// restore it (this isn't really necessary, but feels good.)
	SetErrorMode( m_previousErrorMode );
}


//-------------------------------------------------------------------------------------------------
/** Initialize the game engine */
//-------------------------------------------------------------------------------------------------
void Win32GameEngine::init( void )
{

	// extending functionality
	GameEngine::init();

}  // end init

//-------------------------------------------------------------------------------------------------
/** Reset the system */
//-------------------------------------------------------------------------------------------------
void Win32GameEngine::reset( void )
{

	// extending functionality
	GameEngine::reset();

}  // end reset

//-------------------------------------------------------------------------------------------------
/** Update the game engine by updating the GameClient and 
	* GameLogic singletons. */
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
// If the window is minimized while the device enlarges it to the display resolution, it keeps its
// initial size and the image is squeezed. In windowed mode the client area is matched to the display
// size whenever the window is not minimized.
//-------------------------------------------------------------------------------------------------
static void fixWindowClientSize( void )
{
	extern HWND ApplicationHWnd;
	if (!ApplicationHWnd || !TheDisplay || !TheGlobalData || !TheGlobalData->m_windowed || ::IsIconic(ApplicationHWnd))
		return;
	const Int width = TheDisplay->getWidth();
	const Int height = TheDisplay->getHeight();
	RECT client;
	if (width <= 0 || height <= 0 || !::GetClientRect(ApplicationHWnd, &client))
		return;
	if (client.right - client.left == width && client.bottom - client.top == height)
		return;
	RECT rect = { 0, 0, width, height };
	::AdjustWindowRect(&rect, static_cast<DWORD>(::GetWindowLongPtr(ApplicationHWnd, GWL_STYLE)), FALSE);
	::SetWindowPos(ApplicationHWnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
		SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void Win32GameEngine::update( void )
{


	// call the engine normal update
	GameEngine::update();

	extern HWND ApplicationHWnd;
	// Test runs (-fixedTimeStep, -runner) must not stall in the wait loop below when minimized;
	// restore the window instead.
	if (ApplicationHWnd && ::IsIconic(ApplicationHWnd) && TheGlobalData
		&& (TheGlobalData->m_fixedTimeStep || TheGlobalData->m_runnerMode))
		::ShowWindow(ApplicationHWnd, SW_RESTORE);
	if (ApplicationHWnd && ::IsIconic(ApplicationHWnd)) {
		while (ApplicationHWnd && ::IsIconic(ApplicationHWnd)) {
			// We are alt-tabbed out here.  Sleep a bit, & process windows
			// so that we can become un-alt-tabbed out.
			Sleep(5);
			serviceWindowsOS();

			if (TheLobby != nullptr) {
				// BGC - need to update TheLobby so we can process and respond to other
				// people's messages who may not be alt-tabbed out like we are.
				TheLobby->setIsActive(isActive());
				TheLobby->update();
			}

			// If we are running a multiplayer game, keep running the logic.
			// There is code in the client to skip client redraw if we are 
			// iconic.  jba.
			if (TheGameEngine->getQuitting() || TheGameLogic->isInInternetGame() || TheGameLogic->isInLanGame()) {
				break; // keep running.
			}
		}
    
    // When we are alt-tabbed out... the audio manager seems to go into a coma sometimes
    // and not regain focus properly when we come back. This seems to wake it up nicely.
    AudioAffect aa = static_cast<AudioAffect>(0x10);
		TheAudio->setVolume(TheAudio->getVolume( aa ), aa );

	}

	fixWindowClientSize();

	// allow windows to perform regular windows maintenance stuff like msgs
	serviceWindowsOS();

}  // end update

//-------------------------------------------------------------------------------------------------
/** This function may be called from within this application to let
  * Microsoft Windows do its message processing and dispatching.  Presumeably
	* we would call this at least once each time around the game loop to keep
	* Windows services from backing up */
//-------------------------------------------------------------------------------------------------
void Win32GameEngine::serviceWindowsOS( void )
{
	MSG msg;
  Int returnValue;

	//
	// see if we have any messages to process, a NULL window handle tells the
	// OS to look at the main window associated with the calling thread, us!
	//
	// wide functions: the game window is a Unicode window
	while( PeekMessageW( &msg, nullptr, 0, 0, PM_NOREMOVE ) )
	{

		// get the message
		returnValue = GetMessageW( &msg, nullptr, 0, 0 );

		// this is one possible way to check for quitting conditions as a message
		// of WM_QUIT will cause GetMessage() to return 0
/*
		if( returnValue == 0 )
		{

			setQuitting( true );
			break;

		}
*/

		TheMessageTime = msg.time;
		// translate and dispatch the message
		TranslateMessage( &msg );
		DispatchMessageW( &msg );
		TheMessageTime = 0;
			
	}  // end while

}  // end ServiceWindowsOS

