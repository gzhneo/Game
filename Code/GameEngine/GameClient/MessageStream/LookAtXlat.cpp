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

// LookAtXlat.cpp
// Translate raw input events into camera movement commands
// Author: Michael S. Booth, April 2001

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "windows.h"

#include "Common/GameType.h"
#include "Common/MessageStream.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Recorder.h"
#include "Common/StatsCollector.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameClient/Display.h"
#include "GameClient/GameText.h"
#include "GameClient/Mouse.h"
#include "GameClient/Shell.h"
#include "GameClient/GameClient.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/View.h"
#include "GameClient/Drawable.h"
#include "GameClient/LookAtXlat.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/GameLogic.h"

#include "Common/GlobalData.h"			// for camera pitch angle only
#include "Engine/ScrollTiming.h"
#include "Engine/WheelSteps.h"


enum
{
	DIR_UP = 0,
	DIR_DOWN,
	DIR_LEFT,
	DIR_RIGHT
};

static Bool scrollDir[4] = { false, false, false, false };

Int SCROLL_AMT = 100;
static const Real HWHEEL_STEPS = 4.0f;	// key scroll ticks per notch of wheel tilt

static const Int edgeScrollSize = 3;

static Mouse::MouseCursor prevCursor = Mouse::ARROW;

// Speed factor of one kind of scrolling: its own setting (Settings.ini), else the common ScrollFactor.
static Real scrollFactor(Real own)
{
	return own >= 0.0f ? own : TheGlobalData->m_keyboardScrollFactor;
}

// Edge scrolling in fullscreen and borderless (a window at desktop size); in a window only on request,
// because the cursor leaving the window would keep the camera moving.
static Bool edgeScrollAllowed()
{
	return !TheGlobalData->m_windowed || TheGlobalData->m_borderless || TheGlobalData->m_edgeScrollInWindow;
}

//-----------------------------------------------------------------------------
void LookAtTranslator::setScrolling(Int x)
{
	if (!TheInGameUI->getInputEnabled())
		return;

	prevCursor = TheMouse->getMouseCursor();
	m_isScrolling = true;
	TheInGameUI->setScrolling( TRUE );
	TheTacticalView->setMouseLock( TRUE );
	m_scrollType = x;
	if (x == SCROLL_RMB)
		TheMouse->setDragScroll( TRUE );
	if(TheStatsCollector)
		TheStatsCollector->startScrollTime();
}

//-----------------------------------------------------------------------------
void LookAtTranslator::stopScrolling( void )
{
	m_isScrolling = false;
	TheInGameUI->setScrolling( FALSE );
	TheTacticalView->setMouseLock( FALSE );
	TheMouse->setDragScroll( FALSE );
	TheMouse->setCursor(prevCursor);
	m_scrollType = SCROLL_NONE;
		
	// if we have a stats collectore increment the stats
	if(TheStatsCollector)
		TheStatsCollector->endScrollTime();

}

//-----------------------------------------------------------------------------
LookAtTranslator::LookAtTranslator() :
	m_isScrolling(false),
	m_isRotating(false),
	m_isPitching(false),
	m_isChangingFOV(false),
	m_timestamp(0),
	m_lastPlaneID(INVALID_DRAWABLE_ID),
	m_lastMouseMoveFrame(0),
	m_scrollType(SCROLL_NONE),
	m_lastScrollTickMicros(0)
{
	//Added By Sadullah Nader
	//Initializations misssing and needed
	m_anchor.x = m_anchor.y = 0;
	m_currentPos.x = m_currentPos.y = 0;
	m_originalAnchor.x = m_originalAnchor.y = 0;
	//

	DEBUG_ASSERTCRASH(!TheLookAtTranslator, ("Already have a LookAtTranslator - why do you need two?"));
	::Engine::context().lookAtTranslator = this;
}

//-----------------------------------------------------------------------------
LookAtTranslator::~LookAtTranslator()
{
	if (TheLookAtTranslator == this)
		::Engine::context().lookAtTranslator = nullptr;
}

const ICoord2D* LookAtTranslator::getRMBScrollAnchor(void)
{
	if (m_isScrolling && m_scrollType == SCROLL_RMB)
	{
		return &m_anchor;
	}
	return nullptr;
}

