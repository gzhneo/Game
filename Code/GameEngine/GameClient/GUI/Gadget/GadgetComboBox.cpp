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

// FILE: GadgetComboBox.cpp ///////////////////////////////////////////////////
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
// File name: ListBox.cpp
//
// Created:   Dean Iverson, March 1998
//						Colin Day, June 2001
//
// Desc:      ListBox GUI control
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "Common/Language.h"
#include "Common/AudioEventRTS.h"
#include "Common/GameAudio.h"
#include "Common/Debug.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameWindow.h"
#include "GameClient/Gadget.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetSlider.h"
#include "GameClient/GameWindowGlobal.h"
#include "GameClient/GadgetClone.h"

// DEFINES ////////////////////////////////////////////////////////////////////

// PRIVATE TYPES //////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// PRIVATE DATA ///////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// PUBLIC DATA ////////////////////////////////////////////////////////////////

// PRIVATE PROTOTYPES /////////////////////////////////////////////////////////

// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void HideListBox(GameWindow * window);

// GadgetComboBoxInput =========================================================
/** Handle input for Combo box */
//=============================================================================
WindowMsgHandledType GadgetComboBoxInput( GameWindow *window, UnsignedInt msg,
												 WindowMsgData mData1, WindowMsgData mData2 )
{
//	ComboBoxData *combo = (ComboBoxData *)window->winGetUserData();
	WinInstanceData *instData = window->winGetInstanceData();
	GameWindow *editBox = GadgetComboBoxGetEditBox(window);
	switch (msg)
	{

		// ------------------------------------------------------------------------
		case GWM_CHAR:
		{

			switch (mData1)
			{
				
				// --------------------------------------------------------------------
				case KEY_DOWN:
				case KEY_RIGHT:
				case KEY_TAB:

					if( BitTest( mData2, KEY_STATE_DOWN ) )
						TheWindowManager->winNextTab(window);
					break;

				// --------------------------------------------------------------------
				case KEY_UP:
				case KEY_LEFT:

					if( BitTest( mData2, KEY_STATE_DOWN ) )
						TheWindowManager->winPrevTab(window);
					break;

				// --------------------------------------------------------------------
				default:
					return TheWindowManager->winSendInputMsg( editBox, GWM_CHAR, mData1, mData2 );

			}  // end switch( mData1 )

			break;

		}  // end case char

		// ------------------------------------------------------------------------
		case GWM_WHEEL_DOWN:
		{

			break;

		}  // end wheel down

		// ------------------------------------------------------------------------
		case GWM_WHEEL_UP:
		{
			break;

		}  // end wheel up

		// ------------------------------------------------------------------------
		case GWM_LEFT_UP:
		{
			ComboBoxData *comboData = static_cast<ComboBoxData *>(window->winGetUserData());
				ICoord2D winSize;
				//ICoord2D winPosition;
				ICoord2D newSize;
				Int listX =0;
				Int multiplier;
				comboData->dontHide = FALSE;
				AudioEventRTS buttonClick("GUIClick");

				if( TheAudio )
				{
					TheAudio->addAudioEvent( &buttonClick );
				}  // end if

				GameWindow *listBox = GadgetComboBoxGetListBox(window);
				if (listBox)
				{
					TheWindowManager->winSetLoneWindow(window);
					// If the Listbox isn't showing, Show it.
					if(listBox->winIsHidden())
					{
						listBox->winHide(FALSE);
						// later siblings would cover the open list
						window->winBringToTop();
						window->winGetSize(&winSize.x, &winSize.y);
						WinInstanceData *listInstData = listBox->winGetInstanceData();
						ListboxData *listData = static_cast<ListboxData *>(listBox->winGetUserData());
						// If we have less entries then our max display is set to, only show
						// those entries and not additional blank lines.  Also, just so it looks
						// pretty, hide the list box's sliders if we don't need to scroll.
						if(comboData->entryCount <= comboData->maxDisplay)
						{
							multiplier = comboData->entryCount;
							listX = winSize.x;// + 16;
							
							if(listData->upButton)
								listData->upButton->winHide(TRUE);
							if(listData->downButton)
								listData->downButton->winHide(TRUE);
							if(listData->slider)
								listData->slider->winHide(TRUE);
						}
						else
						{
							multiplier = comboData->maxDisplay;
							listX = winSize.x;
							if(listData->upButton)
								listData->upButton->winHide(FALSE);
							if(listData->downButton)
								listData->downButton->winHide(FALSE);
							if(listData->slider)
								listData->slider->winHide(FALSE);
							
						}
						
						newSize.y = ((TheWindowManager->winFontHeight( listInstData->getFont() ) ) * multiplier) + multiplier * 2 + 4;
						window->winSetSize(winSize.x , winSize.y + newSize.y );
						listBox->winSetPosition(0, winSize.y);

						listBox->winSetSize(listX , newSize.y);
					}
					// if the Listbox was showing, hide it.
					else
					{
						HideListBox(window);
					}
				}			break;

		}  // end left click, left up

		// ------------------------------------------------------------------------
		case GWM_RIGHT_UP:
		{
			break;

		}  // end right up, right click

/*
		// ------------------------------------------------------------------------
		case GWM_MOUSE_ENTERING:
		{

			if( BitTest( instData->getStyle(), GWS_MOUSE_TRACK ) ) 
			{

				BitSet( instData->m_state, WIN_STATE_HILITED );
				TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
																						GBM_MOUSE_ENTERING,
																						(WindowMsgData)window, 
																						0 );
				TheWindowManager->winSetFocus( window );

			}  // end if

			break;

		}  //  end mouse entering

		// ------------------------------------------------------------------------
		case GWM_MOUSE_LEAVING:
		{

			if( BitTest( instData->getStyle(), GWS_MOUSE_TRACK )) 
			{

				BitClear( instData->m_state, WIN_STATE_HILITED );
				TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
																						GBM_MOUSE_LEAVING,
																						(WindowMsgData)window, 
																						0 );
			}  // end if

			break;

		}  // end mouse leaving
*/

		// ------------------------------------------------------------------------
		case GWM_LEFT_DRAG:

			if (BitTest( instData->getStyle(), GWS_MOUSE_TRACK ) )
				TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
																						GGM_LEFT_DRAG,
																						reinterpret_cast<WindowMsgData>(window), 
																						0 );
			break;

		// ------------------------------------------------------------------------
//		case GWM_LEFT_DOWN:

			// we want to eat the down... so we may receive the up.
//			return MSG_HANDLED;

		//-------------------------------------------------------------------------
		default:
			return WindowMsgHandledType::MSG_IGNORED;

	}  // end switch msg

	return WindowMsgHandledType::MSG_HANDLED;

}  // end GadgetComboBoxInput

