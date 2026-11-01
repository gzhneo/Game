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

// GameWindowManager: making gadgets and message boxes, and their default look

//-------------------------------------------------------------------------------------------------
/** Create a Modal Message Box */
//-------------------------------------------------------------------------------------------------
GameWindow *GameWindowManager::gogoMessageBox(Int x, Int y, Int width, Int height, UnsignedShort buttonFlags,
                        UnicodeString titleString, UnicodeString bodyString,
                        GameWinMsgBoxFunc yesCallback,
                        GameWinMsgBoxFunc noCallback,
                        GameWinMsgBoxFunc okCallback,
                        GameWinMsgBoxFunc cancelCallback )
{
	return gogoMessageBox(x,y, width,height,buttonFlags,titleString,bodyString,yesCallback,noCallback,okCallback,cancelCallback, FALSE);
}

GameWindow *GameWindowManager::gogoMessageBox(Int x, Int y, Int width, Int height, UnsignedShort buttonFlags,
                        UnicodeString titleString, UnicodeString bodyString,
                        GameWinMsgBoxFunc yesCallback,
                        GameWinMsgBoxFunc noCallback,
                        GameWinMsgBoxFunc okCallback,
                        GameWinMsgBoxFunc cancelCallback, Bool useLogo )

{
	// first check to make sure we have some buttons to display
	if(buttonFlags == 0 )
	{
		return nullptr;
	}
	GameWindow *trueParent = nullptr;
	//Changed by Chris
	if(useLogo)
		trueParent = winCreateFromScript( AsciiString("Menus/QuitMessageBox.wnd") ); 
	else
		trueParent = winCreateFromScript( AsciiString("Menus/MessageBox.wnd") ); 
	//Added By Chris
	AsciiString menuName;
	if(useLogo)
		menuName.set("QuitMessageBox.wnd:");
	else
		menuName.set("MessageBox.wnd:");

	AsciiString tempName;
	GameWindow *parent = nullptr;
	
	tempName = menuName;
	tempName.concat("MessageBoxParent");
	parent = TheWindowManager->winGetWindowFromId(trueParent, TheNameKeyGenerator->nameToKey( tempName ));
	TheWindowManager->winSetModal( trueParent ); 
	TheWindowManager->winSetFocus( nullptr ); // make sure we lose focus from other windows even if we refuse focus ourselves
	TheWindowManager->winSetFocus( parent	 );

	// If the user wants the size to be different then the default
	float ratioX, ratioY = 1;

	if( width > 0 && height > 0 )
	{
		ICoord2D temp;
		//First grab the percent increase/decrease compaired to the default size
		parent->winGetSize( &temp.x, &temp.y);
		ratioX = static_cast<float>(width) / static_cast<float>(temp.x);
		ratioY = static_cast<float>(height) / static_cast<float>(temp.y);
		//Set the window's new size
		parent->winSetSize( width, height);	
	
		//Resize/reposition all the children windows based off the ratio
		GameWindow *child;
		for( child = parent->winGetChild(); child; child = child->winGetNext() )
		{
			child->winGetSize(&temp.x, &temp.y);
			temp.x =Int(temp.x * ratioX);
			temp.y =Int(temp.y * ratioY);
			child->winSetSize(temp.x, temp.y);

			child->winGetPosition(&temp.x, &temp.y);
			temp.x =Int(temp.x * ratioX);
			temp.y =Int(temp.y * ratioY);
			child->winSetPosition(temp.x, temp.y);
		}
	}
	
	// If the user wants to position the message box somewhere other then default
	if( x >= 0 && y >= 0)
		parent->winSetPosition(x, y);	
	
	// Reposition the buttons 
	Int buttonX[3], buttonY[3];
		 	
	//In the layout, buttonOk will be in the first button position
	NameKeyType buttonOkID = NAMEKEY_INVALID;
	
	tempName = menuName;
	tempName.concat("ButtonOk");
	buttonOkID = TheNameKeyGenerator->nameToKey( tempName );
	GameWindow *buttonOk = TheWindowManager->winGetWindowFromId(parent, buttonOkID);
	buttonOk->winGetPosition(&buttonX[0], &buttonY[0]);

	tempName = menuName;
	tempName.concat("ButtonYes");
	NameKeyType buttonYesID = TheNameKeyGenerator->nameToKey( tempName );
	GameWindow *buttonYes = TheWindowManager->winGetWindowFromId(parent, buttonYesID);
	//buttonNo in the second position
	tempName = menuName;
	tempName.concat("ButtonNo");
	NameKeyType buttonNoID = TheNameKeyGenerator->nameToKey(tempName);
	GameWindow *buttonNo = TheWindowManager->winGetWindowFromId(parent, buttonNoID);
	buttonNo->winGetPosition(&buttonX[1], &buttonY[1]);
	
	//and buttonCancel in the third
	tempName = menuName;
	tempName.concat("ButtonCancel");
	NameKeyType buttonCancelID = TheNameKeyGenerator->nameToKey( tempName );
	GameWindow *buttonCancel = TheWindowManager->winGetWindowFromId(parent, buttonCancelID);
	buttonCancel->winGetPosition(&buttonX[2], &buttonY[2]);
	
	//we shouldn't have button OK and Yes on the same dialog
	if((buttonFlags & (MSG_BOX_OK | MSG_BOX_YES)) == (MSG_BOX_OK | MSG_BOX_YES) )
	{
		DEBUG_ASSERTCRASH(false, ("Passed in MSG_BOX_OK and MSG_BOX_YES.  Big No No."));
	}
	
	//Position the OK button if we have one
	if( (buttonFlags & MSG_BOX_OK) == MSG_BOX_OK)
	{
		buttonOk->winSetPosition(buttonX[0], buttonY[0]);
		buttonOk->winHide(FALSE);
	}
	else if( (buttonFlags & MSG_BOX_YES) == MSG_BOX_YES)
	{
		//Position the Yes if we have one
		buttonYes->winSetPosition(buttonX[0], buttonY[0]);
		buttonYes->winHide(FALSE);
	}
	
	if((buttonFlags & (MSG_BOX_NO | MSG_BOX_CANCEL)) == (MSG_BOX_NO | MSG_BOX_CANCEL) )
	{
		//If we have both the No and Cancel button, then the no should go in the middle position
		buttonNo->winSetPosition(buttonX[1], buttonY[1]);
		buttonCancel->winSetPosition(buttonX[2], buttonY[2]);
		buttonNo->winHide(FALSE);
		buttonCancel->winHide(FALSE);
	}
	else if( (buttonFlags & MSG_BOX_NO) == MSG_BOX_NO)
	{
		//if we just have the no button, then position it in the right most spot
		buttonNo->winSetPosition(buttonX[2], buttonY[2]);
		buttonNo->winHide(FALSE);
	}
	else if( (buttonFlags & MSG_BOX_CANCEL) == MSG_BOX_CANCEL)
	{
		//else if we just have the Cancel button, well, it should always go in the right spot
		buttonCancel->winSetPosition(buttonX[2], buttonY[2]);
		buttonCancel->winHide(FALSE);
	}
		
	// Fill the text into the text boxes
	tempName = menuName;
	tempName.concat("StaticTextTitle");
	NameKeyType staticTextTitleID = TheNameKeyGenerator->nameToKey( tempName );
	GameWindow *staticTextTitle = TheWindowManager->winGetWindowFromId(parent, staticTextTitleID);
	GadgetStaticTextSetText(staticTextTitle,titleString);
	tempName = menuName;
	tempName.concat("StaticTextMessage");
	NameKeyType staticTextMessageID = TheNameKeyGenerator->nameToKey( tempName );
	GameWindow *staticTextMessage = TheWindowManager->winGetWindowFromId(parent, staticTextMessageID);
	GadgetStaticTextSetText(staticTextMessage,bodyString);

	// create a structure that will pass the functions to 
	WindowMessageBoxData *MsgBoxCallbacks = NEW WindowMessageBoxData;
	MsgBoxCallbacks->cancelCallback = cancelCallback;
	MsgBoxCallbacks->noCallback = noCallback;
	MsgBoxCallbacks->okCallback = okCallback;
	MsgBoxCallbacks->yesCallback = yesCallback;
	//pass the structure to the dialog
	trueParent->winSetUserData( MsgBoxCallbacks );
	
	// the window file is loaded here, not through WindowLayout::load: fit button texts that are too wide
	// for their buttons in this language and resolution (a long "Cancel" would wrap)
	fitLayoutTexts( trueParent );

	//make sure the dialog is showing and bring it to the top
	parent->winHide(FALSE);
	parent->winBringToTop();
	
	//Changed By Chris
	return trueParent;
}// gogoMessageBox

//-------------------------------------------------------------------------------------------------
/** Create a button GUI control */
//-------------------------------------------------------------------------------------------------
GameWindow *GameWindowManager::gogoGadgetPushButton( GameWindow *parent,
																										 UnsignedInt status,
																										 Int x, Int y,
																										 Int width, Int height,
																										 WinInstanceData *instData,
																										 GameFont *defaultFont,
																										 Bool defaultVisual )
{
	GameWindow *button;

	// we MUST have a push button style window to do this
	if( BitTest( instData->getStyle(), GWS_PUSH_BUTTON ) == FALSE )
	{

		DEBUG_LOG(( "Cann't create button gadget, instance data not button type\n" ));
		assert( 0 );
		return nullptr;

	}  // end if

	// create the button window
	button = TheWindowManager->winCreate( parent, status, 
																				x, y, width, height, 
																				GadgetPushButtonSystem,
																				instData );
	if( button == nullptr )
	{
	
		DEBUG_LOG(( "Unable to create button for push button gadget\n" ));
		assert( 0 );
		return nullptr;

	}  // end if

	// assign input function
	button->winSetInputFunc( GadgetPushButtonInput );

	//
	// assign draw function, the draw functions must actually be implemented
	// on the device level of the engine
	//
	if( BitTest( button->winGetStatus(), WIN_STATUS_IMAGE ) )
		button->winSetDrawFunc( getPushButtonImageDrawFunc() );
	else
		button->winSetDrawFunc( getPushButtonDrawFunc() );

	// set the owner to the parent, or if no parent it will be itself
	button->winSetOwner( parent );
	
	// Init the userdata to NULL
	button->winSetUserData(nullptr);

	// assign the default images/colors
	assignDefaultGadgetLook( button, defaultFont, defaultVisual );

	// assign text from label
	UnicodeString text = winTextLabelToText( instData->m_textLabelString );
	if( text.getLength() )
		GadgetButtonSetText( button, text );
	
	return button;

}  // end gogoGadgetPushButton

//-------------------------------------------------------------------------------------------------
/** Create a checkbox UI element */
//-------------------------------------------------------------------------------------------------
GameWindow *GameWindowManager::gogoGadgetCheckbox( GameWindow *parent, 
																									 UnsignedInt status,
																									 Int x, Int y, 
																									 Int width, Int height, 
																									 WinInstanceData *instData,
																									 GameFont *defaultFont,
																									 Bool defaultVisual )

{
	GameWindow *checkbox;

	// we MUST have a push button style window to do this
	if( BitTest( instData->getStyle(), GWS_CHECK_BOX ) == FALSE )
	{

		DEBUG_LOG(( "Cann't create checkbox gadget, instance data not checkbox type\n" ));
		assert( 0 );
		return nullptr;

	}  // end if

	// create the button window
	checkbox = TheWindowManager->winCreate( parent, status, 
																					x, y, width, height, 
																					GadgetCheckBoxSystem,
																					instData );
	if( checkbox == nullptr )
	{
	
		DEBUG_LOG(( "Unable to create checkbox window\n" ));
		assert( 0 );
		return nullptr;

	}  // end if

	// assign input function
	checkbox->winSetInputFunc( GadgetCheckBoxInput );

	//
	// assign draw function, the draw functions must actually be implemented
	// on the device level of the engine
	//
	if( BitTest( checkbox->winGetStatus(), WIN_STATUS_IMAGE ) )
		checkbox->winSetDrawFunc( getCheckBoxImageDrawFunc() );
	else
		checkbox->winSetDrawFunc( getCheckBoxDrawFunc() );

	// set the owner to the parent, or if no parent it will be itself
	checkbox->winSetOwner( parent );

	// assign the default images/colors
	assignDefaultGadgetLook( checkbox, defaultFont, defaultVisual );

	// assign text from label
	UnicodeString text = winTextLabelToText( instData->m_textLabelString );
	if( text.getLength() )
		GadgetCheckBoxSetText( checkbox, text );

	return checkbox;

}  // end gogoGadgetCheckbox

