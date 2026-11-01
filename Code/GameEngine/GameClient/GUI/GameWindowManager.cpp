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

// FILE: GameWindowManager.cpp ////////////////////////////////////////////////////////////////////
// Created:   Colin Day, June 2001
//						Dean Iverson, March 1998 (Original window code)
// Desc:      The game window manager is the singleton class that we interface
//						with to interact with the game windowing system.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/Debug.h"
#include "Common/Language.h"
#include "GameClient/Display.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GameWindow.h"
#include "GameClient/Mouse.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/GameWindowGlobal.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetTabControl.h"
#include "GameClient/GadgetProgressBar.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetSlider.h"
#include "GameClient/GadgetRadioButton.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/GameWindowTransitions.h"
#include "Common/NameKeyGenerator.h"

// PUBLIC DATA ////////////////////////////////////////////////////////////////////////////////////
UnsignedInt WindowLayoutCurrentVersion = 2;

///////////////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//
// with this statis set to true, the window system will propogate mouse position
// messages to windows.  You may want to disable this if you feel the mouse position
// messages are "spamming" your window and making a particular debuggin situation
// difficult.  Make sure you do enable this before you check in again tho because
// it is necessary for any code that needs to look at objects or anything under
// the radar cursor
//
static Bool sendMousePosMessages = TRUE;

//-------------------------------------------------------------------------------------------------
/** Process windows waiting to be destroyed */
//-------------------------------------------------------------------------------------------------
void GameWindowManager::processDestroyList( void )
{
	GameWindow *next;
	GameWindow *doDestroy;

	//
	// we need to pass the ownership of the destroy list so
	// if, while destroying a window, we need to add other windows
	// to the destroy list it won't cause problems.
	//
	doDestroy = m_destroyList;

	// set the list to empty
	m_destroyList = nullptr;

	// do the destroys
	for( ; doDestroy; doDestroy = next )
	{

		next = doDestroy->m_next;

		// Check to see if this window is "special"
		if( m_mouseCaptor == doDestroy )
			winRelease( doDestroy );

		if( m_keyboardFocus == doDestroy )
			winSetFocus( nullptr );
		if( m_focusLostByHiding == doDestroy )
			m_focusLostByHiding = nullptr;

		if( (m_modalHead != nullptr) && (doDestroy == m_modalHead->window) )
			winUnsetModal( m_modalHead->window );

		if( m_currMouseRgn == doDestroy )
			m_currMouseRgn = nullptr;

		if( m_grabWindow == doDestroy )
			m_grabWindow = nullptr;

		// send the destroy message to the window we're about to kill
		winSendSystemMsg( doDestroy, GWM_DESTROY, 0, 0 );

		// free the memory
		if (doDestroy)
			deleteInstanceOf(doDestroy);

	}  // end for

}  // end processDestroyList

///////////////////////////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
/** Generic function to simply propagate only button press messages to parent and let it deal with it */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType PassSelectedButtonsToParentSystem( GameWindow *window, UnsignedInt msg,
																												WindowMsgData mData1, WindowMsgData mData2 )
{

	// sanity
	if( window == nullptr )
		return WindowMsgHandledType::MSG_IGNORED;

	// this callback replaces the gadget's own system function, so the push button data is released here
	if( msg == GWM_DESTROY && BitTest( window->winGetStyle(), GWS_PUSH_BUTTON ) )
		return GadgetPushButtonSystem( window, msg, mData1, mData2 );

	if( (msg == GBM_SELECTED)  ||  (msg == GBM_SELECTED_RIGHT) || (msg == GBM_MOUSE_ENTERING) || (msg == GBM_MOUSE_LEAVING) || (msg == GEM_EDIT_DONE))
	{
		GameWindow *parent = window->winGetParent();

		if( parent )
			return TheWindowManager->winSendSystemMsg( parent, msg, mData1, mData2 );

	}  // end if
	
	return WindowMsgHandledType::MSG_IGNORED;

}  // end PassSelectedButtonsToParentSystem

//-------------------------------------------------------------------------------------------------
/** Generic function to simply propagate only button press messages to parent and let it deal with it */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType PassMessagesToParentSystem( GameWindow *window, UnsignedInt msg,
																												WindowMsgData mData1, WindowMsgData mData2 )
{

	// sanity
	if( window == nullptr )
		return WindowMsgHandledType::MSG_IGNORED;


	GameWindow *parent = window->winGetParent();

	if( parent )
		return TheWindowManager->winSendSystemMsg( parent, msg, mData1, mData2 );
	
	return WindowMsgHandledType::MSG_IGNORED;

}  // end PassSelectedButtonsToParentSystem


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
GameWindowManager::GameWindowManager( void )
{

	m_windowList = nullptr;			// list of all top level windows
	m_windowTail = nullptr;			// last in windowList

	m_destroyList = nullptr;			// list of windows to destroy

	m_currMouseRgn = nullptr;		// window that mouse is over
	m_mouseCaptor = nullptr;			// window that captured mouse
	m_keyboardFocus = nullptr;		// window that has input focus
	m_focusLostByHiding = nullptr;
	m_modalHead = nullptr;			// top of windows in the modal stack
	m_grabWindow = nullptr;			// window that grabbed the last down event
	m_loneWindow = nullptr;		// Set if we just opened a combo box

	m_cursorBitmap = nullptr;
	m_captureFlags = 0;

}  // end GameWindowManger

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
GameWindowManager::~GameWindowManager( void )
{

	// destroy all windows
	winDestroyAll();
	freeStaticStrings();
	if(TheTransitionHandler)
		delete TheTransitionHandler;
	::Engine::context().transitionHandler = nullptr;
}  // end ~GameWindowManager

//-------------------------------------------------------------------------------------------------
/** Initialize the game window manager system */
//-------------------------------------------------------------------------------------------------
void GameWindowManager::init( void )
{
	if(!TheTransitionHandler)
		::Engine::context().transitionHandler = NEW GameWindowTransitionsHandler;
	TheTransitionHandler->load();
	TheTransitionHandler->init();
}  // end init

//-------------------------------------------------------------------------------------------------
/** Reset window system */
//-------------------------------------------------------------------------------------------------
void GameWindowManager::reset( void )
{

	// destroy all windows left
	winDestroyAll();
	if(TheTransitionHandler)
		TheTransitionHandler->reset();

}  // end reset

//-------------------------------------------------------------------------------------------------
/** Update cycle for game widnow manager */
//-------------------------------------------------------------------------------------------------
void GameWindowManager::update( void )
{

	// Process windows waiting to be destroyed
	processDestroyList();
	if(TheTransitionHandler)
		TheTransitionHandler->update();
}  // end update

