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

// FILE: Image.cpp ////////////////////////////////////////////////////////////////////////////////
// Created:   Colin Day, June 2001
// Desc:      High level representation of images, this is currently being
//						written so we have a way to refer to images in the windows
//						GUI, this system should be replaced with something that can
//						handle real image management or written to accomodate 
//						all parts of the engine that need images.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "formatinto.h"

#define DEFINE_IMAGE_STATUS_NAMES
#include "Lib/BaseType.h"
#include "Common/Debug.h"
#include "Common/INI.h"
#include "Common/GlobalData.h"
#include "GameClient/Image.h"
#include "Common/NameKeyGenerator.h"
#include "Engine/UserDataFiles.h"

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
const FieldParse Image::m_imageFieldParseTable[] = 
{

	{ "Texture",				INI::parseAsciiString,							nullptr, 		offsetof( Image, m_filename ) },
	{ "TextureWidth",		INI::parseInt,											nullptr, 		offsetof( Image, m_textureSize.x ) },
	{ "TextureHeight",	INI::parseInt,											nullptr, 		offsetof( Image, m_textureSize.y ) },
	{ "Coords",					Image::parseImageCoords,						nullptr, 		offsetof( Image, m_UVCoords ) },
	{ "Status",					Image::parseImageStatus,						nullptr, 		offsetof( Image, m_status ) },

	{ nullptr,							nullptr,																nullptr, 		0 }

};

// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////////////////////////
//-------------------------------------------------------------------------------------------------
/** Parse an image coordinates in the form of
	*
	* COORDS = Left:AAA Top:BBB Right:CCC Bottom:DDD */
//-------------------------------------------------------------------------------------------------
void Image::parseImageCoords( INI* ini, void *instance, void *store, const void* /*userData*/ )
{
	Int left = INI::scanInt(ini->getNextSubToken("Left"));
	Int top = INI::scanInt(ini->getNextSubToken("Top"));
	Int right = INI::scanInt(ini->getNextSubToken("Right"));
	Int bottom = INI::scanInt(ini->getNextSubToken("Bottom"));

	// get the image we're storing in
	Image *theImage = static_cast<Image *>(instance);

	//
	// store the UV coords based on what we've read in and the texture size
	// defined for this image
	//
	Region2D uvCoords;

	uvCoords.lo.x = static_cast<Real>(left);
	uvCoords.lo.y = static_cast<Real>(top);
	uvCoords.hi.x = static_cast<Real>(right);
	uvCoords.hi.y = static_cast<Real>(bottom);
	
	// adjust the coords by texture size
	const ICoord2D *textureSize = theImage->getTextureSize();
	if( textureSize->x )
	{
		uvCoords.lo.x /= static_cast<Real>(textureSize->x);
		uvCoords.hi.x /= static_cast<Real>(textureSize->x);
	}  // end if
	if( textureSize->y )
	{
		uvCoords.lo.y /= static_cast<Real>(textureSize->y);
		uvCoords.hi.y /= static_cast<Real>(textureSize->y);
	}  // end if

	// store the uv coords
	theImage->setUV( &uvCoords );

	// compute the image size based on the coords we read and store
	ICoord2D imageSize;
	imageSize.x = right - left;
	imageSize.y = bottom - top;
	theImage->setImageSize( &imageSize );

}  // end parseImageCoord

//-------------------------------------------------------------------------------------------------
/** Parse the image status line */
//-------------------------------------------------------------------------------------------------
void Image::parseImageStatus( INI* ini, void *instance, void *store, const void* /*userData*/)
{	
	// use existing INI parsing for the bit strings
	INI::parseBitString32(ini, instance, store, imageStatusNames);

	//
	// if we are rotated 90 degrees clockwise we need to swap our width and height as
	// they were computed from the page location rect, which was for the rotated image
	// (see ImagePacker tool for more details)
	//
	UnsignedInt *theStatusBits = static_cast<UnsignedInt *>(store);
	if( BitTest( *theStatusBits, IMAGE_STATUS_ROTATED_90_CLOCKWISE ) )
	{
		Image *theImage = static_cast<Image *>(instance);
		ICoord2D imageSize;

		imageSize.x = theImage->getImageHeight();  // note it's height not width
		imageSize.y = theImage->getImageWidth();   // note it's width not height
		theImage->setImageSize( &imageSize );

	}  // end if

}  // end parseImageStatus

