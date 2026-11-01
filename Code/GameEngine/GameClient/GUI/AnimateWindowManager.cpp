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

// FILE: AnimateWindowManager.cpp /////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Electronic Arts Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2002 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
//	created:	Mar 2002
//
//	Filename: 	AnimateWindowManager.cpp
//
//	author:		Chris Huybregts
//	
//	purpose:	This will contain the logic behind the different animations that 
//						can happen to a window.
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// USER INCLUDES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/AnimateWindowManager.h"
#include "GameClient/GameWindow.h"
#include "GameClient/Display.h"
#include "GameClient/ProcessAnimateWindow.h"
//-----------------------------------------------------------------------------
// DEFINES ////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// AnimateWindow PUBLIC FUNCTIONS /////////////////////////////////////////////
//-----------------------------------------------------------------------------
AnimateWindow::AnimateWindow( void )
{
	m_delay = 0;
	m_startPos.x = m_startPos.y = 0;
	m_endPos.x = m_endPos.y = 0;
	m_curPos.x = m_curPos.y = 0;
	m_win = nullptr;
	m_animType = AnimTypes::WIN_ANIMATION_NONE;

	m_restPos.x = m_restPos.y = 0;
	m_vel.x = m_vel.y = 0.0f;
	m_needsToFinish = FALSE;
	m_isFinished = FALSE;
	m_endTime = 0;
	m_startTime = 0;
}
AnimateWindow::~AnimateWindow( void )
{
	m_win = nullptr;
}

void AnimateWindow::setAnimData( 	ICoord2D startPos, ICoord2D endPos, 
																	ICoord2D curPos, ICoord2D restPos,
																	Coord2D vel, UnsignedInt startTime,
																	UnsignedInt endTime )

{
	m_startPos = startPos;
	m_endPos = endPos;
	m_curPos = curPos;
	m_restPos = restPos;
	m_vel = vel;
	m_startTime = startTime;
	m_endTime = endTime;

}

//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

static void clearWinList(AnimateWindowList &winList)
{
	AnimateWindow *win = nullptr;
	while (!winList.empty())
	{
		win = *(winList.begin());
		winList.pop_front();
		if (win)
			deleteInstanceOf(win);
		win = nullptr;
	}
}

AnimateWindowManager::AnimateWindowManager( void )
{
// we don't allocate many of these, so no MemoryPools used
	m_slideFromRight = std::make_unique<ProcessAnimateWindowSlideFromRight>();
	m_slideFromRightFast = std::make_unique<ProcessAnimateWindowSlideFromRightFast>();
	m_slideFromLeft = std::make_unique<ProcessAnimateWindowSlideFromLeft>();
	m_slideFromTop = std::make_unique<ProcessAnimateWindowSlideFromTop>();
	m_slideFromTopFast = std::make_unique<ProcessAnimateWindowSlideFromTopFast>();
	m_slideFromBottom = std::make_unique<ProcessAnimateWindowSlideFromBottom>();
	m_spiral = std::make_unique<ProcessAnimateWindowSpiral>();
	m_slideFromBottomTimed = std::make_unique<ProcessAnimateWindowSlideFromBottomTimed>();
	m_winList.clear();
	m_needsUpdate = FALSE;
	m_reverse = FALSE;
	m_winMustFinishList.clear();
}
AnimateWindowManager::~AnimateWindowManager( void )
{
	resetToRestPosition( );
	clearWinList(m_winList);
	clearWinList(m_winMustFinishList);
}

	
void AnimateWindowManager::init( void )
{
	clearWinList(m_winList);
	clearWinList(m_winMustFinishList);
	m_needsUpdate = FALSE;
	m_reverse = FALSE;
}

void AnimateWindowManager::reset( void )
{
	resetToRestPosition();
	clearWinList(m_winList);
	clearWinList(m_winMustFinishList);
	m_needsUpdate = FALSE;
	m_reverse = FALSE;
}

void AnimateWindowManager::update( void )
{
	
	ProcessAnimateWindow *processAnim = nullptr;

	// if we need to update the windows that need to finish, update that list
	if(m_needsUpdate)
	{
		AnimateWindowList::iterator it = m_winMustFinishList.begin();
		m_needsUpdate = FALSE;
		
		while (it != m_winMustFinishList.end())
		{
			AnimateWindow *animWin = *it;
			if (!animWin)
			{
				DEBUG_CRASH(("There's No AnimateWindow in the AnimateWindow List"));
				return;
			}
			processAnim = getProcessAnimate( animWin->getAnimType() );
			if(processAnim)
			{
				if(m_reverse)
				{
					if(!processAnim->reverseAnimateWindow(animWin))
						m_needsUpdate = TRUE;
				}
				else
				{
					if(!processAnim->updateAnimateWindow(animWin))
						m_needsUpdate = TRUE;
				}
			}
			
			it ++;
		}
	}

	AnimateWindowList::iterator it = m_winList.begin();
		
	while (it != m_winList.end())
	{
		AnimateWindow *animWin = *it;
		if (!animWin)
		{
			DEBUG_CRASH(("There's No AnimateWindow in the AnimateWindow List"));
			return;
		}
		processAnim = getProcessAnimate( animWin->getAnimType() );
		if(m_reverse)
		{
			if(processAnim)
				processAnim->reverseAnimateWindow(animWin);			
		}
		else
		{
			if(processAnim)
				processAnim->updateAnimateWindow(animWin);
		}
		it ++;
	}
}