//-------------------------------------------------------------------------------------------------
/** Create a radio button GUI element */
//-------------------------------------------------------------------------------------------------
GameWindow *GameWindowManager::gogoGadgetRadioButton( GameWindow *parent, 
																										  UnsignedInt status,
																										  Int x, Int y, 
																										  Int width, Int height, 
																										  WinInstanceData *instData,
																											RadioButtonData *rData,
																										  GameFont *defaultFont,
																										  Bool defaultVisual )

{
	GameWindow *radioButton;
	RadioButtonData *radioData;

	// we MUST have a push button style window to do this
	if( BitTest( instData->getStyle(), GWS_RADIO_BUTTON ) == FALSE )
	{

		DEBUG_LOG(( "Cann't create radioButton gadget, instance data not radioButton type\n" ));
		assert( 0 );
		return nullptr;

	}  // end if

	// create the button window
	radioButton = TheWindowManager->winCreate( parent, status, 
																					   x, y, width, height, 
																						 GadgetRadioButtonSystem,
																						 instData );
	if( radioButton == nullptr )
	{
	
		DEBUG_LOG(( "Unable to create radio button window\n" ));
		assert( 0 );
		return nullptr;

	}  // end if

	// allocate and store the radio button user data
	radioData = NEW RadioButtonData;
	memcpy( radioData, rData, sizeof( RadioButtonData ) );
	radioButton->winSetUserData( radioData );

	// assign input function
	radioButton->winSetInputFunc( GadgetRadioButtonInput );

	//
	// assign draw function, the draw functions must actually be implemented
	// on the device level of the engine
	//
	if( BitTest( radioButton->winGetStatus(), WIN_STATUS_IMAGE ) )
		radioButton->winSetDrawFunc( getRadioButtonImageDrawFunc() );
	else
		radioButton->winSetDrawFunc( getRadioButtonDrawFunc() );

	// set the owner to the parent, or if no parent it will be itself
	radioButton->winSetOwner( parent );

	// assign the default images/colors
	assignDefaultGadgetLook( radioButton, defaultFont, defaultVisual );

	// assign text from label
	UnicodeString text = winTextLabelToText( instData->m_textLabelString );
	if( text.getLength() )
		GadgetRadioSetText( radioButton, text );

	return radioButton;

}  // end gogoGadgetRadioButton

//-------------------------------------------------------------------------------------------------
/** Create a tab control GUI element */
//-------------------------------------------------------------------------------------------------
GameWindow *GameWindowManager::gogoGadgetTabControl( GameWindow *parent, 
																										  UnsignedInt status,
																										  Int x, Int y, 
																										  Int width, Int height, 
																										  WinInstanceData *instData,
																											TabControlData *tData,
																										  GameFont *defaultFont,
																										  Bool defaultVisual )

{
	GameWindow *tabControl;
	TabControlData *tabData;

	// we MUST have a tab control style window to do this
	if( BitTest( instData->getStyle(), GWS_TAB_CONTROL ) == FALSE )
	{

		DEBUG_LOG(( "Cann't create tabControl gadget, instance data not tabControl type\n" ));
		assert( 0 );
		return nullptr;

	}  // end if

	// create the tab control window
	tabControl = TheWindowManager->winCreate( parent, status, 
																					   x, y, width, height, 
																						 GadgetTabControlSystem,
																						 instData );
	if( tabControl == nullptr )
	{
	
		DEBUG_LOG(( "Unable to create tab control window\n" ));
		assert( 0 );
		return nullptr;

	}  // end if

	// allocate and store the tab control user data
	tabData = NEW TabControlData;
	memcpy( tabData, tData, sizeof( TabControlData ) );
	tabControl->winSetUserData( tabData );

	GadgetTabControlComputeTabRegion( tabControl );
	GadgetTabControlCreateSubPanes( tabControl );
	GadgetTabControlShowSubPane( tabControl, 0 );

	// assign input function
	tabControl->winSetInputFunc( GadgetTabControlInput );

	//
	// assign draw function, the draw functions must actually be implemented
	// on the device level of the engine
	//
	if( BitTest( tabControl->winGetStatus(), WIN_STATUS_IMAGE ) )
		tabControl->winSetDrawFunc( getTabControlImageDrawFunc() );
	else
		tabControl->winSetDrawFunc( getTabControlDrawFunc() );

	// set the owner to the parent, or if no parent it will be itself
	tabControl->winSetOwner( parent );

	// assign the default images/colors
	assignDefaultGadgetLook( tabControl, defaultFont, defaultVisual );

	return tabControl;

}  // end gogoGadgetTabControl

//-------------------------------------------------------------------------------------------------
/** Create a list box GUI control */
//-------------------------------------------------------------------------------------------------
GameWindow *GameWindowManager::gogoGadgetListBox( GameWindow *parent, 
																									UnsignedInt status,
										                              Int x, Int y, 
																									Int width, Int height,
																									WinInstanceData *instData, 
																									ListboxData *listboxDataTemplate,
																								  GameFont *defaultFont,
																								  Bool defaultVisual )

{
  GameWindow *listbox;
  ListboxData *listboxData;
	Bool title = FALSE;

	// we MUST have a push button style window to do this
	if( BitTest( instData->getStyle(), GWS_SCROLL_LISTBOX ) == FALSE )
	{

		DEBUG_LOG(( "Cann't create listbox gadget, instance data not listbox type\n" ));
		assert( 0 );
		return nullptr;

	}  // end if

	// create the listbox
  listbox = winCreate( parent, status, x, y, width, height, 
											 GadgetListBoxSystem, instData );
	
	if( listbox == nullptr )
	{

		DEBUG_LOG(( "Unable to create listbox window\n" ));
		assert( 0 );
		return nullptr;

	}  // end if

	// allocate the listbox data, copy template data over and set it into box
	listboxData = NEW ListboxData;
	DEBUG_ASSERTCRASH(listboxDataTemplate, ("listboxDataTemplate not initialized"));
	memcpy( listboxData, listboxDataTemplate, sizeof( ListboxData ) );

  // Add the list box data struct to the window's class data
  listbox->winSetUserData( listboxData );

	// set the owner to the parent, or if no parent it will be itself
	listbox->winSetOwner( parent );

	// Adjust for list title if present
	if( instData->getTextLength() )
		title = TRUE;

	// Set up list box redraw callbacks
	if( BitTest( listbox->winGetStatus(), WIN_STATUS_IMAGE ))
		listbox->winSetDrawFunc( getListBoxImageDrawFunc() );
	else
		listbox->winSetDrawFunc( getListBoxDrawFunc() );

	// Set up list box input callbacks
	if( listboxData->multiSelect )
		listbox->winSetInputFunc( GadgetListBoxMultiInput );
	else
		listbox->winSetInputFunc( GadgetListBoxInput );


	
	//
	// allocate and set the list length, note that setting the list length will
	// automatically allocate the selection entries that are needed for multi
	// select list boxes etc.
	//
	Int length = listboxData->listLength;
	listboxData->listLength = 0;  // hacky!
	GadgetListBoxSetListLength( listbox, length );

	// store display height
	listboxData->displayHeight = static_cast<Short>(height);

	if( title )
	{
		const Int fontHeight = winFontHeight( instData->getFont() );
		listboxData->displayHeight = static_cast<Short>(listboxData->displayHeight - fontHeight);
	}

  // Set display position to the top of the list
  listboxData->displayPos = 0;
  listboxData->selectPos = -1;
	listboxData->doubleClickTime = 0;
  listboxData->insertPos = 0;
  listboxData->endPos = 0;
	listboxData->totalHeight = 0;

	// if this listbox has multiple selections prep it
	//if( listboxData->multiSelect )
	//	GadgetListBoxAddMultiSelect( listbox );

	// If ScrollBar was requested ... create it.
	if( listboxData->scrollBar )
		GadgetListboxCreateScrollbar( listbox );
	//
	// Setup listbox Columns
	//
	if( listboxData->columns == 1 )
	{
		listboxData->columnWidth = NEW Int[1];
		listboxData->columnWidth[0] = width;
		if( listboxData->slider )
		{
			ICoord2D sliderSize;

			listboxData->slider->winGetSize( &sliderSize.x, &sliderSize.y );
			listboxData->columnWidth[0] -= (sliderSize.x + 2);

		}  // end if
	}// if
	else
	{
		if( !listboxData->columnWidthPercentage )
			return nullptr;
		listboxData->columnWidth = NEW Int[listboxData->columns];
		if(!listboxData->columnWidth)
			return nullptr;

		Int totalWidth = width;
		if( listboxData->slider )
		{
			ICoord2D sliderSize;

			listboxData->slider->winGetSize( &sliderSize.x, &sliderSize.y );
			totalWidth -= (sliderSize.x + 2);

		}  // end if
		for(Int i = 0; i < listboxData->columns; i++ )
		{
			listboxData->columnWidth[i] = listboxData->columnWidthPercentage[i] * totalWidth / 100;
		}// for
	}// else
	// assign the default images/colors
	assignDefaultGadgetLook( listbox, defaultFont, defaultVisual );

  return listbox;

}  // end gogoGadgetListBox

//-------------------------------------------------------------------------------------------------
/** Does all generic window creation, calls appropriate slider create
	* function to set up slider-specific data */
//-------------------------------------------------------------------------------------------------
GameWindow *GameWindowManager::gogoGadgetSlider( GameWindow *parent, 
																								 UnsignedInt status,
																								 Int x, Int y, 
																								 Int width, Int height,
																								 WinInstanceData *instData, 
																								 SliderData *sliderData,
																								 GameFont *defaultFont,
																								 Bool defaultVisual )

