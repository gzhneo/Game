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

// FILE: Debug.cpp 
//-----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2001 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: Debug.cpp
//
// Created:   Steven Johnson, August 2001
//
// Desc:      Debug logging and other debug utilities
//
// ----------------------------------------------------------------------------

// SYSTEM INCLUDES 
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "formatinto.h"


// USER INCLUDES 
#define DEBUG_THREADSAFE
#ifdef DEBUG_THREADSAFE
#include "Common/CriticalSection.h"
#endif
#include "Common/Debug.h"
#include "Common/InstallInfo.h"	// GetGameLanguage for the crash message
#include "Common/SystemInfo.h"
#include "Common/UnicodeString.h"
#include "GameClient/GameText.h"
#include "GameClient/Keyboard.h"
#include "GameClient/Mouse.h"
#include "Lib/SafeString.h"
#if defined(DEBUG_STACKTRACE) || defined(IG_DEBUG_STACKTRACE)
	#include "Common/StackDump.h"
#endif

// Horrible reference, but we really, really need to know if we are windowed.
extern bool RenderDevice_IsWindowed;
extern HWND ApplicationHWnd;

extern const char *gAppPrefix;	// C++20: const /// So WB can have a different log file name.

// ----------------------------------------------------------------------------
// DEFINES 
// ----------------------------------------------------------------------------

#ifdef DEBUG_LOGGING

#if defined(_DEBUG)
	#define DEBUG_FILE_NAME				"DebugLogFileD.txt"
	#define DEBUG_FILE_NAME_PREV	"DebugLogFilePrevD.txt"
#else
	#define DEBUG_FILE_NAME				"DebugLogFile.txt"
	#define DEBUG_FILE_NAME_PREV	"DebugLogFilePrev.txt"
#endif

#endif

// ----------------------------------------------------------------------------
// PRIVATE TYPES 
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// PRIVATE DATA 
// ----------------------------------------------------------------------------
#ifdef DEBUG_LOGGING
static FILE *theLogFile = nullptr;
#endif
constexpr auto LARGE_BUFFER = 8192;
static char theBuffer[ LARGE_BUFFER ];	// make it big to avoid weird overflow bugs in debug mode
static int theDebugFlags = 0;
static DWORD theMainThreadID = 0;
// ----------------------------------------------------------------------------
// PUBLIC DATA 
// ----------------------------------------------------------------------------

char* TheCurrentIgnoreCrashPtr = nullptr;
#ifdef DEBUG_LOGGING
// DEBUG_LOG_LEVEL, see Common/Debug.h.
UnsignedInt DebugLevelMask = 0;
const char *TheDebugLevels[DEBUG_LEVEL_MAX] = {
	"NET"
};
#endif

// ----------------------------------------------------------------------------
// PRIVATE PROTOTYPES 
// ----------------------------------------------------------------------------
static const char *getCurrentTimeString(void);
#if defined(DEBUG_LOGGING) || defined(DEBUG_CRASHING)
static const char *getCurrentTickString(void);
static const char *prepBuffer(const char* format, char *buffer);
#endif
#ifdef DEBUG_LOGGING
static void doLogOutput(const char *buffer);
static void whackFunnyCharacters(char *buf);
#endif
#ifdef DEBUG_CRASHING
static int doCrashBox(const char *buffer, Bool logResult);
#endif
#ifdef DEBUG_STACKTRACE
static void doStackDump();
#endif

// ----------------------------------------------------------------------------
// PRIVATE FUNCTIONS 
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
#if defined(_DEBUG)
// The leak report at the end of WinMain runs after TheGlobalData is destroyed. To keep -ignoreAsserts
// in effect there (unattended test runs must not stop at an assert dialog), ~GameEngine latches the
// flag before the teardown.
static Bool s_ignoreAssertsLatched = FALSE;
void DebugLatchIgnoreAsserts( void )
{
	if (TheGlobalData && TheGlobalData->m_debugIgnoreAsserts)
		s_ignoreAssertsLatched = TRUE;
}
#endif

