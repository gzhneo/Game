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


//--- A MACRO TO APPLY TO TheTacticalView->getZoom() ------ To Clamp the return to a visually pleasing size
//--- so that icons, emoticons, health bars, pips, etc, look reasonably solid and don't shimmer or tweed
//#define CLAMP_ICON_ZOOM_FACTOR(n) (MAX(0.80f, MIN(1.00f, n)))
#define CLAMP_ICON_ZOOM_FACTOR(n) (n)//nothing

enum
{
	// The code tries to be below the health bar, but that pus it square under passenger pips.
	BOMB_ICON_EXTRA_OFFSET = 5
};

// Drawable: what is drawn over the unit -- health bar, icons, emoticons, captions, veterancy, construction

// ------------------------------------------------------------------------------------------------
// constants
const UnsignedInt HEALING_ICON_DISPLAY_TIME	= LOGICFRAMES_PER_SECOND * 3;

// ------------------------------------------------------------------------------------------------
/** Copy what the icon drawing reads from the object. Runs after the logic tick; between two ticks the
	* object does not change, so drawing from the copy shows the same as reading the object. */
// ------------------------------------------------------------------------------------------------
void Drawable::captureObjectView( void )
{
	DrawableObjectView &view = m_objectView;
	const Object *obj = m_object;
	view.valid = obj != nullptr;

	// Shroud status for the local player; GameClient::update has already brought it up to date. A
	// drawable without an object can take it from another object (prisoners in a prison camp).
	view.hasShroudedStatus = FALSE;
	const Player *localPlayer = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
	if( localPlayer )
	{
		const Object *shroudObject = obj;
		if( obj == nullptr && m_drawableInfo.m_shroudStatusObjectID != INVALID_ID )
			shroudObject = TheGameLogic->findObjectByID( m_drawableInfo.m_shroudStatusObjectID );
		if( shroudObject )
		{
			view.shroudedStatus = shroudObject->getShroudedStatus( localPlayer->getPlayerIndex() );
			view.hasShroudedStatus = TRUE;
		}
	}

	if( obj == nullptr )
		return;

	view.position = *obj->getPosition();
	obj->getHealthBoxPosition( view.healthBoxPosition );
	view.hasHealthBox = obj->getHealthBoxDimensions( view.healthBoxHeight, view.healthBoxWidth );
	view.maxHeightAbovePosition = obj->getGeometryInfo().getMaxHeightAbovePosition();
	view.boundingSphereRadius = obj->getGeometryInfo().getBoundingSphereRadius();

	Player *owner = obj->getControllingPlayer();
	view.controlledByLocalPlayer = owner == ThePlayerList->getLocalPlayer();
	view.ownerColor = owner ? owner->getPlayerColor() : 0;
	view.locallyControlled = obj->isLocallyControlled();
	view.squadNumber = ( view.locallyControlled && owner ) ? owner->getSquadNumberForObject( obj ) : NO_HOTKEY_SQUAD;
	view.formationID = obj->getFormationID();

	view.effectivelyDead = obj->isEffectivelyDead();
	view.ignoredInGui = obj->isKindOf( KINDOF_IGNORED_IN_GUI );
	view.noHealIcon = obj->isKindOf( KINDOF_NO_HEAL_ICON );
	view.forceAttackable = obj->isKindOf( KINDOF_FORCEATTACKABLE );
	view.statusBits = obj->getStatusBits();

	view.hasAmmoPips = obj->getAmmoPipShowingInfo( view.ammoTotal, view.ammoFull );

	ContainModuleInterface *container = obj->getContain();
	view.hasContainerPips = container && container->getContainerPipsToShow( view.containerTotal, view.containerFull );
	view.containedInfantry = 0;
	if( view.hasContainerPips && view.containerFull > 0 )
	{
		const ContainedItemsList *contained = container->getContainedItemsList();
		if( contained )
		{
			for( ContainedItemsList::const_iterator it = contained->begin(); it != contained->end(); ++it )
			{
				if( (*it)->isKindOf( KINDOF_INFANTRY ) )
					++view.containedInfantry;
			}
		}
	}

	BodyModuleInterface *body = obj->getBodyModule();
	view.hasBody = body != nullptr;
	if( body )
	{
		view.health = body->getHealth();
		view.maxHealth = body->getMaxHealth();
		view.lastHealingTimestamp = body->getLastHealingTimestamp();
	}

	view.enthusiastic = obj->testWeaponBonusCondition( WEAPONBONUSCONDITION_ENTHUSIASTIC ) == TRUE;
	view.subliminal = obj->testWeaponBonusCondition( WEAPONBONUSCONDITION_SUBLIMINAL ) == TRUE;
	view.carBomb = obj->testWeaponSetFlag( WEAPONSET_CARBOMB );

	static NameKeyType key_StickyBombUpdate = NAMEKEY( "StickyBombUpdate" );
	StickyBombUpdate *update = static_cast<StickyBombUpdate*>(obj->findUpdateModule( key_StickyBombUpdate ));
	view.hasStickyBomb = update && update->getTargetObject();
	view.stickyBombTimed = view.hasStickyBomb && update->isTimedBomb();
	view.stickyBombDetonationFrame = view.hasStickyBomb ? update->getDetonationFrame() : 0;

	view.disabledIcon = obj->isDisabledByType( DISABLED_HACKED )
		|| obj->isDisabledByType( DISABLED_PARALYZED )
		|| obj->isDisabledByType( DISABLED_EMP )
		|| obj->isDisabledByType( DISABLED_SUBDUED )
		|| obj->isDisabledByType( DISABLED_UNDERPOWERED );
	view.disabledHealthColor = obj->isDisabled() && !obj->isDisabledByType( DISABLED_HELD );

	view.constructionPercent = obj->getConstructionPercent();
	view.hasExperience = obj->getExperienceTracker() != nullptr;
	view.veterancy = view.hasExperience ? obj->getVeterancyLevel() : LEVEL_REGULAR;
}

// ------------------------------------------------------------------------------------------------
/** Compute the health bar region based on the health of the object and the
	* zoom level of the camera */
// ------------------------------------------------------------------------------------------------
static Bool computeHealthRegion( const Drawable *draw, IRegion2D& region )
{

	// sanity
	if( draw == nullptr )
		return FALSE;

	const DrawableObjectView &view = draw->getObjectView();
	if( !view.valid )
		return FALSE;

	Coord3D p = view.healthBoxPosition;
	{	// above the interpolated unit, not the tick state
		Coord3D d;
		draw->getRenderInterpolationOffset(&d);
		p.x += d.x; p.y += d.y; p.z += d.z;
	}
	ICoord2D screenCenter;
	if( !TheTacticalView->worldToScreen( &p, &screenCenter ) )
		return FALSE;

	if (!view.hasHealthBox)
		return FALSE;
	Real healthBoxWidth = view.healthBoxWidth, healthBoxHeight = view.healthBoxHeight;

	// scale the health bars according to the zoom
	Real zoom = TheTacticalView->getOverlayZoom();
	//Real widthScale = 1.3f / zoom;
	Real widthScale = 1.0f / zoom;
	//Real heightScale = 0.8f / zoom; 
	Real heightScale = 1.0f; 

	healthBoxWidth *= widthScale;
	healthBoxHeight *= heightScale;

	// do this so health bar doesn't get too skinny or fat after scaling
	//healthBoxHeight = max(3.0f, healthBoxHeight);
	healthBoxHeight = 3.0f;

	// figure out the final region for the health box
	region.lo.x = static_cast<Int>(screenCenter.x - healthBoxWidth * 0.45f);
	region.lo.y = static_cast<Int>(screenCenter.y - healthBoxHeight * 0.5f);
	region.hi.x = static_cast<Int>(region.lo.x + healthBoxWidth);
	region.hi.y = static_cast<Int>(region.lo.y + healthBoxHeight);

	return TRUE;

}  // end computeHealthRegion