//-------------------------------------------------------------------------------------------------
/** Puts a window at the head of the window list */
//-------------------------------------------------------------------------------------------------
void GameWindowManager::linkWindow( GameWindow *window )
{
	GameWindow *lastModalWindow = nullptr;
	GameWindow *tmp = m_windowList;
	while (tmp)
	{
		const ModalWindow *modal = m_modalHead;
		while (modal)
		{
			if (modal->window == tmp && modal->window != window)
			{
				lastModalWindow = tmp;
			}
			modal = modal->next;
		}
		tmp = tmp->m_next;
	}

	if (!lastModalWindow)
	{

		// Add to head of the top level window list
		window->m_prev = nullptr;
		window->m_next = m_windowList;

		if( m_windowList )
			m_windowList->m_prev = window;
		else 
		{
			// first on list is also the tail
			m_windowTail = window;
		}

		m_windowList = window;
	}
	else
	{
		// lastModalWindow points to a modal window - add behind it
		window->m_prev = lastModalWindow;
		window->m_next = lastModalWindow->m_next;
		lastModalWindow->m_next = window;
		if (window->m_next)
		{
			window->m_next->m_prev = window;
		}
	}

}  // end linkWindow

//-------------------------------------------------------------------------------------------------
/** Insert the window ahead of the the 'aheadOf' window.  'aheadOf' can
	* be a window in the master list or a child of any window in that master
	* list */
//-------------------------------------------------------------------------------------------------
void GameWindowManager::insertWindowAheadOf( GameWindow *window, 
																						 GameWindow *aheadOf )
{
	
	// sanity
	if( window == nullptr )
		return;

	// we'll say that an aheadOf window means at the head of the list
	if( aheadOf == nullptr )	
	{

		linkWindow( window );
		return;

	}  // end if

	// get parent of aheadOf
	GameWindow *aheadOfParent = aheadOf->winGetParent();

	//
	// if ahead of has no parent insert it in the master list just before
	// ahead of
	//
	if( aheadOfParent == nullptr )
	{

		window->m_prev = aheadOf->m_prev;

		if( aheadOf->m_prev )
			aheadOf->m_prev->m_next = window;
		else
			m_windowList = window;

		aheadOf->m_prev = window;
		window->m_next = aheadOf;

	}  // end if
	else
	{

		window->m_prev = aheadOf->m_prev;

		if( aheadOf->m_prev )
			aheadOf->m_prev->m_next = window;
		else
			aheadOfParent->m_child = window;

		aheadOf->m_prev = window;
		window->m_next = aheadOf;

		window->m_parent = aheadOfParent;

	}  // end else

}  // end insertWindowAheadOf

//-------------------------------------------------------------------------------------------------
/** Takes a window off the window list */
//-------------------------------------------------------------------------------------------------
void GameWindowManager::unlinkWindow( GameWindow *window )
{

	if( window->m_next )
		window->m_next->m_prev = window->m_prev;
	else 
	{
		// no next means this is the tail
		m_windowTail = window->m_prev;
	}

	if( window->m_prev )
		window->m_prev->m_next = window->m_next;
	else
		m_windowList = window->m_next;

}  // end unlinkWindow

//-------------------------------------------------------------------------------------------------
/** Takes a child window off its parent's window list */
//-------------------------------------------------------------------------------------------------
void GameWindowManager::unlinkChildWindow( GameWindow *window )
{

	if( window->m_prev ) 
	{

		window->m_prev->m_next = window->m_next;

		if( window->m_next )
			window->m_next->m_prev = window->m_prev;

	} 
	else 
	{

		if( window->m_next ) 
		{

			window->m_parent->m_child = window->m_next;

			window->m_next->m_prev = window->m_prev;

			window->m_next = nullptr;

		} 
		else 
		{

			window->m_parent->m_child = nullptr;

		}

	}  // end else

	// remove the parent reference from this window
	window->m_parent = nullptr;

}  // end unlinkChildWindow

//-------------------------------------------------------------------------------------------------
/** Check window and parents to see if this window is enabled */
//-------------------------------------------------------------------------------------------------
Bool GameWindowManager::isEnabled( GameWindow *win )
{

	// sanity
	if( win == nullptr )
		return FALSE;
	
	if( BitTest( win->m_status, WIN_STATUS_ENABLED ) == FALSE )
	{
		return FALSE;
	}

	while( win->m_parent )
	{
		win = win->m_parent;
		if( BitTest( win->m_status, WIN_STATUS_ENABLED ) == FALSE )
		{
			return FALSE;
		}
	}

	return TRUE;

}  // end isEnabled

//-------------------------------------------------------------------------------------------------
/** Check window and parents to see if this window is hidden */
//-------------------------------------------------------------------------------------------------
Bool GameWindowManager::isHidden( GameWindow *win )
{

	// we'll allow for the idea that if a window doesn't exist it is hidden
	if( win == nullptr )
		return TRUE;
	
	if( BitTest( win->m_status, WIN_STATUS_HIDDEN ))
	{
		return TRUE;
	}

	while( win->m_parent )
	{
		win = win->m_parent;
		if( BitTest( win->m_status, WIN_STATUS_HIDDEN ))
		{
			return TRUE;
		}
	}

	return FALSE;

}  // end isHidden

//-------------------------------------------------------------------------------------------------
// Adds a child window to its parent.
//-------------------------------------------------------------------------------------------------
void GameWindowManager::addWindowToParent( GameWindow *window, 
																					 GameWindow *parent )
{
	if( parent ) 
	{

		// add to parent's list of children
		window->m_prev = nullptr;
		window->m_next = parent->m_child;

		if( parent->m_child )
			parent->m_child->m_prev = window;

		parent->m_child = window;

		window->m_parent = parent;

	}

}  // end addWindowToParent

//-------------------------------------------------------------------------------------------------
/** Add a child window to the parent, put place it at the end of the 
	* parent window child list */
//-------------------------------------------------------------------------------------------------
void GameWindowManager::addWindowToParentAtEnd( GameWindow *window,	
																								GameWindow *parent )
{

	if( parent )
	{

		window->m_prev = nullptr;
		window->m_next = nullptr;
		if( parent->m_child )
		{
			GameWindow *last;

			// wind down to last child in list
			last = parent->m_child;
			while( last->m_next != nullptr )
				last = last->m_next;

			// tie to list
			last->m_next = window;
			window->m_prev = last;

		}  // end if
		else
			parent->m_child = window;

		// assign the parent to the window
		window->m_parent = parent;

	}  // end if

}  // end addWindowToParentAtEnd