inline Bool ignoringAsserts()
{
#if defined(_DEBUG)
	// -ignoreAsserts stays in effect after TheGlobalData is destroyed, see DebugLatchIgnoreAsserts().
	DebugLatchIgnoreAsserts();
	return !RenderDevice_IsWindowed || s_ignoreAssertsLatched;
#else
	return !RenderDevice_IsWindowed;
#endif
}

// ----------------------------------------------------------------------------
inline HWND getThreadHWND()
{
	return (theMainThreadID == GetCurrentThreadId())?ApplicationHWnd:nullptr;
}

// ----------------------------------------------------------------------------

int MessageBoxWrapper( LPCSTR lpText, LPCSTR lpCaption, UINT uType )
{
	HWND threadHWND = getThreadHWND();
	if (!threadHWND)
		return (uType & MB_ABORTRETRYIGNORE)?IDIGNORE:IDYES;

	return ::MessageBox(threadHWND, lpText, lpCaption, uType);
}

// ----------------------------------------------------------------------------
// getCurrentTimeString 
/** 
	Return the current time in string form
*/
// ----------------------------------------------------------------------------
static const char *getCurrentTimeString(void)
{
	time_t aclock;
	time(&aclock);
	struct tm *newtime = localtime(&aclock);
	return asctime(newtime);
}

#if defined(DEBUG_LOGGING) || defined(DEBUG_CRASHING)
// ----------------------------------------------------------------------------
// getCurrentTickString 
/** 
	Return the current TickCount in string form
*/
// ----------------------------------------------------------------------------
static const char *getCurrentTickString(void)
{
	static char TheTickString[32];
	WWFormat::Into(TheTickString, "(T={:08x})", ::GetTickCount());
	return TheTickString;
}

// ----------------------------------------------------------------------------
// prepBuffer
// zap the buffer and optionally prepend the tick time.
// ----------------------------------------------------------------------------
/**
	Empty the buffer passed in, then optionally prepend the current TickCount
	value in string form, depending on the setting of theDebugFlags.
*/
static const char *prepBuffer(const char* format, char *buffer)
{
	buffer[0] = 0;
#ifdef ALLOW_DEBUG_UTILS
	if (theDebugFlags & DEBUG_FLAG_PREPEND_TIME)
	{
		WWFormat::Into(buffer, LARGE_BUFFER, "{} ", getCurrentTickString());
	}
#endif
	return format;
}
#endif

// ----------------------------------------------------------------------------
// doLogOutput
/** 
	send a string directly to the log file and/or console without further processing.
*/
// ----------------------------------------------------------------------------
#ifdef DEBUG_LOGGING
static void doLogOutput(const char *buffer)
{
	// log message to file
	if (theDebugFlags & DEBUG_FLAG_LOG_TO_FILE)
	{
		if (theLogFile)
		{
			fprintf(theLogFile, "%s", buffer);	// note, no \n (should be there already)
			fflush(theLogFile);
		}
	}

	// log message to dev studio output window
	if (theDebugFlags & DEBUG_FLAG_LOG_TO_CONSOLE)
	{
		::OutputDebugString(buffer);
	}
}
#endif

#ifdef DEBUG_CRASHING
// ----------------------------------------------------------------------------
// doCrashBox
/*
	present a messagebox with the given message. Depending on user selection,
	we exit the app, break into debugger, or continue execution. 
*/
// ----------------------------------------------------------------------------
static int doCrashBox(const char *buffer, Bool logResult)
{
	int result;

	if (!ignoringAsserts()) {
		result = MessageBoxWrapper(buffer, "Assertion Failure", MB_ABORTRETRYIGNORE|MB_TASKMODAL|MB_ICONWARNING|MB_DEFBUTTON3);
		//result = MessageBoxWrapper(buffer, "Assertion Failure", MB_ABORTRETRYIGNORE|MB_TASKMODAL|MB_ICONWARNING);
	}	else {
		result = IDIGNORE;
	}

	switch(result)
	{
		case IDABORT:
#ifdef DEBUG_LOGGING
			if (logResult)
				DebugLog("[Abort]\n");
#endif
			_exit(1);
			break;
		case IDRETRY:
#ifdef DEBUG_LOGGING
			if (logResult)
				DebugLog("[Retry]\n");
#endif
			::DebugBreak();
			break;
		case IDIGNORE:
#ifdef DEBUG_LOGGING
			// do nothing, just keep going
			if (logResult)
				DebugLog("[Ignore]\n");
#endif
			break;
	}
	return result;
}
#endif

