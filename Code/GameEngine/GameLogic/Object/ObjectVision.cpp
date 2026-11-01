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

#include "Common/LogicToClient.h"
#include "Common/LogicAudio.h"
#include "Common/BitFlagsIO.h"
#include "Common/BuildAssistant.h"
#include "Common/Dict.h"
#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/ModuleFactory.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Radar.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "Common/WellKnownKeys.h"
#include "Common/Xfer.h"
#include "Common/XferCRC.h"
#include "Common/PerfTimer.h"
#include "GameClient/Anim2D.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"
#include "GameClient/Eva.h"
#include "GameClient/GameClient.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/AIStats.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/FiringTracker.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/AutoHealBehavior.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/CollideModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/CountermeasuresBehavior.h"
#include "GameLogic/Module/CreateModule.h"
#include "GameLogic/Module/DamageModule.h"
#include "GameLogic/Module/DeletionUpdate.h"
#include "GameLogic/Module/DestroyModule.h"
#include "GameLogic/Module/DieModule.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/ObjectDefectionHelper.h"
#include "GameLogic/Module/ObjectRepulsorHelper.h"
#include "GameLogic/Module/ObjectSMCHelper.h"
#include "GameLogic/Module/ObjectWeaponStatusHelper.h"
#include "GameLogic/Module/OverchargeBehavior.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Module/PowerPlantUpgrade.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/RadarUpgrade.h"
#include "GameLogic/Module/RebuildHoleBehavior.h"
#include "GameLogic/Module/SpawnBehavior.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Module/SpecialAbilityUpdate.h"
#include "GameLogic/Module/StatusDamageHelper.h"
#include "GameLogic/Module/StickyBombUpdate.h"
#include "GameLogic/Module/SubdualDamageHelper.h"
#include "GameLogic/Module/TempWeaponBonusHelper.h"
#include "GameLogic/Module/ToppleUpdate.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/Module/UpgradeModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponSet.h"
#include "GameLogic/Module/RadarUpdate.h"
#include "GameLogic/Module/PowerPlantUpdate.h"
#include "Common/CRCDebug.h"
#include "Common/MiscAudio.h"
#include "Common/AudioEventInfo.h"
#include "Common/DynamicAudioEventInfo.h"

//-------------------------------------------------------------------------------------------------
extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);

// Vision, shroud and the value and threat maps

//-------------------------------------------------------------------------------------------------
ObjectShroudStatus Object::getShroudedStatus(Int playerIndex) const 
{
	if (getTemplate()->isKindOf( KINDOF_ALWAYS_VISIBLE ))
		return OBJECTSHROUD_CLEAR;

	if (m_partitionData)
		return m_partitionData->getShroudedStatus(playerIndex); 

	// This can happen for objects removed from the partition system (e.g.,
	// for soldiers that are garrisoned inside a building). 
	return OBJECTSHROUD_CLEAR;
}

//-------------------------------------------------------------------------------------------------
/// We have moved a 'significant' amount, so do maintenence that can be considered 'cell-based'
void Object::onPartitionCellChange()
{
	handlePartitionCellMaintenance();
}

//-------------------------------------------------------------------------------------------------
void Object::handlePartitionCellMaintenance()
{
	handleShroud();
	handleValueMap();
	handleThreatMap();
}

//-------------------------------------------------------------------------------------------------
void Object::handleShroud()
{
	// Undo last looking
	unlook();
	// and shrouding
	unshroud();

	// redo shrouding
	shroud();
	// Redo looking
	look();
}

//-------------------------------------------------------------------------------------------------
void Object::handleValueMap()
{
	removeValue();
	addValue();
}

//-------------------------------------------------------------------------------------------------
void Object::handleThreatMap()
{
	removeThreat();
	addThreat();
}