{
	GameWindow *slider;
	GameWindow *button;
	SliderData *data;

	//
	// All sliders need to have the tab stop status in order for
	// the focus chain to work correctly.
	//
	BitSet( status, WIN_STATUS_TAB_STOP );

	if( BitTest( instData->getStyle(), GWS_HORZ_SLIDER ) ) 
	{

		slider = winCreate( parent, status, x, y, width, height, 
												GadgetHorizontalSliderSystem, instData );

		// Set up horizontal slider callbacks
		slider->winSetInputFunc( GadgetHorizontalSliderInput );
		if( BitTest( slider->winGetStatus(), WIN_STATUS_IMAGE ) )
			slider->winSetDrawFunc( getHorizontalSliderImageDrawFunc() );
		else
			slider->winSetDrawFunc( getHorizontalSliderDrawFunc() );

	}  // end if
	else if ( BitTest( instData->getStyle(), GWS_VERT_SLIDER ) ) 
	{

		slider = winCreate( parent, status, x, y, width, height, 
												GadgetVerticalSliderSystem, instData );

		// Set up vertical slider callbacks
		slider->winSetInputFunc( GadgetVerticalSliderInput );
		
		if( BitTest( slider->winGetStatus(), WIN_STATUS_IMAGE ) && !(parent && BitTest(parent->winGetStyle(), GWS_SCROLL_LISTBOX)))
			slider->winSetDrawFunc( getVerticalSliderImageDrawFunc() );
		else
			slider->winSetDrawFunc( getVerticalSliderDrawFunc() );

	}  // end else if
	else 
	{

		DEBUG_LOG(( "gogoGadgetSlider warning: unrecognized slider style.\n" ));
		assert( 0 );
		return nullptr;
		
	}  // end else

	// sanity
	if( slider == nullptr )
	{

		DEBUG_LOG(( "Unable to create slider control window\n" ));
		assert( 0 );
		return nullptr;

	}  // end if

	// set the owner to the parent, or if no parent it will be itself
	slider->winSetOwner( parent );

	// create the slider thumb button
	WinInstanceData buttonInstData;
	UnsignedInt statusFlags = status | WIN_STATUS_ENABLED | WIN_STATUS_DRAGABLE;

	buttonInstData.init();

	// if parent is hidden we don't need to be.
	BitClear( statusFlags, WIN_STATUS_HIDDEN );

	buttonInstData.m_owner = slider;
	buttonInstData.m_style = GWS_PUSH_BUTTON;

	// if slider tracks, so will this sub control
	if( BitTest( instData->getStyle(), GWS_MOUSE_TRACK ) )
		BitSet( buttonInstData.m_style, GWS_MOUSE_TRACK );

	if( BitTest( instData->getStyle(), GWS_HORZ_SLIDER ) )
		button = gogoGadgetPushButton( slider, statusFlags, 0, HORIZONTAL_SLIDER_THUMB_POSITION,
											 						 HORIZONTAL_SLIDER_THUMB_WIDTH, HORIZONTAL_SLIDER_THUMB_HEIGHT, &buttonInstData, nullptr, TRUE );
	else
		button = gogoGadgetPushButton( slider, statusFlags, 0, 0,
																	 width, width+1, &buttonInstData, nullptr, TRUE );

	// Protect against divide by zero
	if( sliderData->maxVal == sliderData->minVal )
		sliderData->maxVal = sliderData->minVal + 1;

	if( BitTest( instData->getStyle(), GWS_HORZ_SLIDER ) ) 
	{
		sliderData->numTicks = static_cast<float>(width - HORIZONTAL_SLIDER_THUMB_WIDTH) /
													 static_cast<float>(sliderData->maxVal - sliderData->minVal);
	} else 
	{
		sliderData->numTicks = static_cast<float>(height - GADGET_SIZE) /
													 static_cast<float>(sliderData->maxVal - sliderData->minVal);
	}

	data = NEW SliderData;
	memcpy( data, sliderData, sizeof(SliderData) );

	// Add the slider data struct to the window's class data
	slider->winSetUserData( data );

	// assign the default images/colors
	assignDefaultGadgetLook( slider, defaultFont, defaultVisual );

	return slider;

}  // end gogoGadgetSlider

//-------------------------------------------------------------------------------------------------
/** Create a Combo Box GUI element */
//-------------------------------------------------------------------------------------------------
GameWindow *GameWindowManager::gogoGadgetComboBox( GameWindow *parent, 
																									UnsignedInt status,
										                              Int x, Int y, 
																									Int width, Int height,
																									WinInstanceData *instData, 
																									ComboBoxData *comboBoxDataTemplate,
																								  GameFont *defaultFont,
																								  Bool defaultVisual )
{
  GameWindow *comboBox;
  ComboBoxData *comboBoxData;
	Bool title = FALSE;

	// we MUST have a push button style window to do this
	if( BitTest( instData->getStyle(), GWS_COMBO_BOX) == FALSE )
	{

		DEBUG_LOG(( "Cann't create ComboBox gadget, instance data not ComboBox type\n" ));
		assert( 0 );
		return nullptr;

	}  // end if

	// create the listbox
  comboBox = winCreate( parent, status, x, y, width, height, 
											 GadgetComboBoxSystem, instData );
	
	if( comboBox == nullptr )
	{

		DEBUG_LOG(( "Unable to create ComboBox window\n" ));
		assert( 0 );
		return nullptr;

	}  // end if


// begin here
	// allocate the listbox data, copy template data over and set it into box
	comboBoxData = NEW ComboBoxData;
	memcpy( comboBoxData, comboBoxDataTemplate, sizeof( ComboBoxData ) );

  // Add the list box data struct to the window's class data
  comboBox->winSetUserData( comboBoxData );

	// set the owner to the parent, or if no parent it will be itself
	comboBox->winSetOwner( parent );

	// Adjust for list title if present
	if( instData->getTextLength() )
		title = TRUE;

	// Set up list box redraw callbacks
	if( BitTest( comboBox->winGetStatus(), WIN_STATUS_IMAGE ))
		comboBox->winSetDrawFunc( getComboBoxImageDrawFunc() );
	else
		comboBox->winSetDrawFunc( getComboBoxDrawFunc() );

	// Set up list box input callbacks
	comboBox->winSetInputFunc( GadgetComboBoxInput );

	//Create the windows that make up 


	WinInstanceData winInstData;
	Int buttonWidth, buttonHeight;
	Int fontHeight;
	Int top;
	Int bottom;


	// do we have a title
	if( comboBox->winGetTextLength() )
		title = TRUE;

	// remove unwanted status bits.
	status &= ~(WIN_STATUS_BORDER | WIN_STATUS_HIDDEN);

	fontHeight = TheWindowManager->winFontHeight( comboBox->winGetFont() );
	top = title ? (fontHeight + 1):0;
	bottom = title ? (height - (fontHeight + 1)):height;

	// intialize instData
	winInstData.init();

	// size of button
	buttonWidth = 21;
	buttonHeight = 22;

	// ----------------------------------------------------------------------
	// Create Drop Down Button
	// ----------------------------------------------------------------------

	winInstData.m_owner = comboBox;
	winInstData.m_style = GWS_PUSH_BUTTON;

	// if listbox tracks, so will this sub control
	if( BitTest( comboBox->winGetStyle(), GWS_MOUSE_TRACK ) )
		BitSet( winInstData.m_style, GWS_MOUSE_TRACK );
	
	comboBoxData->dropDownButton =
		 TheWindowManager->gogoGadgetPushButton( comboBox,
																						 status | WIN_STATUS_ACTIVE | WIN_STATUS_ENABLED,
																						 width - buttonWidth, 0,
																						 buttonWidth, height,
																						 &winInstData, nullptr, TRUE );
	comboBoxData->dropDownButton->winSetTooltipFunc(comboBox->winGetTooltipFunc());
	comboBoxData->dropDownButton->winSetTooltip(instData->getTooltipText());
	comboBoxData->dropDownButton->setTooltipDelay(comboBox->getTooltipDelay());
	// ----------------------------------------------------------------------
	// Create text entry
	// ----------------------------------------------------------------------
	UnsignedInt statusTextEntry;
	winInstData.init();
	
	winInstData.m_owner = comboBox;
  winInstData.m_style |= GWS_ENTRY_FIELD;
	winInstData.m_textLabelString = "Entry";
	if( BitTest( comboBox->winGetStyle(), GWS_MOUSE_TRACK ) )
		BitSet( winInstData.m_style, GWS_MOUSE_TRACK );
	if( comboBoxData->isEditable)
	{
		statusTextEntry = status;
	}
	else
	{
		statusTextEntry = status | WIN_STATUS_NO_INPUT ;//| WIN_STATUS_NO_FOCUS;
		comboBoxData->entryData->drawTextFromStart = TRUE;
	}
  comboBoxData->editBox = TheWindowManager->gogoGadgetTextEntry( comboBox, statusTextEntry ,
																										 0,0 , 
																										width - buttonWidth , height ,
																										&winInstData, comboBoxData->entryData, 
																										winInstData.m_font, FALSE );
	comboBoxData->editBox->winSetTooltipFunc(comboBox->winGetTooltipFunc());
	comboBoxData->editBox->winSetTooltip(instData->getTooltipText());
	comboBoxData->editBox->setTooltipDelay(comboBox->getTooltipDelay());

	delete (comboBoxData->entryData);
	comboBoxData->entryData = static_cast<EntryData *>(comboBoxData->editBox->winGetUserData());
	// ----------------------------------------------------------------------
	// Create list box
	// ----------------------------------------------------------------------
	winInstData.init();
	
	winInstData.m_owner = comboBox;
  if( BitTest( comboBox->winGetStyle(), GWS_MOUSE_TRACK ) )
		BitSet( winInstData.m_style, GWS_MOUSE_TRACK );
	BitSet( winInstData.m_style, WIN_STATUS_HIDDEN );
  winInstData.m_style |= GWS_SCROLL_LISTBOX; 
	status &= ~(WIN_STATUS_IMAGE);
  comboBoxData->listBox = TheWindowManager->gogoGadgetListBox( comboBox, status | WIN_STATUS_ABOVE | WIN_STATUS_ONE_LINE, 0, height, 
																								width, height,
																								&winInstData, comboBoxData->listboxData, 
																								winInstData.m_font, FALSE );
	comboBoxData->listBox->winHide(TRUE);
	delete(comboBoxData->listboxData);
	comboBoxData->listboxData = static_cast<ListboxData *>(comboBoxData->listBox->winGetUserData());

	comboBoxData->listBox->winSetTooltipFunc(comboBox->winGetTooltipFunc());
	comboBoxData->listBox->winSetTooltip(instData->getTooltipText());
	comboBoxData->listBox->setTooltipDelay(comboBox->getTooltipDelay());
	GadgetListBoxSetAudioFeedback(comboBoxData->listBox, TRUE);

	// Initialize the ComboBox's variables and the controls with them
	GadgetComboBoxSetIsEditable(comboBox, comboBoxData->isEditable);
	GadgetComboBoxSetMaxChars( comboBox, comboBoxData->maxChars );
	GadgetComboBoxSetMaxDisplay( comboBox, comboBoxData->maxDisplay );

	//Initialize the control's text colors
	Color color, border;

	color = comboBox->winGetEnabledTextColor();
	border = comboBox->winGetEnabledTextBorderColor();
	if(comboBoxData->listBox)
		comboBoxData->listBox->winSetEnabledTextColors( color,border);
	if(comboBoxData->editBox)
		comboBoxData->editBox->winSetEnabledTextColors(color,border);

	color = comboBox->winGetDisabledTextColor();
	border = comboBox->winGetDisabledTextBorderColor();
	if(comboBoxData->listBox)
		comboBoxData->listBox->winSetDisabledTextColors( color,border);
	if(comboBoxData->editBox)
		comboBoxData->editBox->winSetDisabledTextColors(color,border);


	color = comboBox->winGetHiliteTextColor();
	border = comboBox->winGetHiliteTextBorderColor();
	if(comboBoxData->listBox)
		comboBoxData->listBox->winSetHiliteTextColors( color,border);
	if(comboBoxData->editBox)
		comboBoxData->editBox->winSetHiliteTextColors(color,border);

	comboBoxData->dontHide = FALSE;

	// assign the default images/colors
	assignDefaultGadgetLook( comboBox, defaultFont, defaultVisual );

  return comboBox;

}

//-------------------------------------------------------------------------------------------------
/** Create a progress bar GUI element */
//-------------------------------------------------------------------------------------------------
GameWindow *GameWindowManager::gogoGadgetProgressBar( GameWindow *parent, 
																										  UnsignedInt status,
																										  Int x, Int y, 
																										  Int width, Int height, 
																										  WinInstanceData *instData,
																										  GameFont *defaultFont,
																										  Bool defaultVisual )

