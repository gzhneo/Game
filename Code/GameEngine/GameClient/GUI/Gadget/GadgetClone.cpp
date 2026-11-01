/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// GadgetClone.cpp -- gadgets with the look of a gadget from a window file, see GadgetClone.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/NameKeyGenerator.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/GadgetClone.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"

#include <cstring>

void GadgetCopyDrawData( GameWindow *from, GameWindow *to )
{
	if( from == nullptr || to == nullptr )
		return;
	WinInstanceData *src = from->winGetInstanceData();
	WinInstanceData *dst = to->winGetInstanceData();
	for( Int i = 0; i < MAX_DRAW_DATA; i++ )
	{
		dst->m_enabledDrawData[ i ] = src->m_enabledDrawData[ i ];
		dst->m_disabledDrawData[ i ] = src->m_disabledDrawData[ i ];
		dst->m_hiliteDrawData[ i ] = src->m_hiliteDrawData[ i ];
	}
}

// The look of the model under a new name; the text, tooltip, video and owner belong to the model.
static WinInstanceData cloneInstanceData( GameWindow *model, const char *decoratedName, const char *textLabel )
{
	WinInstanceData instData = *model->winGetInstanceData();
	instData.m_text = nullptr;
	instData.m_tooltip = nullptr;
	instData.m_videoBuffer = nullptr;
	instData.m_owner = nullptr;
	instData.m_state = 0;
	instData.m_decoratedNameString = decoratedName;
	if( textLabel )
		instData.m_textLabelString = textLabel;
	else
		instData.m_textLabelString.clear();
	instData.m_tooltipString.clear();
	instData.m_id = static_cast<Int>( TheNameKeyGenerator->nameToKey( instData.m_decoratedNameString ) );
	return instData;
}

// The gadget constructors show a text label as it is; the layout loader looks it up, so do the same.
static Bool hasText( GameWindow *window, const char *textLabel )
{
	return window != nullptr && textLabel != nullptr && *textLabel != 0;
}

// Models may be hidden or disabled in the window file; the clone starts usable.
static UnsignedInt cloneStatus( GameWindow *model )
{
	UnsignedInt status = model->winGetStatus();
	BitClear( status, WIN_STATUS_HIDDEN );
	BitSet( status, WIN_STATUS_ENABLED );
	return status;
}

static Bool usable( GameWindow *model, GameWindow *parent, UnsignedInt style )
{
	return model != nullptr && parent != nullptr && BitTest( model->winGetStyle(), style );
}

GameWindow *GadgetCloneCheckBox( GameWindow *model, GameWindow *parent, const char *decoratedName,
																 Int x, Int y, Int width, Int height, const char *textLabel )
{
	if( !usable( model, parent, GWS_CHECK_BOX ) )
		return nullptr;
	WinInstanceData instData = cloneInstanceData( model, decoratedName, textLabel );
	GameWindow *box = TheWindowManager->gogoGadgetCheckbox( parent, cloneStatus( model ), x, y, width, height,
																													&instData, instData.m_font, FALSE );
	if( box )
		box->winSetWindowId( instData.m_id );
	if( hasText( box, textLabel ) )
		GadgetCheckBoxSetText( box, TheGameText->fetch( textLabel ) );
	return box;
}

GameWindow *GadgetCloneStaticText( GameWindow *model, GameWindow *parent, const char *decoratedName,
																	 Int x, Int y, Int width, Int height, const char *textLabel )
{
	if( !usable( model, parent, GWS_STATIC_TEXT ) )
		return nullptr;
	const TextData *modelData = static_cast<const TextData *>( model->winGetUserData() );
	TextData data;
	memset( &data, 0, sizeof( data ) );
	if( modelData )
	{
		data.centered = modelData->centered;
		data.centeredVertically = modelData->centeredVertically;
		data.leftMargin = modelData->leftMargin;
		data.topMargin = modelData->topMargin;
	}
	WinInstanceData instData = cloneInstanceData( model, decoratedName, textLabel );
	GameWindow *text = TheWindowManager->gogoGadgetStaticText( parent, cloneStatus( model ), x, y, width, height,
																														 &instData, &data, instData.m_font, FALSE );
	if( text )
		text->winSetWindowId( instData.m_id );
	if( hasText( text, textLabel ) )
		GadgetStaticTextSetText( text, TheGameText->fetch( textLabel ) );
	return text;
}

