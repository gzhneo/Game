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

// ControlBar: the special power shortcut bar

//-------------------------------------------------------------------------------------------------
// The Generals SpecialPower.ini has no "ShortcutPower". Generals shows the power bar while the
// player owns a command center (the powers live there); Zero Hour looks for any power with
// ShortcutPower.
//-------------------------------------------------------------------------------------------------
static Bool playerHasShortcutSpecialPower( Player *player )
{
	if( !isGameEditionZeroHour() )
		return player->findNaturalCommandCenter() != nullptr;
	return player->hasAnyShortcutSpecialPower();
}

void ControlBar::initSpecialPowershortcutBar( Player *player)
{
	Int i;
	for( i = 0; i < MAX_SPECIAL_POWER_SHORTCUTS; ++i )
	{
		m_specialPowerShortcutButtonParents[i] = nullptr;
		m_specialPowerShortcutButtons[i] = nullptr;
	}

	if(m_specialPowerLayout)
	{
		m_specialPowerLayout->destroyWindows();
		deleteInstanceOf(m_specialPowerLayout);
		m_specialPowerLayout = nullptr;
	}
	m_specialPowerShortcutParent = nullptr;
	m_currentlyUsedSpecialPowersButtons = 0;
	const PlayerTemplate *pt = player->getPlayerTemplate();

	if(!player || !pt|| !player->isLocalPlayer()
			|| pt->getSpecialPowerShortcutButtonCount() == 0  
			|| pt->getSpecialPowerShortcutWinName().isEmpty()
			|| !player->isPlayerActive())
		return;
	m_currentlyUsedSpecialPowersButtons = pt->getSpecialPowerShortcutButtonCount();
	AsciiString layoutName, tempName, windowName, parentName;
	layoutName = pt->getSpecialPowerShortcutWinName();
	m_specialPowerLayout = TheWindowManager->winCreateLayout(layoutName);
	m_specialPowerLayout->hide(TRUE);

	tempName = layoutName;
	tempName.concat(":GenPowersShortcutBarParent");
	NameKeyType id = TheNameKeyGenerator->nameToKey( tempName );
	m_specialPowerShortcutParent = TheWindowManager->winGetWindowFromId( nullptr, id );//m_scienceLayout->getFirstWindow();

	tempName = layoutName;
	tempName.concat(":ButtonCommand%d");
	parentName = layoutName;
	parentName.concat(":ButtonParent%d");
	m_currentlyUsedSpecialPowersButtons = MIN(pt->getSpecialPowerShortcutButtonCount(), MAX_SPECIAL_POWER_SHORTCUTS);
	for( i = 0; i < MAX_SPECIAL_POWER_SHORTCUTS; i++ )
	{
		windowName.format( tempName, i+1 );
		id = TheNameKeyGenerator->nameToKey( windowName.str() );
		m_specialPowerShortcutButtons[ i ] =
			TheWindowManager->winGetWindowFromId( m_specialPowerShortcutParent, id );
		// The Generals GenPowersShortcutBar*.wnd has only 3 to 5 buttons (Zero Hour 11); the missing ones
		// stay NULL as in populateSpecialPowerShortcut().
		if( m_specialPowerShortcutButtons[ i ] )
		{
			m_specialPowerShortcutButtons[ i ]->winSetStatus( WIN_STATUS_USE_OVERLAY_STATES );
			// Oh god... this is a total hack for shortcut buttons to handle rendering text top left corner...
			m_specialPowerShortcutButtons[ i ]->winSetStatus( WIN_STATUS_SHORTCUT_BUTTON );
		}

		windowName.format( parentName, i+1 );
		id = TheNameKeyGenerator->nameToKey( windowName.str() );
		m_specialPowerShortcutButtonParents[ i ] = 
			TheWindowManager->winGetWindowFromId( m_specialPowerShortcutParent, id );
	}  // end for i

}

