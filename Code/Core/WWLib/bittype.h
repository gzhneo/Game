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

/* $Header: /G/wwlib/bittype.h 4     4/02/99 1:37p Eric_c $ */
/*************************************************************************** 
 ***                  Confidential - Westwood Studios                    *** 
 *************************************************************************** 
 *                                                                         * 
 *                 Project Name : Voxel Technology                         * 
 *                                                                         * 
 *                    File Name : BITTYPE.H                                * 
 *                                                                         * 
 *                   Programmer : Greg Hjelstrom                           * 
 *                                                                         * 
 *                   Start Date : 02/24/97                                 * 
 *                                                                         * 
 *                  Last Update : February 24, 1997 [GH]                   * 
 *                                                                         * 
 *-------------------------------------------------------------------------* 
 * Functions:                                                              * 
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
#pragma once

#ifndef BITTYPE_H
#define BITTYPE_H

using uint8 = unsigned char;
using uint16 = unsigned short;
using uint32 = unsigned long;
using uint = unsigned int;

using sint8 = signed char;
using sint16 = signed short;
using sint32 = signed long;
using sint = signed int;

using float32 = float;
using float64 = double;

using DWORD = unsigned long;
using WORD = unsigned short;
using BYTE = unsigned char;
using BOOL = int;
using USHORT = unsigned short;
using LPCSTR = const char *;
using UINT = unsigned int;
using ULONG = unsigned long;

#endif //BITTYPE_H
