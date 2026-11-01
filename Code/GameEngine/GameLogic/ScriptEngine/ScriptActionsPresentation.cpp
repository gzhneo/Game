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


enum
{
	SUBTITLE_DURATION = 8000
};

// ScriptActions: what the player sees and hears -- sound, music, movies, text, radar, counters, timers

//-------------------------------------------------------------------------------------------------
/** doDebugMessage */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doDebugMessage(const AsciiString& msg, Bool pause )
{
	TheScriptEngine->AppendDebugMessage(msg, pause);
}  

//-------------------------------------------------------------------------------------------------
/** doPlaySoundEffect */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlaySoundEffect(const AsciiString& sound)
{
	AudioEventRTS audioEvent(sound);
	audioEvent.setIsLogicalAudio(true);
	audioEvent.setPlayerIndex(ThePlayerList->getLocalPlayer()->getPlayerIndex());
	TheLogicAudio->addAudioEvent( &audioEvent );
}  

//-------------------------------------------------------------------------------------------------
/** doPlaySoundEffectAt */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlaySoundEffectAt(const AsciiString& sound, const AsciiString& waypoint)
{	
	Waypoint *way = TheTerrainLogic->getWaypointByName(waypoint);
	if (!way) {
		return;
	}

	AudioEventRTS audioEvent(sound, way->getLocation());
	audioEvent.setIsLogicalAudio(true);
	audioEvent.setPlayerIndex(ThePlayerList->getLocalPlayer()->getPlayerIndex());
	TheLogicAudio->addAudioEvent( &audioEvent );
}  

//-------------------------------------------------------------------------------------------------
/** doEnableObjectSound */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doEnableObjectSound(const AsciiString& objectName, Bool enable )
{	
	Object *object = TheScriptEngine->getUnitNamed(objectName);
	if (!object)
	{
		return;
	}

	Drawable *drawable = object->getDrawable();
	if ( !drawable )
	{
		return;
	}

  drawable->enableAmbientSoundFromScript( enable );
}	

//-------------------------------------------------------------------------------------------------
/** doDisplayText */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doDisplayText(const AsciiString& displayText)
{
	TheLogicToClient->uiMessage(displayText);	
}

//-------------------------------------------------------------------------------------------------
/** doInGamePopupMessage */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doInGamePopupMessage( const AsciiString& message, Int x, Int y, Int width, Bool pause )
{
	// With GENERALS_SCRIPT_TRACE set, missions run without a player (tools/determinism/missions.py);
	// a popup with pause would stop them until someone clicks OK. Skip it as if the player had
	// confirmed at once. Without the environment variable nothing changes.
	if (getenv("GENERALS_SCRIPT_TRACE"))
		return;
	TheInGameUI->popupMessage(message, x,y,width, pause, FALSE);
}

//-------------------------------------------------------------------------------------------------
/** doDisplayCinematicText */
//-------------------------------------------------------------------------------------------------
// Client part, run after the logic tick.
static void showCinematicText( const AsciiString& displayText, const AsciiString& fontType, Int timeInSeconds )
{
	// set the text
	UnicodeString uStr = TheGameText->fetch( displayText );
	AsciiString aStr;
	aStr.translate( uStr );
	TheDisplay->setCinematicText( aStr );

	// Gets the font info from parsing through fontType

	// get the font name
	AsciiString fontName = AsciiString::TheEmptyString;
	char buf[256];
	char *c;
	safe_strcpy(buf, fontType.str());
	for( c = buf; *c != '\0'; *c++ )
	{
		// append the character; the loop header advances the pointer
		if( *c != ' ' && *c != '-' )
			fontName.concat( *c );
		else
			break;
	}
	while( *c != ':' )
		*c++;
	*c++;  // eat through " - Size:"

	// get font size
	AsciiString fontSize = AsciiString::TheEmptyString;
	for( ; *c != '\0'; *c++ )
	{
		if( *c != '\0' && *c != ' ' )
		{
			fontSize.concat( *c );
		}
		else
		{
			break;
		}
	}
	Int size = atoi( fontSize.str() );

	// get font fold
	Bool bold = FALSE;
	if( fontType.endsWith( "[Bold]" ) )
		bold = TRUE;

	// phew, now set as new font
	GameFont *font = TheFontLibrary->getFont( fontName, 
		TheGlobalLanguageData->adjustFontSize(size), bold );
	TheDisplay->setCinematicFont( font );

	// set time
	Int frames = LOGICFRAMES_PER_SECOND * timeInSeconds;
	TheDisplay->setCinematicTextFrames( frames );
}


