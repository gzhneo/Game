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

#include "Common/Xfer.h"
#include "Common/ActionManager.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/TestAids.h"
#include "Common/GameType.h"
#include "Common/MessageStream.h"
#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Radar.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/BuildAssistant.h"
#include "Common/Recorder.h"
#include "Common/BuildAssistant.h"
#include "Common/SpecialPower.h"
#include "GameClient/Anim2D.h"
#include "GameClient/ControlBar.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/Diplomacy.h"
#include "GameClient/Eva.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Drawable.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameWindowGlobal.h"
#include "GameClient/GameWindowID.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/InGameUI.h"
#include "GameClient/VideoPlayer.h"
#include "GameClient/Mouse.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/View.h"
#include "GameClient/TerrainVisual.h"	
#include "GameClient/ControlBar.h"
#include "GameClient/Display.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/LookAtXlat.h"
#include "GameClient/SelectionXlat.h"
#include "GameClient/Shadow.h"
#include "GameClient/GlobalLanguage.h"
#include "GameLogic/AIGuard.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/Object.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Module/StealthUpdate.h"
#include "GameLogic/Module/SupplyWarehouseDockUpdate.h"
#include "GameLogic/Module/MobMemberSlavedUpdate.h"//ML
#include "Common/StateMachine.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/AIPathfind.h"

// InGameUI: hints at the cursor -- area select, move, attack, mouse-over and command hints

//-------------------------------------------------------------------------------------------------
/** An area selection is occurring, start graphical "hint". */
//-------------------------------------------------------------------------------------------------
void InGameUI::beginAreaSelectHint( const GameMessage *msg )
{
	m_isDragSelecting = true;
	m_dragSelectRegion = msg->getArgument( 0 )->pixelRegion;
}

//-------------------------------------------------------------------------------------------------
/** An area selection has occurred, finish graphical "hint". */
//-------------------------------------------------------------------------------------------------
void InGameUI::endAreaSelectHint( const GameMessage *msg )
{
	m_isDragSelecting = false;
}

//-------------------------------------------------------------------------------------------------
/** A move command has occurred, start graphical "hint". */
//-------------------------------------------------------------------------------------------------
void InGameUI::createMoveHint( const GameMessage *msg )
{
	Int i;

	// first, remove any existing move hint for this source if present
	for( i = 0; i < MAX_MOVE_HINTS; i++ )
		if( m_moveHint[ i ].sourceID == static_cast<UnsignedInt>(msg->getArgument( 0 )->objectID) &&
				m_moveHint[ i ].frame != 0 )
			expireHint( HintType::MOVE_HINT, i );

		
	if( getSelectCount() == 1 )
	{
		Drawable *draw = getFirstSelectedDrawable();
		Object *obj = draw ? draw->getObject() : nullptr;
		if( obj && obj->isKindOf( KINDOF_IMMOBILE ) )
		{
			//Don't allow move hints to be created if our selected object can't move!
			return;
		}
	}

	m_moveHint[ m_nextMoveHint ].frame = TheGameClient->getFrame();
	m_moveHint[ m_nextMoveHint ].pos = msg->getArgument( 0 )->location;
		
	m_nextMoveHint++;

	// wrap around
	if (m_nextMoveHint == InGameUI::MAX_MOVE_HINTS)
		m_nextMoveHint = 0;
}

//-------------------------------------------------------------------------------------------------
/** An attack command has occurred, start graphical "hint". */
//-------------------------------------------------------------------------------------------------
void InGameUI::createAttackHint( const GameMessage *msg )
{

}

//-------------------------------------------------------------------------------------------------
/** A force attack command has occurred, start graphical "hint". */
//-------------------------------------------------------------------------------------------------
void InGameUI::createForceAttackHint( const GameMessage *msg )
{

}

//-------------------------------------------------------------------------------------------------
/** An garrison command has occurred, start graphical "hint". */
//-------------------------------------------------------------------------------------------------
void InGameUI::createGarrisonHint( const GameMessage *msg )
{
	Drawable *draw = TheGameClient->findDrawableByID( msg->getArgument(0)->drawableID );
	if( draw )
	{
		draw->onSelected();
	}
}

//-------------------------------------------------------------------------------------------------
/** Details of what is mouse hovered over right now are in this message.  Terrain might result
	* in just a tooltip.  An object might get a tooltip and show its hit points.
 */
