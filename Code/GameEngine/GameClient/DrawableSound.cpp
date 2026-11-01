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

#include "Common/AudioEventInfo.h"
#include "Common/DynamicAudioEventInfo.h"
#include "Common/AudioSettings.h"
#include "Common/BitFlagsIO.h"
#include "Common/BuildAssistant.h"
#include "Common/ClientUpdateModule.h"
#include "Common/DrawModule.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/GameLOD.h"
#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/ModuleFactory.h"
#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/GameLogic.h"		// for logic frame count
#include "GameLogic/Object.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Module/StealthUpdate.h"
#include "GameLogic/Module/StickyBombUpdate.h"
#include "GameLogic/Module/BattlePlanUpdate.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Weapon.h"
#include "GameClient/Anim2D.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/Drawable.h"
#include "DrawableInternal.h"
#include "GameClient/DrawGroupInfo.h"
#include "GameClient/GameClient.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Image.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/LanguageFilter.h"
#include "GameClient/Shadow.h"
#include "GameClient/GameText.h"

// Drawable: the ambient sound

//-------------------------------------------------------------------------------------------------
/** decodes the current previous damage type and sets the ambient sound set from that. */
//-------------------------------------------------------------------------------------------------
const AudioEventRTS& Drawable::getAmbientSoundByDamage(BodyDamageType dt)
{
	switch (dt)
	{
		case BODY_DAMAGED:
			return *getTemplate()->getSoundAmbientDamaged();
		case BODY_REALLYDAMAGED:
			return *getTemplate()->getSoundAmbientReallyDamaged();
		case BODY_RUBBLE:
			return *getTemplate()->getSoundAmbientRubble();
		case BODY_PRISTINE:
		default:
			return *getTemplate()->getSoundAmbient();
	}
}

/** 
 * If you wish to change some parameters of the default ambient sound, but keep the rest, 
 * this function will give you the default ambient sound's info
 */
const AudioEventInfo * Drawable::getBaseSoundAmbientInfo() const
{
  const AudioEventRTS * baseAmbient = getTemplate()->getSoundAmbient();
  if ( baseAmbient )
    return baseAmbient->getAudioEventInfo();

  return nullptr;
}

/** 
 * Produce a unique-across-entire-level name for this audio event
 */
void Drawable::mangleCustomAudioName( DynamicAudioEventInfo * audioToMangle ) const
{
  AsciiString customizedName;
  customizedName.format( " CUSTOM %d ", static_cast<Int>(getID()) ); // Note space at beginning prevents collision with any names from INI file
  customizedName.concat( audioToMangle->m_audioName );
  audioToMangle->overrideAudioName( customizedName );
}

/** 
 * Force the Drawable to not ever get an ambient sound attached to it (except possibly for RUBBLE)
 */
void Drawable::setCustomSoundAmbientOff()
{
  clearCustomSoundAmbient( false );
  
  m_customSoundAmbientInfo = getNoSoundMarker();  
}

/** 
 * Force the Drawable to use the sound described by customAmbientInfo as its ambient sound.
 * The Drawable expects TheAudio to own the actual info pointer
 */
void Drawable::setCustomSoundAmbientInfo( DynamicAudioEventInfo * customAmbientInfo )
{
  clearCustomSoundAmbient( false );

  // This is mostly to make sure no one delete's the no sound marker, causing it to be 
  // recycled as a new no sound marker
  DEBUG_ASSERTCRASH( customAmbientInfo != getNoSoundMarker(), ("No sound marker passed as custom ambient") );

  // Set name to something different so we don't get confused
 
  m_customSoundAmbientInfo = customAmbientInfo;

  startAmbientSound(); // Note: checks for enabled flag
}

/** 
 * Return to using default ambient sound
 */
void Drawable::clearCustomSoundAmbient( bool restartSound )
{
  if ( m_ambientSound )
  {
    // Make sure sound doesn't keep a reference to the deleted pointer
    m_ambientSound->m_event.setAudioEventInfo( nullptr );
  }

  // Stop using old info
  stopAmbientSound();

  m_customSoundAmbientInfo = nullptr;
  
  if ( restartSound )
  {
    startAmbientSound(); // Note: checks for enabled flag
  }
}

