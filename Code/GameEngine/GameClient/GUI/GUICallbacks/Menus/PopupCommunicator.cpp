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

// FILE: PopupCommunicator.cpp /////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Electronic Arts Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2002 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: PopupCommunicator.cpp
//
// Created:   Chris Brue, July 2002
//
// Desc:      Electronic Arts instant messaging system
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/GUICallbacks.h"
#include "GameClient/GameWindowManager.h"

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
static NameKeyType buttonOkID = NAMEKEY_INVALID;
static GameWindow *buttonOk = nullptr;
static GameWindow *parent = nullptr;

// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
/** Initialize the Popup Communicator */
//-------------------------------------------------------------------------------------------------
void PopupCommunicatorInit( WindowLayout *layout, void *userData )
{

	//set keyboard focus to main parent and set modal
	NameKeyType parentID = TheNameKeyGenerator->nameToKey("PopupCommunicator.wnd:PopupCommunicator");
	parent = TheWindowManager->winGetWindowFromId( nullptr, parentID );
	TheWindowManager->winSetFocus( parent );
	TheWindowManager->winSetModal( parent );

	// get ids for our children controls
	buttonOkID = TheNameKeyGenerator->nameToKey( AsciiString("PopupCommunicator.wnd:ButtonOk") );
	buttonOk = TheWindowManager->winGetWindowFromId( parent, buttonOkID );

}  // end PopupCommunicatorInit

//-------------------------------------------------------------------------------------------------
/** Popup Communicator shutdown method */
//-------------------------------------------------------------------------------------------------
void PopupCommunicatorShutdown( WindowLayout *layout, void *userData )
{

}  // end PopupCommunicatorShutdown

//-------------------------------------------------------------------------------------------------
/** Popup Communicator update method */
//-------------------------------------------------------------------------------------------------
void PopupcommunicatorUpdate( WindowLayout *layout, void *userData )
{

}  // end PopupCommunicatorUpdate 

//-------------------------------------------------------------------------------------------------
/** Popup Communicator input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType PopupCommunicatorInput( GameWindow *window, UnsignedInt msg,
																						WindowMsgData mData1, WindowMsgData mData2 )
{

	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = static_cast<UnsignedByte>(mData1);
			UnsignedByte state = static_cast<UnsignedByte>(mData2);

			switch( key )
			{

				// ----------------------------------------------------------------------------------------
				case KEY_ESC:
				{
					
					//
					// send a simulated selected event to the parent window of the
					// back/exit button
					//
					if( BitTest( state, KEY_STATE_UP ) )
					{

						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED, 
																								reinterpret_cast<WindowMsgData>(buttonOk), buttonOkID );

					}  // end if

					// don't let key fall through anywhere else
					return WindowMsgHandledType::MSG_HANDLED;

				}  // end escape

			}  // end switch( key )

		}  // end char

	}  // end switch( msg )

	return WindowMsgHandledType::MSG_IGNORED;

}  // end PopupCommunicatorInput

//-------------------------------------------------------------------------------------------------
/** Popup Communicator window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType PopupCommunicatorSystem( GameWindow *window, UnsignedInt msg, 
																		 WindowMsgData mData1, WindowMsgData mData2 )
{

  switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CREATE:
		{

			break;

		}  // end create
    //---------------------------------------------------------------------------------------------
		case GWM_DESTROY:
		{

			break;

		}  // end case

    //----------------------------------------------------------------------------------------------
    case GWM_INPUT_FOCUS:
		{

			// if we're givin the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*reinterpret_cast<Bool *>(mData2) = TRUE;

			break;

		}  // end input
    //---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			GameWindow *control = reinterpret_cast<GameWindow *>(mData1);
			Int controlID = control->winGetWindowId();
      
			if( controlID == buttonOkID )
			{
        WindowLayout *popupCommunicatorLayout = window->winGetLayout();
        popupCommunicatorLayout->destroyWindows();
				deleteInstanceOf(popupCommunicatorLayout);
				popupCommunicatorLayout = nullptr;
			}  // end if
	
			break;

		}  // end selected

		default:
			return WindowMsgHandledType::MSG_IGNORED;

	}  // end switch

	return WindowMsgHandledType::MSG_HANDLED;

}