//-------------------------------------------------------------------------------------------------
void InGameUI::createMouseoverHint( const GameMessage *msg )
{
	if (m_isScrolling || m_isSelecting)
		return; // no mouseover for you

	GameWindow *window = nullptr;
	const MouseIO *io = TheMouse->getMouseStatus();
	Bool underWindow = false;
	if (io && TheWindowManager)
		window = TheWindowManager->getWindowUnderCursor(io->pos.x, io->pos.y);

	while (window)
	{
		if (window->winGetInputFunc() == LeftHUDInput) {
			underWindow = false;
			break;
		}
		
		// check to see if it or any of its parents are opaque.  If so, we can't select anything.
		if (!BitTest( window->winGetStatus(), WIN_STATUS_SEE_THRU ))
		{
			underWindow = true;
			break;
		}

		window = window->winGetParent();
	}
	if (underWindow)
	{
		setMouseCursor(Mouse::ARROW); // regardless of m_mouseMode
		return;
	}

  



	DrawableID oldID = m_mousedOverDrawableID;

	if (msg->getType() == GameMessage::MSG_MOUSEOVER_DRAWABLE_HINT)
	{
		TheMouse->setCursorTooltip(UnicodeString::TheEmptyString );
		m_mousedOverDrawableID = INVALID_DRAWABLE_ID;
		const Drawable *draw = TheGameClient->findDrawableByID(msg->getArgument(0)->drawableID);
		const Object *obj = draw ? draw->getObject() : nullptr;
		if( obj )
		{
			
 			//Ahh, here is a wierd exception: if the moused-over drawable is a mob-member
			//(e.g. AngryMob), Lets fool the UI into creating the hint for the NEXUS instead...
 			if (obj->isKindOf( KINDOF_IGNORED_IN_GUI ))
 			{
 				static NameKeyType key_MobMemberSlavedUpdate = NAMEKEY( "MobMemberSlavedUpdate" );
 				MobMemberSlavedUpdate *MMSUpdate = static_cast<MobMemberSlavedUpdate*>(obj->findUpdateModule( key_MobMemberSlavedUpdate ));
 				if( MMSUpdate )
 				{
 					Object *slaver = TheGameLogic->findObjectByID(MMSUpdate->getSlaverID());
 					if ( slaver )
 					{
 						Drawable *slaverDraw = slaver->getDrawable();
 						if ( slaverDraw )
 							m_mousedOverDrawableID = slaverDraw->getID();
 							// if this fails, not to worry... it has already defaulted to INVALID_DRAWABLE_ID, above
 					}
 				}
 			}
 			else
 				m_mousedOverDrawableID = draw->getID();

#if defined(_DEBUG) //Extra hacky, sorry, but I need to use this in constantdebug report
			if ( TheGlobalData->m_constantDebugUpdate == TRUE )
				m_mousedOverDrawableID = draw->getID();
#endif


			const Player* player = nullptr;
			const ThingTemplate *thingTemplate = obj->getTemplate();

			ContainModuleInterface* contain = obj->getContain();
			if( contain )
				player = contain->getApparentControllingPlayer(ThePlayerList->getLocalPlayer());

			if (player == nullptr)
				player = obj->getControllingPlayer();

			Bool disguised = false;
			if( obj->isKindOf( KINDOF_DISGUISER ) )
			{
				//Because we have support for disguised units pretending to be units from another
				//team, we need to intercept it here and make sure it's rendered appropriately
				//based on which client is rendering it.
        StealthUpdate *update = obj->getStealth();
				if( update )
				{
					if( update->isDisguised() )
					{
						Player *clientPlayer = ThePlayerList->getLocalPlayer();
						Player *disguisedPlayer = ThePlayerList->getNthPlayer( update->getDisguisedPlayerIndex() );
						if( player->getRelationship( clientPlayer->getDefaultTeam() ) != ALLIES && clientPlayer->isPlayerActive() )
						{
							//Neutrals and enemies will see this disguised unit as the team it's disguised as.
							player = disguisedPlayer;
							const ThingTemplate *disguisedTemplate = update->getDisguisedTemplate();
							if( disguisedTemplate )
							{
								thingTemplate = disguisedTemplate;
								disguised = true;
							}
						}
						//Otherwise, the color will show up as the team it really belongs to (already set above).
					}
				}
			}


			UnicodeString str = thingTemplate->getDisplayName();
			UnicodeString displayName = thingTemplate->getDisplayName();
			if( str.isEmpty() )
			{
				AsciiString txtTemp;
				txtTemp.format("ThingTemplate:%s", obj->getTemplate()->getName().str());
				str = TheGameText->fetch(txtTemp);
				//str.format(L"ThingTemplate:'%hs'", obj->getTemplate()->getName().str());
			}

#ifdef AI_DEBUG_TOOLTIPS
			if (TheGlobalData->m_debugAI) {
				const Team *team = obj->getTeam();
				AsciiString objName = obj->getName();
				AsciiString teamName;
				AsciiString stateName;
				
				AIUpdateInterface *ai = const_cast<AIUpdateInterface*>(obj->getAI());
				if (ai) {
					if (ai->getPath()) {
						TheAI->pathfinder()->setDebugPath(ai->getPath());
					}
#ifdef STATE_MACHINE_DEBUG	
					stateName = ai->getCurrentStateName();
					if (ai->getAttackInfo()) {
						stateName.concat(" AttackPriority=");
						stateName.concat(ai->getAttackInfo()->getName());
					}
#endif
				}
				if( team )
				{
					teamName = team->getName();
				}
				if (!objName.isEmpty())
				{
					if (!teamName.isEmpty())
					{
						str.format(L"%hs(%hs): %s", teamName.str(), objName.str(), str.str());
					}
					else
					{
						str.format(L"%hs: %s", objName.str(), str.str());
					}
				}
				else
				{
					if (!teamName.isEmpty())
					{
						str.format(L"%hs: %s", teamName.str(), str.str());
					}
				}
				str.format(L"%s - %hs", str.str(), stateName.str());

			}
#endif
			UnicodeString warehouseFeedback;
			// Add on dollar amount of warehouse contents so people don't freak out until the art is hooked up
			static const NameKeyType warehouseModuleKey = TheNameKeyGenerator->nameToKey( "SupplyWarehouseDockUpdate" );
			SupplyWarehouseDockUpdate *warehouseModule = static_cast<SupplyWarehouseDockUpdate *>(obj->findUpdateModule( warehouseModuleKey ));
			if( warehouseModule != nullptr )
			{
				Int boxes = warehouseModule->getBoxesStored();
				Int value = boxes * TheGlobalData->m_baseValuePerSupplyBox;
				warehouseFeedback.format(TheGameText->fetch("TOOLTIP:SupplyWarehouse"), value);
				str.concat(warehouseFeedback);
			}

      if (player)
			{
				UnicodeString tooltip;
				//if (TheRecorder->isMultiplayer() && player->getPlayerType() == PLAYER_HUMAN)
				if (TheRecorder->isMultiplayer() && player->isPlayableSide())
					tooltip.format(L"%s\n%s", str.str(), (const_cast<Player *>(player))->getPlayerDisplayName().str());
				else
					tooltip = str;

				Int localPlayerIndex = ThePlayerList ? ThePlayerList->getLocalPlayer()->getPlayerIndex() : 0;

				Int x, y;
				ThePartitionManager->worldToCell(obj->getPosition()->x, obj->getPosition()->y, &x, &y);
				if( ThePartitionManager->getShroudStatusForPlayer(localPlayerIndex, x, y) == CellShroudStatus::CELLSHROUD_CLEAR )
				{
					RGBColor rgb;
					if( disguised )
					{
						rgb.setFromInt( player->getPlayerColor() );
					}
					else
					{
						rgb.setFromInt(draw->getObject()->getIndicatorColor());

						// Unless this is a stealth garrisoned building, 
						// Let's not use the contained's housecolor
						const Object *obj2 = draw->getObject();
						if ( obj2 )
						{
							ContainModuleInterface *contain2 = obj2->getContain();
							if ( contain2 && contain2->isGarrisonable() )
							{
								const Player *play = contain2->getApparentControllingPlayer( ThePlayerList->getLocalPlayer() );
								if ( play )
									rgb.setFromInt( play->getPlayerColor() );
							}
						}

					}

					//Object:Prop is a blank string... but we don't want to show
					//any popup box at all if that is the case!
					if( displayName.compare( TheGameText->fetch( "OBJECT:Prop" ) ) )
					{
	  				TheMouse->setCursorTooltip(tooltip, -1, &rgb );
					}
				}
			}
		}

	}
	else
	{
		m_mousedOverDrawableID = INVALID_DRAWABLE_ID;
	}

	if (oldID != m_mousedOverDrawableID)
	{
		//DEBUG_LOG(("Resetting tooltip delay\n"));
		TheMouse->resetTooltipDelay();
	}

	if (m_mouseMode == MouseMode::MOUSEMODE_DEFAULT && !m_isScrolling && !m_isSelecting && !TheInGameUI->getSelectCount() && (TheRecorder->getMode() != RecorderModeType::RECORDERMODETYPE_PLAYBACK || TheLookAtTranslator->hasMouseMovedRecently()))
	{
		if( m_mousedOverDrawableID != INVALID_DRAWABLE_ID )
		{
			Drawable *draw = TheGameClient->findDrawableByID(m_mousedOverDrawableID);
			
			//Add basic logic to determine if we can select a unit (or hint)
			const Object *obj = draw ? draw->getObject() : nullptr;
			Bool drawSelectable = CanSelectDrawable(draw, FALSE);
			if( !obj )
			{
				drawSelectable = false;
			}

			if( drawSelectable && obj->isLocallyControlled() )
			{
				setMouseCursor(Mouse::SELECTING);
			}
			else
			{
				setMouseCursor(Mouse::ARROW);
			}
		}
		else
		{
			setMouseCursor(Mouse::ARROW);
		}
	}
	else if (m_mouseMode != MouseMode::MOUSEMODE_DEFAULT && m_mouseMode != MouseMode::MOUSEMODE_BUILD_PLACE )
	{
		setMouseCursor(static_cast<Mouse::MouseCursor>(m_mouseModeCursor));
	}
}

