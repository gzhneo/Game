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
 *                     $Archive:: /Commando/Code/wwdebug/wwdebug.cpp                          $*
 *                                                                                             *
 *                      $Author:: Greg_h                                                      $*
 *                                                                                             *
 *                     $Modtime:: 1/13/02 1:46p                                               $*
 *                                                                                             *
 *                    $Revision:: 16                                                          $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   WWDebug_Install_Message_Handler -- install function for handling the debug messages       *
 *   WWDebug_Install_Assert_Handler -- Install a function for handling the assert messages     *
 *   WWDebug_Install_Trigger_Handler -- install a trigger handler function                     *
 *   WWDebug_Printf -- Internal function for passing messages to installed handler             *
 *   WWDebug_Assert_Fail -- Internal function for passing assert messages to installed handler *
 *   WWDebug_Assert_Fail_Print -- Internal function, passes assert message to handler          *
 *   WWDebug_Check_Trigger -- calls the user-installed debug trigger handler                   *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


#include "wwdebug.h"
#include <windows.h>
//#include "win.h" can use this if allowed to see wwlib
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <signal.h>
#include <format>
#include <string>


static PrintFunc			_CurMessageHandler = nullptr;
static AssertPrintFunc	_CurAssertHandler = nullptr;
static TriggerFunc		_CurTriggerHandler = nullptr;
static ProfileFunc		_CurProfileStartHandler = nullptr;
static ProfileFunc		_CurProfileStopHandler = nullptr;

// Convert the latest system error into a string and return a pointer to
// a static buffer containing the error string.

void Convert_System_Error_To_String(int id, char* buffer, int buf_len)
{
	FormatMessage(
		FORMAT_MESSAGE_FROM_SYSTEM,
		nullptr,
		id,
		0,
		buffer,
		buf_len,
		nullptr);
}

int Get_Last_System_Error()
{
	return GetLastError();
}

/***********************************************************************************************
 * WWDebug_Install_Message_Handler -- install function for handling the debug messages         *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/19/98    GTH : Created.                                                                 *
 *=============================================================================================*/
PrintFunc WWDebug_Install_Message_Handler(PrintFunc func)
{
	PrintFunc tmp = _CurMessageHandler;
	_CurMessageHandler = func;
	return tmp;
}


/***********************************************************************************************
 * WWDebug_Install_Assert_Handler -- Install a function for handling the assert messages       *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/19/98    GTH : Created.                                                                 *
 *=============================================================================================*/
AssertPrintFunc WWDebug_Install_Assert_Handler(AssertPrintFunc func)
{
	AssertPrintFunc tmp = _CurAssertHandler;
	_CurAssertHandler = func;
	return tmp;
}


/***********************************************************************************************
 * WWDebug_Install_Trigger_Handler -- install a trigger handler function                       *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/24/98    GTH : Created.                                                                 *
 *=============================================================================================*/
TriggerFunc	WWDebug_Install_Trigger_Handler(TriggerFunc func)
{
	TriggerFunc tmp = _CurTriggerHandler;
	_CurTriggerHandler = func;
	return tmp;
}


/***********************************************************************************************
 * WWDebug_Install_Profile_Start_Handler -- install a profile handler function                 *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/24/98    GTH : Created.                                                                 *
 *=============================================================================================*/
ProfileFunc	WWDebug_Install_Profile_Start_Handler(ProfileFunc func)
{
	ProfileFunc tmp = _CurProfileStartHandler;
	_CurProfileStartHandler = func;
	return tmp;
}


/***********************************************************************************************
 * WWDebug_Install_Profile_Stop_Handler -- install a profile handler function                  *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/24/98    GTH : Created.                                                                 *
 *=============================================================================================*/
ProfileFunc	WWDebug_Install_Profile_Stop_Handler(ProfileFunc func)
{
	ProfileFunc tmp = _CurProfileStopHandler;
	_CurProfileStopHandler = func;
	return tmp;
}