#ifdef DEBUG_STACKTRACE
// ----------------------------------------------------------------------------
/**
	Dumps a stack trace (from the current PC) to logfile and/or console.
*/
static void doStackDump()
{
	const int STACKTRACE_SIZE	= 24;
	const int STACKTRACE_SKIP = 2;
	void* stacktrace[STACKTRACE_SIZE];

	doLogOutput("\nStack Dump:\n");
	::FillStackAddresses(stacktrace, STACKTRACE_SIZE, STACKTRACE_SKIP);
	::StackDumpFromAddresses(stacktrace, STACKTRACE_SIZE, doLogOutput);
}
#endif

#ifdef DEBUG_LOGGING
// ----------------------------------------------------------------------------
// whackFunnyCharacters 
/** 
	Eliminates any undesirable nonprinting characters, aside from newline,
	replacing them with spaces.
*/
// ----------------------------------------------------------------------------
static void whackFunnyCharacters(char *buf)
{
	for (char *p = buf + strlen(buf) - 1; p >= buf; --p)
	{
		// ok, these are naughty magic numbers, but I'm guessing you know ASCII.... 
		if (*p >= 0 && *p < 32 && *p != 10 && *p != 13)
			*p = 32;
	}
}
#endif

// ----------------------------------------------------------------------------
// PUBLIC FUNCTIONS 
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// DebugInit 
// ----------------------------------------------------------------------------
#ifdef ALLOW_DEBUG_UTILS
/**
	Initialize the debug utilities. This should be called once, as near to the 
	start of the app as possible, before anything else (since other code will
	probably want to make use of it).
*/
void DebugInit(int flags)
{
//	if (theDebugFlags != 0)
//		::MessageBox(NULL, "Debug already inited", "", MB_OK|MB_APPLMODAL);

	// just quietly allow multiple calls to this, so that static ctors can call it.
	if (theDebugFlags == 0) 
	{
		theDebugFlags = flags;

		theMainThreadID = GetCurrentThreadId();

	#ifdef DEBUG_LOGGING

		char dirbuf[ _MAX_PATH ];
		::GetModuleFileName( nullptr, dirbuf, sizeof( dirbuf ) );
		char *pEnd = dirbuf + strlen( dirbuf );
		while( pEnd != dirbuf ) 
		{
			if( *pEnd == '\\' ) 
			{
				*(pEnd + 1) = 0;
				break;
			}
			pEnd--;
		}

		char prevbuf[ _MAX_PATH ];
		char curbuf[ _MAX_PATH ];

		// -mehrfach (see WinMain.cpp): one log per instance, m<PID>_DebugLogFile*.txt. Read directly from
		// the command line because static constructors call DebugInit before WinMain.
		char instancePrefix[32] = "";
		{
			const char *cmd = ::GetCommandLineA();
			for (const char *p = cmd; p && *p; ++p) {
				if (_strnicmp(p, "-mehrfach", 9) == 0 && (p[9] == 0 || p[9] == ' ' || p[9] == '"')
					&& p != cmd && (p[-1] == ' ' || p[-1] == '"')) {
					WWFormat::Into(instancePrefix, "m{:d}_", ::GetCurrentProcessId());
					break;
				}
			}
		}

		safe_strcpy(prevbuf, dirbuf);
		safe_strcat(prevbuf, instancePrefix);
		safe_strcat(prevbuf, gAppPrefix);
		safe_strcat(prevbuf, DEBUG_FILE_NAME_PREV);
		safe_strcpy(curbuf, dirbuf);
		safe_strcat(curbuf, instancePrefix);
		safe_strcat(curbuf, gAppPrefix);
		safe_strcat(curbuf, DEBUG_FILE_NAME);

 		remove(prevbuf);
		rename(curbuf, prevbuf);
		theLogFile = fopen(curbuf, "w");
		if (theLogFile != nullptr)
		{
			DebugLog("Log %s opened: %s\n", curbuf, getCurrentTimeString());
		} 
	#endif
	}

}  
#endif