//-------------------------------------------------------------------------------------------------
/** this gets called from winHide() when a window hides itself */
//-------------------------------------------------------------------------------------------------
void GameWindowManager::windowHiding( GameWindow *window )
{

	// if this window has keyboard focus remove it; remembered so it comes back when the window shows
	// again (menu transitions hide windows for a few frames)
	if( m_keyboardFocus == window )
	{
		m_keyboardFocus = nullptr;
		m_focusLostByHiding = window;
	}

	// if this is the modal head, unset it
	if( m_modalHead && m_modalHead->window == window )
		winUnsetModal( window );

	// if this is the captor, it shall no longer be
	if( m_mouseCaptor == window )
		winCapture( nullptr );

	//
	// since hiding a parent will also hide the children, when a parent
	// hides we must call this same method for all the children so they
	// each have a chance to go through this logic
	//
	GameWindow *child;
	for( child = window->winGetChild(); child; child = child->winGetNext() )
		windowHiding( child );

}  // end windowHiding

//-------------------------------------------------------------------------------------------------
/** this gets called from winHide() when a window shows itself again: give the focus back to the
	* window that lost it by hiding, when nothing else took it meanwhile and it is visible again */
//-------------------------------------------------------------------------------------------------
void GameWindowManager::windowShowing( GameWindow *window )
{
	GameWindow *remembered = m_focusLostByHiding;
	if( remembered == nullptr || m_keyboardFocus != nullptr )
		return;

	Bool inside = FALSE;
	for( GameWindow *w = remembered; w; w = w->winGetParent() )
	{
		if( w->winIsHidden() )
			return;
		if( w == window )
			inside = TRUE;
	}

	if( inside )
		winSetFocus( remembered );

}  // end windowShowing

//-------------------------------------------------------------------------------------------------
/** Hide all windows in a certain range of id's (inclusive) */
//-------------------------------------------------------------------------------------------------
void GameWindowManager::hideWindowsInRange( GameWindow *baseWindow, 
																						Int first, Int last, 
																						Bool hideFlag )
{
	Int i;
	GameWindow *window;

	for( i = first; i <= last; i++ ) 
	{

		window = winGetWindowFromId( baseWindow, i );
		if( window )
			window->winHide( hideFlag );

	}  // end for i

}  // end hideWindowsInRange

//-------------------------------------------------------------------------------------------------
// Enable all windows in a certain range of id's (inclusive)
//-------------------------------------------------------------------------------------------------
void GameWindowManager::enableWindowsInRange( GameWindow *baseWindow, 
																							Int first, Int last, 
																							Bool enableFlag )
{
	Int i;
	GameWindow *window;

	for( i =first; i <= last; i++ ) 
	{

		window = winGetWindowFromId( baseWindow, i );
		if( window )
			window->winEnable( enableFlag );

	}  // end for i

}  // end enableWindowsInRange

//-------------------------------------------------------------------------------------------------
/** Captures the mouse capture. */
//-------------------------------------------------------------------------------------------------
Int GameWindowManager::winCapture( GameWindow *window )
{

	if( m_mouseCaptor != nullptr)
		return WIN_ERR_MOUSE_CAPTURED;

	m_mouseCaptor = window;

	return WIN_ERR_OK;

}  // end WinCapture

//-------------------------------------------------------------------------------------------------
/** Releases the mouse capture. */
//-------------------------------------------------------------------------------------------------
Int GameWindowManager::winRelease( GameWindow *window )
{

	if( window == m_mouseCaptor )
		m_mouseCaptor = nullptr;

	return WIN_ERR_OK;

}  // end WinRelease

//-------------------------------------------------------------------------------------------------
/** Returns the current mouse captor. */
//-------------------------------------------------------------------------------------------------
GameWindow *GameWindowManager::winGetCapture( void )
{

	return m_mouseCaptor;

}  // end WinGetCapture

//-------------------------------------------------------------------------------------------------
/** Gets the window pointer from its id */
//-------------------------------------------------------------------------------------------------
GameWindow *GameWindowManager::winGetWindowFromId( GameWindow *window, Int id )
{

	if( window == nullptr )
		window = m_windowList;

	for( ; window; window = window->m_next ) 
	{

		if( window->winGetWindowId() == id)
			return window;
		else if( window->m_child ) 
		{
			GameWindow *child = winGetWindowFromId( window->m_child, id );

			if( child )
				return child;

		}  // end else if

	}  // end for

	return nullptr;

}  // end WinGetWindowFromId

//-------------------------------------------------------------------------------------------------
/** Gets the Window List Pointer */
//-------------------------------------------------------------------------------------------------
GameWindow *GameWindowManager::winGetWindowList( void )
{

	return m_windowList;

}  // end winGetWindowList

//-------------------------------------------------------------------------------------------------
/** Send a system message to the specified window */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType GameWindowManager::winSendSystemMsg( GameWindow *window, 
																					UnsignedInt msg,
																					WindowMsgData mData1, 
																					WindowMsgData mData2 )
{

	if( window == nullptr)
		return WindowMsgHandledType::MSG_IGNORED;

	if( msg != GWM_DESTROY && BitTest( window->m_status, WIN_STATUS_DESTROYED ) )
		return WindowMsgHandledType::MSG_IGNORED;

	return window->m_system( window, msg, mData1, mData2 );

}  // end winSendSystemMsg

//-------------------------------------------------------------------------------------------------
/** Send a system message to the specified window */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType GameWindowManager::winSendInputMsg( GameWindow *window, 
																				 UnsignedInt msg,
																				 WindowMsgData mData1, 
																				 WindowMsgData mData2 )
{

	if( window == nullptr )
		return WindowMsgHandledType::MSG_IGNORED;

	if( msg != GWM_DESTROY && BitTest( window->m_status, WIN_STATUS_DESTROYED ) )
		return WindowMsgHandledType::MSG_IGNORED;

	return window->m_input( window, msg, mData1, mData2 );

}  // end winSendInputMsg

//-------------------------------------------------------------------------------------------------
/** Get the current input focus */
//-------------------------------------------------------------------------------------------------
GameWindow *GameWindowManager::winGetFocus( void )
{

	return m_keyboardFocus;

}  // end WinGetFocus

