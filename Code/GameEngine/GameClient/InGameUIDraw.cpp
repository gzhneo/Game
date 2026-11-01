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
#include <algorithm>

// InGameUI: drawing -- the frame, floating text, world animations, pop-up messages

//-------------------------------------------------------------------------------------------------
/** This is called after the UI has been drawn. */
//-------------------------------------------------------------------------------------------------
void InGameUI::postDraw( void )
{
	// display of the active test helpers
	TestAids_Draw();

	// render our display strings for the messages if on
	if( m_messagesOn )
	{
		Int i, x, y;
		Color dropColor;
		UnsignedByte r, g, b, a;

		x = m_messagePosition.x;
		y = m_messagePosition.y;
		for( i = MAX_UI_MESSAGES - 1; i >= 0; i-- )
		{

			if( m_uiMessages[ i ].displayString )
			{

				// make drop color black, but use the alpha setting of the fill color specified (for fading)
				GameGetColorComponents( m_uiMessages[ i ].color, &r, &g, &b, &a );
				dropColor = GameMakeColor( 0, 0, 0, a );

				// draw the text
				m_uiMessages[ i ].displayString->draw( x, y, m_uiMessages[ i ].color, dropColor );

				// increment text spot to next location
				GameFont *font = m_uiMessages[ i ].displayString->getFont();
				y += font->height;

			}  //end if

		}  // end for i

	}  // end if

	if( m_militarySubtitle )
	{
		ICoord2D pos;
		pos.x = m_militarySubtitle->position.x;
		pos.y = m_militarySubtitle->position.y;
		Color dropColor;
		UnsignedByte r, g, b, a;
		GameGetColorComponents( m_militarySubtitle->color, &r, &g, &b, &a );
		dropColor = GameMakeColor( 0, 0, 0, a );
		for(Int i = 0; static_cast<UnsignedInt>(i) <= m_militarySubtitle->currentDisplayString; i++)
		{
			m_militarySubtitle->displayStrings[i]->draw(pos.x,pos.y, m_militarySubtitle->color,dropColor );
			Int height;
			m_militarySubtitle->displayStrings[i]->getSize(nullptr, &height);
			pos.y += height;
		}
		if( m_militarySubtitle->blockDrawn )
		{
			ICoord2D size;
			size.y = m_militarySubtitle->displayStrings[m_militarySubtitle->currentDisplayString]->getFont()->height;
			size.x = static_cast<Int>(size.y * 0.8f);
			TheDisplay->drawFillRect(m_militarySubtitle->blockPos.x, m_militarySubtitle->blockPos.y, size.x, size.y, m_militarySubtitle->color);
		}

	}

	// draw superweapon timers
  // Also responsible for Eva saying "Superweapon is ready for launch"
  //  IMPORTANT: Don't bail out of this block early just because you don't 
  //  want to display the timers -- Eva still needs to be checked
	if (TheGameLogic->getFrame() > 0 )
	{
//	Int superweaponCount = 0;
		Int startX = static_cast<Int>(m_superweaponPosition.x * TheDisplay->getWidth());
		Int startY = static_cast<Int>(m_superweaponPosition.y * TheDisplay->getHeight());
		
		Int bottomMargin = static_cast<Int>( static_cast<Real>(TheTacticalView->getHeight()) * 0.82f ); 
			


		Bool marginExceeded = FALSE;

		for (Int i=0; i<MAX_PLAYER_COUNT; ++i)
		{
			Color bgColor = GameMakeColor( 0, 0, 0, 255 );
			for (SuperweaponMap::iterator mapIt = m_superweapons[i].begin(); mapIt != m_superweapons[i].end(); ++mapIt)
			{
				AsciiString templateName = mapIt->first;
				for (SuperweaponList::iterator listIt = mapIt->second.begin(); listIt != mapIt->second.end(); ++listIt)
				{
					SuperweaponInfo *info = *listIt;
					DEBUG_ASSERTCRASH(info, ("No superweapon info!"));
					if (info && !info->m_hiddenByScript && !info->m_hiddenByScience)
					{
						//enforce bottom margin of tactical view
						if ( startY >= bottomMargin)
						{
							UnicodeString ellipsis;
							ellipsis.format(L"...");
							info->setText( ellipsis, ellipsis );
							info->setFont( m_superweaponReadyFont, m_superweaponNormalPointSize, m_superweaponNormalBold );
							info->drawTime( startX,	startY, m_superweaponFlashColor, bgColor );
							
							marginExceeded = TRUE;
						}

						Object * owningObject = TheGameLogic->findObjectByID(info->m_id);
						if (owningObject)
						{
							
							// We don't draw our timers until we are finished with construction.
							// It is important that let the SpecialPowerUpdate is add its timer in its contructor,,
							// since the science for it could be added before construction is finished,
							// And thus the timer set to READY before the timer is first drawn, here
							if ( owningObject->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ))
								continue;

							SpecialPowerModuleInterface *module = owningObject->getSpecialPowerModule(info->getSpecialPowerTemplate());
							if (module)
							{
								// found one - draw it
 								Bool isReady = module->isReady();
 								Int readySecs;
 
								// IsReady includes disabledness, so if you have a 0 timer disabled super, you don't want 
 								// the UnsignedInt to wrap around to hundreds of millions of seconds.
 								if( module->getReadyFrame() < TheGameLogic->getFrame() )
									readySecs = 0;
 								else 
 									readySecs = (module->getReadyFrame() - TheGameLogic->getFrame()) / LOGICFRAMES_PER_SECOND;
								// Yes, integer math.  We can't have float imprecision display 4:01 on a disabled superweapon.
 
                // Only if we actually changed the ready status do we want to play an Eva event.
                if ( isReady && !info->m_evaReadyPlayed )
                {
                  if ( TheGameLogic->getFrame() > 0 )
                  {
                    SpecialPowerType type = module->getSpecialPowerTemplate()->getSpecialPowerType();
                  
                    Player *localPlayer = ThePlayerList->getLocalPlayer();
                  
                    if( type == SPECIAL_PARTICLE_UPLINK_CANNON || type == SUPW_SPECIAL_PARTICLE_UPLINK_CANNON || type == LAZR_SPECIAL_PARTICLE_UPLINK_CANNON )
                    {
                      if ( localPlayer == owningObject->getControllingPlayer() )
                      {
                        TheEva->setShouldPlay(EVA_SuperweaponReady_Own_ParticleCannon);
                      }
                      else if ( localPlayer->getRelationship(owningObject->getTeam()) != ENEMIES )
                      {
                        // Note: counting relationship NEUTRAL as ally. Not sure if this makes a difference???
                        TheEva->setShouldPlay(EVA_SuperweaponReady_Ally_ParticleCannon);
                      }
                      else
                      {
                        TheEva->setShouldPlay(EVA_SuperweaponReady_Enemy_ParticleCannon);
                      }
                    }
                    else if( type == SPECIAL_NEUTRON_MISSILE || type == NUKE_SPECIAL_NEUTRON_MISSILE || type == SUPW_SPECIAL_NEUTRON_MISSILE )
                    {
                      if ( localPlayer == owningObject->getControllingPlayer() )
                      {
                        TheEva->setShouldPlay(EVA_SuperweaponReady_Own_Nuke);
                      }
                      else if ( localPlayer->getRelationship(owningObject->getTeam()) != ENEMIES )
                      {
                        // Note: counting relationship NEUTRAL as ally. Not sure if this makes a difference???
                        TheEva->setShouldPlay(EVA_SuperweaponReady_Ally_Nuke);
                      }
                      else
                      {
                        TheEva->setShouldPlay(EVA_SuperweaponReady_Enemy_Nuke);
                      }
                    }
                    else if (type == SPECIAL_SCUD_STORM)
                    {
                      if ( localPlayer == owningObject->getControllingPlayer() )
                      {
                        TheEva->setShouldPlay(EVA_SuperweaponReady_Own_ScudStorm);
                      }
                      else if ( localPlayer->getRelationship(owningObject->getTeam()) != ENEMIES )
                      {
                        // Note: counting relationship NEUTRAL as ally. Not sure if this makes a difference???
                        TheEva->setShouldPlay(EVA_SuperweaponReady_Ally_ScudStorm);
                      }
                      else
                      {
                        TheEva->setShouldPlay(EVA_SuperweaponReady_Enemy_ScudStorm);
                      }
                    }
                  }
                  info->m_evaReadyPlayed = true;
                }
                else
                {
                  if ( !isReady )
                    info->m_evaReadyPlayed = false; // Reset Eva for next time
                }
              
                // draw the text
                if ( !m_superweaponHiddenByScript && !marginExceeded )
                {
                  // Similarly, only checking timers is not truly indicitive of readyness.
 								  Bool changeBolding = (static_cast<UnsignedInt>(readySecs) != info->m_timestamp) || (isReady != info->m_ready) || info->m_forceUpdateText;
 								  if (changeBolding)
 								  {
 									  if (isReady)
									  {
										  // go bold - we're good to go
										  info->setFont( m_superweaponReadyFont, m_superweaponReadyPointSize, m_superweaponReadyBold );
									  }
									  else
									  {
										  // if we were at 0, we've just fired - kill the bold
										  if (info->m_timestamp == 0)
										  {
											  info->setFont( m_superweaponNormalFont, m_superweaponNormalPointSize, m_superweaponNormalBold );
										  }
									  }
                  
									  
									  info->m_forceUpdateText = false;
 									  info->m_ready = isReady;
									  info->m_timestamp = readySecs;
                    Int min = readySecs/60;
                    Int sec = readySecs - min*60;
                    AsciiString strIndex;
                    strIndex.format("GUI:%s", templateName.str());
                    UnicodeString name, time;
                    name.format(L"%ls: ", TheGameText->fetch(strIndex.str()).str());
                    time.format(L"%d:%2.2d", min, sec);
                    info->setText(name, time);
                  }

                  if (isReady)
								  {
									  if ( m_superweaponFlashDuration != 0.0f )
									  {
										  if ( TheGameLogic->getFrame() >= static_cast<UnsignedInt>(m_superweaponLastFlashFrame + static_cast<Int>(m_superweaponFlashDuration)) )
										  {
											  m_superweaponUsedFlashColor = !m_superweaponUsedFlashColor;
											  m_superweaponLastFlashFrame = TheGameLogic->getFrame();
										  }
										  info->drawName( startX,
											  startY, (m_superweaponUsedFlashColor)?0:m_superweaponFlashColor, bgColor );
										  info->drawTime( startX,
											  startY, (m_superweaponUsedFlashColor)?0:m_superweaponFlashColor, bgColor );
									  }
									  else
									  {
										  info->drawName( startX, startY, 0, bgColor );
										  info->drawTime( startX, startY, 0, bgColor );
									  }
								  }
								  else
								  {
									  info->drawName( startX,	startY, 0, bgColor );
									  info->drawTime( startX, startY, 0, bgColor );
								  }

								  // increment text spot to next location
								  startY = static_cast<Int>(startY + (info->getHeight()));

                }
                if (info->getSpecialPowerTemplate()->isSharedNSync())
                  break; // Wow, it is almost too easy!
                // This prevents redundant timers for shared powers/superweapons
                // No matter how many specialpowermodules register their timers with me,
                // I will only draw the timer of the first valid one in my list,
                // since they all have the same template, ans they all
                // use the Player::getReadyFrame() functions to stay in sync.
              }
						}
					}
				}
			}
		}
	}

	// remaining start protection, in the style of the named timers and above them
	if (TheGameLogic->isGracePeriod() && TheGameLogic->getFrame() > 0)
	{
		Int secs = static_cast<Int>(SECONDS_PER_LOGICFRAME_REAL * (TheGameLogic->getGraceEndFrame() - TheGameLogic->getFrame()));
		if (m_graceDisplayString == nullptr)
		{
			m_graceDisplayString = TheDisplayStringManager->newDisplayString();
			m_graceDisplayString->setFont( TheFontLibrary->getFont( m_namedTimerNormalFont,
				TheGlobalLanguageData->adjustFontSize(m_namedTimerNormalPointSize), m_namedTimerNormalBold ) );
			m_graceShownSecs = -1;
		}
		if (secs != m_graceShownSecs)
		{
			m_graceShownSecs = secs;
			UnicodeString line;
			line.format( L"%s %d:%02d", TheGameText->fetch( "GZH:GracePeriodTimer" ).str(), secs / 60, secs % 60 );
			m_graceDisplayString->setText( line );
		}
		Int x = static_cast<Int>(m_namedTimerPosition.x * TheDisplay->getWidth());
		if (m_namedTimerPosition.x >= 0.5f)
			x -= m_graceDisplayString->getWidth();
		Int y = static_cast<Int>(m_namedTimerPosition.y * TheDisplay->getHeight()) - m_graceDisplayString->getFont()->height;
		m_graceDisplayString->draw( x, y, m_namedTimerNormalColor, GameMakeColor( 0, 0, 0, 255 ) );
	}
	else if (m_graceDisplayString)
	{
		freeGraceDisplay();
	}

	// draw named timers
	if (TheGameLogic->getFrame() > 0 && m_showNamedTimers)
	{
//		Int namedTimerCount = 0;
		Bool reverseXDir = (m_namedTimerPosition.x >= 0.5f);
		Int startX = static_cast<Int>(m_namedTimerPosition.x * TheDisplay->getWidth());
		Int startY = static_cast<Int>(m_namedTimerPosition.y * TheDisplay->getHeight());
		Color bgColor = GameMakeColor( 0, 0, 0, 255 );
		for (NamedTimerMapIt mapIt = m_namedTimers.begin(); mapIt != m_namedTimers.end(); ++mapIt)
		{
			AsciiString timerName = mapIt->first;
			NamedTimerInfo *info = mapIt->second;
			DEBUG_ASSERTCRASH(info, ("No namedTimer info!"));
			if (info)
			{
				// found one - draw it
				UnicodeString line;
				Int framesLeft = TheScriptEngine->getCounter(timerName)->value;
				UnsignedInt readyFrame = TheGameLogic->getFrame();
				if (framesLeft > 0)
					readyFrame += framesLeft;
				Int readySecs = static_cast<Int>(SECONDS_PER_LOGICFRAME_REAL * (readyFrame - TheGameLogic->getFrame()));
				if ( (info->isCountdown && static_cast<UnsignedInt>(readySecs) != info->timestamp) || (!info->isCountdown && static_cast<UnsignedInt>(framesLeft) != info->timestamp) )
				{
					if (!readySecs && info->isCountdown)
					{
						// go bold - we're good to go
						info->displayString->setFont( TheFontLibrary->getFont( m_namedTimerReadyFont, 
							TheGlobalLanguageData->adjustFontSize(m_namedTimerReadyPointSize), m_namedTimerReadyBold ) );
					}
					else
					{
						// if we were at 0, we've just fired - kill the bold
						if (info->timestamp == 0 || info->isCountdown)
						{
							info->displayString->setFont( TheFontLibrary->getFont( m_namedTimerNormalFont, 
								TheGlobalLanguageData->adjustFontSize(m_namedTimerNormalPointSize), m_namedTimerNormalBold ) );
						}
					}

					info->timestamp = readySecs;
					Int min = readySecs/60;
					Int sec = readySecs - min*60;
					
					if (!info->isCountdown)
						line.format(L"%s %d", info->timerText.str(), framesLeft);
					else
					{
						if (sec >= 10)
							line.format(L"%s %d:%d", info->timerText.str(), min, sec);
						else
							line.format(L"%s %d:0%d", info->timerText.str(), min, sec);
					}
					info->displayString->setText(line);
				}

				// draw the text
				Int drawX = startX;
				if (reverseXDir)
					drawX -= info->displayString->getWidth();
				if (!readySecs && info->isCountdown)
				{
					if ( m_namedTimerFlashDuration != 0.0f )
					{
						if ( TheGameLogic->getFrame() >= static_cast<UnsignedInt>(m_namedTimerLastFlashFrame + static_cast<Int>(m_namedTimerFlashDuration)) )
						{
							m_namedTimerUsedFlashColor = !m_namedTimerUsedFlashColor;
							m_namedTimerLastFlashFrame = TheGameLogic->getFrame();
						}
						info->displayString->draw( drawX, startY, (m_namedTimerUsedFlashColor)?info->color:m_namedTimerFlashColor, bgColor );
					}
					else
					{
						info->displayString->draw( drawX, startY, info->color, bgColor );
					}
				}
				else
				{
					info->displayString->draw( drawX, startY, info->color, bgColor );
				}

				// increment text spot to next location
				startY -= info->displayString->getFont()->height;
			}
		}
	}
	
	// draw RMB scroll anchor
	if (TheLookAtTranslator && m_drawRMBScrollAnchor)
	{
		const ICoord2D* anchor = TheLookAtTranslator->getRMBScrollAnchor();
		if (anchor)
		{
			static const Int w = 2;
			static const Int h = 2;
			static const Int r = 4; // ratio
			static const Color mainColor = GameMakeColor(0, 255, 0, 255);
			static const Color dropColor = GameMakeColor(0, 0, 0, 255);
			TheDisplay->drawFillRect( anchor->x-w*r-1, anchor->y-h-1, w*2*r+3, h*2+3, dropColor );
			TheDisplay->drawFillRect( anchor->x-w-1, anchor->y-h*r-1, w*2+3, h*2*r+3, dropColor );
			TheDisplay->drawFillRect( anchor->x-w*r, anchor->y-h, w*2*r+1, h*2+1, mainColor );
			TheDisplay->drawFillRect( anchor->x-w, anchor->y-h*r, w*2+1, h*2*r+1, mainColor );
		}
	}

	//draw superweapon ready multipliers
	TheControlBar->drawSpecialPowerShortcutMultiplierText();

}  // end postDraw

