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

// ScriptActions: the camera and the view -- moves, zoom, rotation, tether, shake, letterbox, sky, weather
//
// Waypoints and units are looked up here, during the tick; the view calls themselves go through
// TheLogicToClient->camera() and run after the tick (or earlier, when the logic reads camera state).
// Objects are passed by ID, waypoints by name: both are looked up again when the call runs.

// The first waypoint with this name, as the loops below find it.
static Waypoint *findWaypointNamed( const AsciiString& name )
{
	for (Waypoint *way = TheTerrainLogic->getFirstWaypoint(); way; way = way->getNext())
		if (way->getName() == name)
			return way;
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
/** doCameraFollowNamed */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doCameraFollowNamed(const AsciiString& unit, Bool snapToUnit)
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );
	if (theObj) 
	{	
		const ObjectID id = theObj->getID();
		TheLogicToClient->camera( [id, snapToUnit]()
		{
			TheTacticalView->setCameraLock(id);
			if (snapToUnit)
				TheTacticalView->snapToCameraLock();

			TheTacticalView->setSnapMode( View::CameraLockType::LOCK_FOLLOW, 0.0f );
		} );
	}
}

//-------------------------------------------------------------------------------------------------
/** doStopCameraFollowUnit */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doStopCameraFollowUnit(void)
{
	TheLogicToClient->camera( []() { TheTacticalView->setCameraLock(INVALID_ID); } );
}