// PUBLIC DATA ////////////////////////////////////////////////////////////////////////////////////

// PUBLIC FUNCTIONS////////////////////////////////////////////////////////////////////////////////
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Image::Image( void )
{

	m_name.clear();
	m_filename.clear();
	m_textureSize.x = 0;
	m_textureSize.y = 0;
	m_UVCoords.lo.x = 0.0f;
	m_UVCoords.lo.y = 0.0f;
	m_UVCoords.hi.x = 1.0f;
	m_UVCoords.hi.y = 1.0f;
	m_imageSize.x = 0;
	m_imageSize.y = 0;
	m_rawTextureData = nullptr;
	m_status = IMAGE_STATUS_NONE;

}  // end Image

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Image::~Image( void )
{

}  // end ~Image

//-------------------------------------------------------------------------------------------------
/** Set a status bit into the existing status, return the previous status
	* bit collection from before the set */
//-------------------------------------------------------------------------------------------------
UnsignedInt Image::setStatus( UnsignedInt bit )
{
	UnsignedInt prevStatus = m_status;

	BitSet( m_status, bit );
	return prevStatus;

}  // end setStatus

//-------------------------------------------------------------------------------------------------
/** Clear a status bit from the existing status, return the previous
	* status bit collection from before the clear */
//-------------------------------------------------------------------------------------------------
UnsignedInt Image::clearStatus( UnsignedInt bit )
{
	UnsignedInt prevStatus = m_status;

	BitClear( m_status, bit );
	return prevStatus;

}  // end clearStatus

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ImageCollection::ImageCollection( void )
{
}  // end ImageCollection

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ImageCollection::~ImageCollection( void )
{
}  // end ~ImageCollection

//-------------------------------------------------------------------------------------------------
/** adds the given image to the collection, transfers ownership to this object */
//-------------------------------------------------------------------------------------------------
void ImageCollection::addImage( Image *image )
{
  PoolPtr<Image> &slot = m_imageMap[TheNameKeyGenerator->nameToLowercaseKey(image->getName())];
  if (slot.get() != image)
    slot.reset(image);
}  // end newImage

//-------------------------------------------------------------------------------------------------
/** Find an image given the image name */
//-------------------------------------------------------------------------------------------------
const Image *ImageCollection::findImageByName( const AsciiString& name )
{
  std::map<unsigned,PoolPtr<Image>>::iterator i=m_imageMap.find(TheNameKeyGenerator->nameToLowercaseKey(name));
  return i==m_imageMap.end()?nullptr:i->second.get();
}  // end findImageByName

//-------------------------------------------------------------------------------------------------
/** Load this image collection with all the images specified in the INI files
	* for the proper texture size directory */
//-------------------------------------------------------------------------------------------------
void ImageCollection::load( Int textureSize )
{
	char buffer[ _MAX_PATH ];
	INI ini;
	// first load in the user created mapped image files if we have them.
	AsciiString userDataPath;	
	if(TheGlobalData)
	{
		userDataPath.format("%sINI\\MappedImages",TheGlobalData->getPath_UserData().str());
		if(!::Engine::UserDataFiles::list(::Engine::pathFromNarrow(userDataPath.str()), "*.ini").empty())
		{
			ini.loadDirectory(userDataPath, TRUE, INILoadType::INI_LOAD_OVERWRITE, nullptr );
		}
	}

	// construct path to the mapped images folder of the correct texture size
	WWFormat::Into(buffer, "Data\\INI\\MappedImages\\TextureSize_{:d}", textureSize);

	// load all the ine files in that directory

	ini.loadDirectory( AsciiString( buffer ), TRUE, INILoadType::INI_LOAD_OVERWRITE, nullptr );

	ini.loadDirectory("Data\\INI\\MappedImages\\HandCreated", TRUE, INILoadType::INI_LOAD_OVERWRITE, nullptr );


}  // end load