//-------------------------------------------------------------------------------------------------
/** Add a new floating text to our list */
//-------------------------------------------------------------------------------------------------
void InGameUI::addFloatingText(const UnicodeString& text,const Coord3D *pos, Color color)
{
	addFloatingTextIssued( text, pos, color, TheGameLogic->getFrame() );
}

void InGameUI::addFloatingTextIssued(const UnicodeString& text, const Coord3D *pos, Color color, UnsignedInt issuedFrame)
{
	if( TheGameLogic->getDrawIconUI() )
	{
		FloatingTextData *newFTD = newInstance( FloatingTextData );
		newFTD->m_frameCount = 0;
		newFTD->m_color = color;
		newFTD->m_pos3D.x = pos->x;
		newFTD->m_pos3D.z = pos->z;
		newFTD->m_pos3D.y = pos->y;
		newFTD->m_text = text;
		newFTD->m_dString->setText(text);
		
			
		if(m_floatingTextTimeOut <= 0)
			newFTD->m_frameTimeOut = issuedFrame +  DEFAULT_FLOATING_TEXT_TIMEOUT;
		else
			newFTD->m_frameTimeOut = issuedFrame +  m_floatingTextTimeOut; 
		
		m_floatingTextList.push_front( newFTD ); // add to the list
	}
}

