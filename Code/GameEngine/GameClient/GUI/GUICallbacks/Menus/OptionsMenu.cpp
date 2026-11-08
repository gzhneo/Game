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

// FILE: OptionsMenu.cpp //////////////////////////////////////////////////////////////////////////
// Author: Colin Day, October 2001
// Description: options menu window callbacks
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/AudioAffect.h"
#include "Common/AudioSettings.h"
#include "Common/GameAudio.h"
#include "Common/GameEdition.h"
#include "Common/GameEngine.h"
#include "Common/UserSettings.h"
#include "Engine/TextureTier.h"
#include "Common/GameLOD.h"
#include "Common/InstallInfo.h"
#include "Common/version.h"

#include "GameClient/GameClient.h"
#include "GameClient/InGameUI.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetRadioButton.h"
#include "GameClient/GadgetSlider.h"
#include "GameClient/HeaderTemplate.h"
#include "GameClient/Shell.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Mouse.h"
#include "GameClient/GameText.h"
#include "GameClient/Display.h"
#include "GameClient/IMEManager.h"
#include "GameClient/ShellHooks.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/OptionsMenuTabs.h"
#include "GameNetwork/IPEnumeration.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptEngine.h"
//added by saad
//used to access a messagebox that does "ok" and "cancel"
#include "GameClient/MessageBox.h"

// This is for non-RC builds only!!!
#define VERBOSE_VERSION L"Release"




static NameKeyType    comboBoxAntiAliasingID   = NAMEKEY_INVALID;
static GameWindow *   comboBoxAntiAliasing     = nullptr;

static NameKeyType    comboBoxResolutionID      = NAMEKEY_INVALID;
static GameWindow *   comboBoxResolution       = nullptr; 

static NameKeyType    comboBoxDetailID      = NAMEKEY_INVALID;
static GameWindow *   comboBoxDetail        = nullptr; 

static NameKeyType		checkAlternateMouseID	= NAMEKEY_INVALID;
static GameWindow *		checkAlternateMouse		= nullptr;

static NameKeyType		checkRetaliationID	= NAMEKEY_INVALID;
static GameWindow *		checkRetaliation		= nullptr;

static NameKeyType		checkDoubleClickAttackMoveID	= NAMEKEY_INVALID;
static GameWindow *		checkDoubleClickAttackMove		= nullptr;

static NameKeyType		sliderScrollSpeedID	= NAMEKEY_INVALID;
static GameWindow *		sliderScrollSpeed		= nullptr;

static NameKeyType    checkLanguageFilterID = NAMEKEY_INVALID;
static GameWindow *   checkLanguageFilter   = nullptr;

static NameKeyType		checkUseCameraID		= NAMEKEY_INVALID;
static GameWindow *		checkUseCamera			= nullptr;

static NameKeyType		checkSaveCameraID		= NAMEKEY_INVALID;
static GameWindow *		checkSaveCamera			= nullptr;


static NameKeyType		checkDrawAnchorID		= NAMEKEY_INVALID;
static GameWindow *		checkDrawAnchor			= nullptr;

static NameKeyType		checkMoveAnchorID		= NAMEKEY_INVALID;
static GameWindow *		checkMoveAnchor			= nullptr;

//
//static NameKeyType    checkAudioHardwareID = NAMEKEY_INVALID;
//static GameWindow *   checkAudioHardware   = NULL;
//
//static NameKeyType    checkAudioSurroundID = NAMEKEY_INVALID;
//static GameWindow *   checkAudioSurround   = NULL;
////volume controls
//
static NameKeyType    sliderMusicVolumeID = NAMEKEY_INVALID;
static GameWindow *   sliderMusicVolume   = nullptr;

static NameKeyType    sliderSFXVolumeID = NAMEKEY_INVALID;
static GameWindow *   sliderSFXVolume   = nullptr;

static NameKeyType    sliderVoiceVolumeID = NAMEKEY_INVALID;
static GameWindow *   sliderVoiceVolume   = nullptr;

static NameKeyType    sliderGammaID = NAMEKEY_INVALID;
static GameWindow *   sliderGamma = nullptr;

//Advanced Options Screen
static NameKeyType    WinAdvancedDisplayID      = NAMEKEY_INVALID;
static GameWindow *   WinAdvancedDisplay				= nullptr; 

static NameKeyType    ButtonAdvancedAcceptID      = NAMEKEY_INVALID;
static GameWindow *   ButtonAdvancedAccept				= nullptr; 

static NameKeyType    ButtonAdvancedCancelID      = NAMEKEY_INVALID;
static GameWindow *   ButtonAdvancedCancel				= nullptr; 

static NameKeyType    sliderTextureResolutionID = NAMEKEY_INVALID;
static GameWindow *   sliderTextureResolution = nullptr;

static NameKeyType    sliderParticleCapID = NAMEKEY_INVALID;
static GameWindow *   sliderParticleCap = nullptr;

static NameKeyType    check3DShadowsID = NAMEKEY_INVALID;
static GameWindow *   check3DShadows   = nullptr;

static NameKeyType    check2DShadowsID = NAMEKEY_INVALID;
static GameWindow *   check2DShadows   = nullptr;

static NameKeyType    checkCloudShadowsID = NAMEKEY_INVALID;
static GameWindow *   checkCloudShadows   = nullptr;

static NameKeyType    checkGroundLightingID = NAMEKEY_INVALID;
static GameWindow *   checkGroundLighting   = nullptr;

static NameKeyType    checkSmoothWaterID = NAMEKEY_INVALID;
static GameWindow *   checkSmoothWater   = nullptr;

static NameKeyType    checkBuildingOcclusionID = NAMEKEY_INVALID;
static GameWindow *   checkBuildingOcclusion   = nullptr;

static NameKeyType    checkPropsID = NAMEKEY_INVALID;
static GameWindow *   checkProps   = nullptr;

static NameKeyType    checkExtraAnimationsID = NAMEKEY_INVALID;
static GameWindow *   checkExtraAnimations   = nullptr;

static NameKeyType    checkNoDynamicLodID = NAMEKEY_INVALID;
static GameWindow *   checkNoDynamicLod   = nullptr;

static NameKeyType    checkUnlockFpsID = NAMEKEY_INVALID;
static GameWindow *   checkUnlockFps   = nullptr;

static NameKeyType    checkHeatEffectsID = NAMEKEY_INVALID;
static GameWindow *   checkHeatEffects   = nullptr;

/*

static NameKeyType    radioHighID = NAMEKEY_INVALID;
static GameWindow *   radioHigh   = NULL;
static NameKeyType    radioMediumID = NAMEKEY_INVALID;
static GameWindow *   radioMedium   = NULL;
static NameKeyType    radioLowID = NAMEKEY_INVALID;
static GameWindow *   radioLow   = NULL;

*/

//Added By Saad for the resolution confirmation dialog box
DisplaySettings oldDispSettings, newDispSettings;
Bool dispChanged = FALSE;
extern Int timer;
extern void DoResolutionDialog();
//

static Bool ignoreSelected = FALSE;
static Bool detailLevelChanged = FALSE;		// saveOptions applied another detail level: a note after closing
WindowLayout *OptionsLayout = nullptr;

enum Detail
{
	VERYHIGHDETAIL = 0,	// "Ultra" (STATIC_GAME_LOD_VERY_HIGH)
	HIGHDETAIL,
	MEDIUMDETAIL,
	LOWDETAIL,
	CUSTOMDETAIL,

	DETAIL,
};


// Changes made in the open menu: "Accept" hands them to the settings and saves, "Back" drops them.
// On the heap only while changes are staged: a static container would keep a debug-build allocation
// past the leak check at exit.
struct MenuChange
{
	const char *id;
	AsciiString value;
	Bool unset;		///< remove the value from the file
};
using MenuChanges = std::vector<MenuChange>;
static std::unique_ptr<MenuChanges> s_menuChanges;

static void stageChange( const char *id, const AsciiString &value, Bool unset )
{
	if( !s_menuChanges )
		s_menuChanges = std::make_unique<MenuChanges>();
	for( auto &change : *s_menuChanges )
	{
		if( strcmp( change.id, id ) == 0 )
		{
			change.value = value;
			change.unset = unset;
			return;
		}
	}
	s_menuChanges->push_back( MenuChange{ id, value, unset } );
}

static void stageSetting( const char *id, const AsciiString &value )
{
	stageChange( id, value, FALSE );
}

// Not an overload of stageSetting: a text would convert to Bool rather than to AsciiString.
static void stageFlag( const char *id, Bool value )
{
	stageSetting( id, AsciiString( value ? "yes" : "no" ) );
}

