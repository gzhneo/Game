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
 *                 Project Name : Command & Conquer                                            * 
 *                                                                                             * 
 *                     $Archive:: /Commando/Code/wwlib/trim.cpp                               $* 
 *                                                                                             * 
 *                      $Author:: Denzil_l                                                    $*
 *                                                                                             * 
 *                     $Modtime:: 11/08/01 11:35a                                             $*
 *                                                                                             * 
 *                    $Revision:: 4                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------* 
 * Functions:                                                                                  * 
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include	"always.h"
#include	"trim.h"
#include	<string.h>

/*********************************************************************************************** 
 * strtrim -- Trim leading and trailing white space off of string.                             * 
 *                                                                                             * 
 *    This routine will remove the leading and trailing whitespace from the string specifed.   * 
 *    The string is modified in place.                                                         * 
 *                                                                                             * 
 * INPUT:   buffer   -- Pointer to the string to be trimmed.                                   * 
 *                                                                                             * 
 * OUTPUT:  none                                                                               * 
 *                                                                                             * 
 * WARNINGS:   none                                                                            * 
 *                                                                                             * 
 * HISTORY:                                                                                    * 
 *   02/06/1997 JLB : Created.                                                                 * 
 *=============================================================================================*/
char* strtrim(char* buffer)
{
	if (buffer) {
		/* Strip leading white space from the string. */
		char* source = buffer;

		while ((*source != 0) && (static_cast<unsigned char>(*source) <= 32)) {
			++source;
		}

		if (source != buffer) {
			// "source" points into the same buffer, so the ranges overlap; memmove is required
			// (strcpy would be undefined behavior).
			memmove(buffer, source, strlen(source) + 1);
		}

		/* Clip trailing white space from the string. */
		for (int index = static_cast<int>(strlen(buffer)) - 1; index >= 0; --index) {
			if (static_cast<unsigned char>(buffer[index]) <= 32) {
				buffer[index] = '\0';
			} else {
				break;
			}
		}
	}

	return buffer;
}


wchar_t* wcstrim(wchar_t* buffer)
{
	if (buffer) {
		/* Strip leading white space from the string. */
		wchar_t* source = buffer;

		while ((*source != 0) && (static_cast<unsigned int>(*source) <= 32)) {
			++source;
		}
		
		if (source != buffer) {
			// Overlapping ranges, see strtrim() above.
			memmove(buffer, source, (wcslen(source) + 1) * sizeof(wchar_t));
		}

		/* Clip trailing white space from the string. */
		for (int index = static_cast<int>(wcslen(buffer)) - 1; index >= 0; --index) {
			if (static_cast<unsigned int>(buffer[index]) <= 32) {
				buffer[index] = L'\0';
			} else {
				break;
			}
		}
	}

	return buffer;
}