//-------------------------------------------------------------------------------------------------
/** Set the current input focus */
//-------------------------------------------------------------------------------------------------
Int GameWindowManager::winSetFocus( GameWindow *window )
{
	Bool wantsFocus = FALSE;

	// an explicit choice replaces a focus lost by hiding
	m_focusLostByHiding = nullptr;

	// if a window doesn't want keyboard focus don't give it
	if( window )
		if( BitTest( window->winGetStatus(), WIN_STATUS_NO_FOCUS) )
			return 0;

	//
	// Tell current focus window that it's losing focus
	// unless we are trying to give focus to the current focus window
	//
	if( (m_keyboardFocus) && (m_keyboardFocus != window) )
	{
		Bool wf;	// dummy var, ignored, but must be passed
		winSendSystemMsg( m_keyboardFocus, GWM_INPUT_FOCUS, FALSE, reinterpret_cast<WindowMsgData>(&wf) );
	}

	// Set focus to new window
	m_keyboardFocus = window;

	// Tell new focus window that it has focus
	if( m_keyboardFocus ) 
	{

		for (;;)
		{
			winSendSystemMsg( window, GWM_INPUT_FOCUS, TRUE, reinterpret_cast<WindowMsgData>(&wantsFocus) );
			if (wantsFocus)
				break;

			window = window->winGetParent();
			if( window == nullptr )
				break;
		}

	}  // end if

	// If new window doesn't want focus, set focus to NULL
	if( wantsFocus == FALSE )
		m_keyboardFocus = nullptr;

	return WIN_ERR_OK;

}  // end WinSetFocus

//-------------------------------------------------------------------------------------------------
/** Process key press through the GUI. */
//-------------------------------------------------------------------------------------------------
WinInputReturnCode GameWindowManager::winProcessKey( UnsignedByte key, 
																										 UnsignedByte state )
{
	WinInputReturnCode returnCode = WinInputReturnCode::WIN_INPUT_NOT_USED;

	// Check for keyboard focus and a legal key for sanity
	if( m_keyboardFocus && (key != KEY_NONE) )
	{
		GameWindow *win = m_keyboardFocus;
			
		returnCode = WinInputReturnCode::WIN_INPUT_USED;  // assume input will be used

		//
		// Pass the keystroke up the window hierarchy until it is
		// processed or we reach the top level window
		//
		while( winSendInputMsg( win, GWM_CHAR, key, state ) == WindowMsgHandledType::MSG_IGNORED )
		{

			win = win->winGetParent();
			if( win == nullptr )
			{

				returnCode = WinInputReturnCode::WIN_INPUT_NOT_USED;  // oops, it wasn't used after all
				break;

			}  // end if

		}  // end while

	}  // end if

	return returnCode;

}  // end winProcessKey