// After "Accept": a setting that applies at the next start now differs from what this run started with.
// In the shell the game offers to start again at once; in a match it only says so.
static void offerRestart()
{
	if( TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() )
		MessageBoxFitText( MessageBoxOk( TheGameText->fetch( "GZH:OptionsRestartTitle" ), TheGameText->fetch( "GZH:OptionsRestartLater" ), nullptr ) );
	else
		MessageBoxFitText( MessageBoxYesNo( TheGameText->fetch( "GZH:OptionsRestartTitle" ), TheGameText->fetch( "GZH:OptionsRestart" ),
			GameEngine_RequestRestart, nullptr ) );
}

// Returns whether it offered a restart.
static Bool commitSettings()
{
	Bool restartChange = FALSE;
	if( s_menuChanges )
	{
		for( const auto &change : *s_menuChanges )
		{
			if( change.unset )
				UserSettings::unset( change.id );
			else
				UserSettings::set( change.id, change.value );
		}
		// only what this Accept changed counts: a value changed back to the running one asks nothing
		for( const auto &change : *s_menuChanges )
			if( UserSettings::differsFromStart( change.id ) )
				restartChange = TRUE;
		s_menuChanges.reset();
	}
	UserSettings::save();
	OptionsMenuTabs::applySaved();
	if( restartChange )
		offerRestart();
	return restartChange;
}

static void discardSettings()
{
	s_menuChanges.reset();
}

static void setDefaults( void )
{
	//-------------------------------------------------------------------------------------------------
	// provider type
//	GadgetCheckBoxSetChecked(checkAudioHardware, FALSE);

	//-------------------------------------------------------------------------------------------------
	// speaker type
//	GadgetCheckBoxSetChecked(checkAudioSurround, FALSE);

	//-------------------------------------------------------------------------------------------------
	// language filter
	GadgetCheckBoxSetChecked( checkLanguageFilter, TRUE );

	//-------------------------------------------------------------------------------------------------
	// LOD
	if ((TheGameLogic->isInGame() == FALSE) || (TheGameLogic->isInShellGame() == TRUE)) {
		StaticGameLODLevel level=TheGameLODManager->findStaticLODLevel();
		switch (level)
		{
		case STATIC_GAME_LOD_LOW:
			GadgetComboBoxSetSelectedPos(comboBoxDetail, LOWDETAIL);
			break;
		case STATIC_GAME_LOD_MEDIUM:
			GadgetComboBoxSetSelectedPos(comboBoxDetail, MEDIUMDETAIL);
			break;
		case STATIC_GAME_LOD_HIGH:
			GadgetComboBoxSetSelectedPos(comboBoxDetail, HIGHDETAIL);
			break;
		case STATIC_GAME_LOD_VERY_HIGH:
			GadgetComboBoxSetSelectedPos(comboBoxDetail, VERYHIGHDETAIL);
			break;
		case STATIC_GAME_LOD_CUSTOM:
			GadgetComboBoxSetSelectedPos(comboBoxDetail, CUSTOMDETAIL);
			break;
		default:
			DEBUG_ASSERTCRASH(FALSE,("Tried to set comboBoxDetail to a value of %d ", TheGameLODManager->getStaticLODLevel()) );
		};
	}
	
	//-------------------------------------------------------------------------------------------------
	// Resolution
	//Find index of 800x600 mode.
	if ((TheGameLogic->isInGame() == FALSE) || (TheGameLogic->isInShellGame() == TRUE)) {
		Int numResolutions = TheDisplay->getDisplayModeCount();
		Int defaultResIndex=0;
		for( Int i = 0; i < numResolutions; ++i )
		{	Int xres,yres,bitDepth;
			TheDisplay->getDisplayModeDescription(i,&xres,&yres,&bitDepth);
			if (xres == 800 && yres == 600)	//keep track of default mode in case we need it.
			{	defaultResIndex=i;
				break;
			}
		}
		GadgetComboBoxSetSelectedPos( comboBoxResolution, defaultResIndex );	//should be 800x600 (our lowest supported mode)
	}


	//-------------------------------------------------------------------------------------------------
	// Mouse Mode
	GadgetCheckBoxSetChecked(checkAlternateMouse, FALSE);
	if (checkRetaliation)	// created at run time in Generals; its default stays off there
	GadgetCheckBoxSetChecked(checkRetaliation, isGameEditionZeroHour() );
	if (checkDoubleClickAttackMove)
	GadgetCheckBoxSetChecked( checkDoubleClickAttackMove, FALSE );

	//-------------------------------------------------------------------------------------------------
//	// scroll speed val
	Int valMin, valMax;
//	GadgetSliderGetMinMax(sliderScrollSpeed,&valMin, &valMax);
//	GadgetSliderSetPosition(sliderScrollSpeed, ((valMax - valMin) / 2 + valMin));
	Int scrollPos = static_cast<Int>(TheGlobalData->m_keyboardDefaultScrollFactor*100.0f + 0.5f);
	GadgetSliderSetPosition( sliderScrollSpeed, scrollPos );


	//-------------------------------------------------------------------------------------------------
	// slider music volume
	GadgetSliderGetMinMax(sliderMusicVolume,&valMin, &valMax);
	GadgetSliderSetPosition(sliderMusicVolume,REAL_TO_INT(TheAudio->getAudioSettings()->m_defaultMusicVolume * 100.0f));

	//-------------------------------------------------------------------------------------------------
	// slider SFX volume
	GadgetSliderGetMinMax(sliderSFXVolume,&valMin, &valMax);
	Real maxVolume = MAX( TheAudio->getAudioSettings()->m_defaultSoundVolume, TheAudio->getAudioSettings()->m_default3DSoundVolume );
	GadgetSliderSetPosition( sliderSFXVolume, REAL_TO_INT( maxVolume * 100.0f ) );

	//-------------------------------------------------------------------------------------------------
	// slider Voice volume
	GadgetSliderGetMinMax(sliderVoiceVolume,&valMin, &valMax);
	GadgetSliderSetPosition(sliderVoiceVolume, REAL_TO_INT(TheAudio->getAudioSettings()->m_defaultSpeechVolume * 100.0f));

	//-------------------------------------------------------------------------------------------------
 	// slider Gamma
 	GadgetSliderGetMinMax(sliderGamma,&valMin, &valMax);
 	GadgetSliderSetPosition(sliderGamma, ((valMax - valMin) / 2 + valMin));

	//-------------------------------------------------------------------------------------------------
 	// Texture resolution slider
	//

	if ((TheGameLogic->isInGame() == FALSE) || (TheGameLogic->isInShellGame() == TRUE))
	{	
		Int	txtFact=TheGameLODManager->getRecommendedTextureReduction();

		GadgetSliderSetPosition( sliderTextureResolution, 2-txtFact);

		//-------------------------------------------------------------------------------------------------
 		// 3D Shadows checkbox
		//
		GadgetCheckBoxSetChecked( check3DShadows, TheGlobalData->m_useShadowVolumes);

		//-------------------------------------------------------------------------------------------------
 		// 2D Shadows checkbox
		//
		GadgetCheckBoxSetChecked( check2DShadows, TheGlobalData->m_useShadowDecals);

		//-------------------------------------------------------------------------------------------------
 		// Cloud Shadows checkbox
		//
		GadgetCheckBoxSetChecked( checkCloudShadows, TheGlobalData->m_useCloudMap);

		//-------------------------------------------------------------------------------------------------
 		// Ground Lighting (lightmap) checkbox
		//
		GadgetCheckBoxSetChecked( checkGroundLighting, TheGlobalData->m_useLightMap);

		//-------------------------------------------------------------------------------------------------
 		// Smooth Water Border checkbox
		//
		GadgetCheckBoxSetChecked( checkSmoothWater, TheGlobalData->m_showSoftWaterEdge);

		//-------------------------------------------------------------------------------------------------
 		// Extra Animations (tree sway and buildups) checkbox
		//
		GadgetCheckBoxSetChecked( checkExtraAnimations, !TheGlobalData->m_useDrawModuleLOD);

		//-------------------------------------------------------------------------------------------------
 		// DisableDynamicLOD
		//
		GadgetCheckBoxSetChecked( checkNoDynamicLod, !TheGlobalData->m_enableDynamicLOD);

		//-------------------------------------------------------------------------------------------------
 		// Disable FPS Limit
		//
		GadgetCheckBoxSetChecked( checkUnlockFps, !TheGlobalData->m_useFpsLimit);

		//-------------------------------------------------------------------------------------------------
 		// Heat Effects
		//
		if (checkHeatEffects)	// missing in the Generals OptionsMenu.wnd
		GadgetCheckBoxSetChecked( checkHeatEffects, TheGlobalData->m_useHeatEffects);

		//-------------------------------------------------------------------------------------------------
 		// Building Occlusion checkbox
		//
		GadgetCheckBoxSetChecked( checkBuildingOcclusion, TheGlobalData->m_enableBehindBuildingMarkers);

		//-------------------------------------------------------------------------------------------------
 		// Particle Cap slider
		//
		GadgetSliderSetPosition( sliderParticleCap, TheGlobalData->m_maxParticleCount);

		//-------------------------------------------------------------------------------------------------
 		// Trees and Shrubs
		//
		GadgetCheckBoxSetChecked( checkProps, TheGlobalData->m_useTrees);
	}

	OptionsMenuTabs::setDefaults();
}