void ScriptActions::doDisplayCinematicText(const AsciiString& displayText, const AsciiString& fontType, Int timeInSeconds)
{
	TheLogicToClient->post( [displayText, fontType, timeInSeconds]() { showCinematicText( displayText, fontType, timeInSeconds ); } );
}
//-------------------------------------------------------------------------------------------------
/** doCameoFlash */
//-------------------------------------------------------------------------------------------------
// Client part, run after the logic tick.
static void flashCameo( const AsciiString& name, Int timeInSeconds )
{
	const CommandButton *button;

	//sanity
	button = TheControlBar->findCommandButton( name );
	if( button == nullptr )
	{
		DEBUG_CRASH(( "ScriptActions::doCameoFlash can't find AsciiString cameoflash" ));
		return;
	}

	Int frames = LOGICFRAMES_PER_SECOND * timeInSeconds;
	// every time the framecount % 20 == 0,  controlbar:: update will do Cameo Flash
	Int count = frames / DRAWABLE_FRAMES_PER_FLASH;
	// make sure count is even, so the cameo will return to its original state
	if( count % 2 == 1 )
		count++;

	button->setFlashCount(count);
	TheControlBar->setFlash( TRUE );

}

void ScriptActions::doCameoFlash(const AsciiString& name, Int timeInSeconds)
{
	TheLogicToClient->post( [name, timeInSeconds]() { flashCameo( name, timeInSeconds ); } );
}

//-------------------------------------------------------------------------------------------------
/** doNamedCustomColor */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedCustomColor(const AsciiString& unitName, Color c)
{
	//sanity
	Object *obj = TheScriptEngine->getUnitNamed( unitName );
	if ( !obj )
	{
		return;
	}
	obj->setCustomIndicatorColor(c);
}

