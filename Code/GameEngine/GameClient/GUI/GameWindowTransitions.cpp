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

// FILE: GameWindowTransitions.cpp /////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Electronic Arts Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2002 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
//	created:	Dec 2002
//
//	Filename: 	GameWindowTransitions.cpp
//
//	author:		Chris Huybregts
//	
//	purpose:	
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

//-----------------------------------------------------------------------------
// USER INCLUDES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "GameLogic/GameLogic.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
//-----------------------------------------------------------------------------
// DEFINES ////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
const FieldParse GameWindowTransitionsHandler::m_gameWindowTransitionsFieldParseTable[] = 
{

	{ "Window",		GameWindowTransitionsHandler::parseWindow,	NULL, NULL	},
	{ "FireOnce",	INI::parseBool,															nullptr, offsetof( TransitionGroup, m_fireOnce) 	},
	
	{ nullptr,										nullptr,													nullptr, 0 }  // keep this last

};

void INI::parseWindowTransitions( INI* ini )
{
	AsciiString name;
	TransitionGroup *g;

	// read the name
	const char* c = ini->getNextToken();
	name.set( c );	

	// find existing item if present
	
	DEBUG_ASSERTCRASH( TheTransitionHandler, ("parseWindowTransitions: TheTransitionHandler doesn't exist yet\n") );
	if( !TheTransitionHandler )
		return;

	// If we have a previously allocated control bar, this will return a cleared out pointer to it so we
	// can overwrite it	
	g = TheTransitionHandler->getNewGroup( name );

	// sanity
	DEBUG_ASSERTCRASH( g, ("parseWindowTransitions: Unable to allocate group '%s'\n", name.str()) );

	// parse the ini definition
	ini->initFromINI( g, TheTransitionHandler->getFieldParse() );


}
//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
Transition *getTransitionForStyle( Int style )
{
	switch (style) {
	case TRANSITION_FLASH:
		return NEW FlashTransition;
	case BUTTON_TRANSITION_FLASH:
		return NEW ButtonFlashTransition;
	case WIN_FADE_TRANSITION:
		return NEW FadeTransition;
	case WIN_SCALE_UP_TRANSITION:
		return NEW ScaleUpTransition;
	case MAINMENU_SCALE_UP_TRANSITION:
		return NEW MainMenuScaleUpTransition;
	case TEXT_TYPE_TRANSITION:
		return NEW TextTypeTransition;
	case SCREEN_FADE_TRANSITION:
		return NEW ScreenFadeTransition;
	case COUNT_UP_TRANSITION:
		return NEW CountUpTransition;
	case FULL_FADE_TRANSITION:
		return NEW FullFadeTransition;
	case TEXT_ON_FRAME_TRANSITION:
		return NEW TextOnFrameTransition;
	case REVERSE_SOUND_TRANSITION:
		return NEW ReverseSoundTransition;

	case MAINMENU_MEDIUM_SCALE_UP_TRANSITION:
		return NEW MainMenuMediumScaleUpTransition;
	case MAINMENU_SMALL_SCALE_DOWN_TRANSITION:
		return NEW MainMenuSmallScaleDownTransition;
	case CONTROL_BAR_ARROW_TRANSITION:
		return NEW ControlBarArrowTransition;
	case SCORE_SCALE_UP_TRANSITION:
		return NEW ScoreScaleUpTransition;

	default:
		DEBUG_ASSERTCRASH(FALSE, ("getTransitionForStyle:: An invalid style was passed in. Style = %d", style));
		return nullptr;
	}
}

TransitionWindow::TransitionWindow( void )
{
	m_currentFrameDelay = m_frameDelay = 0;
	m_style = 0;

	m_winID = NAMEKEY_INVALID;
	m_win = nullptr;
	m_transition = nullptr;
}

TransitionWindow::~TransitionWindow( void )
{
	m_win = nullptr;
	m_transition.reset();
}

Bool TransitionWindow::init( void )
{
	m_winID = TheNameKeyGenerator->nameToKey(m_winName);
	m_win		= TheWindowManager->winGetWindowFromId(nullptr, m_winID);
	m_currentFrameDelay = m_frameDelay;
//	DEBUG_ASSERTCRASH( m_win, ("TransitionWindow::init Failed to find window %s", m_winName.str()));
//	if( !m_win )
//		return FALSE;
	
	m_transition.reset( getTransitionForStyle( m_style ) );
	m_transition->init(m_win);

	return TRUE;
}

void TransitionWindow::update( Int frame )
{
	if(frame < m_currentFrameDelay || frame > (m_currentFrameDelay + m_transition->getFrameLength()))
		return;
	
	if(m_transition)
		m_transition->update( frame - m_currentFrameDelay);
}

Bool TransitionWindow::isFinished( void )
{
	if(m_transition)
		return m_transition->isFinished();
	return TRUE;
}

void TransitionWindow::reverse( Int totalFrames )
{
	//m_currentFrameDelay = totalFrames - (m_transition->getFrameLength() + m_frameDelay);
	if(m_transition)
		m_transition->reverse();
}

