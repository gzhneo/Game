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

// FILE: MessageBox.cpp /////////////////////////////////////////////////////////
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
// File name: MessageBox.cpp
//
// Created:   Chris Huybregts, June 2001
//
// Desc:      the Message Box control
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GameEngine.h"
#include "Common/NameKeyGenerator.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/Shell.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/MessageBox.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameFont.h"
#include "GameClient/GlobalLanguage.h"


GameWindow *MessageBoxYesNo(UnicodeString titleString,UnicodeString bodyString,GameWinMsgBoxFunc yesCallback,GameWinMsgBoxFunc noCallback)  ///< convenience function for displaying a Message box with Yes and No buttons
{
	return TheWindowManager->gogoMessageBox(-1,-1,-1,-1,MSG_BOX_NO | MSG_BOX_YES , titleString, bodyString, yesCallback, noCallback, nullptr, nullptr);
}
GameWindow *QuitMessageBoxYesNo(UnicodeString titleString,UnicodeString bodyString,GameWinMsgBoxFunc yesCallback,GameWinMsgBoxFunc noCallback)  ///< convenience function for displaying a Message box with Yes and No buttons
{
	return TheWindowManager->gogoMessageBox(-1,-1,-1,-1,MSG_BOX_NO | MSG_BOX_YES , titleString, bodyString, yesCallback, noCallback, nullptr, nullptr, TRUE);
}


GameWindow *MessageBoxYesNoCancel(UnicodeString titleString,UnicodeString bodyString, GameWinMsgBoxFunc yesCallback, GameWinMsgBoxFunc noCallback, GameWinMsgBoxFunc cancelCallback)///< convenience function for displaying a Message box with Yes,No and Cancel buttons
{
	return TheWindowManager->gogoMessageBox(-1,-1,-1,-1,MSG_BOX_NO | MSG_BOX_YES | MSG_BOX_CANCEL , titleString, bodyString, yesCallback, noCallback, nullptr, cancelCallback);
}


GameWindow *MessageBoxOkCancel(UnicodeString titleString,UnicodeString bodyString,GameWinMsgBoxFunc okCallback,GameWinMsgBoxFunc cancelCallback)///< convenience function for displaying a Message box with Ok and Cancel buttons
{
	return TheWindowManager->gogoMessageBox(-1,-1,-1,-1,MSG_BOX_OK | MSG_BOX_CANCEL , titleString, bodyString, nullptr, nullptr, okCallback, cancelCallback);
}

GameWindow *MessageBoxOk(UnicodeString titleString,UnicodeString bodyString,GameWinMsgBoxFunc okCallback)///< convenience function for displaying a Message box with Ok button
{
	return TheWindowManager->gogoMessageBox(-1,-1,-1,-1,MSG_BOX_OK, titleString, bodyString, nullptr, nullptr, okCallback, nullptr);
}


GameWindow *MessageBoxCancel(UnicodeString titleString,UnicodeString bodyString,GameWinMsgBoxFunc cancelCallback)///< convenience function for displaying a Message box with Cancel button
{
	return TheWindowManager->gogoMessageBox(-1,-1,-1,-1, MSG_BOX_CANCEL, titleString, bodyString, nullptr, nullptr, nullptr, cancelCallback);
}