// HideListBox ================================================================
/** Called to close the listbox if it is opened */
//=============================================================================
void HideListBox(GameWindow * window)
{
	ICoord2D winSize;
	ICoord2D newSize;
	GameWindow *listBox = GadgetComboBoxGetListBox(window);
	if (!listBox)
		return;

	if(!listBox->winIsHidden())
	{
		listBox->winHide(TRUE);
		GameWindow *editBox = GadgetComboBoxGetEditBox(window);
		editBox->winGetSize(&winSize.x, &winSize.y);
		window->winGetSize(&newSize.x, &newSize.y);
		window->winSetSize(newSize.x, winSize.y );
	}
}

// GadgetListBoxSystem ========================================================
/** Handle system messages for list box */
//=============================================================================
WindowMsgHandledType GadgetComboBoxSystem( GameWindow *window, UnsignedInt msg,
													WindowMsgData mData1, WindowMsgData mData2 )
{
//	ListboxData *list = (ListboxData *)window->winGetUserData();
	WinInstanceData *instData = window->winGetInstanceData();
	ComboBoxData *comboData = static_cast<ComboBoxData *>(window->winGetUserData());
	switch( msg )
	{
		// ------------------------------------------------------------------------
		case GGM_SET_LABEL:
		{
			instData->setText(*reinterpret_cast<UnicodeString*>(mData1));
			break;

		}  // end set lavel

		// ------------------------------------------------------------------------
		case GCM_GET_TEXT:
		{
			if(comboData->editBox)
				*reinterpret_cast<UnicodeString*>(mData2) = GadgetTextEntryGetText(comboData->editBox);
			break;
		} // end Get text

		// ------------------------------------------------------------------------
		case GCM_SET_TEXT:
		{
			if (comboData->editBox)
				GadgetTextEntrySetText(comboData->editBox,*reinterpret_cast<const UnicodeString*>(mData1));
			break;

		}  // end set text

		case GEM_UPDATE_TEXT:
		{
			TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
																	GCM_UPDATE_TEXT,
																	reinterpret_cast<WindowMsgData>(window), 
																	0 );
			if (comboData->listBox)
			{
				GadgetListBoxSetSelected(comboData->listBox, -1);
				HideListBox(window);
			}
			break;
		}

		// ------------------------------------------------------------------------
		// if we get sent an edit done message from the text box, lets notify the parent
		case GEM_EDIT_DONE:
		{
			if (reinterpret_cast<GameWindow *>(mData1) == comboData->editBox)
			{
				HideListBox(window);
				TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
																		GCM_SELECTED,
																		reinterpret_cast<WindowMsgData>(window), 
																		0 );
			}
			break;
		}
		// ------------------------------------------------------------------------
		// Set the selection in the listbox, this will trigger the listbox selected message 
		// which will set the edit box.
		case GCM_SET_SELECTION:
		{		
			GameWindow *listBox = GadgetComboBoxGetListBox(window);					
			if(listBox)
			{
				if( !listBox->winIsHidden() && mData2 == TRUE )
					comboData->dontHide = TRUE;

				GadgetListBoxSetSelected(listBox, static_cast<Int>(mData1));
			}
			break;
		}
		// ------------------------------------------------------------------------
		// Get what the listbox has selected.
		case GCM_GET_SELECTION:
		{
			if(comboData->listBox)
				GadgetListBoxGetSelected(comboData->listBox, reinterpret_cast<Int *>(mData2));
			else
			{
				DEBUG_ASSERTCRASH(0,("We don't have a listbox as part of the combo box"));
				*reinterpret_cast<Int *>(mData2) = -1;
			}
			break;
		} //case GCM_GET_SELECTION:
		// ------------------------------------------------------------------------
		// Set the User Data for the specified listbox element
		case GCM_SET_ITEM_DATA:
		{
			if(comboData->listBox)
			{
				GadgetListBoxSetItemData(comboData->listBox, reinterpret_cast<void *>(mData2), static_cast<Int>(mData1) );
			}

			break;
		}
		// ------------------------------------------------------------------------
		// Get the user Data for the specified listbox element
		case GCM_GET_ITEM_DATA:
		{
			if(comboData->listBox)
			{
				*reinterpret_cast<void **>(mData2) = GadgetListBoxGetItemData(comboData->listBox, static_cast<Int>(mData1), 0);
			}
			break;
		}
		// ------------------------------------------------------------------------
		// Pass onto the parent window the selection the listbox just made
		case GLM_SELECTED:
		{
				if(reinterpret_cast<GameWindow *>(mData1) == comboData->listBox)
				{
					if( comboData->dontHide == TRUE )
					{
						comboData->dontHide = FALSE;
					}
					else
						HideListBox(window);
					
					// Nothing was actually selected, so we just want to 
					if( mData2 == -1)
					{
						break;
					}

					//Grab the text that was selected
					UnicodeString tempUString;
					Color color;
					tempUString = GadgetListBoxGetTextAndColor( comboData->listBox, &color, static_cast<Int>(mData2), 0 );
									
					GadgetTextEntrySetTextColor(comboData->editBox, color);
					
					GadgetTextEntrySetText(comboData->editBox, tempUString);
					
					TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
																		GCM_SELECTED,
																		reinterpret_cast<WindowMsgData>(window), 
																		0 );
				}
			break;
		}
		
		// ------------------------------------------------------------------------
		case GGM_LEFT_DRAG:
		{

			break;

		}  // end left drag

		// ------------------------------------------------------------------------
		case GCM_DEL_ALL:
		{
			if ( comboData->listBox )
				GadgetListBoxReset(comboData->listBox);
			if ( comboData->editBox )
				GadgetTextEntrySetText(comboData->editBox, UnicodeString::TheEmptyString );
			comboData->entryCount = 0;
			//HideListBox(window);	
			break;

		}  // end delete all

		// ------------------------------------------------------------------------
		case GCM_DEL_ENTRY:
		{
			
			break;

		}  // end delete entry
		// ------------------------------------------------------------------------
		case GGM_CLOSE:
		{
			HideListBox(window);
			break;

		}  // end delete entry

		// ------------------------------------------------------------------------
		case GCM_ADD_ENTRY:
		{
			GameWindow *listBox = GadgetComboBoxGetListBox(window);
			ListboxData *listData = static_cast<ListboxData *>(listBox->winGetUserData());
			
			Int addedIndex = -1;
			if( listBox )
			{
				//Increase our internal entry count
				comboData->entryCount++;
				//If we've exceeded the set listlength, resize it to twice the size
				if(comboData->entryCount >= listData->listLength)
					GadgetListBoxSetListLength(listBox,listData->listLength * 2);
				//Add the entry to the Listbox
				addedIndex = GadgetListBoxAddEntryText( listBox, *reinterpret_cast<UnicodeString*>(mData1), static_cast<Int>(mData2), -1, 0 );

				// Now resize the list box
				ICoord2D winSize;
				ICoord2D newSize;
				ICoord2D editBoxSize;
				Int listX;
				Int multiplier;
				WinInstanceData *listInstData = listBox->winGetInstanceData();
				GameWindow *editBox = GadgetComboBoxGetEditBox(window);
				window->winGetSize(&winSize.x, &winSize.y);
				editBox->winGetSize(&editBoxSize.x, &editBoxSize.y);
				// If the listbox has less entries then the MaxDisplay, size it smaller
				if(comboData->entryCount <= comboData->maxDisplay)
				{							
					multiplier = comboData->entryCount;
					listX = winSize.x + 16;
					if(listData->upButton)
						listData->upButton->winHide(TRUE);
					if(listData->downButton)
						listData->downButton->winHide(TRUE);
					if(listData->slider)
						listData->slider->winHide(TRUE);
				}
				else
				{
					//Else size it to the MaxDisplay Size
					multiplier = comboData->maxDisplay;
					listX = winSize.x;
					if(listData->upButton)
						listData->upButton->winHide(FALSE);
					if(listData->downButton)
						listData->downButton->winHide(FALSE);
					if(listData->slider)
						listData->slider->winHide(FALSE);
				}
				newSize.y = ((TheWindowManager->winFontHeight( listInstData->getFont() ) ) * multiplier) + multiplier * 2 + 4;
				listBox->winSetPosition(0, editBoxSize.y);
				listBox->winSetSize(listX , newSize.y);
			}

			return( static_cast<WindowMsgHandledType>(addedIndex) );
		}  // end add entry
	
		
		// ------------------------------------------------------------------------
		case GWM_CREATE:
			break;

		// ------------------------------------------------------------------------
		case GGM_RESIZED:
		{
			Int width = static_cast<Int>(mData1);
			Int height = static_cast<Int>(mData2);
			ICoord2D dropDownSize;

			// get needed window sizes
			
			comboData->dropDownButton->winGetSize( &dropDownSize.x, &dropDownSize.y );
		
			GameWindow *listBox = GadgetComboBoxGetListBox(window);
			if (listBox->winIsHidden())
			{
				if (listBox)
					listBox->winSetSize(width,height);
					
				if( comboData->dropDownButton )
				{
					comboData->dropDownButton->winSetPosition( width - dropDownSize.x, 0 );
				}

				if( comboData->editBox )
				{
					comboData->editBox->winSetPosition(  0,  0 );
					comboData->editBox->winSetSize( width - dropDownSize.x, height );
				}
			}
			break;

		}  // end resized

		// ------------------------------------------------------------------------

		// ------------------------------------------------------------------------
		case GWM_DESTROY:
		{
			TheWindowManager->winSetLoneWindow(nullptr); // if we are transitioning screens, close all combo boxes
			if (comboData)
			{
				delete(comboData);
				comboData = nullptr;
			}
			break;
		}  // end destroy

		// ------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
		{

			// If we're losing focus
			if( mData1 == FALSE )
			{
				BitClear( instData->m_state, WIN_STATE_HILITED );
			}
			else
			{
				BitSet( instData->m_state, WIN_STATE_HILITED );
			}

			TheWindowManager->winSendSystemMsg( window->winGetOwner(), 
																					GGM_FOCUS_CHANGE,
																					mData1, 
																					window->winGetWindowId() );

			Bool wantsFocus = FALSE;
			GameWindow *editBox = GadgetComboBoxGetEditBox(window);
			// we need to tell the text entry box to take the focus.
			TheWindowManager->winSendSystemMsg( editBox, GWM_INPUT_FOCUS, mData1, reinterpret_cast<WindowMsgData>(&wantsFocus) );
			
			*reinterpret_cast<Bool*>(mData2) = TRUE;
			break;

		}  // end input focus


		case GBM_SELECTED:
		{
			// See if the drop down button was selected
			if( reinterpret_cast<GameWindow *>(mData1) == comboData->dropDownButton )
			{
				ICoord2D winSize;
				//ICoord2D winPosition;
				ICoord2D newSize;
				Int listX =0;
				Int multiplier;
				comboData->dontHide = FALSE;
				GameWindow *listBox = GadgetComboBoxGetListBox(window);
				if (listBox)
				{
					TheWindowManager->winSetLoneWindow(window);
					// If the Listbox isn't showing, Show it.
					if(listBox->winIsHidden())
					{
						listBox->winHide(FALSE);
						// later siblings would cover the open list
						window->winBringToTop();
						window->winGetSize(&winSize.x, &winSize.y);
						WinInstanceData *listInstData = listBox->winGetInstanceData();
						ListboxData *listData = static_cast<ListboxData *>(listBox->winGetUserData());
						// If we have less entries then our max display is set to, only show
						// those entries and not additional blank lines.  Also, just so it looks
						// pretty, hide the list box's sliders if we don't need to scroll.
						if(comboData->entryCount <= comboData->maxDisplay)
						{
							multiplier = comboData->entryCount;
							listX = winSize.x;// + 16;
							
							if(listData->upButton)
								listData->upButton->winHide(TRUE);
							if(listData->downButton)
								listData->downButton->winHide(TRUE);
							if(listData->slider)
								listData->slider->winHide(TRUE);
						}
						else
						{
							multiplier = comboData->maxDisplay;
							listX = winSize.x;
							if(listData->upButton)
								listData->upButton->winHide(FALSE);
							if(listData->downButton)
								listData->downButton->winHide(FALSE);
							if(listData->slider)
								listData->slider->winHide(FALSE);
							
						}
						
						newSize.y = ((TheWindowManager->winFontHeight( listInstData->getFont() ) ) * multiplier) + multiplier * 2 + 4;
						window->winSetSize(winSize.x , winSize.y + newSize.y );
						listBox->winSetPosition(0, winSize.y);

						listBox->winSetSize(listX , newSize.y);
					}
					// if the Listbox was showing, hide it.
					else
					{
						HideListBox(window);
					}
				}
			}
			break;
		}
		default:
			return WindowMsgHandledType::MSG_IGNORED;

	}  // end switch( msg )

	return WindowMsgHandledType::MSG_HANDLED;

}  // end GadgetListBoxSystem