//-------------------------------------------------------------------------------------------------
void Object::addValue()
{
	if( !m_partitionLastValue->isInvalid() )
	{
		DEBUG_CRASH( ("An Object is adding value, but hasn't removed his previous value.") );
		return;
	}

	if (!getControllingPlayer()) 
		return;

	if( getStatusBits().test( OBJECT_STATUS_UNDER_CONSTRUCTION ) || isEffectivelyDead() || getShroudClearingRange() <= 0.0f )
		return;


	m_partitionLastValue->m_where = *getPosition();
	m_partitionLastValue->m_data = getTemplate()->friend_getBuildCost();

	m_partitionLastValue->m_forWhom = getControllingPlayer()->getPlayerMask();
	m_partitionLastValue->m_howFar = getVisionRange();	// we are valuable all the way to where we can target.

	ThePartitionManager->doValueAffect(m_partitionLastValue->m_where.x,
																		 m_partitionLastValue->m_where.y,
																		 m_partitionLastValue->m_howFar,
																		 m_partitionLastValue->m_data,
																		 m_partitionLastValue->m_forWhom
																		 );
}

//-------------------------------------------------------------------------------------------------
void Object::removeValue()
{
	if( m_partitionLastValue->isInvalid() )
	{
		// removing before adding is valid, cause we always remove before adding. (So the first remove
		// will occur before the first add)
		return;
	}

	ThePartitionManager->undoValueAffect(m_partitionLastValue->m_where.x, 
																			 m_partitionLastValue->m_where.y, 
																			 m_partitionLastValue->m_howFar,
																			 m_partitionLastValue->m_data,
																			 m_partitionLastValue->m_forWhom
																			);

	m_partitionLastValue->reset();
}

//-------------------------------------------------------------------------------------------------
void Object::addThreat()
{
	if( !m_partitionLastThreat->isInvalid() )
	{
		DEBUG_CRASH( ("An Object is adding threat, but hasn't removed his previous threat. (He hasn't finished the threat?)") );
		return;
	}
	
	if (!getControllingPlayer()) 
		return;

	if( getStatusBits().test( OBJECT_STATUS_UNDER_CONSTRUCTION ) || isEffectivelyDead() || getShroudClearingRange() <= 0.0f )
		return;


	m_partitionLastThreat->m_where = *getPosition();
	m_partitionLastThreat->m_data = getTemplate()->getThreatValue();

	m_partitionLastThreat->m_forWhom = getControllingPlayer()->getPlayerMask();
	m_partitionLastThreat->m_howFar = getVisionRange();	// we are threatening all the way to where we can target.

	ThePartitionManager->doThreatAffect(m_partitionLastThreat->m_where.x,
																			m_partitionLastThreat->m_where.y,
																			m_partitionLastThreat->m_howFar,
																			m_partitionLastThreat->m_data,
																			m_partitionLastThreat->m_forWhom
																		 );
}

//-------------------------------------------------------------------------------------------------
void Object::removeThreat()
{
	if( m_partitionLastThreat->isInvalid() )
	{
		// removing before adding is valid, cause we always remove before adding. (So the first remove
		// will occur before the first add)
		return;
	}

	ThePartitionManager->undoThreatAffect(m_partitionLastThreat->m_where.x, 
																			  m_partitionLastThreat->m_where.y, 
																			  m_partitionLastThreat->m_howFar,
																				m_partitionLastThreat->m_data,
																			  m_partitionLastThreat->m_forWhom
																			 );

	m_partitionLastThreat->reset();
}