static void saveOptions( void )
{
	// First: a new resolution rebuilds the shell below, which destroys the menu and its tabs.
	OptionsMenuTabs::stageChanges( stageChange );

	Int index;
	Int val;
	//-------------------------------------------------------------------------------------------------
//	// provider type
//	Bool isChecked = GadgetCheckBoxIsChecked(checkAudioHardware);
//	TheAudio->setHardwareAccelerated(isChecked);
//	(*pref)["3DAudioProvider"] = TheAudio->getProviderName(TheAudio->getSelectedProvider());
//
	//-------------------------------------------------------------------------------------------------
//	// speaker type
	//	isChecked = GadgetCheckBoxIsChecked(checkAudioSurround);
	//	TheAudio->setSpeakerSurround(isChecked);
	//	(*pref)["SpeakerType"] = TheAudio->translateUnsignedIntToSpeakerType(TheAudio->getSpeakerType());
	//
	//-------------------------------------------------------------------------------------------------
	// language filter
	if( GadgetCheckBoxIsChecked( checkLanguageFilter ) )
	{
			//GadgetCheckBoxSetChecked( checkLanguageFilter, true);
			TheWritableGlobalData->m_languageFilterPref = true;
	}
	else
	{
			//GadgetCheckBoxSetChecked( checkLanguageFilter, false);
			TheWritableGlobalData->m_languageFilterPref = false;
	}
	

	//-------------------------------------------------------------------------------------------------
	// Custom game detail settings.
	GadgetComboBoxGetSelectedPos( comboBoxDetail, &index );
	if (index == CUSTOMDETAIL)
	{
 		//-------------------------------------------------------------------------------------------------
 		// Texture resolution slider
		{
				AsciiString prefString;

		 		val = GadgetSliderGetPosition(sliderTextureResolution);
				val = 2-val;

				prefString.format("%d",val);
				stageSetting( "Detail.TextureReduction", prefString );

				if (TheGlobalData->m_textureReductionFactor != val)
				{
					TheGameClient->adjustLOD(val-TheGlobalData->m_textureReductionFactor);	//apply the new setting
				}
		}

		TheWritableGlobalData->m_useShadowVolumes = GadgetCheckBoxIsChecked( check3DShadows );
		stageFlag( "Detail.UseShadowVolumes", TheWritableGlobalData->m_useShadowVolumes );

		TheWritableGlobalData->m_useShadowDecals = GadgetCheckBoxIsChecked( check2DShadows );
		stageFlag( "Detail.UseShadowDecals", TheWritableGlobalData->m_useShadowDecals );

		TheWritableGlobalData->m_useCloudMap = GadgetCheckBoxIsChecked( checkCloudShadows );
		stageFlag( "Detail.UseCloudMap", TheGlobalData->m_useCloudMap );

		TheWritableGlobalData->m_useLightMap = GadgetCheckBoxIsChecked( checkGroundLighting );
		stageFlag( "Detail.UseLightMap", TheGlobalData->m_useLightMap );

		TheWritableGlobalData->m_showSoftWaterEdge = GadgetCheckBoxIsChecked( checkSmoothWater );
		stageFlag( "Detail.ShowSoftWaterEdge", TheGlobalData->m_showSoftWaterEdge );

		TheWritableGlobalData->m_useDrawModuleLOD = !GadgetCheckBoxIsChecked( checkExtraAnimations );
		TheWritableGlobalData->m_useTreeSway = !TheWritableGlobalData->m_useDrawModuleLOD;	//borrow same setting.
		stageFlag( "Detail.ExtraAnimations", !TheGlobalData->m_useDrawModuleLOD );

		TheWritableGlobalData->m_enableDynamicLOD = !GadgetCheckBoxIsChecked( checkNoDynamicLod );
		stageFlag( "Detail.DynamicLOD", TheGlobalData->m_enableDynamicLOD );

		if (checkHeatEffects)	// missing in the Generals OptionsMenu.wnd
		{
		TheWritableGlobalData->m_useHeatEffects = GadgetCheckBoxIsChecked( checkHeatEffects );
		stageFlag( "Detail.HeatEffects", TheGlobalData->m_useHeatEffects );
		}

		// Never write this out
		//TheWritableGlobalData->m_useFpsLimit = !GadgetCheckBoxIsChecked( checkUnlockFps );
		//(*pref)["FPSLimit"] = TheGlobalData->m_useFpsLimit ? AsciiString("yes") : AsciiString("no");

		TheWritableGlobalData->m_enableBehindBuildingMarkers = GadgetCheckBoxIsChecked( checkBuildingOcclusion );
		stageFlag( "Detail.BuildingOcclusion", TheWritableGlobalData->m_enableBehindBuildingMarkers );

		TheWritableGlobalData->m_useTrees = GadgetCheckBoxIsChecked( checkProps);
		stageFlag( "Detail.ShowTrees", TheWritableGlobalData->m_useTrees );

 		//-------------------------------------------------------------------------------------------------
		// Particle Cap slider
		{
				AsciiString prefString;

		 		val = GadgetSliderGetPosition(sliderParticleCap);

				prefString.format("%d",val);
				stageSetting( "Detail.MaxParticleCount", prefString );

				TheWritableGlobalData->m_maxParticleCount = val;
		}
	}

	//-------------------------------------------------------------------------------------------------
	// LOD
	Bool levelChanged=FALSE;
	GadgetComboBoxGetSelectedPos( comboBoxDetail, &index );
	//The levels stored by the LOD Manager are inverted compared to GUI so find correct one:
	switch (index) {
	case VERYHIGHDETAIL:
		levelChanged=TheGameLODManager->setStaticLODLevel(STATIC_GAME_LOD_VERY_HIGH);
		break;
	case HIGHDETAIL:
		levelChanged=TheGameLODManager->setStaticLODLevel(STATIC_GAME_LOD_HIGH);
		break;
	case MEDIUMDETAIL:
		levelChanged=TheGameLODManager->setStaticLODLevel(STATIC_GAME_LOD_MEDIUM);
		break;
	case LOWDETAIL:
		levelChanged=TheGameLODManager->setStaticLODLevel(STATIC_GAME_LOD_LOW);
		break;
	case CUSTOMDETAIL:
		levelChanged=TheGameLODManager->setStaticLODLevel(STATIC_GAME_LOD_CUSTOM);
		break;
	default:
		DEBUG_ASSERTCRASH(FALSE,("LOD passed in was %d, %d is not a supported LOD",index,index));
		break;
	}

	detailLevelChanged = levelChanged && index != CUSTOMDETAIL;
	if (levelChanged)
	        stageSetting( "Detail.StaticGameLOD", AsciiString( TheGameLODManager->getStaticGameLODLevelName(TheGameLODManager->getStaticLODLevel()) ) );

	//-------------------------------------------------------------------------------------------------
	// Resolution
	GadgetComboBoxGetSelectedPos( comboBoxResolution, &index );
	Int xres, yres, bitDepth;
	
	oldDispSettings.xRes = TheDisplay->getWidth();
	oldDispSettings.yRes = TheDisplay->getHeight();
	oldDispSettings.bitDepth = TheDisplay->getBitDepth();
	oldDispSettings.windowed = TheDisplay->getWindowed();
	
	if (index < TheDisplay->getDisplayModeCount() && index >= 0)
	{
		TheDisplay->getDisplayModeDescription(index,&xres,&yres,&bitDepth);
		// Compare against the actual display, not TheGlobalData: that holds the Options.ini value, so
		// selecting the saved but inactive resolution again would have no effect.
		if (static_cast<Int>(TheDisplay->getWidth()) != xres || static_cast<Int>(TheDisplay->getHeight()) != yres)
		{
			
			if (TheDisplay->setDisplayMode(xres,yres,bitDepth,TheDisplay->getWindowed()))
			{
				dispChanged = TRUE;
				TheWritableGlobalData->m_xResolution = xres;
				TheWritableGlobalData->m_yResolution = yres;

				TheHeaderTemplateManager->headerNotifyResolutionChange();
				TheMouse->mouseNotifyResolutionChange();
				
				//Save new settings for a dialog box confirmation after options are accepted
				newDispSettings.xRes = xres;
				newDispSettings.yRes = yres;
				newDispSettings.bitDepth = bitDepth;
				newDispSettings.windowed = TheDisplay->getWindowed();

				AsciiString prefString;
				prefString.format("%d %d", xres, yres );
				stageSetting( "Display.Resolution", prefString );

				// ~Shell calls the options menu shutdown, which drops the staged changes, in the middle of
				// saveOptions. Keep them across the shell rebuild.
				std::unique_ptr<MenuChanges> staged = std::move( s_menuChanges );

				// delete the shell
				delete TheShell;
				::Engine::context().shell = nullptr;

				s_menuChanges = std::move( staged );

				// create the shell
				::Engine::context().shell = MSGNEW("GameClientSubsystem") Shell;
				if( TheShell )
					TheShell->init();
				
				TheInGameUI->recreateControlBar();

				TheShell->push( AsciiString("Menus/MainMenu.wnd") );
			}
		}
	}


	//-------------------------------------------------------------------------------------------------
	// antialiasing
  GadgetComboBoxGetSelectedPos(comboBoxAntiAliasing, &index);
  if( index >= 0 && TheGlobalData->m_antiAliasBoxValue != index )
  {
    TheWritableGlobalData->m_antiAliasBoxValue = index;
    AsciiString prefString;
		prefString.format("%d", index);
		stageSetting( "Display.AntiAliasing", prefString );
  }


	//-------------------------------------------------------------------------------------------------
	// mouse mode
	TheWritableGlobalData->m_useAlternateMouse = GadgetCheckBoxIsChecked(checkAlternateMouse);
	stageFlag( "Controls.UseAlternateMouse", TheWritableGlobalData->m_useAlternateMouse );

	// Without the windows (the Generals OptionsMenu.wnd lacks the double-click box, and the
	// retaliation box when there was no room for it) the values stay and are not written.
	if (checkRetaliation)
	{
	TheWritableGlobalData->m_clientRetaliationModeEnabled = GadgetCheckBoxIsChecked(checkRetaliation);
	stageFlag( "Controls.Retaliation", TheWritableGlobalData->m_clientRetaliationModeEnabled );
	}

	if (checkDoubleClickAttackMove)
	{
	TheWritableGlobalData->m_doubleClickAttackMove = GadgetCheckBoxIsChecked( checkDoubleClickAttackMove );
	stageFlag( "Controls.UseDoubleClickAttackMove", TheWritableGlobalData->m_doubleClickAttackMove );
	}

	//-------------------------------------------------------------------------------------------------
	// scroll speed val
	val = GadgetSliderGetPosition(sliderScrollSpeed);
	if(val != -1)
	{
		TheWritableGlobalData->m_keyboardScrollFactor = val/100.0f;
		DEBUG_LOG(("Scroll Spped val %d, keyboard scroll factor %f\n", val, TheGlobalData->m_keyboardScrollFactor));
		AsciiString prefString;
		prefString.format("%d", val);
		stageSetting( "Controls.ScrollFactor", prefString );
	}
	
	//-------------------------------------------------------------------------------------------------
	// slider music volume
	val = GadgetSliderGetPosition(sliderMusicVolume);
	if(val != -1)
	{
	  TheWritableGlobalData->m_musicVolumeFactor = static_cast<Real>(val);
    AsciiString prefString;
    prefString.format("%d", val);
    stageSetting( "Audio.MusicVolume", prefString );
    TheAudio->setVolume(val / 100.0f, static_cast<AudioAffect>(AudioAffect_Music | AudioAffect_SystemSetting));
	}
	
	//-------------------------------------------------------------------------------------------------
	// slider SFX volume
	val = GadgetSliderGetPosition(sliderSFXVolume);
	if(val != -1)
	{
		//Both 2D and 3D sound effects are sharing the same slider. However, there is a 
		//relative slider that gets applied to one of these values to lower that sound volume.
		Real sound2DVolume = val / 100.0f;
		Real sound3DVolume = val / 100.0f;
		Real relative2DVolume = TheAudio->getAudioSettings()->m_relative2DVolume;
		relative2DVolume = static_cast<Real>(MIN( 1.0f, MAX( -1.0, relative2DVolume ) ));
		if( relative2DVolume < 0.0f )
		{
			//Lower the 2D volume
			sound2DVolume *= 1.0f + relative2DVolume;
		}
		else
		{
			//Lower the 3D volume
			sound3DVolume *= 1.0f - relative2DVolume;
		}

		//Apply the sound volumes in the audio system now.
    TheAudio->setVolume( sound2DVolume, static_cast<AudioAffect>(AudioAffect_Sound | AudioAffect_SystemSetting) );
		TheAudio->setVolume( sound3DVolume, static_cast<AudioAffect>(AudioAffect_Sound3D | AudioAffect_SystemSetting) );

		//Save the settings in the options.ini.
    TheWritableGlobalData->m_SFXVolumeFactor = static_cast<Real>(val);
    AsciiString prefString;
    prefString.format("%d", REAL_TO_INT( sound2DVolume * 100.0f ) );
    stageSetting( "Audio.SFXVolume", prefString );
    prefString.format("%d", REAL_TO_INT( sound3DVolume * 100.0f ) );
		stageSetting( "Audio.SFX3DVolume", prefString );
	}

	//-------------------------------------------------------------------------------------------------
	// slider Voice volume
	val = GadgetSliderGetPosition(sliderVoiceVolume);
	if(val != -1)
	{
    TheWritableGlobalData->m_voiceVolumeFactor = static_cast<Real>(val);
    AsciiString prefString;
    prefString.format("%d", val);
    stageSetting( "Audio.VoiceVolume", prefString );
    TheAudio->setVolume(val / 100.0f, static_cast<AudioAffect>(AudioAffect_Speech | AudioAffect_SystemSetting));
	}

 	//-------------------------------------------------------------------------------------------------
 	// slider Gamma
 	val = GadgetSliderGetPosition(sliderGamma);
 	if(val != -1 && !UserSettings::isForced( "Display.Gamma" ))
 	{
		Real gammaval=1.0f;
		//generate a value between 0.6 and 2.0.
		if (val < 50)
		{	//darker gamma
			if (val <= 0)
				gammaval = 0.6f;
			else
				gammaval=1.0f-(0.4f) * static_cast<Real>(50-val)/50.0f;
		}
		else
		if (val > 50)
			gammaval=1.0f+(1.0f) * static_cast<Real>(val-50)/50.0f;

 		AsciiString prefString;
 		prefString.format("%d", val);
 		stageSetting( "Display.Gamma", prefString );

		if (TheGlobalData->m_displayGamma != gammaval)
		{	TheWritableGlobalData->m_displayGamma = gammaval;
			TheDisplay->setGamma(TheGlobalData->m_displayGamma,0.0f, 1.0f, FALSE);
		}
 	}
}