// ----------------------------------------------------------------------------
// DebugLog 
// ----------------------------------------------------------------------------
#ifdef DEBUG_LOGGING
/**
	Print a character string to the logfile and/or console.
*/
void DebugLog(const char *format, ...)
{
#ifdef DEBUG_THREADSAFE
	ScopedCriticalSection scopedCriticalSection(TheDebugLogCriticalSection);
#endif

	if (theDebugFlags == 0)
		MessageBoxWrapper("DebugLog - Debug not inited properly", "", MB_OK|MB_TASKMODAL);

	format = prepBuffer(format, theBuffer);

	va_list arg;
  va_start(arg, format);
  const size_t used = strlen(theBuffer);
  vsnprintf(theBuffer + used, sizeof(theBuffer) - used, format, arg);
  va_end(arg);

	if (strlen(theBuffer) >= sizeof(theBuffer))
		MessageBoxWrapper("String too long for debug buffer", "", MB_OK|MB_TASKMODAL);

	whackFunnyCharacters(theBuffer);
	doLogOutput(theBuffer);
} 
#endif

// ----------------------------------------------------------------------------
// DebugCrash
// ----------------------------------------------------------------------------
#ifdef DEBUG_CRASHING
/**
	Print a character string to the logfile and/or console, then halt execution
	while presenting the user with an exit/debug/ignore dialog containing the same
	text message.
*/
void DebugCrash(const char *format, ...)
{
	// Note: You might want to make this thread safe, but we cannot. The reason is that 
	// there is an implicit requirement on other threads that the message loop be running.

	// make it not static so that it'll be thread-safe.
	// make it big to avoid weird overflow bugs in debug mode
	char theCrashBuffer[ LARGE_BUFFER ];	
	if (theDebugFlags == 0)
	{
		if (!RenderDevice_IsWindowed) {
			if (ApplicationHWnd) {
				ShowWindow(ApplicationHWnd, SW_HIDE);
			}
		}
		MessageBoxWrapper("DebugCrash - Debug not inited properly", "", MB_OK|MB_TASKMODAL);
	}

	format = prepBuffer(format, theCrashBuffer);
	safe_strcat(theCrashBuffer, "ASSERTION FAILURE: ");

	va_list arg;
  va_start(arg, format);
  const size_t used = strlen(theCrashBuffer);
  vsnprintf(theCrashBuffer + used, sizeof(theCrashBuffer) - used, format, arg);
  va_end(arg);

	if (strlen(theCrashBuffer) >= sizeof(theCrashBuffer))
	{
		if (!RenderDevice_IsWindowed) {
			if (ApplicationHWnd) {
				ShowWindow(ApplicationHWnd, SW_HIDE);
			}
		}
		MessageBoxWrapper("String too long for debug buffers", "", MB_OK|MB_TASKMODAL);
	}

#ifdef DEBUG_LOGGING
	if (ignoringAsserts()) 
	{
		doLogOutput("**** CRASH IN FULL SCREEN - Auto-ignored, CHECK THIS LOG!\n");
	}
	whackFunnyCharacters(theCrashBuffer);
	doLogOutput(theCrashBuffer);
#endif
#ifdef DEBUG_STACKTRACE
	if (!(TheGlobalData && TheGlobalData->m_debugIgnoreStackTrace))
	{
		doStackDump();
	}
#endif

	safe_strcat(theCrashBuffer, "\n\nAbort->exception; Retry->debugger; Ignore->continue\n");

	int result = doCrashBox(theCrashBuffer, true);

	if (result == IDIGNORE && TheCurrentIgnoreCrashPtr != nullptr) 
	{
		int yn;
		if (!ignoringAsserts()) 
		{
			yn = MessageBoxWrapper("Ignore this crash from now on?", "", MB_YESNO|MB_TASKMODAL);
		}	
		else 
		{
			yn = IDYES;
		}
		if (yn == IDYES)
			*TheCurrentIgnoreCrashPtr = 1;
		if( TheKeyboard )
			TheKeyboard->resetKeys();
		if( TheMouse )
			TheMouse->reset();
	}

}  
#endif

