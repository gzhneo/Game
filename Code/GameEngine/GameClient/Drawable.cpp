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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// Drawable.cpp ///////////////////////////////////////////////////////////////////////////////////
// "Drawables" - graphical GameClient entities bound to GameLogic objects
// Author: Michael S. Booth, March 2001
///////////////////////////////////////////////////////////////////////////////////////////////////
  
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Engine/ThreadRole.h"

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

//#define KRIS_BRUTAL_HACK_FOR_AIRCRAFT_CARRIER_DEBUGGING 
#ifdef KRIS_BRUTAL_HACK_FOR_AIRCRAFT_CARRIER_DEBUGGING
	#include "GameLogic/Module/ParkingPlaceBehavior.h"
#endif

constexpr auto VERY_TRANSPARENT_MATERIAL_PASS_OPACITY = 0.001f;
constexpr auto MATERIAL_PASS_OPACITY_FADE_SCALAR = 0.8f;

static const char *TheDrawableIconNames[] = 
{
	"DefaultHeal",
	"StructureHeal",
	"VehicleHeal",
	"Demoralized_OBSOLETE",
	"BombTimed",
	"BombRemote",
	"Disabled",
	"BattlePlanIcon_Bombard",
	"BattlePlanIcon_HoldTheLine",
	"BattlePlanIcon_SeekAndDestroy",
  "Emoticon",
	"Enthusiastic",//a red cross? // soon to replace?
	"Subliminal",  //with the gold border! replace?
	"CarBomb",
	nullptr
};


/** 
 * Returns a special DynamicAudioEventInfo which can be used to mark a sound as "no sound".
 * E.g. if m_customSoundAmbientInfo equals the value returned from this function, we
 * know it really means don't allow an ambient sound to be attached. 
 *
 * OK, so it's a bit of a hack, but it saves memory in every Drawable
 */
// File-level so killStaticImages() can free it at shutdown, before the leak report.
static DynamicAudioEventInfo  * s_noSoundMarker = nullptr;

DynamicAudioEventInfo  * getNoSoundMarker()
{
  if ( s_noSoundMarker == nullptr )
  {
    // Initialize first time function is called
    s_noSoundMarker = newInstance( DynamicAudioEventInfo  );
  }

  return s_noSoundMarker;
}



// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
DrawableIconInfo::DrawableIconInfo()
{
	for (int i = 0; i < MAX_ICONS; ++i)
	{
		m_icon[i] = nullptr;
		m_keepTillFrame[i] = 0;
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
DrawableIconInfo::~DrawableIconInfo()
{
	clear();
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void DrawableIconInfo::clear()
{
	for (int i = 0; i < MAX_ICONS; ++i)
	{
		if (m_icon[i])
			deleteInstanceOf(m_icon[i]);
		m_icon[i] = nullptr;
		m_keepTillFrame[i] = 0;
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void DrawableIconInfo::killIcon(DrawableIconType t)
{
	if (m_icon[t])
	{
		deleteInstanceOf(m_icon[t]);
		m_icon[t] = nullptr;
		m_keepTillFrame[t] = 0;
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
DrawableLocoInfo::DrawableLocoInfo()
{
	m_pitch = 0.0f;
	m_pitchRate = 0.0f;
	m_roll = 0.0f;
	m_rollRate = 0.0f;
	m_yaw = 0.0f;
	m_accelerationPitch = 0.0f;
	m_accelerationPitchRate = 0.0f;
	m_accelerationRoll = 0.0f;
	m_accelerationRollRate = 0.0f;
	m_overlapZVel = 0.0f;
	m_overlapZ = 0.0f;
	m_wobble = 1.0f;

	m_wheelInfo.m_frontLeftHeightOffset = 0;
	m_wheelInfo.m_frontRightHeightOffset = 0;
	m_wheelInfo.m_rearLeftHeightOffset = 0;
	m_wheelInfo.m_rearRightHeightOffset = 0;
	m_wheelInfo.m_framesAirborneCounter = 0;
	m_wheelInfo.m_framesAirborne = 0;
	m_wheelInfo.m_wheelAngle = 0;

  m_yawModulator = 0.0f;
  m_pitchModulator = 0.0f;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
DrawableLocoInfo::~DrawableLocoInfo()
{
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
const char *drawableIconIndexToName( DrawableIconType iconIndex )
{

	DEBUG_ASSERTCRASH( iconIndex >= ICON_FIRST && iconIndex < MAX_ICONS,
										 ("drawableIconIndexToName - Illegal index '%d'\n", iconIndex) );

	return TheDrawableIconNames[ iconIndex ];

}  // end drawableIconIndexToName

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
DrawableIconType drawableIconNameToIndex( const char *iconName )
{

	DEBUG_ASSERTCRASH( iconName != nullptr, ("drawableIconNameToIndex - Illegal name\n") );

	for( Int i = ICON_FIRST; i < MAX_ICONS; ++i )
		if( stricmp( TheDrawableIconNames[ i ], iconName ) == 0 )
			return static_cast<DrawableIconType>(i);

	return ICON_INVALID;

}  // end drawableIconNameToIndex

const UnsignedInt DEFAULT_HEAL_ICON_WIDTH		= 32;
const UnsignedInt DEFAULT_HEAL_ICON_HEIGHT	= 32;
const RGBColor SICKLY_GREEN_POISONED_COLOR	= {-1.0f,  1.0f, -1.0f};
const RGBColor DARK_GRAY_DISABLED_COLOR			= {-0.5f, -0.5f, -0.5f};
const RGBColor RED_IRRADIATED_COLOR					= { 1.0f, -1.0f, -1.0f};
const RGBColor SUBDUAL_DAMAGE_COLOR					= {-0.2f, -0.2f,  0.8f};
const RGBColor FRENZY_COLOR									= { 0.2f, -0.2f, -0.2f};
const RGBColor FRENZY_COLOR_INFANTRY				= { 0.0f, -0.7f, -0.7f};
const Int MAX_ENABLED_MODULES								= 16;

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

/*static*/ Bool							Drawable::s_staticImagesInited = false;
/*static*/ const Image*			Drawable::s_veterancyImage[LEVEL_COUNT]	= { nullptr };
/*static*/ const Image*			Drawable::s_fullAmmo = nullptr;
/*static*/ const Image*			Drawable::s_emptyAmmo = nullptr;
/*static*/ const Image*			Drawable::s_fullContainer = nullptr;
/*static*/ const Image*			Drawable::s_emptyContainer = nullptr;
/*static*/ Anim2DTemplate**	Drawable::s_animationTemplates = nullptr;
#ifdef DIRTY_CONDITION_FLAGS
/*static*/ Int							Drawable::s_modelLockCount = 0;
#endif

// ------------------------------------------------------------------------------------------------
/*static*/ void Drawable::initStaticImages()
{
	if (s_staticImagesInited)
		return;

	s_veterancyImage[0] = nullptr;
 	s_veterancyImage[1] = TheMappedImageCollection->findImageByName("SCVeter1");
	s_veterancyImage[2] = TheMappedImageCollection->findImageByName("SCVeter2");
	s_veterancyImage[3] = TheMappedImageCollection->findImageByName("SCVeter3");

	s_fullAmmo	= TheMappedImageCollection->findImageByName("SCPAmmoFull");
	s_emptyAmmo	= TheMappedImageCollection->findImageByName("SCPAmmoEmpty");
	s_fullContainer	= TheMappedImageCollection->findImageByName("SCPPipFull");
	s_emptyContainer	= TheMappedImageCollection->findImageByName("SCPPipEmpty");
	
	s_animationTemplates = NEW Anim2DTemplate* [ MAX_ICONS ];

	s_animationTemplates[ICON_DEFAULT_HEAL]			= TheAnim2DCollection->findTemplate(TheDrawableIconNames[ICON_DEFAULT_HEAL]);
	s_animationTemplates[ICON_STRUCTURE_HEAL]		= TheAnim2DCollection->findTemplate(TheDrawableIconNames[ICON_STRUCTURE_HEAL]);
	s_animationTemplates[ICON_VEHICLE_HEAL]			= TheAnim2DCollection->findTemplate(TheDrawableIconNames[ICON_VEHICLE_HEAL]);
	s_animationTemplates[ICON_DEMORALIZED_OBSOLETE]	= nullptr;
	s_animationTemplates[ICON_BOMB_TIMED]				= TheAnim2DCollection->findTemplate(TheDrawableIconNames[ICON_BOMB_TIMED]);
	s_animationTemplates[ICON_BOMB_REMOTE]			= TheAnim2DCollection->findTemplate(TheDrawableIconNames[ICON_BOMB_REMOTE]);
	s_animationTemplates[ICON_DISABLED]					= TheAnim2DCollection->findTemplate(TheDrawableIconNames[ICON_DISABLED]);
	s_animationTemplates[ICON_BATTLEPLAN_BOMBARD]						= TheAnim2DCollection->findTemplate(TheDrawableIconNames[ICON_BATTLEPLAN_BOMBARD]);
	s_animationTemplates[ICON_BATTLEPLAN_HOLDTHELINE]				= TheAnim2DCollection->findTemplate(TheDrawableIconNames[ICON_BATTLEPLAN_HOLDTHELINE]);
	s_animationTemplates[ICON_BATTLEPLAN_SEARCHANDDESTROY]	= TheAnim2DCollection->findTemplate(TheDrawableIconNames[ICON_BATTLEPLAN_SEARCHANDDESTROY]);
	s_animationTemplates[ICON_EMOTICON]					= nullptr; //Emoticons can be anything, so we'll need to handle it dynamically.
	s_animationTemplates[ICON_ENTHUSIASTIC]			= TheAnim2DCollection->findTemplate(TheDrawableIconNames[ICON_ENTHUSIASTIC]);
	s_animationTemplates[ICON_ENTHUSIASTIC_SUBLIMINAL]			= TheAnim2DCollection->findTemplate(TheDrawableIconNames[ICON_ENTHUSIASTIC_SUBLIMINAL]);
	s_animationTemplates[ICON_CARBOMB]			= TheAnim2DCollection->findTemplate(TheDrawableIconNames[ICON_CARBOMB]);

	s_staticImagesInited = true;

}

//-------------------------------------------------------------------------------------------------
/*static*/ void Drawable::killStaticImages()
{
	if( s_animationTemplates )
	{
		delete[] s_animationTemplates;
		s_animationTemplates = nullptr;
	}
	// all drawables are gone here, none points to the marker any more
	if( s_noSoundMarker )
	{
		deleteInstanceOf(s_noSoundMarker);
		s_noSoundMarker = nullptr;
	}
}

//-------------------------------------------------------------------------------------------------
void Drawable::saturateRGB(RGBColor& color, Real factor)
{
	color.red *= factor;
	color.green *= factor;
	color.blue *= factor;

	Real halfFactor = factor * 0.5f;

	color.red -= halfFactor;
	color.green -= halfFactor;
	color.blue -= halfFactor;

}




//-------------------------------------------------------------------------------------------------
/** Drawables are lightweight, graphical entities which live on the GameClient,
 * and are usually bound to GameLogic objects.  In other words, they are the
 * graphical side of a logical object, whereas GameLogic objects encapsulate
 * behaviors and physics.  */
//-------------------------------------------------------------------------------------------------
Drawable::Drawable( const ThingTemplate *thingTemplate, DrawableStatus statusBits ) 
				: Thing( thingTemplate )
{

	// assign status bits before anything else can be done
	m_status = statusBits;
	
	// Added By Sadullah Nader
	// Initialization missing and needed
	m_nextDrawable = nullptr;
	m_prevDrawable = nullptr;
	//

  m_customSoundAmbientInfo = nullptr;

	// register drawable with the GameClient ... do this first before we start doing anything
	// complex that uses any of the drawable data so that we have and ID!!  It's ok to initialize
	// members of the drawable before this registration happens
	//
	TheGameClient->registerDrawable( this );

	Int i;

	// Added By Sadullah Nader
	// Initialization missing and needed
	m_flashColor = 0;
	m_selected = '\0';
	//

	m_expirationDate = 0;  // 0 == never expires

	m_lastConstructDisplayed = -1.0f;
	
	//Added By Sadullah Nader
	//Fix for the building percent
	m_constructDisplayString = TheDisplayStringManager->newDisplayString();
	m_constructDisplayString->setFont(TheFontLibrary->getFont(TheInGameUI->getDrawableCaptionFontName(),
																TheGlobalLanguageData->adjustFontSize(TheInGameUI->getDrawableCaptionPointSize()),
																TheInGameUI->isDrawableCaptionBold() ));

	m_ambientSound = nullptr;
  m_ambientSoundEnabled = true;
  m_ambientSoundEnabledFromScript = true;
 
	m_decalOpacityFadeTarget = 0;
	m_decalOpacityFadeRate = 0;
	m_decalOpacity = 0;

	m_explicitOpacity = 1.0f;
	m_stealthOpacity = 1.0f;
	m_effectiveStealthOpacity = 1.0f;
	m_terrainDecalType = TERRAIN_DECAL_NONE;

  m_fadeMode = FADING_NONE;
	m_timeElapsedFade = 0;
	m_timeToFade = 0;

	m_shroudClearFrame = 0;

	for (i = 0; i < NUM_DRAWABLE_MODULE_TYPES; ++i)
		m_modules[i] = nullptr;

	m_stealthLook = STEALTHLOOK_NONE;

	m_flashCount = 0;

	m_locoInfo = nullptr;

	// sanity
	if( TheGameClient == nullptr || thingTemplate == nullptr )
	{

		assert( 0 );
		return;

	}  // end if

	m_instance.Make_Identity();
	m_instanceIsIdentity = true;

	//Real scaleFuzziness = thingTemplate->getInstanceScaleFuzziness();
	//Real fuzzyScale = ( 1.0f + GameClientRandomValueReal( -scaleFuzziness, scaleFuzziness ));
	m_instanceScale = thingTemplate->getAssetScale();// * fuzzyScale;

	// initially not bound to an object
	m_object = nullptr;
	m_particle = nullptr;

	// tintStatusTracking
	m_tintStatus = 0;
	m_prevTintStatus = 0;

#ifdef DIRTY_CONDITION_FLAGS
	m_isModelDirty = true;
#endif

	m_hidden = false;
	m_interpChangeSerial = 0;
	m_interpIntervalTicks = 1;
	m_interpPrevTransform.Make_Identity();
	m_hiddenByStealth = false;
	m_secondMaterialPassOpacity = 0.0f;
	m_drawableFullyObscuredByShroud = false;

  m_receivesDynamicLights = TRUE; // a good default... overridden by one of my draw modules if at all
	
	// allocate any modules we need to, we should keep
	// this at or near the end of the drawable construction so that we have
	// all the valid data about the thing when we create the module
	//

	//Filter out drawable modules which have been disabled because of game LOD.
	Int modIdx;
	Module** m;

	const ModuleInfo& drawMI = thingTemplate->getDrawModuleInfo();
	m_modules[MODULETYPE_DRAW - FIRST_DRAWABLE_MODULE_TYPE] = MSGNEW("ModulePtrs") Module*[drawMI.getCount()+1];	// pool[]ify
	m = m_modules[MODULETYPE_DRAW - FIRST_DRAWABLE_MODULE_TYPE];
	for (modIdx = 0; modIdx < drawMI.getCount(); ++modIdx)
	{
		const ModuleData* newModData = drawMI.getNthData(modIdx);
		if (TheGlobalData->m_useDrawModuleLOD && 
				newModData->getMinimumRequiredGameLOD() > TheGameLODManager->getStaticLODLevel())
			continue;
		*m++ = TheModuleFactory->newModule(this, drawMI.getNthName(modIdx), newModData, MODULETYPE_DRAW);
	}
	*m = nullptr;

	const ModuleInfo& cuMI = thingTemplate->getClientUpdateModuleInfo();
	if (cuMI.getCount())
	{
		// since most things don't have CU modules, we allow this to be null!
		m_modules[MODULETYPE_CLIENT_UPDATE - FIRST_DRAWABLE_MODULE_TYPE] = MSGNEW("ModulePtrs") Module*[cuMI.getCount()+1];	// pool[]ify
		m = m_modules[MODULETYPE_CLIENT_UPDATE - FIRST_DRAWABLE_MODULE_TYPE];
		for (modIdx = 0; modIdx < cuMI.getCount(); ++modIdx)
		{
			const ModuleData* newModData = cuMI.getNthData(modIdx);

	/// @todo srj -- this is evil, we shouldn't look at the module name directly!
			if (thingTemplate->isKindOf(KINDOF_SHRUBBERY) && 
					!TheGlobalData->m_useTreeSway &&
					cuMI.getNthName(modIdx).compareNoCase("SwayClientUpdate") == 0)
				continue;

			*m++ = TheModuleFactory->newModule(this, cuMI.getNthName(modIdx), newModData, MODULETYPE_CLIENT_UPDATE);
		}
		*m = nullptr;
	}

	/// allow for inter-Module resolution
	for (i = 0; i < NUM_DRAWABLE_MODULE_TYPES; ++i)
	{
		for (Module** m3 = m_modules[i]; m3 && *m3; ++m3)
			(*m3)->onObjectCreated();
	}
	
	m_groupNumber = nullptr;
	m_captionDisplayString = nullptr;
	m_drawableInfo.m_drawable = this;
	m_drawableInfo.m_ghostObject = nullptr;

	m_iconInfo = nullptr;								// lazily allocate!
	m_selectionFlashEnvelope = nullptr;	// lazily allocate!
	m_colorTintEnvelope = nullptr;				// lazily allocate!

	initStaticImages(); 

  // If we are inside GameLogic::startNewGame(), then starting the ambient sound 
  // will be taken care of by Drawable::onLevelStart(). It's important that we 
  // wait until Drawable::onLevelStart(), because we may have a customized ambient
  // sound which we'll only learn about after the constructor is finished. The
  // map maker may also have disabled the ambient sound; again, we only learn that
  // after the constructor is done.
  // By the same token, when loading from save, we may learn that the ambient sound 
  // is enabled or disabled in xfer(), and we may learn we have a customized sound there,
  // so don't start the ambient sound yet. 
  // This is all really traceable to the fact that stopAmbientSound() won't stop a sound which 
  // is in the middle of playing; it will only stop it when the current wavefile is finished.
  // So we have to be very careful of called startAmbientSound() because we can't "take it back" later.
  if ( TheGameLogic != nullptr && !TheGameLogic->isLoadingMap() && TheGameState != nullptr && !TheGameState->isInLoadGame() )
  {
  	startAmbientSound();
  }

}  // end Drawable

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Drawable::~Drawable()
{
	Int i;

	if( m_constructDisplayString )
		TheDisplayStringManager->freeDisplayString( m_constructDisplayString );
	m_constructDisplayString = nullptr;

	if ( m_captionDisplayString )
		TheDisplayStringManager->freeDisplayString( m_captionDisplayString );
	m_captionDisplayString = nullptr;

	m_groupNumber = nullptr;

	// delete any modules callbacks
	for (i = 0; i < NUM_DRAWABLE_MODULE_TYPES; ++i)
	{
		for (Module** m = m_modules[i]; m && *m; ++m)
		{
			deleteInstanceOf((*m));
			*m = nullptr;	// in case other modules call findModule from their dtor!
		}
		delete [] m_modules[i]; 
		m_modules[i] = nullptr;
	}

	stopAmbientSound();
	if (m_ambientSound)
	{
		deleteInstanceOf(m_ambientSound);
		m_ambientSound = nullptr;
	}

  clearCustomSoundAmbient( false );

	/// @todo this is nasty, we need a real general effects system
	// remove any entries that might be present from the ray effect system
	TheGameClient->removeFromRayEffects( this );

	// reset object to NULL so we never mistaken grab "dead" objects
	m_object = nullptr;
	m_particle = nullptr;

	// delete any icons present
	if (m_iconInfo)
		deleteInstanceOf(m_iconInfo);

	if (m_selectionFlashEnvelope)
		deleteInstanceOf(m_selectionFlashEnvelope);

	if (m_colorTintEnvelope)
		deleteInstanceOf(m_colorTintEnvelope);

	if (m_locoInfo)
	{
		deleteInstanceOf(m_locoInfo);
		m_locoInfo = nullptr;
	}
}

//-------------------------------------------------------------------------------------------------
/** Run from GameClient::destroyDrawable */
//-------------------------------------------------------------------------------------------------
void Drawable::onDestroy( void )
{

	//
	// run the onDelete on all modules present so they each have an opportunity to cleanup
	// anything they need to ... including talking to any other modules
	//
	for( Int i = 0; i < NUM_DRAWABLE_MODULE_TYPES; i++ )
	{

		for( Module** m = m_modules[ i ]; m && *m; ++m )
			(*m)->onDelete();

	}  // end for i

}  // end onDestroy

//-------------------------------------------------------------------------------------------------
Bool Drawable::isVisible()
{
	for (DrawModule** dm = getDrawModules(); *dm; ++dm)
	{
		if ((*dm)->isVisible())
		{
			return TRUE;
		}
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
Bool Drawable::getShouldAnimate( Bool considerPower ) const
{
	const Object *obj = getObject();

	if (obj)
	{
		if (considerPower && obj->testScriptStatusBit(OBJECT_STATUS_SCRIPT_UNPOWERED))
			return FALSE;

		if (obj->isDisabled())
		{
			if( 
         ! obj->isKindOf( KINDOF_PRODUCED_AT_HELIPAD )  && 
        // mal sez: helicopters just look goofy if they stop animating, so keep animating them, anyway

        (  obj->isDisabledByType( DISABLED_HACKED ) 
				|| obj->isDisabledByType( DISABLED_PARALYZED ) 
				|| obj->isDisabledByType( DISABLED_EMP ) 
				|| obj->isDisabledByType( DISABLED_SUBDUED ) 
				// srj sez: unmanned things also should not animate. (eg, gattling tanks,
				// which have a slight barrel animation even when at rest). if this causes
				// a problem, we will need to fix gattling tanks in another way.
				|| obj->isDisabledByType( DISABLED_UNMANNED ) )
        
				)
				return FALSE;

			if (considerPower && obj->isDisabledByType(DISABLED_UNDERPOWERED))
			{
				// We only pause animations if this draw module says so
				// By checking for the others first, we prevent underpower from allowing a True on an addition disable type
				return FALSE;
			}
		}
	}

	return TRUE;
}

//-------------------------------------------------------------------------------------------------
// this method must ONLY be called from the client, NEVER From the logic, not even indirectly.
Bool Drawable::clientOnly_getFirstRenderObjInfo(Coord3D* pos, Real* boundingSphereRadius, Matrix3D* transform)
{
	DrawModule** dm = getDrawModules();
	const ObjectDrawInterface* di = (dm && *dm) ? (*dm)->getObjectDrawInterface() : nullptr;
	if (di)
	{
		return di->clientOnly_getRenderObjInfo(pos, boundingSphereRadius, transform);
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
Bool Drawable::getProjectileLaunchOffset(WeaponSlotType wslot, Int specificBarrelToUse, Matrix3D* launchPos, WhichTurretType tur, Coord3D* turretRotPos, Coord3D* turretPitchPos) const
{
	for (const DrawModule** dm = getDrawModules(); *dm; ++dm)
	{
		const ObjectDrawInterface* di = (*dm)->getObjectDrawInterface();
		if (di && di->getProjectileLaunchOffset(m_conditionState, wslot, specificBarrelToUse, launchPos, tur, turretRotPos, turretPitchPos))
			return true;
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
void Drawable::setAnimationLoopDuration(UnsignedInt numFrames)
{
	for (DrawModule** dm = getDrawModules(); *dm; ++dm)
	{
		ObjectDrawInterface* di = (*dm)->getObjectDrawInterface();
		if (di)
			di->setAnimationLoopDuration(numFrames);
	}
}

//-------------------------------------------------------------------------------------------------
void Drawable::setAnimationCompletionTime(UnsignedInt numFrames)
{
	for (DrawModule** dm = getDrawModules(); *dm; ++dm)
	{
		ObjectDrawInterface* di = (*dm)->getObjectDrawInterface();
		if (di)
			di->setAnimationCompletionTime(numFrames);
	}
}

//Kris: Manually set a drawable's current animation to specific frame.
//-------------------------------------------------------------------------------------------------
void Drawable::setAnimationFrame( int frame )
{
	for (DrawModule** dm = getDrawModules(); *dm; ++dm)
	{
		ObjectDrawInterface* di = (*dm)->getObjectDrawInterface();
		if (di)
			di->setAnimationFrame( frame );
	}
}

//-------------------------------------------------------------------------------------------------
void Drawable::updateSubObjects()
{
	for (DrawModule** dm = getDrawModules(); *dm; ++dm)
	{
		ObjectDrawInterface* di = (*dm)->getObjectDrawInterface();
		if (di)
			di->updateSubObjects();
	}
}

//-------------------------------------------------------------------------------------------------
void Drawable::showSubObject( const AsciiString& name, Bool show )
{
	for (DrawModule** dm = getDrawModules(); *dm; ++dm)
	{
		ObjectDrawInterface* di = (*dm)->getObjectDrawInterface();
		if (di)
		{
			di->showSubObject( name, show );
		}
	}
}

#ifdef ALLOW_ANIM_INQUIRIES
// srj sez: not sure if this is a good idea, for net sync reasons...
//-------------------------------------------------------------------------------------------------
/**
	This call asks, "In the current animation (if any) how far along are you, from 0.0f to 1.0f". 
*/
Real Drawable::getAnimationScrubScalar( void ) const // lorenzen
{
	for (const DrawModule** dm = getDrawModules(); *dm; ++dm)
	{
		const ObjectDrawInterface* di = (*dm)->getObjectDrawInterface();
		if (di)
		{
			return di->getAnimationScrubScalar();
		}
	}
	
	return 0.0f;

}
#endif

//-------------------------------------------------------------------------------------------------
Int Drawable::getPristineBonePositions(const char* boneNamePrefix, Int startIndex, Coord3D* positions, Matrix3D* transforms, Int maxBones) const
{
	Int count = 0;
	for (const DrawModule** dm = getDrawModules(); *dm; ++dm)
	{
		if (maxBones <= 0)
			break;

		const ObjectDrawInterface* di = (*dm)->getObjectDrawInterface();
		if (di)
		{
			Int subcount = 
				di->getPristineBonePositionsForConditionState(m_conditionState, boneNamePrefix, startIndex, positions, transforms, maxBones);

			if (subcount > 0)
			{
				count += subcount;
				if (positions)
					positions += subcount;
				if (transforms)
					transforms += subcount;
				maxBones -= subcount;
			}
		}
	}
	return count;
}

//-------------------------------------------------------------------------------------------------
Int Drawable::getCurrentClientBonePositions(const char* boneNamePrefix, Int startIndex, Coord3D* positions, Matrix3D* transforms, Int maxBones) const
{
	Int count = 0;
	for (const DrawModule** dm = getDrawModules(); *dm; ++dm)
	{
		if (maxBones <= 0)
			break;

		const ObjectDrawInterface* di = (*dm)->getObjectDrawInterface();
		if (di)
		{
			Int subcount = 
				di->getCurrentBonePositions(boneNamePrefix, startIndex, positions, transforms, maxBones);

			if (subcount > 0)
			{
				count += subcount;
				if (positions)
					positions += subcount;
				if (transforms)
					transforms += subcount;
				maxBones -= subcount;
			}
		}
	}
	return count;
}

//-------------------------------------------------------------------------------------------------
Bool Drawable::getCurrentWorldspaceClientBonePositions(const char* boneName, Matrix3D& transform) const
{
	for (const DrawModule** dm = getDrawModules(); *dm; ++dm)
	{
		const ObjectDrawInterface* di = (*dm)->getObjectDrawInterface();
		if (di && di->getCurrentWorldspaceClientBonePositions(boneName, transform))
			return true;
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
/** Attach to a particle system */
// Only the Generals particle system links particles to drawables.
//-------------------------------------------------------------------------------------------------
void Drawable::attachToParticleSystem( Particle *p )
{
	m_particle = p;
}

//-------------------------------------------------------------------------------------------------
/** Detach from a particle system, if attached */
//-------------------------------------------------------------------------------------------------
void Drawable::detachFromParticleSystem( void )
{
	if (m_particle)
	{
		m_particle->detachDrawable();
		m_particle = nullptr;
	}
}

//-------------------------------------------------------------------------------------------------
void Drawable::setTerrainDecal(TerrainDecalType type)
{
	if (m_terrainDecalType == type)
		return;

	m_terrainDecalType=type;

	DrawModule** dm = getDrawModules();

	//Only the first draw module gets a decal to prevent stacking.
	//Should be okay as long as we keep the primary object in the
	//first module.
	if (*dm)
		(*dm)->setTerrainDecal(type);

}

//-------------------------------------------------------------------------------------------------
void Drawable::setTerrainDecalSize(Real x, Real y)
{
	DrawModule** dm = getDrawModules();

	if (*dm)
		(*dm)->setTerrainDecalSize(x,y);
}

//-------------------------------------------------------------------------------------------------
void Drawable::setTerrainDecalFadeTarget(Real target, Real rate)
{
	if (m_decalOpacityFadeTarget != target)
	{
		m_decalOpacityFadeTarget = target;
		m_decalOpacityFadeRate = rate;
	}
	//else
	//	m_decalOpacityFadeRate = 0;

}

//-------------------------------------------------------------------------------------------------
void Drawable::setShadowsEnabled(Bool enable)
{
	// set status bit
	if( enable )
		setDrawableStatus( DRAWABLE_STATUS_SHADOWS );
	else
		clearDrawableStatus( DRAWABLE_STATUS_SHADOWS );

	for (DrawModule** dm = getDrawModules(); *dm; ++dm)
	{
		(*dm)->setShadowsEnabled(enable);
	}
}

//-------------------------------------------------------------------------------------------------
/**frees all shadow resources used by this module - used by Options screen.*/
void Drawable::releaseShadows(void)
{
	for (DrawModule** dm = getDrawModules(); *dm; ++dm)
	{
		(*dm)->releaseShadows();
	}
}

//-------------------------------------------------------------------------------------------------
/**create shadow resources if not already present. Used by Options screen.*/
void Drawable::allocateShadows(void)
{
	for (DrawModule** dm = getDrawModules(); *dm; ++dm)
	{
		(*dm)->allocateShadows();
	}
}

//-------------------------------------------------------------------------------------------------
void Drawable::setFullyObscuredByShroud(Bool fullyObscured)
{
	if (m_drawableFullyObscuredByShroud != fullyObscured)
	{
		for (DrawModule** dm = getDrawModules(); *dm; ++dm)
		{
			(*dm)->setFullyObscuredByShroud(fullyObscured);
		}
		m_drawableFullyObscuredByShroud = fullyObscured;
	}
}

//-------------------------------------------------------------------------------------------------
/** Set drawable's "selected" status, if not already set.  Also update running
 * total count of selected drawables. */
//-------------------------------------------------------------------------------------------------
void Drawable::friend_setSelected( void ) 
{ 
	if(isSelected() == false)
	{
		m_selected = TRUE;
		onSelected();
	}

}			

//-------------------------------------------------------------------------------------------------
/** Clear drawable's "selected" status, if not already clear.  Also update running
 * total count of selected drawables. */
//-------------------------------------------------------------------------------------------------
void Drawable::friend_clearSelected( void ) 
{
	if(isSelected()) 
	{
		m_selected = FALSE;
		onUnselected();
	}
}

// ------------------------------------------------------------------------------------------------
/** Flash the drawable with the color */
// ------------------------------------------------------------------------------------------------
void Drawable::colorFlash( const RGBColor* color, UnsignedInt decayFrames, UnsignedInt attackFrames, UnsignedInt sustainAtPeak )
{
	if (m_colorTintEnvelope == nullptr)
		m_colorTintEnvelope = newInstance(TintEnvelope);

	if( color )
	{
		m_colorTintEnvelope->play( color, attackFrames, decayFrames, sustainAtPeak);
	}
	else
	{
		RGBColor white;
		white.setFromInt(0xffffffff);
		m_colorTintEnvelope->play( &white );
	}

	// make sure the tint color is unlocked so we "fade back down" to normal
	clearDrawableStatus( DRAWABLE_STATUS_TINT_COLOR_LOCKED );
} 

// ------------------------------------------------------------------------------------------------
/** Tint a drawable a specified color */
// ------------------------------------------------------------------------------------------------
void Drawable::colorTint( const RGBColor* color )
{
	if( color )
	{
		// set the color via color flash
		colorFlash( color, 0, 0, TRUE );

		// lock the tint color so the flash never "fades back down"
		setDrawableStatus( DRAWABLE_STATUS_TINT_COLOR_LOCKED );

	}
	else
	{
		if (m_colorTintEnvelope == nullptr)
			m_colorTintEnvelope = newInstance(TintEnvelope);

		// remove the tint applied to the object
		m_colorTintEnvelope->rest();

		// set the tint as unlocked so we can flash and stuff again
		clearDrawableStatus( DRAWABLE_STATUS_TINT_COLOR_LOCKED );

	}

}

//-------------------------------------------------------------------------------------------------
/** Gathering point for all things besides actual selection that must happen on selection */
//-------------------------------------------------------------------------------------------------
void Drawable::onSelected()
{

	flashAsSelected();//much simpler
	
	Object* obj = getObject();
	if ( obj )
	{
		ContainModuleInterface* contain = obj->getContain();
		if ( contain )
		{
			contain->clientVisibleContainedFlashAsSelected();
		}
	}

}  // end onSelected

//-------------------------------------------------------------------------------------------------
/** Gathering point for all things besides actual selection that must happen on deselection */
//-------------------------------------------------------------------------------------------------
void Drawable::onUnselected()
{
	// nothing
}

//-------------------------------------------------------------------------------------------------
/** get FX color value to add to ALL LIGHTS when drawing */
//-------------------------------------------------------------------------------------------------
const Vector3 * Drawable::getTintColor( void ) const
{
	if ( m_colorTintEnvelope )
	{
		if (m_colorTintEnvelope->isEffective())
		{
			return m_colorTintEnvelope->getColor();
		}
	}

	return nullptr;

}
//-------------------------------------------------------------------------------------------------
/** get SELECTION color value to add to ALL LIGHTS when drawing */
//-------------------------------------------------------------------------------------------------
const Vector3 * Drawable::getSelectionColor( void )	const
{
	if (m_selectionFlashEnvelope)
	{
		if (m_selectionFlashEnvelope->isEffective())
		{
			return m_selectionFlashEnvelope->getColor();
		}
	}

	return nullptr;

}

		
//-------------------------------------------------------------------------------------------------
/** fades the object out gradually...how gradually is determined by number of frames */
//-------------------------------------------------------------------------------------------------
void Drawable::fadeOut( UnsignedInt frames )		///< cloak object
{
	setDrawableOpacity(1.0);
	m_fadeMode = FADING_OUT;
	m_timeToFade = frames;
	m_timeElapsedFade = 0;
}

//-------------------------------------------------------------------------------------------------
/** fades the object in gradually...how gradually is determined by number of frames */
//-------------------------------------------------------------------------------------------------
void Drawable::fadeIn( UnsignedInt frames )		///< decloak object
{
	setDrawableOpacity(0.0);
	m_fadeMode = FADING_IN;
	m_timeToFade = frames;
	m_timeElapsedFade = 0;
}


//-------------------------------------------------------------------------------------------------
const Real Drawable::getScale (void) const 
{ 
	return m_instanceScale; 
//	return getTemplate()->getAssetScale(); 
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void Drawable::reactToBodyDamageStateChange(BodyDamageType newState)
{
	static const ModelConditionFlagType TheDamageMap[BODYDAMAGETYPE_COUNT] = 
	{
		MODELCONDITION_INVALID,
		MODELCONDITION_DAMAGED,
		MODELCONDITION_REALLY_DAMAGED,
		MODELCONDITION_RUBBLE,
	};

	ModelConditionFlags newDamage;
	if (TheDamageMap[newState] != MODELCONDITION_INVALID)
		newDamage.set(TheDamageMap[newState]);

	clearAndSetModelConditionFlags(
		MAKE_MODELCONDITION_MASK3(MODELCONDITION_DAMAGED, MODELCONDITION_REALLY_DAMAGED, MODELCONDITION_RUBBLE), 
		newDamage);

  // When loading map, ambient sound starting is handled by onLevelStart(), so that we can
  // correctly react to customizations
  if ( !TheGameLogic->isLoadingMap() )
 	  startAmbientSound(newState, TheGlobalData->m_timeOfDay);
}

//-------------------------------------------------------------------------------------------------
void Drawable::setEffectiveOpacity( Real pulseFactor, Real explicitOpacity /* = -1.0f */)
{
	if( explicitOpacity != -1.0f )
	{
		m_stealthOpacity = MIN( 1.0f, MAX( 0.0f, explicitOpacity ) );
	}

	Real pf = MIN(1.0f, MAX(0.0f, pulseFactor));

	Real pulseMargin = (1.0f - m_stealthOpacity);
	Real pulseAmount = pulseMargin * pf;

	m_effectiveStealthOpacity = m_stealthOpacity + pulseAmount; 
}		///< get alpha/opacity value used to override defaults when drawing.



//-------------------------------------------------------------------------------------------------
void Drawable::imitateStealthLook( Drawable& otherDraw )
{
  m_stealthOpacity = otherDraw.friend_getStealthOpacity();
  m_explicitOpacity = otherDraw.friend_getExplicitOpacity();
  m_effectiveStealthOpacity = otherDraw.friend_getEffectiveStealthOpacity();
  m_hidden = otherDraw.isDrawableEffectivelyHidden();
  m_hiddenByStealth = otherDraw.isDrawableEffectivelyHidden();
  m_stealthLook = otherDraw.getStealthLook();
  m_secondMaterialPassOpacity = otherDraw.getSecondMaterialPassOpacity();

}









//-------------------------------------------------------------------------------------------------
/** update is called once per frame */
//-------------------------------------------------------------------------------------------------
//DECLARE_PERF_TIMER(updateDrawable)
void Drawable::updateDrawable( void )
{
	//USE_PERF_TIMER(updateDrawable)

	UnsignedInt now = TheGameLogic->getFrame();
	Object *obj = getObject();

	{
		for (ClientUpdateModule** cu = getClientUpdateModules(); cu && *cu; ++cu)
		{
			(*cu)->clientUpdate();
		}
	}

	{

		// handle fading in or out
		if (m_fadeMode != FADING_NONE)
		{
			Real numer = static_cast<Real>((m_fadeMode == FADING_IN) ? (m_timeElapsedFade) : (m_timeToFade-m_timeElapsedFade));

			setDrawableOpacity(numer/static_cast<Real>(m_timeToFade));
			++m_timeElapsedFade;
			
			if (m_timeElapsedFade > m_timeToFade)
				m_fadeMode = FADING_NONE;
		}
	}


	if ( getTerrainDecalType() != TERRAIN_DECAL_NONE )
	{
		DrawModule** dm = getDrawModules();

		if (*dm)
		{
			if (m_decalOpacityFadeRate != 0)
			{
				//LERP
				(*dm)->setTerrainDecalOpacity(m_decalOpacity);
				m_decalOpacity += m_decalOpacityFadeRate;
			}
			//---------------

			if (m_decalOpacityFadeRate < 0 && m_decalOpacity <= 0 )
			{
				m_decalOpacityFadeRate = 0.0f;
				m_decalOpacity = 0.0f;
				this->setTerrainDecal(TERRAIN_DECAL_NONE);
			}
			else if (m_decalOpacityFadeRate > 0 && m_decalOpacity >= 1.0f)
			{
				m_decalOpacity = 1.0f;
				m_decalOpacityFadeRate = 0.0f;
				(*dm)->setTerrainDecalOpacity(m_decalOpacity);
			}

		}//end if (*dm)
	}
	else
		m_decalOpacity = 0;


	{

		if (m_expirationDate != 0 && now >= m_expirationDate)
		{
			DEBUG_ASSERTCRASH(obj == nullptr, ("Drawables with Objects should not have expiration dates!"));
			TheGameClient->destroyDrawable(this);
			return;
		}
	}

	{

		if (m_flashCount > 0  && (TheGameClient->getFrame() % DRAWABLE_FRAMES_PER_FLASH) == 0)
		{
			RGBColor tmp;
			tmp.setFromInt(m_flashColor);
			colorFlash(&tmp);
			m_flashCount--;
		}
	}

	//Lets figure out whether we should be changing colors right about now
	// we'll use an ifelseif ladder since we are scanning bits
	if( m_prevTintStatus != m_tintStatus )// edge test 
	{
		if ( testTintStatus( TINT_STATUS_DISABLED ) )
		{
			if (m_colorTintEnvelope == nullptr)
				m_colorTintEnvelope = newInstance(TintEnvelope);
			m_colorTintEnvelope->play( &DARK_GRAY_DISABLED_COLOR, 30, 30, SUSTAIN_INDEFINITELY);
		}
		else if( testTintStatus(TINT_STATUS_GAINING_SUBDUAL_DAMAGE) )
		{
			// Disabled has precendence, so it goes first
			if (m_colorTintEnvelope == nullptr)
				m_colorTintEnvelope = newInstance(TintEnvelope);
			m_colorTintEnvelope->play( &SUBDUAL_DAMAGE_COLOR, 150, 150, SUSTAIN_INDEFINITELY);
		}
		else if( testTintStatus(TINT_STATUS_FRENZY) )
		{
			// Disabled has precendence, so it goes first
			if (m_colorTintEnvelope == nullptr)
				m_colorTintEnvelope = newInstance(TintEnvelope);

      m_colorTintEnvelope->play( isKindOf( KINDOF_INFANTRY) ? &FRENZY_COLOR_INFANTRY:&FRENZY_COLOR, 30, 30, SUSTAIN_INDEFINITELY);
		
    }
//		else if ( testTintStatus( TINT_STATUS_POISONED) )
//		{
//			if (m_colorTintEnvelope == NULL)
//				m_colorTintEnvelope = newInstance(TintEnvelope);
//			m_colorTintEnvelope->play( &SICKLY_GREEN_POISONED_COLOR, 30, 30, SUSTAIN_INDEFINITELY);
//		}
//		else if ( testTintStatus( TINT_STATUS_IRRADIATED) )
//		{
//			if (m_colorTintEnvelope == NULL)
//				m_colorTintEnvelope = newInstance(TintEnvelope);
//			m_colorTintEnvelope->play( &RED_IRRADIATED_COLOR, 30, 30, SUSTAIN_INDEFINITELY);
//		}
		else 
		{ 
			// NO TINTING SHOULD BE PRESENT
			if (m_colorTintEnvelope == nullptr)
				m_colorTintEnvelope = newInstance(TintEnvelope);
			m_colorTintEnvelope->release(); // head on back to normal, now
		}

	}

	m_prevTintStatus = m_tintStatus;//for next frame

	if ( obj )
	{
		if ( ! obj->isEffectivelyDead() )
			clearTintStatus( TINT_STATUS_IRRADIATED); // so the res glow stops when not exposed
	}

	if (m_colorTintEnvelope)
	  m_colorTintEnvelope->update(); // defector fx, disable fx, etc...

	if (m_selectionFlashEnvelope)
		m_selectionFlashEnvelope->update(); // selection flashing

	//If we have an ambient sound, and we aren't currently playing it, attempt to play it now.
  // However, if the attached sound is a one-shot (non-looping) sound, don't restart it -- only
  // start it ONCE. The problem is, looping sounds need to keep being restarted. Why? Because 
  // the audio manager will kill the sound if it gets
  // out of range. Looping ambient sounds need to restart if the user moves back into range.
  // The audio manager doesn't handle this, so we need to keep checking looping sounds
  // to see if they are in range. But this messes up non-looping sounds -- they keep looping!
  // End result: a hack of testing the looping bit and only restarting the sound if the looping
  // bit is on and the loop count is 0 (loop forever).
  if( m_ambientSound && m_ambientSoundEnabled && m_ambientSoundEnabledFromScript && 
      !m_ambientSound->m_event.getEventName().isEmpty() && !m_ambientSound->m_event.isCurrentlyPlaying() ) 
  {
    const AudioEventInfo * eventInfo = m_ambientSound->m_event.getAudioEventInfo();

    if ( eventInfo == nullptr && TheAudio != nullptr )
    {
      // We'll need this in a second anyway so cache it
      TheAudio->getInfoForAudioEvent( &m_ambientSound->m_event );
      eventInfo = m_ambientSound->m_event.getAudioEventInfo();
    }

    if ( eventInfo == nullptr || ( eventInfo->isPermanentSound() ) )
    {
  		startAmbientSound();
    }
 	}
}	
 
//-------------------------------------------------------------------------------------------------
// Called just after the level loads. Only called for NEW games, not save games.
void Drawable::onLevelStart()
{
  // Make sure the current ambient sound is playing if it should be playing. Needed because
  // the call to startAmbientSound in the constructor is too early to
  // actually start the sound if the constructor is called during level load. 
  if( m_ambientSoundEnabled && m_ambientSoundEnabledFromScript && 
      ( m_ambientSound == nullptr || 
        ( !m_ambientSound->m_event.getEventName().isEmpty() && !m_ambientSound->m_event.isCurrentlyPlaying() ) ) )
  {
    // Unlike the check in the update() function, we want to do this for looping & one-shot sounds equally
    startAmbientSound();
  }
}

//-------------------------------------------------------------------------------------------------
void Drawable::flashAsSelected( const RGBColor *color ) ///< drawable takes care of the details if you spec no color
{
	if (m_selectionFlashEnvelope == nullptr)
		m_selectionFlashEnvelope = newInstance(TintEnvelope);

	if ( color )
	{
		m_selectionFlashEnvelope->play( color, 0, 4 );
	}
	else
	{
		RGBColor tempColor; 
		if (getSelectionFlashColor(tempColor))
			m_selectionFlashEnvelope->play( &tempColor, 0, 4 );
	}
}

Bool Drawable::getSelectionFlashColor( RGBColor &color )
{
	const Object *obj = getObject();
	if (!obj)
		return FALSE;
	if (TheGlobalData->m_selectionFlashHouseColor)
		color.setFromInt(obj->getIndicatorColor());
	else
		color.setFromInt(0xffffffff);//white

	Real saturation = TheGlobalData->m_selectionFlashSaturationFactor;
	saturateRGB( color, saturation );
	return TRUE;
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
#ifdef _DEBUG
void Drawable::validatePos() const
{
	const Coord3D* ourPos = getPosition();
	if (_isnan(ourPos->x) || _isnan(ourPos->y) || _isnan(ourPos->z))
	{
		DEBUG_CRASH(("Drawable/Object position NAN! '%s'\n", getTemplate()->getName().str()));
	}
	// A laser places its drawable at the middle of the beam (for culling) and, as a client update, cannot touch the
	// logic position; the two legitimately differ.
	static const NameKeyType s_laserKey = NAMEKEY("LaserUpdate");
	if (getObject() && const_cast<Drawable*>(this)->findClientUpdateModule(s_laserKey) == nullptr)
	{
		const Coord3D* objPos = getObject()->getPosition();
		if (ourPos->x != objPos->x || ourPos->y != objPos->y || ourPos->z != objPos->z)
		{
			DEBUG_CRASH(("Drawable/Object position mismatch! '%s'\n", getTemplate()->getName().str()));
		}
	}
}
#endif


//=============================================================================
void Drawable::setStealthLook(StealthLookType look)
{
	if (look != m_stealthLook)
	{

		m_stealthOpacity = 1.0f;	//assume not transparent
		switch (look)
		{
			case STEALTHLOOK_NONE:
				m_hiddenByStealth = false;
				m_secondMaterialPassOpacity = 0.0f;
				break;

			case STEALTHLOOK_VISIBLE_FRIENDLY:
			case STEALTHLOOK_VISIBLE_FRIENDLY_DETECTED:
			{
				Real opacity = TheGlobalData->m_stealthFriendlyOpacity;

				Object *obj = getObject();
				if( obj )
				{
					//Try to get the stealthupdate module and see if the opacity value is overriden.
          StealthUpdate *stealth = obj->getStealth();
					if( stealth )
					{
						if( stealth->isDisguised() )
						{
							//Disguised objects drive the opacity level directly, hence the break.
							m_hiddenByStealth = false;
							break;
						}
						else
						{
							Real friendlyOpacity = stealth->getFriendlyOpacity();
							if( friendlyOpacity != INVALID_OPACITY )
							{
								opacity = friendlyOpacity;
							}
						}
					}
				}

				m_stealthOpacity = opacity;	// make as partially transparent as this while pulsing
				m_hiddenByStealth = false;

			/** @todo srj -- evil hack here... this whole heat-vision thing is fucked.
				don't want it on mines but no good way to do that. hack for now. */
				if (look == STEALTHLOOK_VISIBLE_FRIENDLY_DETECTED && !isKindOf(KINDOF_MINE))
					m_secondMaterialPassOpacity = 1.0f;
				else
					m_secondMaterialPassOpacity = 0.0f;

				break;
			}
			
			case STEALTHLOOK_DISGUISED_ENEMY:
				m_hiddenByStealth = false;
				m_secondMaterialPassOpacity = 0.0f;
				break;

			// this is for the non-controllingplayer that can see me anyway
			case STEALTHLOOK_VISIBLE_DETECTED:
				m_hiddenByStealth = false;// let the scene omit the first drawing pass
				/** @todo srj -- evil hack here... this whole heat-vision thing is fucked.
					don't want it on mines but no good way to do that. hack for now. */
				if (isKindOf(KINDOF_MINE))
					m_secondMaterialPassOpacity = 0.0f;
				else
					m_secondMaterialPassOpacity = 1.0f;// Draw() will fade until it is set to 1 again
				break;

			case STEALTHLOOK_INVISIBLE:
				m_hiddenByStealth = true;
				m_secondMaterialPassOpacity = 0.0f;
				break;
		}
		m_stealthLook = look;
		updateHiddenStatus();
	}
}


// ------------------------------------------------------------------------------------------------
// Returns the interpolation fraction for the current render frame (see Drawable.h).
Bool Drawable::getRenderInterpolation( Real& t ) const
{
	// also in the tick frame after the logic, not only in the in-between frames
	if (!GameClient_IsRenderInterpolated() || m_interpChangeSerial == 0)
		return FALSE;
	const UnsignedInt now = TheGameClient->getTickSerial();
	if (now < m_interpChangeSerial)
		return FALSE;
	// progress since the last change, relative to the interval between the last two
	const Real elapsed = static_cast<Real>(now - m_interpChangeSerial) + GameClient_RenderInterpolation();
	t = elapsed / static_cast<Real>(m_interpIntervalTicks);
	if (t >= 1.0f)
		return FALSE;
	if (t < 0.0f)
		t = 0.0f;
	// jump (teleport, new object, recycling): do not interpolate
	const Real INTERP_SNAP_DIST_SQR = 100.0f * 100.0f;
	Vector3 delta = getTransformMatrix()->Get_Translation() - m_interpPrevTransform.Get_Translation();
	return delta.Length2() < INTERP_SNAP_DIST_SQR;
}

// ------------------------------------------------------------------------------------------------
void Drawable::getRenderInterpolationOffset( Coord3D *delta ) const
{
	delta->x = delta->y = delta->z = 0.0f;
	Real t;
	if (!getRenderInterpolation(t))
		return;
	const Vector3 cur = getTransformMatrix()->Get_Translation();
	const Vector3 prev = m_interpPrevTransform.Get_Translation();
	// interpolated = prev + (cur - prev) * t; offset to the tick position = (t - 1) * (cur - prev)
	delta->x = (cur.X - prev.X) * (t - 1.0f);
	delta->y = (cur.Y - prev.Y) * (t - 1.0f);
	delta->z = (cur.Z - prev.Z) * (t - 1.0f);
}

//-------------------------------------------------------------------------------------------------
/** default draw is to just call the database defined draw */
//-------------------------------------------------------------------------------------------------
void Drawable::draw( View *view )
{

  if ( testTintStatus( TINT_STATUS_FRENZY ) == FALSE )
  {
    if ( getObject() && getObject()->isEffectivelyDead() )
		  m_secondMaterialPassOpacity = 0.0f;//dead folks don't stealth anyway
	  else if ( m_secondMaterialPassOpacity > VERY_TRANSPARENT_MATERIAL_PASS_OPACITY )// keep fading any add'l material unless something has set it to zero
		{
			if (!GameClient_IsRenderOnlyFrame())	// fade advances per tick
				m_secondMaterialPassOpacity *= MATERIAL_PASS_OPACITY_FADE_SCALAR;
		}
	  else
		  m_secondMaterialPassOpacity = 0.0f;
  }


	if (m_hidden || m_hiddenByStealth || getFullyObscuredByShroud())
		return;	// my, that was easy

	if ( getObject() && !getObject()->isEffectivelyDead() )
		setShadowsEnabled( m_stealthLook != STEALTHLOOK_VISIBLE_DETECTED );



#ifdef _DEBUG
	validatePos();
#endif

	// call the database defined draw action method
	Matrix3D transformMtx = *getTransformMatrix();
	// In render-only frames interpolate between the state after the previous and the current tick. The
	// tick state (getPosition() etc.) is unchanged; this is presentation only.
	{
		Real t;
		if (getRenderInterpolation(t))
		{
			Matrix3D lerped;
			Matrix3D::Lerp(m_interpPrevTransform, transformMtx, t, lerped);
			transformMtx = lerped;
		}
	}
	if (!isInstanceIdentity())
	{
		transformMtx.postMul(*getInstanceMatrix());
	}

	applyPhysicsXform(&transformMtx);

	for (DrawModule** dm = getDrawModules(); *dm; ++dm)
	{
		(*dm)->doDrawModule(&transformMtx);
	}
}


// ------------------------------------------------------------------------------------------------



//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void Drawable::clearAndSetModelConditionState( ModelConditionFlagType clr, ModelConditionFlagType set )
{
	ModelConditionFlags c, s;
	if (clr != MODELCONDITION_INVALID)
		c.set(clr);
	if (set != MODELCONDITION_INVALID)
		s.set(set);
	clearAndSetModelConditionFlags(c, s);
}

//-------------------------------------------------------------------------------------------------
DrawModule** Drawable::getDrawModulesNonDirty() 
{ 
	DrawModule** dm = reinterpret_cast<DrawModule**>(getModuleList(MODULETYPE_DRAW)); 
	return dm;
}

//-------------------------------------------------------------------------------------------------
DrawModule** Drawable::getDrawModules() 
{ 
	DrawModule** dm = reinterpret_cast<DrawModule**>(getModuleList(MODULETYPE_DRAW)); 
#ifdef DIRTY_CONDITION_FLAGS
	if (m_isModelDirty)
	{
		if (s_modelLockCount > 0)
		{
			DEBUG_CRASH(("Should not need to update dirty stuff while locked-for-iteration. Ignoring."));
			// this shouldn't happen, but if it does, just return the current (dirty) scenario.
			// we must NOT update the condition state, as someone is relying on the current
			// list of W3D render objects not being munged. (srj)
		}
		else
		{
			for (DrawModule** dm2 = dm; *dm2; ++dm2)
			{
				ObjectDrawInterface* di = (*dm2)->getObjectDrawInterface();
				if (di)
					di->replaceModelConditionState( m_conditionState );
			}
			m_isModelDirty = false;
		}
	}
#endif
	return dm;
}

//-------------------------------------------------------------------------------------------------
DrawModule const** Drawable::getDrawModules() const 
{ 
	DrawModule const** dm = (DrawModule const**)getModuleList(MODULETYPE_DRAW); 
#ifdef DIRTY_CONDITION_FLAGS
	if (m_isModelDirty)
	{
		if (s_modelLockCount > 0)
		{
			DEBUG_CRASH(("Should not need to update dirty stuff while locked-for-iteration. Ignoring."));
			// this shouldn't happen, but if it does, just return the current (dirty) scenario.
			// we must NOT update the condition state, as someone is relying on the current
			// list of W3D render objects not being munged. (srj)
		}
		else
		{
			// yeah, yeah, yeah... I know (srj)
			for (DrawModule** dm2 = const_cast<DrawModule**>(dm); *dm2; ++dm2)
			{
				ObjectDrawInterface* di = (*dm2)->getObjectDrawInterface();
				if (di)
					di->replaceModelConditionState( m_conditionState );
			}
			m_isModelDirty = false;
		}
	}
#endif
	return dm;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void Drawable::clearAndSetModelConditionFlags(const ModelConditionFlags& clr, const ModelConditionFlags& setf)
{
	ASSERT_MAIN_THREAD();	// the logic still calls this directly; must move to the sync point
	ModelConditionFlags oldFlags = m_conditionState;

	m_conditionState.clearAndSet(clr, setf);
	
	if (m_conditionState == oldFlags)
		return;

#ifdef DIRTY_CONDITION_FLAGS
	m_isModelDirty = true;
#else
	for (DrawModule** dm = getDrawModules(); *dm; ++dm)
	{
		ObjectDrawInterface* di = (*dm)->getObjectDrawInterface();
		if (di)
			di->replaceModelConditionState( m_conditionState );
	}
#endif
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void Drawable::replaceModelConditionFlags( const ModelConditionFlags &flags, Bool forceReplace )
{

	//
	// this is a no-op if the new flags are the same as our existing flags (unless we
	// have the forceReplace parameter set, in which case we will force the setting of the
	// new flags)
	//
	if( forceReplace == FALSE && m_conditionState == flags )
		return;
		
	m_conditionState = flags;
#ifdef DIRTY_CONDITION_FLAGS
	// when forcing a replace we won't use dirty flags, we will immediately do an update now
	if( forceReplace == TRUE )
	{
		for (DrawModule** dm = getDrawModules(); *dm; ++dm)
		{
			ObjectDrawInterface* di = (*dm)->getObjectDrawInterface();
			if (di)
				di->replaceModelConditionState( m_conditionState );
		}
		m_isModelDirty = false;
	}
	else 
		m_isModelDirty = true;
#else
	for (DrawModule** dm = getDrawModules(); *dm; ++dm)
	{
		ObjectDrawInterface* di = (*dm)->getObjectDrawInterface();
		if (di)
			di->replaceModelConditionState( m_conditionState );
	}
#endif
}

//-------------------------------------------------------------------------------------------------
void Drawable::setIndicatorColor(Color color)
{
	for (DrawModule** dm = getDrawModules(); *dm; ++dm)
	{
		ObjectDrawInterface* di = (*dm)->getObjectDrawInterface();
		if (di)
			di->replaceIndicatorColor(color);
	}
}

// ------------------------------------------------------------------------------------------------
const GeometryInfo& Drawable::getDrawableGeometryInfo() const 
{ 
	return getObject() ? getObject()->getGeometryInfo() : getTemplate()->getTemplateGeometryInfo(); 
}

// ------------------------------------------------------------------------------------------------
/** Set the id for this drawable */
// ------------------------------------------------------------------------------------------------
void Drawable::setID( DrawableID id )
{

	// if id hasn't changed do nothing
	if( m_id == id )
		return;

	// remove this objects previous id from the lookup table
	if( m_id != INVALID_DRAWABLE_ID )
		TheGameClient->removeDrawableFromLookupTable( this );

	// assign new id
	m_id = id;

	// add new id to lookup table
	if( m_id != INVALID_DRAWABLE_ID )
	{
		TheGameClient->addDrawableToLookupTable( this );
		if (m_ambientSound)
			m_ambientSound->m_event.setDrawableID(m_id);
	}

}  // end setID

// ------------------------------------------------------------------------------------------------
/** Return drawable ID, this ID is only good on the client */
// ------------------------------------------------------------------------------------------------
DrawableID Drawable::getID( void ) const
{

	// we should never be getting the ID of a drawable who doesn't yet have and ID assigned to it
	DEBUG_ASSERTCRASH( m_id != 0, ("Drawable::getID - Using ID before it was assigned!!!!\n") );

	return m_id;

}  // end get ID

//-------------------------------------------------------------------------------------------------
void Drawable::friend_bindToObject( Object *obj ) ///< bind this drawable to an object ID
{ 
	m_object = obj; 
	if (getObject())
	{
		if (TheGlobalData->m_timeOfDay == TIME_OF_DAY_NIGHT)
			setIndicatorColor(getObject()->getNightIndicatorColor());
		else
			setIndicatorColor(getObject()->getIndicatorColor());

		if (getObject()->isKindOf(KINDOF_FS_FAKE))
		{
			Relationship rel=ThePlayerList->getLocalPlayer()->getRelationship(getObject()->getTeam());
			if (rel == ALLIES || rel == NEUTRAL)
				setTerrainDecal(TERRAIN_DECAL_SHADOW_TEXTURE);
			else
				setTerrainDecal(TERRAIN_DECAL_NONE);
		}
	}

	for (DrawModule** dm = getDrawModules(); *dm; ++dm)
	{
		(*dm)->onDrawableBoundToObject();
	}
}					
//-------------------------------------------------------------------------------------------------
	// when our Object changes teams, it calls us to let us know, so
	// we can update our model, etc., if necessary. NOTE, we don't guarantee
	// that the new team is different from the old team, nor do we guarantee
	// that the team is nonnull.
void Drawable::changedTeam()
{
	Object *object = getObject();
	if( object )
	{
		if (TheGlobalData->m_timeOfDay == TIME_OF_DAY_NIGHT)
			setIndicatorColor( object->getNightIndicatorColor() );
		else
			setIndicatorColor( object->getIndicatorColor() );

		if (object->isKindOf(KINDOF_FS_FAKE))
		{
			Relationship rel=ThePlayerList->getLocalPlayer()->getRelationship(object->getTeam());
			if (rel == ALLIES || rel == NEUTRAL)
				setTerrainDecal(TERRAIN_DECAL_SHADOW_TEXTURE);
			else
				setTerrainDecal(TERRAIN_DECAL_NONE);
		}
	}
}

//-------------------------------------------------------------------------------------------------
void Drawable::setPosition(const Coord3D *pos) 
{
	// extend
	Thing::setPosition(pos);

}

//-------------------------------------------------------------------------------------------------
void Drawable::reactToTransformChange(const Matrix3D* oldMtx, const Coord3D* oldPos, Real oldAngle)
{
	// Remember the state before the first change in the current tick: the state after the previous
	// tick, which the render frames interpolate from.
	if (TheGameClient)
	{
		const UnsignedInt serial = TheGameClient->getTickSerial();
		if (m_interpChangeSerial != serial)
		{
			m_interpPrevTransform = *oldMtx;
			// Objects that move only every second or third tick (sleeping update modules) are interpolated
			// over the whole interval; after a longer pause (> 4 ticks) one tick applies again.
			UnsignedInt interval = (m_interpChangeSerial != 0 && serial > m_interpChangeSerial) ? (serial - m_interpChangeSerial) : 1;
			m_interpIntervalTicks = (interval > 4) ? 1 : interval;
			m_interpChangeSerial = serial;
		}
	}
	for (DrawModule** dm = getDrawModules(); *dm; ++dm)
	{
		(*dm)->reactToTransformChange(oldMtx, oldPos, oldAngle);
	}
} 

//-------------------------------------------------------------------------------------------------
void Drawable::reactToGeometryChange()
{
	for (DrawModule** dm = getDrawModules(); *dm; ++dm)
	{
		(*dm)->reactToGeometryChange();
	}
} 

//-------------------------------------------------------------------------------------------------
Bool Drawable::handleWeaponFireFX(WeaponSlotType wslot, Int specificBarrelToUse, const FXList* fxl, Real weaponSpeed, Real recoilAmount, Real recoilAngle, const Coord3D* victimPos, Real damageRadius)
{	  
	if (recoilAmount != 0.0f)
	{
		// adjust recoil from absolute to relative.
		if (getObject())
			recoilAngle -= getObject()->getOrientation();
		// flip direction 180 degrees.
		recoilAngle += PI;
		if (m_locoInfo)
		{
			m_locoInfo->m_accelerationPitchRate += recoilAmount * Cos(recoilAngle);
			m_locoInfo->m_accelerationRollRate += recoilAmount * Sin(recoilAngle);
		}
	}

	for (DrawModule** dm = getDrawModules(); *dm; ++dm)
	{
		ObjectDrawInterface* di = (*dm)->getObjectDrawInterface();
		if (di && di->handleWeaponFireFX(wslot, specificBarrelToUse, fxl, weaponSpeed, victimPos, damageRadius))
			return true;
	}
	return false;
} 

//-------------------------------------------------------------------------------------------------
Int Drawable::getBarrelCount(WeaponSlotType wslot) const
{
	for (const DrawModule** dm = getDrawModules(); *dm; ++dm)
	{
		const ObjectDrawInterface* di = (*dm)->getObjectDrawInterface();
		Int count = di ? di->getBarrelCount(wslot) : 0;
		if (count != 0)
			return count;
	}
	return 0;
} 

//-------------------------------------------------------------------------------------------------
/** Set the Drawable's instance transform */
//-------------------------------------------------------------------------------------------------
void Drawable::setInstanceMatrix( const Matrix3D *instance ) 
{ 
	if (instance)
	{
		m_instance = *instance; 
		m_instanceIsIdentity = false;
	}
	else
	{
		m_instance.Make_Identity();
		m_instanceIsIdentity = true;
	}
}


//-------------------------------------------------------------------------------------------------
/** 
 * Return the Drawable's world transform.
 * If this Drawable is attached to an Object, return the Object's transform instead.
 */
//-------------------------------------------------------------------------------------------------
const Matrix3D *Drawable::getTransformMatrix( void ) const
{
	const Object *obj = getObject();

	if (obj)
		return obj->getTransformMatrix();
	else
		return Thing::getTransformMatrix();
}

//-------------------------------------------------------------------------------------------------
/** 
 * Set and clear the drawable's caption text
 */
//-------------------------------------------------------------------------------------------------
void Drawable::setCaptionText( const UnicodeString& captionText )
{
	if (captionText.isEmpty())
	{
		clearCaptionText();
		return;
	}

	UnicodeString sanitizedString = captionText;
	TheLanguageFilter->filterLine(sanitizedString);

	if( m_captionDisplayString == nullptr )
	{
		m_captionDisplayString = TheDisplayStringManager->newDisplayString();
		GameFont *font = TheFontLibrary->getFont(
			TheInGameUI->getDrawableCaptionFontName(),
			TheGlobalLanguageData->adjustFontSize(TheInGameUI->getDrawableCaptionPointSize()),
			TheInGameUI->isDrawableCaptionBold() );
		m_captionDisplayString->setFont( font );
		m_captionDisplayString->setText( sanitizedString );
	}
	else
	{
		// set the string if the value has changed
		if( m_captionDisplayString->getText().compare(sanitizedString) != 0 )
		{
			m_captionDisplayString->setText( sanitizedString );
		}
	}
}

//-------------------------------------------------------------------------------------------------
void Drawable::clearCaptionText( void )
{
	if (m_captionDisplayString)
		TheDisplayStringManager->freeDisplayString(m_captionDisplayString);
	m_captionDisplayString = nullptr;
}

//-------------------------------------------------------------------------------------------------
UnicodeString Drawable::getCaptionText( void )
{
	if (m_captionDisplayString)
		return m_captionDisplayString->getText();

	return UnicodeString::TheEmptyString;
}

//-------------------------------------------------------------------------------------------------
/** Attach and start playing an ambient sound to this drawable */
//-------------------------------------------------------------------------------------------------
void	Drawable::setTimeOfDay(TimeOfDay tod)
{
	BodyDamageType dt = BODY_PRISTINE;
	if (getObject() && getObject()->getBodyModule())
		dt = getObject()->getBodyModule()->getDamageState();

	startAmbientSound(dt, tod);

	ModelConditionFlags c = m_conditionState;
	c.set(MODELCONDITION_NIGHT, (tod == TIME_OF_DAY_NIGHT) ? 1 : 0);
	replaceModelConditionFlags(c);
}




//-------------------------------------------------------------------------------------------------
/** add self to the linked list */
//-------------------------------------------------------------------------------------------------
void Drawable::prependToList(Drawable **pListHead)
{
	// add the object to the global list
	m_prevDrawable = nullptr;
	m_nextDrawable = *pListHead;
	if (*pListHead)
		(*pListHead)->m_prevDrawable = this;
	*pListHead = this;
}

//-------------------------------------------------------------------------------------------------
/** remove self from the linked list */
//-------------------------------------------------------------------------------------------------
void Drawable::removeFromList(Drawable **pListHead)
{
	if (m_nextDrawable)
		m_nextDrawable->m_prevDrawable = m_prevDrawable;

	if (m_prevDrawable)
		m_prevDrawable->m_nextDrawable = m_nextDrawable;
	else
		*pListHead = m_nextDrawable;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void Drawable::updateHiddenStatus()
{
	Bool hidden = m_hidden || m_hiddenByStealth;
	if( hidden )
		TheInGameUI->deselectDrawable( this );

	for (DrawModule** dm = getDrawModules(); *dm; ++dm)
	{
		ObjectDrawInterface* di = (*dm)->getObjectDrawInterface();
		if (di)
			di->setHidden(hidden != 0);
	}
	
}

//-------------------------------------------------------------------------------------------------
/** Hide or un-hide drawable */
//-------------------------------------------------------------------------------------------------
void Drawable::setDrawableHidden( Bool hidden )
{
	ASSERT_MAIN_THREAD();	// the logic still calls this directly; must move to the sync point
	if (hidden != m_hidden)
	{
		m_hidden = hidden;
		updateHiddenStatus();
	}
}

//-------------------------------------------------------------------------------------------------
void Drawable::updateDrawableClipStatus( UnsignedInt shotsRemaining, UnsignedInt maxShots, WeaponSlotType slot )
{
	for (DrawModule** dm = getDrawModulesNonDirty(); *dm; ++dm)
	{
		ObjectDrawInterface* di = (*dm)->getObjectDrawInterface();
		if (di)
			di->updateProjectileClipStatus(shotsRemaining, maxShots, slot);
	}
}

//-------------------------------------------------------------------------------------------------
void Drawable::updateDrawableSupplyStatus( Int maxSupply, Int currentSupply )
{
	for (DrawModule** dm = getDrawModules(); *dm; ++dm)
	{
		ObjectDrawInterface* di = (*dm)->getObjectDrawInterface();
		if (di)
			di->updateDrawModuleSupplyStatus( maxSupply, currentSupply );
	}
}

//-------------------------------------------------------------------------------------------------
void Drawable::notifyDrawableDependencyCleared()
{
	for (DrawModule** dm = getDrawModules(); *dm; ++dm)
	{
		ObjectDrawInterface* di = (*dm)->getObjectDrawInterface();
		if (di)
			di->notifyDrawModuleDependencyCleared();
	}
}

//-------------------------------------------------------------------------------------------------
/** Set as selectable or not. */
//-------------------------------------------------------------------------------------------------
void Drawable::setSelectable( Bool selectable )
{
	// unselct drawable if it is no longer selectable.
	if( !selectable )
		TheInGameUI->deselectDrawable( this );

	for (DrawModule** dm = getDrawModules(); *dm; ++dm)
	{
		ObjectDrawInterface* di = (*dm)->getObjectDrawInterface();
		if (di)
			di->setSelectable(selectable);
	}
}

//-------------------------------------------------------------------------------------------------
/** Return whether or not this Drawable is selectable. */
//-------------------------------------------------------------------------------------------------
Bool Drawable::isSelectable( void ) const
{
	return getObject() && getObject()->isSelectable();
}

//-------------------------------------------------------------------------------------------------
/** Return whether or not this Drawable is selectable as part of a group. */
//-------------------------------------------------------------------------------------------------
Bool Drawable::isMassSelectable( void ) const
{
	return getObject() && getObject()->isMassSelectable();
}

//-------------------------------------------------------------------------------------------------
/** Preload all our assets that we can for all our possible states in this time of day */
//-------------------------------------------------------------------------------------------------
void Drawable::preloadAssets( TimeOfDay timeOfDay )
{

	/// walk all our modules and preload any assets we need to
	for( Int i = 0; i < NUM_DRAWABLE_MODULE_TYPES; ++i )
		for( Module** m = m_modules[i]; m && *m; ++m )
			(*m)->preloadAssets( timeOfDay );

}  // end preloadAssets

//-------------------------------------------------------------------------------------------------
// Simply searches for the first occurrence of a specified client update module.
//-------------------------------------------------------------------------------------------------
ClientUpdateModule* Drawable::findClientUpdateModule( NameKeyType key )
{
	ClientUpdateModule **clientModules = getClientUpdateModules();
	if( clientModules )
	{
		while( *clientModules )
		{
			if( (*clientModules)->getModuleNameKey() == key )
			{
				return *clientModules;
			}
			++clientModules;
		}
	}
	return nullptr;
}

//=================================================================================================
//=================================================================================================
#ifdef DIRTY_CONDITION_FLAGS
/*static*/ void Drawable::friend_lockDirtyStuffForIteration()
{
	if (s_modelLockCount == 0)
	{
		if (TheGameClient)	// WB has no GameClient!
		{
			for (Drawable* d = TheGameClient->firstDrawable(); d != nullptr; d = d->getNextDrawable())
			{
				// this will force us to update stuff.
				d->getDrawModules();
			}
		}
	}
	++s_modelLockCount;
}
#endif

//=================================================================================================
//=================================================================================================
#ifdef DIRTY_CONDITION_FLAGS
/*static*/ void Drawable::friend_unlockDirtyStuffForIteration()
{
	if (s_modelLockCount > 0)
		--s_modelLockCount;
}
#endif

//=================================================================================================
//=================================================================================================
TintEnvelope::TintEnvelope(void)
{
	m_attackRate.Set(0,0,0);
	m_decayRate.Set(0,0,0);
	m_peakColor.Set(0,0,0);
	m_currentColor.Set(0,0,0);
	m_envState = ENVELOPE_STATE_REST;
	m_sustainCounter = 0;
	m_affect = FALSE;
}

//-------------------------------------------------------------------------------------------------
const Real FADE_RATE_EPSILON = (0.001f);

//-------------------------------------------------------------------------------------------------
void TintEnvelope::play(const RGBColor *peak, UnsignedInt atackFrames, UnsignedInt decayFrames, UnsignedInt sustainAtPeak )    
{
	setPeakColor( peak );

	setAttackFrames( atackFrames );
	setDecayFrames( decayFrames );

	m_envState = ENVELOPE_STATE_ATTACK;
	m_sustainCounter = sustainAtPeak;
	m_affect = TRUE;

	Vector3 delta;
	Vector3::Subtract(m_currentColor, m_peakColor, &delta);

	if ( delta.Length() <= FADE_RATE_EPSILON ) // we are practically already at this color
		m_envState = ENVELOPE_STATE_SUSTAIN;

}

//-------------------------------------------------------------------------------------------------
void TintEnvelope::setAttackFrames(UnsignedInt frames) 
{
	Real recipFrames = 1.0f / static_cast<Real>(MAX(1,frames));
	m_attackRate.Set( m_currentColor );
	Vector3::Subtract( m_peakColor, m_attackRate, &m_attackRate);
	m_attackRate.Scale( Vector3(recipFrames, recipFrames, recipFrames) );
}

//-------------------------------------------------------------------------------------------------
void TintEnvelope::setDecayFrames( UnsignedInt frames )
{
	Real recipFrames = ( -1.0f ) / static_cast<Real>(MAX(1,frames));
	m_decayRate.Set( m_peakColor );
	m_decayRate.Scale( Vector3(recipFrames, recipFrames, recipFrames) );
}

//-------------------------------------------------------------------------------------------------
void TintEnvelope::update(void)
{
	switch ( m_envState )
	{
		case ( ENVELOPE_STATE_REST ) : //most likely case
		{
			m_currentColor.Set(0,0,0);
			m_affect = FALSE;
			break;
		}
		case ( ENVELOPE_STATE_DECAY ) : // much more likely than attack
		{
			if (m_decayRate.Length() > m_currentColor.Length() || m_currentColor.Length() <= FADE_RATE_EPSILON) //we are at rest
			{
				m_envState = ENVELOPE_STATE_REST;
				m_affect = FALSE;
			}
			else
			{
				Vector3::Add( m_decayRate, m_currentColor, &m_currentColor );//Add the decayRate to the current color;
				m_affect = TRUE;
			}
			break;
		}
		case ( ENVELOPE_STATE_ATTACK ) : 
		{
			Vector3 delta;
			Vector3::Subtract(m_currentColor, m_peakColor, &delta);
			
			if (m_attackRate.Length() > delta.Length() || delta.Length() <= FADE_RATE_EPSILON) //we are at the peak
			{
				if ( m_sustainCounter )
				{
					m_envState = ENVELOPE_STATE_SUSTAIN;
				}
				else
				{
					m_envState = ENVELOPE_STATE_DECAY;
				}

			}
			else
			{
				Vector3::Add( m_attackRate, m_currentColor, &m_currentColor );//Add the attackRate to the current color;
				m_affect = TRUE;
			}
			
			break;
		}
		case ( ENVELOPE_STATE_SUSTAIN ) :
		{
			if ( m_sustainCounter > 0 )
				--m_sustainCounter;
			else
				release();
				
			break;
		}
		default:
		{
			//do nothing, we are sustaining until externally triggered to release (decay)
			break;
		}
	}
	// here we transition the color from current to peak to release, according to 

}

