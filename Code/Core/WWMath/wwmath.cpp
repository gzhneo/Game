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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : WWMath                                                       *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/wwmath/wwmath.cpp                            $*
 *                                                                                             *
 *                       Author:: Eric_c                                                       *
 *                                                                                             *
 *                     $Modtime:: 5/10/01 10:52p                                              $*
 *                                                                                             *
 *                    $Revision:: 11                                                          $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


#include "wwmath.h"
#include "multilist.h"
#include <stdlib.h>
#include "wwdebug.h"
#include "wwprofile.h"

// TODO: convert to use loouptablemanager...
float _FastAcosTable[ARC_TABLE_SIZE];
float _FastAsinTable[ARC_TABLE_SIZE];
float _FastSinTable[SIN_TABLE_SIZE];
float _FastInvSinTable[SIN_TABLE_SIZE];

void		WWMath::Init(void)
{
	int a;
	for (a=0;a<ARC_TABLE_SIZE;++a) {
		float cv=float(a-ARC_TABLE_SIZE/2)*(1.0f/(ARC_TABLE_SIZE/2));
		_FastAcosTable[a]=static_cast<float>(acos(cv));
		_FastAsinTable[a]=static_cast<float>(asin(cv));
	}

	for (a=0;a<SIN_TABLE_SIZE;++a) {
		float cv= static_cast<float>(a) * 2.0f * WWMATH_PI / SIN_TABLE_SIZE; //float(a-SIN_TABLE_SIZE/2)*(1.0f/(SIN_TABLE_SIZE/2));
		_FastSinTable[a]=static_cast<float>(sin(cv));
		
		if (a>0) {
			_FastInvSinTable[a]=1.0f/_FastSinTable[a];
		} else {
			_FastInvSinTable[a]=WWMATH_FLOAT_MAX;
		}
	}
}

void		WWMath::Shutdown(void)
{
	// The list nodes (MultiListClass) live in a static AutoPool that would otherwise free its
	// blocks only after the memory report in WinMain, where they would show up as leaks.
	MultiListNodeClass::Free_Unused_Pool_Memory();
}

float		WWMath::Random_Float(void) 
{ 
	return (static_cast<float>(rand() & 0xFFF)) / static_cast<float>(0xFFF); 
}