static void DestroyOptionsLayout() {

	SignalUIInteraction(SHELL_SCRIPT_HOOK_OPTIONS_CLOSED);
	OptionsMenuTabs::release();

	TheShell->destroyOptionsLayout();
	OptionsLayout = nullptr;
	MainMenuShowAfterOptions();
	QuitMenuShowAfterOptions();
}

static void showAdvancedOptions()
{
	if( OptionsMenuTabs::isBuilt() )
		OptionsMenuTabs::showPage( OptionsMenuTabs::PAGE_DETAILS );
	else
		WinAdvancedDisplay->winHide(FALSE);
}

// The detail page shows the values of the level chosen in the box; they only become the user's
// own when one of them is changed, which selects "custom".
static void showDetailLevel( Int index )
{
	StaticGameLODLevel level;
	switch( index )
	{
	case VERYHIGHDETAIL:	level = STATIC_GAME_LOD_VERY_HIGH; break;
	case HIGHDETAIL:			level = STATIC_GAME_LOD_HIGH; break;
	case MEDIUMDETAIL:		level = STATIC_GAME_LOD_MEDIUM; break;
	case LOWDETAIL:				level = STATIC_GAME_LOD_LOW; break;
	default:							return;
	}
	const StaticGameLODInfo &info = TheGameLODManager->m_staticGameLODInfo[ level ];
	// Setting a check box or slider notifies the menu like a click would; that must not turn the level into "custom".
	const Bool wasIgnoring = ignoreSelected;
	ignoreSelected = TRUE;
	GadgetSliderSetPosition( sliderTextureResolution, 2 - TheGameLODManager->getRecommendedTextureReduction() );
	GadgetCheckBoxSetChecked( check3DShadows, info.m_useShadowVolumes );
	GadgetCheckBoxSetChecked( check2DShadows, info.m_useShadowDecals );
	GadgetCheckBoxSetChecked( checkCloudShadows, info.m_useCloudMap );
	GadgetCheckBoxSetChecked( checkGroundLighting, info.m_useLightMap );
	GadgetCheckBoxSetChecked( checkSmoothWater, info.m_showSoftWaterEdge );
	GadgetCheckBoxSetChecked( checkExtraAnimations, info.m_useBuildupScaffolds );
	GadgetCheckBoxSetChecked( checkNoDynamicLod, !info.m_enableDynamicLOD );
	if( checkHeatEffects )
		GadgetCheckBoxSetChecked( checkHeatEffects, info.m_useHeatEffects );
	GadgetCheckBoxSetChecked( checkUnlockFps, !info.m_useFpsLimit );
	GadgetCheckBoxSetChecked( checkProps, TRUE );
	GadgetSliderSetPosition( sliderParticleCap, info.m_maxParticleCount );
	ignoreSelected = wasIgnoring;
}