// ----------------------------------------------------------------------------
// DebugShutdown 
// ----------------------------------------------------------------------------
#ifdef ALLOW_DEBUG_UTILS
/**
	Shut down the debug utilities. This should be called once, as near to the 
	end of the app as possible, after everything else (since other code will
	probably want to make use of it).
*/
void DebugShutdown()
{
#ifdef DEBUG_LOGGING
	if (theLogFile)
	{
		DebugLog("Log closed: %s\n", getCurrentTimeString());
		fclose(theLogFile);
	}
	theLogFile = nullptr;
#endif
	theDebugFlags = 0;
}  

// ----------------------------------------------------------------------------
// DebugGetFlags 
// ----------------------------------------------------------------------------
/**
	Get the current values for the flags passed to DebugInit. Most code will never
	need to use this; the most common usage would be to temporarily enable or disable
	the DEBUG_FLAG_PREPEND_TIME bit for complex logfile messages.
*/
int DebugGetFlags()
{
	return theDebugFlags;
}

// ----------------------------------------------------------------------------
// DebugSetFlags 
// ----------------------------------------------------------------------------
/**
	Set the current values for the flags passed to DebugInit. Most code will never
	need to use this; the most common usage would be to temporarily enable or disable
	the DEBUG_FLAG_PREPEND_TIME bit for complex logfile messages.
*/
void DebugSetFlags(int flags)
{
	theDebugFlags = flags;
}

#endif	// ALLOW_DEBUG_UTILS

#ifdef DEBUG_PROFILE
// ----------------------------------------------------------------------------
SimpleProfiler::SimpleProfiler()
{
	QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER*>(&m_freq));
	m_startThisSession = 0;
	m_totalThisSession = 0;
	m_totalAllSessions = 0;
	m_numSessions = 0;
}

// ----------------------------------------------------------------------------
void SimpleProfiler::start()
{
	DEBUG_ASSERTCRASH(m_startThisSession == 0, ("already started"));
	QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&m_startThisSession));
}

// ----------------------------------------------------------------------------
void SimpleProfiler::stop()
{
	if (m_startThisSession != 0) 
	{
		__int64 stop;
		QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&stop));
		m_totalThisSession = stop - m_startThisSession;
		m_totalAllSessions += stop - m_startThisSession;
		m_startThisSession = 0;
		++m_numSessions;
	}
}

// ----------------------------------------------------------------------------
void SimpleProfiler::stopAndLog(const char *msg, int howOftenToLog, int howOftenToResetAvg)
{
	stop();
	// howOftenToResetAvg==0 means "never reset"
	if (howOftenToResetAvg > 0 && m_numSessions >= howOftenToResetAvg)
	{
		m_numSessions = 0;
		m_totalAllSessions = 0;
		DEBUG_LOG(("%s: reset averages\n",msg));
	}
	DEBUG_ASSERTLOG(m_numSessions % howOftenToLog != 0, ("%s: %f msec, total %f msec, avg %f msec\n",msg,getTime(),getTotalTime(),getAverageTime()));
}

// ----------------------------------------------------------------------------
double SimpleProfiler::getTime()
{
	stop();
	return static_cast<double>(m_totalThisSession) / static_cast<double>(m_freq) * 1000.0;
}

// ----------------------------------------------------------------------------
int SimpleProfiler::getNumSessions()
{
	stop();
	return m_numSessions;
}

// ----------------------------------------------------------------------------
double SimpleProfiler::getTotalTime()
{
	stop();
	if (!m_numSessions)
		return 0.0;

	return static_cast<double>(m_totalAllSessions) * 1000.0 / (static_cast<double>(m_freq));
}

// ----------------------------------------------------------------------------
double SimpleProfiler::getAverageTime()
{
	stop();
	if (!m_numSessions)
		return 0.0;

	return static_cast<double>(m_totalAllSessions) * 1000.0 / (static_cast<double>(m_freq) * static_cast<double>(m_numSessions));
}

#endif	// ALLOW_DEBUG_UTILS