// GadgetComboBoxSetColors ====================================================
/** Set the colors for a Combo box, note that this will also automatically
	* change the colors of any attached slider, slider thumb, and slider 
	* buttons */
//=============================================================================
void GadgetComboBoxSetColors( GameWindow *comboBox,
														 Color enabledColor, 
														 Color enabledBorderColor,
														 Color enabledSelectedItemColor, 
														 Color enabledSelectedItemBorderColor,
														 Color disabledColor, 
														 Color disabledBorderColor,
														 Color disabledSelectedItemColor, 
														 Color disabledSelectedItemBorderColor,
														 Color hiliteColor, 
														 Color hiliteBorderColor,
														 Color hiliteSelectedItemColor, 
														 Color hiliteSelectedItemBorderColor )
{
//	ComboBoxData *comboBoxData = (ComboBoxData *)comboBox->winGetUserData();
	// enabled

// enabled
		GadgetComboBoxSetEnabledColor( comboBox, enabledColor);
		GadgetComboBoxSetEnabledBorderColor( comboBox, enabledBorderColor );
		GadgetComboBoxSetEnabledSelectedItemColor( comboBox, enabledSelectedItemColor );
		GadgetComboBoxSetEnabledSelectedItemBorderColor( comboBox, enabledSelectedItemBorderColor );
		// disabled
		GadgetComboBoxSetDisabledColor( comboBox, disabledColor );
		GadgetComboBoxSetDisabledBorderColor( comboBox, disabledBorderColor );
		GadgetComboBoxSetDisabledSelectedItemColor( comboBox, disabledSelectedItemColor );
		GadgetComboBoxSetDisabledSelectedItemBorderColor( comboBox, disabledSelectedItemBorderColor );
		// hilite
		GadgetComboBoxSetHiliteColor( comboBox, hiliteColor );
		GadgetComboBoxSetHiliteBorderColor( comboBox,hiliteBorderColor );
		GadgetComboBoxSetHiliteSelectedItemColor( comboBox, hiliteSelectedItemColor );
		GadgetComboBoxSetHiliteSelectedItemBorderColor( comboBox, hiliteSelectedItemBorderColor );
	
	GameWindow *editBox = GadgetComboBoxGetEditBox(comboBox);
	if (editBox)
	{
		// enabled
		GadgetButtonSetEnabledColor( editBox, enabledColor );
		GadgetButtonSetEnabledBorderColor( editBox, enabledBorderColor );
		GadgetButtonSetEnabledSelectedColor( editBox, enabledSelectedItemColor );
		GadgetButtonSetEnabledSelectedBorderColor( editBox, enabledSelectedItemBorderColor );
		// disabled
		GadgetButtonSetDisabledColor( editBox, disabledColor );
		GadgetButtonSetDisabledBorderColor( editBox, disabledBorderColor );
		GadgetButtonSetDisabledSelectedColor( editBox, disabledSelectedItemColor );
		GadgetButtonSetDisabledSelectedBorderColor( editBox, disabledSelectedItemBorderColor );
		// hilite
		GadgetButtonSetHiliteColor( editBox,hiliteColor );
		GadgetButtonSetHiliteBorderColor( editBox, hiliteBorderColor );
		GadgetButtonSetHiliteSelectedColor( editBox, hiliteSelectedItemColor );
		GadgetButtonSetHiliteSelectedBorderColor( editBox, hiliteSelectedItemBorderColor );
	}	

	GameWindow *dropDownButton = GadgetComboBoxGetDropDownButton(comboBox);
	if (dropDownButton)
	{
		// enabled
		GadgetButtonSetEnabledColor( dropDownButton, enabledColor );
		GadgetButtonSetEnabledBorderColor( dropDownButton, enabledBorderColor );
		GadgetButtonSetEnabledSelectedColor( dropDownButton, enabledSelectedItemColor );
		GadgetButtonSetEnabledSelectedBorderColor( dropDownButton, enabledSelectedItemBorderColor );
		// disabled
		GadgetButtonSetDisabledColor( dropDownButton, disabledColor );
		GadgetButtonSetDisabledBorderColor( dropDownButton, disabledBorderColor );
		GadgetButtonSetDisabledSelectedColor( dropDownButton, disabledSelectedItemColor );
		GadgetButtonSetDisabledSelectedBorderColor( dropDownButton, disabledSelectedItemBorderColor );
		// hilite
		GadgetButtonSetHiliteColor( dropDownButton,hiliteColor );
		GadgetButtonSetHiliteBorderColor( dropDownButton, hiliteBorderColor );
		GadgetButtonSetHiliteSelectedColor( dropDownButton, hiliteSelectedItemColor );
		GadgetButtonSetHiliteSelectedBorderColor( dropDownButton, hiliteSelectedItemBorderColor );
	}	

	GameWindow * listBox = GadgetComboBoxGetListBox( comboBox );
	if ( listBox )
	{
		GadgetListBoxSetColors(listBox,
													enabledColor, 
													enabledBorderColor,
													enabledSelectedItemColor, 
													enabledSelectedItemBorderColor,
													disabledColor, 
													disabledBorderColor,
													disabledSelectedItemColor, 
													disabledSelectedItemBorderColor,
													hiliteColor, 
													hiliteBorderColor,
													hiliteSelectedItemColor, 
													hiliteSelectedItemBorderColor );
	}
}  // end GadgetComboBoxSetColors