void TransitionWindow::skip( void )
{
	if(m_transition)
		m_transition->skip();
}

void TransitionWindow::draw( void )
{
	if(m_transition)
		m_transition->draw();
}

Int TransitionWindow::getTotalFrames( void )
{
	if(m_transition)
	{
		return m_frameDelay + m_transition->getFrameLength();
	}

	return m_frameDelay;
}

//-----------------------------------------------------------------------------
TransitionGroup::TransitionGroup( void )
{
	m_currentFrame = 0;
	m_fireOnce = FALSE;
}

TransitionGroup::~TransitionGroup( void )
{
	m_transitionWindowList.clear();
}

void TransitionGroup::init( void )
{
	m_currentFrame = 0;
	m_directionMultiplier = 1;
	TransitionWindowList::iterator it = m_transitionWindowList.begin();
	while (it != m_transitionWindowList.end())
	{
		TransitionWindow *tWin = it->get();
		tWin->init();
		it++;
	}

}

void TransitionGroup::update( void )
{
	m_currentFrame += m_directionMultiplier; // we go forward or backwards depending.
	TransitionWindowList::iterator it = m_transitionWindowList.begin();
	while (it != m_transitionWindowList.end())
	{
		TransitionWindow *tWin = it->get();
		tWin->update(m_currentFrame);
		it++;
	}	
}

Bool TransitionGroup::isFinished( void )
{
	TransitionWindowList::iterator it = m_transitionWindowList.begin();
	while (it != m_transitionWindowList.end())
	{
		TransitionWindow *tWin = it->get();
		if(tWin->isFinished() == FALSE)
			return FALSE;
		it++;
	}

	return TRUE;
}

void TransitionGroup::reverse( void )
{
	Int totalFrames =0;
	m_directionMultiplier = -1;
	
	TransitionWindowList::iterator it = m_transitionWindowList.begin();
	while (it != m_transitionWindowList.end())
	{
		TransitionWindow *tWin = it->get();
		Int winFrames = tWin->getTotalFrames();
		if(winFrames > totalFrames)
			totalFrames = winFrames;
		it++;
	}
	it = m_transitionWindowList.begin();
	while (it != m_transitionWindowList.end())
	{
		TransitionWindow *tWin = it->get();
		tWin->reverse(totalFrames);
		it++;
	}
	m_currentFrame = totalFrames;
//	m_currentFrame ++;
}

Bool TransitionGroup::isReversed( void )
{
	if(m_directionMultiplier < 0)
		return TRUE;
	return FALSE;
}

void TransitionGroup::skip ( void )
{
	TransitionWindowList::iterator it = m_transitionWindowList.begin();
	while (it != m_transitionWindowList.end())
	{
		TransitionWindow *tWin = it->get();
		tWin->skip();
		it++;
	}
}

void TransitionGroup::draw ( void )
{
	TransitionWindowList::iterator it = m_transitionWindowList.begin();
	while (it != m_transitionWindowList.end())
	{
		TransitionWindow *tWin = it->get();
		tWin->draw();
		it++;
	}
}

void TransitionGroup::addWindow( std::unique_ptr<TransitionWindow> transWin )
{
	if(!transWin)
		return;
	m_transitionWindowList.push_back(std::move(transWin));
}

//-----------------------------------------------------------------------------

GameWindowTransitionsHandler::GameWindowTransitionsHandler(void)
{
	m_currentGroup = nullptr;
	m_pendingGroup = nullptr;
	m_drawGroup = nullptr;
	m_secondaryDrawGroup = nullptr;

}

GameWindowTransitionsHandler::~GameWindowTransitionsHandler( void )
{
	m_currentGroup = nullptr;
	m_pendingGroup = nullptr;
	m_drawGroup = nullptr;
	m_secondaryDrawGroup = nullptr;

	m_transitionGroupList.clear();
}

void GameWindowTransitionsHandler::init(void )
{
	m_currentGroup = nullptr;
	m_pendingGroup = nullptr;
	m_drawGroup = nullptr;
	m_secondaryDrawGroup = nullptr;
}

void GameWindowTransitionsHandler::load(void )
{
	INI ini;
	// Read from INI all the ControlBarSchemes
	ini.load( AsciiString( "Data\\INI\\WindowTransitions.ini" ), INILoadType::INI_LOAD_OVERWRITE, nullptr );

}

void GameWindowTransitionsHandler::reset( void )
{
	m_currentGroup = nullptr;
	m_pendingGroup = nullptr;
	m_drawGroup = nullptr;
	m_secondaryDrawGroup = nullptr;

}