#if defined(_DEBUG)
inline Bool isClose(Real a, Real b) { return fabs(a-b) <= 1.0f; }

inline Bool isClose(const Coord3D& a, const Coord3D& b) 
{
		return	isClose(a.x, b.x) && 
			isClose(a.y, b.y) && 
			isClose(a.z, b.z);
}

void InGameUI::DEBUG_addFloatingText(const AsciiString& text, const Coord3D * pos, Color color)
{
	const Int POINTSIZE = 8;
	const Int LEADING = 0;

	Coord3D posToUse = *pos;

	// Move down until no other floating text sits at the same spot.
	while (std::any_of(m_floatingTextList.begin(), m_floatingTextList.end(),
		[&posToUse](const FloatingTextData *ftd) { return isClose(ftd->m_pos3D, posToUse); }))
	{
		posToUse.z -= (POINTSIZE + LEADING);
	}

	FloatingTextData *newFTD = newInstance( FloatingTextData );
	newFTD->m_color = color;
	newFTD->m_pos3D.x = posToUse.x;
	newFTD->m_pos3D.y = posToUse.y;
	newFTD->m_pos3D.z = posToUse.z;
	UnicodeString translate;
	translate.translate(text);
	newFTD->m_text = translate;
	newFTD->m_dString->setText(translate);
	newFTD->m_dString->setFont(TheWindowManager->winFindFont( AsciiString("Arial"), POINTSIZE, FALSE ));
				
	if(m_floatingTextTimeOut <= 0)
		newFTD->m_frameTimeOut = TheGameLogic->getFrame() +  DEFAULT_FLOATING_TEXT_TIMEOUT;
	else
		newFTD->m_frameTimeOut = TheGameLogic->getFrame() +  m_floatingTextTimeOut; 
	
	m_floatingTextList.push_front( newFTD ); // add to the list

	//DEBUG_LOG(("%s\n",text.str()));
}
#endif