// A change on the detail page makes the detail level custom.
static void markCustomDetail()
{
	Int index = -1;
	GadgetComboBoxGetSelectedPos( comboBoxDetail, &index );
	if( index == CUSTOMDETAIL )
		return;
	const Bool wasIgnoring = ignoreSelected;
	ignoreSelected = TRUE;
	GadgetComboBoxSetSelectedPos( comboBoxDetail, CUSTOMDETAIL );
	ignoreSelected = wasIgnoring;
}

static Bool isDetailControl( Int controlID )
{
	return controlID == check3DShadowsID || controlID == check2DShadowsID || controlID == checkCloudShadowsID
		|| controlID == checkGroundLightingID || controlID == checkSmoothWaterID || controlID == checkExtraAnimationsID
		|| controlID == checkNoDynamicLodID || controlID == checkHeatEffectsID || controlID == checkUnlockFpsID
		|| controlID == checkBuildingOcclusionID || controlID == checkPropsID
		|| controlID == sliderTextureResolutionID || controlID == sliderParticleCapID;
}

static void acceptAdvancedOptions()
{
	WinAdvancedDisplay->winHide(TRUE);
}

static void cancelAdvancedOptions()
{
	//restore the detail selection back to initial state
	switch (TheGameLODManager->getStaticLODLevel())
	{
	case STATIC_GAME_LOD_LOW:
		GadgetComboBoxSetSelectedPos(comboBoxDetail, LOWDETAIL);
		break;
	case STATIC_GAME_LOD_MEDIUM:
		GadgetComboBoxSetSelectedPos(comboBoxDetail, MEDIUMDETAIL);
		break;
	case STATIC_GAME_LOD_HIGH:
		GadgetComboBoxSetSelectedPos(comboBoxDetail, HIGHDETAIL);
		break;
	case STATIC_GAME_LOD_VERY_HIGH:
		GadgetComboBoxSetSelectedPos(comboBoxDetail, VERYHIGHDETAIL);
		break;
	case STATIC_GAME_LOD_CUSTOM:
		GadgetComboBoxSetSelectedPos(comboBoxDetail, CUSTOMDETAIL);
		break;
	default:
		DEBUG_ASSERTCRASH(FALSE,("Tried to set comboBoxDetail to a value of %d ", TheGameLODManager->getStaticLODLevel()) );
	};

	WinAdvancedDisplay->winHide(TRUE);
}
//-------------------------------------------------------------------------------------------------
/** The network part of the window file (LAN address, online address, proxy, firewall) has no
    function: the lobby uses every adapter, and the online service is gone. Its controls and their
    labels are hidden, found by the names the window file gives them. */
