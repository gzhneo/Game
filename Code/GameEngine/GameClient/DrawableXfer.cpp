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

// Drawable and TintEnvelope: saving and loading

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void Drawable::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer the drawable modules
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void Drawable::xferDrawableModules( Xfer *xfer )
{

	// version
	const XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	//
	// when using dirty condition flags ... we want to make sure that the modules are updated to
	// the current state of the drawable
	//
#ifdef DIRTY_CONDITION_FLAGS
	if( xfer->getXferMode() == XFER_SAVE )
		getDrawModules();  // will re-evaluate modules that are dirty and update them
#endif

	// xfer number of module types
	UnsignedShort moduleTypes = NUM_DRAWABLE_MODULE_TYPES;
	xfer->xferUnsignedShort( &moduleTypes );

	// xfer each set of modules for each type
	AsciiString moduleIdentifier;
	for( UnsignedShort curModuleType = 0; curModuleType < moduleTypes; ++curModuleType )
	{

		// how many modules are here for this type
		Module **m;
		UnsignedShort moduleCount = 0;
		for( m = m_modules[ curModuleType ]; m && *m; ++m )
			moduleCount++;
		xfer->xferUnsignedShort( &moduleCount );

		// xfer each module data
		if( xfer->getXferMode() == XFER_SAVE )
		{

			// save each module
			for( m = m_modules[ curModuleType ]; m && *m; ++m )
			{

				// write module identifier
				moduleIdentifier = TheNameKeyGenerator->keyToName( (*m)->getModuleTagNameKey() );
				DEBUG_ASSERTCRASH( moduleIdentifier != AsciiString::TheEmptyString,
													 ("Drawable::xferDrawableModules - module name key does not translate to a string!\n") );
				xfer->xferAsciiString( &moduleIdentifier );

				// begin data block
				xfer->beginBlock();

				// xfer data
				xfer->xferSnapshot( *m );

				// end data block
				xfer->endBlock();

			}  // end for, m

		}  // end if, save
		else
		{
			// read each module
			for( UnsignedShort j = 0; j < moduleCount; ++j )
			{

				// read module identifier
				xfer->xferAsciiString( &moduleIdentifier );
				NameKeyType moduleIdentifierKey = TheNameKeyGenerator->nameToKey(moduleIdentifier);

				// find module in the drawable module list
				Module* module = nullptr;
				for( Module **m2 = m_modules[curModuleType]; m2 && *m2; ++m2 )
				{
					if (moduleIdentifierKey == (*m2)->getModuleTagNameKey())
					{

						module = *m2;
						break;  // exit for m

					}  // end if

				}  // end for, m

				// new block of data
				Int dataSize = xfer->beginBlock();

				//
				// if we didn't find the module, it's quite possible that we have removed
				// it from the object definition in a future patch, if that is so, we need to
				// skip the module data in the file
				//
				if( module == nullptr )
				{

					// for testing purposes, this module better be found
					DEBUG_CRASH(( "Drawable::xferDrawableModules - Module '%s' was indicated in file, but not found on Drawable %s %d\n",
												moduleIdentifier.str(), getTemplate()->getName().str(),getID() ));

					// skip this data in the file
					xfer->skip( dataSize );

				}  // end if
				else
				{

					// xfer the data into this module
					xfer->xferSnapshot( module );

				}  // end else

				// end of data block
				xfer->endBlock();

			}  // end for j

		}  // end else, load

	}  // end for curModuleType

}  // end xferDrawableModules

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info;
	* 1: Initial version
	* 2: Moved condition state xfer before module xfer so we can restore anim frame 
	*    during the module xfer (CBD)
	* 4: Added m_ambientSoundEnabled flag
	* 5: save full mtx, not pos+orient.
  * 6: Added m_ambientSoundEnabledFromScript flag
  * 7: Save the customize ambient sound info
	*/