// GadgetComboBoxSetIsEditable ================================================
/** Sets up the Text Entry gadget as editable or not */
//=============================================================================
void GadgetComboBoxSetIsEditable(GameWindow *comboBox, Bool isEditable  )
{
	ComboBoxData *comboData = static_cast<ComboBoxData *>(comboBox->winGetUserData());	
	GameWindow *editBox = GadgetComboBoxGetEditBox(comboBox);
	UnsignedInt status ;
	if(!editBox)
	 return;

	comboData->isEditable = isEditable;
	if (isEditable)
	{
		status = editBox->winGetStatus();
		
		BitClear(status, WIN_STATUS_NO_INPUT);
//		BitClear(status, WIN_STATUS_NO_FOCUS);
		editBox->winSetStatus(status);
	}
	else
	{
		status = editBox->winGetStatus();
		
		BitSet(status, WIN_STATUS_NO_INPUT);
//		BitSet(status, WIN_STATUS_NO_FOCUS);
		editBox->winSetStatus(status);
	}
}//void GadgetComboBoxSetIsEditable(GameWindow *comboBox, Int maxChars )

// GadgetComboBoxSetIsAsciiOnly ==================================================
/** Get the text the Combo Box */
//=============================================================================
void GadgetComboBoxSetLettersAndNumbersOnly(GameWindow *comboBox, Bool isLettersAndNumbersOnly)
{	
	//sanity
	if(comboBox == nullptr)
		return;
	ComboBoxData *comboData = static_cast<ComboBoxData *>(comboBox->winGetUserData());

	comboData->lettersAndNumbersOnly = isLettersAndNumbersOnly;
	if(comboData->entryData)
		comboData->entryData->alphaNumericalOnly = isLettersAndNumbersOnly;

}