//-------------------------------------------------------------------------------------------------
/** Process a single mouse event through the window system */
//-------------------------------------------------------------------------------------------------
WinInputReturnCode GameWindowManager::winProcessMouseEvent( GameWindowMessage msg,
																														ICoord2D *mousePos,
																														void *data )
{
	WinInputReturnCode returnCode = WinInputReturnCode::WIN_INPUT_NOT_USED;
	Bool objectTooltip = FALSE;
	UnsignedInt packedMouseCoords;
	GameWindow *window = nullptr;
	GameWindow *toolTipWindow = nullptr;
	GameWindow *childWindow;
	Int dx, dy;
	Bool clearGrabWindow = FALSE;

	// pack mouse coords into one entity for message passing
	packedMouseCoords = SHORTTOLONG( mousePos->x, mousePos->y );

	// clear tooltip ... it will be reset if necessary
	TheMouse->setCursorTooltip( UnicodeString::TheEmptyString );

	// Check for mouse capture
	if( m_mouseCaptor )
	{

		// no window grabbed as of yet
		m_grabWindow = nullptr;

		// what what window within the captured window are we in
		window = m_mouseCaptor->winPointInChild( mousePos->x, mousePos->y );

		//
		// send buttons, drags, wheels to the windows, we don't continually
		// send mouse positions
		//
		if( sendMousePosMessages == TRUE || msg != GWM_MOUSE_POS )
		{
			GameWindow *win = window;

			if( win )
			{
				while( win != nullptr )
				{

					if( winSendInputMsg( win, msg, packedMouseCoords, 0 ) == WindowMsgHandledType::MSG_HANDLED )
					{

						// if used clear the event
						returnCode = WinInputReturnCode::WIN_INPUT_USED;
						break;

					}

					// if we just tested mouseCaptor don't go any higher in the chain
					if( win == m_mouseCaptor )
						break;

					win = win->winGetParent();

				}  // end while

			}  // end if
			else
			{

				// if used clear the event
				if(	winSendInputMsg( m_mouseCaptor, msg, packedMouseCoords, 0 ) == WindowMsgHandledType::MSG_HANDLED )
					returnCode = WinInputReturnCode::WIN_INPUT_USED;

			}  // end else

		}  // end if

	}  // end if, mouse captor window present
	else
	{

		if( m_grabWindow )
		{
			GameWindow *parent;

			switch( msg )
			{

				// --------------------------------------------------------------------
				case GWM_LEFT_UP:
				{
					//Play a beep sound if the window is disabled.
					m_grabWindow->winPointInChild( mousePos->x, mousePos->y, FALSE, TRUE );

					BitClear( m_grabWindow->m_status, WIN_STATUS_ACTIVE );
					if( m_grabWindow->winPointInWindow( mousePos->x, mousePos->y ) )
						winSendInputMsg( m_grabWindow, GWM_LEFT_UP, packedMouseCoords, 0 );
					else if( BitTest( m_grabWindow->m_status, WIN_STATUS_DRAGABLE ))
					{
						winSendInputMsg( m_grabWindow, GWM_LEFT_UP, packedMouseCoords, 0 );
					}

					clearGrabWindow = TRUE;
					break;

				}  // end left up

				// --------------------------------------------------------------------
				case MOUSE_EVENT_NONE:
				case GWM_LEFT_DRAG:
				{

					if( BitTest( m_grabWindow->m_status, WIN_STATUS_DRAGABLE ) )
					{
						ICoord2D *mouseDelta = static_cast<ICoord2D *>(data);
						dx = mouseDelta->x;
						dy = mouseDelta->y;

						// Clip window to parent
						if( m_grabWindow->winGetParent() )
						{

							parent = m_grabWindow->winGetParent();

							if( m_grabWindow->m_region.lo.x + dx < 0 )
								dx = 0 - m_grabWindow->m_region.lo.x;
							else if( m_grabWindow->m_region.hi.x + dx > parent->m_size.x )
								dx = parent->m_size.x - m_grabWindow->m_region.hi.x;

							if( m_grabWindow->m_region.lo.y + dy < 0 )
								dy = 0 - m_grabWindow->m_region.lo.y;
							else if( m_grabWindow->m_region.hi.y + dy > parent->m_size.y )
								dy = parent->m_size.y - m_grabWindow->m_region.hi.y;
						}

						// Move the window, but keep it completely visible within screen boundaries
						IRegion2D newRegion;
						ICoord2D grabSize;

						m_grabWindow->winGetPosition( &newRegion.lo.x, &newRegion.lo.y );
						m_grabWindow->winGetSize( &grabSize.x, &grabSize.y );

						newRegion.lo.x += dx;
						newRegion.lo.y += dy;
						if( newRegion.lo.x < 0 )
							newRegion.lo.x = 0;
						if( newRegion.lo.y < 0 )
							newRegion.lo.y = 0;
						
						newRegion.hi.x = newRegion.lo.x + grabSize.x;
						newRegion.hi.y = newRegion.lo.y + grabSize.y;
						if( newRegion.hi.x > static_cast<Int>(TheDisplay->getWidth()) )
							newRegion.hi.x = static_cast<Int>(TheDisplay->getWidth());
						if( newRegion.hi.y > static_cast<Int>(TheDisplay->getHeight()) )
							newRegion.hi.y = static_cast<Int>(TheDisplay->getHeight());
						
						newRegion.lo.x = newRegion.hi.x - grabSize.x;
						newRegion.lo.y = newRegion.hi.y - grabSize.y;

						m_grabWindow->winSetPosition( newRegion.lo.x, newRegion.lo.y );

					}  // end if, draggable window

					// Send mouse drag message
					winSendInputMsg( m_grabWindow, msg, packedMouseCoords, 0 );
					break;

				}  // end mouse event none or left drag

			}  // end switch

			// mark event handled
			returnCode = WinInputReturnCode::WIN_INPUT_USED;

		}  // end if, m_grabWindow
		else
		{

			if( m_modalHead && m_modalHead->window )
			{
				window = m_modalHead->window->winPointInChild( mousePos->x, mousePos->y );
			}
			else
			{
			
				/**@todo Colin, there are 3 cases here that are nearly identical code,
				break them up into functions with parameters */

				// search for top-level window which contains pointer
				for( window = m_windowList; window; window = window->m_next )
				{

					if( BitTest( window->m_status, WIN_STATUS_ABOVE ) &&
							!BitTest( window->m_status, WIN_STATUS_HIDDEN ) &&
							mousePos->x >= window->m_region.lo.x &&
							mousePos->x <= window->m_region.hi.x &&
							mousePos->y >= window->m_region.lo.y &&
							mousePos->y <= window->m_region.hi.y)
					{

						childWindow = window->winPointInAnyChild( mousePos->x, mousePos->y, TRUE, TRUE );
						if( toolTipWindow == nullptr )
						{
							if( childWindow->m_tooltip || 
									childWindow->m_instData.getTooltipTextLength() )
							{
								toolTipWindow = childWindow;
							}
						}
						if( BitTest( window->m_status, WIN_STATUS_ENABLED ) )
						{
							// determine which child window the mouse is in
							window = window->winPointInChild( mousePos->x, mousePos->y );
							break;  // exit for
						}
						
					}  // end if

				}  // end for window

				// check !above, below and hidden
				if( window == nullptr )
				{

					for( window = m_windowList; window; window = window->m_next )
					{

						if( !BitTest( window->m_status, WIN_STATUS_ABOVE | 
																						WIN_STATUS_BELOW | 
																						WIN_STATUS_HIDDEN ) &&
								mousePos->x >= window->m_region.lo.x &&
								mousePos->x <= window->m_region.hi.x &&
								mousePos->y >= window->m_region.lo.y &&
								mousePos->y <= window->m_region.hi.y)
						{

							childWindow = window->winPointInAnyChild( mousePos->x, mousePos->y, TRUE, TRUE );
							if( toolTipWindow == nullptr )
							{
								if( childWindow->m_tooltip || 
										childWindow->m_instData.getTooltipTextLength() )
								{
									toolTipWindow = childWindow;
								}
							}
							if( BitTest( window->m_status, WIN_STATUS_ENABLED ))
							{								
								// determine which child window the mouse is in
								window = window->winPointInChild( mousePos->x, mousePos->y );
								break;  // exit for
							}
						}
					}
				}  // end if, window == NULL

				// check below and !hidden
				if( window == nullptr )
				{

					for( window = m_windowList; window; window = window->m_next )
					{

						if( BitTest( window->m_status, WIN_STATUS_BELOW ) &&
								!BitTest( window->m_status, WIN_STATUS_HIDDEN ) &&
								mousePos->x >= window->m_region.lo.x &&
								mousePos->x <= window->m_region.hi.x &&
								mousePos->y >= window->m_region.lo.y &&
								mousePos->y <= window->m_region.hi.y)
						{

							childWindow = window->winPointInAnyChild( mousePos->x, mousePos->y, TRUE, TRUE );
							if( toolTipWindow == nullptr )
							{
								if( childWindow->m_tooltip || 
										childWindow->m_instData.getTooltipTextLength() )
								{
									toolTipWindow = childWindow;
								}
							}
							if( BitTest( window->m_status, WIN_STATUS_ENABLED ))
							{
								// determine which child window the mouse is in
								window = window->winPointInChild( mousePos->x, mousePos->y );
								break;  // exit for
							}
						}
					}
				}  // end if

			}  // end else, no modal head

			if( window )
				if( BitTest( window->m_status, WIN_STATUS_NO_INPUT ) )
				{
					if(window->winGetParent() && BitTest( window->winGetParent()->winGetInstanceData()->getStyle(), GWS_COMBO_BOX ))
						window = window->winGetParent();
					else
						window = nullptr;
				}

			if( window )
			{
				GameWindow *tempWin;

				//
				// only send messages for button states, wheel states, we do not
				// continually send messages for mouse positions
				//
				if( sendMousePosMessages == TRUE || msg != GWM_MOUSE_POS )
				{

					tempWin = window;
	
					// Give everyone a chance to do something with the clicks
					GameWindow *oldLoneWindow = m_loneWindow;
					while( winSendInputMsg( tempWin, msg, packedMouseCoords, 0 ) == WindowMsgHandledType::MSG_IGNORED )
					{

						tempWin = tempWin->m_parent;
						if( tempWin == nullptr )
							break;

					}  // end while

						
					// First check to see if m_loneWindow is set if so, close the window
					if( m_loneWindow && m_loneWindow == oldLoneWindow 
					&&( msg == GWM_LEFT_UP || msg == GWM_MIDDLE_UP || msg == GWM_RIGHT_UP || tempWin))
					{
						if(!m_loneWindow->winIsChild(tempWin))
							winSetLoneWindow( nullptr );
						/*
								ComboBoxData *cData = (ComboBoxData *)m_comboBoxOpen->winGetUserData();
															// verify that the window that ate the message wasn't one of our own
															if(cData->dropDownButton != tempWin &&
																cData->editBox != tempWin &&
																cData->listBox != tempWin &&
																cData->listboxData->upButton != tempWin &&
																cData->listboxData->downButton != tempWin &&
																cData->listboxData->slider != tempWin &&
																cData->listboxData->slider != tempWin->winGetParent())
																	winSetOpenComboBoxWindow( NULL );*/
								
					}
					if( tempWin )
					{
					
						//
						// Someone cares, if this is a left button down event
						// it should get "grabbed"
						/// @todo should allow for left handed mouse configs here?
						//
						if( msg == GWM_LEFT_DOWN )
						{

//						if( tempWin != windowList ) 
//							WinActivate( tempWin );
							m_grabWindow = tempWin;

						}  // end if

						// event is used
						returnCode = WinInputReturnCode::WIN_INPUT_USED;

					}  // end if, tempWin

				}  // end if

			}  // end if( window ) 

			if( toolTipWindow == nullptr )
			{

				if( isHidden( window ) == FALSE )
					toolTipWindow = window;

			}  // end if

			// if tooltips are on set them into the window
			Bool tooltipsOn = TRUE;
			if( tooltipsOn )
			{
//				if( toolTipWindow && toolTipWindow->winGetParent() && BitTest( toolTipWindow->winGetParent()->winGetInstanceData()->getStyle(), GWS_COMBO_BOX ))
//					toolTipWindow = toolTipWindow->winGetParent();
				if( toolTipWindow )
				{
					// do we have a callback to call for the tooltip
					if( toolTipWindow->m_tooltip )
						toolTipWindow->m_tooltip( toolTipWindow, 
																			&toolTipWindow->m_instData, 
																			packedMouseCoords );

					// else, do we have a normal tooltip to set
					else if( toolTipWindow->m_instData.getTooltipTextLength() )
						TheMouse->setCursorTooltip( toolTipWindow->m_instData.getTooltipText(), toolTipWindow->m_instData.m_tooltipDelay );

				}  // end if
				else
				{

					//
					// not pointing at a window... perhaps we are pointing at a valid
					// tooltip-able object in the game world ... let's set a flag so 
					// during the object testing we can set the tooltip, we can do
					// whatever we like now that we know no other tooltip was set from
					// a window
					//
					objectTooltip = TRUE;

				}  // end else

			}  // end if

		}  // end if grabWindow not present

	}  // end else (mouseCaptor) 

	//
	// check if new current window is different from the last
	// but only if both windows fall within the mouseCaptor if one exists
	//
	if( (m_grabWindow == nullptr) && (window != m_currMouseRgn) )
	{
		if( m_mouseCaptor )
		{
			if( m_mouseCaptor->winIsChild( m_currMouseRgn ) )
				winSendInputMsg( m_currMouseRgn, GWM_MOUSE_LEAVING, packedMouseCoords, 0 );
		}
		else if( m_currMouseRgn )
			winSendInputMsg( m_currMouseRgn, GWM_MOUSE_LEAVING, packedMouseCoords, 0 );

		if( window )
			winSendInputMsg( window, GWM_MOUSE_ENTERING, packedMouseCoords, 0 );

		m_currMouseRgn = window;

	}  // end if

	// clear grabWindow if necessary
	if( clearGrabWindow == TRUE )
	{

		m_grabWindow = nullptr;
		clearGrabWindow = FALSE;

	}  // end if

	return returnCode;

}  // end winProcessMouseEvent