namespace
{

struct GrowPass
{
	GameWindow *text;
	Int textLeft, textTop, textRight, textBottom;
	Int grow;
	Int screenWidth;
};

// Windows that enclose the text (the frame, wherever it sits in the tree) change their height; windows
// below the text move with the lower edge, unless their parent already moved.
void growWindows( GameWindow *window, const GrowPass &pass, Bool parentMoved )
{
	for( GameWindow *child = window->winGetChild(); child; child = child->winGetNext() )
	{
		Bool moved = parentMoved;
		if( child != pass.text )
		{
			Int x, y, w, h;
			child->winGetScreenPosition( &x, &y );
			child->winGetSize( &w, &h );
			const Bool encloses = x <= pass.textLeft && y <= pass.textTop && x + w >= pass.textRight && y + h >= pass.textBottom;
			if( encloses && w < pass.screenWidth )
				child->winSetSize( w, h + pass.grow );
			else if( !parentMoved && y >= pass.textBottom )
			{
				Int px, py;
				child->winGetPosition( &px, &py );
				child->winSetPosition( px, py + pass.grow );
				moved = TRUE;
			}
		}
		growWindows( child, pass, moved );
	}
}

// The size of the message text in the 800 x 600 raster, the label size of the options pages.
constexpr Int MESSAGE_POINTS = 12;

GameWindow *findChild( GameWindow *messageBox, const char *name )
{
	return TheWindowManager->winGetWindowFromId( messageBox, TheNameKeyGenerator->nameToKey( AsciiString( name ) ) );
}

// The levels of the options pages: the title in the font of the buttons, the text in the label size.
void setFonts( GameWindow *title, GameWindow *text, GameWindow *button )
{
	if( button != nullptr && button->winGetFont() != nullptr )
		title->winSetFont( button->winGetFont() );
	const GameFont *face = text->winGetFont();
	if( face != nullptr && TheFontLibrary != nullptr && TheGlobalLanguageData != nullptr )
	{
		if( GameFont *font = TheFontLibrary->getFont( face->nameString, TheGlobalLanguageData->adjustFontSize( MESSAGE_POINTS ), FALSE ) )
			text->winSetFont( font );
	}
}

// The text starts where the title starts and is as wide.
void alignWithTitle( GameWindow *title, GameWindow *text )
{
	Int titleX, titleY, textX, textY;
	title->winGetScreenPosition( &titleX, &titleY );
	text->winGetScreenPosition( &textX, &textY );
	Int titleW, titleH, textW, textH;
	title->winGetSize( &titleW, &titleH );
	text->winGetSize( &textW, &textH );
	Int x, y;
	text->winGetPosition( &x, &y );
	text->winSetPosition( x + titleX - textX, y );
	text->winSetSize( titleW, textH );
}

// The visible buttons side by side in the middle of the title, a quarter of a button apart (the file puts
// Yes and No at the two outer places).
void centerButtons( GameWindow *messageBox, GameWindow *title )
{
	GameWindow *buttons[4];
	Int count = 0;
	for( const char *name : { "MessageBox.wnd:ButtonOk", "MessageBox.wnd:ButtonYes", "MessageBox.wnd:ButtonNo", "MessageBox.wnd:ButtonCancel" } )
	{
		GameWindow *button = findChild( messageBox, name );
		if( button != nullptr && !button->winIsHidden() )
			buttons[count++] = button;
	}
	if( count < 2 )
		return;
	// left to right as the file placed them
	for( Int i = 1; i < count; ++i )
	{
		for( Int j = i; j > 0; --j )
		{
			Int x, y, prevX, prevY;
			buttons[j]->winGetScreenPosition( &x, &y );
			buttons[j - 1]->winGetScreenPosition( &prevX, &prevY );
			if( x >= prevX )
				break;
			GameWindow *swap = buttons[j];
			buttons[j] = buttons[j - 1];
			buttons[j - 1] = swap;
		}
	}
	Int width = 0, gap = 0;
	for( Int i = 0; i < count; ++i )
	{
		Int w, h;
		buttons[i]->winGetSize( &w, &h );
		width += w;
		if( i == 0 )
			gap = w / 4;
	}
	width += gap * ( count - 1 );
	Int titleX, titleY, titleW, titleH;
	title->winGetScreenPosition( &titleX, &titleY );
	title->winGetSize( &titleW, &titleH );
	Int left = titleX + ( titleW - width ) / 2;
	for( Int i = 0; i < count; ++i )
	{
		Int screenX, screenY, x, y, w, h;
		buttons[i]->winGetScreenPosition( &screenX, &screenY );
		buttons[i]->winGetPosition( &x, &y );
		buttons[i]->winSetPosition( x + left - screenX, y );
		buttons[i]->winGetSize( &w, &h );
		left += w + gap;
	}
}

}	// namespace

void MessageBoxFitText(GameWindow *messageBox)
{
	if( messageBox == nullptr )
		return;
	GameWindow *text = findChild( messageBox, "MessageBox.wnd:StaticTextMessage" );
	if( text == nullptr )
		return;
	GameWindow *title = findChild( messageBox, "MessageBox.wnd:StaticTextTitle" );
	if( title != nullptr )
	{
		setFonts( title, text, findChild( messageBox, "MessageBox.wnd:ButtonOk" ) );
		alignWithTitle( title, text );
	}
	Int textW, textH;
	text->winGetSize( &textW, &textH );
	DisplayString *measure = TheDisplayStringManager->newDisplayString();
	measure->setFont( text->winGetFont() );
	measure->setWordWrap( textW );
	measure->setText( GadgetStaticTextGetText( text ) );
	Int needW, needH;
	measure->getSize( &needW, &needH );
	TheDisplayStringManager->freeDisplayString( measure );
	const GameFont *font = text->winGetFont();
	const Int minimum = font != nullptr ? 3 * font->height : textH;
	Int target = needH + needH / 8;
	if( target < minimum )
		target = minimum;
	const Int grow = target - textH;
	if( grow != 0 )
	{
		GrowPass pass;
		pass.text = text;
		text->winGetScreenPosition( &pass.textLeft, &pass.textTop );
		pass.textRight = pass.textLeft + textW;
		pass.textBottom = pass.textTop + textH;
		pass.grow = grow;
		pass.screenWidth = TheDisplay->getWidth();
		growWindows( messageBox, pass, FALSE );
		text->winSetSize( textW, textH + grow );
	}
	if( title != nullptr )
		centerButtons( messageBox, title );
}

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////