{
	GameWindow *progressBar;

	// we MUST have a push button style window to do this
	if( BitTest( instData->getStyle(), GWS_PROGRESS_BAR ) == FALSE )
	{

		DEBUG_LOG(( "Cann't create progressBar gadget, instance data not progressBar type\n" ));
		assert( 0 );
		return nullptr;

	}  // end if

	// create the button window
	progressBar = TheWindowManager->winCreate( parent, status, 
																					   x, y, width, height, 
																						 GadgetProgressBarSystem,
																						 instData );
	if( progressBar == nullptr )
	{

		DEBUG_LOG(( "Unable to create progress bar control\n" ));
		assert( 0 );
		return nullptr;

	}  // end if

	//
	// assign draw function, the draw functions must actually be implemented
	// on the device level of the engine
	//
	if( BitTest( progressBar->winGetStatus(), WIN_STATUS_IMAGE ) )
		progressBar->winSetDrawFunc( getProgressBarImageDrawFunc() );
	else
		progressBar->winSetDrawFunc( getProgressBarDrawFunc() );

	// set the owner to the parent, or if no parent it will be itself
	progressBar->winSetOwner( parent );

	// assign the default images/colors
	assignDefaultGadgetLook( progressBar, defaultFont, defaultVisual );

	return progressBar;

}  // end gogoGadgetProgressBar

//-------------------------------------------------------------------------------------------------
/** Does all generic window creation, calls appropriate text field create
	* function to set up specific data */
//-------------------------------------------------------------------------------------------------
GameWindow *GameWindowManager::gogoGadgetStaticText( GameWindow *parent, 
																										 UnsignedInt status,
																										 Int x, Int y, 
																										 Int width, Int height,
																										 WinInstanceData *instData, 
																										 TextData *textData,
																										 GameFont *defaultFont,
																										 Bool defaultVisual )

{
  GameWindow *textWin;
  TextData *data;

	// Static Text can not be a Tab Stop
	BitClear( instData->m_style, GWS_TAB_STOP );

  if( BitTest( instData->getStyle(), GWS_STATIC_TEXT ) ) 
	{
    textWin = winCreate( parent, status, x, y, width, height, 
												 GadgetStaticTextSystem, instData );
  } 
	else 
	{
    DEBUG_LOG(( "gogoGadgetText warning: unrecognized text style.\n" ));
    return nullptr;
  }
  
  if( textWin != nullptr )
	{

		// set the owner to the parent, or if no parent it will be itself
		textWin->winSetOwner( parent );

		// assign callbacks
		textWin->winSetInputFunc( GadgetStaticTextInput );
		if( BitTest( textWin->winGetStatus(), WIN_STATUS_IMAGE ) )
			textWin->winSetDrawFunc( getStaticTextImageDrawFunc() );
		else
			textWin->winSetDrawFunc( getStaticTextDrawFunc() );

    data = NEW TextData;
		assert( textData != nullptr );
    memcpy( data, textData, sizeof(TextData) );

		// allocate a display string for the tet
		data->text = TheDisplayStringManager->newDisplayString();
		// set whether or not we center the wrapped text
		data->text->setWordWrapCentered( BitTest( instData->getStatus(), WIN_STATUS_WRAP_CENTERED ));
    // Add the entry field data struct to the window's class data
    textWin->winSetUserData( data );

		// assign the default images/colors
		assignDefaultGadgetLook( textWin, defaultFont, defaultVisual );

		// assign text from label
		UnicodeString text = winTextLabelToText( instData->m_textLabelString );
		if( text.getLength() )
			GadgetStaticTextSetText( textWin, text );

  }  // end if

  return textWin;

}  // end gogoGadetStaticText

//-------------------------------------------------------------------------------------------------
/** Does all generic window creation, calls appropriate entry field create
	* function to set up specific data */
//-------------------------------------------------------------------------------------------------
GameWindow *GameWindowManager::gogoGadgetTextEntry( GameWindow *parent, 
																										UnsignedInt status,
																										Int x, Int y, 
																										Int width, Int height,
																										WinInstanceData *instData, 
																										EntryData *entryData,
																										GameFont *defaultFont,
																										Bool defaultVisual )

{
	GameWindow *entry;
	EntryData *data;

	if( BitTest( instData->getStyle(), GWS_ENTRY_FIELD ) == FALSE )
	{

		DEBUG_LOG(( "Unable to create text entry, style not entry type\n" ));
		assert( 0 );
		return nullptr;

	}  // end if

	// create the window
	entry = winCreate( parent, status, x, y, width, height, 
										 GadgetTextEntrySystem, instData );
	if( entry == nullptr )
	{

		DEBUG_LOG(( "Unable to create text entry window\n" ));
		assert( 0 );
		return nullptr;

	}  // end if

	// set owner of this control
	entry->winSetOwner( parent );

	// assign callbacks
	entry->winSetInputFunc( GadgetTextEntryInput );
	if( BitTest( entry->winGetStatus(), WIN_STATUS_IMAGE ) )
		entry->winSetDrawFunc( getTextEntryImageDrawFunc() );
	else
		entry->winSetDrawFunc( getTextEntryDrawFunc() );

	// zero entry data
//	memset( entryData->text, 0, ENTRY_TEXT_LEN );
//	memset( entryData->constructText, 0, ENTRY_TEXT_LEN );

	// initialize character positions, legths etc
	if( entryData->text )
		entryData->charPos = static_cast<UnsignedShort>(entryData->text->getTextLength());
	else
		entryData->charPos = 0;
	entryData->conCharPos = 0;
	entryData->receivedUnichar = FALSE;
	if( entryData->maxTextLen >= ENTRY_TEXT_LEN )
		entryData->maxTextLen = ENTRY_TEXT_LEN;

	// allocate entry data
	data = NEW EntryData;

	// copy over data control
	memcpy( data, entryData, sizeof(EntryData) );

	// allocate new text display string
	data->text = TheDisplayStringManager->newDisplayString();
	data->sText = TheDisplayStringManager->newDisplayString();
	data->constructText = TheDisplayStringManager->newDisplayString();

	// set the max for the text lengths
//	data->text->allocateFixed( ENTRY_TEXT_LEN );
//	data->sText->allocateFixed( ENTRY_TEXT_LEN );

	// do any real display string copies
	if( entryData->text )
		data->text->setText( entryData->text->getText() );
	if( entryData->sText )
		data->sText->setText( entryData->sText->getText() );

	// set data into window
	entry->winSetUserData( data );

	// asian languages get to have list box kanji character completion
	data->constructList = nullptr;
	if( OurLanguage == LANGUAGE_ID_KOREAN || 
			OurLanguage == LANGUAGE_ID_JAPANESE )
	{
		// we need to create the construct listbox
		WinInstanceData boxInstData;
		ListboxData lData;

			// intialize instData
		boxInstData.init();

		// define display region
		memset( &lData, 0, sizeof(ListboxData) );
		lData.listLength = 128;
		lData.autoScroll = FALSE;
		lData.scrollIfAtEnd = FALSE;
		lData.autoPurge = TRUE;
		lData.scrollBar = TRUE;
		lData.multiSelect = FALSE;
		lData.columns = 1;
		lData.columnWidth = nullptr;

		boxInstData.m_style = GWS_SCROLL_LISTBOX | GWS_MOUSE_TRACK;

		data->constructList = gogoGadgetListBox( nullptr, 
																						 WIN_STATUS_ABOVE | 
																						 WIN_STATUS_HIDDEN |
																						 WIN_STATUS_NO_FOCUS | 
																						 WIN_STATUS_ONE_LINE,
																						 0, height, 
																						 110, 119, 
																						 &boxInstData, 
																						 &lData, 
																						 nullptr, 
																						 TRUE );

		if( data->constructList == nullptr )
		{

			DEBUG_LOG(( "gogoGadgetEntry warning: Failed to create listbox.\n" ));
			assert( 0 );
			winDestroy( entry );
			return nullptr;

		}  // end if

	}  // end, korean or japanese

	// assign the default images/colors
	assignDefaultGadgetLook( entry, defaultFont, defaultVisual );

	// assign text from label
	UnicodeString text = winTextLabelToText( instData->m_textLabelString );
	if( text.getLength() )
		GadgetTextEntrySetText( entry, text );

	return entry;

}  // end gogoGadgetTextEntry

//-------------------------------------------------------------------------------------------------
/** Use this method to assign the default images/colors to gadgets as 
	* they area created */