//-------------------------------------------------------------------------------------------------
/** Attach and start playing an ambient sound to this drawable */
//-------------------------------------------------------------------------------------------------
void Drawable::startAmbientSound(BodyDamageType dt, TimeOfDay tod, Bool onlyIfPermanent)
{
	stopAmbientSound();

  Bool trySound = FALSE;

  // Look for customized sound info
  if ( dt != BODY_RUBBLE && m_customSoundAmbientInfo != nullptr )
  {
    if ( m_customSoundAmbientInfo != getNoSoundMarker() )
    {
      if (m_ambientSound == nullptr)
        m_ambientSound = newInstance(DynamicAudioEventRTS);

      // Make sure m_event will accept the custom info
      m_ambientSound->m_event.setEventName( m_customSoundAmbientInfo->m_audioName );
      m_ambientSound->m_event.setAudioEventInfo( m_customSoundAmbientInfo );
      trySound = TRUE;
    }       
  }
  else
  {
    // Didn't get customized sound
    //Get the specific ambient sound for the damage type.
	  const AudioEventRTS& audio = getAmbientSoundByDamage(dt);
	  if( audio.getEventName().isNotEmpty() )
	  {
		  if (m_ambientSound == nullptr)
			  m_ambientSound = newInstance(DynamicAudioEventRTS);

		  (m_ambientSound->m_event) = audio;
		  trySound = TRUE;
	  }
	  else if( dt != BODY_PRISTINE && dt != BODY_RUBBLE )
	  {
		  //If the ambient sound was absent in the case of non-pristine damage types,
		  //try getting the pristine one. Most of our cases actually specify just the
		  //pristine sound and want to use it for all states (except dead/rubble).
		  const AudioEventRTS& pristineAudio = getAmbientSoundByDamage( BODY_PRISTINE );
		  if( pristineAudio.getEventName().isNotEmpty() )
		  {
			  if (m_ambientSound == nullptr)
				  m_ambientSound = newInstance(DynamicAudioEventRTS);
			  (m_ambientSound->m_event) = pristineAudio;
			  trySound = TRUE;
		  }
	  }
  }
  
	
	if( trySound && m_ambientSound )
	{
		const AudioEventInfo *info = m_ambientSound->m_event.getAudioEventInfo();
		if( info )
		{
      if ( !onlyIfPermanent || info->isPermanentSound() )
      {
			  if( BitTest( info->m_type, ST_GLOBAL) || info->m_priority == AP_CRITICAL )
			  {
				  //Play it anyways.
				  m_ambientSound->m_event.setDrawableID(getID());
				  m_ambientSound->m_event.setTimeOfDay(tod);
				  m_ambientSound->m_event.setPlayingHandle(TheAudio->addAudioEvent( &m_ambientSound->m_event ));
			  }
			  else
			  {
				  //Check if it's close enough to try playing (optimization)
				  Coord3D vector = *getPosition();
				  vector.sub( TheAudio->getListenerPosition() );
				  Real distSqr = vector.lengthSqr();
				  if( distSqr < sqr( info->m_maxDistance ) )
				  {
					  m_ambientSound->m_event.setDrawableID(getID());
					  m_ambientSound->m_event.setTimeOfDay(tod);
					  m_ambientSound->m_event.setPlayingHandle(TheAudio->addAudioEvent( &m_ambientSound->m_event ));
				  }
			  }
      }
		}
		else
		{
			DEBUG_CRASH( ("Ambient sound %s missing! Skipping...", m_ambientSound->m_event.getEventName().str() ) );
			deleteInstanceOf(m_ambientSound);
			m_ambientSound = nullptr;
		}
	}
}

//-------------------------------------------------------------------------------------------------
// Attach and start playing an ambient sound to this drawable. Calculates states automatically.
//-------------------------------------------------------------------------------------------------
void Drawable::startAmbientSound( Bool onlyIfPermanent )
{
  // Must go through enableAmbientSound() if sound is disabled
  if ( !m_ambientSoundEnabled || !m_ambientSoundEnabledFromScript )
    return;
  
  stopAmbientSound();
	BodyDamageType bodyCondition = BODY_PRISTINE;
	Object *obj = getObject();
	if( obj )
	{
		bodyCondition = obj->getBodyModule()->getDamageState();
	}
	startAmbientSound( bodyCondition, TheGlobalData->m_timeOfDay, onlyIfPermanent );
}

//-------------------------------------------------------------------------------------------------
/** Stop playing the drawables ambient sound if it has one */
//-------------------------------------------------------------------------------------------------
void	Drawable::stopAmbientSound( void )
{
	if (m_ambientSound)
  {
		TheAudio->removeAudioEvent(m_ambientSound->m_event.getPlayingHandle());
  }
}

//-------------------------------------------------------------------------------------------------
// Enable and disable ambient sound from the game logic
void Drawable::enableAmbientSound( Bool enable )
{
	if( m_ambientSoundEnabled == enable )
	{
		return;
	}

	m_ambientSoundEnabled = enable;
	if( enable )
	{
    if ( m_ambientSoundEnabledFromScript )
    {
      startAmbientSound();
    }
	}
	else
	{
		stopAmbientSound();
	}
}

//-------------------------------------------------------------------------------------------------
// Enable and disable sound because the map designer wants us too
void Drawable::enableAmbientSoundFromScript( Bool enable )
{
  // Note: deliberately skipping if( m_ambientSoundEnabledFromScript == enable ) check here
  // Allow ENABLE_OBJECT_SOUND to trigger one-shot attached sound multiple times

  m_ambientSoundEnabledFromScript = enable;
  if( enable )
  {
    if ( m_ambientSoundEnabled )
    {
      startAmbientSound();
    }
  }
  else
  {
    stopAmbientSound();
  }
}