// GadgetComboBoxSetAsciiOnly ==================================================
/** Get the text the Combo Box */
//=============================================================================
void GadgetComboBoxSetAsciiOnly(GameWindow *comboBox, Bool isAsciiOnly  )
{
	//sanity
	if(comboBox == nullptr)
		return;
	ComboBoxData *comboData = static_cast<ComboBoxData *>(comboBox->winGetUserData());

	comboData->asciiOnly = isAsciiOnly;
	if(comboData->entryData)
		comboData->entryData->aSCIIOnly = isAsciiOnly;

}

// GadgetComboBoxSetMaxChars ==================================================
/** Get the text the Combo Box */
//=============================================================================
void GadgetComboBoxSetMaxChars( GameWindow *comboBox, Int maxChars )
{
	//sanity
	if(comboBox == nullptr)
		return;

	ComboBoxData *comboData = static_cast<ComboBoxData *>(comboBox->winGetUserData());
	comboData->maxChars = maxChars;
	comboData->entryData->maxTextLen = static_cast<Short>(maxChars);

}//void GadgetComboBoxSetMaxChars( GameWindow *comboBox, Int maxChars )

// GadgetComboBoxSetMaxDisplay ================================================
/** Sets the MaxDisplay variable to the new Max Display */
//=============================================================================
void GadgetComboBoxSetMaxDisplay( GameWindow *comboBox, Int maxDisplay )
{
	ComboBoxData *comboData = static_cast<ComboBoxData *>(comboBox->winGetUserData());
	comboData->maxDisplay = maxDisplay;

}//void GadgetComboBoxSetMaxDisplay( GameWindow *comboBox, Int maxDisplay );

// GadgetComboBoxGetText =======================================================
/** Get the text the Combo Box */
//=============================================================================
UnicodeString GadgetComboBoxGetText( GameWindow *comboBox )
{

	// sanity
	if( comboBox == nullptr )
		return UnicodeString::TheEmptyString;

	// verify that this is a combo box
	if( BitTest( comboBox->winGetStyle(), GWS_COMBO_BOX ) == FALSE )
		return UnicodeString::TheEmptyString;
	
	return GadgetTextEntryGetText( GadgetComboBoxGetEditBox(comboBox) );
}