// ----------------------------------------------------------------------------
// ReleaseCrash
// ----------------------------------------------------------------------------
/**
	Halt the application, EVEN IN FINAL RELEASE BUILDS. This should be called
	only when a crash is guaranteed by continuing, and no meaningful continuation
	of processing is possible, even by throwing an exception.
*/

	constexpr char RELEASECRASH_FILE_NAME[] = "ReleaseCrashInfo.txt";
	constexpr char RELEASECRASH_FILE_NAME_PREV[] = "ReleaseCrashInfoPrev.txt";

	static FILE *theReleaseCrashLogFile = nullptr;

	static void releaseCrashLogOutput(const char *buffer)
	{
		if (theReleaseCrashLogFile)
		{
			fprintf(theReleaseCrashLogFile, "%s", buffer);	// note, no \n (should be there already)
			fflush(theReleaseCrashLogFile);
		}
	}

void ReleaseCrash(const char *reason)
{
	/// do additional reporting on the crash, if possible

	if (!RenderDevice_IsWindowed) {
		if (ApplicationHWnd) {
			ShowWindow(ApplicationHWnd, SW_HIDE);
		}
	}
//#if defined(_DEBUG)
//	/* static */ char buff[8192]; // not so static so we can be threadsafe
//	_snprintf(buff, 8192, "Sorry, a serious error occurred. (%s)", reason);/
//	buff[8191] = 0;
//	::MessageBox(NULL, buff, "Technical Difficulties...", MB_OK|MB_SYSTEMMODAL|MB_ICONERROR);
//#else
//	::MessageBox(NULL, "Sorry, a serious error occurred.", "Technical Difficulties...", MB_OK|MB_TASKMODAL|MB_ICONERROR);
//#endif

	char prevbuf[ _MAX_PATH ];
	char curbuf[ _MAX_PATH ];

	if (TheGlobalData==nullptr) {
		return; // We are shutting down, and TheGlobalData has been freed.  jba. [4/15/2003]
	}

	safe_strcpy(prevbuf, TheGlobalData->getPath_UserData().str());
	safe_strcat(prevbuf, RELEASECRASH_FILE_NAME_PREV);
	safe_strcpy(curbuf, TheGlobalData->getPath_UserData().str());
	safe_strcat(curbuf, RELEASECRASH_FILE_NAME);

 	remove(prevbuf);
	rename(curbuf, prevbuf);

	theReleaseCrashLogFile = fopen(curbuf, "w");
	if (theReleaseCrashLogFile)
	{
		fprintf(theReleaseCrashLogFile, "Release Crash at %s; Reason %s\n", getCurrentTimeString(), reason);
		fprintf(theReleaseCrashLogFile, "\nLast error:\n%s\n\nCurrent stack:\n", g_LastErrorDump.str());
		const int STACKTRACE_SIZE	= 12;
		const int STACKTRACE_SKIP = 6;
		void* stacktrace[STACKTRACE_SIZE];
		::FillStackAddresses(stacktrace, STACKTRACE_SIZE, STACKTRACE_SKIP);
		::StackDumpFromAddresses(stacktrace, STACKTRACE_SIZE, releaseCrashLogOutput);

		fflush(theReleaseCrashLogFile);
		fclose(theReleaseCrashLogFile);
		theReleaseCrashLogFile = nullptr;
	}

	if (!RenderDevice_IsWindowed) {
		if (ApplicationHWnd) {
			ShowWindow(ApplicationHWnd, SW_HIDE);
		}
	}
#if defined(_DEBUG)
	/* static */ char buff[8192]; // not so static so we can be threadsafe
	WWFormat::Into(buff, 8192, "Sorry, a serious error occurred. ({})", reason);
	buff[8191] = 0;
	::MessageBox(nullptr, buff, "Technical Difficulties...", MB_OK|MB_SYSTEMMODAL|MB_ICONERROR);
#else
// crash error messaged changed 3/6/03 BGC
//	::MessageBox(NULL, "Sorry, a serious error occurred.", "Technical Difficulties...", MB_OK|MB_TASKMODAL|MB_ICONERROR);
//	::MessageBox(NULL, "You have encountered a serious error.  Serious errors can be caused by many things including viruses, overheated hardware and hardware that does not meet the minimum specifications for the game. Please visit the forums at www.generals.ea.com for suggested courses of action or consult your manual for Technical Support contact information.", "Technical Difficulties...", MB_OK|MB_TASKMODAL|MB_ICONERROR);

// crash error message changed again 8/22/03 M Lorenzen... made this message box modal to the system so it will appear on top of any task-modal windows, splash-screen, etc.
	// German installations get the German message.
	if (!GetGameLanguage().compareNoCase("german2") || !GetGameLanguage().compareNoCase("german") )
	{
		::MessageBox(nullptr, "Es ist ein gravierender Fehler aufgetreten. Solche Fehler können durch viele verschiedene Dinge wie Viren, überhitzte Hardware und Hardware, die den Mindestanforderungen des Spiels nicht entspricht, ausgelöst werden.", "Fehler...", MB_OK|MB_SYSTEMMODAL|MB_ICONERROR);
	}
	else
	{
  ::MessageBox(nullptr, "You have encountered a serious error.  Serious errors can be caused by many things including viruses, overheated hardware and hardware that does not meet the minimum specifications for the game.", 
   "Technical Difficulties...", 
   MB_OK|MB_SYSTEMMODAL|MB_ICONERROR);
	}


#endif

	_exit(1);
}  