void ControlBar::populateSpecialPowerShortcut( Player *player)
{
	const CommandSet *commandSet;
	Int i;
	if(!player || !player->getPlayerTemplate() 
			|| !player->isLocalPlayer() || m_currentlyUsedSpecialPowersButtons == 0)
		return;
	for( i = 0; i < MAX_SPECIAL_POWER_SHORTCUTS; ++i )
	{
		if (m_specialPowerShortcutButtons[i])
			m_specialPowerShortcutButtons[i]->winHide(TRUE);
		if (m_specialPowerShortcutButtonParents[i])
			m_specialPowerShortcutButtonParents[i]->winHide(TRUE);
		
	}
	
	// get command set
	if(player->getPlayerTemplate()->getSpecialPowerShortcutCommandSet().isEmpty() )
		return;
	commandSet = TheControlBar->findCommandSet(player->getPlayerTemplate()->getSpecialPowerShortcutCommandSet()); // TEMP WILL CHANGE TO PROPER WAY ONCE WORKING
	if(!commandSet)
		return;
	// populate the button with commands defined
	Int currentButton = 0;
	const CommandButton *commandButton;
	for( i = 0; i < m_currentlyUsedSpecialPowersButtons; i++ )
	{

		// get command button
		commandButton = commandSet->getCommandButton(i);

		// if button is not present, just hide the window
		if( commandButton == nullptr )
		{
			continue;
			// hide window on interface
			//m_specialPowerShortcutButtons[ i ]->winHide( TRUE );

		}  // end if
		else
		{

			if( BitTest( commandButton->getOptions(), NEED_UPGRADE ) )
			{
				const UpgradeTemplate *upgrade = commandButton->getUpgradeTemplate();
				if( upgrade && !ThePlayerList->getLocalPlayer()->hasUpgradeComplete( upgrade->getUpgradeMask() ) )
				{
					//Kris: 8/13/03 - Don't show shortcut buttons that require upgrades we don't have. As far as 
					//I know, only the radar van scan has this. The MOAB is handled differently (sciences).
					continue;
				}
			}

			//
			// commands that require sciences we don't have are hidden so they never show up
			// cause we can never pick "another" general technology throughout the game
			//
			if( BitTest( commandButton->getOptions(), NEED_SPECIAL_POWER_SCIENCE ) )
			{
				const SpecialPowerTemplate *power = commandButton->getSpecialPowerTemplate();

				if( !power )
				{
					//Should have the power.. button is probably missing the SpecialPower = xxx entry.
					DEBUG_CRASH( ("CommandButton %s needs a SpecialPower entry, but it's either incorrect or missing.", commandButton->getName().str()) );
					continue;
				}

				//We just need to find something that has the power.
				Object *obj = ThePlayerList->getLocalPlayer()->findMostReadyShortcutSpecialPowerOfType( commandButton->getSpecialPowerTemplate()->getSpecialPowerType() );
				if( !obj )
				{
					continue;
				}

				if( power->getRequiredScience() != SCIENCE_INVALID )
				{
					if( player->hasScience( power->getRequiredScience() ) == FALSE )
					{
						//Hide the power
						//m_specialPowerShortcutButtons[ i ]->winHide( TRUE );
						continue;
					}
					else
					{
						//The player does have the special power! Now determine if the images require
						//enhancement based on upgraded versions. This is determined by the command
						//button specifying a vector of sciences in the command button.
						Int bestIndex = -1;
						ScienceType science;
						for( Int scienceIndex = 0; scienceIndex < commandButton->getScienceVec().size(); ++scienceIndex )
						{
							science = commandButton->getScienceVec()[ scienceIndex ];
							
							//Keep going until we reach the end or don't have the required science!
							if( player->hasScience( science ) )
							{
								bestIndex = scienceIndex;
							}
							else
							{
								break;
							}
						}

						if( bestIndex != -1 )
						{
							//Now get the best sciencetype.
							science = commandButton->getScienceVec()[ bestIndex ];

							const CommandSet *commandSet1;
							const CommandSet *commandSet3;
							const CommandSet *commandSet8;
							Int i3;

							// get command set
							if( !player || !player->getPlayerTemplate() 
									|| player->getPlayerTemplate()->getPurchaseScienceCommandSetRank1().isEmpty()
									|| player->getPlayerTemplate()->getPurchaseScienceCommandSetRank3().isEmpty()
									|| player->getPlayerTemplate()->getPurchaseScienceCommandSetRank8().isEmpty() )
							{
								continue;
							}
							commandSet1 = TheControlBar->findCommandSet( player->getPlayerTemplate()->getPurchaseScienceCommandSetRank1() ); 
							commandSet3 = TheControlBar->findCommandSet( player->getPlayerTemplate()->getPurchaseScienceCommandSetRank3() ); 
							commandSet8 = TheControlBar->findCommandSet( player->getPlayerTemplate()->getPurchaseScienceCommandSetRank8() ); 

							if( !commandSet1 || !commandSet3 || !commandSet8 )
							{
								continue;
							}

							Bool found = FALSE;
							for( i3 = 0; !found && i3 < MAX_PURCHASE_SCIENCE_RANK_1; i3++ )
							{
								const CommandButton *command = commandSet1->getCommandButton( i3 );
								if( command && command->getCommandType() == GUI_COMMAND_PURCHASE_SCIENCE )
								{
									//All purchase sciences specify a single science.
									if( command->getScienceVec().empty() )
									{
										DEBUG_CRASH( ("Commandbutton %s is a purchase science button without any science! Please add it.", command->getName().str() ) );
									}
									else if( command->getScienceVec()[0] == science )
									{
										commandButton->copyImagesFrom( command, TRUE );
										commandButton->copyButtonTextFrom( command, TRUE, TRUE );
										found = TRUE;
										break;
									}
								}
							}
							for( i3 = 0; !found && i3 < MAX_PURCHASE_SCIENCE_RANK_3; i3++ )
							{
								const CommandButton *command = commandSet3->getCommandButton( i3 );
								if( command && command->getCommandType() == GUI_COMMAND_PURCHASE_SCIENCE )
								{
									//All purchase sciences specify a single science.
									if( command->getScienceVec().empty() )
									{
										DEBUG_CRASH( ("Commandbutton %s is a purchase science button without any science! Please add it.", command->getName().str() ) );
									}
									else if( command->getScienceVec()[0] == science )
									{
										commandButton->copyImagesFrom( command, TRUE );
										commandButton->copyButtonTextFrom( command, TRUE, TRUE );
										found = TRUE;
										break;
									}
								}
							}
							for( i3 = 0; !found && i3 < MAX_PURCHASE_SCIENCE_RANK_8; i3++ )
							{
								const CommandButton *command = commandSet8->getCommandButton( i3 );
								if( command && command->getCommandType() == GUI_COMMAND_PURCHASE_SCIENCE )
								{
									//All purchase sciences specify a single science.
									if( command->getScienceVec().empty() )
									{
										DEBUG_CRASH( ("Commandbutton %s is a purchase science button without any science! Please add it.", command->getName().str() ) );
									}
									else if( command->getScienceVec()[0] == science )
									{
										commandButton->copyImagesFrom( command, TRUE );
										commandButton->copyButtonTextFrom( command, TRUE, TRUE );
										found = TRUE;
										break;
									}
								}
							}
						}
					}
				}
			}  // end if			
			else if( commandButton->getCommandType() == GUI_COMMAND_SELECT_ALL_UNITS_OF_TYPE )
			{
				//Make sure we actually have an object of type that we want to be able to select.
				Object *obj = ThePlayerList->getLocalPlayer()->findAnyExistingObjectWithThingTemplate( commandButton->getThingTemplate() );
				if( !obj )
				{
					continue;
				}
			}

			// make sure the window is not hidden
			m_specialPowerShortcutButtons[ currentButton ]->winHide( FALSE );
			m_specialPowerShortcutButtonParents[ currentButton ]->winHide( FALSE );
			// enable by default
			m_specialPowerShortcutButtons[ currentButton ]->winEnable( TRUE );
			m_specialPowerShortcutButtonParents[ currentButton ]->winEnable( TRUE );

			// populate the visible button with data from the command button
			setControlCommand( m_specialPowerShortcutButtons[ currentButton ], commandButton );
			GadgetButtonSetAltSound(m_specialPowerShortcutButtons[ currentButton ], "GUIGenShortcutClick");
			currentButton++;
					
		}  // end else

	}  // end for i
	if(m_contextParent[ CP_MASTER ] && !m_contextParent[ CP_MASTER ]->winIsHidden() && m_specialPowerShortcutParent->winIsHidden())
	{
		showSpecialPowerShortcut();
		animateSpecialPowerShortcut(TRUE);
	}
	updateSpecialPowerShortcut();
}