GameWindow *GadgetClonePushButton( GameWindow *model, GameWindow *parent, const char *decoratedName,
																	 Int x, Int y, Int width, Int height, const char *textLabel )
{
	if( !usable( model, parent, GWS_PUSH_BUTTON ) )
		return nullptr;
	WinInstanceData instData = cloneInstanceData( model, decoratedName, textLabel );
	GameWindow *button = TheWindowManager->gogoGadgetPushButton( parent, cloneStatus( model ), x, y, width, height,
																															 &instData, instData.m_font, FALSE );
	if( button )
		button->winSetWindowId( instData.m_id );
	if( hasText( button, textLabel ) )
		GadgetButtonSetText( button, TheGameText->fetch( textLabel ) );
	return button;
}

GameWindow *GadgetCloneComboBox( GameWindow *model, GameWindow *parent, const char *decoratedName,
																 Int x, Int y, Int width, Int height )
{
	if( !usable( model, parent, GWS_COMBO_BOX ) )
		return nullptr;
	const ComboBoxData *modelData = static_cast<const ComboBoxData *>( model->winGetUserData() );
	if( modelData == nullptr )
		return nullptr;

	// set up like the layout loader does for a COMBOBOX entry
	ComboBoxData data;
	memset( &data, 0, sizeof( data ) );
	data.isEditable = FALSE;
	data.maxDisplay = modelData->maxDisplay;
	data.maxChars = modelData->maxChars;
	data.asciiOnly = modelData->asciiOnly;
	data.lettersAndNumbersOnly = modelData->lettersAndNumbersOnly;
	data.entryData = NEW EntryData;
	memset( data.entryData, 0, sizeof( EntryData ) );
	data.entryData->aSCIIOnly = data.asciiOnly;
	data.entryData->alphaNumericalOnly = data.lettersAndNumbersOnly;
	data.entryData->maxTextLen = static_cast<Short>( data.maxChars );
	data.listboxData = NEW ListboxData;
	memset( data.listboxData, 0, sizeof( ListboxData ) );
	data.listboxData->listLength = 10;
	data.listboxData->scrollBar = 1;
	data.listboxData->forceSelect = 1;
	data.listboxData->columns = 1;

	WinInstanceData instData = cloneInstanceData( model, decoratedName, nullptr );
	GameWindow *box = TheWindowManager->gogoGadgetComboBox( parent, cloneStatus( model ), x, y, width, height,
																													&instData, &data, instData.m_font, FALSE );
	if( box == nullptr )
		return nullptr;
	box->winSetWindowId( instData.m_id );

	GadgetCopyDrawData( GadgetComboBoxGetDropDownButton( model ), GadgetComboBoxGetDropDownButton( box ) );
	GadgetCopyDrawData( GadgetComboBoxGetEditBox( model ), GadgetComboBoxGetEditBox( box ) );
	GameWindow *modelList = GadgetComboBoxGetListBox( model );
	GameWindow *list = GadgetComboBoxGetListBox( box );
	GadgetCopyDrawData( modelList, list );
	if( modelList && list )
	{
		GadgetCopyDrawData( GadgetListBoxGetUpButton( modelList ), GadgetListBoxGetUpButton( list ) );
		GadgetCopyDrawData( GadgetListBoxGetDownButton( modelList ), GadgetListBoxGetDownButton( list ) );
		GameWindow *modelSlider = GadgetListBoxGetSlider( modelList );
		GameWindow *slider = GadgetListBoxGetSlider( list );
		GadgetCopyDrawData( modelSlider, slider );
		if( modelSlider && slider )
			GadgetCopyDrawData( modelSlider->winGetChild(), slider->winGetChild() );
	}
	GadgetComboBoxSetMaxDisplay( box, modelData->maxDisplay );
	return box;
}
