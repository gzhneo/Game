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

// FILE: STLTypedefs.h ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information					         
//                Copyright (C) 2001 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
// Project:    RTS3
//
// File name:  STLTypedefs.h
//
// Created:    John McDonald
//
// Desc:			 @todo
//
//-----------------------------------------------------------------------------

#pragma once

#ifndef __STLTYPEDEFS_H__
#define __STLTYPEDEFS_H__

//-----------------------------------------------------------------------------
#include "Common/AsciiString.h"
#include "Common/UnicodeString.h"
#include "Common/GameCommon.h"
#include "Common/GameMemory.h"

//-----------------------------------------------------------------------------


// FORWARD DECLARATIONS
class Object;
enum NameKeyType : int;	// forward declaration needs the underlying type (C4471)
enum ObjectID : int;	// forward declaration needs the underlying type (C4471)
enum DrawableID : int;	// forward declaration needs the underlying type (C4471)

#include <algorithm>
#include <bitset>
#include <functional>
#include <list>
#include <map>
#include <queue>
#include <set>
#include <stack>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// List of AsciiStrings to allow list of ThingTemplate names from INI and such
using AsciiStringList = std::list< AsciiString >;
using AsciiStringListIterator = std::list< AsciiString >::iterator;
using AsciiStringListConstIterator = std::list< AsciiString >::const_iterator;

// One is used in GameLogic to keep track of objects to be destroyed
using ObjectPointerList = std::list<Object *>;
using ObjectPointerListIterator = std::list<Object *>::iterator;

using ObjectIDVector = std::vector<ObjectID>;
using ObjectIDVectorIterator = std::vector<ObjectID>::iterator;

// Terribly useful, especially with Bezier curves
using VecCoord3D = std::vector<Coord3D>;
using VecCoord3DIt = VecCoord3D::iterator;

// Used for cursor->3D position request caching in the heightmap
using PosRequest = std::pair<ICoord2D, Coord3D>;
using VecPosRequests = std::vector<PosRequest>;
using VecPosRequestsIt = std::vector<PosRequest>::iterator;

// Used to cache off names of objects for faster lookup
using NamedRequest = std::pair<AsciiString, Object*>;
using VecNamedRequests = std::vector<NamedRequest>;
using VecNamedRequestsIt = std::vector<NamedRequest>::iterator;

// Rumor has it that a Vector of Bools gets stored as a bitfield internally.
using BoolVector = std::vector<Bool>;
using BoolVectorIterator = std::vector<Bool>::iterator;

using ProductionChangeMap = std::map< NameKeyType, Real, std::less<NameKeyType> >;
using ProductionVeterancyMap = std::map< NameKeyType, VeterancyLevel, std::less<NameKeyType> >;

// Some useful, common hash and equal_to functors for use with hash_map
namespace rts 
{
	
	// Generic hash functor. This should almost always be overridden for 
	// specific types.
	template<typename T> struct hash
	{
		size_t operator()(const T& __t) const
		{ 
			std::hash<T> tmp;
			return tmp(__t);
		}
	};

	// Generic equal_to functor. This should be overridden if there is no
	// operator==, or if that isn't the behavior desired. (For instance, in
	// the case of pointers.)
	template<typename T> struct equal_to
	{
		Bool operator()(const T& __t1, const T& __t2) const
		{
			return (__t1 == __t2);
		}
	};

	// Generic less_than_nocase functor. This should be overridden if there is no
	// operator<, or if that isn't the behavior desired. (For instance, in
	// the case of pointers, or strings.)
	template<typename T> struct less_than_nocase
	{
		bool operator()(const T& __t1, const T& __t2) const
		{
			return (__t1 < __t2);
		}
	};

	template<> struct hash<NameKeyType>
	{
		size_t operator()(NameKeyType nkt) const
		{ 
			std::hash<UnsignedInt> tmp;
			return tmp(static_cast<UnsignedInt>(nkt));
		}
	};

	template<> struct hash<DrawableID>
	{
		size_t operator()(DrawableID nkt) const
		{ 
			std::hash<UnsignedInt> tmp;
			return tmp(static_cast<UnsignedInt>(nkt));
		}
	};

	template<> struct hash<ObjectID>
	{
		size_t operator()(ObjectID nkt) const
		{ 
			std::hash<UnsignedInt> tmp;
			return tmp(static_cast<UnsignedInt>(nkt));
		}
	};

	// This is the equal_to overload for char* comparisons. We compare the
	// strings to determine whether they are equal or not.
	// Other overloads should go into specific header files, not here (unless
	// they are ot be used in lots of places.)
	template<> struct equal_to<const char*>
	{
		Bool operator()(const char* s1, const char* s2) const
		{
			return strcmp(s1, s2) == 0;
		}
	};

	// std::hash<const char*> hashes the pointer, not the contents. Use the classic SGI STL
	// content hash instead.
	inline size_t stlStyleStringHash(const char* s)
	{
		unsigned long h = 0;
		for ( ; s && *s; ++s)
			h = 5*h + static_cast<unsigned char>(*s);
		return static_cast<size_t>(h);
	}

	// Same content hash for WideChar strings.
	inline size_t stlStyleStringHash(const WideChar* s)
	{
		unsigned long h = 0;
		for ( ; s && *s; ++s)
			h = 5*h + static_cast<unsigned short>(*s);
		return static_cast<size_t>(h);
	}

	template<> struct hash<const char*>
	{
		size_t operator()(const char* s) const
		{
			return stlStyleStringHash(s);
		}
	};

	template<> struct hash<AsciiString>
	{
		size_t operator()(const AsciiString& ast) const
		{
			return stlStyleStringHash(ast.str());
		}
	};

	// Content-based hash for UnicodeString, like hash<AsciiString>.
	template<> struct hash<UnicodeString>
	{
		size_t operator()(const UnicodeString& ust) const
		{
			return stlStyleStringHash(ust.str());
		}
	};

	template<> struct equal_to<AsciiString>
	{
		Bool operator()(const AsciiString& __t1, const AsciiString& __t2) const
		{
			return (__t1 == __t2);
		}
	};

	template<> struct less_than_nocase<AsciiString>
	{
		bool operator()(const AsciiString& __t1, const AsciiString& __t2) const
		{
			return (__t1.compareNoCase(__t2) < 0);
		}
	};

	template<> struct less_than_nocase<UnicodeString>
	{
		bool operator()(const UnicodeString& __t1, const UnicodeString& __t2) const
		{
			return (__t1.compareNoCase(__t2) < 0);
		}
	};
}

#endif /* __STLTYPEDEFS_H__ */