// ------------------------------------------------------------------------------------------------
void Drawable::xfer( Xfer *xfer )
{

	// version
	const XferVersion currentVersion = 7;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	//Wow, because the constructor creates the ambient sound, the xfer can
	//change the ID the sound points to, therefore, we must remove it now
	//and restore it in loadPostProcess().
	if( xfer->getXferMode() == XFER_LOAD && m_ambientSound )
	{
		TheAudio->killAudioEventImmediately( m_ambientSound->m_event.getPlayingHandle() );
		deleteInstanceOf(m_ambientSound);
		m_ambientSound = nullptr;
	}

	// drawable id
	DrawableID id = getID();
	xfer->xferDrawableID( &id );
	setID( id );

	// condition state, note that when we're loading we need to force a replace of these flags
	if( version >= 2 )
	{

		m_conditionState.xfer( xfer );
		if( xfer->getXferMode() == XFER_LOAD	)
			replaceModelConditionFlags( m_conditionState, TRUE );

	}  // end if

	if( version >= 3 )
	{
		if (version >= 5)
		{
			Matrix3D mtx = *getTransformMatrix();
			xfer->xferMatrix3D(&mtx);
			setTransformMatrix(&mtx);
		}
		else
		{
			// position
			Coord3D pos = *getPosition();
			xfer->xferCoord3D( &pos );
			setPosition( &pos );

			// orientation
			Real orientation = getOrientation();
			xfer->xferReal( &orientation );
			setOrientation( orientation );
		}
	}

	// selection flash envelope
	Bool selFlash = (m_selectionFlashEnvelope != nullptr);
	xfer->xferBool( &selFlash );
	if( selFlash )
	{

		// allocate selection flash envelope if we need to
		if( m_selectionFlashEnvelope == nullptr )
			m_selectionFlashEnvelope = newInstance( TintEnvelope );

		// xfer
		xfer->xferSnapshot( m_selectionFlashEnvelope );

	}  // end if

	// color tint envelope
	Bool colFlash = (m_colorTintEnvelope != nullptr);
	xfer->xferBool( &colFlash );
	if( colFlash )
	{

		// allocate envelope if we need to
		if( m_colorTintEnvelope == nullptr )
			m_colorTintEnvelope = newInstance( TintEnvelope );

		// xfer
		xfer->xferSnapshot( m_colorTintEnvelope );

	}  // end if

	// terrain decal type
	TerrainDecalType decal = getTerrainDecalType();
	xfer->xferUser( &decal, sizeof( TerrainDecalType ) );
	if( xfer->getXferMode() == XFER_LOAD )
		setTerrainDecal( decal );

	// explicit opacity
	xfer->xferReal( &m_explicitOpacity );

	// stealth opacity
	xfer->xferReal( &m_stealthOpacity );

	// effective stealth opacity
	xfer->xferReal( &m_effectiveStealthOpacity );

	// decalOpacityFadeTarget
	xfer->xferReal( &m_decalOpacityFadeTarget );

	// decalOpacityFadeRate
	xfer->xferReal( &m_decalOpacityFadeRate );

	// decalOpacityFadeRate
	xfer->xferReal( &m_decalOpacity );

	// object (if present)
	ObjectID objectID = m_object ? m_object->getID() : INVALID_ID;
	xfer->xferObjectID( &objectID );
	// sanity
	if( xfer->getXferMode() == XFER_LOAD )
	{

		if( m_object )
		{

			if( objectID != m_object->getID() )
			{
			
				DEBUG_CRASH(( "Drawable::xfer - Drawable '%s' is attached to wrong object '%s'\n",
											getTemplate()->getName().str(), m_object->getTemplate()->getName().str() ));
				throw SaveCode::SC_INVALID_DATA;

			}  // end if


		}  // end if
		else
		{

			if( objectID != INVALID_ID )
			{
#ifdef DEBUG_CRASHING
				Object *obj = TheGameLogic->findObjectByID( objectID );

				DEBUG_CRASH(( "Drawable::xfer - Drawable '%s' is not attached to an object but should be attached to object '%s' with id '%d'\n",
											getTemplate()->getName().str(),
											obj ? obj->getTemplate()->getName().str() : "Unknown",
											objectID ));
#endif
				throw SaveCode::SC_INVALID_DATA;

			}  // end if

		}  // end else

	}  // end if


	// particle
	// we don't need to worry about this, the particle itself will set it upon loading 

	// selected
	// we won't worry about selection, we'll let TheInGameUI take care of it all

	// status
	xfer->xferUnsignedInt( &m_status );

	// tint status
	xfer->xferUnsignedInt( &m_tintStatus );

	// prev tint status
	xfer->xferUnsignedInt( &m_prevTintStatus );

	// fading mode
	xfer->xferUser( &m_fadeMode, sizeof( FadingMode ) );

	// time elapsed fade
	xfer->xferUnsignedInt( &m_timeElapsedFade );

	// time to fade
	xfer->xferUnsignedInt( &m_timeToFade );

	Bool hasLocoInfo = (m_locoInfo != nullptr);
	xfer->xferBool( &hasLocoInfo );
	if (hasLocoInfo)
	{
		if( xfer->getXferMode() == XFER_LOAD && m_locoInfo == nullptr	)
			m_locoInfo = newInstance(DrawableLocoInfo);

		// pitch
		xfer->xferReal( &m_locoInfo->m_pitch );

		// pitch rate
		xfer->xferReal( &m_locoInfo->m_pitchRate );

		// roll
		xfer->xferReal( &m_locoInfo->m_roll );

		// roll rate
		xfer->xferReal( &m_locoInfo->m_rollRate );

		// yaw
		xfer->xferReal( &m_locoInfo->m_yaw );

		// acceleration pitch
		xfer->xferReal( &m_locoInfo->m_accelerationPitch );

		// acceleration pitch rate
		xfer->xferReal( &m_locoInfo->m_accelerationPitchRate );

		// acceleration roll
		xfer->xferReal( &m_locoInfo->m_accelerationRoll );

		// acceleration roll rate
		xfer->xferReal( &m_locoInfo->m_accelerationRollRate );

		// overlap z vel
		xfer->xferReal( &m_locoInfo->m_overlapZVel );

		// overlap z
		xfer->xferReal( &m_locoInfo->m_overlapZ );

		// wobble
		xfer->xferReal( &m_locoInfo->m_wobble );

		// wheel info
		xfer->xferReal( &m_locoInfo->m_wheelInfo.m_frontLeftHeightOffset );
		xfer->xferReal( &m_locoInfo->m_wheelInfo.m_frontRightHeightOffset );
		xfer->xferReal( &m_locoInfo->m_wheelInfo.m_rearLeftHeightOffset );
		xfer->xferReal( &m_locoInfo->m_wheelInfo.m_rearRightHeightOffset );
		xfer->xferReal( &m_locoInfo->m_wheelInfo.m_wheelAngle );
		xfer->xferInt( &m_locoInfo->m_wheelInfo.m_framesAirborneCounter );
		xfer->xferInt( &m_locoInfo->m_wheelInfo.m_framesAirborne );
	}

	// modules
	xferDrawableModules( xfer );

	// stealth look
	xfer->xferUser( &m_stealthLook, sizeof( StealthLookType ) );


	// flash count
	xfer->xferInt( &m_flashCount );

	// flash color
	xfer->xferColor( &m_flashColor );

	// hidden
	xfer->xferBool( &m_hidden );

	// hidden by stealth
	xfer->xferBool( &m_hiddenByStealth );

	// heat vision opacity
	xfer->xferReal( &m_secondMaterialPassOpacity );

	// instance is identity
	xfer->xferBool( &m_instanceIsIdentity );

	// instance matrix
	xfer->xferUser( &m_instance, sizeof( Matrix3D ) );

	// instance scale
	xfer->xferReal( &m_instanceScale );

	// drawable Info - mostly hold FOW related data.
	xfer->xferObjectID(&m_drawableInfo.m_shroudStatusObjectID);

	// we do not need to save m_drawableInfo
	// m_drawableInfo <--- do nothing with this

	// condition state used to be here so we must keep it here for compatibility
	if( version < 2 )
	{

		// sanity, we don't write old versions we can only read them
		DEBUG_ASSERTCRASH( xfer->getXferMode() == XFER_LOAD, 
											 ("Drawable::xfer - Writing an old format!!!\n") );

		// condition state, note that when we're loading we need to force a replace of these flags
		m_conditionState.xfer( xfer );
		if( xfer->getXferMode() == XFER_LOAD	)
			replaceModelConditionFlags( m_conditionState, TRUE );

	}  // end if

	// expiration date
	xfer->xferUnsignedInt( &m_expirationDate );

	// icon count
	UnsignedByte iconCount = 0;
	if (hasIconInfo())
	{
		for( UnsignedByte i = 0; i < MAX_ICONS; ++i )
			if( getIconInfo()->m_icon[ i ] )
				iconCount++;
	}
	xfer->xferUnsignedByte( &iconCount );

	// icon data
	AsciiString iconIndexName;
	AsciiString iconTemplateName;
	UnsignedInt iconKeepFrame;
	if( xfer->getXferMode() == XFER_SAVE )
	{

		for( UnsignedByte i = 0; i < MAX_ICONS; ++i )
		{

			// skip empty icon slots
			if( !hasIconInfo() || getIconInfo()->m_icon[ i ] == nullptr )
				continue;

			// icon index name
			iconIndexName.set( drawableIconIndexToName( static_cast<DrawableIconType>(i) ) );
			xfer->xferAsciiString( &iconIndexName );

			// keep till frame
			iconKeepFrame = getIconInfo()->m_keepTillFrame[ i ];
			xfer->xferUnsignedInt( &iconKeepFrame );

			// icon template name
			iconTemplateName = getIconInfo()->m_icon[ i ]->getAnimTemplate()->getName();
			xfer->xferAsciiString( &iconTemplateName );

			// icon data
			xfer->xferSnapshot( getIconInfo()->m_icon[ i ] );

		}  // end for, i

	}  // end if, save
	else
	{
		Int i;

		// destroy any icons that might be present right now in favor of what we'll load from the file
		if (hasIconInfo())
			getIconInfo()->clear();

		// read each data segment from the file
		DrawableIconType iconIndex;
		Anim2DTemplate *animTemplate;
		for( i = 0; i < iconCount; ++i )
		{

			// icon index name
			xfer->xferAsciiString( &iconIndexName );
			iconIndex = drawableIconNameToIndex( iconIndexName.str() );

			// keep till frame
			xfer->xferUnsignedInt( &iconKeepFrame );
			getIconInfo()->m_keepTillFrame[ iconIndex ] = iconKeepFrame;

			// icon template name
			xfer->xferAsciiString( &iconTemplateName );
			animTemplate = TheAnim2DCollection->findTemplate( iconTemplateName );
			if( animTemplate == nullptr )
			{

				DEBUG_CRASH(( "Drawable::xfer - Unknown icon template '%s'\n", iconTemplateName.str() ));
				throw SaveCode::SC_INVALID_DATA;

			}  // end if

			// create icon
			getIconInfo()->m_icon[ iconIndex ] = newInstance(Anim2D)( animTemplate, TheAnim2DCollection );

			// icon data
			xfer->xferSnapshot( getIconInfo()->m_icon[ iconIndex ] );

		}  // end for, i

	}  // end else, load

	if( xfer->getXferMode() == XFER_LOAD )
	{
		// On load, we want to set it to none, because stealthlook updates
		// when it changes.  So in the next stealth update, it will be set to 
		// it's correct value, and the drawable updated (hide shadows and such).  jba.
		m_stealthLook = STEALTHLOOK_NONE;
		// Also, need to update the hidden status for all sub-modules.
		if (m_hidden || m_hiddenByStealth) {
			updateHiddenStatus();
		}
	}


	//
	// when saving we should never have dirty modules, but when loading we will force the modules
	// to be dirty just to be sure that they get re-evaluated after the load
	//
#ifdef DIRTY_CONDITION_FLAGS
	if( xfer->getXferMode() == XFER_SAVE )
		DEBUG_ASSERTCRASH( m_isModelDirty == FALSE, ("Drawble::xfer - m_isModelDirty is not FALSE!\n") );
	else
		m_isModelDirty = TRUE;
#endif

  if( xfer->getXferMode() == XFER_LOAD )
  {
    stopAmbientSound(); // Restarted in loadPostProcess()
  }

  if( version >= 4 )
	{
		xfer->xferBool( &m_ambientSoundEnabled );
	}

  if( version >= 6 )
  {
    xfer->xferBool( &m_ambientSoundEnabledFromScript );
  }


  if ( version >= 7 )
  {
    Bool customized = ( m_customSoundAmbientInfo != nullptr );
    xfer->xferBool( &customized );

    if ( customized )
    {
      Bool customizedToSilence = ( m_customSoundAmbientInfo == getNoSoundMarker() );

      xfer->xferBool( &customizedToSilence );
      if ( xfer->getXferMode() == XFER_LOAD )
      {
        if ( customizedToSilence )
        {
          setCustomSoundAmbientOff();
        }
        else
        {
          AsciiString baseInfoName;
          xfer->xferAsciiString( &baseInfoName );

          const AudioEventInfo * baseInfo = TheAudio->findAudioEventInfo( baseInfoName );
          DynamicAudioEventInfo * customizedInfo;
          Bool successfulLoad = true;

          if ( baseInfo == nullptr )
          {
            DEBUG_CRASH( ( "Load failed to load customized ambient sound because sound '%s' no longer exists", baseInfoName.str() ) );
            
            // Keep trying to load if we possibly can... Don't completely ruin save files just because an old sound
            // entry in the INI files was removed or renamed
            customizedInfo = newInstance( DynamicAudioEventInfo );
            successfulLoad = false;
          }
          else
          {
            customizedInfo = newInstance( DynamicAudioEventInfo )( *baseInfo );
          }

          try
          {
            // Get custom name back
            mangleCustomAudioName( customizedInfo );
        
            customizedInfo->xferNoName( xfer );

            if ( successfulLoad )
            {
              TheAudio->addAudioEventInfo( customizedInfo );

              clearCustomSoundAmbient( false );
              m_customSoundAmbientInfo = customizedInfo;
              
              customizedInfo = nullptr; // Belongs to TheAudio now
            }
            else
            {
              deleteInstanceOf(customizedInfo);
              customizedInfo = nullptr;
            }
          }
          catch( ... )
          {
            // since Xfer can throw exceptions -- don't leak memory!
            if ( customizedInfo != nullptr ) 
              deleteInstanceOf(customizedInfo);

            throw; //rethrow
          }
        }
      }
      else // else we are saving...
      {
        if ( !customizedToSilence )
        {
          AsciiString baseInfoName = m_customSoundAmbientInfo->getOriginalName();
          xfer->xferAsciiString( &baseInfoName );
          m_customSoundAmbientInfo->xferNoName( xfer );
        }
      }
    }
  }
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void Drawable::loadPostProcess( void )
{
		// if we have an object, we don't need to save/load the pos, just restore it.
		// if we don't, we'd better save it!
	if (m_object != nullptr)
	{
		setTransformMatrix(m_object->getTransformMatrix());
	}
	
	if( m_ambientSoundEnabled && m_ambientSoundEnabledFromScript )
	{
    // Do we actually want to start the ambient sound up? 
    // If it is a permanent sound, then yes; but if it is
    // a one-shot sound, we don't want to start it even
    // if it's enabled (because the sound might have finished
    // playing long ago). This is what the "onlyIfPermanent"
    // parameter does -- almost like it was added just for
    // this special case! 
    startAmbientSound( true );
	}
	else
	{
		stopAmbientSound();
	}

}  // end loadPostProcess

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void TintEnvelope::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method
	* Version Info;
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void TintEnvelope::xfer( Xfer *xfer )
{

	// version 
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// attack rate
	xfer->xferUser( &m_attackRate, sizeof( Vector3 ) );

	// decay rate
	xfer->xferUser( &m_decayRate, sizeof( Vector3 ) );

	// peak color
	xfer->xferUser( &m_peakColor, sizeof( Vector3 ) );

	// current color
	xfer->xferUser( &m_currentColor, sizeof( Vector3 ) );

	// sustain counter
	xfer->xferUnsignedInt( &m_sustainCounter );

	// affect
	xfer->xferBool( &m_affect );

	// state
	xfer->xferByte( &m_envState );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load Post Process */
// ------------------------------------------------------------------------------------------------
void TintEnvelope::loadPostProcess( void )
{

}  // end loadPostProcess