//-------------------------------------------------------------------------------------------------
/** modify the position of our floating text */
//-------------------------------------------------------------------------------------------------
void InGameUI::updateFloatingText( void )
{
	FloatingTextData *ftd;		// pointer to our floating point data
	UnsignedInt currLogicFrame = TheGameLogic->getFrame();			// the current logic frame
	UnsignedByte r, g, b, a;	// we'll need to break apart our color so we can modify the alpha
	Int amount;								// The amout we'll change the alpha
	static UnsignedInt lastLogicFrameUpdate = currLogicFrame;		// We need to make sure our current frame is different then our last frame we updated.

	// only update the position if we're incrementing frames
	if(lastLogicFrameUpdate == currLogicFrame)
		return;
	
	lastLogicFrameUpdate = currLogicFrame;

	// Loop through our floating text list
	for(FloatingTextListIt it = m_floatingTextList.begin(); it != m_floatingTextList.end();)
	{
		ftd = *it;
		
		// move it up
		++ftd->m_frameCount;
		
		// fade the text
		if( currLogicFrame > static_cast<UnsignedInt>(ftd->m_frameTimeOut))
		{
			// modify the color
			GameGetColorComponents(ftd->m_color, &r, &g, &b, &a);		
			amount = REAL_TO_INT( (currLogicFrame - ftd->m_frameTimeOut) * m_floatingTextMoveVanishRate);
			if(a - amount < 0)
				a = 0;
			else
				a = static_cast<UnsignedByte>(a - amount);
			ftd->m_color = GameMakeColor(r, g, b, a);
			// if we have 0 alpha delete it
			if( a <= 0)
			{
				it = m_floatingTextList.erase(it);
				deleteInstanceOf(ftd);
				continue; // don't do the ++it below
			}

		}
		// increase our itterator
		++it;
	
	}

}