//-------------------------------------------------------------------------------------------------
/** Draw a window and its children, in parent-first order.
	* Children's coordinates are relative to their parents.
	* Note that hidden windows automatically will not draw any
	* of their children ... but see-thru windows only will not 
	* draw themselves, but will give their children an
	* opportunity to draw */
//-------------------------------------------------------------------------------------------------
Int GameWindowManager::drawWindow( GameWindow *window )
{
	GameWindow *child;

	if( window == nullptr )
		return WIN_ERR_INVALID_WINDOW;

	if( BitTest( window->m_status, WIN_STATUS_HIDDEN ) == FALSE )
	{

		if( !BitTest( window->m_status, WIN_STATUS_SEE_THRU ) && window->m_draw )
			window->m_draw( window, &window->m_instData );

		/// @todo visit list boxes and borders, this is stupid!
		// for list boxes only draw the borders BEFORE the children
		if( BitTest( window->winGetStyle(), GWS_SCROLL_LISTBOX ) )
			if( BitTest( window->m_status, WIN_STATUS_BORDER ) == TRUE &&
					!BitTest( window->m_status, WIN_STATUS_SEE_THRU ) )
				window->winDrawBorder();

		// draw children in reverse order just like the window list
		child = window->m_child;
		while( child && child->m_next )
			child = child->m_next;

		for( ; child; child = child->m_prev )
				drawWindow( child );

		//
		// draw the border for the window AFTER the window contents AND the
		// children contents have drawn
		//
		if( !BitTest( window->winGetStyle(), GWS_SCROLL_LISTBOX ) )
			if( BitTest( window->m_status, WIN_STATUS_BORDER ) == TRUE &&
					!BitTest( window->m_status, WIN_STATUS_SEE_THRU ) )
				window->winDrawBorder();

	}  // end if

	return WIN_ERR_OK;

}  // end drawWindow

//-------------------------------------------------------------------------------------------------
/** Draw the GUI in reverse order to correlate with clicking priority */
//-------------------------------------------------------------------------------------------------
void GameWindowManager::winRepaint( void )
{
	GameWindow *window, *next;

	// draw below windows
	for( window = m_windowTail; window; window = next )
	{
		next = window->m_prev;

		if( BitTest( window->m_status, WIN_STATUS_BELOW ) )
			drawWindow( window );
	}

	// draw non-above and non-below windows
	for( window = m_windowTail; window; window = next )
	{
		next = window->m_prev;

		if (BitTest( window->m_status, WIN_STATUS_ABOVE | 
																	 WIN_STATUS_BELOW ) == FALSE)
			drawWindow( window );
	}

	// draw above windows
	for( window = m_windowTail; window; window = next )
	{
		next = window->m_prev;

		if( BitTest( window->m_status, WIN_STATUS_ABOVE ) )
			drawWindow( window );
	}

	if(TheTransitionHandler)
		TheTransitionHandler->draw();
}  // end WinRepaint

//-------------------------------------------------------------------------------------------------
/** Dump information about all the windows for resource problems */
//-------------------------------------------------------------------------------------------------
void GameWindowManager::dumpWindow( GameWindow *window )
{
#ifndef FINAL
	GameWindow *child;

	if( window == nullptr )
		return;

	DEBUG_LOG(( "ID: %d\tRedraw: %p\tUser Data: %p\n",
				 	 window->winGetWindowId(), reinterpret_cast<void *>(window->m_draw), window->m_userData ));
	
	for( child = window->m_child; child; child = child->m_next )
		dumpWindow( child );

	return;
#endif
}  // end dumpWindow

