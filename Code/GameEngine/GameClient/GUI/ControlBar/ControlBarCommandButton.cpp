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

#define DEFINE_GUI_COMMMAND_NAMES

#include "Common/ActionManager.h"
#include "Common/GameType.h"
#include "Common/MultiplayerSettings.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Override.h"
#include "Common/PlayerTemplate.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ProductionPrerequisite.h"
#include "Common/SpecialPower.h"
#include "Common/ThingTemplate.h"
#include "Common/ThingFactory.h"
#include "Common/Upgrade.h"
#include "Common/Recorder.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/OCLUpdate.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Module/StealthUpdate.h"
#include "GameLogic/Module/RebuildHoleBehavior.h"
#include "GameLogic/ScriptEngine.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/ControlBar.h"
#include "GameClient/ControlBarScheme.h"
#include "GameClient/Drawable.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayStringManager.h"
#include "Common/GameEdition.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GameText.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetProgressBar.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/InGameUI.h"
#include "GameClient/WindowVideoManager.h"
#include "GameClient/ControlBarResizer.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/HotKey.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/GUICallbacks.h"
#include "GameNetwork/GameInfo.h"

// CommandButton and CommandSet: the buttons of the control bar and their sets

//-------------------------------------------------------------------------------------------------
/** parse command definition */
//-------------------------------------------------------------------------------------------------
void CommandButton::parseCommand( INI* ini, void *instance, void *store, const void *userData )
{
	const char *token = ini->getNextToken();
	Int i;

	for( i = 0; TheGuiCommandNames[ i ]; i++ )
	{

		if( stricmp( TheGuiCommandNames[ i ], token ) == 0 )
		{

			GUICommandType *command = static_cast<GUICommandType *>(store);
			*command = static_cast<GUICommandType>(i);
			return;

		}  // end if

	}  // end for i

	// The Generals CommandButton.ini uses the old name of this command (Command_*FromCommandCenter).
	// The Zero Hour data do not use it.
	if( _stricmp( token, "SPECIAL_POWER_FROM_COMMAND_CENTER" ) == 0 )
	{
		GUICommandType *command = static_cast<GUICommandType *>(store);
		*command = GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT;
		return;
	}

	// if we're here the command was not found
	throw INI_INVALID_DATA;

}  // end parseCommand

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
CommandButton::CommandButton( void )
{

	m_command = GUI_COMMAND_NONE;
	m_thingTemplate = nullptr;
	m_upgradeTemplate = nullptr;
	m_weaponSlot = PRIMARY_WEAPON;
	m_maxShotsToFire = 0x7fffffff;	// huge number
	m_science.clear();
	m_specialPower = nullptr;
	m_buttonImage = nullptr;

	//Code renderer handles these states now.
	//m_disabledImage = NULL;
	//m_hiliteImage = NULL;
	//m_pushedImage = NULL;

	m_flashCount = 0;

	// Added by Sadullah Nader
	// The purpose is to initialize these variable to values that are zero or empty

	m_conflictingLabel.clear();
	m_cursorName.clear();
	m_descriptionLabel.clear();
	m_invalidCursorName.clear();
	m_name.clear();
	m_options = 0;
	m_purchasedLabel.clear();
	m_textLabel.clear();

	// End Add
	
	m_window = nullptr;
	m_commandButtonBorder = COMMAND_BUTTON_BORDER_NONE;
	//m_prev = NULL;
	m_next = nullptr;			
	m_radiusCursor = RADIUSCURSOR_NONE;													
	
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
CommandButton::~CommandButton( void )
{
	
}

//-------------------------------------------------------------------------------------------------
Bool CommandButton::isValidRelationshipTarget(Relationship r) const
{
	UnsignedInt mask = 0;
	if (r == ENEMIES) mask |= NEED_TARGET_ENEMY_OBJECT;
	else if (r == ALLIES) mask |= NEED_TARGET_ALLY_OBJECT;
	else if (r == NEUTRAL) mask |= NEED_TARGET_NEUTRAL_OBJECT;

	return (m_options & mask) != 0;
}

//-------------------------------------------------------------------------------------------------
Bool CommandButton::isValidObjectTarget(const Player* sourcePlayer, const Object* targetObj) const
{
	if (!sourcePlayer || !targetObj)
		return false;

	Relationship r = sourcePlayer->getRelationship(targetObj->getTeam());

	return isValidRelationshipTarget(r);
}

//-------------------------------------------------------------------------------------------------
Bool CommandButton::isValidObjectTarget(const Object* sourceObj, const Object* targetObj) const
{
	if (!sourceObj || !targetObj)
		return false;

	Relationship r = sourceObj->getRelationship(targetObj);

	return isValidRelationshipTarget(r);
}

//-------------------------------------------------------------------------------------------------
Bool CommandButton::isValidToUseOn(const Object *sourceObj, const Object *targetObj, const Coord3D *targetLocation, CommandSourceType commandSource) const
{
	if (m_upgradeTemplate) {
		// @todo: Make a const version of pui. We're not altering the production queue, so this const-cast
		// is okay.
		ProductionUpdateInterface *pui = const_cast<Object*>(sourceObj)->getProductionUpdateInterface();
		if (pui) {
			const ProductionEntry *pe = pui->firstProduction();
			while (pe) {
				if (pe->getProductionUpgrade() != nullptr) 
					return false;
				pe = pui->nextProduction(pe);
			}
			return sourceObj->affectedByUpgrade(m_upgradeTemplate) && !sourceObj->hasUpgrade(m_upgradeTemplate);
		}
		// No ProductionUpdateInterface means we can't do this.
		return false;
	}

	if( BitTest( m_options, COMMAND_OPTION_NEED_OBJECT_TARGET ) && !targetObj ) 
	{
		return false;
	}

	Coord3D pos;
	if( targetLocation )
	{
		pos.set( targetLocation );
	}

	if( BitTest( m_options, NEED_TARGET_POS ) && !targetLocation ) 
	{
		if( targetObj )
		{
			pos.set( targetObj->getPosition() );
		}
		else
		{
			return false;
		}
	}
	
	if( BitTest( m_options, COMMAND_OPTION_NEED_OBJECT_TARGET ) ) 
	{
		return TheActionManager->canDoSpecialPowerAtObject( sourceObj, targetObj, commandSource, m_specialPower, m_options, false );
	}

	if( BitTest( m_options, NEED_TARGET_POS ) ) 
	{
		return TheActionManager->canDoSpecialPowerAtLocation( sourceObj, &pos, commandSource, m_specialPower, nullptr, m_options, false );
	}

	return TheActionManager->canDoSpecialPower( sourceObj, m_specialPower, commandSource, m_options, false );
}

//-------------------------------------------------------------------------------------------------
Bool CommandButton::isReady(const Object *sourceObj) const
{
	SpecialPowerModuleInterface *mod = sourceObj->getSpecialPowerModule( m_specialPower );
	if( mod && mod->getPercentReady() == 1.0f ) 
		return true;
	
	if (m_upgradeTemplate && sourceObj->affectedByUpgrade(m_upgradeTemplate) && !sourceObj->hasUpgrade(m_upgradeTemplate))
		return true;

	return false;
}

//-------------------------------------------------------------------------------------------------
Bool CommandButton::isValidObjectTarget(const Drawable* source, const Drawable* target) const
{
	return isValidObjectTarget(source ? source->getObject() : nullptr, target ? target->getObject() : nullptr);
}

//-------------------------------------------------------------------------------------------------
Bool CommandButton::isContextCommand() const
{
	return BitTest( m_options, CONTEXTMODE_COMMAND );
}

//-------------------------------------------------------------------------------------------------
// bleah. shouldn't be const, but is. sue me. (srj)
void CommandButton::copyImagesFrom( const CommandButton *button, Bool markUIDirtyIfChanged ) const
{
	if( m_buttonImage != button->getButtonImage() )
	{
		m_buttonImage = button->getButtonImage();

		//Code renderer handles these states now.
		//m_disabledImage = button->getDisabledImage();
		//m_hiliteImage = button->getHiliteImage();
		//m_pushedImage = button->getPushedImage();

		if( markUIDirtyIfChanged )
		{
			TheControlBar->markUIDirty();
		}
	}
}

//-------------------------------------------------------------------------------------------------
// bleah. shouldn't be const, but is. sue me. (Kris) -snork!
void CommandButton::copyButtonTextFrom( const CommandButton *button, Bool shortcutButton, Bool markUIDirtyIfChanged ) const
{
	//This function was added to change the strings when you upgrade from a DaisyCutter to a MOAB. All other special
	//powers are the same.
	Bool change = FALSE;
	if( shortcutButton )
	{
		//Not the best code, but conflicting label means shortcut label (most won't have any string specified).
		if( button->getConflictingLabel().isNotEmpty() && m_textLabel.compare( button->getConflictingLabel() ) )
		{
			m_textLabel = button->getConflictingLabel();
			change = TRUE;
		}
	}
	else
	{	
		//Copy the text from the purchase science button if it exists (most won't).
		if( button->getTextLabel().isNotEmpty() && m_textLabel.compare( button->getTextLabel() ) )
		{
			m_textLabel = button->getTextLabel();
			change = TRUE;
		}
	}
	if( button->getDescriptionLabel().isNotEmpty() && m_descriptionLabel.compare( button->getDescriptionLabel() ) )
	{
		m_descriptionLabel = button->getDescriptionLabel();
		change = TRUE;
	}
	if( markUIDirtyIfChanged && change )
	{
		TheControlBar->markUIDirty();
	}
}

//-------------------------------------------------------------------------------------------------
/** Parse a single command button definition */
//-------------------------------------------------------------------------------------------------
void CommandSet::parseCommandButton( INI* ini, void *instance, void *store, const void *userData )
{
	const char *token = ini->getNextToken();

	// get find the command button from this name
	const CommandButton *commandButton = TheControlBar->findCommandButton( AsciiString( token ) );
	if( commandButton == nullptr )
	{

		DEBUG_CRASH(( "[LINE: %d - FILE: '%s'] Unknown command '%s' found in command set\n",
								  ini->getLineNum(), ini->getFilename().str(), token ));
		throw INI_INVALID_DATA;

	}  // end if

	// get the index to store the command at, and the command array itself
	const CommandButton **buttonArray = static_cast<const CommandButton **>(store);
	Int buttonIndex = static_cast<Int>(reinterpret_cast<intptr_t>(userData));

	// sanity
	DEBUG_ASSERTCRASH( buttonIndex < MAX_COMMANDS_PER_SET, ("parseCommandButton: button index '%d' out of range\n", 
										 buttonIndex) );

	// save it
	buttonArray[ buttonIndex ] = commandButton;

}  // end parseCommand

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
CommandSet::CommandSet(const AsciiString& name) : 
	m_name(name),
	m_next(nullptr)
{
	for( Int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
		m_command[ i ] = nullptr;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
const CommandButton* CommandSet::getCommandButton(Int i) const 
{ 
	const CommandButton* button;
  // Check for TheGameLogic == null, cause it is in Worldbuilder, and wb gets command bar info. jba.
	if (TheGameLogic && TheGameLogic->findControlBarOverride(m_name, i, button))
		return button;

	return m_command[i]; 
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void CommandSet::friend_addToList(CommandSet** listHead)
{
	m_next = *listHead;
	*listHead = this;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
CommandSet::~CommandSet( void )
{

}  // end ~CommandSet

//-------------------------------------------------------------------------------------------------
void CommandButton::cacheButtonImage()
{
	if (!TheMappedImageCollection) {
		return;
	}
	if( m_buttonImageName.isNotEmpty() )
	{
		m_buttonImage = TheMappedImageCollection->findImageByName( m_buttonImageName );
		DEBUG_ASSERTCRASH( m_buttonImage, ("CommandButton: %s is looking for button image %s but can't find it. Skipping...", m_name.str(), m_buttonImageName.str() ) );
		m_buttonImageName.clear();	// we're done with this, so nuke it
	}
}
