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

// FILE: CheckBox.cpp /////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2001 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: CheckBox.cpp
//
// Created:   Colin Day, June 2001
//
// Desc:      Checkbox GUI control
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "Common/Language.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/GadgetClone.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Keyboard.h"

// DEFINES ////////////////////////////////////////////////////////////////////

// PRIVATE TYPES //////////////////////////////////////////////////////////////

// PRIVATE DATA ///////////////////////////////////////////////////////////////

// PUBLIC DATA ////////////////////////////////////////////////////////////////

// PRIVATE PROTOTYPES /////////////////////////////////////////////////////////

// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// GadgetCheckBoxInput ========================================================
/** Handle input for check box */
//=============================================================================
WindowMsgHandledType GadgetCheckBoxInput( GameWindow *window, UnsignedInt msg,
													WindowMsgData mData1, WindowMsgData mData2 )
{
	WinInstanceData *instData = window->winGetInstanceData();

	switch( msg ) 
	{

		// ------------------------------------------------------------------------
		case GWM_MOUSE_ENTERING:
		{

			if( BitTest( instData->getStyle(), GWS_MOUSE_TRACK ) ) 
			{

				BitSet( instData->m_state, WIN_STATE_HILITED );
				TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
																						GBM_MOUSE_ENTERING,
																						reinterpret_cast<WindowMsgData>(window), 
																						mData1 );
				//TheWindowManager->winSetFocus( window );

			}  // end if
			break;

		}  // end mouse entering

		// ------------------------------------------------------------------------
		case GWM_MOUSE_LEAVING:
		{

			if( BitTest( instData->getStyle(), GWS_MOUSE_TRACK ) ) 
			{

				BitClear( instData->m_state, WIN_STATE_HILITED );
				TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
																						GBM_MOUSE_LEAVING,
																						reinterpret_cast<WindowMsgData>(window), 
																						mData1 );
			}  // end if
			break;

		}  // end mouse leaving

		// ------------------------------------------------------------------------
		case GWM_LEFT_DRAG:
		{

			TheWindowManager->winSendSystemMsg( window->winGetOwner(), GGM_LEFT_DRAG,
																					reinterpret_cast<WindowMsgData>(window), mData1 );
			break;

		}  // end left drag

		// ------------------------------------------------------------------------
		case GWM_LEFT_DOWN:
		{

			break;

		}  // end left down

		// ------------------------------------------------------------------------
		case GWM_LEFT_UP:
		{

			if( BitTest( instData->getState(), WIN_STATE_HILITED ) == FALSE )
			{
				// this up click was not meant for this button
				return WindowMsgHandledType::MSG_IGNORED;
			}

			// Toggle the check state
			instData->m_state ^= WIN_STATE_SELECTED;

			TheWindowManager->winSendSystemMsg( window->winGetOwner(), GBM_SELECTED,
																					reinterpret_cast<WindowMsgData>(window), mData1 );


			break;

		}  // end left up and left click

		// ------------------------------------------------------------------------
		case GWM_RIGHT_DOWN:
		{

			break;
		}  // end right down

		//-------------------------------------------------------------------------
		case GWM_RIGHT_UP:
		{
			// Need to be specially marked to care about right mouse events
			if( BitTest( instData->getState(), WIN_STATE_SELECTED ) )
			{
				TheWindowManager->winSendSystemMsg( instData->getOwner(), GBM_SELECTED_RIGHT,
																						reinterpret_cast<WindowMsgData>(window), mData1 );

				BitClear( instData->m_state, WIN_STATE_SELECTED );
			}
			else
			{
				// this up click was not meant for this button
				return WindowMsgHandledType::MSG_IGNORED;
			}
			break;
		}  // end right up or right click

		// ------------------------------------------------------------------------
		case GWM_CHAR:
		{

			switch( mData1 ) 
			{

				// --------------------------------------------------------------------
				case KEY_ENTER:
				case KEY_SPACE:
				{

					if( BitTest( mData2, KEY_STATE_DOWN ) )
					{
						// Toggle the check state
						instData->m_state ^= WIN_STATE_SELECTED;

						TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
																								GBM_SELECTED,
																								reinterpret_cast<WindowMsgData>(window), 
																								0 );
					}  //end if
					break;

				}  // end enter/space

				// --------------------------------------------------------------------
				case KEY_DOWN:
				case KEY_RIGHT:
				case KEY_TAB:
				{

					if( BitTest( mData2, KEY_STATE_DOWN ) )
						TheWindowManager->winNextTab(window);
					break;

				}  // end down, right, tab

				// --------------------------------------------------------------------
				case KEY_UP:
				case KEY_LEFT:
				{

					if( BitTest( mData2, KEY_STATE_DOWN ) )
						TheWindowManager->winPrevTab(window);
					break;

				}  // end up, left

				// --------------------------------------------------------------------
				default:
				{

					return WindowMsgHandledType::MSG_IGNORED;

				}  // end default

			}  // end switch

			break;

		}  // end char msg

		// ------------------------------------------------------------------------
		default:
		{

			return WindowMsgHandledType::MSG_IGNORED;

		}  // end default

	}  // end switch( msg )

	return WindowMsgHandledType::MSG_HANDLED;

}  // end GadgetCheckBoxInput