/***********************************************************************************************
 * WWDebug_Printf -- Internal function for passing messages to installed handler               *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/19/98    GTH : Created.                                                                 *
 *=============================================================================================*/


/*
** The log functions format into a growing std::string after measuring the required length,
** so long messages cannot overflow a fixed stack buffer. std::format is not usable here: the
** interface is C varargs with printf formats, and a va_list cannot become std::format_args.
*/
static std::string WWDebug_Format_Args(const char * format, va_list va)
{
	va_list measure;
	va_copy(measure, va);
	const int needed = vsnprintf(nullptr, 0, format, measure);
	va_end(measure);

	if (needed <= 0) {
		return std::string();
	}

	std::string text(static_cast<size_t>(needed), '\0');
	vsnprintf(text.data(), static_cast<size_t>(needed) + 1, format, va);
	return text;
}

void WWDebug_Printf(const char * format,...)
{
	if (_CurMessageHandler != nullptr) {

		va_list	va;

		va_start(va, format);
		const std::string buffer = WWDebug_Format_Args(format, va);
		va_end(va);

		_CurMessageHandler(WWDEBUG_TYPE_INFORMATION, buffer.c_str());

	}
}

/***********************************************************************************************
 * WWDebug_Printf_Warning -- Internal function for passing messages to installed handler       *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/19/98    GTH : Created.                                                                 *
 *=============================================================================================*/

void WWDebug_Printf_Warning(const char * format,...)
{
	if (_CurMessageHandler != nullptr) {

		va_list	va;

		va_start(va, format);
		const std::string buffer = WWDebug_Format_Args(format, va);
		va_end(va);

		_CurMessageHandler(WWDEBUG_TYPE_WARNING, buffer.c_str());

	}
}

/***********************************************************************************************
 * WWDebug_Printf_Error -- Internal function for passing messages to installed handler         *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/19/98    GTH : Created.                                                                 *
 *=============================================================================================*/

void WWDebug_Printf_Error(const char * format,...)
{
	if (_CurMessageHandler != nullptr) {

		va_list	va;

		va_start(va, format);
		const std::string buffer = WWDebug_Format_Args(format, va);
		va_end(va);

		_CurMessageHandler(WWDEBUG_TYPE_ERROR, buffer.c_str());

	}
}

/***********************************************************************************************
 * WWDebug_Assert_Fail -- Internal function for passing assert messages to installed handler   *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/19/98    GTH : Created.                                                                 *
 *=============================================================================================*/




/***********************************************************************************************
 * _assert -- Catch all asserts by overriding lib function                                     *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Assert stuff                                                                      *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   12/11/2001 3:56PM ST : Created                                                            *
 *=============================================================================================*/



/***********************************************************************************************
 * WWDebug_Assert_Fail_Print -- Internal function, passes assert message to handler            *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/19/98    GTH : Created.                                                                 *
 *=============================================================================================*/


/***********************************************************************************************
 * WWDebug_Check_Trigger -- calls the user-installed debug trigger handler                     *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/24/98    GTH : Created.                                                                 *
 *=============================================================================================*/
bool WWDebug_Check_Trigger(int trigger_num)
{
	if (_CurTriggerHandler != nullptr) {
		return _CurTriggerHandler(trigger_num);
	} else {
		return false;
	}
}


/***********************************************************************************************
 * WWDebug_Profile_Start -- calls the user-installed profile start handler                     *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/24/98    GTH : Created.                                                                 *
 *=============================================================================================*/
void WWDebug_Profile_Start( const char * title)
{
	if (_CurProfileStartHandler != nullptr) {
		_CurProfileStartHandler( title );
	}
}


/***********************************************************************************************
 * WWDebug_Profile_Stop -- calls the user-installed profile start handler                      *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/24/98    GTH : Created.                                                                 *
 *=============================================================================================*/
void WWDebug_Profile_Stop( const char * title)
{
	if (_CurProfileStopHandler != nullptr) {
		_CurProfileStopHandler( title );
	}
}