void ReleaseCrashLocalized(const AsciiString& p, const AsciiString& m)
{
	if (!TheGameText) {
		ReleaseCrash(m.str());
		// This won't ever return
		return;
	}

	UnicodeString prompt = TheGameText->fetch(p);
	UnicodeString mesg = TheGameText->fetch(m);


	/// do additional reporting on the crash, if possible

	if (!RenderDevice_IsWindowed) {
		if (ApplicationHWnd) {
			ShowWindow(ApplicationHWnd, SW_HIDE);
		}
	}

	if (TheSystemIsUnicode) 
	{
		::MessageBoxW(nullptr, mesg.str(), prompt.str(), MB_OK|MB_SYSTEMMODAL|MB_ICONERROR);
	} 
	else 
	{
		// However, if we're using the default version of the message box, we need to 
		// translate the string into an AsciiString
		AsciiString promptA, mesgA;
		promptA.translate(prompt);
		mesgA.translate(mesg);
		//Make sure main window is not TOP_MOST
		::SetWindowPos(ApplicationHWnd, HWND_NOTOPMOST, 0, 0, 0, 0,SWP_NOSIZE |SWP_NOMOVE);
		::MessageBoxA(nullptr, mesgA.str(), promptA.str(), MB_OK|MB_TASKMODAL|MB_ICONERROR);
	}

	char prevbuf[ _MAX_PATH ];
	char curbuf[ _MAX_PATH ];

	safe_strcpy(prevbuf, TheGlobalData->getPath_UserData().str());
	safe_strcat(prevbuf, RELEASECRASH_FILE_NAME_PREV);
	safe_strcpy(curbuf, TheGlobalData->getPath_UserData().str());
	safe_strcat(curbuf, RELEASECRASH_FILE_NAME);

 	remove(prevbuf);
	rename(curbuf, prevbuf);

	theReleaseCrashLogFile = fopen(curbuf, "w");
	if (theReleaseCrashLogFile)
	{
		// mesg is a UnicodeString, "%s" expects char*.
		AsciiString mesgLog;
		mesgLog.translate(mesg);
		fprintf(theReleaseCrashLogFile, "Release Crash at %s; Reason %s\n", getCurrentTimeString(), mesgLog.str());

		const int STACKTRACE_SIZE	= 12;
		const int STACKTRACE_SKIP = 6;
		void* stacktrace[STACKTRACE_SIZE];
		::FillStackAddresses(stacktrace, STACKTRACE_SIZE, STACKTRACE_SKIP);
		::StackDumpFromAddresses(stacktrace, STACKTRACE_SIZE, releaseCrashLogOutput);

		fflush(theReleaseCrashLogFile);
		fclose(theReleaseCrashLogFile);
		theReleaseCrashLogFile = nullptr;
	}

	_exit(1);
}