//-------------------------------------------------------------------------------------------------
/** doMoveCameraTo */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doMoveCameraTo(const AsciiString& waypoint, Real sec, Real cameraStutterSec, Real easeIn, Real easeOut)
{
	for (Waypoint *way = TheTerrainLogic->getFirstWaypoint(); way; way = way->getNext()) {
		if (way->getName() == waypoint) {
			const Coord3D destination = *way->getLocation();
			TheLogicToClient->camera( [destination, sec, cameraStutterSec, easeIn, easeOut]()
			{
				TheTacticalView->moveCameraTo(&destination, static_cast<Int>(sec*1000), static_cast<Int>(cameraStutterSec*1000), true, easeIn*1000.0f, easeOut*1000.0f);
			} );
			break;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doZoomCamera */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doZoomCamera(Real zoom, Real sec, Real easeIn, Real easeOut)
{
	TheLogicToClient->camera( [zoom, sec, easeIn, easeOut]() { TheTacticalView->zoomCamera(zoom, static_cast<Int>(sec*1000.0f), easeIn*1000.0f, easeOut*1000.0f); } );
}

//-------------------------------------------------------------------------------------------------
/** doPitchCamera */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPitchCamera(Real pitch, Real sec, Real easeIn, Real easeOut)
{
	TheLogicToClient->camera( [pitch, sec, easeIn, easeOut]() { TheTacticalView->pitchCamera(pitch, static_cast<Int>(sec*1000.0f), easeIn*1000.0f, easeOut*1000.0f); } );
}

//-------------------------------------------------------------------------------------------------
/** doOversizeTheTerrain */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doOversizeTheTerrain(Int amount)
{
	oversizeTheTerrain(amount);
	TheLogicToClient->camera( []()
	{
		Coord2D offset;
		offset.x = 0.0001f;
		offset.y = 0.0001f;
		TheTacticalView->scrollBy(&offset);
		offset.x = -0.0001f;
		offset.y = -0.0001f;
		TheTacticalView->scrollBy(&offset);
	} );
}

//-------------------------------------------------------------------------------------------------
/** doSetupCamera */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doSetupCamera(const AsciiString& waypoint, Real zoom, Real pitch, const AsciiString& lookAtWaypoint)
{
	Waypoint *way = TheTerrainLogic->getWaypointByName(waypoint);
	if (way==nullptr) return;
	Coord3D	pos = *way->getLocation();
	Waypoint *lookat = TheTerrainLogic->getWaypointByName(lookAtWaypoint); 
	if (lookat==nullptr) return;
	Coord3D destination = *lookat->getLocation();
	TheLogicToClient->camera( [pos, destination, pitch, zoom]() mutable
	{
		TheTacticalView->moveCameraTo(&pos, 0, 0, true, 0.0f, 0.0f);
		TheTacticalView->cameraModLookToward(&destination);
		TheTacticalView->cameraModFinalPitch(pitch, 0.0f, 0.0f);
		TheTacticalView->cameraModFinalZoom(zoom, 0.0f, 0.0f);
	} );
}

//-------------------------------------------------------------------------------------------------
/** doModCameraLookToward */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doModCameraLookToward(const AsciiString& waypoint)
{
	for (Waypoint *way = TheTerrainLogic->getFirstWaypoint(); way; way = way->getNext()) {
		if (way->getName() == waypoint) {
			Coord3D destination = *way->getLocation();
			TheLogicToClient->camera( [destination]() mutable { TheTacticalView->cameraModLookToward(&destination); } );
			break;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doModCameraFinalLookToward */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doModCameraFinalLookToward(const AsciiString& waypoint)
{
	for (Waypoint *way = TheTerrainLogic->getFirstWaypoint(); way; way = way->getNext()) {
		if (way->getName() == waypoint) {
			Coord3D destination = *way->getLocation();
			TheLogicToClient->camera( [destination]() mutable { TheTacticalView->cameraModFinalLookToward(&destination); } );
			break;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doModCameraMoveToSelection */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doModCameraMoveToSelection(void)
{
	Int count=0;
	Coord3D destination;
	destination.x=destination.y=destination.z = 0;

	for (Drawable *d = TheGameClient->firstDrawable(); d; d = d->getNextDrawable())
	{
		if (d->isSelected())
		{	
			Coord3D pos = *d->getPosition();
			destination.x += pos.x;
			destination.y += pos.y;
			destination.z += pos.z;
			count++;
		}
	}
	if (count) {
		destination.z /= count;
		destination.x /= count;
		destination.y /= count;
		TheLogicToClient->camera( [destination]() mutable { TheTacticalView->cameraModFinalMoveTo(&destination); } );
	}

}

//-------------------------------------------------------------------------------------------------
/** doResetCamera */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doResetCamera(const AsciiString& waypoint, Real sec, Real easeIn, Real easeOut)
{
	for (Waypoint *way = TheTerrainLogic->getFirstWaypoint(); way; way = way->getNext()) {
		if (way->getName() == waypoint) {
			const Coord3D destination = *way->getLocation();
			TheLogicToClient->camera( [destination, sec, easeIn, easeOut]()
			{
				TheTacticalView->resetCamera(&destination, static_cast<Int>(sec*1000), easeIn*1000.0f, easeOut*1000.0f);
			} );
			break;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doRotateCamera around the object we are looking at. */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doRotateCamera(Real rotations, Real sec, Real easeIn, Real easeOut)
{
	TheLogicToClient->camera( [rotations, sec, easeIn, easeOut]() { TheTacticalView->rotateCamera(rotations, static_cast<Int>(sec*1000.0f), easeIn*1000.0f, easeOut*1000.0f); } );
}

//-------------------------------------------------------------------------------------------------
/** doRotateCameraTowardObject */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doRotateCameraTowardObject(const AsciiString& unitName, Real sec, Real holdSec, Real easeIn, Real easeOut)
{
	const Object *unit = TheScriptEngine->getUnitNamed(unitName);
	if (!unit)
		return;
	const ObjectID id = unit->getID();
	TheLogicToClient->camera( [id, sec, holdSec, easeIn, easeOut]()
	{
		TheTacticalView->rotateCameraTowardObject(id, static_cast<Int>(sec*1000.0f), static_cast<Int>(holdSec*1000.0f), easeIn*1000.0f, easeOut*1000.0f);
	} );
}

//-------------------------------------------------------------------------------------------------
/** doRotateCameraTowardWaypoint */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doRotateCameraTowardWaypoint(const AsciiString& waypointName, Real sec, Real easeIn, Real easeOut, Bool reverseRotation)
{
	Waypoint *way = TheTerrainLogic->getWaypointByName(waypointName);
	if (way==nullptr) return;
	const Coord3D location = *way->getLocation();
	TheLogicToClient->camera( [location, sec, easeIn, easeOut, reverseRotation]()
	{
		TheTacticalView->rotateCameraTowardPosition(&location, static_cast<Int>(sec*1000.0f), easeIn*1000.0f, easeOut*1000.0f, reverseRotation);
	} );
}

//-------------------------------------------------------------------------------------------------
/** doMoveAlongWaypointPath */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doMoveCameraAlongWaypointPath(const AsciiString& waypoint, Real sec, Real cameraStutterSec, Real easeIn, Real easeOut)
{
	if (findWaypointNamed(waypoint) == nullptr)
		return;
	// The view walks the waypoint path itself; waypoints are map data and stay until the map unloads.
	TheLogicToClient->camera( [waypoint, sec, cameraStutterSec, easeIn, easeOut]()
	{
		if (Waypoint *way = findWaypointNamed(waypoint))
			TheTacticalView->moveCameraAlongWaypointPath(way, static_cast<Int>(sec*1000), static_cast<Int>(cameraStutterSec*1000), true, easeIn*1000.0f, easeOut*1000.0f);
	} );
}

//-------------------------------------------------------------------------------------------------
/** doCameraMoveHome */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doCameraMoveHome(void)
{

}

//-------------------------------------------------------------------------------------------------
/** doCameraMotionBlurJump - zoom in at the current location, jump to waypoint, and zoom out.*/
//-------------------------------------------------------------------------------------------------
void ScriptActions::doCameraMotionBlurJump(const AsciiString& waypointName, Bool saturate)
{
	Waypoint *way = TheTerrainLogic->getWaypointByName(waypointName);
	if (!way) {
		return;
	}

	const Coord3D pos = *way->getLocation();
	TheLogicToClient->camera( [pos, saturate]()
	{
		Bool passed = FALSE;	//assume all filters were applied correctly.
		if (TheTacticalView->setViewFilter(FT_VIEW_MOTION_BLUR_FILTER))
		{
			passed = TRUE;
			if (saturate) {
				if (!TheTacticalView->setViewFilterMode(FM_VIEW_MB_IN_AND_OUT_SATURATE))
				{	//failed to set filter so restore default state
					TheTacticalView->setViewFilter(FT_NULL_FILTER);
					passed = FALSE;
				}
			} else {
				if (!TheTacticalView->setViewFilterMode(FM_VIEW_MB_IN_AND_OUT_ALPHA))
				{	//failed to set filter so restore default state
					TheTacticalView->setViewFilter(FT_NULL_FILTER);
					passed = FALSE;
				};
			}
			if (passed)
				TheTacticalView->setViewFilterPos(&pos);
		}
		if (!passed)
		{	//if we failed to apply the filter, we still need to get the camera to the target
			//so do it another way:
			TheTacticalView->lookAt(&pos);
		}
	} );
}

//-------------------------------------------------------------------------------------------------
/** doCameraMotionBlurJump - zoom in at the current location, jump to waypoint, and zoom out.*/
//-------------------------------------------------------------------------------------------------
void ScriptActions::doCameraMotionBlur(Bool zoomIn, Bool saturate)
{
	TheLogicToClient->camera( [zoomIn, saturate]()
	{
		if (TheTacticalView->setViewFilter(FT_VIEW_MOTION_BLUR_FILTER))
		{
			enum FilterModes mode;
			if (saturate) {
				if (zoomIn) {
					mode = FM_VIEW_MB_IN_SATURATE;
				} else {
					mode = FM_VIEW_MB_OUT_SATURATE;
				}
			} else {
				if (zoomIn) {
					mode = FM_VIEW_MB_IN_ALPHA;
				} else {
					mode = FM_VIEW_MB_OUT_ALPHA;
				}
			}
			if (!TheTacticalView->setViewFilterMode(mode))
			{	//failed to set the filter so restore everything to normal
				TheTacticalView->setViewFilter(FT_NULL_FILTER);
			}
		}
	} );
}

//-------------------------------------------------------------------------------------------------
/** doLetterBoxMode */
//-------------------------------------------------------------------------------------------------
// Client part, run after the logic tick.
static void setLetterBoxMode( Bool startLetterbox )
{
	if (startLetterbox)
	{
		HideControlBar(TRUE);
		TheDisplay->enableLetterBox(TRUE);
	}
	else
	{
		ShowControlBar(FALSE);
		TheDisplay->enableLetterBox(FALSE);
	}
}

void ScriptActions::doLetterBoxMode(Bool startLetterbox)
{
	TheLogicToClient->post( [startLetterbox]() { setLetterBoxMode( startLetterbox ); } );
}

//-------------------------------------------------------------------------------------------------
/** doBlackWhiteMode */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doBlackWhiteMode(Bool startBWMode, Int frames)
{
	TheLogicToClient->camera( [startBWMode, frames]()
	{
		if (startBWMode)
		{
			TheTacticalView->setViewFilterMode(FM_VIEW_BW_BLACK_AND_WHITE);
			TheTacticalView->setViewFilter(FT_VIEW_BW_FILTER);
			TheTacticalView->setFadeParameters(frames, 1);
		}
		else
		{
			if ((TheTacticalView->getViewFilterType()) == FT_VIEW_BW_FILTER)
			{	//mode already set, turn it off
				TheTacticalView->setFadeParameters(frames, -1);
			}
		}
	} );
}

void ScriptActions::doSkyBox(Bool startDraw)
{
	if (startDraw)
	{
		doSkyBoxSet(1);
	}
	else
	{
		doSkyBoxSet(0);
	}
}

void ScriptActions::doWeather(Bool showWeather)
{
	TheSnowManager->setVisible(showWeather);
}

//-------------------------------------------------------------------------------------------------
/** Set the audible distance for shots in which the camera is up, and is therefore the center */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doCameraSetAudibleDistance(Real audibleDistance)
{
	// No-op
}

//-------------------------------------------------------------------------------------------------
/** doCameraTetherNamed */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doCameraTetherNamed(const AsciiString& unit, Bool snapToUnit, Real play)
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );
	if (theObj) 
	{	
		const ObjectID id = theObj->getID();
		TheLogicToClient->camera( [id, snapToUnit, play]()
		{
			TheTacticalView->setCameraLock(id);
			if (snapToUnit)
				TheTacticalView->snapToCameraLock();

			TheTacticalView->setSnapMode( View::CameraLockType::LOCK_TETHER, play );
		} );
	}
}

//-------------------------------------------------------------------------------------------------
/** doCameraStopTetherNamed */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doCameraStopTetherNamed(void)
{
	TheLogicToClient->camera( []() { TheTacticalView->setCameraLock(INVALID_ID); } );
}

//-------------------------------------------------------------------------------------------------
/** doCameraSetDefault */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doCameraSetDefault(Real pitch, Real angle, Real maxHeight)
{
	TheLogicToClient->camera( [pitch, angle, maxHeight]() { TheTacticalView->setDefaultView(pitch, angle, maxHeight); } );
}

//-------------------------------------------------------------------------------------------------
/** doScreenShake */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doScreenShake( UnsignedInt intensity )
{
	TheLogicToClient->camera( [intensity]()
	{
		Coord3D pos;
		TheTacticalView->getPosition( &pos );
		TheTacticalView->shake( &pos, static_cast<View::CameraShakeType>(intensity) );
	} );
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doC3CameraDisableSlaveMode( void )
{
	TheLogicToClient->camera( []() { TheTacticalView->cameraDisableSlaveMode(); } );
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doResizeViewGuardband(const Real gbx, const Real gby)
{

	const Coord2D newGuardBand = { gbx, gby };

	TheLogicToClient->camera( [newGuardBand]() { TheTacticalView->setGuardBandBias( &newGuardBand ); } );

}