Bool LookAtTranslator::hasMouseMovedRecently( void )
{
	if (m_lastMouseMoveFrame > TheGameLogic->getFrame())
		m_lastMouseMoveFrame = 0; // reset for new game

	if (m_lastMouseMoveFrame + LOGICFRAMES_PER_SECOND < TheGameLogic->getFrame())
		return false;

	return true;
}

void LookAtTranslator::setCurrentPos( const ICoord2D& pos )
{
	m_currentPos = pos;
}

//-----------------------------------------------------------------------------
/**
 * The LookAt Translator is responsible for camera movements. It is directly responsible for
 * right mouse button scrolling, and CTRL-<F key> bookmarking. It also responds to certain
 * LOOKAT message on the message stream.
 */
GameMessageDisposition LookAtTranslator::translateGameMessage(const GameMessage *msg)
{
	GameMessageDisposition disp = GameMessageDisposition::KEEP_MESSAGE;

	GameMessage::Type t = msg->getType();
	switch (t)
	{
		//-----------------------------------------------------------------------------
		case GameMessage::MSG_RAW_KEY_DOWN:
		case GameMessage::MSG_RAW_KEY_UP:
		{
			// get key and state from args
			UnsignedByte key		= static_cast<UnsignedByte>(msg->getArgument( 0 )->integer);
			UnsignedByte state	= static_cast<UnsignedByte>(msg->getArgument( 1 )->integer);
			Bool isPressed = !(BitTest( state, KEY_STATE_UP ));
			
			if (TheShell && TheShell->isShellActive())
				break;

			switch (key)
			{
			case KEY_UP:
				scrollDir[DIR_UP] = isPressed;
				break;
			case KEY_DOWN:
				scrollDir[DIR_DOWN] = isPressed;
				break;
			case KEY_LEFT:
				scrollDir[DIR_LEFT] = isPressed;
				break;
			case KEY_RIGHT:
				scrollDir[DIR_RIGHT] = isPressed;
				break;
			}

			if (TheInGameUI->isSelecting() || (m_isScrolling && m_scrollType != SCROLL_KEY))
				break;

			// see if we need to start/stop scrolling
			Int numDirs = 0;
			for (Int i=0; i<4; ++i)
			{
				if (scrollDir[i])
					numDirs++;
			}

			if (numDirs && !m_isScrolling)
			{
				setScrolling( SCROLL_KEY );
			}
			else if (!numDirs && m_isScrolling)
			{
				stopScrolling();
			}
			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN:
		{
			m_lastMouseMoveFrame = TheGameLogic->getFrame();

			m_anchor = msg->getArgument( 0 )->pixel;
			m_currentPos = msg->getArgument( 0 )->pixel;

			if (!TheInGameUI->isSelecting() && !m_isScrolling)
			{
				setScrolling(SCROLL_RMB);
			}
			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP:
		{
			m_lastMouseMoveFrame = TheGameLogic->getFrame();

			if (m_scrollType == SCROLL_RMB)
			{
				stopScrolling();
			}
			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_RAW_MOUSE_MIDDLE_BUTTON_DOWN:
		{
			m_lastMouseMoveFrame = TheGameLogic->getFrame();

			m_isRotating = true;
			m_anchor = msg->getArgument( 0 )->pixel;
			m_originalAnchor = msg->getArgument( 0 )->pixel;
			m_currentPos = msg->getArgument( 0 )->pixel;
			m_timestamp = TheGameClient->getFrame();
			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_RAW_MOUSE_MIDDLE_BUTTON_UP:
		{
			m_lastMouseMoveFrame = TheGameLogic->getFrame();

			const UnsignedInt CLICK_DURATION = 5;
			const UnsignedInt PIXEL_OFFSET = 5;

			m_isRotating = false;
			Int dx = m_currentPos.x-m_originalAnchor.x;
			if (dx<0) dx = -dx;
			Int dy = m_currentPos.y-m_originalAnchor.y;
			Bool didMove = dx>PIXEL_OFFSET || dy>PIXEL_OFFSET;
			// if middle button is "clicked", reset to "home" orientation
			if (!didMove && TheGameClient->getFrame() - m_timestamp < CLICK_DURATION)
			{
				TheTacticalView->setAngleAndPitchToDefault();
				TheTacticalView->setZoomToDefault();
			}

			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_RAW_MOUSE_POSITION:
		{
			if (m_currentPos.x != msg->getArgument( 0 )->pixel.x || m_currentPos.y != msg->getArgument( 0 )->pixel.y)
				m_lastMouseMoveFrame = TheGameLogic->getFrame();

			m_currentPos = msg->getArgument( 0 )->pixel;
			
			UnsignedInt height = TheDisplay->getHeight();
			UnsignedInt width  = TheDisplay->getWidth();

			if (TheInGameUI->getInputEnabled() == FALSE) {
				// We don't care how we're scrolling, just stop.
				if (m_isScrolling)
					stopScrolling();
				break;
			}

			{
				const Bool atEdge = m_currentPos.x < edgeScrollSize || m_currentPos.y < edgeScrollSize
					|| static_cast<UnsignedInt>(m_currentPos.y) >= height-edgeScrollSize || static_cast<UnsignedInt>(m_currentPos.x) >= width-edgeScrollSize;
				if (m_isScrolling)
				{
					// also ends when edge scrolling was switched off meanwhile, otherwise the mode would stick
					if (m_scrollType == SCROLL_SCREENEDGE && (!atEdge || !edgeScrollAllowed()))
						stopScrolling();
				}
				else if (atEdge && edgeScrollAllowed())
				{
					setScrolling(SCROLL_SCREENEDGE);
				}
			}

			// rotate the view
			if (m_isRotating)
			{
				const Real FACTOR = 0.01f;

				Real angle = FACTOR * (m_currentPos.x - m_anchor.x);

				TheTacticalView->setAngle( TheTacticalView->getAngle() + angle );
				m_anchor = msg->getArgument( 0 )->pixel;
			}

			// rotate the view up/down
			if (m_isPitching)
			{
				const Real FACTOR = 0.01f;

				Real angle = FACTOR * (m_currentPos.y - m_anchor.y);

				TheTacticalView->setPitch( TheTacticalView->getPitch() + angle );
				m_anchor = msg->getArgument( 0 )->pixel;
			}

#if defined(_DEBUG)
			// adjust the field of view
			if (m_isChangingFOV)
			{
				const Real FACTOR = 0.01f;

				Real angle = FACTOR * (m_currentPos.y - m_anchor.y);

				TheTacticalView->setFieldOfView( TheTacticalView->getFieldOfView() + angle );
				m_anchor = msg->getArgument( 0 )->pixel;
			}
#endif
			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_RAW_MOUSE_WHEEL:
		{
			m_lastMouseMoveFrame = TheGameLogic->getFrame();

			Int spin = msg->getArgument( 1 )->integer;
			// a fine wheel zooms with MSG_RAW_MOUSE_WHEEL_FINE instead
			if (TheMouse->hasFineWheel())
				spin = 0;

			if (spin > 0)
			{
				for ( ; spin > 0; spin--)
					TheTacticalView->zoomIn();
			}
			else
			{
				for ( ;spin < 0; spin++ )
					TheTacticalView->zoomOut();
			}
		}
		
		//-----------------------------------------------------------------------------
		case GameMessage::MSG_RAW_MOUSE_WHEEL_FINE:
		{
			m_lastMouseMoveFrame = TheGameLogic->getFrame();
			TheTacticalView->zoomBy( static_cast<Real>( msg->getArgument( 1 )->integer ) / static_cast<Real>( ::Engine::WHEEL_NOTCH ) );
			break;
		}

		//-----------------------------------------------------------------------------
		// Tilting the wheel scrolls sideways: one notch moves as far as a tick of key scrolling.
		case GameMessage::MSG_RAW_MOUSE_HWHEEL:
		{
			m_lastMouseMoveFrame = TheGameLogic->getFrame();
			const Real notches = static_cast<Real>( msg->getArgument( 1 )->integer ) / static_cast<Real>( ::Engine::WHEEL_NOTCH );
			const Real keyFactor = scrollFactor(TheGlobalData->m_scrollFactorKeyboard);
			Coord2D offset;
			offset.x = notches * static_cast<Real>(::Engine::scrollKeyStep(TheGlobalData->m_horizontalScrollSpeedFactor, SCROLL_AMT, keyFactor)) * HWHEEL_STEPS;
			offset.y = 0.0f;
			TheTacticalView->scrollBy( &offset );
			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_META_OPTIONS:
		{
			// stop the scrolling
			stopScrolling();
			// let the message drop through, cause we need to process this message for 
			// selection as well.
			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_FRAME_TICK:
		{
			Coord2D offset = {0, 0};

			// one step per logic tick, scaled by the real time since the last one
			const UnsignedInt64 now = ::Engine::Clock::microseconds();
			const Real timeScale = static_cast<Real>(::Engine::scrollTickScale(m_lastScrollTickMicros != 0 ? now - m_lastScrollTickMicros : 0));
			m_lastScrollTickMicros = now;

			// If we've been forced to stop scrolling (script action?) then stop
			if (m_isScrolling && !TheInGameUI->isScrolling())
			{
				TheInGameUI->setScrollAmount(offset);
				stopScrolling();
			}
			else
			// scroll the view
			if (m_isScrolling)
			{
				switch (m_scrollType)
				{
				case SCROLL_RMB:
					{
						if (TheInGameUI->shouldMoveRMBScrollAnchor())
						{
							Int maxX = TheDisplay->getWidth()/2;
							Int maxY = TheDisplay->getHeight()/2;

							if (m_currentPos.x + maxX < m_anchor.x)
								m_anchor.x = m_currentPos.x + maxX;
							else if (m_currentPos.x - maxX > m_anchor.x)
								m_anchor.x = m_currentPos.x - maxX;

							if (m_currentPos.y + maxY < m_anchor.y)
								m_anchor.y = m_currentPos.y + maxY;
							else if (m_currentPos.y - maxY > m_anchor.y)
								m_anchor.y = m_currentPos.y - maxY;
						}

						const Real hFactor = TheGlobalData->m_horizontalScrollSpeedFactor;
						const Real vFactor = TheGlobalData->m_verticalScrollSpeedFactor;
						const Real dx = static_cast<Real>(m_currentPos.x - m_anchor.x);
						const Real dy = static_cast<Real>(m_currentPos.y - m_anchor.y);
						Coord2D vec;
						vec.x = hFactor * dx;
						vec.y = vFactor * dy;
						vec.normalize();
						const Real dragFactor = scrollFactor(TheGlobalData->m_scrollFactorDrag);
						offset.x = static_cast<Real>(::Engine::scrollDragStep(hFactor, dx, vec.x, dragFactor));
						offset.y = static_cast<Real>(::Engine::scrollDragStep(vFactor, dy, vec.y, dragFactor));
					}
					break;
				case SCROLL_KEY:
					{
						const Real keyFactor = scrollFactor(TheGlobalData->m_scrollFactorKeyboard);
						const Real stepX = static_cast<Real>(::Engine::scrollKeyStep(TheGlobalData->m_horizontalScrollSpeedFactor, SCROLL_AMT, keyFactor));
						const Real stepY = static_cast<Real>(::Engine::scrollKeyStep(TheGlobalData->m_verticalScrollSpeedFactor, SCROLL_AMT, keyFactor));
						if (scrollDir[DIR_UP])
							offset.y -= stepY;
						if (scrollDir[DIR_DOWN])
							offset.y += stepY;
						if (scrollDir[DIR_LEFT])
							offset.x -= stepX;
						if (scrollDir[DIR_RIGHT])
							offset.x += stepX;
					}
					break;
				case SCROLL_SCREENEDGE:
					{
						const Real edgeFactor = scrollFactor(TheGlobalData->m_scrollFactorEdge);
						const Real stepX = static_cast<Real>(::Engine::scrollKeyStep(TheGlobalData->m_horizontalScrollSpeedFactor, SCROLL_AMT, edgeFactor));
						const Real stepY = static_cast<Real>(::Engine::scrollKeyStep(TheGlobalData->m_verticalScrollSpeedFactor, SCROLL_AMT, edgeFactor));
						UnsignedInt height = TheDisplay->getHeight();
						UnsignedInt width  = TheDisplay->getWidth();
						if (m_currentPos.y < edgeScrollSize)
							offset.y -= stepY;
						if (static_cast<UnsignedInt>(m_currentPos.y) >= height-edgeScrollSize)
							offset.y += stepY;
						if (m_currentPos.x < edgeScrollSize)
							offset.x -= stepX;
						if (static_cast<UnsignedInt>(m_currentPos.x) >= width-edgeScrollSize)
							offset.x += stepX;
					}
					break;
				}

				// Above the normal maximum camera height the world speed grows with the height, so the picture
				// scrolls as fast as at the normal height.
				Real heightScale = 1.0f;
				if (TheTacticalView->getZoomOutFactor() > 1.0f && TheGlobalData->m_maxCameraHeight > 0.0f)
				{
					const Real ratio = TheTacticalView->getHeightAboveGround() / TheGlobalData->m_maxCameraHeight;
					if (ratio > 1.0f)
						heightScale = ratio;
				}
				offset.x *= timeScale * heightScale;
				offset.y *= timeScale * heightScale;
				TheInGameUI->setScrollAmount(offset);
				TheTacticalView->scrollBy( &offset );
			}
			else	//not scrolling so reset amount
				TheInGameUI->setScrollAmount(offset);

			//if (TheGlobalData->m_saveCameraInReplay /*&& TheRecorder->getMode() != RECORDERMODETYPE_PLAYBACK *//**/&& (TheGameLogic->isInSinglePlayerGame() || TheGameLogic->isInSkirmishGame())/**/)
			//if (TheGlobalData->m_saveCameraInReplay && (TheGameLogic->isInMultiplayerGame() || TheGameLogic->isInSinglePlayerGame() || TheGameLogic->isInSkirmishGame()))
			if (TheGlobalData->m_saveCameraInReplay && (TheGameLogic->isInSinglePlayerGame() || TheGameLogic->isInSkirmishGame()))
			{
				ViewLocation currentView;
				TheTacticalView->getLocation(&currentView);
				GameMessage *msg3 = TheMessageStream->appendMessage( GameMessage::MSG_SET_REPLAY_CAMERA );
				msg3->appendLocationArgument( currentView.m_pos );
				msg3->appendRealArgument( currentView.m_angle );
				msg3->appendRealArgument( currentView.m_pitch );
				msg3->appendRealArgument( currentView.m_zoom );
				msg3->appendIntegerArgument( static_cast<Int>(TheMouse->getMouseCursor()) );
				msg3->appendPixelArgument( m_currentPos );
			}
			break;
		}

		// ------------------------------------------------------------------------
#if defined(_DEBUG)
		case GameMessage::MSG_META_DEMO_BEGIN_ADJUST_PITCH:
		{
			DEBUG_ASSERTCRASH(!m_isPitching, ("hmm, mismatched m_isPitching"));
			m_isPitching = true;
			disp = GameMessageDisposition::DESTROY_MESSAGE;
			break;
		}
#endif // #if defined(_DEBUG)

		// ------------------------------------------------------------------------
#if defined(_DEBUG)
		case GameMessage::MSG_META_DEMO_END_ADJUST_PITCH:
		{
			DEBUG_ASSERTCRASH(m_isPitching, ("hmm, mismatched m_isPitching"));
			m_isPitching = false;
			disp = GameMessageDisposition::DESTROY_MESSAGE;
			break;
		}
#endif // #if defined(_DEBUG)

		// ------------------------------------------------------------------------
#if defined(_DEBUG)
		case GameMessage::MSG_META_DEMO_DESHROUD:
		{
			ThePartitionManager->revealMapForPlayerPermanently( ThePlayerList->getLocalPlayer()->getPlayerIndex() );
			break;
		}
#endif // #if defined(_DEBUG)

		// ------------------------------------------------------------------------

		// ------------------------------------------------------------------------
#if defined(_DEBUG)
		case GameMessage::MSG_META_DEMO_ENSHROUD:
		{
			// Need to first undo the permanent Look laid down by DEMO_DESHROUD, then blast a shroud dollop.
			ThePartitionManager->undoRevealMapForPlayerPermanently( ThePlayerList->getLocalPlayer()->getPlayerIndex() );
			ThePartitionManager->shroudMapForPlayer( ThePlayerList->getLocalPlayer()->getPlayerIndex() );
			break;
		}
#endif // #if defined(_DEBUG)

		// ------------------------------------------------------------------------
#if defined(_DEBUG)
		case GameMessage::MSG_META_DEMO_BEGIN_ADJUST_FOV:
		{
			//DEBUG_ASSERTCRASH(!m_isChangingFOV, ("hmm, mismatched m_isChangingFOV"));
			m_isChangingFOV = true;
			m_anchor = m_currentPos;
			break;
		}
#endif // #if defined(_DEBUG)

		// ------------------------------------------------------------------------
#if defined(_DEBUG)
		case GameMessage::MSG_META_DEMO_END_ADJUST_FOV:
		{
		//	DEBUG_ASSERTCRASH(m_isChangingFOV, ("hmm, mismatched m_isChangingFOV"));
			m_isChangingFOV = false;
			break;
		}
#endif // #if defined(_DEBUG)

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_SAVE_VIEW1:
		case GameMessage::MSG_META_SAVE_VIEW2:
		case GameMessage::MSG_META_SAVE_VIEW3:
		case GameMessage::MSG_META_SAVE_VIEW4:
		case GameMessage::MSG_META_SAVE_VIEW5:
		case GameMessage::MSG_META_SAVE_VIEW6:
		case GameMessage::MSG_META_SAVE_VIEW7:
		case GameMessage::MSG_META_SAVE_VIEW8:
		{
			Int slot = t - GameMessage::MSG_META_SAVE_VIEW1 + 1;
			if ( slot > 0 && slot <= MAX_VIEW_LOCS )
			{
				TheTacticalView->getLocation( &m_viewLocation[slot-1] );
				UnicodeString msg2;
				msg2.format( TheGameText->fetch( "GUI:BookmarkXSet" ), slot );
				TheInGameUI->message( msg2 );
			}
			disp = GameMessageDisposition::DESTROY_MESSAGE;
			break;
		}

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_VIEW_VIEW1:
		case GameMessage::MSG_META_VIEW_VIEW2:
		case GameMessage::MSG_META_VIEW_VIEW3:
		case GameMessage::MSG_META_VIEW_VIEW4:
		case GameMessage::MSG_META_VIEW_VIEW5:
		case GameMessage::MSG_META_VIEW_VIEW6:
		case GameMessage::MSG_META_VIEW_VIEW7:
		case GameMessage::MSG_META_VIEW_VIEW8:
		{
			Int slot = t - GameMessage::MSG_META_VIEW_VIEW1 + 1;
			if ( slot > 0 && slot <= MAX_VIEW_LOCS )
			{
				TheTacticalView->setLocation( &m_viewLocation[slot-1] );
			}
			disp = GameMessageDisposition::DESTROY_MESSAGE;
			break;
		}

		//-----------------------------------------------------------------------------
#if defined(_DEBUG)
		case GameMessage::MSG_META_DEMO_LOCK_CAMERA_TO_PLANES:
		{
			Drawable *first = nullptr;

			if (m_lastPlaneID)
				first = TheGameClient->findDrawableByID( m_lastPlaneID );

			if (first == nullptr)
				first = TheGameClient->firstDrawable();

			if (first)
			{
				Drawable *d = first;
				Bool done = false;

				while(!done)
				{
					// get next Drawable, wrapping around to head of list if necessary
					d = d->getNextDrawable();
					if (d == nullptr)
						d = TheGameClient->firstDrawable();

					// if we've found an airborne object, lock onto it
// "isAboveTerrain" only indicates that we are currently in the air, but that
// could be the case if we are a buggy jumping a hill, or a unit being paradropped.
// the right thing would be to look at the locomotors.
// so this isn't really right, but will suffice for demo purposes.
					if (d->getObject() && d->getObject()->isAboveTerrain() )
					{
						Bool doLock = true;

						// but don't lock onto projectiles
						ProjectileUpdateInterface* pui = nullptr;
						for (BehaviorModule** u = d->getObject()->getBehaviorModules(); *u; ++u)
						{
							if ((pui = (*u)->getProjectileUpdateInterface()) != nullptr)
							{
								doLock = false;
								break;
							}
						}

						if (doLock)
						{
							TheTacticalView->setCameraLock( d->getObject()->getID() );
							m_lastPlaneID = d->getID();
							done = true;
							break;
						}
					} // if airborne found

					// if we're back to the first, quit
					if (d == first)
						break;
				} // while
			}	// end plane lock

			disp = GameMessageDisposition::DESTROY_MESSAGE;
			break;
		}
#endif // #if defined(_DEBUG)

	}  // end switch

	return disp;

}  // end LookAtTranslator

void LookAtTranslator::resetModes()
{
	m_isScrolling = FALSE;
	m_isRotating = FALSE;
	m_isPitching = FALSE;
	m_isChangingFOV = FALSE;
}