// GadgetCheckBoxSystem =======================================================
/** Handle system messages for check box */
//=============================================================================
WindowMsgHandledType GadgetCheckBoxSystem( GameWindow *window, UnsignedInt msg,
													 WindowMsgData mData1, WindowMsgData mData2 )
{
	WinInstanceData *instData = window->winGetInstanceData();

	switch( msg ) 
	{
		// ------------------------------------------------------------------------	
		case GGM_SET_LABEL:
		{
			window->winSetText( *reinterpret_cast<UnicodeString*>(mData1) );
			break;
		}

		// ------------------------------------------------------------------------
		case GWM_CREATE:
			break;

		// ------------------------------------------------------------------------
		case GWM_DESTROY:
			break;

		// ------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:

			if( mData1 == FALSE )
				BitClear( instData->m_state, WIN_STATE_HILITED );
			else
				BitSet( instData->m_state, WIN_STATE_HILITED );
			TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
																					GGM_FOCUS_CHANGE,
																					mData1, 
																					window->winGetWindowId() );
			if( mData1 == FALSE )
				*reinterpret_cast<Bool*>(mData2) = FALSE;
			else
				*reinterpret_cast<Bool*>(mData2) = TRUE;
			
			break;

		default: 
			return WindowMsgHandledType::MSG_IGNORED;

	}  // end switch msg

	return WindowMsgHandledType::MSG_HANDLED;

}  // end GadgetCheckBoxSystem

// GadgetCheckBoxSetText ======================================================
/** Set the text for the control */
//=============================================================================
void GadgetCheckBoxSetText( GameWindow *g, UnicodeString text )
{

	// sanity
	if( g == nullptr )
		return;

	TheWindowManager->winSendSystemMsg( g, GGM_SET_LABEL, reinterpret_cast<WindowMsgData>(&text), 0 );

}  // end GadgetCheckBoxSetText

// GadgetCheckBoxSetChecked ============================================
//=============================================================================
/** Set the check state for the check box */
//=============================================================================
void GadgetCheckBoxSetChecked( GameWindow *g, Bool isChecked)
{
	WinInstanceData *instData = g->winGetInstanceData();
	if (isChecked)
	{
		BitSet(instData->m_state,  WIN_STATE_SELECTED);
	}
	else
	{
		BitClear(instData->m_state,  WIN_STATE_SELECTED);
	}

	TheWindowManager->winSendSystemMsg( g->winGetOwner(), GBM_SELECTED,
																					reinterpret_cast<WindowMsgData>(g), 0 );

}

// GadgetCheckBoxToggle ============================================
//=============================================================================
/** Toggle the check state for the check box */
//=============================================================================
void GadgetCheckBoxToggle( GameWindow *g)
{
	WinInstanceData *instData = g->winGetInstanceData();
	Bool isChecked = BitTest(instData->m_state, WIN_STATE_SELECTED);
	if (isChecked)
	{
		BitClear(instData->m_state,  WIN_STATE_SELECTED);
	}
	else
	{
		BitSet(instData->m_state,  WIN_STATE_SELECTED);
	}

	TheWindowManager->winSendSystemMsg( g->winGetOwner(), GBM_SELECTED,
																					reinterpret_cast<WindowMsgData>(g), 0 );

}

// GadgetCheckBoxIsChecked ======================================================
/** Check the check state */
//=============================================================================
Bool GadgetCheckBoxIsChecked( GameWindow *g )
{
	WinInstanceData *instData = g->winGetInstanceData();
	return (BitTest(instData->m_state, WIN_STATE_SELECTED));
}

// Below the lowest check box of the model's column, spaced like them; the look is copied from the model.
GameWindow *GadgetCheckBoxCreateBelow( GameWindow *model, const char *decoratedName, const char *textLabel )
{
	if( model == nullptr || model->winGetParent() == nullptr )
		return nullptr;
	GameWindow *parent = model->winGetParent();

	Int modelX, modelY, width, height, parentW, parentH;
	model->winGetPosition( &modelX, &modelY );
	model->winGetSize( &width, &height );
	parent->winGetSize( &parentW, &parentH );

	// lowest check box of the column and the smallest spacing between two of them
	Int lowestY = modelY;
	Int step = 0;
	for( GameWindow *a = parent->winGetChild(); a; a = a->winGetNext() )
	{
		Int ax, ay;
		a->winGetPosition( &ax, &ay );
		if( ax != modelX || !BitTest( a->winGetStyle(), GWS_CHECK_BOX ) )
			continue;
		if( ay > lowestY )
			lowestY = ay;
		for( GameWindow *b = parent->winGetChild(); b; b = b->winGetNext() )
		{
			Int bx, by;
			b->winGetPosition( &bx, &by );
			if( bx == modelX && BitTest( b->winGetStyle(), GWS_CHECK_BOX ) && by > ay && ( step == 0 || by - ay < step ) )
				step = by - ay;
		}
	}
	if( step < height )
		step = height + height / 2;

	const Int x = modelX;
	const Int y = lowestY + step;
	if( y + height > parentH || x + width > parentW )
		return nullptr;
	// A window that already lies under the model is a background panel, not an obstacle.
	for( GameWindow *w = parent->winGetChild(); w; w = w->winGetNext() )
	{
		Int wx, wy, ww, wh;
		w->winGetPosition( &wx, &wy );
		w->winGetSize( &ww, &wh );
		const Bool underModel = w != model && wx < modelX + width && modelX < wx + ww && wy < modelY + height && modelY < wy + wh;
		if( !w->winIsHidden() && !underModel && wx < x + width && x < wx + ww && wy < y + height && y < wy + wh )
			return nullptr;
	}

	GameWindow *box = GadgetCloneCheckBox( model, parent, decoratedName, x, y, width, height, textLabel );
	if( box && model->winIsHidden() )
		box->winHide( TRUE );
	if( box && !model->winGetEnabled() )
		box->winEnable( FALSE );
	return box;
}