// GadgetComboBoxSetText =======================================================
/** Set the text the Combo Box */
//=============================================================================
void GadgetComboBoxSetText( GameWindow *comboBox, UnicodeString text )
{
	if( comboBox == nullptr )
		return;

	GadgetTextEntrySetText(GadgetComboBoxGetEditBox(comboBox), text);
}

// GadgetComboBoxAddEntry =======================================================
/** Convenience wrapper function for adding an entry */
//=============================================================================
Int GadgetComboBoxAddEntry( GameWindow *comboBox, UnicodeString text, Color color )
{
	// sanity
	if( comboBox == nullptr )
		return -1;
	return static_cast<Int>(TheWindowManager->winSendSystemMsg( comboBox, GCM_ADD_ENTRY, reinterpret_cast<WindowMsgData>(&text), color ));
}
// GadgetComboBoxReset =======================================================
/** Convenience wrapper function for resetting the Combo Box entries */
//=============================================================================
void GadgetComboBoxReset( GameWindow *comboBox )
{
	// sanity
	if( comboBox == nullptr )
		return;
	// reset via system message
	TheWindowManager->winSendSystemMsg( comboBox, GCM_DEL_ALL, 0, 0 );
}
// GadgetComboBoxHideList =======================================================
/** Convenience wrapper function hiding the list */
//=============================================================================
void GadgetComboBoxHideList( GameWindow *comboBox )
{
	// sanity
	if( comboBox == nullptr )
		return;
	// reset via system message
	TheWindowManager->winSendSystemMsg( comboBox, GGM_CLOSE, 0, 0 );
}
// GadgetComboBoxSetFont =======================================================
/** Function used to set the Font of the combo box and all sub gadgets */
//=============================================================================
void GadgetComboBoxSetFont( GameWindow *comboBox, GameFont *font )
{
	// sanity
	if( comboBox == nullptr )
		return;

	// set the ListBox gadget's font
	GameWindow *listBox = GadgetComboBoxGetListBox(comboBox);
	if(listBox)
		listBox->winSetFont( font);

	// set the Text Entry gadget's font
	GameWindow *editBox = GadgetComboBoxGetEditBox(comboBox);
	if(editBox)
		editBox->winSetFont(font);

	//Need to setup the default window font
	DisplayString *dString;

		// set the font for the display strings all windows have
	dString = comboBox->winGetInstanceData()->getTextDisplayString();
	if( dString )
		dString->setFont( font );
	dString = comboBox->winGetInstanceData()->getTooltipDisplayString();
	if( dString )
		dString->setFont( font );
}

// GadgetComboBoxSetEnabledTextColors =========================================
/** Set the Enabled Text Colors for the Sub Gadgets*/
//=============================================================================
void GadgetComboBoxSetEnabledTextColors(GameWindow *comboBox, Color color, Color borderColor )
{
	// sanity
	if( comboBox == nullptr )
		return;
	
	ComboBoxData *comboBoxData = static_cast<ComboBoxData *>(comboBox->winGetUserData());
	if(comboBoxData->listBox)
		comboBoxData->listBox->winSetEnabledTextColors( color,borderColor);
	if(comboBoxData->editBox)
		comboBoxData->editBox->winSetEnabledTextColors(color,borderColor);
}
// GadgetComboBoxSetDisabledTextColors ========================================
/** Set the Disabled Text Colors for the Sub Gadgets */
//=============================================================================
void GadgetComboBoxSetDisabledTextColors(GameWindow *comboBox, Color color, Color borderColor )
{
	ComboBoxData *comboBoxData = static_cast<ComboBoxData *>(comboBox->winGetUserData());
	// sanity
	if( comboBox == nullptr )
		return;

	if(comboBoxData->listBox)
		comboBoxData->listBox->winSetDisabledTextColors( color,borderColor);
	if(comboBoxData->editBox)
		comboBoxData->editBox->winSetDisabledTextColors(color,borderColor);
}
// GadgetComboBoxSetHiliteTextColors ==========================================
/** Set the Hilite Text Colors for the Sub Gadgets */
//=============================================================================
void GadgetComboBoxSetHiliteTextColors( GameWindow *comboBox,Color color, Color borderColor )
{
	// sanity
	if( comboBox == nullptr )
		return;
	
	ComboBoxData *comboBoxData = static_cast<ComboBoxData *>(comboBox->winGetUserData());
	
	if(comboBoxData->listBox)
		comboBoxData->listBox->winSetHiliteTextColors( color,borderColor);
	if(comboBoxData->editBox)
		comboBoxData->editBox->winSetHiliteTextColors(color,borderColor);
}
// GadgetComboBoxSetIMECompositeTextColors ====================================
/** Set the IME Composite Text Colors Text Colors for the Sub Gadgets */
//=============================================================================
void GadgetComboBoxSetIMECompositeTextColors(GameWindow *comboBox, Color color, Color borderColor )
{
	// sanity
	if( comboBox == nullptr )
		return;
	
	ComboBoxData *comboBoxData = static_cast<ComboBoxData *>(comboBox->winGetUserData());

	if(comboBoxData->listBox)
		comboBoxData->listBox->winSetIMECompositeTextColors( color,borderColor);
	if(comboBoxData->editBox)
		comboBoxData->editBox->winSetIMECompositeTextColors(color,borderColor);
}

// GadgetComboBoxGetSelectedPos ===============================================
/** Convenience wrapper function for getting the selected Position */
//=============================================================================
void GadgetComboBoxGetSelectedPos( GameWindow *comboBox, Int *selectedIndex )
{
	// sanity
	if( comboBox == nullptr )
		return;

	// get selected indeces via system message
	TheWindowManager->winSendSystemMsg( comboBox, GCM_GET_SELECTION, 0, reinterpret_cast<WindowMsgData>(selectedIndex) );
}