//-------------------------------------------------------------------------------------------------
void Object::look()
{
	if( ! m_partitionLastLook->isInvalid() )
	{
		DEBUG_CRASH( ("An Object is looking, but hasn't unlooked the last one.") );
		return;
	}

	Player* controller = getControllingPlayer();
	if ( controller )
	{
		// I removed the check for objects under construction by request of designers since
		// they want constructing objects to have a reduced sight range now. -MW
		// dead or blind things don't reveal shroud
		


		// Some things get Destroyed directly without hitting Death.
		if( !isDestroyed() && !isEffectivelyDead() )
		{

      ContainModuleInterface * contain = (getContainedBy() ? getContainedBy()->getContain() : nullptr);
      if ( contain && !contain->isGarrisonable() )
          return;// dont look, 'cause you are in a tunnel, now
			// GS 10-20 Need to expand that exception to all transports or else you get a perma reveal where
			// you entered the transport.  Remember, this hackiness is caused by the fact that we never realized that 
			// garrisoned buildings weren't looking, we were just seeing the leftover last look of the guy inside.
			// Otherwise we'd just have enclosingContainer control looking which is the 'correct' answer.

			Real shroudClearingRange = getShroudClearingRange();
			if( shroudClearingRange > 0.0f )
			{
				PlayerMaskType lookingMask = 0;

				if ( isKindOf(KINDOF_REVEAL_TO_ALL) )
				{
					lookingMask = PLAYERMASK_ALL;
				}
				else
				{
					for( Int currentIndex = ThePlayerList->getPlayerCount() - 1; currentIndex >=0; currentIndex-- )
					{
						const Player *currentPlayer = ThePlayerList->getNthPlayer( currentIndex );
						
						// Build mask of of allies who can see me. 
						// This is the Object-centric game level that cares
						if( getControllingPlayer()->getRelationship( currentPlayer->peekDefaultTeam() ) == ALLIES )
						{
							lookingMask |= currentPlayer->getPlayerMask();
						}
					}
					
					// Other players can also be looking through our eyes.
					lookingMask |= m_visionSpiedMask;
				}

				Coord3D pos = *getPosition();
				ThePartitionManager->doShroudReveal( pos.x, pos.y, shroudClearingRange, lookingMask );

				m_partitionLastLook->m_where = pos;
				m_partitionLastLook->m_forWhom = lookingMask;
				m_partitionLastLook->m_howFar = getShroudClearingRange();

	//			DEBUG_LOG(( "A %s looks at %f, %f for %x at range %f\n",
	//									getTemplate()->getName().str(),
	//									pos.x,
	//									pos.y,
	//									lookingMask,
	//									getShroudClearingRange()
	//									));
			}

			//Now reveal to everyone if we're special. Note this works differently than KINDOF_REVEAL_TO_ALL because
			//the kindof uses the same range as allies, spies, and owners would see. This template based shroud
			//reveal to all range can specify a different value so we can get a much smaller reveal distance.
			// And don't reveal while under construction.  When finished, a refresh occurs, so don't worry.
			Real shroudRevealToAllRange = getTemplate()->getShroudRevealToAllRange();
			if( shroudRevealToAllRange > 0.0f && !testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
			{
				//Kris: August 20, 2003
				//Seeing I added this concept, I'm changing it now to only reveal to all when the unit is visible. If it's stealthed,
				//we won't reveal it anymore (stealth general scudstorm).
				Bool stealthedAndNotDetected = testStatus( OBJECT_STATUS_STEALTHED ) && !testStatus( OBJECT_STATUS_DETECTED ) && !testStatus( OBJECT_STATUS_DISGUISED );
				if( !stealthedAndNotDetected )
				{
					Coord3D pos = *getPosition();
					PlayerMaskType thePlayersMask = ThePlayerList->getPlayersWithRelationship( getControllingPlayer()->getPlayerIndex(), ALLOW_ENEMIES | ALLOW_NEUTRAL );
					ThePartitionManager->doShroudReveal( pos.x, pos.y, shroudRevealToAllRange, thePlayersMask );
					m_partitionRevealAllLastLook->m_where = pos;
					m_partitionRevealAllLastLook->m_forWhom = thePlayersMask;
					m_partitionRevealAllLastLook->m_howFar = shroudRevealToAllRange;
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void Object::unlook()
{
	if( m_partitionLastLook->isInvalid() )
	{
		// Your very first action will be an unlook, so of course you haven't looked yet.  This is not an error
		// This early return prevents an extra unlook if you never looked.  Like you have 0 vision.
		return;
	}

	ThePartitionManager->queueUndoShroudReveal(m_partitionLastLook->m_where.x, 
																				m_partitionLastLook->m_where.y, 
																				m_partitionLastLook->m_howFar, 
																				m_partitionLastLook->m_forWhom
																				);

//			DEBUG_LOG(( "A %s queues an unlook at %f, %f for %x at range %f\n",
//									getTemplate()->getName().str(),
//									m_partitionLastLook.m_where.x,
//									m_partitionLastLook.m_where.y,
//									m_partitionLastLook.m_forWhom,
//									m_partitionLastLook.m_howFar
//									));

	m_partitionLastLook->reset();

	if( !m_partitionRevealAllLastLook->isInvalid() )
	{
		ThePartitionManager->queueUndoShroudReveal(m_partitionRevealAllLastLook->m_where.x, 
																				m_partitionRevealAllLastLook->m_where.y, 
																				m_partitionRevealAllLastLook->m_howFar, 
																				m_partitionRevealAllLastLook->m_forWhom
																				);
		
		m_partitionRevealAllLastLook->reset();
	}
}

//-------------------------------------------------------------------------------------------------
void Object::shroud()
{
	if( ! m_partitionLastShroud->isInvalid() )
	{
		DEBUG_CRASH( ("An Object is shrouding, but hasn't unshrouded the last one.") );
		return;
	}

	Player* controller = getControllingPlayer();
	if ( controller )
	{
		// things under construction don't  shroud. (srj), nor do dead or blind things
		if( !getStatusBits().test( OBJECT_STATUS_UNDER_CONSTRUCTION ) && !isEffectivelyDead()	&& getShroudRange() > 0.0f )
		{
			PlayerMaskType shroudingMask = 0;
			for( Int currentIndex = ThePlayerList->getPlayerCount() - 1; currentIndex >=0; currentIndex-- )
			{
				const Player *currentPlayer = ThePlayerList->getNthPlayer( currentIndex );
				//Build mask of NON-allies.  This is the Object-centric game level that cares
				if( getControllingPlayer()->getRelationship( currentPlayer->peekDefaultTeam() ) != ALLIES )
				{
					shroudingMask |= currentPlayer->getPlayerMask();
				}
			}

			Coord3D pos = *getPosition();
			ThePartitionManager->doShroudCover(pos.x, pos.y, 
				getShroudRange(), 
				shroudingMask);

			m_partitionLastShroud->m_where = pos;
			m_partitionLastShroud->m_forWhom = shroudingMask;
			m_partitionLastShroud->m_howFar = getShroudRange();
		}
	}
}

//-------------------------------------------------------------------------------------------------
void Object::unshroud()
{
	if( m_partitionLastShroud->isInvalid() )
	{
		// Your very first action will be an unlook, so of course you haven't looked yet.  This is not an error
		// This early return prevents an extra unlook if you never looked.  Like you have 0 shroud generation.
		return;
	}

	ThePartitionManager->undoShroudCover(m_partitionLastShroud->m_where.x, 
		m_partitionLastShroud->m_where.y, 
		m_partitionLastShroud->m_howFar, 
		m_partitionLastShroud->m_forWhom);

	m_partitionLastShroud->reset();
}

//-------------------------------------------------------------------------------------------------
Real Object::getVisionRange() const
{
#if defined(_DEBUG)
	if (TheGlobalData->m_debugVisibility) 
	{
		Vector3 pos(m_visionRange, 0, 0);
		for (int i = 0; i < TheGlobalData->m_debugVisibilityTileCount; ++i) 
		{
			pos.Rotate_Z(1.0f * i / TheGlobalData->m_debugVisibilityTileCount * 2 * PI);
			Coord3D coord = { pos.X + getPosition()->x, pos.Y + getPosition()->y, pos.Z + getPosition()->z };

			addIcon(&coord, TheGlobalData->m_debugVisibilityTileWidth, 
											TheGlobalData->m_debugVisibilityTileDuration, 
											TheGlobalData->m_debugVisibilityTargettableColor);
		}
	}
#endif
	return m_visionRange;
}

//-------------------------------------------------------------------------------------------------
void Object::setVisionRange( Real newVisionRange )
{
	m_visionRange = newVisionRange;
}

//-------------------------------------------------------------------------------------------------
Real Object::getShroudClearingRange() const
{
	Real shroudClearingRange=m_shroudClearingRange;

	if( getStatusBits().test( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
	{	
		//structures under construction have limited vision range.  For now, base it
		//on the geometry extents so the structure can only see itself.
		shroudClearingRange = getGeometryInfo().getBoundingCircleRadius();
	}

#if defined(_DEBUG)
	if (TheGlobalData->m_debugVisibility) 
	{
		Vector3 pos(shroudClearingRange, 0, 0);
		for (int i = 0; i < TheGlobalData->m_debugVisibilityTileCount; ++i) 
		{
			pos.Rotate_Z(1.0f * i / TheGlobalData->m_debugVisibilityTileCount * 2 * PI);
			Coord3D coord = { pos.X + getPosition()->x, pos.Y + getPosition()->y, pos.Z + getPosition()->z };

			addIcon(&coord, TheGlobalData->m_debugVisibilityTileWidth, 
											TheGlobalData->m_debugVisibilityTileDuration, 
											TheGlobalData->m_debugVisibilityDeshroudColor);
		}
	}
#endif

	return shroudClearingRange;
}

//-------------------------------------------------------------------------------------------------
void Object::setShroudClearingRange( Real newShroudClearingRange )
{
 	if( newShroudClearingRange != m_shroudClearingRange )
 	{
 		// The partition cell refresh is a slow operation, so only do it if you really have to.
 		// Range change is a valid reason to relook.
 		m_shroudClearingRange = newShroudClearingRange;

		/*
			Complete and total monkey hack fix. 

			The problem: newObject doesn't get an initial pos, so all objects start at 0,0,0.
			Most code paths instantly move 'em to a good pos, but in some cases, that is too late:
			If we have search-and-destroy battle plan, we will apply it at that point, and clear out
			a vision range based on our current (wrong) location. Doh!

			So, this just sez: if you are at 0,0,0, don't call handlePartitionCellMaintenance()... since
			you will either (1) be moved elsewhere immediately, thus forcing it to be called via
			another route anyway, or (2) not be moved, which means you are a very naughty and worthless
			object anyway and we should just ignore you.

			Proper fix for next version is to require initial pos to be passed in to newObject so that
			all objects can start at their proper initial position from the start of the ctor. 

			(srj)
		*/
		const Coord3D* pos = getPosition();
		if (pos->x || pos->y || pos->z)
		{
	 		handlePartitionCellMaintenance();
		}
 	}
}

//-------------------------------------------------------------------------------------------------
Real Object::getShroudRange() const
{
#if defined(_DEBUG)
	if (TheGlobalData->m_debugVisibility) 
	{
		Vector3 pos(m_shroudRange, 0, 0);
		for (int i = 0; i < TheGlobalData->m_debugVisibilityTileCount; ++i) 
		{
			pos.Rotate_Z(1.0f * i / TheGlobalData->m_debugVisibilityTileCount * 2 * PI);
			Coord3D coord = { pos.X + getPosition()->x, pos.Y + getPosition()->y, pos.Z + getPosition()->z };

			addIcon(&coord, TheGlobalData->m_debugVisibilityTileWidth, 
											TheGlobalData->m_debugVisibilityTileDuration, 
											TheGlobalData->m_debugVisibilityGapColor);
		}
	}
#endif

	return m_shroudRange;
}

//-------------------------------------------------------------------------------------------------
void Object::setShroudRange( Real newShroudRange )
{
	m_shroudRange = newShroudRange;
}

//-------------------------------------------------------------------------------------------------
void Object::setVisionSpied(Bool setting, Int byWhom)
{
	Bool needRefresh = FALSE; // If this setting is an edge trigger on the reference count, I need to refresh

	if( setting )
	{
		m_visionSpiedBy[ byWhom ] = m_visionSpiedBy[ byWhom ] + 1;
		if( m_visionSpiedBy[ byWhom ] == 1 )
			needRefresh = TRUE;
	}
	else
	{
		m_visionSpiedBy[ byWhom ] = m_visionSpiedBy[ byWhom ] - 1;
		if( m_visionSpiedBy[ byWhom ] == 0 )
			needRefresh = TRUE;
	}

	if( needRefresh )
	{
		PlayerMaskType workingMask = 0;
		for (Int i = 0; i < MAX_PLAYER_COUNT; ++i) 
		{
			if( m_visionSpiedBy[i] > 0 )
				BitSet( workingMask, ( 1 << i ) );
			else
				BitClear( workingMask, ( 1 << i ) );
		}

		m_visionSpiedMask = workingMask;

		handlePartitionCellMaintenance();
	}
}