//-------------------------------------------------------------------------------------------------
/** Itterates through and draws each floating text */
//-------------------------------------------------------------------------------------------------
void InGameUI::drawFloatingText( void )
{
	FloatingTextData *ftd;
	// loop through and draw all the texts
	for(FloatingTextListIt it = m_floatingTextList.begin(); it != m_floatingTextList.end(); ++it)
	{
		ftd = *it;
		ICoord2D pos;
		// get the local player's index
		Int playerNdx = ThePlayerList->getLocalPlayer()->getPlayerIndex();

		// which PartitionManager cells are we looking at?
		Int pCX, pCY;
		ThePartitionManager->worldToCell(ftd->m_pos3D.x, ftd->m_pos3D.y, &pCX, &pCY);

		// translate it's 3d pos into a 2d screen pos
		if( TheTacticalView->worldToScreen(&ftd->m_pos3D, &pos) 
			&& ftd->m_dString 
			&& ThePartitionManager->getShroudStatusForPlayer(playerNdx, pCX, pCY) == CellShroudStatus::CELLSHROUD_CLEAR )
		{
			pos.y = static_cast<Int>(pos.y - (ftd->m_frameCount * m_floatingTextMoveUpSpeed));
			Color dropColor;
			UnsignedByte r, g, b, a;
			Int width;

			// make drop color black, but use the alpha setting of the fill color specified (for fading)
			GameGetColorComponents( ftd->m_color, &r, &g, &b, &a );
			dropColor = GameMakeColor( 0, 0, 0, a );
			ftd->m_dString->getSize(&width, nullptr);
			// draw it!
			ftd->m_dString->draw(pos.x - (width / 2), pos.y, ftd->m_color,dropColor);
		}

	}
}