// GadgetComboBoxSetSelectedPos ===============================================
/** Convenience wrapper function for setting the selected Position, if don't hide
		is set to true, the listbox won't be forced to hide when the Selected call is
		passed back */
//=============================================================================
void GadgetComboBoxSetSelectedPos( GameWindow *comboBox, Int selectedIndex, Bool dontHide )
{
	// sanity
	if( comboBox == nullptr )
		return;

	// get selected indeces via system message
	TheWindowManager->winSendSystemMsg( comboBox, GCM_SET_SELECTION, selectedIndex, dontHide );
}
// GadgetComboBoxSetItemData ==================================================
/** Convenience wrapper function for setting the Item data for the listbox under the combo box */
//=============================================================================
void GadgetComboBoxSetItemData( GameWindow *comboBox, Int index, void *data )
{
	if (comboBox)
		TheWindowManager->winSendSystemMsg( comboBox, GCM_SET_ITEM_DATA, index, reinterpret_cast<WindowMsgData>(data));
}
// GadgetComboBoxGetItemData ==================================================
/** Convenience wrapper function for getting the Item data from the listbox under the combo Box */
//=============================================================================
void *GadgetComboBoxGetItemData( GameWindow *comboBox, Int index )
{
	void *data = nullptr;
	
	if (comboBox)
	{
		TheWindowManager->winSendSystemMsg( comboBox, GCM_GET_ITEM_DATA, index, reinterpret_cast<WindowMsgData>(&data));
	}
	return (data);
}

// GadgetComboBoxGetLength =================================================
/** Get the list length data contained in the listboxData
	* parameter. */
//=============================================================================
Int GadgetComboBoxGetLength( GameWindow *combobox )
{
	ComboBoxData *comboboxData = static_cast<ComboBoxData *>(combobox->winGetUserData());
	if (comboboxData)
		return comboboxData->entryCount;

	return 0;
}  // end GadgetListBoxGetListLength

