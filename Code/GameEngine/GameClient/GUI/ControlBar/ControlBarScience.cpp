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

// ControlBar: buying sciences (generals' promotions)

void ControlBar::populatePurchaseScience( Player* player )
{
//	TheInGameUI->deselectAllDrawables();

	const CommandSet *commandSet1;
	const CommandSet *commandSet3;
	const CommandSet *commandSet8;
	Int i;
	if(TheScriptEngine->isGameEnding())
		return;
	// get command set
	if(!player ||!player->getPlayerTemplate() || player->getPlayerTemplate()->getPurchaseScienceCommandSetRank1().isEmpty() || 
			player->getPlayerTemplate()->getPurchaseScienceCommandSetRank3().isEmpty() ||
			player->getPlayerTemplate()->getPurchaseScienceCommandSetRank8().isEmpty())
		return;
	commandSet1 = TheControlBar->findCommandSet(player->getPlayerTemplate()->getPurchaseScienceCommandSetRank1()); // TEMP WILL CHANGE TO PROPER WAY ONCE WORKING
	commandSet3 = TheControlBar->findCommandSet(player->getPlayerTemplate()->getPurchaseScienceCommandSetRank3()); // TEMP WILL CHANGE TO PROPER WAY ONCE WORKING
	commandSet8 = TheControlBar->findCommandSet(player->getPlayerTemplate()->getPurchaseScienceCommandSetRank8()); // TEMP WILL CHANGE TO PROPER WAY ONCE WORKING

	// The Generals GeneralsExpPoints.wnd has 3/12/1 science buttons (Zero Hour 4/15/4); missing windows
	// stay NULL and are skipped.
	for( i = 0; i < MAX_PURCHASE_SCIENCE_RANK_1; i++ )
		if( m_sciencePurchaseWindowsRank1[i] )
			m_sciencePurchaseWindowsRank1[i]->winHide(TRUE);
	for( i = 0; i < MAX_PURCHASE_SCIENCE_RANK_3; i++ )
		if( m_sciencePurchaseWindowsRank3[i] )
			m_sciencePurchaseWindowsRank3[i]->winHide(TRUE);
	for( i = 0; i < MAX_PURCHASE_SCIENCE_RANK_8; i++ )
		if( m_sciencePurchaseWindowsRank8[i] )
			m_sciencePurchaseWindowsRank8[i]->winHide(TRUE);


	// if no command set match is found hide all the buttons
	if( commandSet1 == nullptr ||
			commandSet3 == nullptr ||
			commandSet8 == nullptr )
		return;

	// populate the button with commands defined
	const CommandButton *commandButton;
	for( i = 0; i < MAX_PURCHASE_SCIENCE_RANK_1; i++ )
	{
		if( m_sciencePurchaseWindowsRank1[ i ] == nullptr )
			continue;	// the Generals window has fewer buttons

		// get command button
		commandButton = commandSet1->getCommandButton(i);

		// if button is not present, just hide the window
		if( commandButton == nullptr || BitTest( commandButton->getOptions(), SCRIPT_ONLY ) )
		{
			// hide window on interface
			m_sciencePurchaseWindowsRank1[ i ]->winHide( TRUE );
		}  // end if
		else
		{
			// make sure the window is not hidden
			m_sciencePurchaseWindowsRank1[ i ]->winHide( FALSE );

			// Disable by default
			m_sciencePurchaseWindowsRank1[ i ]->winEnable( FALSE );

			// populate the visible button with data from the command button

			setControlCommand( m_sciencePurchaseWindowsRank1[ i ], commandButton );
			if (!commandButton->getScienceVec().empty())
			{
				ScienceType	st = commandButton->getScienceVec()[ 0 ];

				if( player->isScienceDisabled( st ) )
				{
					//A script has deemed this science disabled.
					m_sciencePurchaseWindowsRank1[ i ]->winEnable( FALSE );
				}
				else if( player->isScienceHidden( st ) )
				{
					//A script has deemed this science unavailable, thus hidden
					m_sciencePurchaseWindowsRank1[ i ]->winHide( TRUE );
				}
				else
				{
					//Handle normal game logic cases!
					if(!player->hasScience(st) && TheScienceStore->playerHasPrereqsForScience(player, st) && TheScienceStore->getSciencePurchaseCost(st) <= player->getSciencePurchasePoints())
					{
						m_sciencePurchaseWindowsRank1[ i ]->winEnable( TRUE );
					}
					if(player->hasScience(st))
					{
						m_sciencePurchaseWindowsRank1[ i ]->winSetStatus(WIN_STATUS_ALWAYS_COLOR);
					}
					else
					{
						m_sciencePurchaseWindowsRank1[ i ]->winClearStatus(WIN_STATUS_ALWAYS_COLOR);
					}
					if(!TheScienceStore->playerHasRootPrereqsForScience(player, st))
						m_sciencePurchaseWindowsRank1[ i ]->winHide(TRUE);
				}
			}
		}  // end else

	}  // end for

	for( i = 0; i < MAX_PURCHASE_SCIENCE_RANK_3; i++ )
	{
		if( m_sciencePurchaseWindowsRank3[ i ] == nullptr )
			continue;	// the Generals window has fewer buttons

		// get command button
		commandButton = commandSet3->getCommandButton(i);

		// if button is not present, just hide the window
		if( commandButton == nullptr || BitTest( commandButton->getOptions(), SCRIPT_ONLY ) )
		{
			// hide window on interface
			m_sciencePurchaseWindowsRank3[ i ]->winHide( TRUE );
		}  // end if
		else
		{
			// make sure the window is not hidden
			m_sciencePurchaseWindowsRank3[ i ]->winHide( FALSE );

			// Disable by default
			m_sciencePurchaseWindowsRank3[ i ]->winEnable( FALSE );

			// populate the visible button with data from the command button

			setControlCommand( m_sciencePurchaseWindowsRank3[ i ], commandButton );
			ScienceType	st = SCIENCE_INVALID; 
			ScienceVec sv = commandButton->getScienceVec();
			if (! sv.empty())
			{
				st = sv[ 0 ];
			}

			if( player->isScienceDisabled( st ) )
			{
				//A script has deemed this science disabled.
				m_sciencePurchaseWindowsRank3[ i ]->winEnable( FALSE );
			}
			else if( player->isScienceHidden( st ) )
			{
				//A script has deemed this science unavailable, thus hidden
				m_sciencePurchaseWindowsRank3[ i ]->winHide( TRUE );
			}
			else
			{
				//Handle normal game logic cases!
				if(!player->hasScience(st) && TheScienceStore->playerHasPrereqsForScience(player, st) && TheScienceStore->getSciencePurchaseCost(st) <= player->getSciencePurchasePoints())
				{
					m_sciencePurchaseWindowsRank3[ i ]->winEnable( TRUE );
				}
				if(player->hasScience(st))
				{
					m_sciencePurchaseWindowsRank3[ i ]->winSetStatus(WIN_STATUS_ALWAYS_COLOR);
				}
				else
				{
					m_sciencePurchaseWindowsRank3[ i ]->winClearStatus(WIN_STATUS_ALWAYS_COLOR);
				}
				if(!TheScienceStore->playerHasRootPrereqsForScience(player, st))
					m_sciencePurchaseWindowsRank3[ i ]->winHide(TRUE);
			}

		}  // end else

	}  // end for

	for( i = 0; i < MAX_PURCHASE_SCIENCE_RANK_8; i++ )
	{
		if( m_sciencePurchaseWindowsRank8[ i ] == nullptr )
			continue;	// the Generals window has fewer buttons

		// get command button
		commandButton = commandSet8->getCommandButton(i);

		// if button is not present, just hide the window
		if( commandButton == nullptr || BitTest( commandButton->getOptions(), SCRIPT_ONLY ) )
		{
			// hide window on interface
			m_sciencePurchaseWindowsRank8[ i ]->winHide( TRUE );
		}  // end if
		else
		{
			// make sure the window is not hidden
			m_sciencePurchaseWindowsRank8[ i ]->winHide( FALSE );

			// Disable by default
			m_sciencePurchaseWindowsRank8[ i ]->winEnable( FALSE );

			// populate the visible button with data from the command button

			setControlCommand( m_sciencePurchaseWindowsRank8[ i ], commandButton );
			ScienceType	st = SCIENCE_INVALID; 
			st = commandButton->getScienceVec()[ 0 ];
			if( player->isScienceDisabled( st ) )
			{
				//A script has deemed this science disabled.
				m_sciencePurchaseWindowsRank8[ i ]->winEnable( FALSE );
			}
			else if( player->isScienceHidden( st ) )
			{
				//A script has deemed this science unavailable, thus hidden
				m_sciencePurchaseWindowsRank8[ i ]->winHide( TRUE );
			}
			else
			{
				//Handle normal game logic cases!
				if(!player->hasScience(st) && TheScienceStore->playerHasPrereqsForScience(player, st) && TheScienceStore->getSciencePurchaseCost(st) <= player->getSciencePurchasePoints())
				{
					m_sciencePurchaseWindowsRank8[ i ]->winEnable( TRUE );
				}
				if(player->hasScience(st))
				{
					m_sciencePurchaseWindowsRank8[ i ]->winSetStatus(WIN_STATUS_ALWAYS_COLOR);
				}
				else
				{
					m_sciencePurchaseWindowsRank8[ i ]->winClearStatus(WIN_STATUS_ALWAYS_COLOR);
				}
				if(!TheScienceStore->playerHasRootPrereqsForScience(player, st))
					m_sciencePurchaseWindowsRank8[ i ]->winHide(TRUE);
			}

		}  // end else

	}  // end for


	GameWindow *win = nullptr;
	UnicodeString tempUS;
	win = TheWindowManager->winGetWindowFromId( m_contextParent[ CP_PURCHASE_SCIENCE ], TheNameKeyGenerator->nameToKey( "GeneralsExpPoints.wnd:StaticTextRankPointsAvailable" ) );
	if(win)
	{
		tempUS.format(L"%d", player->getSciencePurchasePoints());
		GadgetStaticTextSetText(win, tempUS);
	}
	
// redundant to StaticTextTitle in the Zero Hour context
	// Generals shows the rank level in StaticTextLevel.
	if( !isGameEditionZeroHour() )
	{
		win = TheWindowManager->winGetWindowFromId( m_contextParent[ CP_PURCHASE_SCIENCE ], TheNameKeyGenerator->nameToKey( "GeneralsExpPoints.wnd:StaticTextLevel" ) );
		if(win)
		{
			tempUS.format(TheGameText->fetch("SCIENCE:Rank"), player->getRankLevel());
			GadgetStaticTextSetText(win, tempUS);
		}
	}
	
	win = TheWindowManager->winGetWindowFromId( m_contextParent[ CP_PURCHASE_SCIENCE ], TheNameKeyGenerator->nameToKey( "GeneralsExpPoints.wnd:ProgressBarExperience" ) );
	if(win)
	{
		Int progress;
		progress = ((player->getSkillPoints() - player->getSkillPointsLevelDown()) * 100) /(player->getSkillPointsLevelUp() - player->getSkillPointsLevelDown());
		GadgetProgressBarSetProgress(win, progress);
	}

	win = TheWindowManager->winGetWindowFromId( m_contextParent[ CP_PURCHASE_SCIENCE ], TheNameKeyGenerator->nameToKey( "GeneralsExpPoints.wnd:StaticTextTitle" ) );
	if(win)
	{
		AsciiString tempAs;

		tempAs.format("SCIENCE:Rank%d", player->getRankLevel());
		GadgetStaticTextSetText(win, TheGameText->fetch(tempAs));
	}
	
	
	//
	// to avoid a one frame delay where windows may become enabled/disabled, run the update
	// at once to get it all in the correct state immediately
	//
	updateContextPurchaseScience();
/*
	// get the side select buttons
	GameWindow* win = m_contextParent[ CP_PURCHASE_SCIENCE ];

	Color color = GameMakeColor(255, 255, 255, 255);

	/// @todo srj -- evil hack testing code. do not imitate.
	ScienceVec purchasable, potential;
	TheScienceStore->getPurchasableSciences(player, purchasable, potential);
	GadgetListBoxReset(win);
	for (ScienceVec::const_iterator it = purchasable.begin(); it != purchasable.end(); ++it)
	{
		ScienceType st = *it;
		UnicodeString u;
		u.translate(TheScienceStore->getInternalNameForScience(st));
		GadgetListBoxAddEntryText(win, u, color, -1, -1);
	}
	for (ScienceVec::const_iterator it2 = potential.begin(); it2 != potential.end(); ++it2)
	{
		ScienceType st = *it2;
		AsciiString foo = "(Not Yet)";
		foo.concat(TheScienceStore->getInternalNameForScience(st));
		UnicodeString u;
		u.translate(foo);
		GadgetListBoxAddEntryText(win, u, color, -1, -1);
	}
	GadgetListBoxAddEntryText(win, UnicodeString(L"Cancel"), color, -1, -1);*/

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void ControlBar::updateContextPurchaseScience( void )
{
	GameWindow *win =nullptr;
	Player *player = ThePlayerList->getLocalPlayer();
	win = TheWindowManager->winGetWindowFromId( m_contextParent[ CP_PURCHASE_SCIENCE ], TheNameKeyGenerator->nameToKey( "GeneralsExpPoints.wnd:ProgressBarExperience" ) );
	if(win)
	{
		Int progress;
		progress = ((player->getSkillPoints() - player->getSkillPointsLevelDown()) * 100) /(player->getSkillPointsLevelUp() - player->getSkillPointsLevelDown());
		GadgetProgressBarSetProgress(win, progress);
	}
	
//	win = TheWindowManager->winGetWindowFromId( m_contextParent[ CP_PURCHASE_SCIENCE ], TheNameKeyGenerator->nameToKey( "ControlBar.wnd:TextEntryGeneralName" ) );
//	if(win)
//	{
//		UnicodeString temp = GadgetTextEntryGetText(win);
//		if(temp.compare(player->getGeneralName()) != 0)
//			player->setGeneralName(temp);
//	}
/*
	/// @todo srj -- evil hack testing code. do not imitate.
	Object *obj = m_currentSelectedDrawable->getObject();

	if( obj == NULL )
		return;

	// sanity
	if( obj->isKindOf( KINDOF_COMMANDCENTER ) == FALSE )
		switchToContext( CB_CONTEXT_NONE, NULL );
	
	GameWindow* win = m_contextParent[ CP_PURCHASE_SCIENCE ];

	Int selected;
	GadgetListBoxGetSelected( win, &selected );
	if( selected != -1 )
	{
		UnicodeString usci = GadgetListBoxGetText( win, selected, 0 );
		AsciiString sci;
		sci.translate(usci);
		ScienceType st = usci.getCharAt(0) == '(' ? SCIENCE_INVALID : TheScienceStore->getScienceFromInternalName(sci);

		if (st != SCIENCE_INVALID)
		{
			GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_PURCHASE_SCIENCE );
			msg->appendIntegerArgument( st );
		}

		switchToContext( CB_CONTEXT_NONE, NULL );
	}
*/

}

//-------------------------------------------------------------------------------------------------
void ControlBar::onPlayerRankChanged(const Player *p)
{
	if (!p->isLocalPlayer())
		return;

	if(!(m_lastFlashedAtPointValue > ThePlayerList->getLocalPlayer()->getSciencePurchasePoints()))
	{
		if(TheTransitionHandler && TheInGameUI->getInputEnabled())
			TheTransitionHandler->setGroup("ControlBarArrow");
	}
//	populateSpecialPowerShortcut((Player *)p);
	m_genStarFlash = TRUE;
	/// @todo implement me
	markUIDirty();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void ControlBar::onPlayerSciencePurchasePointsChanged(const Player *p)
{
	if (!p->isLocalPlayer())
		return;
	if(!(m_lastFlashedAtPointValue > ThePlayerList->getLocalPlayer()->getSciencePurchasePoints()))
	{
		if(TheTransitionHandler && TheInGameUI->getInputEnabled())
			TheTransitionHandler->setGroup("ControlBarArrow");
	}
//	populateSpecialPowerShortcut((Player *)p);
	m_genStarFlash = TRUE;
	/// @todo implement me
	markUIDirty();
}

// ---------------------------------------------------------------------------------------
// Outside hook so when the genera's head is pushed, we can switch to the purchase science
// context
void ControlBar::updatePurchaseScience( void )
{
//	if(m_generalsScreenAnimate && TheGlobalData->m_animateWindows)
//	{
//		Bool wasFinished = m_generalsScreenAnimate->isFinished();
//		m_generalsScreenAnimate->update();
//		if (m_generalsScreenAnimate->isFinished() && !wasFinished && m_generalsScreenAnimate->isReversed())
//			m_contextParent[ CP_PURCHASE_SCIENCE ]->winHide(TRUE);
//	}
}

void ControlBar::showPurchaseScience( void )
{
	
	if(TheScriptEngine->isGameEnding())
		return;
	populatePurchaseScience(ThePlayerList->getLocalPlayer());
	m_genStarFlash = FALSE;
	if(!m_contextParent[ CP_PURCHASE_SCIENCE ]->winIsHidden())
		return;
	//switchToContext(CB_CONTEXT_PURCHASE_SCIENCE, NULL);
	m_contextParent[ CP_PURCHASE_SCIENCE ]->winHide(FALSE);
	if (TheGlobalData->m_animateWindows)
		TheTransitionHandler->setGroup("GenExpFade");
		//m_generalsScreenAnimate->registerGameWindow( m_contextParent[ CP_PURCHASE_SCIENCE ], WIN_ANIMATION_SLIDE_TOP, TRUE, 200 );

}

void ControlBar::hidePurchaseScience( void )
{
	if(m_contextParent[ CP_PURCHASE_SCIENCE ]->winIsHidden())
		return;

	if( m_contextParent[ CP_PURCHASE_SCIENCE ] )
	{
		m_contextParent[ CP_PURCHASE_SCIENCE ]->winHide( TRUE );
	}
//	if (!TheGlobalData->m_animateWindows)
//		{
//			if( m_contextParent[ CP_PURCHASE_SCIENCE ] )
//			{
//				m_contextParent[ CP_PURCHASE_SCIENCE ]->winHide( TRUE );
//			}
//		}
//		else
//		{
//			//if (m_generalsScreenAnimate->isFinished())
//			if(TheTransitionHandler->isFinished())
//				TheTransitionHandler->reverse("GenExpFade");
//				//m_generalsScreenAnimate->reverseAnimateWindow();
//		}
}

void ControlBar::togglePurchaseScience( void )
{
	if(m_contextParent[ CP_PURCHASE_SCIENCE ]->winIsHidden())
		showPurchaseScience();
	else
		hidePurchaseScience();
}