//-------------------------------------------------------------------------------------------------
/** Message Box window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType MessageBoxSystem( GameWindow *window, UnsignedInt msg, 
										 WindowMsgData mData1, WindowMsgData mData2 )
{
	
	
	switch( msg ) 
	{

		//---------------------------------------------------------------------------------------------
		case GWM_DESTROY:
		{
			delete static_cast<WindowMessageBoxData *>(window->winGetUserData());
			window->winSetUserData( nullptr );
			break;

		}  // end case

		// --------------------------------------------------------------------------------------------
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
			static NameKeyType buttonOkID = TheNameKeyGenerator->nameToKey( AsciiString( "MessageBox.wnd:ButtonOk" ) );
			static NameKeyType buttonYesID = TheNameKeyGenerator->nameToKey( AsciiString( "MessageBox.wnd:ButtonYes" ) );
			static NameKeyType buttonNoID = TheNameKeyGenerator->nameToKey( AsciiString( "MessageBox.wnd:ButtonNo" ) );
			static NameKeyType buttonCancelID = TheNameKeyGenerator->nameToKey( AsciiString( "MessageBox.wnd:ButtonCancel" ) );
			WindowMessageBoxData *MsgBoxCallbacks = static_cast<WindowMessageBoxData *>(window->winGetUserData());
			
			if( controlID == buttonOkID )
			{
				//simple enough,if we have a callback, call it, if not, then just destroy the window
				if (MsgBoxCallbacks->okCallback)
					MsgBoxCallbacks->okCallback();
				
				TheWindowManager->winDestroy(window);

			}  // end if
			else if( controlID == buttonYesID )
			{
				if (MsgBoxCallbacks->yesCallback)
					MsgBoxCallbacks->yesCallback();
				TheWindowManager->winDestroy(window);
			}  // end else if
			else if( controlID == buttonNoID )
			{
				if (MsgBoxCallbacks->noCallback)
					MsgBoxCallbacks->noCallback();
				TheWindowManager->winDestroy(window);
			}  // end else if
			else if( controlID == buttonCancelID )
			{
				if (MsgBoxCallbacks->cancelCallback)
					MsgBoxCallbacks->cancelCallback();
				TheWindowManager->winDestroy(window);
			}  // end else if
			
			break;

		}  // end selected
		
		//---------------------------------------------------------------------------------------------
		default:
			return WindowMsgHandledType::MSG_IGNORED;

	}  // end switch

	return WindowMsgHandledType::MSG_HANDLED;

}  // end MessageBoxSystem
//-------------------------------------------------------------------------------------------------
/** Message Box window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType QuitMessageBoxSystem( GameWindow *window, UnsignedInt msg, 
										 WindowMsgData mData1, WindowMsgData mData2 )
{
	
	
	switch( msg ) 
	{

		//---------------------------------------------------------------------------------------------
		case GWM_DESTROY:
		{
			delete static_cast<WindowMessageBoxData *>(window->winGetUserData());
			window->winSetUserData( nullptr );
			break;

		}  // end case

		// --------------------------------------------------------------------------------------------
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
			static NameKeyType buttonOkID = TheNameKeyGenerator->nameToKey( AsciiString( "QuitMessageBox.wnd:ButtonOk" ) );
			static NameKeyType buttonYesID = TheNameKeyGenerator->nameToKey( AsciiString( "QuitMessageBox.wnd:ButtonYes" ) );
			static NameKeyType buttonNoID = TheNameKeyGenerator->nameToKey( AsciiString( "QuitMessageBox.wnd:ButtonNo" ) );
			static NameKeyType buttonCancelID = TheNameKeyGenerator->nameToKey( AsciiString( "QuitMessageBox.wnd:ButtonCancel" ) );
			WindowMessageBoxData *MsgBoxCallbacks = static_cast<WindowMessageBoxData *>(window->winGetUserData());
			
			if( controlID == buttonOkID )
			{
				//simple enough,if we have a callback, call it, if not, then just destroy the window
				if (MsgBoxCallbacks->okCallback)
					MsgBoxCallbacks->okCallback();
				
				TheWindowManager->winDestroy(window);

			}  // end if
			else if( controlID == buttonYesID )
			{
				if (MsgBoxCallbacks->yesCallback)
					MsgBoxCallbacks->yesCallback();
				TheWindowManager->winDestroy(window);
			}  // end else if
			else if( controlID == buttonNoID )
			{
				if (MsgBoxCallbacks->noCallback)
					MsgBoxCallbacks->noCallback();
				TheWindowManager->winDestroy(window);
			}  // end else if
			else if( controlID == buttonCancelID )
			{
				if (MsgBoxCallbacks->cancelCallback)
					MsgBoxCallbacks->cancelCallback();
				TheWindowManager->winDestroy(window);
			}  // end else if
			
			break;

		}  // end selected
		
		//---------------------------------------------------------------------------------------------
		default:
			return WindowMsgHandledType::MSG_IGNORED;

	}  // end switch

	return WindowMsgHandledType::MSG_HANDLED;

}  // end MessageBoxSystem