// Free when no visible sibling overlaps the rectangle; windows that also hold the model (frames,
// backgrounds) do not count.
static Bool isFreeNear( GameWindow *parent, GameWindow *model, Int x, Int y, Int w, Int h )
{
	Int mx, my, mw, mh;
	model->winGetPosition( &mx, &my );
	model->winGetSize( &mw, &mh );
	for( GameWindow *c = parent->winGetChild(); c; c = c->winGetNext() )
	{
		if( c == model || c->winIsHidden() )
			continue;
		Int cx, cy, cw, ch;
		c->winGetPosition( &cx, &cy );
		c->winGetSize( &cw, &ch );
		if( cx <= mx && cy <= my && cx + cw >= mx + mw && cy + ch >= my + mh )
			continue;
		if( cx < x + w && x < cx + cw && cy < y + h && y < cy + ch )
			return FALSE;
	}
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
void GadgetComboBoxFitText( GameWindow *comboBox )
{
	if( comboBox == nullptr || !BitTest( comboBox->winGetStyle(), GWS_COMBO_BOX ) || TheDisplayStringManager == nullptr
			|| TheFontLibrary == nullptr )
		return;
	GameWindow *list = GadgetComboBoxGetListBox( comboBox );
	GameWindow *edit = GadgetComboBoxGetEditBox( comboBox );
	GameFont *font = comboBox->winGetFont();
	if( list == nullptr || edit == nullptr || font == nullptr )
		return;
	Int width, height;
	edit->winGetSize( &width, &height );
	const Int available = width - width / 12 - 2;
	const Int count = GadgetListBoxGetNumEntries( list );
	DisplayString *measure = TheDisplayStringManager->newDisplayString();
	GameFont *fitted = font;
	Int size = font->pointSize;
	const Int minSize = size * 7 / 10;
	for( ;; )
	{
		measure->setFont( fitted );
		Int widest = 0;
		for( Int row = 0; row < count; ++row )
		{
			measure->setText( GadgetListBoxGetText( list, row ) );
			Int w = 0, h = 0;
			measure->getSize( &w, &h );
			if( w > widest )
				widest = w;
		}
		if( widest <= available || size <= minSize )
			break;
		GameFont *smaller = TheFontLibrary->getFont( font->nameString, --size, font->bold );
		if( smaller == nullptr )
			break;
		fitted = smaller;
	}
	TheDisplayStringManager->freeDisplayString( measure );
	if( fitted != font )
		comboBox->winSetFont( fitted );
}

// ------------------------------------------------------------------------------------------------
void GadgetComboBoxWidenToFit( GameWindow *comboBox )
{
	if( comboBox == nullptr || comboBox->winGetParent() == nullptr || !BitTest( comboBox->winGetStyle(), GWS_COMBO_BOX )
			|| TheDisplayStringManager == nullptr )
		return;
	GameWindow *list = GadgetComboBoxGetListBox( comboBox );
	GameWindow *edit = GadgetComboBoxGetEditBox( comboBox );
	GameFont *font = comboBox->winGetFont();
	if( list == nullptr || edit == nullptr || font == nullptr )
		return;

	DisplayString *measure = TheDisplayStringManager->newDisplayString();
	measure->setFont( font );
	Int widest = 0;
	const Int count = GadgetListBoxGetNumEntries( list );
	for( Int row = 0; row < count; ++row )
	{
		measure->setText( GadgetListBoxGetText( list, row ) );
		Int w = 0, h = 0;
		measure->getSize( &w, &h );
		if( w > widest )
			widest = w;
	}
	TheDisplayStringManager->freeDisplayString( measure );

	Int x, y, width, height, editWidth, editHeight, parentW, parentH;
	comboBox->winGetPosition( &x, &y );
	comboBox->winGetSize( &width, &height );
	edit->winGetSize( &editWidth, &editHeight );
	comboBox->winGetParent()->winGetSize( &parentW, &parentH );
	// the edit box needs the text plus the margin GadgetComboBoxFitText leaves
	const Int target = ( width - editWidth ) + ( widest + 2 ) * 12 / 11 + 1;
	if( target <= width )
		return;

	// right first, then left; only into room no other visible window takes, keeping the margin of CreateNear
	const Int gap = 3;
	GameWindow *parent = comboBox->winGetParent();
	Int newX = x, newWidth = width;
	while( newWidth < target && newX + newWidth + 1 + gap <= parentW
				 && isFreeNear( parent, comboBox, newX - gap, y - gap, newWidth + 1 + 2 * gap, height + 2 * gap ) )
		++newWidth;
	while( newWidth < target && newX - 1 - gap >= 0
				 && isFreeNear( parent, comboBox, newX - 1 - gap, y - gap, newWidth + 1 + 2 * gap, height + 2 * gap ) )
	{
		--newX;
		++newWidth;
	}
	if( newWidth == width )
		return;
	comboBox->winSetPosition( newX, y );
	comboBox->winSetSize( newWidth, height );
}

// ------------------------------------------------------------------------------------------------
GameWindow *GadgetComboBoxCreateNear( GameWindow *model, const char *decoratedName, const UnicodeString *entries,
	Int entryCount )
{
	if( model == nullptr || model->winGetParent() == nullptr || !BitTest( model->winGetStyle(), GWS_COMBO_BOX ) )
		return nullptr;
	const ComboBoxData *modelData = static_cast<const ComboBoxData *>(model->winGetUserData());
	if( modelData == nullptr )
		return nullptr;
	GameWindow *parent = model->winGetParent();

	Int modelX, modelY, width, height, parentW, parentH;
	model->winGetPosition( &modelX, &modelY );
	model->winGetSize( &width, &height );
	parent->winGetSize( &parentW, &parentH );

	// as wide as the entries need (same margin as GadgetComboBoxWidenToFit), at most the parent's width
	GameWindow *modelEdit = GadgetComboBoxGetEditBox( model );
	if( entries != nullptr && entryCount > 0 && modelEdit != nullptr && model->winGetFont() != nullptr
			&& TheDisplayStringManager != nullptr )
	{
		DisplayString *measure = TheDisplayStringManager->newDisplayString();
		measure->setFont( model->winGetFont() );
		Int widest = 0;
		for( Int i = 0; i < entryCount; ++i )
		{
			measure->setText( entries[i] );
			Int w = 0, h = 0;
			measure->getSize( &w, &h );
			if( w > widest )
				widest = w;
		}
		TheDisplayStringManager->freeDisplayString( measure );
		Int editWidth, editHeight;
		modelEdit->winGetSize( &editWidth, &editHeight );
		const Int needed = ( width - editWidth ) + ( widest + 2 ) * 12 / 11 + 1;
		if( needed > width )
			width = needed < parentW - 2 * 3 ? needed : parentW - 2 * 3;
	}

	// the list opens below the box, so keep a small margin around it and room on the screen below it.
	// Search order: below and to the left of the model, below and to the right, the model's row to the
	// right, then above; the first free place wins, so a layout with room below the model always gets
	// the same place.
	const Int gap = 3;
	const Int shown = entryCount > 0 && entryCount < modelData->maxDisplay ? entryCount : modelData->maxDisplay;
	GameFont *font = model->winGetFont();
	const Int listHeight = font != nullptr ? TheWindowManager->winFontHeight( font ) * shown + shown * 2 + 4 : 0;
	Int parentScreenX, parentScreenY;
	parent->winGetScreenPosition( &parentScreenX, &parentScreenY );
	const Int screenHeight = TheDisplay != nullptr ? static_cast<Int>( TheDisplay->getHeight() ) : 0;
	Int x = -1, y = -1;
	auto tryAt = [&]( Int cx, Int cy )
	{
		if( x < 0 && ( screenHeight == 0 || parentScreenY + cy + height + listHeight <= screenHeight )
				&& isFreeNear( parent, model, cx - gap, cy - gap, width + 2 * gap, height + 2 * gap ) )
		{
			x = cx;
			y = cy;
		}
	};
	for( Int cy = modelY + height + gap; cy + height + gap <= parentH && x < 0; cy += 2 )
		for( Int cx = modelX; cx >= gap && x < 0; cx -= 8 )
			tryAt( cx, cy );
	for( Int cy = modelY + height + gap; cy + height + gap <= parentH && x < 0; cy += 2 )
		for( Int cx = modelX + 8; cx + width + gap <= parentW && x < 0; cx += 8 )
			tryAt( cx, cy );
	for( Int cx = modelX + width + gap; cx + width + gap <= parentW && x < 0; cx += 8 )
		tryAt( cx, modelY );
	for( Int cy = modelY - height - gap; cy >= gap && x < 0; cy -= 2 )
		for( Int cx = modelX; cx >= gap && x < 0; cx -= 8 )
			tryAt( cx, cy );
	if( x < 0 )
	{
		// for the layout author: what occupies the parent
		DEBUG_LOG(( "GadgetComboBoxCreateNear: no room for %s in %d x %d next to %d %d %d x %d\n", decoratedName, parentW, parentH, modelX, modelY, width, height ));
		for( GameWindow *c = parent->winGetChild(); c; c = c->winGetNext() )
		{
			Int cx, cy, cw, ch;
			c->winGetPosition( &cx, &cy );
			c->winGetSize( &cw, &ch );
			DEBUG_LOG(( "  %s %d %d %d x %d%s\n", c->winGetInstanceData()->m_decoratedNameString.str(), cx, cy, cw, ch, c->winIsHidden() ? " hidden" : "" ));
		}
		return nullptr;
	}

	GameWindow *box = GadgetCloneComboBox( model, parent, decoratedName, x, y, width, height );
	if( box && model->winIsHidden() )
		box->winHide( TRUE );
	if( box && !model->winGetEnabled() )
		box->winEnable( FALSE );
	return box;
}
