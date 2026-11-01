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

#include "Common/LogicAudio.h"
#include "Common/AudioAffect.h"
#include "Common/AudioHandleSpecialValues.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/MapObject.h"							// For MAP_XY_FACTOR
#include "Common/PartitionSolver.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Radar.h"									// For TheRadar
#include "Common/SpecialPower.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Team.h"
#include "Common/Upgrade.h"
#include "GameClient/Anim2D.h"
#include "GameClient/CampaignManager.h"
#include "GameClient/CommandXlat.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"
#include "GameClient/Eva.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameText.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/InGameUI.h"
#include "GameClient/LookAtXlat.h"
#include "GameClient/MessageBox.h"
#include "GameClient/Mouse.h"
#include "GameClient/View.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/Snow.h"
#include "GameLogic/AI.h"
#include "GameLogic/AISkirmishPlayer.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/CaveContain.h"
#include "GameLogic/Module/CommandButtonHuntUpdate.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/DeliverPayloadAIUpdate.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Module/SupplyWarehouseDockUpdate.h"
#include "GameLogic/Module/TransportContain.h"
#include "GameLogic/Module/MobNexusContain.h"
#include "GameLogic/Module/RailroadGuideAIUpdate.h"
#include "GameLogic/Module/StickyBombUpdate.h"
#include "GameLogic/ObjectTypes.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/ScriptActions.h"
#include "ScriptActionsInternal.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/VictoryConditions.h"
#include "GameLogic/AIPathfind.h"
#include "Lib/SafeString.h"

// ScriptActions: players -- construction, money, sciences, relations, buildings

//-------------------------------------------------------------------------------------------------
/** doTeamHunt */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerHunt(const AsciiString& playerName)
{
	Player* pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);

	if (!pPlayer) {
		return;
	}
	pPlayer->setUnitsShouldHunt(true, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doPlayerSellEverything */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerSellEverything(const AsciiString& playerName)
{
	Player* pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);

	if (!pPlayer) {
		return;
	}
	pPlayer->sellEverythingUnderTheSun();
}

//-------------------------------------------------------------------------------------------------
/** doPlayerDisableBaseConstruction */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerDisableBaseConstruction(const AsciiString& playerName)
{
	Player* pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);

	if (!pPlayer) {
		return;
	}
	pPlayer->setCanBuildBase(false);
}

//-------------------------------------------------------------------------------------------------
/** doPlayerDisableFactories */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerDisableFactories(const AsciiString& playerName, const AsciiString& objectName)
{
	Player* pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);

	if (!pPlayer) {
		return;
	}
	pPlayer->setObjectsEnabled(objectName, false);
}

//-------------------------------------------------------------------------------------------------
/** doPlayerDisableUnitConstruction */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerDisableUnitConstruction(const AsciiString& playerName)
{
	Player* pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);
 
	if (!pPlayer) {
		return;
	}
	pPlayer->setCanBuildUnits(false);
}

//-------------------------------------------------------------------------------------------------
/** doPlayerEnableBaseConstruction */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerEnableBaseConstruction(const AsciiString& playerName)
{
	Player* pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);

	if (!pPlayer) {
		return;
	}

	pPlayer->setCanBuildBase(true);
}

//-------------------------------------------------------------------------------------------------
/** doPlayerEnableFactories */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerEnableFactories(const AsciiString& playerName, const AsciiString& objectName)
{
	Player* pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);

	if (!pPlayer) {
		return;
	}
	pPlayer->setObjectsEnabled(objectName, true);
}

//-------------------------------------------------------------------------------------------------
/** doPlayerRepairStructure */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerRepairStructure(const AsciiString& playerName, const AsciiString& structureName)
{
	Player* pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);

	if (!pPlayer) {
		return;
	}
	Object *pStructure = TheScriptEngine->getUnitNamed(structureName);

	if (!pStructure) {
		return;
	}
	pPlayer->repairStructure(pStructure->getID());
}

//-------------------------------------------------------------------------------------------------
/** doPlayerEnableUnitConstruction */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerEnableUnitConstruction(const AsciiString& playerName)
{
	Player* pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);

	if (!pPlayer) {
		return;
	}

	pPlayer->setCanBuildUnits(true);
}

//-------------------------------------------------------------------------------------------------
/** doPlayerKill */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerKill(const AsciiString& playerName)
{
	Player* pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);

	if (!pPlayer) {
		return;
	}
	pPlayer->killPlayer();
}

//-------------------------------------------------------------------------------------------------
/** doPlayerTransferAssetsToPlayer */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerTransferAssetsToPlayer(const AsciiString& playerSrcName, const AsciiString& playerDstName)
{
	Player *pPlayerDest = TheScriptEngine->getPlayerFromAsciiString(playerDstName);
	Player *pPlayerSrc = TheScriptEngine->getPlayerFromAsciiString(playerSrcName);

	if (!pPlayerDest || !pPlayerSrc) {
		return;
	}
	
	pPlayerDest->transferAssetsFromThat(pPlayerSrc);
}