//-------------------------------------------------------------------------------------------------
/** Create a new window by setting up its parameters and callbacks. */
//-------------------------------------------------------------------------------------------------
GameWindow *GameWindowManager::winCreate( GameWindow *parent, 
																				  UnsignedInt status, 
																				  Int x, Int y,
																				  Int width, Int height,
																					GameWinSystemFunc system,
																					WinInstanceData *instData )
{
	GameWindow *window;

	// allocate new window
	window = allocateNewWindow();
	if( window == nullptr )
	{

		DEBUG_LOG(( "WinCreate error: Could not allocate new window\n" ));
#ifndef FINAL
		{
			GameWindow *win;

			for( win = m_windowList; win; win = win->m_next )
				dumpWindow( win );
		}
#endif

		return nullptr;

	}  // endif

	// If this is a child window add it to the parent's window list
	if( parent )
		addWindowToParent( window, parent );
	else
		linkWindow( window );

	window->m_status = status;
	window->m_size.x = width;
	window->m_size.y = height;

	window->m_region.lo.x = x;
	window->m_region.lo.y = y;
	window->m_region.hi.x = x + width;
	window->m_region.hi.y = y + height;

	window->normalizeWindowRegion();

	// set the system function and send a create message to window
	window->winSetSystemFunc( system );
	winSendSystemMsg( window, GWM_CREATE, 0, 0 );

	// copy over instance data if present
	if( instData )
		window->winSetInstanceData( instData );

	// set default font
	if (TheGlobalLanguageData && TheGlobalLanguageData->m_defaultWindowFont.name.isNotEmpty())
	{		window->winSetFont( winFindFont(
			TheGlobalLanguageData->m_defaultWindowFont.name,
			TheGlobalLanguageData->m_defaultWindowFont.size,
			TheGlobalLanguageData->m_defaultWindowFont.bold) );
	}
	else
		window->winSetFont( winFindFont( AsciiString("Times New Roman"), 14, FALSE ) );

	return window;

}  // end WinCreate

//-------------------------------------------------------------------------------------------------
/** Take a window and its children off the top level list and free
	* their allocation class data. */
//-------------------------------------------------------------------------------------------------
Int GameWindowManager::winDestroy( GameWindow *window )
{
	GameWindow *child, *next;
	
	if( window == nullptr )
		return WIN_ERR_INVALID_WINDOW;

	//
	// we should never have edit data allocated in the window code, it's
	// completely handled by the editor ONLY
	//
	DEBUG_ASSERTCRASH( window->winGetEditData() == nullptr,
										 ("winDestroy(): edit data should NOT be present!\n") );

	if( BitTest( window->m_status, WIN_STATUS_DESTROYED ) )
		return WIN_ERR_OK;

	BitSet( window->m_status, WIN_STATUS_DESTROYED );
	window->freeImages();

	if( m_mouseCaptor == window )
		winRelease( window );

	if( m_keyboardFocus == window )
		winSetFocus( nullptr );
	if( m_focusLostByHiding == window )
		m_focusLostByHiding = nullptr;

	if( (m_modalHead != nullptr) && (window == m_modalHead->window) )
		winUnsetModal( m_modalHead->window );
	else
	{
		// a modal window below the top one (a message box opened over another) must leave the stack as
		// well, or it comes back as modal head once the top one closes, after it has been freed
		for( ModalWindow **link = &m_modalHead; *link; link = &( *link )->next )
		{
			if( ( *link )->window == window )
			{
				ModalWindow *entry = *link;
				*link = entry->next;
				deleteInstanceOf( entry );
				break;
			}
		}
	}

	if( m_currMouseRgn == window )
		m_currMouseRgn = nullptr;

	if( m_grabWindow == window )
		m_grabWindow = nullptr;

	for( child = window->m_child; child; child = next )
	{
		next = child->m_next;
		winDestroy( child );
	}

	// Remove the top level window from list
	if( window->m_parent == nullptr )
		unlinkWindow( window );
	else
		unlinkChildWindow( window );

	// Add to head of the destroy list
	window->m_prev = nullptr;
	window->m_next = m_destroyList;

	m_destroyList = window;

	//
	// if this window is part of a layout screen, notify the screen that
	// this window is going away
	//
	if( window->m_layout )
		window->m_layout->removeWindow( window );

	return WIN_ERR_OK;

}  // winDestroy

//-------------------------------------------------------------------------------------------------
/** Destroy all windows on the window list IMMEDIATELY */
//-------------------------------------------------------------------------------------------------
Int GameWindowManager::winDestroyAll( void )
{
	GameWindow *win, *next;

	//
	// NOTE that it is CRITICAL that the windows be destroyed this way,
	// the editor has windows that are not on this main list that must
	// exist throughout a reset of the system (copy/paste for instance)
	// so DO NOT ever change this to a clever pool of memory for the 
	// windows and reset the _pool_ ... I will have to kill you!  CBD
	//

	for( next = win = m_windowList; next; win = next)
	{
		next = win->m_next;

		winDestroy( win );

	}  // end for

	// Destroy All Windows just added to destroy list
	processDestroyList();

	return WIN_ERR_OK;

}  // end WinDestroyAll

//-------------------------------------------------------------------------------------------------
/** Sets selected window into a modal state.  This window will get
	* put at the top of a modal stack */
//-------------------------------------------------------------------------------------------------
Int GameWindowManager::winSetModal( GameWindow *window )
{
	ModalWindow *modal;

	if( window == nullptr )
		return WIN_ERR_INVALID_WINDOW;

	// verify requesting window is a root window
	if( window->m_parent != nullptr )
	{
		DEBUG_LOG(( "WinSetModal: Non Root window attempted to go modal." ));
		return WIN_ERR_INVALID_PARAMETER;			// return error if not
	}
	// Allocate new Modal Window Entry
	modal = newInstance(ModalWindow);
	if( modal == nullptr )
	{
		DEBUG_LOG(( "WinSetModal: Unable to allocate space for Modal Entry." ));
		return WIN_ERR_GENERAL_FAILURE;
	}

	// Put new entry at top of list
	modal->window = window;
	modal->next = m_modalHead;
	m_modalHead = modal;

	return WIN_ERR_OK;

}  // end WinSetModal

//-------------------------------------------------------------------------------------------------
/** pops window off of the modal stack.  If this window is not the top
	* of the modal stack an error will occur. */
