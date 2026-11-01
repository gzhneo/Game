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

// FILE: ShellMenuScheme.cpp /////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Electronic Arts Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2002 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
//	created:	Jul 2002
//
//	Filename: 	ShellMenuScheme.cpp
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

//-----------------------------------------------------------------------------
// USER INCLUDES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/INI.h"
#include "GameClient/ShellMenuScheme.h"
#include "GameClient/Shell.h"
#include "GameClient/Display.h"
//-----------------------------------------------------------------------------
// DEFINES ////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

const FieldParse ShellMenuSchemeManager::m_shellMenuSchemeFieldParseTable[] = 
{

	{ "ImagePart",						ShellMenuSchemeManager::parseImagePart,			NULL, NULL },
	{ "LinePart",							ShellMenuSchemeManager::parseLinePart,	NULL, NULL },
	{ nullptr,										nullptr,													nullptr, 0 }  // keep this last

};

//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
void INI::parseShellMenuSchemeDefinition( INI *ini )
{
	AsciiString name;
	ShellMenuSchemeManager *SMSchemeManager;
	ShellMenuScheme *SMScheme;

	// read the name
	const char* c = ini->getNextToken();
	name.set( c );	

	// find existing item if present
	SMSchemeManager = TheShell->getShellMenuSchemeManager();
	DEBUG_ASSERTCRASH( SMSchemeManager, ("parseShellMenuSchemeDefinition: Unable to Get SMSchemeManager\n") );
	if( !SMSchemeManager )
		return;

	// If we have a previously allocated control bar, this will return a cleared out pointer to it so we
	// can overwrite it	
	SMScheme = SMSchemeManager->newShellMenuScheme( name );

	// sanity
	DEBUG_ASSERTCRASH( SMScheme, ("parseControlBarSchemeDefinition: Unable to allocate Scheme '%s'\n", name.str()) );

	// parse the ini definition
	ini->initFromINI( SMScheme, SMSchemeManager->getFieldParse() );

}  // end parseCommandButtonDefinition

ShellMenuSchemeLine::ShellMenuSchemeLine( void )
{
	m_startPos.x = m_startPos.y = 0;
	m_endPos.x = m_endPos.y = 0;
	m_color = GAME_COLOR_UNDEFINED;
	m_width = 1;
}
ShellMenuSchemeLine::~ShellMenuSchemeLine( void )
{
}

ShellMenuSchemeImage::ShellMenuSchemeImage( void )
{
	m_name.clear();
	m_position.x = m_position.y = 0;
	m_size.x = m_size.x = 0;
	m_image = nullptr;
}

ShellMenuSchemeImage::~ShellMenuSchemeImage( void )
{
	m_image = nullptr;
}

ShellMenuScheme::ShellMenuScheme( void )
{
	
}

ShellMenuScheme::~ShellMenuScheme( void )
{
	m_imageList.clear();
	m_lineList.clear();

	
}

void ShellMenuScheme::addLine( std::unique_ptr<ShellMenuSchemeLine> schemeLine )
{
	if(!schemeLine)
		return;

	m_lineList.push_back( std::move(schemeLine) );
}


void ShellMenuScheme::addImage( std::unique_ptr<ShellMenuSchemeImage> schemeImage )
{
	if(!schemeImage)
		return;

	m_imageList.push_back( std::move(schemeImage) );
}

void ShellMenuScheme::draw( void )
{

	ShellMenuSchemeImageListIt imageIt = m_imageList.begin();
	while(imageIt != m_imageList.end())
	{
		ShellMenuSchemeImage *image = imageIt->get();
		if(image && image->m_image)
		{
			TheDisplay->drawImage(image->m_image, image->m_position.x, image->m_position.y,
														image->m_position.x + image->m_size.x , image->m_position.y + image->m_size.y);
		}
		++imageIt;
	}

	ShellMenuSchemeLineListIt it = m_lineList.begin();
	while(it != m_lineList.end())
	{
		ShellMenuSchemeLine *line = it->get();
		
		if(line)
		{
			TheDisplay->drawLine(line->m_startPos.x, line->m_startPos.y, line->m_endPos.x,
														line->m_endPos.y,static_cast<Real>(line->m_width), line->m_color);
		}
		++it;
	}


}