//-------------------------------------------------------------------------------------------------
/** updatePlayerRelationTowardPlayer */
//-------------------------------------------------------------------------------------------------
void ScriptActions::updatePlayerRelationTowardPlayer(const AsciiString& playerSrcName, Int relationType, const AsciiString& playerDstName)
{
	Player *pPlayerDest = TheScriptEngine->getPlayerFromAsciiString(playerDstName);
	Player *pPlayerSrc = TheScriptEngine->getPlayerFromAsciiString(playerSrcName);

	if (!pPlayerDest || !pPlayerSrc) {
		return;
	}

	pPlayerSrc->setPlayerRelationship(pPlayerDest, static_cast<Relationship>(relationType));
}

//-------------------------------------------------------------------------------------------------
/** doPlayerGarrisonAllBuildings */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerGarrisonAllBuildings(const AsciiString& playerName)
{
	Player* player = TheScriptEngine->getPlayerFromAsciiString(playerName);
	
	if (!player) {
		return;
	}

	player->garrisonAllUnits(CMD_FROM_SCRIPT);	
}

//-------------------------------------------------------------------------------------------------
/** doPlayerExitAllBuildings */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerExitAllBuildings(const AsciiString& playerName)
{
	Player* player = TheScriptEngine->getPlayerFromAsciiString(playerName);
	
	if (!player) {
		return;
	}

	player->ungarrisonAllUnits(CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doSetMoney */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doSetMoney(const AsciiString& playerName, Int money)
{
	Player* player = TheScriptEngine->getPlayerFromAsciiString(playerName);

	if (!player) {
		return;
	}

	Money *m = player->getMoney();
	if (!m)
		return;

	m->withdraw(m->countMoney());
	m->deposit(money);
}

//-------------------------------------------------------------------------------------------------
/** doGiveMoney */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doGiveMoney(const AsciiString& playerName, Int money)
{
	Player* player = TheScriptEngine->getPlayerFromAsciiString(playerName);

	if (!player) {
		return;
	}

	Money *m = player->getMoney();
	if (!m)
		return;

	if (money < 0)
		m->withdraw(-money);
	else
		m->deposit(money);
}

//-------------------------------------------------------------------------------------------------
/** doPlayerSetOverrideRelationToTeam */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerSetOverrideRelationToTeam(const AsciiString& playerName, const AsciiString& otherTeam, Int relation)
{
	Player *thePlayer = ThePlayerList->findPlayerWithNameKey(NAMEKEY(playerName));
	Team *theOtherTeam = TheScriptEngine->getTeamNamed( otherTeam );
	if (thePlayer && theOtherTeam) {
		thePlayer->setTeamRelationship(theOtherTeam, static_cast<Relationship>(relation));
	}
}

//-------------------------------------------------------------------------------------------------
/** doPlayerRemoveOverrideRelationToTeam */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerRemoveOverrideRelationToTeam(const AsciiString& playerName, const AsciiString& otherTeam)
{
	Player *thePlayer = ThePlayerList->findPlayerWithNameKey(NAMEKEY(playerName));
	Team *theOtherTeam = TheScriptEngine->getTeamNamed( otherTeam );
	if (thePlayer && theOtherTeam) {
		thePlayer->removeTeamRelationship(theOtherTeam);
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerAddSkillPoints(const AsciiString& playerName, Int delta)
{
	Player* pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if (!pPlayer)
		return;
	pPlayer->addSkillPoints(delta);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerAddRankLevels(const AsciiString& playerName, Int delta)
{
	Player* pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if (!pPlayer)
		return;
	pPlayer->setRankLevel(pPlayer->getRankLevel() + delta);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerSetRankLevel(const AsciiString& playerName, Int level)
{
	Player* pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if (!pPlayer)
		return;
	pPlayer->setRankLevel(level);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doMapSetRankLevelLimit(Int level)
{
	if (TheGameLogic)
		TheGameLogic->setRankLevelLimit(level);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerGrantScience(const AsciiString& playerName, const AsciiString& scienceName)
{
	Player* pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if (!pPlayer)
		return;
	ScienceType science = TheScienceStore->getScienceFromInternalName(scienceName);
	if (science == SCIENCE_INVALID)
		return;
	pPlayer->grantScience(science);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerPurchaseScience(const AsciiString& playerName, const AsciiString& scienceName)
{
	Player* pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if (!pPlayer)
		return;
	ScienceType science = TheScienceStore->getScienceFromInternalName(scienceName);
	if (science == SCIENCE_INVALID)
		return;
	pPlayer->attemptToPurchaseScience(science);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerSetScienceAvailability( const AsciiString& playerName, const AsciiString& scienceName, const AsciiString& scienceAvailability )
{
	Player* player = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if( player )
	{
		ScienceAvailabilityType saType = player->getScienceAvailabilityTypeFromString( scienceAvailability );
		if( saType != SCIENCE_AVAILABILITY_INVALID )
		{
			ScienceType science = TheScienceStore->getScienceFromInternalName( scienceName );
			if( science != SCIENCE_INVALID )
			{
				player->setScienceAvailability( science, saType );
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doAffectPlayerSkillset(const AsciiString& playerName, Int skillset)
{
	Player *player = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if (!player) {
		return;
	}

	--skillset;
	player->friend_setSkillset(skillset);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doAffectSkillPointsModifier(const AsciiString& playerName, Real newModifier)
{
	Player *playerDst = TheScriptEngine->getPlayerFromAsciiString(playerName);

	if (!playerDst) {
		return;
	}

	playerDst->setSkillPointsModifier(newModifier);
		
}