//-------------------------------------------------------------------------------------------------
/** ittereate through and clear out the list of floating text */
//-------------------------------------------------------------------------------------------------
void InGameUI::clearFloatingText( void )
{
	FloatingTextData *ftd;
	// loop through and draw all the texts
	for(FloatingTextListIt it = m_floatingTextList.begin(); it != m_floatingTextList.end();)
	{
		ftd = *it;
		it = m_floatingTextList.erase(it);
		deleteInstanceOf(ftd);
	}
	
}

//-------------------------------------------------------------------------------------------------
/** If we want to use the default text color, then we call this function */
//-------------------------------------------------------------------------------------------------
void InGameUI::popupMessage( const AsciiString& message, Int x, Int y, Int width, Bool pause, Bool pauseMusic)
{
	popupMessage( message, x, y, width, m_popupMessageColor, pause, pauseMusic);
}

//-------------------------------------------------------------------------------------------------
/** initialize, and popup a message box to the user */
//-------------------------------------------------------------------------------------------------
void InGameUI::popupMessage( const AsciiString& identifier, Int x, Int y, Int width, Color textColor, Bool pause, Bool pauseMusic)
{
	if(m_popupMessageData)
		clearPopupMessageData();

	UpdateDiplomacyBriefingText(identifier, FALSE);

	UnicodeString message = TheGameText->fetch(identifier);

	m_popupMessageData = newInstance( PopupMessageData );	
	m_popupMessageData->message = message;
	// x and why are passed in as a percentage of the screen, convert to screen coords
	if( x > 100 )
		x = 100;
	if( x < 0 )
		x = 0;
	
	if( y > 100 )
		y = 100;
	if( y < 0 )
		y = 0;

	m_popupMessageData->x = static_cast<Int>(TheDisplay->getWidth() * (INT_TO_REAL(x) / 100));
	m_popupMessageData->y = static_cast<Int>(TheDisplay->getHeight() * (INT_TO_REAL(y) / 100));
	// cap the lower limit of the width
	if(width < 50)
		width = 50;
	m_popupMessageData->width = width;
	m_popupMessageData->textColor = textColor;
	m_popupMessageData->pause = pause;
	m_popupMessageData->pauseMusic = pauseMusic;

	if( pause )
		TheGameLogic->setGamePaused(TRUE, pauseMusic);

	m_popupMessageData->layout = TheWindowManager->winCreateLayout(AsciiString("InGamePopupMessage.wnd"));
	m_popupMessageData->layout->runInit();
}