ShellMenuSchemeManager::ShellMenuSchemeManager( void )
{
	m_currentScheme = nullptr;
}

ShellMenuSchemeManager::~ShellMenuSchemeManager( void )
{
	m_currentScheme = nullptr;


	m_schemeList.clear();
	
}

void ShellMenuSchemeManager::parseImagePart(INI *ini, void *instance, void* /*store*/, const void* /*userData*/)
{
	static const FieldParse myFieldParse[] = 
		{
			{ "Position",				INI::parseICoord2D,				nullptr, offsetof( ShellMenuSchemeImage, m_position ) },
			{ "Size",						INI::parseICoord2D,				nullptr, offsetof( ShellMenuSchemeImage, m_size ) },
      { "ImageName",			INI::parseMappedImage,		nullptr, offsetof( ShellMenuSchemeImage, m_image ) },
			{ nullptr,							nullptr,											nullptr, 0 }  // keep this last
		};

	auto schemeImage = std::make_unique<ShellMenuSchemeImage>();
	ini->initFromINI(schemeImage.get(), myFieldParse);
	(static_cast<ShellMenuScheme*>(instance))->addImage(std::move(schemeImage));

}

void ShellMenuSchemeManager::parseLinePart(INI *ini, void *instance, void* /*store*/, const void* /*userData*/)
{
	static const FieldParse myFieldParse[] = 
		{
			{ "StartPosition",		INI::parseICoord2D,				nullptr, offsetof( ShellMenuSchemeLine, m_startPos ) },
			{ "EndPosition",			INI::parseICoord2D,				nullptr, offsetof( ShellMenuSchemeLine, m_endPos ) },
      { "Color",						INI::parseColorInt,				nullptr, offsetof( ShellMenuSchemeLine, m_color ) },
			{ "Width",						INI::parseInt,						nullptr, offsetof( ShellMenuSchemeLine, m_width ) },
			
			{ nullptr,								nullptr,											nullptr, 0 }  // keep this last
		};

	auto schemeLine = std::make_unique<ShellMenuSchemeLine>();
	ini->initFromINI(schemeLine.get(), myFieldParse);
	(static_cast<ShellMenuScheme*>(instance))->addLine(std::move(schemeLine));

}

ShellMenuScheme *ShellMenuSchemeManager::newShellMenuScheme(AsciiString name)
{
	ShellMenuSchemeListIt it;
	it = m_schemeList.begin();
	name.toLower();
	while(it != m_schemeList.end())
	{
		ShellMenuScheme *scheme = it->get();
		if(scheme->m_name.compare(name) == 0)
		{
			if(m_currentScheme == scheme)
				m_currentScheme = nullptr;
			m_schemeList.erase( it );
			break;
		}
		else
			++it;
	}
	auto newScheme = std::make_unique<ShellMenuScheme>();
	ShellMenuScheme *result = newScheme.get();
	newScheme->m_name.set(name);
	m_schemeList.push_back(std::move(newScheme));
	return result;
}

void ShellMenuSchemeManager::init( void )
{
	INI ini;
	// Read from INI all the ControlBarSchemes
	ini.load( AsciiString( "Data\\INI\\Default\\ShellMenuScheme.ini" ), INILoadType::INI_LOAD_OVERWRITE, nullptr );
	ini.load( AsciiString( "Data\\INI\\ShellMenuScheme.ini" ), INILoadType::INI_LOAD_OVERWRITE, nullptr );

}

void ShellMenuSchemeManager::setShellMenuScheme( AsciiString name )
{
	if(name.isEmpty())
	{
		m_currentScheme = nullptr;
		return;
	}

	ShellMenuSchemeListIt it;
	it = m_schemeList.begin();
	name.toLower();
	while(it != m_schemeList.end())
	{
		ShellMenuScheme *scheme = it->get();
		if(scheme->m_name.compare(name) == 0)
		{
			m_currentScheme = scheme;
			break;
		}
		++it;
	}
}

void ShellMenuSchemeManager::draw( void )
{
	if(m_currentScheme)
		m_currentScheme->draw();
}

void ShellMenuSchemeManager::update( void )
{

}
//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