//-------------------------------------------------------------------------------------------------
/** doNamedFlash */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedFlash(const AsciiString& unitName, Int timeInSeconds, const RGBColor *color)
{
	/** This is called the first time this unit is told by the script to flash. timeInSeconds will tell the drawable 
	how long to flash for.  Sets drawable to start flashing but only allows drawable's update to
	call the actual flash method */

	//sanity
	Object *obj = TheScriptEngine->getUnitNamed( unitName );
	if ( !obj )
	{
		return;
	}
	Drawable *drawable = obj->getDrawable();
	if( !drawable )
	{
		return;
	}

	if( timeInSeconds > 0 )
	{
		// set count for drawable, but do not flash, allow drawable update to handle it

		/* The designer specifies how long he wants the unit to flash for.  We will
		convert this number into a count of how many times to call the doNamedFlash method */
		Int frames = LOGICFRAMES_PER_SECOND * timeInSeconds;
		// every time the framecount % 20 == 0, drawable::update will call doNamedFlash
		Int count = frames / DRAWABLE_FRAMES_PER_FLASH;
		Color flashy = (color == nullptr) ? obj->getIndicatorColor() : color->getAsInt();
		TheLogicToClient->drawable( drawable, [flashy, count]( Drawable *d ) { d->setFlashColor( flashy ); d->setFlashCount( count ); } );
		return;
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamFlash */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamFlash(const AsciiString& teamName, Int timeInSeconds, const RGBColor *color)
{
	Team *team = TheScriptEngine->getTeamNamed( teamName );
	if (team == nullptr || !team->hasAnyObjects())
		return;

	DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList();

	while( !iter.done() ) {
		Object *nextObj = iter.cur();
		Object *obj = nextObj;
		if (!obj) {
			break;
		}

		iter.advance();
		Drawable *draw = obj->getDrawable();
		if( !draw )
			break;
		Int frames = LOGICFRAMES_PER_SECOND * timeInSeconds;

		Int count = frames / DRAWABLE_FRAMES_PER_FLASH;
		Color flashy = (color == nullptr) ? obj->getIndicatorColor() : color->getAsInt();
		TheLogicToClient->drawable( draw, [flashy, count]( Drawable *d ) { d->setFlashColor( flashy ); d->setFlashCount( count ); } );
	}

}

//-------------------------------------------------------------------------------------------------
/** doMoviePlayFullScreen */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doMoviePlayFullScreen(const AsciiString& movieName)
{
	TheDisplay->playMovie(movieName);
}

//-------------------------------------------------------------------------------------------------
/** doMoviePlayRadar */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doMoviePlayRadar(const AsciiString& movieName)
{
	TheInGameUI->playMovie(movieName);
}

//-------------------------------------------------------------------------------------------------
/** doSoundPlayFromNamed */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doSoundPlayFromNamed(const AsciiString& soundName, const AsciiString& unitName)
{
	Object *pUnit = TheScriptEngine->getUnitNamed(unitName);

	if (!pUnit) {
		return;
	}
	
	AudioEventRTS sfx(soundName, pUnit->getID());
	sfx.setIsLogicalAudio(true);
	TheLogicAudio->addAudioEvent(&sfx);
}

void ScriptActions::doSpeechPlay(const AsciiString& speechName, Bool allowOverlap)
{	
	AudioEventRTS speech(speechName);
	speech.setIsLogicalAudio(true);
	speech.setPlayerIndex(ThePlayerList->getLocalPlayer()->getPlayerIndex());
	speech.setUninterruptable(!allowOverlap);
	TheLogicAudio->addAudioEvent(&speech);
	
	
	AsciiString subtitleLabel("DIALOGEVENT:");
	subtitleLabel.concat(speechName);
	subtitleLabel.concat("Subtitle");
	
	// Found is important, because a failure will return a valid "Missing: 'Label'" string.
	Bool found = FALSE;
	UnicodeString subtitle = TheGameText->fetch(subtitleLabel, &found);
	if( found && !subtitle.isEmpty() && subtitle.getCharAt(0) != '*')
	{
		// Foreign versions can specify region specifc subtitle strings if they want.
		// English will have strings with / for easy translation, but they don't want to display.
		TheLogicToClient->uiMilitarySubtitle( subtitleLabel, SUBTITLE_DURATION );
	}
}

//-------------------------------------------------------------------------------------------------
/** doRadarCreateEvent */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doRadarCreateEvent(Coord3D *pos, Int eventType)
{
	TheRadar->createEvent(pos, static_cast<RadarEventType>(eventType));
}

//-------------------------------------------------------------------------------------------------
/** doObjectRadarCreateEvent */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doObjectRadarCreateEvent(const AsciiString& unitName, Int eventType)
{
	// get the building
	Object *theBuilding = TheScriptEngine->getUnitNamed( unitName );
	if (!theBuilding)
		return;
	
	// get building's position
	const Coord3D *pos = theBuilding->getPosition();
	if (!pos)
		return;
	
	// create event
	TheRadar->createEvent(pos, static_cast<RadarEventType>(eventType));
}

//-------------------------------------------------------------------------------------------------
/** doTeamRadarCreateEvent */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamRadarCreateEvent(const AsciiString& teamName, Int eventType)
{
	// get the team
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeam)
		return;
	if (!theTeam->hasAnyUnits())
		return;
	
	// get team's position
	const Coord3D *pos = theTeam->getEstimateTeamPosition();
	if (!pos)
		return;

	// create event
	TheRadar->createEvent(pos, static_cast<RadarEventType>(eventType));
}

//-------------------------------------------------------------------------------------------------
/** doRadarDisable */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doRadarDisable(void)
{
	TheRadar->hide(true);
}

//-------------------------------------------------------------------------------------------------
/** doRadarEnable */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doRadarEnable(void)
{
	TheRadar->hide(false);
}

//-------------------------------------------------------------------------------------------------
/** doAmbientSoundsPause */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doAmbientSoundsPause(Bool pausing)	// if true, then pause, if false then resume.
{
	TheLogicAudio->pauseAmbient(pausing);
}

//-------------------------------------------------------------------------------------------------
/** doMusicTrackChange */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doMusicTrackChange(const AsciiString& newTrackName, Bool fadeout, Bool fadein)
{
	// Stop playing the music
	if (fadeout) {
		TheLogicAudio->removeAudioEvent(AHSV_StopTheMusicFade);
	} else {
		TheLogicAudio->removeAudioEvent(AHSV_StopTheMusic);
	}

	AudioEventRTS event(newTrackName);
	event.setShouldFade(fadein);
	event.setPlayerIndex(ThePlayerList->getLocalPlayer()->getPlayerIndex());
	TheLogicAudio->addAudioEvent(&event);

	TheScriptEngine->setCurrentTrackName(newTrackName);
}

//-------------------------------------------------------------------------------------------------
/** Show a military briefing */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doMilitaryCaption(const AsciiString& briefing, Int duration)
{
#if defined(_DEBUG)
	if (TheGlobalData->m_disableMilitaryCaption)
		duration = 1;
#endif

	TheLogicToClient->uiMilitarySubtitle(briefing, duration);
}

//-------------------------------------------------------------------------------------------------
/** doDisableSpecialPowerDisplay */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doDisableSpecialPowerDisplay(void)
{
	TheLogicToClient->uiSetSuperweaponDisplayEnabledByScript(false);
}

//-------------------------------------------------------------------------------------------------
/** doEnableSpecialPowerDisplay */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doEnableSpecialPowerDisplay(void)
{
	TheLogicToClient->uiSetSuperweaponDisplayEnabledByScript(true);
}

//-------------------------------------------------------------------------------------------------
/** doNamedHideSpecialPowerDisplay */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedHideSpecialPowerDisplay(const AsciiString& unit)
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );
	if (theObj)
	{
		TheLogicToClient->uiSetObjectSuperweaponHiddenByScript(theObj->getID(), TRUE);
	}
}