//-------------------------------------------------------------------------------------------------
/** A command would be given if a click were to happen, so give a preview hint of what it would be.
	* Changing the mouse cursor is an example
	*/
void InGameUI::createCommandHint( const GameMessage *msg )
{
	if (m_isScrolling || m_isSelecting || TheRecorder->getMode() == RecorderModeType::RECORDERMODETYPE_PLAYBACK)
		return;

	const Drawable *draw = TheGameClient->findDrawableByID(m_mousedOverDrawableID);
	GameMessage::Type t = msg->getType();
//#ifdef DO_SHROUD_PROJECTION
	if( draw && (t == GameMessage::MSG_DO_ATTACK_OBJECT_HINT || t == GameMessage::MSG_DO_ATTACK_OBJECT_AFTER_MOVING_HINT) )
	{
		const Object* obj = draw->getObject();
		Int localPlayerIndex = ThePlayerList ? ThePlayerList->getLocalPlayer()->getPlayerIndex() : 0;
#if defined(_DEBUG)
		ObjectShroudStatus ss = (!obj || !TheGlobalData->m_shroudOn) ? OBJECTSHROUD_CLEAR : obj->getShroudedStatus(localPlayerIndex);
#else
		ObjectShroudStatus ss = (!obj) ? OBJECTSHROUD_CLEAR : obj->getShroudedStatus(localPlayerIndex);
#endif
		if (ss == OBJECTSHROUD_SHROUDED)
		{
			t = GameMessage::MSG_DO_MOVETO_HINT;	// if the object is hidden, switch to something innocuous
		}
	}
//#endif


	setRadiusCursorNone();
  if ( TheGlobalData->m_doubleClickAttackMove )
  {
    if ( --m_duringDoubleClickAttackMoveGuardHintTimer > 0 )
    {
      setMouseCursor(Mouse::FORCE_ATTACK_GROUND);
		  setRadiusCursor(RADIUSCURSOR_GUARD_AREA, 
										  nullptr,
										  PRIMARY_WEAPON);
      return;
    }
  }





	// set cursor to normal if there is a window under the cursor
	GameWindow *window = nullptr;
	const MouseIO *io = TheMouse->getMouseStatus();
	Bool underWindow = false;
	if (io && TheWindowManager)
		window = TheWindowManager->getWindowUnderCursor(io->pos.x, io->pos.y);


	while (window)
	{
		if (window->winGetInputFunc() == LeftHUDInput) {
			underWindow = false;
			break;
		}
		
		// check to see if it or any of its parents are opaque.  If so, we can't select anything.
		if (!BitTest( window->winGetStatus(), WIN_STATUS_SEE_THRU ))
		{
			underWindow = true;
			break;
		}

		window = window->winGetParent();
	}

	//Add basic logic to determine if we can select a unit (or hint)
	const Object *obj = draw ? draw->getObject() : nullptr;
	Bool drawSelectable = CanSelectDrawable(draw, FALSE);
	if( !obj )
	{
		drawSelectable = false;
	}

	// Note: These are only non-NULL if there is exactly one thing selected.
	const Drawable *srcDraw = nullptr;
	const Object *srcObj = nullptr;
	if (getSelectCount() == 1) {
		srcDraw = getAllSelectedDrawables()->front();
		srcObj = (srcDraw ? srcDraw->getObject() : nullptr);
	}

	switch (m_mouseMode)
	{
		case MouseMode::MOUSEMODE_DEFAULT:
			{ 
				// This section of code only gets called when there is no specific cursor mode happening.
				if (underWindow || (srcObj && !srcObj->isLocallyControlled()))
				{
					setMouseCursor(Mouse::ARROW);
					return;
				}
				switch (t)
				{
					case GameMessage::MSG_DO_MOVETO_HINT:
					{
						if( !drawSelectable && srcObj && srcObj->isLocallyControlled() && srcObj->isKindOf(KINDOF_STRUCTURE))
							setMouseCursor( Mouse::GENERIC_INVALID );
						else if( drawSelectable && obj->isLocallyControlled() && !obj->isKindOf(KINDOF_MINE))
							setMouseCursor( Mouse::SELECTING );
						else if( TheRadar->isRadarWindow( window ) &&
										 TheRadar->isRadarForced() == FALSE &&
										 (TheRadar->isRadarHidden() || 
										 ThePlayerList->getLocalPlayer()->hasRadar() == FALSE) )
							setMouseCursor( Mouse::ARROW );
						else 
							setMouseCursor( Mouse::MOVETO );
						break;
					}
					case GameMessage::MSG_DO_ATTACKMOVETO_HINT:
						if( drawSelectable && obj->isLocallyControlled()  )
							setMouseCursor( Mouse::SELECTING );
						else
							setMouseCursor( Mouse::ATTACKMOVETO );
						break;
					case GameMessage::MSG_ADD_WAYPOINT_HINT:
						setMouseCursor( Mouse::WAYPOINT );
						break;
					case GameMessage::MSG_DO_ATTACK_OBJECT_HINT:
						setMouseCursor( Mouse::ATTACK_OBJECT );
						break;
					case GameMessage::MSG_DO_ATTACK_OBJECT_AFTER_MOVING_HINT:
						setMouseCursor( Mouse::OUTRANGE );
						break;
					case GameMessage::MSG_DO_FORCE_ATTACK_OBJECT_HINT:
						setMouseCursor( Mouse::FORCE_ATTACK_OBJECT );
						break;
					case GameMessage::MSG_DO_FORCE_ATTACK_GROUND_HINT:
						setMouseCursor( Mouse::FORCE_ATTACK_GROUND );
						break;
					case GameMessage::MSG_GET_REPAIRED_HINT:
						setMouseCursor( Mouse::GET_REPAIRED );
						break;
					case GameMessage::MSG_DOCK_HINT:
						setMouseCursor( Mouse::DOCK );
						break;
					case GameMessage::MSG_GET_HEALED_HINT:
						setMouseCursor( Mouse::GET_HEALED );
						break;
					case GameMessage::MSG_DO_REPAIR_HINT:
						setMouseCursor( Mouse::DO_REPAIR );
						break;
					case GameMessage::MSG_RESUME_CONSTRUCTION_HINT:
						setMouseCursor( Mouse::RESUME_CONSTRUCTION );
						break;				
					case GameMessage::MSG_ENTER_HINT:
						setMouseCursor( Mouse::ENTER_FRIENDLY );
						break;
					case GameMessage::MSG_CONVERT_TO_CARBOMB_HINT:
					case GameMessage::MSG_HIJACK_HINT:
					case GameMessage::MSG_SABOTAGE_HINT:
						setMouseCursor( Mouse::ENTER_AGGRESSIVELY );
						break;
					case GameMessage::MSG_DEFECTOR_HINT:
						setMouseCursor( Mouse::DEFECTOR );
						break;
					case GameMessage::MSG_CAPTUREBUILDING_HINT:
						setMouseCursor( Mouse::CAPTUREBUILDING );
						break;
					case GameMessage::MSG_HACK_HINT:
						setMouseCursor( Mouse::HACK );
						break;
					case GameMessage::MSG_IMPOSSIBLE_ATTACK_HINT:
						setMouseCursor( Mouse::GENERIC_INVALID );
						break;
					case GameMessage::MSG_SET_RALLY_POINT_HINT:
						if ( !drawSelectable )
							setMouseCursor( Mouse::SET_RALLY_POINT );
						else
							setMouseCursor( Mouse::SELECTING );
						break;
					case GameMessage::MSG_DO_SPECIAL_POWER_OVERRIDE_DESTINATION_HINT:
						setMouseCursor( Mouse::PARTICLE_UPLINK_CANNON );
						break;
					case GameMessage::MSG_DO_SALVAGE_HINT:
						setMouseCursor( Mouse::MOVETO );
						break;
					case GameMessage::MSG_DO_INVALID_HINT:
						setMouseCursor( Mouse::GENERIC_INVALID );
						break;
				}
			}
			break;
		case MouseMode::MOUSEMODE_BUILD_PLACE:
			{
				if (underWindow)
				{
					setMouseCursor(Mouse::ARROW);
					return;
				}
				switch (t)
				{
					case GameMessage::MSG_DO_MOVETO_HINT:
					case GameMessage::MSG_DO_ATTACKMOVETO_HINT:
					case GameMessage::MSG_ADD_WAYPOINT:
						setMouseCursor(Mouse::BUILD_PLACEMENT);
						break;
					case GameMessage::MSG_DO_ATTACK_OBJECT_HINT:
					case GameMessage::MSG_DO_ATTACK_OBJECT_AFTER_MOVING_HINT:
						setMouseCursor(Mouse::INVALID_BUILD_PLACEMENT);
						break;
				}
			}
			break;
		case MouseMode::MOUSEMODE_GUI_COMMAND:
			{
				if (underWindow)
				{
					setMouseCursor(Mouse::ARROW);
					return;
				}
				// set the mouse cursor for commands that need a targeting or to normal with no command
				if( m_pendingGUICommand )
				{
					if( m_pendingGUICommand->isContextCommand() || 
							m_pendingGUICommand->getCommandType() == GUI_COMMAND_SPECIAL_POWER ||
							m_pendingGUICommand->getCommandType() == GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT )
					{
						//Here is the hook for when we are in a context sensitive command mode. We can
						//either do the specified command mode command or nothing! Whether or not the 
						//command is valid or not was determined in evaluateContextCommand which is
						//called first, and posts the appropriate message.
						AsciiString cursorName;	// empty by default
						switch( t )
						{
							case GameMessage::MSG_VALID_GUICOMMAND_HINT:
								cursorName = m_pendingGUICommand->getCursorName();
								break;
							case GameMessage::MSG_INVALID_GUICOMMAND_HINT:
							default:
								cursorName = m_pendingGUICommand->getInvalidCursorName();
								break;
						}

						Int index = TheMouse->getCursorIndex(cursorName);
						if( index != Mouse::INVALID_MOUSE_CURSOR )
						{
							setMouseCursor( static_cast<Mouse::MouseCursor>(index) );
						}
						else
						{
							setMouseCursor( Mouse::CROSS );
						}
						setRadiusCursor(m_pendingGUICommand->getRadiusCursorType(), //*****************************************************************
														m_pendingGUICommand->getSpecialPowerTemplate(),
														m_pendingGUICommand->getWeaponSlot());
					}
					else if( BitTest( m_pendingGUICommand->getOptions(), COMMAND_OPTION_NEED_TARGET ) )
					{
						Int index = TheMouse->getCursorIndex(m_pendingGUICommand->getCursorName());
						if (index != Mouse::INVALID_MOUSE_CURSOR)
							setMouseCursor( static_cast<Mouse::MouseCursor>(index) );
						else
							setMouseCursor( Mouse::CROSS );
						setRadiusCursor(m_pendingGUICommand->getRadiusCursorType(), //*****************************************************************
														m_pendingGUICommand->getSpecialPowerTemplate(),
														m_pendingGUICommand->getWeaponSlot());
					}
					else
					{
						setRadiusCursorNone();
					}
				}
			}
			break;
	}
}

//-------------------------------------------------------------------------------------------------
/** Expire a hint of the specified type with the corresponding hint index */
//-------------------------------------------------------------------------------------------------
void InGameUI::expireHint( HintType type, UnsignedInt hintIndex )
{

	if( type == HintType::MOVE_HINT )
	{

		// sanity
		if( hintIndex < 0 || hintIndex >= MAX_MOVE_HINTS )
			return;

		m_moveHint[ hintIndex ].sourceID = 0;
		m_moveHint[ hintIndex ].frame = 0;

	}  // end if
	else
	{

		// undefined hint type
		DEBUG_CRASH(("undefined hint type"));
		return;

	}  // end else

}  // end expireHint