//-------------------------------------------------------------------------------------------------
Int GameWindowManager::winUnsetModal( GameWindow *window )
{
	ModalWindow *next;

	if( window == nullptr )
		return WIN_ERR_INVALID_WINDOW;

	// verify entry is at top of list
	if( (m_modalHead == nullptr) || (m_modalHead->window != window) )
	{

		// return error if not
		DEBUG_LOG(( "WinUnsetModal: Invalid window attempting to unset modal (%d)\n", 
								window->winGetWindowId() ));
		return WIN_ERR_GENERAL_FAILURE;

	}  // end if

	// remove from top of list
	next = m_modalHead->next;
	deleteInstanceOf(m_modalHead);
	m_modalHead = next;

	return WIN_ERR_OK;

}  // end WinUnsetModal

//-------------------------------------------------------------------------------------------------
/** Get the grabbed window */
//-------------------------------------------------------------------------------------------------
GameWindow *GameWindowManager::winGetGrabWindow( void )
{

	return m_grabWindow;

}  // end WinGetGrabWindow

//-------------------------------------------------------------------------------------------------
/** Explicitly set the grab window */
//-------------------------------------------------------------------------------------------------
void GameWindowManager::winSetGrabWindow( GameWindow *window )
{

	m_grabWindow = window;

}  // end winSetGrabWindow

//-------------------------------------------------------------------------------------------------
/** Explicitly set the grab window */
//-------------------------------------------------------------------------------------------------
void GameWindowManager::winSetLoneWindow( GameWindow *window )
{
	// ignore if we're trying to set the same window
	if( m_loneWindow == window )
		return;
	if( m_loneWindow )
		TheWindowManager->winSendSystemMsg( m_loneWindow, GGM_CLOSE, 0, 0 );
	m_loneWindow = window;

}  // end winSetGrabWindow

//-------------------------------------------------------------------------------------------------
/** Given a text label, retreive the real localized text associated
	* with that label */
//-------------------------------------------------------------------------------------------------
UnicodeString GameWindowManager::winTextLabelToText( AsciiString label )
{
	
	// sanity
	if( label.isEmpty() )
		return UnicodeString::TheEmptyString;

	/// @todo we need to write the string manager here, this is TEMPORARY!!!
	UnicodeString tmp;
	tmp.translate(label);
	return tmp;

}  // end winTextLabelToText

//-------------------------------------------------------------------------------------------------
/** find the top window at the given coordinates */
//-------------------------------------------------------------------------------------------------
GameWindow *GameWindowManager::getWindowUnderCursor( Int x, Int y, Bool ignoreEnabled )
{
	if( m_mouseCaptor )
	{
		// in what what window within the captured window are we?
		return m_mouseCaptor->winPointInChild( x, y, ignoreEnabled );
	}

	if( m_grabWindow )
	{
		// in what what window within the grabbed window are we?
		return m_grabWindow->winPointInChild( x, y, ignoreEnabled );
	}

	GameWindow *window = nullptr;
	if( m_modalHead && m_modalHead->window )
	{
		return m_modalHead->window->winPointInChild( x, y, ignoreEnabled );
	}
	else
	{
		// search for top-level window which contains pointer
		for( window = m_windowList; window; window = window->m_next )
		{

			if( BitTest( window->m_status, WIN_STATUS_ABOVE ) &&
					!BitTest( window->m_status, WIN_STATUS_HIDDEN ) &&
					x >= window->m_region.lo.x &&
					x <= window->m_region.hi.x &&
					y >= window->m_region.lo.y &&
					y <= window->m_region.hi.y)
			{
				if( BitTest( window->m_status, WIN_STATUS_ENABLED ) || ignoreEnabled )
				{
					// determine which child window the mouse is in
					window = window->winPointInChild( x, y, ignoreEnabled );
					break;  // exit for
				}
			}  // end if
		}  // end for window

		// check !above, below and hidden
		if( window == nullptr )
		{
			for( window = m_windowList; window; window = window->m_next )
			{
				if( !BitTest( window->m_status, WIN_STATUS_ABOVE | 
																				WIN_STATUS_BELOW | 
																				WIN_STATUS_HIDDEN ) &&
						x >= window->m_region.lo.x &&
						x <= window->m_region.hi.x &&
						y >= window->m_region.lo.y &&
						y <= window->m_region.hi.y)
				{
					if( BitTest( window->m_status, WIN_STATUS_ENABLED )|| ignoreEnabled)
					{								
						// determine which child window the mouse is in
						window = window->winPointInChild( x, y, ignoreEnabled );
						break;  // exit for
					}
				}
			}
		}  // end if, window == NULL

		// check below and !hidden
		if( window == nullptr )
		{
			for( window = m_windowList; window; window = window->m_next )
			{
				if( BitTest( window->m_status, WIN_STATUS_BELOW ) &&
						!BitTest( window->m_status, WIN_STATUS_HIDDEN ) &&
						x >= window->m_region.lo.x &&
						x <= window->m_region.hi.x &&
						y >= window->m_region.lo.y &&
						y <= window->m_region.hi.y)
				{
					if( BitTest( window->m_status, WIN_STATUS_ENABLED )|| ignoreEnabled)
					{
						// determine which child window the mouse is in
						window = window->winPointInChild( x, y, ignoreEnabled );
						break;  // exit for
					}
				}
			}
		}  // end if
	}  // end else, no modal head

	if( window )
	{
		if( BitTest( window->m_status, WIN_STATUS_NO_INPUT ))
		{
			// this window does not accept input, discard
			window = nullptr;
		}
		else if( ignoreEnabled && !( BitTest( window->m_status, WIN_STATUS_ENABLED ) ))
		{
			window = nullptr;
		}
	}

	return window;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////


void GameWindowManager::winNextTab( GameWindow *window )
{
	if(m_tabList.size() == 0|| m_modalHead)
		return;

	GameWindowList::iterator it = m_tabList.begin();
	while( it != m_tabList.end())
	{
		if(*it == window)
		{
			it++;
			break;
		}
		it++;
	}
	if(it != m_tabList.end())
		winSetFocus(*it);
	else
	{
		winSetFocus(*m_tabList.begin());
	}
	winSetLoneWindow(nullptr);
}

void GameWindowManager::winPrevTab( GameWindow *window )
{
	if(m_tabList.size() == 0 || m_modalHead)
		return;

	GameWindowList::reverse_iterator it = m_tabList.rbegin();
	while( it != m_tabList.rend())
	{
		if(*it == window)
		{
			it++;
			break;
		}
		it++;
	}
	if(it != m_tabList.rend())
		winSetFocus(*it);
	else
	{
		winSetFocus(*m_tabList.rbegin());
	}	
	winSetLoneWindow(nullptr);
}

void GameWindowManager::registerTabList( GameWindowList tabList )
{
	m_tabList.clear();
	m_tabList = tabList;
}

void GameWindowManager::clearTabList( void )
{
	m_tabList.clear();
}