//-------------------------------------------------------------------------------------------------
/** doNamedShowSpecialPowerDisplay */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedShowSpecialPowerDisplay(const AsciiString& unit)
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );
	if (theObj)
	{
		TheLogicToClient->uiSetObjectSuperweaponHiddenByScript(theObj->getID(), FALSE);
	}
}

//-------------------------------------------------------------------------------------------------
/** doAudioSetVolume */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doAudioSetVolume(AudioAffect whichToAffect, Real newVolumeLevel)
{
	newVolumeLevel /= 100.0f;
	if (newVolumeLevel < 0.0f) {
		newVolumeLevel = 0.0f;
	} else if (newVolumeLevel > 1.0f) {
		newVolumeLevel = 1.0f;
	}

	TheLogicAudio->setVolume(newVolumeLevel, whichToAffect);
}

//-------------------------------------------------------------------------------------------------
/** doDisplayCounter */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doDisplayCounter(const AsciiString& counterName, const AsciiString& counterText)
{
	TheLogicToClient->uiAddNamedTimer(counterName, TheGameText->fetch(counterText), false);
}

//-------------------------------------------------------------------------------------------------
/** doHideCounter */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doHideCounter(const AsciiString& counterName)
{
	TheLogicToClient->uiRemoveNamedTimer(counterName);
}