//-------------------------------------------------------------------------------------------------
static void hideNetworkSection( GameWindow *window )
{
	static const char *const parts[] = { "IP", "Firewall", "Proxy", "SendDelay", "Network" };
	for( GameWindow *child = window->winGetChild(); child; child = child->winGetNext() )
	{
		hideNetworkSection( child );
		const AsciiString &name = child->winGetInstanceData()->m_decoratedNameString;
		for( const char *part : parts )
		{
			if( strstr( name.str(), part ) != nullptr )
			{
				DEBUG_LOG(( "OptionsMenu - hiding %s\n", name.str() ));
				child->winHide( TRUE );
				child->winEnable( FALSE );
				break;
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Initialize the options menu */
//-------------------------------------------------------------------------------------------------
void OptionsMenuInit( WindowLayout *layout, void *userData )
{
	ignoreSelected = TRUE;
	if (TheGameEngine->getQuitting())
		return;
	OptionsLayout = layout;
	discardSettings();

	SignalUIInteraction(SHELL_SCRIPT_HOOK_OPTIONS_OPENED);

	checkAlternateMouseID  = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckAlternateMouse" ) );
	checkAlternateMouse	   = TheWindowManager->winGetWindowFromId( nullptr, checkAlternateMouseID);
	checkRetaliationID		 = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:Retaliation" ) );
	checkRetaliation	     = TheWindowManager->winGetWindowFromId( nullptr, checkRetaliationID);
	// The Generals OptionsMenu.wnd has no retaliation box; add one next to the mouse setting.
	if( checkRetaliation == nullptr && isGameEditionGenerals() )
		checkRetaliation = GadgetCheckBoxCreateBelow( checkAlternateMouse, "OptionsMenu.wnd:Retaliation", "GZH:OptionsRetaliation" );
	checkDoubleClickAttackMoveID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckDoubleClickAttackMove" ) );
	checkDoubleClickAttackMove   = TheWindowManager->winGetWindowFromId( nullptr, checkDoubleClickAttackMoveID );
	sliderScrollSpeedID	   = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:SliderScrollSpeed" ) );
	sliderScrollSpeed		   = TheWindowManager->winGetWindowFromId( nullptr,  sliderScrollSpeedID);
	// The layout data range 10..145 does not match the stored setting (0..100); 0 stands still.
	if (sliderScrollSpeed)
		TheWindowManager->winSendSystemMsg( sliderScrollSpeed, GSM_SET_MIN_MAX, 0, 100 );
	comboBoxAntiAliasingID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:ComboBoxAntiAliasing" ) );
	comboBoxAntiAliasing   = TheWindowManager->winGetWindowFromId( nullptr, comboBoxAntiAliasingID );
	comboBoxResolutionID   = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:ComboBoxResolution" ) );
	comboBoxResolution     = TheWindowManager->winGetWindowFromId( nullptr, comboBoxResolutionID );
	comboBoxDetailID			 = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:ComboBoxDetail" ) );
	comboBoxDetail		   = TheWindowManager->winGetWindowFromId( nullptr, comboBoxDetailID );

	checkLanguageFilterID  = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckLanguageFilter" ) );
	checkLanguageFilter    = TheWindowManager->winGetWindowFromId( nullptr, checkLanguageFilterID );
	checkDrawAnchorID       = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckBoxDrawAnchor" ) );
	checkDrawAnchor				 = TheWindowManager->winGetWindowFromId( nullptr, checkDrawAnchorID);
	checkMoveAnchorID       = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckBoxMoveAnchor" ) );
	checkMoveAnchor				 = TheWindowManager->winGetWindowFromId( nullptr, checkMoveAnchorID);

	// Replay camera
	checkSaveCameraID      = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckBoxSaveCamera" ) );
	checkSaveCamera        = TheWindowManager->winGetWindowFromId( nullptr, checkSaveCameraID );
	checkUseCameraID       = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckBoxUseCamera" ) );
	checkUseCamera         = TheWindowManager->winGetWindowFromId( nullptr, checkUseCameraID );

//	// Speakers and 3-D Audio
//	checkAudioSurroundID   = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckAudioSurround" ) );
//	checkAudioSurround     = TheWindowManager->winGetWindowFromId( NULL, checkAudioSurroundID );
//	checkAudioHardwareID   = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckAudioHardware" ) );
//	checkAudioHardware     = TheWindowManager->winGetWindowFromId( NULL, checkAudioHardwareID );
//
	// Volume Controls
	sliderMusicVolumeID    = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:SliderMusicVolume" ) );
	sliderMusicVolume      = TheWindowManager->winGetWindowFromId( nullptr, sliderMusicVolumeID );
	sliderSFXVolumeID      = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:SliderSFXVolume" ) );
	sliderSFXVolume        = TheWindowManager->winGetWindowFromId( nullptr, sliderSFXVolumeID );
	sliderVoiceVolumeID    = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:SliderVoiceVolume" ) );
	sliderVoiceVolume      = TheWindowManager->winGetWindowFromId( nullptr, sliderVoiceVolumeID );
 	sliderGammaID    = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:SliderGamma" ) );
 	sliderGamma      = TheWindowManager->winGetWindowFromId( nullptr, sliderGammaID );

//	checkBoxLowTextureDetailID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckLowTextureDetail" ) );
//	checkBoxLowTextureDetail      = TheWindowManager->winGetWindowFromId( NULL, checkBoxLowTextureDetailID );
	
	WinAdvancedDisplayID		= TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:WinAdvancedDisplayOptions" ) );
	WinAdvancedDisplay      = TheWindowManager->winGetWindowFromId( nullptr, WinAdvancedDisplayID );

	ButtonAdvancedAcceptID		= TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:ButtonAdvanceAccept" ) );
	ButtonAdvancedAccept      = TheWindowManager->winGetWindowFromId( nullptr, ButtonAdvancedAcceptID );

	ButtonAdvancedCancelID		= TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:ButtonAdvanceBack" ) );
	ButtonAdvancedCancel      = TheWindowManager->winGetWindowFromId( nullptr, ButtonAdvancedCancelID );

	sliderTextureResolutionID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:LowResSlider" ) );
	sliderTextureResolution = TheWindowManager->winGetWindowFromId( nullptr, sliderTextureResolutionID );

	check3DShadowsID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:Check3DShadows" ) );
	check3DShadows   = TheWindowManager->winGetWindowFromId( nullptr, check3DShadowsID);

	check2DShadowsID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:Check2DShadows" ) );
	check2DShadows   = TheWindowManager->winGetWindowFromId( nullptr, check2DShadowsID);

	checkCloudShadowsID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckCloudShadows" ) );
	checkCloudShadows   = TheWindowManager->winGetWindowFromId( nullptr, checkCloudShadowsID);

	checkGroundLightingID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckGroundLighting" ) );
	checkGroundLighting   = TheWindowManager->winGetWindowFromId( nullptr, checkGroundLightingID);

	checkSmoothWaterID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckSmoothWater" ) );
	checkSmoothWater   = TheWindowManager->winGetWindowFromId( nullptr, checkSmoothWaterID);

	checkExtraAnimationsID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckExtraAnimations" ) );
	checkExtraAnimations   = TheWindowManager->winGetWindowFromId( nullptr, checkExtraAnimationsID);

	checkNoDynamicLodID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckNoDynamicLOD" ) );
	checkNoDynamicLod   = TheWindowManager->winGetWindowFromId( nullptr, checkNoDynamicLodID);

	checkHeatEffectsID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckHeatEffects" ) );
	checkHeatEffects   = TheWindowManager->winGetWindowFromId( nullptr, checkHeatEffectsID);

	checkUnlockFpsID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckUnlockFPS" ) );
	checkUnlockFps   = TheWindowManager->winGetWindowFromId( nullptr, checkUnlockFpsID);

	checkBuildingOcclusionID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckBehindBuilding" ) );
	checkBuildingOcclusion   = TheWindowManager->winGetWindowFromId( nullptr, checkBuildingOcclusionID);

	checkPropsID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckShowProps" ) );
	checkProps   = TheWindowManager->winGetWindowFromId( nullptr, checkPropsID);

	sliderParticleCapID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:ParticleCapSlider" ) );
  sliderParticleCap = TheWindowManager->winGetWindowFromId( nullptr, sliderParticleCapID );

	WinAdvancedDisplay->winHide(TRUE);

	Color color =  GameMakeColor(255,255,255,255);

  enum AliasingMode
  {
    OFF = 0,
    LOW,
    HIGH,
    NUM_ALIASING_MODES
  };

	NameKeyType versionID = TheNameKeyGenerator->nameToKey( AsciiString("OptionsMenu.wnd:LabelVersion") );
	GameWindow *labelVersion = TheWindowManager->winGetWindowFromId( nullptr, versionID );
	// The project and its version; the full form with build data stays behind -fullVersion.
	UnicodeString versionString;
	versionString.format(L"GZH Neo %ls", TheVersion ? TheVersion->getUnicodeVersion().str() : L"");
	
	if (TheVersion->showFullVersion())
	{
		if (TheVersion)
		{
			UnicodeString version;
			version.format(L"(%s) %s -- %s", versionString.str(), TheVersion->getFullUnicodeVersion().str(), TheVersion->getUnicodeBuildTime().str());
			GadgetStaticTextSetText( labelVersion, version );
		}
		else
		{
			labelVersion->winHide( TRUE );
		}
	}
	else
	{
		GadgetStaticTextSetText( labelVersion, versionString );
	}



	UnicodeString str;
	Int index;

	// populate anti aliasing modes
	AsciiString selectedAliasingMode = UserSettings::getText( "Display.AntiAliasing", AsciiString::TheEmptyString );
	GadgetComboBoxReset(comboBoxAntiAliasing);
	AsciiString temp;
	Int i;
	for (i=0; i < NUM_ALIASING_MODES; ++i)
	{
		temp.format("GUI:AntiAliasing%d", i);
		str = TheGameText->fetch( temp );
		index = GadgetComboBoxAddEntry(comboBoxAntiAliasing, str, color);
	}
	Int val = atoi(selectedAliasingMode.str());
	if( val < 0 || val >= NUM_ALIASING_MODES )	// >= rejects the invalid index NUM_ALIASING_MODES as well
	{
		TheWritableGlobalData->m_antiAliasBoxValue = val = 0;
	}
	GadgetComboBoxSetSelectedPos(comboBoxAntiAliasing, val);

	// Preselect the active resolution; the Options.ini value is only a fallback while the display
	// reports no valid size.
	AsciiString selectedResolution = UserSettings::getText( "Display.Resolution", AsciiString::TheEmptyString );
	Int selectedXRes=800,selectedYRes=600;
	if (TheDisplay && TheDisplay->getWidth() > 0 && TheDisplay->getHeight() > 0)
		selectedResolution.format("%d %d", TheDisplay->getWidth(), TheDisplay->getHeight());
	Int selectedResIndex=-1;
	Int defaultResIndex=0;	//index of default video mode that should always exist
	if (!selectedResolution.isEmpty())
	{	//try to parse 2 integers out of string
		if (sscanf(selectedResolution.str(),"%d%d", &selectedXRes, &selectedYRes) != 2)
		{	selectedXRes=800; selectedYRes=600;
		}
	}

	// populate resolution modes
	GadgetComboBoxReset(comboBoxResolution);
	Int numResolutions = TheDisplay->getDisplayModeCount();
	for( i = 0; i < numResolutions; ++i )
	{	Int xres,yres,bitDepth;
		TheDisplay->getDisplayModeDescription(i,&xres,&yres,&bitDepth);
		// Show the aspect ratio, since the list contains non-4:3 modes.
		const WideChar *aspectLabel = L"";
		if (yres > 0)
		{
			const Real aspect = static_cast<Real>(xres) / static_cast<Real>(yres);
			if (fabs(aspect - 4.0f/3.0f) < 0.02f)				aspectLabel = L"4:3";
			else if (fabs(aspect - 5.0f/4.0f) < 0.02f)		aspectLabel = L"5:4";
			else if (fabs(aspect - 16.0f/10.0f) < 0.02f)	aspectLabel = L"16:10";
			else if (fabs(aspect - 16.0f/9.0f) < 0.02f)		aspectLabel = L"16:9";
			else if (aspect > 2.3f && aspect < 2.45f)			aspectLabel = L"21:9";
			else if (aspect > 3.4f && aspect < 3.6f)			aspectLabel = L"32:9";
		}
		if (aspectLabel[0] != 0)
			str.format(L"%d x %d (%s)",xres,yres,aspectLabel);
		else
			str.format(L"%d x %d",xres,yres);
		GadgetComboBoxAddEntry( comboBoxResolution, str, color);
		if (xres == 800 && yres == 600)	//keep track of default mode in case we need it.
			defaultResIndex=i;
		if (xres == selectedXRes && yres == selectedYRes)
			selectedResIndex=i;
	}

	// The running size is no display mode (a window of its own size): list it, so that "Accept" keeps it.
	if (selectedResIndex == -1 && TheDisplay && TheDisplay->getWidth() > 0 && TheDisplay->getHeight() > 0
			&& static_cast<Int>(TheDisplay->getWidth()) == selectedXRes && static_cast<Int>(TheDisplay->getHeight()) == selectedYRes)
	{
		str.format(L"%d x %d", selectedXRes, selectedYRes);
		selectedResIndex = GadgetComboBoxAddEntry( comboBoxResolution, str, color);
	}

	if (selectedResIndex == -1)	//check if saved mode no longer available
	{	//pick default resolution
		selectedXRes = 800;
		selectedYRes = 600;
		selectedResIndex = defaultResIndex;
	}

	TheWritableGlobalData->m_xResolution = selectedXRes;
	TheWritableGlobalData->m_yResolution = selectedYRes;

	GadgetComboBoxSetSelectedPos( comboBoxResolution, selectedResIndex );

	// set the display detail
	GadgetComboBoxReset(comboBoxDetail);
	// "Ultra" as fourth level. The game's CSF text files have no label for it (no "GUI:VeryHigh"),
	// so the text is set directly.
	GadgetComboBoxAddEntry(comboBoxDetail, UnicodeString(L"Ultra"), color);
	GadgetComboBoxAddEntry(comboBoxDetail, TheGameText->fetch("GUI:High"), color);
	GadgetComboBoxAddEntry(comboBoxDetail, TheGameText->fetch("GUI:Medium"), color);
	GadgetComboBoxAddEntry(comboBoxDetail, TheGameText->fetch("GUI:Low"), color);
	GadgetComboBoxAddEntry(comboBoxDetail, TheGameText->fetch("GUI:Custom"), color);

	//Check if level was never set and default to setting most suitable for system.
	if (TheGameLODManager->getStaticLODLevel() == STATIC_GAME_LOD_UNKNOWN)
		TheGameLODManager->setStaticLODLevel(TheGameLODManager->findStaticLODLevel());

	switch (TheGameLODManager->getStaticLODLevel())
	{
	case STATIC_GAME_LOD_LOW:
		GadgetComboBoxSetSelectedPos(comboBoxDetail, LOWDETAIL);
		break;
	case STATIC_GAME_LOD_MEDIUM:
		GadgetComboBoxSetSelectedPos(comboBoxDetail, MEDIUMDETAIL);
		break;
	case STATIC_GAME_LOD_HIGH:
		GadgetComboBoxSetSelectedPos(comboBoxDetail, HIGHDETAIL);
		break;
	case STATIC_GAME_LOD_VERY_HIGH:
		GadgetComboBoxSetSelectedPos(comboBoxDetail, VERYHIGHDETAIL);
		break;
	case STATIC_GAME_LOD_CUSTOM:
		GadgetComboBoxSetSelectedPos(comboBoxDetail, CUSTOMDETAIL);
		break;
	default:
		DEBUG_ASSERTCRASH(FALSE,("Tried to set comboBoxDetail to a value of %d ", TheGameLODManager->getStaticLODLevel()) );
	};

	Int txtFact=TheGameLODManager->getCurrentTextureReduction();

	GadgetSliderSetPosition( sliderTextureResolution, 2-txtFact);

	GadgetCheckBoxSetChecked( check3DShadows, TheGlobalData->m_useShadowVolumes);

	GadgetCheckBoxSetChecked( check2DShadows, TheGlobalData->m_useShadowDecals);

	GadgetCheckBoxSetChecked( checkCloudShadows, TheGlobalData->m_useCloudMap);

	GadgetCheckBoxSetChecked( checkGroundLighting, TheGlobalData->m_useLightMap);

	GadgetCheckBoxSetChecked( checkSmoothWater, TheGlobalData->m_showSoftWaterEdge);

	GadgetCheckBoxSetChecked( checkExtraAnimations, !TheGlobalData->m_useDrawModuleLOD);

	GadgetCheckBoxSetChecked( checkNoDynamicLod, !TheGlobalData->m_enableDynamicLOD);

	if (checkHeatEffects)	// missing in the Generals OptionsMenu.wnd
	GadgetCheckBoxSetChecked( checkHeatEffects, TheGlobalData->m_useHeatEffects);

	GadgetCheckBoxSetChecked( checkUnlockFps, !TheGlobalData->m_useFpsLimit);

	GadgetCheckBoxSetChecked( checkBuildingOcclusion, TheGlobalData->m_enableBehindBuildingMarkers);

	GadgetCheckBoxSetChecked( checkProps, TheGlobalData->m_useTrees);
	//checkProps->winEnable(false);	//gray out the option for now.

	GadgetSliderSetPosition( sliderParticleCap, TheGlobalData->m_maxParticleCount );

	//set language filter
	// The language filter is not stored: it starts off.
	GadgetCheckBoxSetChecked( checkLanguageFilter, false);
	TheWritableGlobalData->m_languageFilterPref = false;
	
	//set replay camera
	if (UserSettings::saveCameraInReplays())
	{
		GadgetCheckBoxSetChecked( checkSaveCamera, true);
		TheWritableGlobalData->m_saveCameraInReplay = true;
	}
	else
	{
		GadgetCheckBoxSetChecked( checkSaveCamera, false);
		TheWritableGlobalData->m_saveCameraInReplay = false;
	}
	if (UserSettings::useCameraInReplays())
	{
		GadgetCheckBoxSetChecked( checkUseCamera, true);
		TheWritableGlobalData->m_useCameraInReplay = true;
	}
	else
	{
		GadgetCheckBoxSetChecked( checkUseCamera, false);
		TheWritableGlobalData->m_useCameraInReplay = false;
	}

	//set scroll options
	AsciiString test = UserSettings::getText( "Controls.DrawScrollAnchor", AsciiString::TheEmptyString );
	DEBUG_LOG(("DrawScrollAnchor == [%s]\n", test.str()));
	if (test.compareNoCase("yes") == 0 || (test.isEmpty() && TheInGameUI->getDrawRMBScrollAnchor()))
	{
		GadgetCheckBoxSetChecked( checkDrawAnchor, true);
		TheInGameUI->setDrawRMBScrollAnchor(true);
	}
	else
	{
		GadgetCheckBoxSetChecked( checkDrawAnchor, false);
		TheInGameUI->setDrawRMBScrollAnchor(false);
	}
	test = UserSettings::getText( "Controls.MoveScrollAnchor", AsciiString::TheEmptyString );
	DEBUG_LOG(("MoveScrollAnchor == [%s]\n", test.str()));
	if (test.compareNoCase("yes") == 0 || (test.isEmpty() && TheInGameUI->getMoveRMBScrollAnchor()))
	{
		GadgetCheckBoxSetChecked( checkMoveAnchor, true);
		TheInGameUI->setMoveRMBScrollAnchor(true);
	}
	else
	{
		GadgetCheckBoxSetChecked( checkMoveAnchor, false);
		TheInGameUI->setMoveRMBScrollAnchor(false);
	}

//	// Audio Init shiznat
//	GadgetCheckBoxSetChecked(checkAudioHardware, TheAudio->getHardwareAccelerated());
//	GadgetCheckBoxSetChecked(checkAudioSurround, TheAudio->getSpeakerSurround());

	// set the mouse mode
	GadgetCheckBoxSetChecked(checkAlternateMouse, TheGlobalData->m_useAlternateMouse);
	if (checkRetaliation)
	GadgetCheckBoxSetChecked(checkRetaliation, TheGlobalData->m_clientRetaliationModeEnabled);
	if (checkDoubleClickAttackMove)
	GadgetCheckBoxSetChecked( checkDoubleClickAttackMove, TheGlobalData->m_doubleClickAttackMove );

	// set scroll speed slider
	Int scrollPos = static_cast<Int>(TheGlobalData->m_keyboardScrollFactor*100.0f + 0.5f);
	GadgetSliderSetPosition( sliderScrollSpeed, scrollPos );
	DEBUG_LOG(("Scroll SPeed %d\n", scrollPos));

 	// set volume sliders

	// set music volume slider
	GadgetSliderSetPosition( sliderMusicVolume, REAL_TO_INT(UserSettings::getMusicVolume()) );

	//set SFX volume slider
	Real maxVolume = MAX( UserSettings::getSoundVolume(), UserSettings::get3DSoundVolume() );
	GadgetSliderSetPosition( sliderSFXVolume, REAL_TO_INT( maxVolume ) );

	//set voice volume slider
	GadgetSliderSetPosition( sliderVoiceVolume, REAL_TO_INT(UserSettings::getSpeechVolume()) );
	
	// set the gamma slider
 	GadgetSliderSetPosition( sliderGamma, UserSettings::getGammaValue() );
	// -gamma decides for this run; the slider shows it and is locked.
	if( UserSettings::isForced( "Display.Gamma" ) )
		sliderGamma->winEnable( FALSE );

	// show menu
	layout->hide( FALSE );

	// set keyboard focus to main parent
	AsciiString parentName( "OptionsMenu.wnd:OptionsMenuParent" );
	NameKeyType parentID = TheNameKeyGenerator->nameToKey( parentName );
	GameWindow *parent = TheWindowManager->winGetWindowFromId( nullptr, parentID );
	TheWindowManager->winSetFocus( parent );
	if( parent )
	{
		hideNetworkSection( parent );
		OptionsMenuTabs::build( parent );
	}

	if( (TheGameLogic->isInGame() && TheGameLogic->getGameMode() != GAME_SHELL) )
	{
		// disable controls that you can't change the options for in game
		if (comboBoxDetail)
			comboBoxDetail->winEnable(FALSE);


		if (comboBoxResolution)
			comboBoxResolution->winEnable(FALSE);

//		if (checkAudioSurround)
//			checkAudioSurround->winEnable(FALSE);
//
//		if (checkAudioHardware)
//			checkAudioHardware->winEnable(FALSE);
	}


	TheWindowManager->winSetModal(parent);
	ignoreSelected = FALSE;
}  // end OptionsMenuInit

//-------------------------------------------------------------------------------------------------
/** options menu shutdown method */
//-------------------------------------------------------------------------------------------------
void OptionsMenuShutdown( WindowLayout *layout, void *userData )
{
	// When the program exits with the options menu open, ~Shell destroys the layout without "Back" or
	// "Accept" having run. Discard the changes without saving, like "Back".
	discardSettings();
	OptionsMenuTabs::release();

/*
	comboBoxIP = NULL;

	// hide menu
	layout->hide( TRUE );

	// our shutdown is complete
	TheShell->shutdownComplete( layout );
*/

}  // end OptionsMenuShutdown

//-------------------------------------------------------------------------------------------------
/** options menu update method */
//-------------------------------------------------------------------------------------------------
void OptionsMenuUpdate( WindowLayout *layout, void *userData )
{

}  // end OptionsMenuUpdate

//-------------------------------------------------------------------------------------------------
/** Options menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType OptionsMenuInput( GameWindow *window, UnsignedInt msg,
																			 WindowMsgData mData1, WindowMsgData mData2 )
{

	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = static_cast<UnsignedByte>(mData1);
			UnsignedByte state = static_cast<UnsignedByte>(mData2);

			switch( key )
			{

				// ----------------------------------------------------------------------------------------
				case KEY_ESC:
				{
					
					//
					// send a simulated selected event to the parent window of the
					// back/exit button
					//
					if( BitTest( state, KEY_STATE_UP ) )
					{
						AsciiString buttonName( "OptionsMenu.wnd:ButtonBack" );
						NameKeyType buttonID = TheNameKeyGenerator->nameToKey( buttonName );
						GameWindow *button = TheWindowManager->winGetWindowFromId( window, buttonID );

						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED, 
																								reinterpret_cast<WindowMsgData>(button), buttonID );

					}  // end if

					// don't let key fall through anywhere else
					return WindowMsgHandledType::MSG_HANDLED;

				}  // end escape

			}  // end switch( key )

		}  // end char

	}  // end switch( msg )

	return WindowMsgHandledType::MSG_IGNORED;

}  // end OptionsMenuInput

//-------------------------------------------------------------------------------------------------
/** options menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType OptionsMenuSystem( GameWindow *window, UnsignedInt msg, 
																				WindowMsgData mData1, WindowMsgData mData2 )
{
	static NameKeyType buttonBack = NAMEKEY_INVALID;
	static NameKeyType buttonDefaults = NAMEKEY_INVALID;
	static NameKeyType buttonAccept = NAMEKEY_INVALID;
	static NameKeyType buttonReplayMenu = NAMEKEY_INVALID;
	static NameKeyType buttonKeyboardOptionsMenu = NAMEKEY_INVALID;

	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CREATE:
		{

			// get ids for our children controls
			buttonBack = TheNameKeyGenerator->nameToKey( AsciiString("OptionsMenu.wnd:ButtonBack") );
			buttonDefaults = TheNameKeyGenerator->nameToKey( AsciiString("OptionsMenu.wnd:ButtonDefaults") );
			buttonAccept = TheNameKeyGenerator->nameToKey( AsciiString("OptionsMenu.wnd:ButtonAccept") );
			buttonKeyboardOptionsMenu = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:ButtonKeyboardOptions" ) );

			break;

		}  // end create

		//---------------------------------------------------------------------------------------------
		case GWM_DESTROY:
		{

			break;

		}  // end case

		// --------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
		{

			// if we're givin the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*reinterpret_cast<Bool *>(mData2) = TRUE;

			return WindowMsgHandledType::MSG_HANDLED;

		}  // end input

		//---------------------------------------------------------------------------------------------
		case GCM_SELECTED:
			{
				if(ignoreSelected)
					break;
				GameWindow *control = reinterpret_cast<GameWindow *>(mData1);
				Int controlID = control->winGetWindowId();
		
				if (controlID == comboBoxDetailID)
				{
					Int index;
					GadgetComboBoxGetSelectedPos( comboBoxDetail, &index );
					if(index != CUSTOMDETAIL)
					{
						if( OptionsMenuTabs::isBuilt() )
							showDetailLevel( index );
						break;
					}

					showAdvancedOptions();
				}
			break;
		}

		//---------------------------------------------------------------------------------------------
		case GSM_SLIDER_TRACK:
		{
			if(ignoreSelected)
				break;
			GameWindow *control = reinterpret_cast<GameWindow *>(mData1);
			if( OptionsMenuTabs::isBuilt() && control && isDetailControl( control->winGetWindowId() ) )
				markCustomDetail();
			break;
		}

		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			if(ignoreSelected)
				break;
			GameWindow *control = reinterpret_cast<GameWindow *>(mData1);
			Int controlID = control->winGetWindowId();

			if( OptionsMenuTabs::handleButton( control ) )
				break;
			if( OptionsMenuTabs::isBuilt() && isDetailControl( controlID ) )
				markCustomDetail();

			if( controlID == buttonBack )
			{
				// go back one screen
				//TheShell->pop();
				discardSettings();


				{
					DestroyOptionsLayout();
				}

			}  // end if
			else if (controlID == buttonAccept )
			{
				saveOptions();

				const Bool restartOffered = commitSettings();

				
				if(!TheGameLogic->isInGame() || TheGameLogic->isInShellGame())
					destroyQuitMenu(); // if we're in a game, the change res then enter the same kind of game, we nee the quit menu to be gone.


				{
					DestroyOptionsLayout();
					if (dispChanged)
					{
						DoResolutionDialog();
					}
					else if (detailLevelChanged && !restartOffered)	// the restart question already covers the next start
						MessageBoxFitText( MessageBoxOk( TheGameText->fetch( "GZH:OptionsDetailChangedTitle" ),
							TheGameText->fetch( "GZH:OptionsDetailChangedBody" ), nullptr ) );
					detailLevelChanged = FALSE;
				}

			}
			else if (controlID == buttonDefaults )
			{
				setDefaults();
			}
			else if (controlID == ButtonAdvancedAcceptID )
			{
				acceptAdvancedOptions();
				
			}
			else if (controlID == ButtonAdvancedCancelID )
			{	
				cancelAdvancedOptions();
			}
			else if ( controlID == buttonKeyboardOptionsMenu )
			{
				TheShell->push( AsciiString( "Menus/KeyboardOptionsMenu.wnd" ) );
			}
			else if(controlID == checkDrawAnchorID )
      {
        if( GadgetCheckBoxIsChecked( control ) )
        {
          	TheInGameUI->setDrawRMBScrollAnchor(true);
          	stageFlag( "Controls.DrawScrollAnchor", TRUE );
        }
				else
        {
          	TheInGameUI->setDrawRMBScrollAnchor(false);
          	stageFlag( "Controls.DrawScrollAnchor", FALSE );
        }
      }
			else if(controlID == checkMoveAnchorID )
      {
        if( GadgetCheckBoxIsChecked( control ) )
        {
          	TheInGameUI->setMoveRMBScrollAnchor(true);
          	stageFlag( "Controls.MoveScrollAnchor", TRUE );
        }
				else
        {
          	TheInGameUI->setMoveRMBScrollAnchor(false);
          	stageFlag( "Controls.MoveScrollAnchor", FALSE );
        }
      }
			else if(controlID == checkSaveCameraID )
      {
        if( GadgetCheckBoxIsChecked( control ) )
        {
          	TheWritableGlobalData->m_saveCameraInReplay = true;
          	stageFlag( "Replays.SaveCamera", TRUE );
        }
				else
        {
          	TheWritableGlobalData->m_saveCameraInReplay = false;
          	stageFlag( "Replays.SaveCamera", FALSE );
        }
      }
			else if(controlID == checkUseCameraID )
      {
        if( GadgetCheckBoxIsChecked( control ) )
        {
          	TheWritableGlobalData->m_useCameraInReplay = true;
          	stageFlag( "Replays.UseCamera", TRUE );
        }
				else
        {
          	TheWritableGlobalData->m_useCameraInReplay = false;
          	stageFlag( "Replays.UseCamera", FALSE );
        }
      }
			break;

		}  // end selected

		default:
			return WindowMsgHandledType::MSG_IGNORED;

	}  // end switch

	return WindowMsgHandledType::MSG_HANDLED;

}  // end OptionsMenuSystem