void AnimateWindowManager::registerGameWindow(GameWindow *win, AnimTypes animType, Bool needsToFinish, UnsignedInt ms, UnsignedInt delayMs)
{
	if(!win)
	{	
		DEBUG_CRASH(("Win was NULL as it was passed into registerGameWindow... not good indeed"));
		return;
	}
	if(animType <= AnimTypes::WIN_ANIMATION_NONE || animType >= AnimTypes::WIN_ANIMATION_COUNT )
	{
		DEBUG_CRASH(("an Invalid WIN_ANIMATION type was passed into registerGameWindow... please fix me "));
		return;
	}

	// Create a new AnimateWindow class and fill in it's data.
	AnimateWindow *animWin = newInstance(AnimateWindow);	
	animWin->setGameWindow(win);
	animWin->setAnimType(animType);
	animWin->setNeedsToFinish(needsToFinish);
	animWin->setDelay(delayMs);

	// Run the window through the processAnim's init function.
	ProcessAnimateWindow *processAnim = getProcessAnimate( animType );
	if(processAnim)
	{
		processAnim->setMaxDuration(ms);
		processAnim->initAnimateWindow( animWin );
	}
	
	// Add the Window to the proper list
	if(needsToFinish)
	{
		m_winMustFinishList.push_back(animWin);
		m_needsUpdate = TRUE;
	}
	else
		m_winList.push_back(animWin);
}

ProcessAnimateWindow *AnimateWindowManager::getProcessAnimate( AnimTypes animType )
{
	switch (animType) {
	case AnimTypes::WIN_ANIMATION_SLIDE_RIGHT:
		{
			return m_slideFromRight.get();
		}
	case AnimTypes::WIN_ANIMATION_SLIDE_RIGHT_FAST:
		{
			return m_slideFromRightFast.get();
		}
	case AnimTypes::WIN_ANIMATION_SLIDE_LEFT:
	{
			return m_slideFromLeft.get();
	}
	case AnimTypes::WIN_ANIMATION_SLIDE_TOP:
	{
			return m_slideFromTop.get();
	}
	case AnimTypes::WIN_ANIMATION_SLIDE_BOTTOM:
	{
			return m_slideFromBottom.get();
	}
	case AnimTypes::WIN_ANIMATION_SPIRAL:
	{
			return m_spiral.get();
	}
	case AnimTypes::WIN_ANIMATION_SLIDE_BOTTOM_TIMED:
	{
		return m_slideFromBottomTimed.get();
	}
	case AnimTypes::WIN_ANIMATION_SLIDE_TOP_FAST:
	{
		return m_slideFromTopFast.get();
	}
		default:
		return nullptr;
	}
}

void AnimateWindowManager::reverseAnimateWindow( void )
{
	
	m_reverse = TRUE;
	m_needsUpdate = TRUE;
	ProcessAnimateWindow *processAnim = nullptr;
	
	UnsignedInt maxDelay = 0;
	AnimateWindowList::iterator it = m_winMustFinishList.begin();
	while (it != m_winMustFinishList.end())
	{
		AnimateWindow *animWin = *it;
		if (!animWin)
		{
			DEBUG_CRASH(("There's No AnimateWindow in the AnimateWindow List"));
			return;
		}
		if(animWin->getDelay() > maxDelay)
			maxDelay = animWin->getDelay();
		it ++;
	}

	it = m_winMustFinishList.begin();
	while (it != m_winMustFinishList.end())
	{
		AnimateWindow *animWin = *it;
		if (!animWin)
		{
			DEBUG_CRASH(("There's No AnimateWindow in the AnimateWindow List"));
			return;
		}
		// Run the window through the processAnim's init function.
		 processAnim = getProcessAnimate( animWin->getAnimType() );
		if(processAnim)
		{
			processAnim->initReverseAnimateWindow( animWin, maxDelay );
		}
		
		animWin->setFinished(FALSE);
		it ++;
	}
	
	it = m_winList.begin();
		
	while (it != m_winList.end())
	{
		AnimateWindow *animWin = *it;
		if (!animWin)
		{
			DEBUG_CRASH(("There's No AnimateWindow in the AnimateWindow List"));
			return;
		}
		processAnim = getProcessAnimate( animWin->getAnimType() );
		
		if(processAnim)
			processAnim->initReverseAnimateWindow(animWin);
		animWin->setFinished(FALSE);
		it ++;
	}

}

void AnimateWindowManager::resetToRestPosition( void )
{
	
	m_reverse = TRUE;
	m_needsUpdate = TRUE;
	
	AnimateWindowList::iterator it = m_winMustFinishList.begin();
	while (it != m_winMustFinishList.end())
	{
		AnimateWindow *animWin = *it;
		if (!animWin)
		{
			DEBUG_CRASH(("There's No AnimateWindow in the AnimateWindow List"));
			return;
		}
		ICoord2D restPos = animWin->getRestPos();
		GameWindow *win = animWin->getGameWindow();
		if(win)
			win->winSetPosition(restPos.x, restPos.y);
		it ++;
	}
	it = 	m_winList.begin();
	while (it != m_winList.end())
	{
		AnimateWindow *animWin = *it;
		if (!animWin)
		{
			DEBUG_CRASH(("There's No AnimateWindow in the AnimateWindow List"));
			return;
		}
		ICoord2D restPos = animWin->getRestPos();
		GameWindow *win = animWin->getGameWindow();
		if(win)
			win->winSetPosition(restPos.x, restPos.y);
		it ++;
	}

	
}

//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------