Bool Drawable::drawsAnyUIText( void )
{
	if (!isSelected()) 
		return FALSE;

	const DrawableObjectView &view = m_objectView;
	if ( !view.valid || !view.locallyControlled )  
		return FALSE;

	Int groupNum = view.squadNumber;

	if (groupNum > NO_HOTKEY_SQUAD && groupNum < NUM_HOTKEY_SQUADS ) 
		return TRUE;
	else
		m_groupNumber = nullptr;
		
	if ( view.formationID != NO_FORMATION_ID )
		return TRUE;

	return FALSE;
}

// ------------------------------------------------------------------------------------------------
/** This is called as part of the "post draw" phase when drawable a drawable.  It is there
	* that we should overlay on the screen any 2D elements for purposes of user interface
	* information (such as a heatlh bar, veterency levels, etc.) */
// ------------------------------------------------------------------------------------------------
void Drawable::drawIconUI( void )
{
	if( TheGameLogic->getDrawIconUI() && (TheScriptEngine->getFade()==ScriptEngine::FADE_NONE) )
	{
		IRegion2D healthBarRegionStorage;
		const IRegion2D* healthBarRegion = nullptr;
		if (computeHealthRegion(this, healthBarRegionStorage))
			healthBarRegion = &healthBarRegionStorage; //both data and a PointerAsFlag for logic in the methods below

		const DrawableObjectView &view = m_objectView;
		
		// we only draw icons drawables with objects, so one bail here -------------------------
		if ( ! view.valid )
			return;

		//Icons that can be drawn on dead things
		drawHealthBar( healthBarRegion );                                        
		drawEmoticon( healthBarRegion );                                         

		drawCaption( healthBarRegion );
		drawConstructPercent( healthBarRegion );

		//All Icons Below only draw on ALIVE things, so  bail here -------------------------
		if( view.effectivelyDead || view.ignoredInGui ) // object explicitly wants nothing to do with these icons, so...
			return;
		drawHealing( healthBarRegion );//call so dead things can kill their healing icons
		drawBombed( healthBarRegion );
	
	
		//Disabled for multiplay!
		//drawBattlePlans( healthBarRegion );
	
		if ( drawsAnyUIText() )
			TheGameClient->addTextBearingDrawable( this );

		drawEnthusiastic( healthBarRegion );                                       
		drawDisabled( healthBarRegion );

		drawAmmo( healthBarRegion );                
		drawContained( healthBarRegion ); 

		//Moved this to last so that it shows up over contained and ammo icons.
		drawVeterancy( healthBarRegion ); 

#ifdef KRIS_BRUTAL_HACK_FOR_AIRCRAFT_CARRIER_DEBUGGING
		drawUIText();
#endif
	}
}

//------------------------------------------------------------------------------------------------
DrawableIconInfo* Drawable::getIconInfo()
{
	if (m_iconInfo == nullptr)
		m_iconInfo = newInstance(DrawableIconInfo);
	return m_iconInfo;
}

//------------------------------------------------------------------------------------------------
void Drawable::clearEmoticon()
{
	if (!hasIconInfo())
		return;

	killIcon(ICON_EMOTICON);
}

//------------------------------------------------------------------------------------------------
void Drawable::setEmoticon( const AsciiString &name, Int duration )
{
	setEmoticonIssued( name, duration, TheGameLogic->getFrame() );
}

void Drawable::setEmoticonIssued( const AsciiString &name, Int duration, UnsignedInt issuedFrame )
{
	//A duration of -1 means FOREVER
	clearEmoticon();
	Anim2DTemplate *animTemplate = TheAnim2DCollection->findTemplate( name );
	if( animTemplate )
	{
		DEBUG_ASSERTCRASH( getIconInfo()->m_icon[ ICON_EMOTICON ] == nullptr, ("Drawable::setEmoticon - Emoticon isn't empty, need to refuse to set or destroy the old one in favor of the new one\n") );
		if( getIconInfo()->m_icon[ ICON_EMOTICON ] == nullptr )
		{
			getIconInfo()->m_icon[ ICON_EMOTICON ] = newInstance(Anim2D)( animTemplate, TheAnim2DCollection );
			getIconInfo()->m_keepTillFrame[ ICON_EMOTICON ] = duration >= 0 ? issuedFrame + duration : FOREVER;
		}
	}
}