//-------------------------------------------------------------------------------------------------
void GameWindowManager::assignDefaultGadgetLook( GameWindow *gadget,
																								 GameFont *defaultFont,
																								 Bool assignVisual )
{
	UnsignedByte alpha = 255;
	static Color red				= TheWindowManager->winMakeColor( 255,   0,   0, alpha );
	static Color darkRed		= TheWindowManager->winMakeColor( 128,   0,   0, alpha );
	static Color lightRed		= TheWindowManager->winMakeColor( 255, 128, 128, alpha );
	static Color green			= TheWindowManager->winMakeColor(   0, 255,   0, alpha );
	static Color darkGreen	= TheWindowManager->winMakeColor(   0, 128,   0, alpha );
	static Color lightGreen	= TheWindowManager->winMakeColor( 128, 255, 128, alpha );
	static Color blue				= TheWindowManager->winMakeColor(   0,   0, 255, alpha );
	static Color darkBlue		= TheWindowManager->winMakeColor(   0,   0, 128, alpha );
	static Color lightBlue	= TheWindowManager->winMakeColor( 128, 128, 255, alpha );
	static Color purple			= TheWindowManager->winMakeColor( 255,   0, 255, alpha );
	static Color darkPurple	= TheWindowManager->winMakeColor( 128,   0, 128, alpha );
	static Color lightPurple= TheWindowManager->winMakeColor( 255, 128, 255, alpha );
	static Color yellow			= TheWindowManager->winMakeColor( 255, 255,   0, alpha );
	static Color darkYellow	= TheWindowManager->winMakeColor( 128, 128,   0, alpha );
	static Color lightYellow= TheWindowManager->winMakeColor( 255, 255, 128, alpha );
	static Color cyan				= TheWindowManager->winMakeColor(   0, 255, 255, alpha );
	static Color darkCyan		= TheWindowManager->winMakeColor(  64, 128, 128, alpha );
	static Color lightCyan	= TheWindowManager->winMakeColor( 128, 255, 255, alpha );
	static Color gray				= TheWindowManager->winMakeColor( 128, 128, 128, alpha );
	static Color darkGray		= TheWindowManager->winMakeColor(  64,  64,  64, alpha );
	static Color lightGray	= TheWindowManager->winMakeColor( 192, 192, 192, alpha );
	static Color black			= TheWindowManager->winMakeColor(   0,   0,   0, alpha );
	static Color white			= TheWindowManager->winMakeColor( 254, 254, 254, alpha );
	static Color enabledText					= white;
	static Color enabledTextBorder		= darkGray;
	static Color disabledText					= darkGray;
	static Color disabledTextBorder		= black;
	static Color hiliteText						= lightBlue;
	static Color hiliteTextBorder			= blue;
	static Color imeCompositeText				= green;
	static Color imeCompositeTextBorder	= blue;
	WinInstanceData *instData;
	
	// sanity
	if( gadget == nullptr )
		return;

	// get instance data
	instData = gadget->winGetInstanceData();

	// set default font
	if( defaultFont )
		gadget->winSetFont( defaultFont );
	else
	{
		if (TheGlobalLanguageData && TheGlobalLanguageData->m_defaultWindowFont.name.isNotEmpty())
		{		gadget->winSetFont( TheWindowManager->winFindFont(
				TheGlobalLanguageData->m_defaultWindowFont.name,
				TheGlobalLanguageData->m_defaultWindowFont.size,
				TheGlobalLanguageData->m_defaultWindowFont.bold) );
		}
		else
			gadget->winSetFont( TheWindowManager->winFindFont( AsciiString("Times New Roman"), 14, FALSE ) );
	}
	
	// if we don't want to assign default colors/images get out of here
	if( assignVisual == FALSE )
		return;

	// create images for the correct gadget type
	if( BitTest( instData->getStyle(), GWS_PUSH_BUTTON ) )
	{

		// enabled background
		GadgetButtonSetEnabledImage( gadget, winFindImage( "PushButtonEnabled" ) );
		GadgetButtonSetEnabledColor( gadget, red );
		GadgetButtonSetEnabledBorderColor( gadget, lightRed );
		// enabled selected button
		GadgetButtonSetEnabledSelectedImage( gadget, winFindImage( "PushButtonEnabledSelected" ) );
		GadgetButtonSetEnabledSelectedColor( gadget, yellow );
		GadgetButtonSetEnabledSelectedBorderColor( gadget, white );

		// Disabled background
		GadgetButtonSetDisabledImage( gadget, winFindImage( "PushButtonDisabled" ) );
		GadgetButtonSetDisabledColor( gadget, gray );
		GadgetButtonSetDisabledBorderColor( gadget, lightGray );
		// Disabled selected button
		GadgetButtonSetDisabledSelectedImage( gadget, winFindImage( "PushButtonDisabledSelected" ) );
		GadgetButtonSetDisabledSelectedColor( gadget, lightGray );
		GadgetButtonSetDisabledSelectedBorderColor( gadget, gray );

		// Hilite background
		GadgetButtonSetHiliteImage( gadget, winFindImage( "PushButtonHilite" ) );
		GadgetButtonSetHiliteColor( gadget, green );
		GadgetButtonSetHiliteBorderColor( gadget, darkGreen );
		// Hilite selected button
		GadgetButtonSetHiliteSelectedImage( gadget, winFindImage( "PushButtonHiliteSelected" ) );
		GadgetButtonSetHiliteSelectedColor( gadget, yellow );
		GadgetButtonSetHiliteSelectedBorderColor( gadget, white );

		// set default text colors for the gadget
		gadget->winSetEnabledTextColors( enabledText, enabledTextBorder );
		gadget->winSetDisabledTextColors( disabledText, disabledTextBorder );
		gadget->winSetHiliteTextColors( hiliteText, hiliteTextBorder );
		gadget->winSetIMECompositeTextColors( imeCompositeText, imeCompositeTextBorder );

	}  // end if
	else if( BitTest( instData->getStyle(), GWS_CHECK_BOX ) )
	{

		// enabled background
		GadgetCheckBoxSetEnabledImage( gadget, winFindImage( "CheckBoxEnabled" ) );
		GadgetCheckBoxSetEnabledColor( gadget, red );
		GadgetCheckBoxSetEnabledBorderColor( gadget, lightRed );
		// enabled CheckBox unselected
		GadgetCheckBoxSetEnabledUncheckedBoxImage( gadget, winFindImage( "CheckBoxEnabledBoxUnselected" ) );
		GadgetCheckBoxSetEnabledUncheckedBoxColor( gadget, WIN_COLOR_UNDEFINED );
		GadgetCheckBoxSetEnabledUncheckedBoxBorderColor( gadget, lightBlue );
		// enabled CheckBox selected
		GadgetCheckBoxSetEnabledCheckedBoxImage( gadget, winFindImage( "CheckBoxEnabledBoxSelected" ) );
		GadgetCheckBoxSetEnabledCheckedBoxColor( gadget, blue );
		GadgetCheckBoxSetEnabledCheckedBoxBorderColor( gadget, lightBlue );

		// disabled background
		GadgetCheckBoxSetDisabledImage( gadget, winFindImage( "CheckBoxDisabled" ) );
		GadgetCheckBoxSetDisabledColor( gadget, gray );
		GadgetCheckBoxSetDisabledBorderColor( gadget, lightGray );
		// Disabled CheckBox unselected
		GadgetCheckBoxSetDisabledUncheckedBoxImage( gadget, winFindImage( "CheckBoxDisabledBoxUnselected" ) );
		GadgetCheckBoxSetDisabledUncheckedBoxColor( gadget, WIN_COLOR_UNDEFINED );
		GadgetCheckBoxSetDisabledUncheckedBoxBorderColor( gadget, lightGray );
		// Disabled CheckBox selected
		GadgetCheckBoxSetDisabledCheckedBoxImage( gadget, winFindImage( "CheckBoxDisabledBoxSelected" ) );
		GadgetCheckBoxSetDisabledCheckedBoxColor( gadget, darkGray );
		GadgetCheckBoxSetDisabledCheckedBoxBorderColor( gadget, white );

		// Hilite background
		GadgetCheckBoxSetHiliteImage( gadget, winFindImage( "CheckBoxHilite" ) );
		GadgetCheckBoxSetHiliteColor( gadget, green );
		GadgetCheckBoxSetHiliteBorderColor( gadget, lightGreen );
		// Hilite CheckBox unselected
		GadgetCheckBoxSetHiliteUncheckedBoxImage( gadget, winFindImage( "CheckBoxHiliteBoxUnselected" ) );
		GadgetCheckBoxSetHiliteUncheckedBoxColor( gadget, WIN_COLOR_UNDEFINED );
		GadgetCheckBoxSetHiliteUncheckedBoxBorderColor( gadget, lightBlue );
		// Hilite CheckBox selected
		GadgetCheckBoxSetHiliteCheckedBoxImage( gadget, winFindImage( "CheckBoxHiliteBoxSelected" ) );
		GadgetCheckBoxSetHiliteCheckedBoxColor( gadget, yellow );
		GadgetCheckBoxSetHiliteCheckedBoxBorderColor( gadget, white );

		// set default text colors for the gadget
		gadget->winSetEnabledTextColors( enabledText, enabledTextBorder );
		gadget->winSetDisabledTextColors( disabledText, disabledTextBorder );
		gadget->winSetHiliteTextColors( hiliteText, hiliteTextBorder );
		gadget->winSetIMECompositeTextColors( imeCompositeText, imeCompositeTextBorder );

	}  // end else if
	else if( BitTest( instData->getStyle(), GWS_RADIO_BUTTON ) )
	{

		// enabled background
		GadgetRadioSetEnabledImage( gadget, winFindImage( "RadioButtonEnabled" ) );
		GadgetRadioSetEnabledColor( gadget, red );
		GadgetRadioSetEnabledBorderColor( gadget, lightRed );
		// enabled radio unselected
		GadgetRadioSetEnabledUncheckedBoxImage( gadget, winFindImage( "RadioButtonEnabledBoxUnselected" ) );
		GadgetRadioSetEnabledUncheckedBoxColor( gadget, darkRed );
		GadgetRadioSetEnabledUncheckedBoxBorderColor( gadget, black );
		// enabled radio selected
		GadgetRadioSetEnabledCheckedBoxImage( gadget, winFindImage( "RadioButtonEnabledBoxSelected" ) );
		GadgetRadioSetEnabledCheckedBoxColor( gadget, blue );
		GadgetRadioSetEnabledCheckedBoxBorderColor( gadget, lightBlue );

		// disabled background
		GadgetRadioSetDisabledImage( gadget, winFindImage( "RadioButtonDisabled" ) );
		GadgetRadioSetDisabledColor( gadget, gray );
		GadgetRadioSetDisabledBorderColor( gadget, lightGray );
		// Disabled radio unselected
		GadgetRadioSetDisabledUncheckedBoxImage( gadget, winFindImage( "RadioButtonDisabledBoxUnselected" ) );
		GadgetRadioSetDisabledUncheckedBoxColor( gadget, gray );
		GadgetRadioSetDisabledUncheckedBoxBorderColor( gadget, lightGray );
		// Disabled radio selected
		GadgetRadioSetDisabledCheckedBoxImage( gadget, winFindImage( "RadioButtonDisabledBoxSelected" ) );
		GadgetRadioSetDisabledCheckedBoxColor( gadget, darkGray );
		GadgetRadioSetDisabledCheckedBoxBorderColor( gadget, white );

		// Hilite background
		GadgetRadioSetHiliteImage( gadget, winFindImage( "RadioButtonHilite" ) );
		GadgetRadioSetHiliteColor( gadget, green );
		GadgetRadioSetHiliteBorderColor( gadget, lightGreen );
		// Hilite radio unselected
		GadgetRadioSetHiliteUncheckedBoxImage( gadget, winFindImage( "RadioButtonHiliteBoxUnselected" ) );
		GadgetRadioSetHiliteUncheckedBoxColor( gadget, darkGreen );
		GadgetRadioSetHiliteUncheckedBoxBorderColor( gadget, lightGreen );
		// Hilite radio selected
		GadgetRadioSetHiliteCheckedBoxImage( gadget, winFindImage( "RadioButtonHiliteBoxSelected" ) );
		GadgetRadioSetHiliteCheckedBoxColor( gadget, yellow );
		GadgetRadioSetHiliteCheckedBoxBorderColor( gadget, white );

		// set default text colors for the gadget
		gadget->winSetEnabledTextColors( enabledText, enabledTextBorder );
		gadget->winSetDisabledTextColors( disabledText, disabledTextBorder );
		gadget->winSetHiliteTextColors( hiliteText, hiliteTextBorder );
		gadget->winSetIMECompositeTextColors( imeCompositeText, imeCompositeTextBorder );

	}  // end else if
	else if( BitTest( instData->getStyle(), GWS_HORZ_SLIDER ) )
	{

		// enabled
		GadgetSliderSetEnabledImageLeft( gadget, winFindImage( "HSliderEnabledLeftEnd" ) );
		GadgetSliderSetEnabledImageRight( gadget, winFindImage( "HSliderEnabledRightEnd" ) );
		GadgetSliderSetEnabledImageCenter( gadget, winFindImage( "HSliderEnabledRepeatingCenter" ) );
		GadgetSliderSetEnabledImageSmallCenter( gadget, winFindImage( "HSliderEnabledSmallRepeatingCenter" ) );
		GadgetSliderSetEnabledColor( gadget, red );
		GadgetSliderSetEnabledBorderColor( gadget, lightRed );

		// disabled
		GadgetSliderSetDisabledImageLeft( gadget, winFindImage( "HSliderDisabledLeftEnd" ) );
		GadgetSliderSetDisabledImageRight( gadget, winFindImage( "HSliderDisabledRightEnd" ) );
		GadgetSliderSetDisabledImageCenter( gadget, winFindImage( "HSliderDisabledRepeatingCenter" ) );
		GadgetSliderSetDisabledImageSmallCenter( gadget, winFindImage( "HSliderDisabledSmallRepeatingCenter" ) );
		GadgetSliderSetDisabledColor( gadget, red );
		GadgetSliderSetDisabledBorderColor( gadget, lightRed );

		// hilite
		GadgetSliderSetHiliteImageLeft( gadget, winFindImage( "HSliderHiliteLeftEnd" ) );
		GadgetSliderSetHiliteImageRight( gadget, winFindImage( "HSliderHiliteRightEnd" ) );
		GadgetSliderSetHiliteImageCenter( gadget, winFindImage( "HSliderHiliteRepeatingCenter" ) );
		GadgetSliderSetHiliteImageSmallCenter( gadget, winFindImage( "HSliderHiliteSmallRepeatingCenter" ) );
		GadgetSliderSetHiliteColor( gadget, red );
		GadgetSliderSetHiliteBorderColor( gadget, lightRed );	

		// set default text colors for the gadget
		gadget->winSetEnabledTextColors( enabledText, enabledTextBorder );
		gadget->winSetDisabledTextColors( disabledText, disabledTextBorder );
		gadget->winSetHiliteTextColors( hiliteText, hiliteTextBorder );
		gadget->winSetIMECompositeTextColors( imeCompositeText, imeCompositeTextBorder );

		//
		// set the default colors and images for the slider thumb
		//
		// enabled
		GadgetSliderSetEnabledThumbImage( gadget, winFindImage( "HSliderThumbEnabled" ) );
		GadgetSliderSetEnabledThumbColor( gadget, GadgetSliderGetEnabledColor( gadget ) );
		GadgetSliderSetEnabledThumbBorderColor( gadget, GadgetSliderGetEnabledBorderColor( gadget ) );
		GadgetSliderSetEnabledSelectedThumbImage( gadget, winFindImage( "HSliderThumbEnabled" ) );
		GadgetSliderSetEnabledSelectedThumbColor( gadget, GadgetSliderGetEnabledBorderColor( gadget ) );
		GadgetSliderSetEnabledSelectedThumbBorderColor( gadget, GadgetSliderGetEnabledColor( gadget ) );

		// disabled
		GadgetSliderSetDisabledThumbImage( gadget, winFindImage( "HSliderThumbDisabled" ) );
		GadgetSliderSetDisabledThumbColor( gadget, GadgetSliderGetDisabledColor( gadget ) );
		GadgetSliderSetDisabledThumbBorderColor( gadget, GadgetSliderGetDisabledBorderColor( gadget ) );
		GadgetSliderSetDisabledSelectedThumbImage( gadget, winFindImage( "HSliderThumbDisabled" ) );
		GadgetSliderSetDisabledSelectedThumbColor( gadget, GadgetSliderGetDisabledBorderColor( gadget ) );
		GadgetSliderSetDisabledSelectedThumbBorderColor( gadget, GadgetSliderGetDisabledColor( gadget ) );

		// hilite
		GadgetSliderSetHiliteThumbImage( gadget, winFindImage( "HSliderThumbHilite" ) );
		GadgetSliderSetHiliteThumbColor( gadget, GadgetSliderGetHiliteColor( gadget ) );
		GadgetSliderSetHiliteThumbBorderColor( gadget, GadgetSliderGetHiliteBorderColor( gadget ) );
		GadgetSliderSetHiliteSelectedThumbImage( gadget, winFindImage( "HSliderThumbHiliteSelected" ) );
		GadgetSliderSetHiliteSelectedThumbColor( gadget, GadgetSliderGetHiliteBorderColor( gadget ) );
		GadgetSliderSetHiliteSelectedThumbBorderColor( gadget, GadgetSliderGetHiliteColor( gadget ) );


	}  // end if
	else if( BitTest( instData->getStyle(), GWS_VERT_SLIDER ) )
	{
		// enabled
		GadgetSliderSetEnabledImageTop( gadget, winFindImage( "VSliderEnabledTopEnd" ) );
		GadgetSliderSetEnabledImageBottom( gadget, winFindImage( "VSliderEnabledBottomEnd" ) );
		GadgetSliderSetEnabledImageCenter( gadget, winFindImage( "VSliderEnabledRepeatingCenter" ) );
		GadgetSliderSetEnabledImageSmallCenter( gadget, winFindImage( "VSliderEnabledSmallRepeatingCenter" ) );
		GadgetSliderSetEnabledColor( gadget, red );
		GadgetSliderSetEnabledBorderColor( gadget, lightRed );

		// disabled
		GadgetSliderSetDisabledImageTop( gadget, winFindImage( "VSliderDisabledTopEnd" ) );
		GadgetSliderSetDisabledImageBottom( gadget, winFindImage( "VSliderDisabledBottomEnd" ) );
		GadgetSliderSetDisabledImageCenter( gadget, winFindImage( "VSliderDisabledRepeatingCenter" ) );
		GadgetSliderSetDisabledImageSmallCenter( gadget, winFindImage( "VSliderDisabledSmallRepeatingCenter" ) );
		GadgetSliderSetDisabledColor( gadget, red );
		GadgetSliderSetDisabledBorderColor( gadget, lightRed );

		// hilite
		GadgetSliderSetHiliteImageTop( gadget, winFindImage( "VSliderHiliteTopEnd" ) );
		GadgetSliderSetHiliteImageBottom( gadget, winFindImage( "VSliderHiliteBottomEnd" ) );
		GadgetSliderSetHiliteImageCenter( gadget, winFindImage( "VSliderHiliteRepeatingCenter" ) );
		GadgetSliderSetHiliteImageSmallCenter( gadget, winFindImage( "VSliderHiliteSmallRepeatingCenter" ) );
		GadgetSliderSetHiliteColor( gadget, red );
		GadgetSliderSetHiliteBorderColor( gadget, lightRed );	

		// set default text colors for the gadget
		gadget->winSetEnabledTextColors( enabledText, enabledTextBorder );
		gadget->winSetDisabledTextColors( disabledText, disabledTextBorder );
		gadget->winSetHiliteTextColors( hiliteText, hiliteTextBorder );
		gadget->winSetIMECompositeTextColors( imeCompositeText, imeCompositeTextBorder );

		//
		// set the default colors and images for the slider thumb
		//
		// enabled
		GadgetSliderSetEnabledThumbImage( gadget, winFindImage( "VSliderThumbEnabled" ) );
		GadgetSliderSetEnabledThumbColor( gadget, GadgetSliderGetEnabledColor( gadget ) );
		GadgetSliderSetEnabledThumbBorderColor( gadget, GadgetSliderGetEnabledBorderColor( gadget ) );
		GadgetSliderSetEnabledSelectedThumbImage( gadget, winFindImage( "VSliderThumbEnabled" ) );
		GadgetSliderSetEnabledSelectedThumbColor( gadget, GadgetSliderGetEnabledBorderColor( gadget ) );
		GadgetSliderSetEnabledSelectedThumbBorderColor( gadget, GadgetSliderGetEnabledColor( gadget ) );

		// disabled
		GadgetSliderSetDisabledThumbImage( gadget, winFindImage( "VSliderThumbDisabled" ) );
		GadgetSliderSetDisabledThumbColor( gadget, GadgetSliderGetDisabledColor( gadget ) );
		GadgetSliderSetDisabledThumbBorderColor( gadget, GadgetSliderGetDisabledBorderColor( gadget ) );
		GadgetSliderSetDisabledSelectedThumbImage( gadget, winFindImage( "VSliderThumbDisabled" ) );
		GadgetSliderSetDisabledSelectedThumbColor( gadget, GadgetSliderGetDisabledBorderColor( gadget ) );
		GadgetSliderSetDisabledSelectedThumbBorderColor( gadget, GadgetSliderGetDisabledColor( gadget ) );

		// hilite
		GadgetSliderSetHiliteThumbImage( gadget, winFindImage( "VSliderThumbHilite" ) );
		GadgetSliderSetHiliteThumbColor( gadget, GadgetSliderGetHiliteColor( gadget ) );
		GadgetSliderSetHiliteThumbBorderColor( gadget, GadgetSliderGetHiliteBorderColor( gadget ) );
		GadgetSliderSetHiliteSelectedThumbImage( gadget, winFindImage( "VSliderThumbHiliteSelected" ) );
		GadgetSliderSetHiliteSelectedThumbColor( gadget, GadgetSliderGetHiliteBorderColor( gadget ) );
		GadgetSliderSetHiliteSelectedThumbBorderColor( gadget, GadgetSliderGetHiliteColor( gadget ) );

	}  // end else if
	else if( BitTest( instData->getStyle(), GWS_SCROLL_LISTBOX ) )
	{
		ListboxData *listboxData = static_cast<ListboxData *>(gadget->winGetUserData());

		// set the colors
		GadgetListBoxSetColors( gadget,
														red,							// enabled
														lightRed,					// enabled border
														yellow,						// enabled selected item
														white,						// enabled selected item border
														gray,							// disabled
														lightGray,				// disabled border
														lightGray,				// disabled selected item
														white,						// disabled selected item border
														green,						// hilite
														darkGreen,				// hilite border
														white,						// hilite selected item
														darkGreen );			// hilite selected item border
											
		// now set the images

		// enabled
		GadgetListBoxSetEnabledImage( gadget, winFindImage( "ListBoxEnabled" ) );
		GadgetListBoxSetEnabledSelectedItemImageLeft( gadget, winFindImage( "ListBoxEnabledSelectedItemLeftEnd" ) );
		GadgetListBoxSetEnabledSelectedItemImageRight( gadget, winFindImage( "ListBoxEnabledSelectedItemRightEnd" ) );
		GadgetListBoxSetEnabledSelectedItemImageCenter( gadget, winFindImage( "ListBoxEnabledSelectedItemRepeatingCenter" ) );
		GadgetListBoxSetEnabledSelectedItemImageSmallCenter( gadget, winFindImage( "ListBoxEnabledSelectedItemSmallRepeatingCenter" ) );

		// disabled
		GadgetListBoxSetDisabledImage( gadget, winFindImage( "ListBoxDisabled" ) );
		GadgetListBoxSetDisabledSelectedItemImageLeft( gadget, winFindImage( "ListBoxDisabledSelectedItemLeftEnd" ) );
		GadgetListBoxSetDisabledSelectedItemImageRight( gadget, winFindImage( "ListBoxDisabledSelectedItemRightEnd" ) );
		GadgetListBoxSetDisabledSelectedItemImageCenter( gadget, winFindImage( "ListBoxDisabledSelectedItemRepeatingCenter" ) );
		GadgetListBoxSetDisabledSelectedItemImageSmallCenter( gadget, winFindImage( "ListBoxDisabledSelectedItemSmallRepeatingCenter" ) );


		// hilited
		GadgetListBoxSetHiliteImage( gadget, winFindImage( "ListBoxHilite" ) );
		GadgetListBoxSetHiliteSelectedItemImageLeft( gadget, winFindImage( "ListBoxHiliteSelectedItemLeftEnd" ) );
		GadgetListBoxSetHiliteSelectedItemImageRight( gadget, winFindImage( "ListBoxHiliteSelectedItemRightEnd" ) );
		GadgetListBoxSetHiliteSelectedItemImageCenter( gadget, winFindImage( "ListBoxHiliteSelectedItemRepeatingCenter" ) );
		GadgetListBoxSetHiliteSelectedItemImageSmallCenter( gadget, winFindImage( "ListBoxHiliteSelectedItemSmallRepeatingCenter" ) );

		// assign default slider colors and images as part of the list box
		GameWindow *slider = listboxData->slider;
		if( slider )
		{
			GameWindow *upButton = listboxData->upButton;
			GameWindow *downButton = listboxData->downButton;

			// slider and slider thumb ----------------------------------------------

			// enabled
			GadgetSliderSetEnabledImageTop( slider, winFindImage( "VSliderLargeEnabledTopEnd" ) );
			GadgetSliderSetEnabledImageBottom( slider, winFindImage( "VSliderLargeEnabledBottomEnd" ) );
			GadgetSliderSetEnabledImageCenter( slider, winFindImage( "VSliderLargeEnabledRepeatingCenter" ) );
			GadgetSliderSetEnabledImageSmallCenter( slider, winFindImage( "VSliderLargeEnabledSmallRepeatingCenter" ) );
			GadgetSliderSetEnabledThumbImage( slider, winFindImage( "VSliderLargeThumbEnabled" ) );
			GadgetSliderSetEnabledSelectedThumbImage( slider, winFindImage( "VSliderLargeThumbEnabled" ) );

			// disabled
			GadgetSliderSetDisabledImageTop( slider, winFindImage( "VSliderLargeDisabledTopEnd" ) );
			GadgetSliderSetDisabledImageBottom( slider, winFindImage( "VSliderLargeDisabledBottomEnd" ) );
			GadgetSliderSetDisabledImageCenter( slider, winFindImage( "VSliderLargeDisabledRepeatingCenter" ) );
			GadgetSliderSetDisabledImageSmallCenter( slider, winFindImage( "VSliderLargeDisabledSmallRepeatingCenter" ) );
			GadgetSliderSetDisabledThumbImage( slider, winFindImage( "VSliderLargeThumbDisabled" ) );
			GadgetSliderSetDisabledSelectedThumbImage( slider, winFindImage( "VSliderLargeThumbDisabled" ) );

			// hilite
			GadgetSliderSetHiliteImageTop( slider, winFindImage( "VSliderLargeHiliteTopEnd" ) );
			GadgetSliderSetHiliteImageBottom( slider, winFindImage( "VSliderLargeHiliteBottomEnd" ) );
			GadgetSliderSetHiliteImageCenter( slider, winFindImage( "VSliderLargeHiliteRepeatingCenter" ) );
			GadgetSliderSetHiliteImageSmallCenter( slider, winFindImage( "VSliderLargeHiliteSmallRepeatingCenter" ) );
			GadgetSliderSetHiliteThumbImage( slider, winFindImage( "VSliderLargeThumbHilite" ) );
			GadgetSliderSetHiliteSelectedThumbImage( slider, winFindImage( "VSliderLargeThumbHilite" ) );
	
			// up button ------------------------------------------------------------

			// enabled
			GadgetButtonSetEnabledImage( upButton, winFindImage( "VSliderLargeUpButtonEnabled" ) );
			GadgetButtonSetEnabledSelectedImage( upButton, winFindImage( "VSliderLargeUpButtonEnabled" ) );

			// disabled
			GadgetButtonSetDisabledImage( upButton, winFindImage( "VSliderLargeUpButtonDisabled" ) );
			GadgetButtonSetDisabledSelectedImage( upButton, winFindImage( "VSliderLargeUpButtonDisabled" ) );

			// hilite
			GadgetButtonSetHiliteImage( upButton, winFindImage( "VSliderLargeUpButtonHilite" ) );
			GadgetButtonSetHiliteSelectedImage( upButton, winFindImage( "VSliderLargeUpButtonHiliteSelected" ) );

			// down button ----------------------------------------------------------

			// enabled
			GadgetButtonSetEnabledImage( downButton, winFindImage( "VSliderLargeDownButtonEnabled" ) );
			GadgetButtonSetEnabledSelectedImage( downButton, winFindImage( "VSliderLargeDownButtonEnabled" ) );

			// disabled
			GadgetButtonSetDisabledImage( downButton, winFindImage( "VSliderLargeDownButtonDisabled" ) );
			GadgetButtonSetDisabledSelectedImage( downButton, winFindImage( "VSliderLargeDownButtonDisabled" ) );

			// hilite
			GadgetButtonSetHiliteImage( downButton, winFindImage( "VSliderLargeDownButtonHilite" ) );
			GadgetButtonSetHiliteSelectedImage( downButton, winFindImage( "VSliderLargeDownButtonHiliteSelected" ) );

		}  // end if

	}  // end else if
	else if( BitTest( instData->getStyle(), GWS_COMBO_BOX ) )
	{
//		ComboBoxData *comboBoxData = (ComboBoxData *)gadget->winGetUserData();

		GadgetComboBoxSetColors( gadget,
														red,							// enabled
														lightRed,					// enabled border
														yellow,						// enabled selected item
														white,						// enabled selected item border
														gray,							// disabled
														lightGray,				// disabled border
														lightGray,				// disabled selected item
														white,						// disabled selected item border
														green,						// hilite
														darkGreen,				// hilite border
														white,						// hilite selected item
														darkGreen );			// hilite selected item border
		
		// enabled
		GadgetComboBoxSetEnabledImage( gadget, winFindImage( "ListBoxEnabled" ) );
		GadgetComboBoxSetEnabledSelectedItemImageLeft( gadget, winFindImage( "ListBoxEnabledSelectedItemLeftEnd" ) );
		GadgetComboBoxSetEnabledSelectedItemImageRight( gadget, winFindImage( "ListBoxEnabledSelectedItemRightEnd" ) );
		GadgetComboBoxSetEnabledSelectedItemImageCenter( gadget, winFindImage( "ListBoxEnabledSelectedItemRepeatingCenter" ) );
		GadgetComboBoxSetEnabledSelectedItemImageSmallCenter( gadget, winFindImage( "ListBoxEnabledSelectedItemSmallRepeatingCenter" ) );

		// disabled
		GadgetComboBoxSetDisabledImage( gadget, winFindImage( "ListBoxDisabled" ) );
		GadgetComboBoxSetDisabledSelectedItemImageLeft( gadget, winFindImage( "ListBoxDisabledSelectedItemLeftEnd" ) );
		GadgetComboBoxSetDisabledSelectedItemImageRight( gadget, winFindImage( "ListBoxDisabledSelectedItemRightEnd" ) );
		GadgetComboBoxSetDisabledSelectedItemImageCenter( gadget, winFindImage( "ListBoxDisabledSelectedItemRepeatingCenter" ) );
		GadgetComboBoxSetDisabledSelectedItemImageSmallCenter( gadget, winFindImage( "ListBoxDisabledSelectedItemSmallRepeatingCenter" ) );


		// hilited
		GadgetComboBoxSetHiliteImage( gadget, winFindImage( "ListBoxHilite" ) );
		GadgetComboBoxSetHiliteSelectedItemImageLeft( gadget, winFindImage( "ListBoxHiliteSelectedItemLeftEnd" ) );
		GadgetComboBoxSetHiliteSelectedItemImageRight( gadget, winFindImage( "ListBoxHiliteSelectedItemRightEnd" ) );
		GadgetComboBoxSetHiliteSelectedItemImageCenter( gadget, winFindImage( "ListBoxHiliteSelectedItemRepeatingCenter" ) );
		GadgetComboBoxSetHiliteSelectedItemImageSmallCenter( gadget, winFindImage( "ListBoxHiliteSelectedItemSmallRepeatingCenter" ) );

		gadget->winSetEnabledTextColors( enabledText, enabledTextBorder );
		gadget->winSetDisabledTextColors( disabledText, disabledTextBorder );
		gadget->winSetHiliteTextColors( hiliteText, hiliteTextBorder );
		gadget->winSetIMECompositeTextColors( imeCompositeText, imeCompositeTextBorder );

		GameWindow *dropDownButton = GadgetComboBoxGetDropDownButton( gadget );
		if ( dropDownButton )
		{
			// enabled background
			GadgetButtonSetEnabledImage( dropDownButton, winFindImage( "PushButtonEnabled" ) );
			// enabled selected button
			GadgetButtonSetEnabledSelectedImage( dropDownButton, winFindImage( "PushButtonEnabledSelected" ) );
			
			// Disabled background
			GadgetButtonSetDisabledImage( dropDownButton, winFindImage( "PushButtonDisabled" ) );
			// Disabled selected button
			GadgetButtonSetDisabledSelectedImage( dropDownButton, winFindImage( "PushButtonDisabledSelected" ) );
			
			// Hilite background
			GadgetButtonSetHiliteImage( dropDownButton, winFindImage( "PushButtonHilite" ) );
			// Hilite selected button
			GadgetButtonSetHiliteSelectedImage( dropDownButton, winFindImage( "PushButtonHiliteSelected" ) );
			
		}

		GameWindow *editBox = GadgetComboBoxGetEditBox( gadget );
		if ( editBox )
		{
			// enabled
			GadgetTextEntrySetEnabledImageLeft( editBox, winFindImage( "TextEntryEnabledLeftEnd" ) );
			GadgetTextEntrySetEnabledImageRight( editBox, winFindImage( "TextEntryEnabledRightEnd" ) );
			GadgetTextEntrySetEnabledImageCenter( editBox, winFindImage( "TextEntryEnabledRepeatingCenter" ) );
			GadgetTextEntrySetEnabledImageSmallCenter( editBox, winFindImage( "TextEntryEnabledSmallRepeatingCenter" ) );
			
			// disabled
			GadgetTextEntrySetDisabledImageLeft( editBox, winFindImage( "TextEntryDisabledLeftEnd" ) );
			GadgetTextEntrySetDisabledImageRight( editBox, winFindImage( "TextEntryDisabledRightEnd" ) );
			GadgetTextEntrySetDisabledImageCenter( editBox, winFindImage( "TextEntryDisabledRepeatingCenter" ) );
			GadgetTextEntrySetDisabledImageSmallCenter( editBox, winFindImage( "TextEntryDisabledSmallRepeatingCenter" ) );
			
			// hilited
			GadgetTextEntrySetHiliteImageLeft( editBox, winFindImage( "TextEntryHiliteLeftEnd" ) );
			GadgetTextEntrySetHiliteImageRight( editBox, winFindImage( "TextEntryHiliteRightEnd" ) );
			GadgetTextEntrySetHiliteImageCenter( editBox, winFindImage( "TextEntryHiliteRepeatingCenter" ) );
			GadgetTextEntrySetHiliteImageSmallCenter( editBox, winFindImage( "TextEntryHiliteSmallRepeatingCenter" ) );
			
		}

		GameWindow * listBox = GadgetComboBoxGetListBox( gadget );
		if ( listBox )
		{
														
			// now set the images

			// enabled
			GadgetListBoxSetEnabledImage( listBox, winFindImage( "ListBoxEnabled" ) );
			GadgetListBoxSetEnabledSelectedItemImageLeft( listBox, winFindImage( "ListBoxEnabledSelectedItemLeftEnd" ) );
			GadgetListBoxSetEnabledSelectedItemImageRight( listBox, winFindImage( "ListBoxEnabledSelectedItemRightEnd" ) );
			GadgetListBoxSetEnabledSelectedItemImageCenter( listBox, winFindImage( "ListBoxEnabledSelectedItemRepeatingCenter" ) );
			GadgetListBoxSetEnabledSelectedItemImageSmallCenter( listBox, winFindImage( "ListBoxEnabledSelectedItemSmallRepeatingCenter" ) );

			// disabled
			GadgetListBoxSetDisabledImage( listBox, winFindImage( "ListBoxDisabled" ) );
			GadgetListBoxSetDisabledSelectedItemImageLeft( listBox, winFindImage( "ListBoxDisabledSelectedItemLeftEnd" ) );
			GadgetListBoxSetDisabledSelectedItemImageRight( listBox, winFindImage( "ListBoxDisabledSelectedItemRightEnd" ) );
			GadgetListBoxSetDisabledSelectedItemImageCenter( listBox, winFindImage( "ListBoxDisabledSelectedItemRepeatingCenter" ) );
			GadgetListBoxSetDisabledSelectedItemImageSmallCenter( listBox, winFindImage( "ListBoxDisabledSelectedItemSmallRepeatingCenter" ) );


			// hilited
			GadgetListBoxSetHiliteImage( listBox, winFindImage( "ListBoxHilite" ) );
			GadgetListBoxSetHiliteSelectedItemImageLeft( listBox, winFindImage( "ListBoxHiliteSelectedItemLeftEnd" ) );
			GadgetListBoxSetHiliteSelectedItemImageRight( listBox, winFindImage( "ListBoxHiliteSelectedItemRightEnd" ) );
			GadgetListBoxSetHiliteSelectedItemImageCenter( listBox, winFindImage( "ListBoxHiliteSelectedItemRepeatingCenter" ) );
			GadgetListBoxSetHiliteSelectedItemImageSmallCenter( listBox, winFindImage( "ListBoxHiliteSelectedItemSmallRepeatingCenter" ) );

			// assign default slider colors and images as part of the list box
			GameWindow *slider = GadgetListBoxGetSlider( listBox );
			if( slider )
			{
				GameWindow *upButton = GadgetListBoxGetUpButton( listBox );
				GameWindow *downButton = GadgetListBoxGetDownButton( listBox );

				// slider and slider thumb ----------------------------------------------

				// enabled
				GadgetSliderSetEnabledImageTop( slider, winFindImage( "VSliderLargeEnabledTopEnd" ) );
				GadgetSliderSetEnabledImageBottom( slider, winFindImage( "VSliderLargeEnabledBottomEnd" ) );
				GadgetSliderSetEnabledImageCenter( slider, winFindImage( "VSliderLargeEnabledRepeatingCenter" ) );
				GadgetSliderSetEnabledImageSmallCenter( slider, winFindImage( "VSliderLargeEnabledSmallRepeatingCenter" ) );
				GadgetSliderSetEnabledThumbImage( slider, winFindImage( "VSliderLargeThumbEnabled" ) );
				GadgetSliderSetEnabledSelectedThumbImage( slider, winFindImage( "VSliderLargeThumbEnabled" ) );

				// disabled
				GadgetSliderSetDisabledImageTop( slider, winFindImage( "VSliderLargeDisabledTopEnd" ) );
				GadgetSliderSetDisabledImageBottom( slider, winFindImage( "VSliderLargeDisabledBottomEnd" ) );
				GadgetSliderSetDisabledImageCenter( slider, winFindImage( "VSliderLargeDisabledRepeatingCenter" ) );
				GadgetSliderSetDisabledImageSmallCenter( slider, winFindImage( "VSliderLargeDisabledSmallRepeatingCenter" ) );
				GadgetSliderSetDisabledThumbImage( slider, winFindImage( "VSliderLargeThumbDisabled" ) );
				GadgetSliderSetDisabledSelectedThumbImage( slider, winFindImage( "VSliderLargeThumbDisabled" ) );

				// hilite
				GadgetSliderSetHiliteImageTop( slider, winFindImage( "VSliderLargeHiliteTopEnd" ) );
				GadgetSliderSetHiliteImageBottom( slider, winFindImage( "VSliderLargeHiliteBottomEnd" ) );
				GadgetSliderSetHiliteImageCenter( slider, winFindImage( "VSliderLargeHiliteRepeatingCenter" ) );
				GadgetSliderSetHiliteImageSmallCenter( slider, winFindImage( "VSliderLargeHiliteSmallRepeatingCenter" ) );
				GadgetSliderSetHiliteThumbImage( slider, winFindImage( "VSliderLargeThumbHilite" ) );
				GadgetSliderSetHiliteSelectedThumbImage( slider, winFindImage( "VSliderLargeThumbHilite" ) );
		
				// up button ------------------------------------------------------------

				// enabled
				GadgetButtonSetEnabledImage( upButton, winFindImage( "VSliderLargeUpButtonEnabled" ) );
				GadgetButtonSetEnabledSelectedImage( upButton, winFindImage( "VSliderLargeUpButtonEnabled" ) );

				// disabled
				GadgetButtonSetDisabledImage( upButton, winFindImage( "VSliderLargeUpButtonDisabled" ) );
				GadgetButtonSetDisabledSelectedImage( upButton, winFindImage( "VSliderLargeUpButtonDisabled" ) );

				// hilite
				GadgetButtonSetHiliteImage( upButton, winFindImage( "VSliderLargeUpButtonHilite" ) );
				GadgetButtonSetHiliteSelectedImage( upButton, winFindImage( "VSliderLargeUpButtonHiliteSelected" ) );

				// down button ----------------------------------------------------------

				// enabled
				GadgetButtonSetEnabledImage( downButton, winFindImage( "VSliderLargeDownButtonEnabled" ) );
				GadgetButtonSetEnabledSelectedImage( downButton, winFindImage( "VSliderLargeDownButtonEnabled" ) );

				// disabled
				GadgetButtonSetDisabledImage( downButton, winFindImage( "VSliderLargeDownButtonDisabled" ) );
				GadgetButtonSetDisabledSelectedImage( downButton, winFindImage( "VSliderLargeDownButtonDisabled" ) );

				// hilite
				GadgetButtonSetHiliteImage( downButton, winFindImage( "VSliderLargeDownButtonHilite" ) );
				GadgetButtonSetHiliteSelectedImage( downButton, winFindImage( "VSliderLargeDownButtonHiliteSelected" ) );

			}  // end if
		}
	}  // end else if
	else if( BitTest( instData->getStyle(), GWS_PROGRESS_BAR ) )
	{

		// enabled
		GadgetProgressBarSetEnabledColor( gadget, red );
		GadgetProgressBarSetEnabledBorderColor( gadget, lightRed );
		GadgetProgressBarSetEnabledImageLeft( gadget, winFindImage( "ProgressBarEnabledLeftEnd" ) );
		GadgetProgressBarSetEnabledImageRight( gadget, winFindImage( "ProgressBarEnabledRightEnd" ) );
		GadgetProgressBarSetEnabledImageCenter( gadget, winFindImage( "ProgressBarEnabledRepeatingCenter" ) );
		GadgetProgressBarSetEnabledImageSmallCenter( gadget, winFindImage( "ProgressBarEnabledSmallRepeatingCenter" ) );
		GadgetProgressBarSetEnabledBarColor( gadget, yellow );
		GadgetProgressBarSetEnabledBarBorderColor( gadget, white );
		GadgetProgressBarSetEnabledBarImageLeft( gadget, winFindImage( "ProgressBarEnabledBarLeftEnd" ) );
		GadgetProgressBarSetEnabledBarImageRight( gadget, winFindImage( "ProgressBarEnabledBarRightEnd" ) );
		GadgetProgressBarSetEnabledBarImageCenter( gadget, winFindImage( "ProgressBarEnabledBarRepeatingCenter" ) );
		GadgetProgressBarSetEnabledBarImageSmallCenter( gadget, winFindImage( "ProgressBarEnabledBarSmallRepeatingCenter" ) );

		// disabled
		GadgetProgressBarSetDisabledColor( gadget, darkGray );
		GadgetProgressBarSetDisabledBorderColor( gadget, lightGray );
		GadgetProgressBarSetDisabledImageLeft( gadget, winFindImage( "ProgressBarDisabledLeftEnd" ) );
		GadgetProgressBarSetDisabledImageRight( gadget, winFindImage( "ProgressBarDisabledRightEnd" ) );
		GadgetProgressBarSetDisabledImageCenter( gadget, winFindImage( "ProgressBarDisabledRepeatingCenter" ) );
		GadgetProgressBarSetDisabledImageSmallCenter( gadget, winFindImage( "ProgressBarDisabledSmallRepeatingCenter" ) );
		GadgetProgressBarSetDisabledBarColor( gadget, lightGray );
		GadgetProgressBarSetDisabledBarBorderColor( gadget, white );
		GadgetProgressBarSetDisabledBarImageLeft( gadget, winFindImage( "ProgressBarDisabledBarLeftEnd" ) );
		GadgetProgressBarSetDisabledBarImageRight( gadget, winFindImage( "ProgressBarDisabledBarRightEnd" ) );
		GadgetProgressBarSetDisabledBarImageCenter( gadget, winFindImage( "ProgressBarDisabledBarRepeatingCenter" ) );
		GadgetProgressBarSetDisabledBarImageSmallCenter( gadget, winFindImage( "ProgressBarDisabledBarSmallRepeatingCenter" ) );

		// Hilite
		GadgetProgressBarSetHiliteColor( gadget, green );
		GadgetProgressBarSetHiliteBorderColor( gadget, darkGreen );
		GadgetProgressBarSetHiliteImageLeft( gadget, winFindImage( "ProgressBarHiliteLeftEnd" ) );
		GadgetProgressBarSetHiliteImageRight( gadget, winFindImage( "ProgressBarHiliteRightEnd" ) );
		GadgetProgressBarSetHiliteImageCenter( gadget, winFindImage( "ProgressBarHiliteRepeatingCenter" ) );
		GadgetProgressBarSetHiliteImageSmallCenter( gadget, winFindImage( "ProgressBarHiliteSmallRepeatingCenter" ) );
		GadgetProgressBarSetHiliteBarColor( gadget, yellow );
		GadgetProgressBarSetHiliteBarBorderColor( gadget, white );
		GadgetProgressBarSetHiliteBarImageLeft( gadget, winFindImage( "ProgressBarHiliteBarLeftEnd" ) );
		GadgetProgressBarSetHiliteBarImageRight( gadget, winFindImage( "ProgressBarHiliteBarRightEnd" ) );
		GadgetProgressBarSetHiliteBarImageCenter( gadget, winFindImage( "ProgressBarHiliteBarRepeatingCenter" ) );
		GadgetProgressBarSetHiliteBarImageSmallCenter( gadget, winFindImage( "ProgressBarHiliteBarSmallRepeatingCenter" ) );

	}  // end else if
	else if( BitTest( instData->getStyle(), GWS_STATIC_TEXT ) )
	{

		// enabled
		GadgetStaticTextSetEnabledImage( gadget, winFindImage( "StaticTextEnabled" ) );
		GadgetStaticTextSetEnabledColor( gadget, red );
		GadgetStaticTextSetEnabledBorderColor( gadget, lightRed );

		// disabled
		GadgetStaticTextSetDisabledImage( gadget, winFindImage( "StaticTextDisabled" ) );
		GadgetStaticTextSetDisabledColor( gadget, darkGray );
		GadgetStaticTextSetDisabledBorderColor( gadget, lightGray );

		// hilite
		GadgetStaticTextSetHiliteImage( gadget, winFindImage( "StaticTextHilite" ) );
		GadgetStaticTextSetHiliteColor( gadget, darkGreen );
		GadgetStaticTextSetHiliteBorderColor( gadget, lightGreen );

		// set default text colors for the gadget
		gadget->winSetEnabledTextColors( enabledText, enabledTextBorder );
		gadget->winSetDisabledTextColors( disabledText, disabledTextBorder );
		gadget->winSetHiliteTextColors( hiliteText, hiliteTextBorder );
		gadget->winSetIMECompositeTextColors( imeCompositeText, imeCompositeTextBorder );

	}  // end else if
	else if( BitTest( instData->getStyle(), GWS_ENTRY_FIELD ) )
	{

		// enabled
		GadgetTextEntrySetEnabledImageLeft( gadget, winFindImage( "TextEntryEnabledLeftEnd" ) );
		GadgetTextEntrySetEnabledImageRight( gadget, winFindImage( "TextEntryEnabledRightEnd" ) );
		GadgetTextEntrySetEnabledImageCenter( gadget, winFindImage( "TextEntryEnabledRepeatingCenter" ) );
		GadgetTextEntrySetEnabledImageSmallCenter( gadget, winFindImage( "TextEntryEnabledSmallRepeatingCenter" ) );
		GadgetTextEntrySetEnabledColor( gadget, red );
		GadgetTextEntrySetEnabledBorderColor( gadget, lightRed );

		// disabled
		GadgetTextEntrySetDisabledImageLeft( gadget, winFindImage( "TextEntryDisabledLeftEnd" ) );
		GadgetTextEntrySetDisabledImageRight( gadget, winFindImage( "TextEntryDisabledRightEnd" ) );
		GadgetTextEntrySetDisabledImageCenter( gadget, winFindImage( "TextEntryDisabledRepeatingCenter" ) );
		GadgetTextEntrySetDisabledImageSmallCenter( gadget, winFindImage( "TextEntryDisabledSmallRepeatingCenter" ) );
		GadgetTextEntrySetDisabledColor( gadget, gray );
		GadgetTextEntrySetDisabledBorderColor( gadget, black );

		// hilited
		GadgetTextEntrySetHiliteImageLeft( gadget, winFindImage( "TextEntryHiliteLeftEnd" ) );
		GadgetTextEntrySetHiliteImageRight( gadget, winFindImage( "TextEntryHiliteRightEnd" ) );
		GadgetTextEntrySetHiliteImageCenter( gadget, winFindImage( "TextEntryHiliteRepeatingCenter" ) );
		GadgetTextEntrySetHiliteImageSmallCenter( gadget, winFindImage( "TextEntryHiliteSmallRepeatingCenter" ) );
		GadgetTextEntrySetHiliteColor( gadget, green );
		GadgetTextEntrySetHiliteBorderColor( gadget, darkGreen );

		// set default text colors for the gadget
		gadget->winSetEnabledTextColors( enabledText, enabledTextBorder );
		gadget->winSetDisabledTextColors( disabledText, disabledTextBorder );
		gadget->winSetHiliteTextColors( hiliteText, hiliteTextBorder );
		gadget->winSetIMECompositeTextColors( imeCompositeText, imeCompositeTextBorder );

	}  // end else if

}  // end assignDefaultGadgetLook