void GameWindowTransitionsHandler::update( void )
{
	if(m_drawGroup != m_currentGroup)
		m_secondaryDrawGroup = m_drawGroup;
	else
		m_secondaryDrawGroup = nullptr;

	m_drawGroup = m_currentGroup;
	if(m_currentGroup && !m_currentGroup->isFinished())
		m_currentGroup->update();

	if(m_currentGroup && m_currentGroup->isFinished() && m_currentGroup->isFireOnce())
	{
		m_currentGroup = nullptr;
	}

	if(m_currentGroup && m_pendingGroup && m_currentGroup->isFinished())
	{
		m_currentGroup = m_pendingGroup;
		m_pendingGroup = nullptr;
	}

	if(!m_currentGroup && m_pendingGroup)
	{
		m_currentGroup = m_pendingGroup;
		m_pendingGroup = nullptr;
	}

	if(m_currentGroup && m_currentGroup->isFinished() && m_currentGroup->isReversed())
		m_currentGroup = nullptr;
}


void GameWindowTransitionsHandler::draw( void )
{
//	if( TheGameLogic->getFrame() > 0 )//if( areTransitionsEnabled() ) //KRIS
	if(m_drawGroup)
			m_drawGroup->draw();
	if(m_secondaryDrawGroup)
		m_secondaryDrawGroup->draw();
}

void GameWindowTransitionsHandler::setGroup(AsciiString groupName, Bool immidiate )
{
	if(groupName.isEmpty() && immidiate)
		m_currentGroup = nullptr;
	if(immidiate && m_currentGroup)
	{
		m_currentGroup->skip();
		m_currentGroup = findGroup(groupName);
		if(m_currentGroup)
			m_currentGroup->init();
		return;
	}
	
	if(m_currentGroup)
	{
		if(!m_currentGroup->isFireOnce() && !m_currentGroup->isReversed())
			m_currentGroup->reverse();
		m_pendingGroup = findGroup(groupName);
		if(m_pendingGroup)
			m_pendingGroup->init();
		return;
	}

	m_currentGroup = findGroup(groupName);
	if(m_currentGroup)
		m_currentGroup->init();

	
	
}

void GameWindowTransitionsHandler::reverse( AsciiString groupName )
{
	TransitionGroup *g = findGroup(groupName);
	if( !g )
		return;
	if( m_currentGroup == g )
	{
		m_currentGroup->reverse();
		return;
	}
	if( m_pendingGroup == g)
	{
		m_pendingGroup = nullptr;
		return;
	}
	if(m_currentGroup)
		m_currentGroup->skip();
	if(m_pendingGroup)
		m_pendingGroup->skip();

	m_currentGroup = g;
	m_currentGroup->init();
	m_currentGroup->skip();
	m_currentGroup->reverse();
	m_pendingGroup = nullptr;
}

void GameWindowTransitionsHandler::remove( AsciiString groupName,  Bool skipPending )
{
	TransitionGroup *g = findGroup(groupName);
	if( !g )
		return;
	if(m_pendingGroup == g)
	{
		if(skipPending)
			m_pendingGroup->skip();

		m_pendingGroup = nullptr;
	}
	if(m_currentGroup == g)
	{
		m_currentGroup->skip();
		m_currentGroup = nullptr;
		if(m_pendingGroup)
			m_currentGroup = m_pendingGroup;
	}
}

TransitionGroup *GameWindowTransitionsHandler::getNewGroup( AsciiString name )
{
	if(name.isEmpty())
		return nullptr;

	// test to see if we're trying to add an already exisitng group.
	if(findGroup(name))
	{
		DEBUG_ASSERTCRASH(FALSE, ("GameWindowTransitionsHandler::getNewGroup - We already have a group %s", name.str()));
		return nullptr;
	}
	auto newGroup = std::make_unique<TransitionGroup>();
	TransitionGroup *g = newGroup.get();
	g->setName(name);
	m_transitionGroupList.push_back(std::move(newGroup));
	return g;
}

Bool GameWindowTransitionsHandler::isFinished( void )
{
	if(m_currentGroup)
		return m_currentGroup->isFinished();
	return TRUE;
}

//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
TransitionGroup *GameWindowTransitionsHandler::findGroup( AsciiString groupName )
{
	if(groupName.isEmpty())
		return nullptr;

	TransitionGroupList::iterator it = m_transitionGroupList.begin();
	while( it != m_transitionGroupList.end() )
	{
		TransitionGroup *g = it->get();
		if(groupName.compareNoCase(g->getName()) == 0)
			return g;
		it++;
	}
	return nullptr;
}

void GameWindowTransitionsHandler::parseWindow( INI* ini, void *instance, void *store, const void *userData )
{
	static const FieldParse myFieldParse[] = 
		{
			{ "WinName",				INI::parseAsciiString,		nullptr,									offsetof( TransitionWindow, m_winName ) },
      { "Style",					INI::parseLookupList,			TransitionStyleNames,	offsetof( TransitionWindow, m_style ) },
			{ "FrameDelay",			INI::parseInt,						nullptr,									offsetof( TransitionWindow, m_frameDelay ) },
			{ nullptr,							nullptr,											nullptr, 0 }  // keep this last
		};
	auto transWin = std::make_unique<TransitionWindow>();
	ini->initFromINI(transWin.get(), myFieldParse);
	(static_cast<TransitionGroup*>(instance))->addWindow(std::move(transWin));
}