//------------------------------------------------------------------------------------------------
void Drawable::drawEmoticon( const IRegion2D *healthBarRegion )
{
	if( hasIconInfo() && getIconInfo()->m_icon[ ICON_EMOTICON ] )
	{
		UnsignedInt now = TheGameLogic->getFrame();
		if( healthBarRegion && getIconInfo()->m_keepTillFrame[ ICON_EMOTICON ] >= now )
		{
			//Draw the emoticon.
			Int barWidth = healthBarRegion->hi.x - healthBarRegion->lo.x;
			//Int barHeight = healthBarRegion.hi.y - healthBarRegion.lo.y;
			Int frameWidth = getIconInfo()->m_icon[ ICON_EMOTICON ]->getCurrentFrameWidth();
			Int frameHeight = getIconInfo()->m_icon[ ICON_EMOTICON ]->getCurrentFrameHeight();

#ifdef SCALE_ICONS_WITH_ZOOM_ML
			// adjust the width to be a % of the health bar region size
			Int size = REAL_TO_INT( barWidth * 0.3f );
			frameHeight = REAL_TO_INT((INT_TO_REAL(size) / INT_TO_REAL(frameWidth)) * frameHeight);
			frameWidth = size;
#endif			
			// given our scaled width and height we need to find the top left point to draw the image at
			ICoord2D screen;
			screen.x = static_cast<Int>(healthBarRegion->lo.x + (barWidth * 0.5f) - (frameWidth * 0.5f));
			screen.y = healthBarRegion->hi.y - frameHeight;
			getIconInfo()->m_icon[ ICON_EMOTICON ]->draw( screen.x, screen.y, frameWidth, frameHeight );
		}
		else
		{
			//Get rid of the emoticon.
			clearEmoticon();
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void Drawable::drawAmmo( const IRegion2D *healthBarRegion )
{
	const DrawableObjectView &view = m_objectView;

	if (!(
				TheGlobalData->m_showObjectHealth && 
				(isSelected() || (TheInGameUI && (TheInGameUI->getMousedOverDrawableID() == getID()))) &&
				view.controlledByLocalPlayer
			))
		return;

	if (!view.hasAmmoPips)
		return;
	Int numTotal = view.ammoTotal;
	Int numFull = view.ammoFull;

	if (!s_fullAmmo || !s_emptyAmmo)
		return;


	
#ifdef SCALE_ICONS_WITH_ZOOM_ML
	Real scale = TheGlobalData->m_ammoPipScaleFactor / CLAMP_ICON_ZOOM_FACTOR( TheTacticalView->getOverlayZoom() );
#else
	Real scale = 1.0f;
#endif

	Int boxWidth  = REAL_TO_INT(s_emptyAmmo->getImageWidth() * scale);
	Int boxHeight = REAL_TO_INT(s_emptyAmmo->getImageHeight() * scale);
	const Int SPACING = 1;
	//Int totalWidth = (boxWidth+SPACING)*numTotal;

	ICoord2D screenCenter;
	Coord3D pos = view.position;
	{	// offset to the interpolated position
		Coord3D d;
		getRenderInterpolationOffset(&d);
		pos.x += d.x; pos.y += d.y; pos.z += d.z;
	}
	pos.x += TheGlobalData->m_ammoPipWorldOffset.x;
	pos.y += TheGlobalData->m_ammoPipWorldOffset.y;
	pos.z += TheGlobalData->m_ammoPipWorldOffset.z + view.maxHeightAbovePosition;
	if( !TheTacticalView->worldToScreen( &pos, &screenCenter ) )
		return;

	Real bounding = view.boundingSphereRadius * scale;
	//Int posx = screenCenter.x + REAL_TO_INT(TheGlobalData->m_ammoPipScreenOffset.x*bounding) - totalWidth;
	//**CHANGING CODE: Left justify with health bar min
	Int posx = healthBarRegion->lo.x;
	Int posy = screenCenter.y + REAL_TO_INT(TheGlobalData->m_ammoPipScreenOffset.y*bounding);
	for (Int i = 0; i < numTotal; ++i)
	{
		TheDisplay->drawImage(i < numFull ? s_fullAmmo : s_emptyAmmo, posx, posy + 1, posx + boxWidth, posy + 1 + boxHeight);
		posx += boxWidth + SPACING;
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void Drawable::drawContained( const IRegion2D *healthBarRegion )
{
	const DrawableObjectView &view = m_objectView;

	if (!(
				TheGlobalData->m_showObjectHealth && 
				(isSelected() || (TheInGameUI && (TheInGameUI->getMousedOverDrawableID() == getID()))) &&
				view.controlledByLocalPlayer
			))
		return;

	if (!view.hasContainerPips)
		return;
	Int numTotal = view.containerTotal;
	Int numFull = view.containerFull;

	// if empty, don't show nothin'
	if (numFull == 0)
		return;

	Int numInfantry = view.containedInfantry;

#ifdef SCALE_ICONS_WITH_ZOOM_ML
	Real scale = TheGlobalData->m_ammoPipScaleFactor / CLAMP_ICON_ZOOM_FACTOR( TheTacticalView->getOverlayZoom() );
#else
	Real scale = 1.0f;
#endif
	Int boxWidth  = REAL_TO_INT(s_emptyContainer->getImageWidth() * scale);
	Int boxHeight = REAL_TO_INT(s_emptyContainer->getImageHeight() * scale);
	const Int SPACING = 1;
	//Int totalWidth = (boxWidth+SPACING)*numTotal;

	ICoord2D screenCenter;
	Coord3D pos = view.position;
	{	// offset to the interpolated position
		Coord3D d;
		getRenderInterpolationOffset(&d);
		pos.x += d.x; pos.y += d.y; pos.z += d.z;
	}
	pos.x += TheGlobalData->m_containerPipWorldOffset.x;
	pos.y += TheGlobalData->m_containerPipWorldOffset.y;
	pos.z += TheGlobalData->m_containerPipWorldOffset.z + view.maxHeightAbovePosition;
	if( !TheTacticalView->worldToScreen( &pos, &screenCenter ) )
		return;

	Real bounding = view.boundingSphereRadius * scale;
	
	//Int posx = screenCenter.x + REAL_TO_INT(TheGlobalData->m_containerPipScreenOffset.x*bounding) - totalWidth;
	//**CHANGING CODE: Left justify with health bar min
	Int posx = healthBarRegion->lo.x;
	Int posy = screenCenter.y + REAL_TO_INT(TheGlobalData->m_containerPipScreenOffset.y*bounding);

	for (Int i = 0; i < numTotal; ++i)
	{
		const Color INFANTRY_COLOR = GameMakeColor(0, 255, 0, 255);
		const Color NON_INFANTRY_COLOR = GameMakeColor(0, 0, 255, 255);
		if (i < numFull)
			TheDisplay->drawImage(s_fullContainer, posx, posy, posx + boxWidth, posy + boxHeight, 
				(i < numInfantry) ? INFANTRY_COLOR : NON_INFANTRY_COLOR);
		else
			TheDisplay->drawImage(s_emptyContainer, posx, posy + 1, posx + boxWidth, posy + 1 + boxHeight);
		posx += boxWidth + SPACING;
	}
}

//-------------------------------------------------------------------------------------------------
void Drawable::drawBattlePlans( const IRegion2D *healthBarRegion )
{
	Object *obj = getObject();
	if( !obj || !healthBarRegion )
	{
		return;
	}


	Player *player = obj->getControllingPlayer();
	if( player && player->getNumBattlePlansActive() > 0 && player->doesObjectQualifyForBattlePlan( obj ) )
	{
		if( player->getBattlePlansActiveSpecific( PLANSTATUS_BOMBARDMENT ) && s_animationTemplates[ ICON_BATTLEPLAN_BOMBARD ] )
		{
			if( !getIconInfo()->m_icon[ ICON_BATTLEPLAN_BOMBARD ] )
			{
				getIconInfo()->m_icon[ ICON_BATTLEPLAN_BOMBARD ] = newInstance(Anim2D)( s_animationTemplates[ ICON_BATTLEPLAN_BOMBARD ], TheAnim2DCollection );
			}
			//Int barHeight = healthBarRegion.hi.y - healthBarRegion.lo.y;
			Int frameWidth = getIconInfo()->m_icon[ ICON_BATTLEPLAN_BOMBARD ]->getCurrentFrameWidth();
			Int frameHeight = getIconInfo()->m_icon[ ICON_BATTLEPLAN_BOMBARD ]->getCurrentFrameHeight();
			
#ifdef SCALE_ICONS_WITH_ZOOM_ML
			// adjust the width to be a % of the health bar region size
			Int barWidth = healthBarRegion->hi.x - healthBarRegion->lo.x;
			Int size = REAL_TO_INT( barWidth * 0.3f );
			frameHeight = REAL_TO_INT((INT_TO_REAL(size) / INT_TO_REAL(frameWidth)) * frameHeight);
			frameWidth = size;
#endif			
			// given our scaled width and height we need to find the top left point to draw the image at
			ICoord2D screen;
			screen.x = healthBarRegion->lo.x;
			screen.y = healthBarRegion->lo.y + frameHeight;
			getIconInfo()->m_icon[ ICON_BATTLEPLAN_BOMBARD ]->draw( screen.x, screen.y, frameWidth, frameHeight );
		}
		else
		{
			killIcon(ICON_BATTLEPLAN_BOMBARD);
		}

		if( player->getBattlePlansActiveSpecific( PLANSTATUS_HOLDTHELINE ) && s_animationTemplates[ ICON_BATTLEPLAN_HOLDTHELINE ] )
		{
			if( !getIconInfo()->m_icon[ ICON_BATTLEPLAN_HOLDTHELINE ] )
			{
				getIconInfo()->m_icon[ ICON_BATTLEPLAN_HOLDTHELINE ] = newInstance(Anim2D)( s_animationTemplates[ ICON_BATTLEPLAN_HOLDTHELINE ], TheAnim2DCollection );
			}
			// draw the icon
			Int frameWidth = getIconInfo()->m_icon[ ICON_BATTLEPLAN_HOLDTHELINE ]->getCurrentFrameWidth();
			Int frameHeight = getIconInfo()->m_icon[ ICON_BATTLEPLAN_HOLDTHELINE ]->getCurrentFrameHeight();
			
#ifdef SCALE_ICONS_WITH_ZOOM_ML
			// adjust the width to be a % of the health bar region size
			Int barWidth = healthBarRegion->hi.x - healthBarRegion->lo.x;
			Int size = REAL_TO_INT( barWidth * 0.3f );
			frameHeight = REAL_TO_INT((INT_TO_REAL(size) / INT_TO_REAL(frameWidth)) * frameHeight);
			frameWidth = size;
#endif			
			// given our scaled width and height we need to find the top left point to draw the image at
			ICoord2D screen;
			screen.x = healthBarRegion->lo.x;
			screen.y = healthBarRegion->lo.y + frameHeight;
			getIconInfo()->m_icon[ ICON_BATTLEPLAN_HOLDTHELINE ]->draw( screen.x + frameWidth, screen.y, frameWidth, frameHeight );
		}
		else 
		{
			killIcon(ICON_BATTLEPLAN_HOLDTHELINE);
		}

		if( player->getBattlePlansActiveSpecific( PLANSTATUS_SEARCHANDDESTROY ) && s_animationTemplates[ ICON_BATTLEPLAN_SEARCHANDDESTROY ] )
		{
			if( !getIconInfo()->m_icon[ ICON_BATTLEPLAN_SEARCHANDDESTROY ] )
			{
				getIconInfo()->m_icon[ ICON_BATTLEPLAN_SEARCHANDDESTROY ] = newInstance(Anim2D)( s_animationTemplates[ ICON_BATTLEPLAN_SEARCHANDDESTROY ], TheAnim2DCollection );
			}
			// draw the icon
			Int frameWidth = getIconInfo()->m_icon[ ICON_BATTLEPLAN_SEARCHANDDESTROY ]->getCurrentFrameWidth();
			Int frameHeight = getIconInfo()->m_icon[ ICON_BATTLEPLAN_SEARCHANDDESTROY ]->getCurrentFrameHeight();

#ifdef SCALE_ICONS_WITH_ZOOM_ML
			// adjust the width to be a % of the health bar region size
			Int barWidth = healthBarRegion->hi.x - healthBarRegion->lo.x;
			Int size = REAL_TO_INT( barWidth * 0.3f );
			frameHeight = REAL_TO_INT((INT_TO_REAL(size) / INT_TO_REAL(frameWidth)) * frameHeight);
			frameWidth = size;
#endif

			// given our scaled width and height we need to find the top left point to draw the image at
			ICoord2D screen;
			screen.x = healthBarRegion->lo.x;
			screen.y = healthBarRegion->lo.y + frameHeight;
			getIconInfo()->m_icon[ ICON_BATTLEPLAN_SEARCHANDDESTROY ]->draw( screen.x + (frameWidth * 2), screen.y, frameWidth, frameHeight );
		}
		else 
		{
			killIcon(ICON_BATTLEPLAN_SEARCHANDDESTROY);
		}
		
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void Drawable::drawUIText()
{

	// This gets called by GameClient now
	// GameClient caches a list of us drawables during Drawablepostdraw()
	// then our group numbers get spit out last, so they draw in front
	
	const IRegion2D* healthBarRegion = nullptr;
	IRegion2D healthBarRegionStorage;
	if (computeHealthRegion(this, healthBarRegionStorage))
		healthBarRegion = &healthBarRegionStorage; //both data and a PointerAsFlag for logic in the methods below

	if (!healthBarRegion)
		return;

	const DrawableObjectView &view = m_objectView;

	Int groupNum = view.squadNumber;

	Color color = TheDrawGroupInfo->m_usePlayerColor ? view.ownerColor : TheDrawGroupInfo->m_colorForText;

	if (groupNum > NO_HOTKEY_SQUAD && groupNum < NUM_HOTKEY_SQUADS ) 
	{
		Int xPos = healthBarRegion->lo.x;
		Int yPos = healthBarRegion->lo.y;

		if (TheDrawGroupInfo->m_usingPixelOffsetX) {
			xPos += TheDrawGroupInfo->m_pixelOffsetX;
		} else {
			xPos = static_cast<Int>(xPos + ((healthBarRegion->width() * TheDrawGroupInfo->m_percentOffsetX)));
		}

		if (TheDrawGroupInfo->m_usingPixelOffsetY) {
			yPos += TheDrawGroupInfo->m_pixelOffsetY;
		} else {
			yPos = static_cast<Int>(yPos + ((healthBarRegion->width() * TheDrawGroupInfo->m_percentOffsetY)));
		}

		m_groupNumber = TheDisplayStringManager->getGroupNumeralString(groupNum);

		
		m_groupNumber->draw(xPos, yPos, color, 
												TheDrawGroupInfo->m_colorForTextDropShadow, 
												TheDrawGroupInfo->m_dropShadowOffsetX, 
												TheDrawGroupInfo->m_dropShadowOffsetY);
	}


	if ( view.formationID != NO_FORMATION_ID )
	{
		//draw an F, here
		Coord3D p = view.healthBoxPosition;
		ICoord2D screenCenter;
		if( ! TheTacticalView->worldToScreen( &p, &screenCenter ) )
			return;

		if ( ! view.hasHealthBox )
			return;
		Real healthBoxWidth = view.healthBoxWidth;

		Real scale = 1.3f/CLAMP_ICON_ZOOM_FACTOR( TheTacticalView->getOverlayZoom() );
		screenCenter.x = static_cast<Int>(screenCenter.x + ((healthBoxWidth * scale * 0.5f) + 10)) ; 


		DisplayString *formationMarker = TheDisplayStringManager->getFormationLetterString();
		//static DisplayString *formationMarker = TheDisplayStringManager->getGroupNumeralString( 5 );
		if ( formationMarker )
			formationMarker->draw(screenCenter.x, screenCenter.y, color,
													TheDrawGroupInfo->m_colorForTextDropShadow, 
													TheDrawGroupInfo->m_dropShadowOffsetX, 
													TheDrawGroupInfo->m_dropShadowOffsetY);

	}
	
#ifdef KRIS_BRUTAL_HACK_FOR_AIRCRAFT_CARRIER_DEBUGGING
	const Object *obj = getObject();
	if( obj->testStatus( OBJECT_STATUS_DECK_HEIGHT_OFFSET ) )
	{
		Object *carrier = TheGameLogic->findObjectByID( obj->getProducerID() );
		if( carrier )
		{
			for (BehaviorModule** i = carrier->getBehaviorModules(); *i; ++i)
			{
				ParkingPlaceBehaviorInterface *pp = (*i)->getParkingPlaceBehaviorInterface();
				if( pp )
				{
					Int index = pp->getSpaceIndex( obj->getID() );
					
					Int xPos = healthBarRegion->lo.x;
					Int yPos = healthBarRegion->lo.y;

					if (TheDrawGroupInfo->m_usingPixelOffsetX) {
						xPos += TheDrawGroupInfo->m_pixelOffsetX;
					} else {
						xPos += (healthBarRegion->width() * TheDrawGroupInfo->m_percentOffsetX);
					}

					if (TheDrawGroupInfo->m_usingPixelOffsetY) {
						yPos += TheDrawGroupInfo->m_pixelOffsetY;
					} else {
						yPos += (healthBarRegion->width() * TheDrawGroupInfo->m_percentOffsetY);
					}

					m_groupNumber = TheDisplayStringManager->getGroupNumeralString(index);

					
					m_groupNumber->draw(xPos, yPos, color, 
															TheDrawGroupInfo->m_colorForTextDropShadow, 
															TheDrawGroupInfo->m_dropShadowOffsetX, 
															TheDrawGroupInfo->m_dropShadowOffsetY);
					break;
				}
			}
		}
	}
#endif
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void Drawable::drawHealing(const IRegion2D* healthBarRegion)
{

	const DrawableObjectView &view = m_objectView;

	// we do show show icons for things that explicitly forbid it
	if( view.noHealIcon || view.statusBits.test( OBJECT_STATUS_SOLD ) )
		return;


	// see if healing has been done to us recently
	Bool showHealing = FALSE;
	if( view.hasBody && view.health != view.maxHealth )
	{
//		const DamageInfo* lastDamage = body->getLastDamageInfo();
//		if( lastDamage != NULL && lastDamage->in.m_damageType == DAMAGE_HEALING 
//			&&(TheGameLogic->getFrame() - body->getLastHealingTimestamp()) <= HEALING_ICON_DISPLAY_TIME 
//			)
		if ( TheGameLogic->getFrame() > HEALING_ICON_DISPLAY_TIME && // because so many things init health early in game
			(TheGameLogic->getFrame() - view.lastHealingTimestamp <= HEALING_ICON_DISPLAY_TIME) )

			showHealing = TRUE;
	}

	// based on our own kind of we have certain icons to display at a size scale
	Real scale;
	DrawableIconType typeIndex;
	if( isKindOf( KINDOF_STRUCTURE ) )
	{
		typeIndex = ICON_STRUCTURE_HEAL;
		scale = 0.33f;
	}
	else if( isKindOf( KINDOF_VEHICLE ) )
	{
		typeIndex = ICON_VEHICLE_HEAL;
		scale = 0.7f;
	}
	else
	{
		typeIndex = ICON_DEFAULT_HEAL;
		scale = 0.7f;
	}

	//
	// if we are to show healing make sure we have the animation for it allocated, otherwise
	// free any animation we may have allocated back to the animation memory pool
	//
	if( showHealing ) /// @todo HERE, WE NEED TO LEAVE STUFF ALONE, IF WE ARE ALREADY SHOWING HEALING
	{
		if (healthBarRegion != nullptr)
		{

			if( getIconInfo()->m_icon[ typeIndex ] == nullptr && s_animationTemplates[ typeIndex ] )
				getIconInfo()->m_icon[ typeIndex ] = newInstance(Anim2D)( s_animationTemplates[ typeIndex ], TheAnim2DCollection );

			// draw the animation if present
			if( getIconInfo()->m_icon[ typeIndex ] != nullptr)
			{
				
				//
				// we are going to draw the healing icon relative to the size of the health bar region
				// since that region takes into account hit point size and zoom factor of the camera too
				//
				Int barWidth = healthBarRegion->hi.x - healthBarRegion->lo.x;

				Int frameWidth = getIconInfo()->m_icon[ typeIndex ]->getCurrentFrameWidth();
				Int frameHeight = getIconInfo()->m_icon[ typeIndex ]->getCurrentFrameHeight();

#ifdef SCALE_ICONS_WITH_ZOOM_ML
				// adjust the width to be a % of the health bar region size
				Int size = REAL_TO_INT( barWidth * scale );
				frameHeight = REAL_TO_INT((INT_TO_REAL(size) / INT_TO_REAL(frameWidth)) * frameHeight);
				frameWidth = size;
#endif
				// given our scaled width and height we need to find the top left point to draw the image at
				ICoord2D screen;
				screen.x = REAL_TO_INT( healthBarRegion->lo.x + (barWidth * 0.75f) - (frameWidth * 0.5f) );
				screen.y = REAL_TO_INT( healthBarRegion->lo.y - frameHeight );
				getIconInfo()->m_icon[ typeIndex ]->draw( screen.x, screen.y, frameWidth, frameHeight );
								
			}	
		}
	}
	else
	{
		killIcon(typeIndex);
	}

}

// ------------------------------------------------------------------------------------------------
/** This enthusiastic effect is TEMPORARY for the multiplayer test */
// ------------------------------------------------------------------------------------------------
void Drawable::drawEnthusiastic(const IRegion2D* healthBarRegion)
{

	const DrawableObjectView &view = m_objectView;
	//
	// if we are to show effect make sure we have the animation for it allocated, otherwise
	// free any animation we may have allocated back to the animation memory pool
	//
	// only display if have enthusiasm
	
	if( view.enthusiastic &&
			healthBarRegion != nullptr )
	{

		DrawableIconType iconIndex = ICON_ENTHUSIASTIC;

		if (view.subliminal )// unless...
			iconIndex = ICON_ENTHUSIASTIC_SUBLIMINAL;




		if( getIconInfo()->m_icon[ iconIndex ] == nullptr && s_animationTemplates[ iconIndex ] )
			getIconInfo()->m_icon[ iconIndex ] = newInstance(Anim2D)( s_animationTemplates[ iconIndex ], TheAnim2DCollection );

		// draw the animation if present
		if( getIconInfo()->m_icon[ iconIndex ] != nullptr)
		{
			
			//
			// we are going to draw the healing icon relative to the size of the health bar region
			// since that region takes into account hit point size and zoom factor of the camera too
			//
			Int barWidth = healthBarRegion->hi.x - healthBarRegion->lo.x;// used for position

			// based on our own kind of we have certain icons to display at a size scale
			Real scale;
			if( isKindOf( KINDOF_STRUCTURE ) || isKindOf( KINDOF_HUGE_VEHICLE ) )
				scale = 1.00f;
			else if( isKindOf( KINDOF_VEHICLE ) )
				scale = 0.75f;
			else
				scale = 0.5f;
 
			Int frameWidth = static_cast<Int>(getIconInfo()->m_icon[ iconIndex ]->getCurrentFrameWidth() * scale);
			Int frameHeight = static_cast<Int>(getIconInfo()->m_icon[ iconIndex ]->getCurrentFrameHeight() * scale);

#ifdef SCALE_ICONS_WITH_ZOOM_ML
			// adjust the width to be a % of the health bar region size
			Int size = REAL_TO_INT( barWidth * scale );
			frameHeight = REAL_TO_INT((INT_TO_REAL(size) / INT_TO_REAL(frameWidth)) * frameHeight);
			frameWidth = size;
#endif
			// given our scaled width and height we need to find the bottom left point to draw the image at
			ICoord2D screen;
			screen.x = REAL_TO_INT( healthBarRegion->lo.x + (barWidth * 0.25f) - (frameWidth * 0.5f) );
			screen.y = static_cast<Int>(healthBarRegion->hi.y + (frameHeight * 0.25));
			getIconInfo()->m_icon[ iconIndex ]->draw( screen.x, screen.y, frameWidth, frameHeight );
							
		}	
	}
	else
	{
		killIcon(ICON_ENTHUSIASTIC);
		killIcon(ICON_ENTHUSIASTIC_SUBLIMINAL);
	}

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void Drawable::drawBombed(const IRegion2D* healthBarRegion)
{

	const DrawableObjectView &view = m_objectView;


	UnsignedInt now = TheGameLogic->getFrame();

	if( view.carBomb &&
				view.controlledByLocalPlayer)
	{
		if( !getIconInfo()->m_icon[ ICON_CARBOMB ] && s_animationTemplates[ ICON_CARBOMB ] )
			getIconInfo()->m_icon[ ICON_CARBOMB ] = newInstance(Anim2D)( s_animationTemplates[ ICON_CARBOMB ], TheAnim2DCollection );

		if( getIconInfo()->m_icon[ ICON_CARBOMB ] )
		{
			//
			// we are going to draw the healing icon relative to the size of the health bar region
			// since that region takes into account hit point size and zoom factor of the camera too
			//
			if( healthBarRegion )
			{
				Int barWidth = healthBarRegion->hi.x - healthBarRegion->lo.x;
				Int barHeight = healthBarRegion->hi.y - healthBarRegion->lo.y;

				Int frameWidth = getIconInfo()->m_icon[ ICON_CARBOMB ]->getCurrentFrameWidth();
				Int frameHeight = getIconInfo()->m_icon[ ICON_CARBOMB ]->getCurrentFrameHeight();

				// adjust the width to be a % of the health bar region size
				Int size = REAL_TO_INT( barWidth * 0.5f );
				frameHeight = REAL_TO_INT((INT_TO_REAL(size) / INT_TO_REAL(frameWidth)) * frameHeight);
				frameWidth = size;

				// given our scaled width and height we need to find the top left point to draw the image at
				ICoord2D screen;
				screen.x = REAL_TO_INT( healthBarRegion->lo.x + (barWidth * 0.5f) - (frameWidth * 0.5f) );
				screen.y = REAL_TO_INT( healthBarRegion->lo.y + barHeight * 0.5f ) + BOMB_ICON_EXTRA_OFFSET;

				getIconInfo()->m_icon[ ICON_CARBOMB ]->draw( screen.x, screen.y, frameWidth, frameHeight );	
				getIconInfo()->m_keepTillFrame[ ICON_CARBOMB ] = FOREVER;
			}
		}
}
	else
	{
		killIcon(ICON_CARBOMB);
	}

	//
	// Bombed?
	//
	{
		//This case is tricky. The object that is bombed doesn't know it... but the bomb itself does.
		//So what we do is get it's target, then determine if the target has the icon or not.
		if( view.hasStickyBomb )
		{
			if( view.stickyBombTimed )
			{
				//Timed bomb
				if( !getIconInfo()->m_icon[ ICON_BOMB_TIMED ] && s_animationTemplates[ ICON_BOMB_TIMED ] && s_animationTemplates[ ICON_BOMB_REMOTE ] )
				{
					getIconInfo()->m_icon[ ICON_BOMB_REMOTE ] = newInstance(Anim2D)( s_animationTemplates[ ICON_BOMB_REMOTE ], TheAnim2DCollection );
					getIconInfo()->m_icon[ ICON_BOMB_TIMED ] = newInstance(Anim2D)( s_animationTemplates[ ICON_BOMB_TIMED ], TheAnim2DCollection );

					//Because this is a counter icon that ranges from 0-60 seconds, we need to calculate which frame to 
					//start the animation from. Because timers are second based -- 1000 ms equal 1 frame. So we simply
					//calculate the time via detonation frame.
					//
					// srj sez: this may sound familiar somehow, but let me reiterate, just in case you missed it:
					//
					// hardcoding is bad. 
					//
					// the anim got changed and now is only 20 seconds max, so the previous code was wrong. 
					// 
					// hey, I've got an idea! why don't we ASK the anim how long it is?
					//
					UnsignedInt dieFrame = view.stickyBombDetonationFrame;
					UnsignedInt seconds = REAL_TO_INT_CEIL( (dieFrame - now) * SECONDS_PER_LOGICFRAME_REAL);

					UnsignedInt numFrames = getIconInfo()->m_icon[ ICON_BOMB_TIMED ]->getAnimTemplate()->getNumFrames();
					// this anim goes from "N" seconds down to zero, so the max seconds we can use is N-1.
					if (seconds > numFrames - 1)
						seconds = numFrames - 1;
					
					getIconInfo()->m_icon[ ICON_BOMB_TIMED ]->setMinFrame(static_cast<UnsignedShort>(numFrames - seconds - 1));
					getIconInfo()->m_icon[ ICON_BOMB_TIMED ]->reset();
				}
				if( getIconInfo()->m_icon[ ICON_BOMB_TIMED ] )
				{
					//
					// we are going to draw the healing icon relative to the size of the health bar region
					// since that region takes into account hit point size and zoom factor of the camera too
					//
					if( healthBarRegion )
					{
						Int barWidth = healthBarRegion->hi.x - healthBarRegion->lo.x;
						Int barHeight = healthBarRegion->hi.y - healthBarRegion->lo.y;

						Int frameWidth = getIconInfo()->m_icon[ ICON_BOMB_TIMED ]->getCurrentFrameWidth();
						Int frameHeight = getIconInfo()->m_icon[ ICON_BOMB_TIMED ]->getCurrentFrameHeight();

						// adjust the width to be a % of the health bar region size
						Int size = REAL_TO_INT( barWidth * 0.65f );
						frameHeight = REAL_TO_INT((INT_TO_REAL(size) / INT_TO_REAL(frameWidth)) * frameHeight);
						frameWidth = size;
						
						// given our scaled width and height we need to find the top left point to draw the image at
						ICoord2D screen;
						screen.x = REAL_TO_INT( healthBarRegion->lo.x + (barWidth * 0.5f) - (frameWidth * 0.5f) );
						screen.y = REAL_TO_INT( healthBarRegion->lo.y + barHeight * 0.5f ) + BOMB_ICON_EXTRA_OFFSET;

						getIconInfo()->m_icon[ ICON_BOMB_REMOTE ]->draw( screen.x, screen.y, frameWidth, frameHeight );	
						getIconInfo()->m_keepTillFrame[ ICON_BOMB_REMOTE ] = now + 1;
						getIconInfo()->m_icon[ ICON_BOMB_TIMED ]->draw( screen.x, screen.y, frameWidth, frameHeight );	
						getIconInfo()->m_keepTillFrame[ ICON_BOMB_TIMED ] = now + 1;
					}
				}
			}
			else
			{
				//Remote charge
				//Timed bomb
				if( !getIconInfo()->m_icon[ ICON_BOMB_REMOTE ] && s_animationTemplates[ ICON_BOMB_REMOTE ] )
				{
					getIconInfo()->m_icon[ ICON_BOMB_REMOTE ] = newInstance(Anim2D)( s_animationTemplates[ ICON_BOMB_REMOTE ], TheAnim2DCollection );
				}
				if( getIconInfo()->m_icon[ ICON_BOMB_REMOTE ] )
				{
					//
					// we are going to draw the healing icon relative to the size of the health bar region
					// since that region takes into account hit point size and zoom factor of the camera too
					//
					if( healthBarRegion )
					{
						Int barWidth = healthBarRegion->hi.x - healthBarRegion->lo.x;
						Int barHeight = healthBarRegion->hi.y - healthBarRegion->lo.y;

						Int frameWidth = getIconInfo()->m_icon[ ICON_BOMB_REMOTE ]->getCurrentFrameWidth();
						Int frameHeight = getIconInfo()->m_icon[ ICON_BOMB_REMOTE ]->getCurrentFrameHeight();


						// adjust the width to be a % of the health bar region size
						Int size = REAL_TO_INT( barWidth * 0.65f );
						frameHeight = REAL_TO_INT((INT_TO_REAL(size) / INT_TO_REAL(frameWidth)) * frameHeight);
						frameWidth = size;

						// given our scaled width and height we need to find the top left point to draw the image at
						ICoord2D screen;
						screen.x = REAL_TO_INT( healthBarRegion->lo.x + (barWidth * 0.5f) - (frameWidth * 0.5f) );
						screen.y = REAL_TO_INT( healthBarRegion->lo.y + barHeight * 0.5f ) + BOMB_ICON_EXTRA_OFFSET;

						getIconInfo()->m_icon[ ICON_BOMB_REMOTE ]->draw( screen.x, screen.y, frameWidth, frameHeight );	
						getIconInfo()->m_keepTillFrame[ ICON_BOMB_REMOTE ] = now + 1;
					}
				}
			}
		}
	}

	if (hasIconInfo())
	{
		if(getIconInfo()->m_keepTillFrame[ ICON_BOMB_TIMED ] <= now )
		{
			killIcon(ICON_BOMB_TIMED);
		}
		if(getIconInfo()->m_keepTillFrame[ ICON_BOMB_REMOTE ] <= now )
		{
			killIcon(ICON_BOMB_REMOTE);
		}
	}
}

// ------------------------------------------------------------------------------------------------
/** Draw any icon information that needs to be drawn */
// ------------------------------------------------------------------------------------------------
void Drawable::drawDisabled(const IRegion2D* healthBarRegion)
{

	const DrawableObjectView &view = m_objectView;


	//
	// Disabled Emoticon /Lightning
	//                   7/
	if( view.disabledIcon )
	{
		// create icon if necessary
		if( getIconInfo()->m_icon[ ICON_DISABLED ] == nullptr && s_animationTemplates[ ICON_DISABLED ] )
		{
			getIconInfo()->m_icon[ ICON_DISABLED ] = newInstance(Anim2D)
			( s_animationTemplates[ ICON_DISABLED ], TheAnim2DCollection );
		}

		// draw the icon
		if( healthBarRegion && getIconInfo()->m_icon[ ICON_DISABLED ] )
		{
			Int barHeight = healthBarRegion->hi.y - healthBarRegion->lo.y;

			Int frameWidth = getIconInfo()->m_icon[ ICON_DISABLED ]->getCurrentFrameWidth();
			Int frameHeight = getIconInfo()->m_icon[ ICON_DISABLED ]->getCurrentFrameHeight();

#ifdef SCALE_ICONS_WITH_ZOOM_ML
			// adjust the width to be a % of the health bar region size
			Int barWidth = healthBarRegion->hi.x - healthBarRegion->lo.x;
			Int size = REAL_TO_INT( barWidth * 0.3f );
			frameHeight = REAL_TO_INT((INT_TO_REAL(size) / INT_TO_REAL(frameWidth)) * frameHeight);
			frameWidth = size;
#endif
			// given our scaled width and height we need to find the top left point to draw the image at
			ICoord2D screen;
			screen.x = healthBarRegion->lo.x;
			screen.y = healthBarRegion->hi.y - (frameHeight + barHeight);
			getIconInfo()->m_icon[ ICON_DISABLED ]->draw( screen.x, screen.y, frameWidth, frameHeight );

		}  // end if
	}  // end if
	else
	{
		// delete icon if necessary
		killIcon(ICON_DISABLED);

	}  // end if

}

//-------------------------------------------------------------------------------------------------
/** Draw construction percent for drawables that have objects that are "under construction" */
//-------------------------------------------------------------------------------------------------
void Drawable::drawConstructPercent( const IRegion2D *healthBarRegion )
{

	// this data is in an attached object
	const DrawableObjectView &view = m_objectView;

	if( !view.valid || !view.statusBits.test( OBJECT_STATUS_UNDER_CONSTRUCTION ) ||
			view.statusBits.test( OBJECT_STATUS_SOLD ) )
	{
		// no object, or we are now complete get rid of the string if we have one
		if( m_constructDisplayString )
		{
		
			TheDisplayStringManager->freeDisplayString( m_constructDisplayString );
			m_constructDisplayString = nullptr;
		}
		return;
	}

	//if( obj->isEffectivelyDead() )
	//{
		//Don't render icons for dead things.
	//	return;
	//}

	// construction is partially complete, allocate a display string if we need one
	if( m_constructDisplayString == nullptr )
		m_constructDisplayString = TheDisplayStringManager->newDisplayString();

	// set the string if the value has changed
	if( m_lastConstructDisplayed != view.constructionPercent )
	{
		UnicodeString buffer;

		
		buffer.format( TheGameText->fetch("CONTROLBAR:UnderConstructionDesc"), view.constructionPercent);
		m_constructDisplayString->setText( buffer );

		// record this percent as our last displayed so we don't un-necessarily rebuild the string
		m_lastConstructDisplayed = view.constructionPercent;
				
	}  // end if

	// get center position in drawable
	ICoord2D screen;
	Coord3D pos;
	getDrawableGeometryInfo().getCenterPosition(*getPosition(), pos);

	// convert drawable center position to screen coords
	TheTacticalView->worldToScreen( &pos, &screen );

  if ( screen.x < 1 )
    return;

	// draw the text
	Color color = GameMakeColor( 255, 255, 255, 255 );
	Color dropColor = GameMakeColor( 0, 0, 0, 255 );
	screen.x -= (m_constructDisplayString->getWidth() / 2);
	m_constructDisplayString->draw( screen.x, screen.y, color, dropColor );

}  // end drawConstructPercent

//-------------------------------------------------------------------------------------------------
/** Draw caption */
//-------------------------------------------------------------------------------------------------
void Drawable::drawCaption( const IRegion2D *healthBarRegion )
{
	if (!m_captionDisplayString)
		return;

	// get center position in drawable
	ICoord2D screen;
	Coord3D pos;
	getDrawableGeometryInfo().getCenterPosition(*getPosition(), pos);

	// convert drawable center position to screen coords
	TheTacticalView->worldToScreen( &pos, &screen );
	screen.x -= (m_captionDisplayString->getWidth() / 2);

	// draw background
	{
		Int width, xPos;
		Int height, yPos;
		m_captionDisplayString->getSize(&width,&height);
		xPos = screen.x - 1;
		yPos = screen.y - 1;

		TheDisplay->drawFillRect(xPos, yPos, width + 2,height + 2, GameMakeColor(0,0,0,125));
		TheDisplay->drawOpenRect(xPos, yPos, width + 2,height + 2, 1.0, GameMakeColor(20,20,20,255));
	}

	// draw the text
	Color color = TheInGameUI->getDrawableCaptionColor();
	Color dropColor = GameMakeColor( 0, 0, 0, 255 );
	m_captionDisplayString->draw( screen.x, screen.y, color, dropColor );

}  // end drawCaption

// ------------------------------------------------------------------------------------------------
/** Draw any veterency markers that should be displayed */
// ------------------------------------------------------------------------------------------------
void Drawable::drawVeterancy( const IRegion2D *healthBarRegion )
{
	const DrawableObjectView &view = m_objectView;

	if( !view.hasExperience )
	{
		//Only objects with experience trackers can possibly have veterancy.
		return;
	}

	VeterancyLevel level = static_cast<VeterancyLevel>(view.veterancy);
	const Image* image = s_veterancyImage[level];
	if (!image)
		return;

	Real scale = 1.3f/CLAMP_ICON_ZOOM_FACTOR( TheTacticalView->getOverlayZoom() );
#ifdef SCALE_ICONS_WITH_ZOOM_ML
	Real objScale = scale * 1.55f;
#else
	Real objScale = 1.0f;
#endif


	Real vetBoxWidth  = image->getImageWidth()*objScale;
	Real vetBoxHeight = image->getImageHeight()*objScale;

	//
	// take the center position of the object, go down to it's bottom extent, and project
	// that point to the screen, that will be the "center" of our veterancy box
	//

	Coord3D p = view.healthBoxPosition;
	ICoord2D screenCenter;
	if( !TheTacticalView->worldToScreen( &p, &screenCenter ) )
		return;

	if (!view.hasHealthBox)
		return;
	Real healthBoxWidth = view.healthBoxWidth;

	screenCenter.x = static_cast<Int>(screenCenter.x + (healthBoxWidth * scale * 0.5f));

	// draw the image
	TheDisplay->drawImage(image, screenCenter.x + 1, screenCenter.y + 1, static_cast<Int>(screenCenter.x + 1 + vetBoxWidth), static_cast<Int>(screenCenter.y + 1 + vetBoxHeight));

}  // end drawVeterancy

// ------------------------------------------------------------------------------------------------
/** Draw health bar information for drawable */
// ------------------------------------------------------------------------------------------------
void Drawable::drawHealthBar(const IRegion2D* healthBarRegion)
{
	if (!healthBarRegion)
		return;

	//
	// only draw health for selected drawbles and drawables that have been moused over
	// by the cursor
	//
	if( TheGlobalData->m_showObjectHealth && 
			(isSelected() || (TheInGameUI && (TheInGameUI->getMousedOverDrawableID() == getID()))) )
	{
		const DrawableObjectView &view = m_objectView;

		// if no object, nothing to do
		if( !view.valid )
			return;

		if( view.forceAttackable )
		{
			//Currently (Nov 2002), everything that is forceattackable are civ fences, and they all have a
			//single hit point and they aren't selectable. However, a bug is when you force attack it, it shows
			//the healthbar. Well, this stops it, however, should force attackable kindofs change, then this
			//will require reevaluation.
			return;
		}

		// get the health and max health
		if( !view.hasBody )
			return;
		Real health = view.health;
		Real maxHealth = view.maxHealth;

		// if no max health or health at all we will draw nothing
		if( maxHealth == 0.0f || health == 0.0f )
			return;

		// what is our health ratio
		Real healthRatio = health / maxHealth;

		//
		// what color will we use for the health bar based on our ratio, this makes it
		// slowly go from green to red, (or from blue to cyan if under construction, or disabled)
		//

		Color color, outlineColor;
		if( view.statusBits.test( OBJECT_STATUS_UNDER_CONSTRUCTION ) || view.disabledHealthColor )
		{
			color = GameMakeColor( 0, static_cast<UnsignedByte>(healthRatio * 255.0f), 255, 255 );//blue to cyan
			outlineColor = GameMakeColor( 0, static_cast<UnsignedByte>(healthRatio * 128.0f), 128, 255 );//dark blue to dark cyan

		}
		else //red to green
		{

			RGBColor inColor, outColor;
			inColor.blue = 0; // health bars do not display blue...
			outColor.blue = 0; // health bars do not display blue...

			if( healthRatio >= 0.5f )
			{
				inColor.red = 1.0f - ((healthRatio - 0.5f) / 0.5f);
				inColor.green = 1.0f;
//				color = GameMakeColor       (  255 - ((healthRatio - 0.5f) / 0.5f) * 255, 255, 0, 255 );
//				outlineColor = GameMakeColor( (255 - ((healthRatio - 0.5f) / 0.5f) * 255) * 0.5, 255 * 0.5, 0, 255 );
			}
			else
			{
				inColor.red = 1.0f;
				inColor.green = 1.0f - ((0.5f - healthRatio) / 0.5f);
//				color = GameMakeColor( 255, 255 - ((0.5f - healthRatio) / 0.5f) * 255, 0, 255 );
//				outlineColor = GameMakeColor( 255 * 0.5, (255 - ((0.5f - healthRatio) / 0.5f) * 255) * 0.5, 0, 255 );
			}

			outColor.red = inColor.red * 0.5f;
			outColor.green =inColor.green * 0.5f;

			if( m_conditionState.test( MODELCONDITION_REALLY_DAMAGED ) == TRUE )
			{//average the above color with red
				inColor.red = (1.0f + inColor.red) * 0.5f;
				inColor.green *= 0.5f;
			}
			else if ( m_conditionState.test( MODELCONDITION_DAMAGED ) == FALSE )
			{//average the above color with green
				inColor.green = (1.0f + inColor.green) * 0.5f;
				inColor.red *= 0.5f;
			}

			color =        GameMakeColor( static_cast<UnsignedByte>(255.0 * inColor.red), static_cast<UnsignedByte>(255.0 * inColor.green), static_cast<UnsignedByte>(255.0 * inColor.blue), 255);
			outlineColor = GameMakeColor( static_cast<UnsignedByte>(255.0 * outColor.red), static_cast<UnsignedByte>(255.0 * outColor.green), static_cast<UnsignedByte>(255.0 * outColor.blue), 255);
		
		
		}




///		Real scale = 1.3f / TheTacticalView->getZoom();
		Real healthBoxWidth = static_cast<Real>(healthBarRegion->hi.x - healthBarRegion->lo.x);
			
		Real healthBoxHeight = static_cast<Real>(max(3, healthBarRegion->hi.y - healthBarRegion->lo.y));
		Real healthBoxOutlineSize = 1.0f;

		// draw the health box outline
		TheDisplay->drawOpenRect( healthBarRegion->lo.x, healthBarRegion->lo.y, static_cast<Int>(healthBoxWidth), static_cast<Int>(healthBoxHeight),
															healthBoxOutlineSize, outlineColor );

		// draw a filled bar for the health
		TheDisplay->drawFillRect( healthBarRegion->lo.x + 1, healthBarRegion->lo.y + 1,
															static_cast<Int>((healthBoxWidth - 2) * healthRatio), static_cast<Int>(healthBoxHeight - 2),
															color );
	}  // end if

}  // end drawHealthBar