//-------------------------------------------------------------------------------------------------
/** take care of the logic of clearing the popupMessageData */
//-------------------------------------------------------------------------------------------------
void InGameUI::clearPopupMessageData( void )
{
	if(!m_popupMessageData)
		return;
	if(m_popupMessageData->layout)
	{
		m_popupMessageData->layout->destroyWindows();
		deleteInstanceOf(m_popupMessageData->layout);
		m_popupMessageData->layout = nullptr;
	}
	if( m_popupMessageData->pause )
		TheGameLogic->setGamePaused(FALSE, m_popupMessageData->pauseMusic);
	deleteInstanceOf(m_popupMessageData);
	m_popupMessageData = nullptr;
	
}

//-------------------------------------------------------------------------------------------------
/** Floating Text Constructor */
//-------------------------------------------------------------------------------------------------
FloatingTextData::FloatingTextData(void)
{
	// Added By Sadullah Nader
	// Initializations missing and needed
	m_color = 0;
	m_frameCount = 0;
	m_frameTimeOut = 0;
	m_pos3D.zero();
	m_text.clear();
	//
	m_dString = TheDisplayStringManager->newDisplayString();
}

//-------------------------------------------------------------------------------------------------
/** Floating Text Destructor */
//-------------------------------------------------------------------------------------------------
FloatingTextData::~FloatingTextData(void)
{
	if(m_dString)
		TheDisplayStringManager->freeDisplayString( m_dString );
	m_dString = nullptr;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
WorldAnimationData::WorldAnimationData( void )
{

	m_anim = nullptr;
	m_worldPos.zero();
	m_expireFrame = 0;
	m_options = WORLD_ANIM_NO_OPTIONS;
	m_zRisePerSecond = 0.0f;

}  // end WorldAnimationData

WorldAnimationData::~WorldAnimationData( void )
{
	if( m_anim )
		deleteInstanceOf(m_anim);
}

// ------------------------------------------------------------------------------------------------
/** Add a 2D animation at a spot in the world */
// ------------------------------------------------------------------------------------------------
void InGameUI::addWorldAnimation( Anim2DTemplate *animTemplate,
																	const Coord3D *pos,
																	WorldAnimationOptions options,
																	Real durationInSeconds,
																	Real zRisePerSecond )
{
	addWorldAnimationIssued( animTemplate, pos, options, durationInSeconds, zRisePerSecond, TheGameLogic->getFrame() );
}

void InGameUI::addWorldAnimationIssued( Anim2DTemplate *animTemplate, const Coord3D *pos, WorldAnimationOptions options,
																				Real durationInSeconds, Real zRisePerSecond, UnsignedInt issuedFrame )
{

	// sanity
	if( animTemplate == nullptr || pos == nullptr || durationInSeconds <= 0.0f )
		return;

	// allocate a new world animation data struct
	// (huh huh, he said "wad")
	auto wad = std::make_unique<WorldAnimationData>();

	// allocate a new animation instance
	Anim2D *anim = newInstance(Anim2D)( animTemplate, TheAnim2DCollection );

	// assign all data
	wad->m_anim = anim;
	wad->m_expireFrame = static_cast<UnsignedInt>(issuedFrame + (durationInSeconds * static_cast<Real>(LOGICFRAMES_PER_SECOND)));
	wad->m_options = options;
	wad->m_worldPos = *pos;
	wad->m_zRisePerSecond = zRisePerSecond;

	// add to list
	m_worldAnimationList.push_front( std::move(wad) );

}  // end addWorldAnimation

// ------------------------------------------------------------------------------------------------
/** Delete all world animations */
// ------------------------------------------------------------------------------------------------
void InGameUI::clearWorldAnimations( void )
{
	// the entries delete their animation data and animation instance
	m_worldAnimationList.clear();
	
}  // end clearWorldAnimations

static const UnsignedInt FRAMES_BEFORE_EXPIRE_TO_FADE = LOGICFRAMES_PER_SECOND * 1;

// ------------------------------------------------------------------------------------------------
/** Update all world animations and draw the visible ones */
// ------------------------------------------------------------------------------------------------
void InGameUI::updateAndDrawWorldAnimations( void )
{
	WorldAnimationData *wad;

	// go through all animations
	for( WorldAnimationListIterator it = m_worldAnimationList.begin();
			 it != m_worldAnimationList.end(); /*empty*/ )
	{

		// get data
		wad = it->get();

		// update portion ... only when the game is in motion
		if( wad && TheGameLogic->isGamePaused() == FALSE )
		{

			//
			// see if it's time to expire this animation based on animation type and options or
			// the expire frame
			//
			if( TheGameLogic->getFrame() >= wad->m_expireFrame ||
					(BitTest( wad->m_options, WORLD_ANIM_PLAY_ONCE_AND_DESTROY ) &&
					 BitTest( wad->m_anim->getStatus(), ANIM_2D_STATUS_COMPLETE )) )
			{

				// delete this element and continue
				it = m_worldAnimationList.erase( it );
				continue;

			}  // end if

			// update the Z value
			if( wad->m_zRisePerSecond )
				wad->m_worldPos.z += wad->m_zRisePerSecond / static_cast<Real>(LOGICFRAMES_PER_SECOND) * GameEngine_RenderFrameSteps();

		}  // end if

		//
		// don't bother going forward with the draw process if this location is shrouded for
		// the local player
		//
		Int playerIndex = ThePlayerList->getLocalPlayer()->getPlayerIndex();
		if( ThePartitionManager->getShroudStatusForPlayer( playerIndex, &wad->m_worldPos ) != CellShroudStatus::CELLSHROUD_CLEAR )
		{

			++it;
			continue;

		}  // end if

		// update translucency value
		if( BitTest( wad->m_options, WORLD_ANIM_FADE_ON_EXPIRE ) )
		{

			// see if we should be setting the translucency value
			UnsignedInt framesTillExpire = wad->m_expireFrame - TheGameLogic->getFrame();
			if( framesTillExpire < FRAMES_BEFORE_EXPIRE_TO_FADE )
			{
				
				// compute alpha level so that we're totally gone by the expire frame
				Real alpha = INT_TO_REAL( framesTillExpire ) / INT_TO_REAL( FRAMES_BEFORE_EXPIRE_TO_FADE );
				wad->m_anim->setAlpha( alpha );
				
			}  // end if

		}  // end if

		// project the point to screen space
		ICoord2D screen;
		if( TheTacticalView->worldToScreen( &wad->m_worldPos, &screen ) == TRUE )
		{
			UnsignedInt width = wad->m_anim->getCurrentFrameWidth();
			UnsignedInt height = wad->m_anim->getCurrentFrameHeight();

			// scale the width and height given the camera zoom level
			Real zoomScale = TheTacticalView->getMaxZoom() / TheTacticalView->getOverlayZoom();
			width = static_cast<UnsignedInt>(width * (zoomScale));
			height = static_cast<UnsignedInt>(height * (zoomScale));

			// adjust the screen position to draw so the image is centered at the location
			screen.x -= width / 2;
			screen.y -= height / 2;

			// draw the animation
			wad->m_anim->draw( screen.x, screen.y, width, height );

		}  // end if

		// go to the next element in the list
		++it;

	}  // end for

}  // end updateAndDrawWorldAnimations