//-------------------------------------------------------------------------------------------------
Bool ControlBar::hasAnyShortcutSelection() const
{
	for( Int i = 0; i < m_currentlyUsedSpecialPowersButtons; i++ )
	{
		GameWindow *win;
		const CommandButton *command;

		win = m_specialPowerShortcutButtons[ i ];
		if( win->winIsHidden() == TRUE )
			continue;

		// get the command from the control
		command = static_cast<const CommandButton *>(GadgetButtonGetData(win));
		if( !command )
			continue;

		if( command->getCommandType() == GUI_COMMAND_SELECT_ALL_UNITS_OF_TYPE )
		{
			//We found one, so we'll always show shortcuts!
			return TRUE;
		}
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
void ControlBar::updateSpecialPowerShortcut( void )
{
	if(!m_specialPowerShortcutParent
	   || !ThePlayerList || !ThePlayerList->getLocalPlayer())
		return;

	Bool hasShortcutSelectionButtons = hasAnyShortcutSelection();
	Bool hasAnyShortcutSpecialPower = playerHasShortcutSpecialPower( ThePlayerList->getLocalPlayer() );

	Bool hasValidShortcutButton = hasShortcutSelectionButtons || hasAnyShortcutSpecialPower;

	if( hasValidShortcutButton
		  && m_specialPowerShortcutParent->winIsHidden() 
			&& m_contextParent[ CP_MASTER ] 
			&& !m_contextParent[ CP_MASTER ]->winIsHidden() )
	{
		showSpecialPowerShortcut();
		animateSpecialPowerShortcut(TRUE);
	}
	else if( !hasValidShortcutButton 
					 && !m_specialPowerShortcutParent->winIsHidden() 
					 && m_animateWindowManagerForGenShortcuts->isFinished() )
	{
		animateSpecialPowerShortcut(FALSE);		
	}

	if(m_specialPowerShortcutParent->winIsHidden())
		return;

	if(!ThePlayerList->getLocalPlayer()->isPlayerActive())
	{
		hideSpecialPowerShortcut();
		return;
	}
	if(m_contextParent[ CP_MASTER ] && !m_contextParent[ CP_MASTER ]->winIsHidden() && m_specialPowerShortcutParent->winIsHidden())
		showSpecialPowerShortcut();

	for( Int i = 0; i < m_currentlyUsedSpecialPowersButtons; i++ )
	{
		GameWindow *win;
		const CommandButton *command;
		// get the window
		win = m_specialPowerShortcutButtons[ i ];

		if( win->winIsHidden() == TRUE )
			continue;
		// get the command from the control
		command = static_cast<const CommandButton *>(GadgetButtonGetData(win));
		//command = (const CommandButton *)win->winGetUserData();
		if( command == nullptr )
			continue;


		win->winClearStatus( WIN_STATUS_NOT_READY );
		win->winClearStatus( WIN_STATUS_ALWAYS_COLOR );
		

		// is the command available

		CommandAvailability availability = CommandAvailability::COMMAND_RESTRICTED;

		const SpecialPowerTemplate *spTemplate = command->getSpecialPowerTemplate();
		Object *obj = nullptr; 
		if( spTemplate )
		{
			obj = ThePlayerList->getLocalPlayer()->findMostReadyShortcutSpecialPowerOfType( command->getSpecialPowerTemplate()->getSpecialPowerType() );
			availability = getCommandAvailability( command, obj, win );
		}
		else if( command->getCommandType() == GUI_COMMAND_SELECT_ALL_UNITS_OF_TYPE )
		{
			availability = CommandAvailability::COMMAND_HIDDEN;
			Object *obj2 = ThePlayerList->getLocalPlayer()->findAnyExistingObjectWithThingTemplate( command->getThingTemplate() );
			if( obj2 )
			{
				//Make command available if it isn't a special power template shortcut power.
				availability = CommandAvailability::COMMAND_AVAILABLE;

				UnsignedInt mostReadyPercentage;
				obj2 = ThePlayerList->getLocalPlayer()->findMostReadyShortcutSpecialPowerForThing( command->getThingTemplate(), mostReadyPercentage );
				if( obj2 )
				{
					//Ugh... hacky.
					//Look for a command button for a special power and if so, then get the command availability for it.
					const CommandSet *commandSet = TheControlBar->findCommandSet( obj2->getCommandSetString() );
					if( commandSet )
					{
						for( Int commandIndex = 0; commandIndex < MAX_COMMANDS_PER_SET; commandIndex++ )
						{
							const CommandButton *evalButton = commandSet->getCommandButton( commandIndex );
							GameWindow *evalButtonWin = m_commandWindows[ commandIndex ];
							if( evalButton && evalButton->getCommandType() == GUI_COMMAND_SPECIAL_POWER )
							{
								//We want to evaluate the special powerbutton... but apply the clock overlay to our button!
								availability = getCommandAvailability( evalButton, obj2, evalButtonWin, win );
								break;
							}
						}
					}
				}
			}
		}

		// enable/disable the window control
		switch( availability )
		{
			case CommandAvailability::COMMAND_HIDDEN:
				win->winHide( TRUE );
				break;
			case CommandAvailability::COMMAND_RESTRICTED:
				win->winEnable( FALSE );
				break;
			case CommandAvailability::COMMAND_NOT_READY:
				win->winEnable( FALSE );
				win->winSetStatus( WIN_STATUS_NOT_READY );
				break;
			case CommandAvailability::COMMAND_CANT_AFFORD:
				win->winEnable( FALSE );
				win->winSetStatus( WIN_STATUS_ALWAYS_COLOR );
				break;
			default:
				win->winEnable( TRUE );
				break;
		}

	}
}

//-------------------------------------------------------------------------------------------------
void ControlBar::drawSpecialPowerShortcutMultiplierText()
{
	for( Int i = 0; i < m_currentlyUsedSpecialPowersButtons; i++ )
	{
		GameWindow *win;
		const CommandButton *command;
		// get the window
		win = m_specialPowerShortcutButtons[ i ];

		if( win->winIsHidden() == TRUE )
			continue;
		// get the command from the control
		command = static_cast<const CommandButton *>(GadgetButtonGetData(win));
		//command = (const CommandButton *)win->winGetUserData();
		if( command == nullptr )
			continue;

		//draw superweapon ready multipliers
		for( int i2 = 0; i2 < MAX_SPECIAL_POWER_SHORTCUTS; i2++ )
		{
			if( !m_shortcutDisplayStrings[ i2 ] )
			{
				//m_shortcutDisplayStrings[ i ] = TheDisplayStringManager->newDisplayString();
				//m_shortcutDisplayStrings[ i ]->setFont( TheFontLibrary->getFont( "Arial", 16, false ) );
			}
			
			const SpecialPowerTemplate *spTemplate = command->getSpecialPowerTemplate();
			Int numReady = 0;
			if( spTemplate )
			{
				numReady = ThePlayerList->getLocalPlayer()->countReadyShortcutSpecialPowersOfType( spTemplate->getSpecialPowerType() );
			}
			if( numReady > 1 ) // Lorenzen changed... Displaying a "1" is superfluous
			{
				UnicodeString unibuffer;
				unibuffer.format( L"%d", numReady );
				
				GadgetButtonSetText( win, unibuffer );

				//m_shortcutDisplayStrings[ i ]->setText( unibuffer );
				//TheControlBar->m_shortcutDisplayStrings[ i ]->draw( 600, i * 40 + 40, GameMakeColor(255,255,255,255), GameMakeColor(0,0,0,0), 0, 0 );
			}
			else
			{
				UnicodeString unibuffer;
				GadgetButtonSetText( win, unibuffer );
				//TheDisplayStringManager->freeDisplayString( m_shortcutDisplayStrings[ i ] );
				//m_shortcutDisplayStrings[ i ] = NULL;
			}
		}
	}
}

void ControlBar::animateSpecialPowerShortcut( Bool isOn )
{
	if(!m_specialPowerShortcutParent || !m_animateWindowManagerForGenShortcuts || !m_currentlyUsedSpecialPowersButtons)
		return;
	Bool dontAnimate = TRUE;
	for( Int i = 0; i < m_currentlyUsedSpecialPowersButtons; ++i )
	{
		if (m_specialPowerShortcutButtons[i]->winGetUserData())
		{
			dontAnimate = FALSE;
			break;
		}
	}
	if(dontAnimate)
		return;

	if(isOn)
	{	
		m_animateWindowManagerForGenShortcuts->reset();
		m_animateWindowManagerForGenShortcuts->registerGameWindow(m_specialPowerShortcutParent,AnimTypes::WIN_ANIMATION_SLIDE_RIGHT,TRUE,500,0);
	}
	else
	{
		m_animateWindowManagerForGenShortcuts->reverseAnimateWindow();
	}
}

void ControlBar::showSpecialPowerShortcut( void )
{
	if(TheScriptEngine->isGameEnding() || !m_specialPowerShortcutParent 
		|| !ThePlayerList || !ThePlayerList->getLocalPlayer())
		return;
	Bool dontAnimate = TRUE;
	for( Int i = 0; i < m_currentlyUsedSpecialPowersButtons; ++i )
	{
		if (m_specialPowerShortcutButtons[i]->winGetUserData())
		{
			dontAnimate = FALSE;
			break;
		}
	}
	if( dontAnimate || (!playerHasShortcutSpecialPower( ThePlayerList->getLocalPlayer() ) && !hasAnyShortcutSelection()) )
		return;
	m_specialPowerShortcutParent->winHide(FALSE);
	populateSpecialPowerShortcut(ThePlayerList->getLocalPlayer());
		
}

void ControlBar::hideSpecialPowerShortcut( void )
{
	if(!m_specialPowerShortcutParent)
		return;
	
	m_specialPowerShortcutParent->winHide(TRUE);
		
}