//-------------------------------------------------------------------------------------------------
/** doDisplayCountdownTimer */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doDisplayCountdownTimer(const AsciiString& timerName, const AsciiString& timerText)
{
	TheLogicToClient->uiAddNamedTimer(timerName, TheGameText->fetch(timerText), true);
}

//-------------------------------------------------------------------------------------------------
/** doHideCountdownTimer */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doHideCountdownTimer(const AsciiString& timerName)
{
	TheLogicToClient->uiRemoveNamedTimer(timerName);
}

//-------------------------------------------------------------------------------------------------
/** doDisableCountdownTimerDisplay */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doDisableCountdownTimerDisplay(void)
{
	TheLogicToClient->uiShowNamedTimerDisplay(FALSE);
}

//-------------------------------------------------------------------------------------------------
/** doEnableCountdownTimerDisplay */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doEnableCountdownTimerDisplay(void)
{
	TheLogicToClient->uiShowNamedTimerDisplay(TRUE);
}

//-------------------------------------------------------------------------------------------------
/** doRadarRefresh */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doRadarRefresh( void )
{
	TheRadar->refreshTerrain( TheTerrainLogic );
}

//-------------------------------------------------------------------------------------------------
/** doRadarForceEnable */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doRadarForceEnable(void)
{
	TheRadar->forceOn(true);
}

//-------------------------------------------------------------------------------------------------
/** doRadarRevertNormal */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doRadarRevertNormal(void)
{
	TheRadar->forceOn(false);
}

//-------------------------------------------------------------------------------------------------
/** doSoundEnableType */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doSoundEnableType( const AsciiString& soundEventName, Bool enable )
{
	TheLogicAudio->setAudioEventEnabled(soundEventName, enable);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doSoundRemoveAllDisabled()
{
	TheLogicAudio->removeDisabledEvents(); 
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doSoundRemoveType( const AsciiString& soundEventName )
{
	TheLogicAudio->removeAudioEvent( soundEventName );
}

//-------------------------------------------------------------------------------------------------
/** doSoundOverrideVolume */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doSoundOverrideVolume( const AsciiString& soundEventName, Real newVolume )
{
	TheLogicAudio->setAudioEventVolumeOverride(soundEventName, newVolume / 100.0f);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamEmoticon(const AsciiString& teamName, const AsciiString& emoticonName, Real duration)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if( !theTeam ) 
	{
		return;
	}

	AIGroup* theGroup = TheAI->createGroup();
	if( !theGroup ) 
	{
		return;
	}
	theTeam->getTeamAsAIGroup( theGroup );
	
	Int frames = static_cast<Int>( duration * static_cast<Real>(LOGICFRAMES_PER_SECOND) );
	theGroup->groupSetEmoticon( emoticonName, frames );
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedEmoticon(const AsciiString& unitName, const AsciiString& emoticonName, Real duration)
{
	Object *obj = TheScriptEngine->getUnitNamed( unitName );
	if( obj )
	{
		Drawable *draw = obj->getDrawable();
		if( draw )
		{
			Int frames = static_cast<Int>( duration * static_cast<Real>(LOGICFRAMES_PER_SECOND) );
			TheLogicToClient->drawableSetEmoticon( draw, emoticonName, frames );
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doEvaEnabledDisabled(Bool setEnabled)
{
	TheLogicToClient->evaSetEnabled(setEnabled);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doSetOcclusionMode(Bool setEnabled)
{
	TheGameLogic->setShowBehindBuildingMarkers(setEnabled);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doSetDrawIconUIMode(Bool setEnabled)
{
	TheGameLogic->setDrawIconUI(setEnabled);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doSetDynamicLODMode(Bool setEnabled)
{
	TheGameLogic->setShowDynamicLOD(setEnabled);
}
