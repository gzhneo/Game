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

// FILE: ControlBarResizer.cpp /////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Electronic Arts Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2002 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
//	created:	Sep 2002
//
//	Filename: 	ControlBarResizer.cpp
//
//	author:		Chris Huybregts
//	
//	purpose:	We want a "squished" control bar, this is the methods that will do it
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
#include "GameClient/ControlBar.h"
#include "GameClient/ControlBarResizer.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Display.h"
//-----------------------------------------------------------------------------
// DEFINES ////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

const FieldParse ControlBarResizer::m_controlBarResizerParseTable[] = 
{
	{ "AltPosition",		INI::parseICoord2D,						nullptr, offsetof( ResizerWindow, m_altPos ) },
	{ "AltSize",				INI::parseICoord2D,						nullptr, offsetof( ResizerWindow, m_altSize ) },
	{ nullptr,										nullptr,													nullptr, 0 }  // keep this last

};
//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

ResizerWindow::ResizerWindow(void)
{
	m_defaultPos.x = m_defaultPos.y = 0;
	m_defaultSize.x = m_defaultSize.y = 0;
	m_altSize.x = m_altSize.y = 0;
	m_altPos.x = m_altPos.y = 0;
}

ControlBarResizer::ControlBarResizer( void )
{

}
ControlBarResizer::~ControlBarResizer( void )
{
	m_resizerWindowsList.clear();
}
	
void ControlBarResizer::init( void )
{
	INI ini;
	// Read from INI all the ControlBarSchemes
	ini.load( AsciiString( "Data\\INI\\ControlBarResizer.ini" ), INILoadType::INI_LOAD_OVERWRITE, nullptr );

}

	
ResizerWindow *ControlBarResizer::findResizerWindow( AsciiString name )
{
	ResizerWindowList::iterator it = m_resizerWindowsList.begin();

	while (it != m_resizerWindowsList.end())
	{
		ResizerWindow *rWin = it->get();
		if( !rWin )
		{
			DEBUG_ASSERTCRASH(FALSE,("There's no resizerWindow in ControlBarResizer::findResizerWindow"));
			it++;
			continue;
		}
		// find the scheme that best matches our resolution
		if(rWin->m_name.compare(name) == 0)
		{
			return rWin;
		}
		it ++;	
	}
	return nullptr;
}

ResizerWindow *ControlBarResizer::newResizerWindow( AsciiString name )
{
	auto newRwin = std::make_unique<ResizerWindow>();

	newRwin->m_name = name;
	GameWindow *win = nullptr;
	win = TheWindowManager->winGetWindowFromId(nullptr, TheNameKeyGenerator->nameToKey(name));
	if( !win )
	{
		DEBUG_ASSERTCRASH(win,("ControlBarResizer::newResizerWindow could not find window %s Are you sure that window is loaded yet?", name.str()) );
		return nullptr;
	}
	win->winGetPosition(&newRwin->m_defaultPos.x,&newRwin->m_defaultPos.y);
	win->winGetSize(&newRwin->m_defaultSize.x,&newRwin->m_defaultSize.y);
	ResizerWindow *result = newRwin.get();
	m_resizerWindowsList.push_back(std::move(newRwin));
	return result;
}	
void ControlBarResizer::sizeWindowsDefault( void )
{
	ResizerWindowList::iterator it = m_resizerWindowsList.begin();
	GameWindow *win = nullptr;
	while (it != m_resizerWindowsList.end())
	{
		ResizerWindow *rWin = it->get();
		if( !rWin )
		{
			DEBUG_ASSERTCRASH(FALSE,("There's no resizerWindow in ControlBarResizer::sizeWindowsDefault"));
			it++;
			continue;
		}
		win = TheWindowManager->winGetWindowFromId(nullptr, TheNameKeyGenerator->nameToKey(rWin->m_name));
		if(!win)
		{
			it++;
			continue;
		}
		win->winSetPosition(rWin->m_defaultPos.x, rWin->m_defaultPos.y);
		win->winSetSize(rWin->m_defaultSize.x, rWin->m_defaultSize.y);
		DEBUG_LOG(("sizeWindowsDefault:%s pos X:%d pos Y: %d size X:%d sizeY: %d",rWin->m_name.str(),rWin->m_defaultPos.x, rWin->m_defaultPos.y,rWin->m_defaultSize.x, rWin->m_defaultSize.y ));
		it ++;	
	}
}
void ControlBarResizer::sizeWindowsAlt( void )
{
	ResizerWindowList::iterator it = m_resizerWindowsList.begin();
	GameWindow *win = nullptr;
	// AltPos/AltSize are 800x600 coordinates; scale uniformly and center like parseScreenRect.
	Real x, y;
	Int ox, oy;
	TheDisplay->getUILayoutTransform( 800, 600, &x, &y, &ox, &oy );
	while (it != m_resizerWindowsList.end())
	{
		ResizerWindow *rWin = it->get();
		if( !rWin )
		{
			DEBUG_ASSERTCRASH(FALSE,("There's no resizerWindow in ControlBarResizer::sizeWindowsDefault"));
			it++;
			continue;
		}
		win = TheWindowManager->winGetWindowFromId(nullptr, TheNameKeyGenerator->nameToKey(rWin->m_name));
		if(!win)
		{
			it++;
			continue;
		}
		
		win->winSetPosition(static_cast<Int>(rWin->m_altPos.x * x + ox), static_cast<Int>(rWin->m_altPos.y * y + oy));
		if(rWin->m_altSize.x >0 || rWin->m_altSize.y > 0)
			win->winSetSize(static_cast<Int>(rWin->m_altSize.x *x), static_cast<Int>(rWin->m_altSize.y *y));
		DEBUG_LOG(("sizeWindowsAlt:%s pos X:%g pos Y: %g size X:%g sizeY: %g",rWin->m_name.str(), rWin->m_altPos.x*x, rWin->m_altPos.y*y,rWin->m_altSize.x*x, rWin->m_altSize.y *y));
		it ++;	
	}
}


void INI::parseControlBarResizerDefinition( INI* ini )
{
//	AsciiString name;
//	ResizerWindow *rWin = NULL;
//
//	// read the name
//	const char* c = ini->getNextToken();
//	name.set( c );	
//
////	ControlBarResizer *resizer = TheControlBar->getControlBarResizer();
//	if( !resizer )
//	{
//		//We don't need it if we're in the builder... which doesn't have this.
//		return;
//	}
//	rWin = resizer->findResizerWindow( name );
//	if( rWin == NULL )
//	{
//
//		// image not found, create a new one
//		rWin = resizer->newResizerWindow(name);
//		DEBUG_ASSERTCRASH( rWin, ("parseControlBarResizerDefinition: unable to allocate ResizerWindow for '%s'\n",
//															name.str()) );
//
//	}  // end if
//
//	// parse the ini definition
//	ini->initFromINI( rWin, resizer->getFieldParse());
//
}  // end parseMappedImage


//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

