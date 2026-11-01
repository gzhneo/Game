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
 *                 Project Name : WWDebug                                                      *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/wwdebug/wwdebug.h                            $*
 *                                                                                             *
 *                      $Author:: Jani_p                                                      $*
 *                                                                                             *
 *                     $Modtime:: 5/04/01 7:43p                                               $*
 *                                                                                             *
 *                    $Revision:: 18                                                          $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#pragma once

#ifndef WWDEBUG_H
#define WWDEBUG_H

#include "Engine/FormatCheck.h"
				
// The macro MESSAGE allows user to put:
// #pragma MESSAGE("Hello world")
// anywhere in a source file.  The message:
// sourcefname.cpp (123) : Hello world
// would be printed if put in sourcefname.cpp on line 123 in compile window like an error.
// You can then use next/prev error	hot keys to see where comment is.  It is not an error and
// will be printed everytime it is compiled.  Very useful to put comments in code that cannot
// be forgoten.
#define STRING_IT(a) #a																				  
#define TOKEN_IT(a) STRING_IT(,##a)
#define MESSAGE(a) message (__FILE__ "(" TOKEN_IT(__LINE__) ") : " a)

void Convert_System_Error_To_String(int error_id, char* buffer, int buf_len);
int Get_Last_System_Error();

typedef enum {
	WWDEBUG_TYPE_INFORMATION,
	WWDEBUG_TYPE_WARNING,
	WWDEBUG_TYPE_ERROR,
	WWDEBUG_TYPE_USER
} DebugType;

using PrintFunc = void (*)(DebugType type, const char * message);
using AssertPrintFunc = void (*)(const char * message);
using TriggerFunc = bool (*)(int trigger_num);
using ProfileFunc = void (*)(const char * title);

PrintFunc			WWDebug_Install_Message_Handler(PrintFunc func);
AssertPrintFunc	WWDebug_Install_Assert_Handler(AssertPrintFunc func);
TriggerFunc			WWDebug_Install_Trigger_Handler(TriggerFunc func);
ProfileFunc			WWDebug_Install_Profile_Start_Handler(ProfileFunc func);
ProfileFunc			WWDebug_Install_Profile_Stop_Handler(ProfileFunc func);


/*
** Users should not call the following three functions directly!  Use the macros below instead...
*/
void					WWDebug_Printf(const char * format,...) ENGINE_PRINTF_FORMAT(1, 2);
void					WWDebug_Printf_Warning(const char * format,...) ENGINE_PRINTF_FORMAT(1, 2);
void					WWDebug_Printf_Error(const char * format,...) ENGINE_PRINTF_FORMAT(1, 2);


/*
** Use the following #define so that all of the debugging messages
** and strings go away when the release version is built.  
** WWDEBUG_SAY(("dir = %f\n",dir));
*/

// The header exists once under Code/GameEngine; WWDebug passes the include path on as
// PUBLIC.
#include "Common/Debug.h"

#ifdef DEBUG_LOGGING
#define WWDEBUG_SAY(x)							DEBUG_LOG(x)
#define WWDEBUG_WARNING(x)					DEBUG_LOG(x)
#else
#define WWDEBUG_SAY(x)
#define WWDEBUG_WARNING(x)
#endif

#define WWRELEASE_SAY(x)						WWDebug_Printf x
#define WWRELEASE_WARNING(x)					WWDebug_Printf_Warning x
#define WWRELEASE_ERROR(x)						WWDebug_Printf_Error x
/*
** The WWASSERT and WWASSERT_PRINT macros will send messages to your
** assert handler.
*/
#ifdef DEBUG_CRASHING
#define WWASSERT(expr)							DEBUG_ASSERTCRASH(expr, ("%s, %s, %d", #expr,__FILE__,__LINE__))
#define WWASSERT_PRINT( expr, string )		DEBUG_ASSERTCRASH(expr, ("%s, %s, %d - %s", #expr,__FILE__,__LINE__,string))
#define W3D_DIE                               	DEBUG_CRASH(("DIE!, %s, %d", __FILE__,__LINE__))
#define WWDEBUG_ERROR(x)						DEBUG_CRASH(x)
#else
#define WWASSERT( expr )
#define WWASSERT_PRINT( expr, string )	
#define W3D_DIE
#define WWDEBUG_ERROR(x)
#endif

/*
** The WWDEBUG_BREAK macro will cause the application to break into
** the debugger...
*/
#include <intrin.h>	// __debugbreak
#define WWDEBUG_BREAK							__debugbreak()	// intrinsic, x64 has no inline assembler

/*
** The WWDEBUG_TRIGGER macro can be used to ask the application if 
** a debug trigger is set.  We define a couple of generic triggers
** for casual use.
*/
#define WWDEBUG_TRIGGER_GENERIC0				0
#define WWDEBUG_TRIGGER_GENERIC1				1

#define WWDEBUG_TRIGGER(x)						(0)	


/*
** The WWDEBUG_PROFILE macros can be used to time blocks of code
*/
#define WWDEBUG_PROFILE_START(x)				
#define WWDEBUG_PROFILE_STOP(x)

#endif
