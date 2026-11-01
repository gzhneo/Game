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

// FILE: WinMain.cpp //////////////////////////////////////////////////////////
// 
// Entry point for game application
//
// Author: Colin Day, April 2001
//
///////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN  // only bare bones windows stuff wanted
#endif
#include <windows.h>
#include <stdlib.h>
#include <crtdbg.h>
#include <eh.h>
#include <ole2.h>
#include <dbt.h>
#include <shellapi.h>
#include <string>
#include <vector>

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "WinMain.h"
#include "formatinto.h"
#include "Lib/BaseType.h"
#include "Common/CriticalSection.h"
#include "Common/GlobalData.h"
#include "Common/GameEngine.h"
#include "Engine/ModCatalog.h"
#include "Common/GameSounds.h"
#include "Common/Debug.h"
#include "Common/GameMemory.h"
#include "Common/StackDump.h"
#include <signal.h>	// SIGABRT handler (onAbortSignal)
#include "Common/MessageStream.h"
#include "Common/InstallInfo.h"
#include "Common/GameEdition.h"
#include "Common/Team.h"
#include "GameClient/InGameUI.h"
#include "GameClient/GameClient.h"
#include "GameLogic/GameLogic.h"  ///< @todo for demo, remove
#include "GameClient/Mouse.h"
#include "GameClient/IMEManager.h"
#include "Win32Device/GameClient/Win32Mouse.h"
#include "Win32Device/GameClient/Win32Keyboard.h"
#include "Win32Device/Common/Win32GameEngine.h"
#include "Win32Device/Common/Win32Monitors.h"
#include "Common/UserSettings.h"
#include "Common/version.h"
#include "BuildVersion.h"
#include "resource.h"

// GLOBALS ////////////////////////////////////////////////////////////////////
HINSTANCE ApplicationHInstance = nullptr;  ///< our application instance
HWND ApplicationHWnd = nullptr;  ///< our application window handle
Bool ApplicationIsWindowed = false;
Bool ApplicationIsBorderless = false;	///< -borderless: borderless window at desktop size
static Int ApplicationFullscreenAreaWidth = 0;	///< -fullscreenArea: initial fullscreen window size
static Int ApplicationFullscreenAreaHeight = 0;
DWORD TheMessageTime = 0;	///< For getting the time that a message was posted from Windows.

const Char *g_strFile = "data\\Generals.str";
const Char *g_csfFile = "data\\%s\\Generals.csf";
const char *gAppPrefix = "";	/// So WB can have a different debug log file name.

static HANDLE GeneralsMutex = nullptr;
// Own mutex GUID per game, different from the original 685EAFF2-...: other Generals/Zero Hour
// builds on the same machine use that one, and a running instance of them would otherwise make
// this game quit with "Generals is already running".
#define GENERALS_GUID (isGameEditionZeroHour() ? "9A21F5C4-6E3B-4D7A-8F02-3C5B7A1E4D69" : "E7657C2F-B508-42A7-9AE1-58ACFC246ECF")
constexpr auto DEFAULT_XRESOLUTION = 800;
constexpr auto DEFAULT_YRESOLUTION = 600;

static Bool gInitializing = false;
static Bool gDoPaint = true;
static Bool isWinMainActive = false; 

static HBITMAP gLoadScreenBitmap = nullptr;

extern bool RenderDevice_IsWindowed;

// While the game is active the cursor stays in the window: always in fullscreen and borderless (with
// two monitors it would otherwise wander onto the other one and the edge scroll would miss that
// edge), in a window only if the setting asks for it. Released whenever another window is active.
static void updateCursorClip( HWND window, Bool active )
{
	Bool confine = active && window != nullptr && !IsIconic( window );
	if( confine && RenderDevice_IsWindowed && !ApplicationIsBorderless )
		confine = TheGlobalData != nullptr && UserSettings::get( "Controls.ConfineCursorInWindow" ).compareNoCase( "yes" ) == 0;
	RECT client;
	if( confine && GetClientRect( window, &client ) && MapWindowPoints( window, nullptr, reinterpret_cast<POINT *>( &client ), 2 ) != 0 )
		ClipCursor( &client );
	else
		ClipCursor( nullptr );
}

// The monitor the window is on, kept for the next start (State.ini).
static void rememberMonitor( HWND window )
{
	if( TheGlobalData == nullptr || TheGameEngine == nullptr || gInitializing )
		return;
	const AsciiString name = WindowMonitorName( window );
	if( name.isNotEmpty() && name.compareNoCase( UserSettings::get( "Window.Monitor" ) ) != 0 )
		UserSettings::set( "Window.Monitor", name );
}

//#define DEBUG_WINDOWS_MESSAGES

#ifdef DEBUG_WINDOWS_MESSAGES
static const char *messageToString(unsigned int message)
{	
	static char name[32];

	switch (message)
	{
	case WM_NULL: return "WM_NULL";                     
	case WM_CREATE: return  "WM_CREATE";               
	case WM_DESTROY: return  "WM_DESTROY";            
	case WM_MOVE: return  "WM_MOVE";               
	case WM_SIZE: return  "WM_SIZE";                 
	case WM_ACTIVATE: return  "WM_ACTIVATE";             
	case WM_SETFOCUS: return  "WM_SETFOCUS";             
	case WM_KILLFOCUS: return  "WM_KILLFOCUS";            
	case WM_ENABLE: return  "WM_ENABLE";               
	case WM_SETREDRAW: return  "WM_SETREDRAW";            
	case WM_SETTEXT: return  "WM_SETTEXT";              
	case WM_GETTEXT: return  "WM_GETTEXT";              
	case WM_GETTEXTLENGTH: return  "WM_GETTEXTLENGTH";        
	case WM_PAINT: return  "WM_PAINT";                
	case WM_CLOSE: return  "WM_CLOSE";                
	case WM_QUERYENDSESSION: return  "WM_QUERYENDSESSION";      
	case WM_QUIT: return  "WM_QUIT";                 
	case WM_QUERYOPEN: return  "WM_QUERYOPEN";            
	case WM_ERASEBKGND: return  "WM_ERASEBKGND";           
	case WM_SYSCOLORCHANGE: return  "WM_SYSCOLORCHANGE";       
	case WM_ENDSESSION: return  "WM_ENDSESSION";           
	case WM_SHOWWINDOW: return  "WM_SHOWWINDOW";           
	case WM_WININICHANGE: return "WM_WININICHANGE";
	case WM_DEVMODECHANGE: return  "WM_DEVMODECHANGE";        
	case WM_ACTIVATEAPP: return  "WM_ACTIVATEAPP";          
	case WM_FONTCHANGE: return  "WM_FONTCHANGE";           
	case WM_TIMECHANGE: return  "WM_TIMECHANGE";           
	case WM_CANCELMODE: return  "WM_CANCELMODE";           
	case WM_SETCURSOR: return  "WM_SETCURSOR";            
	case WM_MOUSEACTIVATE: return  "WM_MOUSEACTIVATE";        
	case WM_CHILDACTIVATE: return  "WM_CHILDACTIVATE";        
	case WM_QUEUESYNC: return  "WM_QUEUESYNC";            
	case WM_GETMINMAXINFO: return  "WM_GETMINMAXINFO";        
	case WM_PAINTICON: return  "WM_PAINTICON";            
	case WM_ICONERASEBKGND: return  "WM_ICONERASEBKGND";       
	case WM_NEXTDLGCTL: return  "WM_NEXTDLGCTL";           
	case WM_SPOOLERSTATUS: return  "WM_SPOOLERSTATUS";        
	case WM_DRAWITEM: return  "WM_DRAWITEM";             
	case WM_MEASUREITEM: return  "WM_MEASUREITEM";          
	case WM_DELETEITEM: return  "WM_DELETEITEM";           
	case WM_VKEYTOITEM: return  "WM_VKEYTOITEM";           
	case WM_CHARTOITEM: return  "WM_CHARTOITEM";           
	case WM_SETFONT: return  "WM_SETFONT";              
	case WM_GETFONT: return  "WM_GETFONT";              
	case WM_SETHOTKEY: return  "WM_SETHOTKEY";            
	case WM_GETHOTKEY: return  "WM_GETHOTKEY";            
	case WM_QUERYDRAGICON: return  "WM_QUERYDRAGICON";        
	case WM_COMPAREITEM: return  "WM_COMPAREITEM";          
	case WM_COMPACTING: return  "WM_COMPACTING";           
	case WM_COMMNOTIFY: return  "WM_COMMNOTIFY";
	case WM_WINDOWPOSCHANGING: return  "WM_WINDOWPOSCHANGING";    
	case WM_WINDOWPOSCHANGED: return  "WM_WINDOWPOSCHANGED";     
	case WM_POWER: return  "WM_POWER";                
	case WM_COPYDATA: return  "WM_COPYDATA";             
	case WM_CANCELJOURNAL: return  "WM_CANCELJOURNAL";        
	case WM_NOTIFY: return  "WM_NOTIFY";               
	case WM_INPUTLANGCHANGEREQUEST: return  "WM_INPUTLANGCHANGEREQUES";
	case WM_INPUTLANGCHANGE: return  "WM_INPUTLANGCHANGE";      
	case WM_TCARD: return  "WM_TCARD";                
	case WM_HELP: return  "WM_HELP";                 
	case WM_USERCHANGED: return  "WM_USERCHANGED";          
	case WM_NOTIFYFORMAT: return  "WM_NOTIFYFORMAT";         
	case WM_CONTEXTMENU: return  "WM_CONTEXTMENU";          
	case WM_STYLECHANGING: return  "WM_STYLECHANGING";        
	case WM_STYLECHANGED: return  "WM_STYLECHANGED";         
	case WM_DISPLAYCHANGE: return  "WM_DISPLAYCHANGE";        
	case WM_GETICON: return  "WM_GETICON";              
	case WM_SETICON: return  "WM_SETICON";              
	case WM_NCCREATE: return  "WM_NCCREATE";             
	case WM_NCDESTROY: return  "WM_NCDESTROY";            
	case WM_NCCALCSIZE: return  "WM_NCCALCSIZE";           
	case WM_NCHITTEST: return  "WM_NCHITTEST";            
	case WM_NCPAINT: return  "WM_NCPAINT";              
	case WM_NCACTIVATE: return  "WM_NCACTIVATE";           
	case WM_GETDLGCODE: return  "WM_GETDLGCODE";           
	case WM_SYNCPAINT: return  "WM_SYNCPAINT";            
	case WM_NCMOUSEMOVE: return  "WM_NCMOUSEMOVE";          
	case WM_NCLBUTTONDOWN: return  "WM_NCLBUTTONDOWN";        
	case WM_NCLBUTTONUP: return  "WM_NCLBUTTONUP";          
	case WM_NCLBUTTONDBLCLK: return  "WM_NCLBUTTONDBLCLK";      
	case WM_NCRBUTTONDOWN: return  "WM_NCRBUTTONDOWN";        
	case WM_NCRBUTTONUP: return  "WM_NCRBUTTONUP";          
	case WM_NCRBUTTONDBLCLK: return  "WM_NCRBUTTONDBLCLK";      
	case WM_NCMBUTTONDOWN: return  "WM_NCMBUTTONDOWN";        
	case WM_NCMBUTTONUP: return  "WM_NCMBUTTONUP";          
	case WM_NCMBUTTONDBLCLK: return  "WM_NCMBUTTONDBLCLK";      
	case WM_KEYDOWN: return  "WM_KEYDOWN";              
	case WM_KEYUP: return  "WM_KEYUP";                
	case WM_CHAR: return  "WM_CHAR";                 
	case WM_DEADCHAR: return  "WM_DEADCHAR";             
	case WM_SYSKEYDOWN: return  "WM_SYSKEYDOWN";           
	case WM_SYSKEYUP: return  "WM_SYSKEYUP";             
	case WM_SYSCHAR: return  "WM_SYSCHAR";              
	case WM_SYSDEADCHAR: return  "WM_SYSDEADCHAR";          
	case WM_KEYLAST: return  "WM_KEYLAST";              
	case WM_IME_STARTCOMPOSITION: return  "WM_IME_STARTCOMPOSITION"; 
	case WM_IME_ENDCOMPOSITION: return  "WM_IME_ENDCOMPOSITION";   
	case WM_IME_COMPOSITION: return  "WM_IME_COMPOSITION";      
	case WM_INITDIALOG: return  "WM_INITDIALOG";           
	case WM_COMMAND: return  "WM_COMMAND";              
	case WM_SYSCOMMAND: return  "WM_SYSCOMMAND";           
	case WM_TIMER: return  "WM_TIMER";                
	case WM_HSCROLL: return  "WM_HSCROLL";              
	case WM_VSCROLL: return  "WM_VSCROLL";              
	case WM_INITMENU: return  "WM_INITMENU";             
	case WM_INITMENUPOPUP: return  "WM_INITMENUPOPUP";        
	case WM_MENUSELECT: return  "WM_MENUSELECT";           
	case WM_MENUCHAR: return  "WM_MENUCHAR";             
	case WM_ENTERIDLE: return  "WM_ENTERIDLE";            
	case WM_CTLCOLORMSGBOX: return  "WM_CTLCOLORMSGBOX";       
	case WM_CTLCOLOREDIT: return  "WM_CTLCOLOREDIT";         
	case WM_CTLCOLORLISTBOX: return  "WM_CTLCOLORLISTBOX";      
	case WM_CTLCOLORBTN: return  "WM_CTLCOLORBTN";          
	case WM_CTLCOLORDLG: return  "WM_CTLCOLORDLG";          
	case WM_CTLCOLORSCROLLBAR: return  "WM_CTLCOLORSCROLLBAR";    
	case WM_CTLCOLORSTATIC: return  "WM_CTLCOLORSTATIC";       
	case WM_MOUSEMOVE: return  "WM_MOUSEMOVE";            
	case WM_LBUTTONDOWN: return  "WM_LBUTTONDOWN";          
	case WM_LBUTTONUP: return  "WM_LBUTTONUP";            
	case WM_LBUTTONDBLCLK: return  "WM_LBUTTONDBLCLK";        
	case WM_RBUTTONDOWN: return  "WM_RBUTTONDOWN";          
	case WM_RBUTTONUP: return  "WM_RBUTTONUP";            
	case WM_RBUTTONDBLCLK: return  "WM_RBUTTONDBLCLK";        
	case WM_MBUTTONDOWN: return  "WM_MBUTTONDOWN";          
	case WM_MBUTTONUP: return  "WM_MBUTTONUP";            
	case WM_MBUTTONDBLCLK: return  "WM_MBUTTONDBLCLK";        
//	case WM_MOUSEWHEEL: return  "WM_MOUSEWHEEL";           
	case WM_PARENTNOTIFY: return  "WM_PARENTNOTIFY";         
	case WM_ENTERMENULOOP: return  "WM_ENTERMENULOOP";        
	case WM_EXITMENULOOP: return  "WM_EXITMENULOOP";         
	case WM_NEXTMENU: return  "WM_NEXTMENU";             
	case WM_SIZING: return  "WM_SIZING";               
	case WM_CAPTURECHANGED: return  "WM_CAPTURECHANGED";       
	case WM_MOVING: return  "WM_MOVING";               
	case WM_POWERBROADCAST: return  "WM_POWERBROADCAST";
	case WM_DEVICECHANGE: return  "WM_DEVICECHANGE";         
	case WM_MDICREATE: return  "WM_MDICREATE";            
	case WM_MDIDESTROY: return  "WM_MDIDESTROY";           
	case WM_MDIACTIVATE: return  "WM_MDIACTIVATE";          
	case WM_MDIRESTORE: return  "WM_MDIRESTORE";           
	case WM_MDINEXT: return  "WM_MDINEXT";              
	case WM_MDIMAXIMIZE: return  "WM_MDIMAXIMIZE";          
	case WM_MDITILE: return  "WM_MDITILE";              
	case WM_MDICASCADE: return  "WM_MDICASCADE";           
	case WM_MDIICONARRANGE: return  "WM_MDIICONARRANGE";       
	case WM_MDIGETACTIVE: return  "WM_MDIGETACTIVE";         
	case WM_MDISETMENU: return  "WM_MDISETMENU";           
	case WM_ENTERSIZEMOVE: return  "WM_ENTERSIZEMOVE";        
	case WM_EXITSIZEMOVE: return  "WM_EXITSIZEMOVE";         
	case WM_DROPFILES: return  "WM_DROPFILES";            
	case WM_MDIREFRESHMENU: return  "WM_MDIREFRESHMENU";       
	case WM_IME_SETCONTEXT: return  "WM_IME_SETCONTEXT";       
	case WM_IME_NOTIFY: return  "WM_IME_NOTIFY";           
	case WM_IME_CONTROL: return  "WM_IME_CONTROL";          
	case WM_IME_COMPOSITIONFULL: return  "WM_IME_COMPOSITIONFULL";  
	case WM_IME_SELECT: return  "WM_IME_SELECT";           
	case WM_IME_CHAR: return  "WM_IME_CHAR";             
	case WM_IME_KEYDOWN: return  "WM_IME_KEYDOWN";          
	case WM_IME_KEYUP: return  "WM_IME_KEYUP";            
//	case WM_MOUSEHOVER: return  "WM_MOUSEHOVER";           
//	case WM_MOUSELEAVE: return  "WM_MOUSELEAVE";           
	case WM_CUT: return  "WM_CUT";                  
	case WM_COPY: return  "WM_COPY";                 
	case WM_PASTE: return  "WM_PASTE";                
	case WM_CLEAR: return  "WM_CLEAR";                
	case WM_UNDO: return  "WM_UNDO";                 
	case WM_RENDERFORMAT: return  "WM_RENDERFORMAT";         
	case WM_RENDERALLFORMATS: return  "WM_RENDERALLFORMATS";     
	case WM_DESTROYCLIPBOARD: return  "WM_DESTROYCLIPBOARD";     
	case WM_DRAWCLIPBOARD: return  "WM_DRAWCLIPBOARD";        
	case WM_PAINTCLIPBOARD: return  "WM_PAINTCLIPBOARD";       
	case WM_VSCROLLCLIPBOARD: return  "WM_VSCROLLCLIPBOARD";     
	case WM_SIZECLIPBOARD: return  "WM_SIZECLIPBOARD";        
	case WM_ASKCBFORMATNAME: return  "WM_ASKCBFORMATNAME";      
	case WM_CHANGECBCHAIN: return  "WM_CHANGECBCHAIN";        
	case WM_HSCROLLCLIPBOARD: return  "WM_HSCROLLCLIPBOARD";     
	case WM_QUERYNEWPALETTE: return  "WM_QUERYNEWPALETTE";      
	case WM_PALETTEISCHANGING: return  "WM_PALETTEISCHANGING";    
	case WM_PALETTECHANGED: return  "WM_PALETTECHANGED";       
	case WM_HOTKEY: return  "WM_HOTKEY";               
	case WM_PRINT: return  "WM_PRINT";                
	case WM_PRINTCLIENT: return  "WM_PRINTCLIENT";          
	case WM_HANDHELDFIRST: return  "WM_HANDHELDFIRST";        
	case WM_HANDHELDLAST: return  "WM_HANDHELDLAST";         
	case WM_AFXFIRST: return  "WM_AFXFIRST";             
	case WM_AFXLAST: return  "WM_AFXLAST";              
	case WM_PENWINFIRST: return  "WM_PENWINFIRST";          
	case WM_PENWINLAST: return  "WM_PENWINLAST";
	default: return "WM_UNKNOWN";
	};
}
#endif

// WndProc ====================================================================
/** Window Procedure */
//=============================================================================
LRESULT CALLBACK WndProc( HWND hWnd, UINT message, 
													WPARAM wParam, LPARAM lParam )
{

	try
	{
		// First let the IME manager do it's stuff. 
		if ( TheIMEManager )
		{
			if ( TheIMEManager->serviceIMEMessage( hWnd, message, wParam, lParam ) )
			{
				// The manager intercepted an IME message so return the result
				return TheIMEManager->result();
			}
		}

#ifdef	DEBUG_WINDOWS_MESSAGES
		static msgCount=0;
		char testString[256];
		WWFormat::Into(testString, "\n{:d}: {} ({:X},{:X})", msgCount++, messageToString(message), static_cast<unsigned>(wParam), static_cast<unsigned>(lParam)); 
		OutputDebugString(testString);
#endif

		// handle all window messages
		switch( message ) 
		{
			//-------------------------------------------------------------------------
			case WM_NCHITTEST:
			// Prevent the user from selecting the menu in fullscreen mode
            if( !ApplicationIsWindowed )
                return HTCLIENT;
            break;

			//-------------------------------------------------------------------------
			case WM_POWERBROADCAST:
            switch( wParam )
            {
                #ifndef PBT_APMQUERYSUSPEND
                    #define PBT_APMQUERYSUSPEND 0x0000
                #endif
                case PBT_APMQUERYSUSPEND:
                    // At this point, the app should save any data for open
                    // network connections, files, etc., and prepare to go into
                    // a suspended mode.
                    return TRUE;

                #ifndef PBT_APMRESUMESUSPEND
                    #define PBT_APMRESUMESUSPEND 0x0007
                #endif
                case PBT_APMRESUMESUSPEND:
                    // At this point, the app should recover any data, network
                    // connections, files, etc., and resume running from when
                    // the app was suspended.
                    return TRUE;
            }
            break;
			//-------------------------------------------------------------------------
			case WM_SYSCOMMAND:
            // Prevent moving/sizing and power loss in fullscreen mode
            switch( wParam )
            {
                case SC_MOVE:
                case SC_SIZE:
                case SC_MAXIMIZE:
                case SC_KEYMENU:
                case SC_MONITORPOWER:
                    if( FALSE == ApplicationIsWindowed )
                        return 1;
                    break;
            }
            break;

			// ------------------------------------------------------------------------
			case WM_QUERYENDSESSION:
			{
				TheMessageStream->appendMessage(GameMessage::MSG_META_DEMO_INSTANT_QUIT);
				return 0;	//don't allow Windows to shutdown while game is running.
			}

			// ------------------------------------------------------------------------
			case WM_CLOSE:
			if (!TheGameEngine->getQuitting())
			{
				//user is exiting without using the menus

				//This method didn't work in cinematics because we don't process messages.
				//But it's the cleanest way to exit that's similar to using menus.
				TheMessageStream->appendMessage(GameMessage::MSG_META_DEMO_INSTANT_QUIT);

				//This method used to disable quitting.  We just put up the options screen instead.
				//TheMessageStream->appendMessage(GameMessage::MSG_META_OPTIONS);

				//This method works everywhere but isn't as clean at shutting down.
				//TheGameEngine->checkAbnormalQuitting();	//old way to log disconnections for ALT-F4
				//TheGameEngine->reset();
				//TheGameEngine->setQuitting(TRUE);
				//_exit(EXIT_SUCCESS);
				return 0;
			}
			// Original behaviour: while already quitting, deliberately falls through (no break)
			// into the WM_SETFOCUS case.
			[[fallthrough]];

			// ------------------------------------------------------------------------
			case WM_SETFOCUS:
			{

				//
				// reset the state of our keyboard cause we haven't been paying
				// attention to the keys while focus was away
				//
				if( TheKeyboard )
					TheKeyboard->resetKeys();

				if (TheWin32Mouse)
					TheWin32Mouse->lostFocus(FALSE);
				// no key counts as pressed any more (see WM_KILLFOCUS)
				if( TheWin32Keyboard )
					TheWin32Keyboard->releaseAllKeys();

				break;

			}  // end set focus

			//-------------------------------------------------------------------------
			case WM_INPUTLANGCHANGE:
				// key names follow the active layout
				if( TheKeyboard )
					TheKeyboard->refreshKeyNames();
				break;

			//-------------------------------------------------------------------------
			case WM_SIZE:
				// When W3D initializes, it resizes the window.  So stop repainting.
				if (!gInitializing) 
					gDoPaint = false;
				updateCursorClip( hWnd, isWinMainActive && GetActiveWindow() == hWnd );
				break;

			case WM_MOVE:
				updateCursorClip( hWnd, isWinMainActive && GetActiveWindow() == hWnd );
				rememberMonitor( hWnd );
				break;

			// The window moved to a monitor with other scaling. A game window keeps its client size (the
			// resolution); it takes only the position Windows suggests.
			case 0x02E0:	// WM_DPICHANGED
			{
				const RECT *suggested = reinterpret_cast<const RECT *>( lParam );
				if( suggested != nullptr && RenderDevice_IsWindowed && !ApplicationIsBorderless )
				{
					// the frame changes with the scaling: size the window for the same client area at the new DPI
					typedef BOOL (WINAPI *AdjustForDpiProc)( LPRECT, DWORD, BOOL, DWORD, UINT );
					static AdjustForDpiProc s_adjust = reinterpret_cast<AdjustForDpiProc>(
						reinterpret_cast<void *>( GetProcAddress( GetModuleHandleW( L"user32.dll" ), "AdjustWindowRectExForDpi" ) ) );
					RECT client;
					GetClientRect( hWnd, &client );
					RECT frame = client;
					const DWORD style = static_cast<DWORD>( GetWindowLongW( hWnd, GWL_STYLE ) );
					const DWORD exStyle = static_cast<DWORD>( GetWindowLongW( hWnd, GWL_EXSTYLE ) );
					if( s_adjust != nullptr && s_adjust( &frame, style, FALSE, exStyle, HIWORD( wParam ) ) )
						SetWindowPos( hWnd, nullptr, suggested->left, suggested->top, frame.right - frame.left, frame.bottom - frame.top,
							SWP_NOZORDER | SWP_NOACTIVATE );
					else
						SetWindowPos( hWnd, nullptr, suggested->left, suggested->top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
				}
				updateCursorClip( hWnd, isWinMainActive && GetActiveWindow() == hWnd );
				return 0;
			}

			//-------------------------------------------------------------------------
			case WM_KILLFOCUS:
			{
				if (TheKeyboard )
					TheKeyboard->resetKeys();
				if (TheWin32Mouse)
					TheWin32Mouse->lostFocus(TRUE);
				// Queue a release for every key still held: the real key-up now goes to another
				// window, otherwise the key would stay stuck.
				if( TheWin32Keyboard )
					TheWin32Keyboard->releaseAllKeys();

				break;
			}

			//-------------------------------------------------------------------------
			case WM_ACTIVATEAPP:
			{
//				DWORD threadId=GetCurrentThreadId();
				if (static_cast<bool>(wParam) != isWinMainActive)
				{	isWinMainActive = static_cast<BOOL>(wParam);
					
					if (TheGameEngine)
						TheGameEngine->setIsActive(isWinMainActive);

					if (isWinMainActive)
					{	//restore mouse cursor to our custom version.
						if (TheWin32Mouse)
							TheWin32Mouse->setCursor(TheWin32Mouse->getMouseCursor());
					}
				}
				return 0;
			}
			//-------------------------------------------------------------------------
			case WM_ACTIVATE:
			{
				Int active = LOWORD( wParam );

				//
				// when window is becoming deactivated we must release mouse cursor
				// locks on our region, otherwise set the mouse limit region again
				// which will clip the cursor to our window
				//
				if( active == WA_INACTIVE )
				{

					updateCursorClip( hWnd, FALSE );
					if (TheAudio)
						TheAudio->loseFocus();
				}  // end if
				else
				{
					if( TheMouse )
						TheMouse->setMouseLimits();
					updateCursorClip( hWnd, TRUE );

					if (TheAudio)
						TheAudio->regainFocus();

				}  // end else
				break;

			}  // end case activate

			//-------------------------------------------------------------------------
			// The keyboard (Win32Keyboard) is fed from these messages. WM_SYSKEY* then continue to
			// DefWindowProc (Alt+F4).
			case WM_SYSKEYDOWN:
			case WM_SYSKEYUP:
			case WM_KEYUP:
			{
				if( TheWin32Keyboard )
					TheWin32Keyboard->addWin32Event( message, wParam, lParam );
				break;
			}
			case WM_KEYDOWN:
			{
				if( TheWin32Keyboard )
					TheWin32Keyboard->addWin32Event( message, wParam, lParam );
				return 0;

			}  // end WM_KEYDOWN

			//-------------------------------------------------------------------------
			case WM_LBUTTONDOWN:
			case WM_LBUTTONUP:
			case WM_LBUTTONDBLCLK:

			case WM_MBUTTONDOWN:
			case WM_MBUTTONUP:
			case WM_MBUTTONDBLCLK:

			case WM_RBUTTONDOWN:
			case WM_RBUTTONUP:
			case WM_RBUTTONDBLCLK:
			{

				if( TheWin32Mouse )
					TheWin32Mouse->addWin32Event( message, wParam, lParam, TheMessageTime );

				return 0;

			}  // end WM_LBUTTONDOWN

			//-------------------------------------------------------------------------
			case 0x020A: // WM_MOUSEWHEEL
			case 0x020E: // WM_MOUSEHWHEEL
			{
				// screen coordinates, signed: negative on monitors left of or above the primary one
				long x = static_cast<Short>(LOWORD(lParam));
				long y = static_cast<Short>(HIWORD(lParam));
				RECT rect;

				// ignore when outside of client area
				GetWindowRect( ApplicationHWnd, &rect );
				if( x < rect.left || x > rect.right || y < rect.top || y > rect.bottom )
					return 0;

				if( TheWin32Mouse )
					TheWin32Mouse->addWin32Event( message, wParam, lParam, TheMessageTime );

				return 0;

			}  // end WM_MOUSEWHEEL


			//-------------------------------------------------------------------------
			case WM_MOUSEMOVE:
			{
				Int x = static_cast<Short>(LOWORD( lParam ));
				Int y = static_cast<Short>(HIWORD( lParam ));
				RECT rect;
//				Int keys = wParam;

				// ignore when outside of client area
				GetClientRect( ApplicationHWnd, &rect );
				if( x < rect.left || x > rect.right || y < rect.top || y > rect.bottom )
					return 0;

				if( TheWin32Mouse )
					TheWin32Mouse->addWin32Event( message, wParam, lParam, TheMessageTime );

				return 0;

			}  // end WM_MOUSEMOVE

			//-------------------------------------------------------------------------
			case WM_SETCURSOR:
			{
				if (TheWin32Mouse && reinterpret_cast<HWND>(wParam) == ApplicationHWnd)
					TheWin32Mouse->setCursor(TheWin32Mouse->getMouseCursor());
				return TRUE;	//tell Windows not to reset mouse cursor image to default.
			}

			case WM_PAINT:
			{
				if (gDoPaint) {
					PAINTSTRUCT paint;
					HDC dc = ::BeginPaint(hWnd, &paint);
					if (gLoadScreenBitmap!=nullptr) {
						Int savContext = ::SaveDC(dc);
						// Center the splash image on a black window; a full-screen window is larger than the image.
						RECT client;
						::GetClientRect(hWnd, &client);
						::FillRect(dc, &client, static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH)));
						BITMAP bmp = {};
						Int imageW = DEFAULT_XRESOLUTION, imageH = DEFAULT_YRESOLUTION;
						if (::GetObject(gLoadScreenBitmap, sizeof(bmp), &bmp) != 0) {
							imageW = bmp.bmWidth;
							imageH = bmp.bmHeight;
						}
						const Int left = ((client.right - client.left) - imageW) / 2;
						const Int top = ((client.bottom - client.top) - imageH) / 2;
						HDC tmpDC = ::CreateCompatibleDC(dc);
						HBITMAP savBitmap = static_cast<HBITMAP>(::SelectObject(tmpDC, gLoadScreenBitmap));
						::BitBlt(dc, left, top, imageW, imageH, tmpDC, 0, 0, SRCCOPY);
						::SelectObject(tmpDC, savBitmap);
						::DeleteDC(tmpDC);
						::RestoreDC(dc, savContext);
					}
					::EndPaint(hWnd, &paint);
					return TRUE;
				}
				break;
			}

			case WM_ERASEBKGND:
			{
				if (!gDoPaint) 
					return TRUE;	//we don't need to erase the background because we always draw entire window.
				break;
			}

		}  // end switch

	}
	catch (...)
	{
		RELEASE_CRASH(("Uncaught exception in Main::WndProc... probably should not happen\n"));
		// no rethrow
	}

//In full-screen mode, only pass these messages onto the default windows handler.
//Appears to fix issues with dual monitor systems but doesn't seem safe?
///@todo: Look into proper support for dual monitor systems.
/*	if (!ApplicationIsWindowed)
	switch (message)
	{
		case WM_PAINT:
		case WM_NCCREATE:
		case WM_NCDESTROY:
		case WM_NCCALCSIZE:
		case WM_NCPAINT:
				return DefWindowProcW( hWnd, message, wParam, lParam );
	}
	return 0;*/

	return DefWindowProcW( hWnd, message, wParam, lParam );

}  // end WndProc

// initializeAppWindows =======================================================
/** Register windows class and create application windows. */
//=============================================================================
static Bool initializeAppWindows( HINSTANCE hInstance, Int nCmdShow, Bool runWindowed )
{
	DWORD windowStyle;
	Int startWidth = DEFAULT_XRESOLUTION,
			startHeight = DEFAULT_YRESOLUTION;

	// register the window class

	// Unicode window: WM_CHAR and WM_IME_CHAR carry UTF-16, independent of the system code page.
  WNDCLASSW wndClass = { CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, WndProc, 0, 0, hInstance,
                       LoadIconW (hInstance, MAKEINTRESOURCEW(IDI_ApplicationIcon)),
                       nullptr/*LoadCursor(NULL, IDC_ARROW)*/, 
                       static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)), nullptr,
	                     L"Game Window" };
  RegisterClassW( &wndClass );

   // Create our main window
	windowStyle =  WS_POPUP|WS_VISIBLE;
	if (ApplicationIsBorderless)
		// Borderless window at desktop size at position 0,0: no frame, no title bar, no exclusive
		// fullscreen mode switch. renderdevice later fits the window to the resolution (set to
		// desktop size); AdjustWindowRect is a no-op for WS_POPUP, SWP_NOMOVE keeps 0,0.
		windowStyle = WS_POPUP | WS_VISIBLE;
	else if (runWindowed)
		// WS_MINIMIZEBOX so the windowed game can be minimized. No WS_MAXIMIZEBOX: the window
		// size is fixed to the chosen resolution and there is no resize handling for an
		// arbitrarily maximized size.
		windowStyle |= WS_DLGFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
	else
		// Fullscreen: borderless window covering the monitor (renderdevice fits it to the monitor
		// the window ends up on). Not topmost, so Alt-Tab brings other windows in front of it.
		windowStyle |= WS_SYSMENU;

	if (ApplicationIsBorderless || !runWindowed) {
		startWidth = GetSystemMetrics( SM_CXSCREEN );
		startHeight = GetSystemMetrics( SM_CYSCREEN );
		if (!runWindowed && ApplicationFullscreenAreaWidth > 0 && ApplicationFullscreenAreaHeight > 0 &&
			ApplicationFullscreenAreaWidth <= startWidth && ApplicationFullscreenAreaHeight <= startHeight) {
			startWidth = ApplicationFullscreenAreaWidth;
			startHeight = ApplicationFullscreenAreaHeight;
		}
	}

	RECT rect;
	rect.left = 0;
	rect.top = 0;
	rect.right = startWidth;
	rect.bottom = startHeight;
	AdjustWindowRect (&rect, windowStyle, FALSE);
	if (runWindowed && !ApplicationIsBorderless) {
		// Makes the normal debug 800x600 window center in the screen.
		startWidth = DEFAULT_XRESOLUTION;
		startHeight= DEFAULT_YRESOLUTION;
	}

	gInitializing = true;

  HWND hWnd = CreateWindowW( L"Game Window",
                            isGameEditionZeroHour() ? L"GZH Neo \u2013 Zero Hour" : L"GZH Neo \u2013 Generals",
                            windowStyle,
														ApplicationIsBorderless ? 0 : (GetSystemMetrics( SM_CXSCREEN ) / 2) - (startWidth / 2), // original position X
														ApplicationIsBorderless ? 0 : (GetSystemMetrics( SM_CYSCREEN ) / 2) - (startHeight / 2),// original position Y
														// Lorenzen nudged the window higher
														// so the constantdebug report would 
														// not get obliterated by assert windows, thank you.
														//(GetSystemMetrics( SM_CXSCREEN ) / 2) - (startWidth / 2),   //this works with any screen res
														//(GetSystemMetrics( SM_CYSCREEN ) / 25) - (startHeight / 25),//this works with any screen res
														rect.right-rect.left,
														rect.bottom-rect.top,
														0L, 
														0L, 
														hInstance, 
														0L );


	SetWindowPos(hWnd, HWND_TOP, 0, 0, 0, 0,SWP_NOSIZE |SWP_NOMOVE);

	SetFocus(hWnd);

	SetForegroundWindow(hWnd);
	ShowWindow( hWnd, nCmdShow );
	UpdateWindow( hWnd );

	// save our application instance and window handle for future use
	ApplicationHInstance = hInstance;
	ApplicationHWnd = hWnd;
	gInitializing = false;
	if (!runWindowed) {
		gDoPaint = false;
	}

	return true;  // success

}  // end initializeAppWindows

// trimToken ===================================================================
/** Trim leading and trailing whitespace from a character string (in place). */
//=============================================================================
static char* trimToken(char* buffer)
{
	if (buffer != nullptr) {
		//	Strip leading white space from the string.
		char * source = buffer;
		while ((*source != 0) && (static_cast<unsigned char>(*source) <= 32))
		{
			source++;
		}

		if (source != buffer)
		{
			memmove(buffer, source, strlen(source) + 1);	// overlapping ranges
		}

		//	Clip trailing white space from the string.
		for (int index = static_cast<int>(strlen(buffer))-1; index >= 0; index--)
		{
			if (static_cast<unsigned char>(buffer[index]) <= 32)
			{
				buffer[index] = '\0';
			}
			else
			{
				break;
			}
		}
	}

	return buffer;
}

char *nextParam(char *newSource, const char *seps)
{
	static char *source = nullptr;
	if (newSource)
	{
		source = newSource;
	}
	if (!source)
	{
		return nullptr;
	}

	// find first separator
	char *first = source;//strpbrk(source, seps);
	if (first)
	{
		// go past separator
		char *firstSep = strpbrk(first, seps);
		char firstChar[2] = {0,0};
		if (firstSep == first)
		{
			firstChar[0] = *first;
			while (*first == firstChar[0]) first++;
		}

		// find end
		char *end;
		if (firstChar[0])
			end = strpbrk(first, firstChar);
		else
			end = strpbrk(first, seps);

		// trim string & save next start pos
		if (end)
		{
			source = end+1;
			*end = 0;

			if (!*source)
				source = nullptr;
		}
		else
		{
			source = nullptr;
		}

		if (first && !*first)
			first = nullptr;
	}

	return first;
}

// Necessary to allow memory managers and such to have useful critical sections
// The four engine locks are created by Common/CriticalSection.cpp on first use, so they also work
// before WinMain and in tools without WinMain. critSec1 is unused.
static CriticalSection critSec1;

// WinMain ====================================================================
/** Application entry point */
//=============================================================================
#ifdef _DEBUG
// Diagnostic aid, installed only with "-crashOnAssert" (see the command line preparse below): a CRT
// debug assert (_CRT_ASSERT, e.g. the bounds check of std::vector::operator[] in the MSVC STL)
// normally shows a blocking modal dialog without a usable C++ stack. This report hook turns the
// _CRT_ASSERT into a real access violation, which the exception handlers below log with a full
// stack trace to DebugLogFileD.txt. Without the flag the normal assert dialog stays.
static int CrtAssertToCrashHook(int reportType, char *message, int * /*returnValue*/)
{
	if (reportType == _CRT_ASSERT)
	{
		DEBUG_LOG(("CRT ASSERT turned into a crash for the stack trace: %s\n", message ? message : "(no message)"));
		*static_cast<volatile int *>(0) = 0;
	}
	return FALSE;
}
#endif

// Process-wide unhandled exception filter. It does not depend on C++ exception handling and also
// catches crashes on other threads or outside the try block in WinMain; _set_se_translator only
// covers the main thread's try block.
static LONG WINAPI TheUnhandledExceptionFilter( EXCEPTION_POINTERS *e_info )
{
	DumpExceptionInfo( e_info->ExceptionRecord->ExceptionCode, e_info );
	return EXCEPTION_EXECUTE_HANDLER;
}

#include "wwtracy.h"	// thread name
#include "Engine/EngineLog.h"
#include "Engine/ThreadRole.h"
#include "Lib/SafeString.h"

#ifdef _DEBUG
// abort() (e.g. an assertion inside FFmpeg or std::terminate) would otherwise end with the modal CRT
// dialog or silently with exit code 3, leaving no trace in DebugLogFileD.txt. The handler logs the
// stack (line "ABORT", which tools/lecksuche treats as a crash); with -ignoreAsserts the dialog
// is suppressed so unattended runs do not hang.
static void abortStackLine( const char *lineText )
{
	DEBUG_LOG(( "  %s\n", lineText ));
}

static void __cdecl onAbortSignal( int )
{
	DEBUG_LOG(( "ABORT: abort() called (SIGABRT), stack:\n" ));
	StackDump( abortStackLine );
}
#endif

// A changed mod list: start the game again with the switches of this run (without -mod). The
// single-instance lock goes first, or the new process would find this one and stop.
static void startAgain( void )
{
	if (GeneralsMutex != nullptr)
	{
		CloseHandle(GeneralsMutex);
		GeneralsMutex = nullptr;
	}
	int count = 0;
	LPWSTR *args = ::CommandLineToArgvW(::GetCommandLineW(), &count);
	if (args == nullptr || count < 1)
		return;
	wchar_t exe[MAX_PATH * 4];
	const DWORD length = ::GetModuleFileNameW(nullptr, exe, MAX_PATH * 4);
	{
		std::vector<std::wstring> arguments(args, args + count);
		if (length > 0 && length < MAX_PATH * 4)
			arguments[0] = exe;
		std::wstring line = ::Engine::restartCommandLine(arguments);
		STARTUPINFOW startup = {};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process = {};
		if (::CreateProcessW(length > 0 ? exe : nullptr, line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process))
		{
			::CloseHandle(process.hThread);
			::CloseHandle(process.hProcess);
		}
		else
			DEBUG_LOG(("Restart failed, error %u\n", static_cast<unsigned>(::GetLastError())));
	}
	::LocalFree(args);
}

Int APIENTRY WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance,
                      LPSTR lpCmdLine, Int nCmdShow )
{
	::SetUnhandledExceptionFilter( TheUnhandledExceptionFilter );
#ifdef _DEBUG
	signal( SIGABRT, onAbortSignal );	// see onAbortSignal
#endif
	WWDebug_Set_Thread_Name("Main");
	// The logic ticks on the main thread, so it carries both roles; this keeps
	// ASSERT_MAIN_THREAD and ASSERT_SIM_THREAD correct.
	::Engine::setThreadRole(::Engine::THREAD_ROLE_MAIN_AND_SIM);

	try {

#if defined(_MSC_VER)	// MinGW has no structured exception translation; the unhandled exception filter covers it
		_set_se_translator( DumpExceptionInfo ); // Hook that allows stack trace.
#endif
		//
		// there is something about checkin in and out the .dsp and .dsw files 
		// that blows the working directory information away on each of the 
		// developers machines so we're going to hack it for a while and set our
		// working directory to the directory with the .exe since that's not the
		// default in a DevStudio project
		//


		/// @todo remove this force set of working directory later
		Char buffer[ _MAX_PATH ];
		GetModuleFileName( nullptr, buffer, sizeof( buffer ) );
		Char *pEnd = buffer + strlen( buffer );
		while( pEnd != buffer ) 
		{
			if( *pEnd == '\\' ) 
			{
				*pEnd = 0;
				break;
			}
			pEnd--;
		}
		::SetCurrentDirectory(buffer);

		// -mehrfach: several instances side by side for parallel test runs (image comparison, start
		// tests). Skips the single-instance lock, and the logs in the exe folder (DebugLogFile*.txt,
		// EngineLog*.txt) get the per-instance prefix m<PID>_ so instances do not overwrite each other.
		// Read early because the logs are opened before the regular command line parsing.
		Bool isMultiInstance = FALSE;
		static char s_instancePrefix[32] = "";
		for (const char *p = lpCmdLine; p && *p; ++p) {
			if (_strnicmp(p, "-mehrfach", 9) == 0 && (p[9] == 0 || p[9] == ' ' || p[9] == '"')
				&& (p == lpCmdLine || p[-1] == ' ' || p[-1] == '"')) {
				isMultiInstance = TRUE;
				break;
			}
		}
		if (isMultiInstance) {
			// Debug.cpp names DebugLogFile*.txt itself (DebugInit runs before WinMain); only EngineLog here.
			WWFormat::Into(s_instancePrefix, "m{:d}_", ::GetCurrentProcessId());
		}

#if defined(_DEBUG) || defined(TRACY_ENABLE)
		// Also in the profile build (Tracy) so the lock statistics are readable.
		// Thread-safe engine log next to the old DEBUG_LOG. Opened only here: before, the working
		// directory is still the caller's (shortcut, debugger) and the file would end up elsewhere
		// than DebugLogFileD.txt. The ring buffer runs in every build anyway (crash report), even
		// without a file.
		{
			char engineLogName[64];	// -mehrfach: per-instance prefix (see above)
			WWFormat::Into(engineLogName, "{}{}{}", s_instancePrefix, gAppPrefix, isGameEditionZeroHour() ? "EngineLogZH.txt" : "EngineLog.txt");
			engineLogName[sizeof(engineLogName) - 1] = 0;
			::Engine::Log::openFile(engineLogName);
		}
		::Engine::Log::setMinLevel(::Engine::LOG_DEBUG);
#endif


		/*
		** Convert WinMain arguments to simple main argc and argv
		*/
		// Room for long runner command lines (-skirmish/-recordReplay/-crcLog ...).
		enum { MAX_COMMAND_LINE_ARGS = 128 };
		int argc = 1;
		char * argv[MAX_COMMAND_LINE_ARGS];
		argv[0] = nullptr;

		char *token;
		Bool isRunnerInstance = FALSE;	// -runner skips the single-instance lock (see below)
		Int fullscreenAreaArgs = 0;		// values of -fullscreenArea still to read
		token = nextParam(lpCmdLine, "\" ");
		while (argc < MAX_COMMAND_LINE_ARGS && token != nullptr) {
			argv[argc++] = trimToken(token);
			if (fullscreenAreaArgs == 2)
				ApplicationFullscreenAreaWidth = atoi(token);
			else if (fullscreenAreaArgs == 1)
				ApplicationFullscreenAreaHeight = atoi(token);
			if (fullscreenAreaArgs > 0)
				--fullscreenAreaArgs;
			if (stricmp(token,"-fullscreenArea")==0)
				fullscreenAreaArgs = 2;
			//added a preparse step for this flag because it affects window creation style
			if (stricmp(token,"-runner")==0)
				isRunnerInstance = TRUE;
			if (stricmp(token,"-win")==0)
				ApplicationIsWindowed=true;
			else if (stricmp(token,"-borderless")==0)
			{
				ApplicationIsWindowed=true;
				ApplicationIsBorderless=true;
			}
#ifdef _DEBUG
			//preparse step: install CrtAssertToCrashHook only on request, otherwise a CRT assert
			//shows the normal dialog
			else if (stricmp(token,"-ignoreAsserts")==0)
			{
#if defined(_MSC_VER)	// no CRT abort behaviour control in MinGW
				_set_abort_behavior(0, _WRITE_ABORT_MSG);	// abort() without dialog, see onAbortSignal
#endif
			}
			else if (stricmp(token,"-crashOnAssert")==0)
				_CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, CrtAssertToCrashHook);
#endif
			token = nextParam(nullptr, "\" ");
		}

		if (argc>2 && strcmp(argv[1],"-DX")==0) {  
			Int i;
			DEBUG_LOG(("\n--- DX STACK DUMP\n"));
			for (i=2; i<argc; i++) {
				Int pc;
				pc = 0;
				sscanf(argv[i], "%x",  &pc);
				char name[_MAX_PATH], file[_MAX_PATH];
				unsigned int line;
				unsigned int addr;
				GetFunctionDetails(reinterpret_cast<void*>(static_cast<uintptr_t>(static_cast<UnsignedInt>(pc))), name, sizeof(name), file, sizeof(file), &line, &addr);
				DEBUG_LOG(("0x%x - %s, %s, line %d address 0x%x\n", pc, name, file, line, addr));
			}
			DEBUG_LOG(("\n--- END OF DX STACK DUMP\n"));
			return 0;
		}

		#ifdef _DEBUG
			// Turn on Memory heap tracking
			int tmpFlag = _CrtSetDbgFlag( _CRTDBG_REPORT_FLAG );
			tmpFlag |= (_CRTDBG_LEAK_CHECK_DF|_CRTDBG_ALLOC_MEM_DF);
			tmpFlag &= ~_CRTDBG_CHECK_CRT_DF;
			_CrtSetDbgFlag( tmpFlag );
			// The CRT reports its leaks (malloc outside the memory pools, e.g. in DLLs) at process
			// exit only to the debugger. With GENERALS_CRT_LEAKFILE=<file> they also go to that file
			// (tools/lecksuche). The handle stays open until process exit; the report comes after
			// WinMain returns.
			{
				char crtLeakFile[_MAX_PATH];
				const DWORD len = GetEnvironmentVariableA( "GENERALS_CRT_LEAKFILE", crtLeakFile, sizeof( crtLeakFile ) );
				if( len > 0 && len < sizeof( crtLeakFile ) )
				{
					HANDLE h = CreateFileA( crtLeakFile, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
					if( h != INVALID_HANDLE_VALUE )
					{
						_CrtSetReportMode( _CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG );
						_CrtSetReportFile( _CRT_WARN, h );
					}
				}
			}
		#endif



		// install debug callbacks
	//	WWDebug_Install_Message_Handler(WWDebug_Message_Callback);
	//	WWDebug_Install_Assert_Handler(WWAssert_Callback);

		// Force "splash image" to be loaded from a file, not a resource so same exe can be used in different localizations.
#if defined _DEBUG

			// check both localized directory and root dir
		char filePath[_MAX_PATH];
		const char *fileName = "Install_Final.bmp";
		WWFormat::Into(filePath, "Data/{}/", GetGameLanguage().str());
		safe_strcat( filePath, fileName );
		FILE *fileImage = fopen(filePath, "r");
		if (fileImage) {
			fclose(fileImage);
			gLoadScreenBitmap = static_cast<HBITMAP>(LoadImage(hInstance, filePath, IMAGE_BITMAP, 0, 0, LR_SHARED|LR_LOADFROMFILE));
		}
		else {
			gLoadScreenBitmap = static_cast<HBITMAP>(LoadImage(hInstance, fileName, IMAGE_BITMAP, 0, 0, LR_SHARED|LR_LOADFROMFILE));
		}
#else

		// in release, the file only ever lives in the root dir
		gLoadScreenBitmap = (HBITMAP)LoadImage(hInstance, "Install_Final.bmp", IMAGE_BITMAP, 0, 0, LR_SHARED|LR_LOADFROMFILE);
#endif

		// register windows class and create application window
		if( initializeAppWindows( hInstance, nCmdShow, ApplicationIsWindowed) == false )
			return 0;

		if (gLoadScreenBitmap!=nullptr) {
			::DeleteObject(gLoadScreenBitmap);
			gLoadScreenBitmap = nullptr;
		}


		// BGC - initialize COM
	//	OleInitialize(NULL);

		// start the log
		DEBUG_INIT(DEBUG_FLAGS_DEFAULT);
		initMemoryManager();

 
		// Set up version info
		::Engine::context().version = NEW Version;
		TheVersion->setVersion(VERSION_MAJOR, VERSION_MINOR, VERSION_BUILDNUM, VERSION_LOCALBUILDNUM,
			AsciiString(VERSION_BUILDUSER), AsciiString(VERSION_BUILDLOC),
			AsciiString(__TIME__), AsciiString(__DATE__));
		TheVersion->setSuffix(AsciiString(VERSION_SUFFIX));

		//Create a mutex with a unique name to Generals in order to determine if
		//our app is already running.
		//WARNING: DO NOT use this number for any other application except Generals.
		// Runner instances (-runner, determinism tests/benchmarks) and -mehrfach skip the lock;
		// otherwise a test run would silently exit with 0 whenever any game with the same GUID runs.
		// Parallel runner recordings share the replay file 00000000.rep and must not run at once.
		const Bool skipMutex = isRunnerInstance || isMultiInstance;
		GeneralsMutex = skipMutex ? nullptr : CreateMutex(nullptr, FALSE, GENERALS_GUID);
		if (!skipMutex && GetLastError() == ERROR_ALREADY_EXISTS)
		{
			HWND ccwindow = FindWindow(GENERALS_GUID, nullptr);
			if (ccwindow)
			{
				SetForegroundWindow(ccwindow);
				ShowWindow(ccwindow, SW_RESTORE);
			}
			if (GeneralsMutex != nullptr)
			{
				CloseHandle(GeneralsMutex);
				GeneralsMutex = nullptr;
			}

			DEBUG_LOG(("Generals is already running...Bail!\n"));
			delete TheVersion;
			::Engine::context().version = nullptr;
			shutdownMemoryManager();
			DEBUG_SHUTDOWN();
			return 0;
		}
		DEBUG_LOG(("Create GeneralsMutex okay.\n"));

		DEBUG_LOG(("CRC message is %d\n", GameMessage::MSG_LOGIC_CRC));

		// run the game main loop
		GameMain(argc, argv);

		if (GameEngine_RestartRequested())
			startAgain();

		delete TheVersion;
		::Engine::context().version = nullptr;


	#ifdef MEMORYPOOL_DEBUG
		TheMemoryPoolFactory->debugMemoryReport(REPORT_POOLINFO | REPORT_POOL_OVERFLOW | REPORT_SIMPLE_LEAKS, 0, 0);
	#endif
	#if defined(_DEBUG)
		TheMemoryPoolFactory->memoryPoolUsageReport("AAAMemStats");
	#endif

		// close the log
		shutdownMemoryManager();
		DEBUG_SHUTDOWN();

		// BGC - shut down COM
	//	OleUninitialize();
	}	
	catch (...) 
	{ 
	
	}

	// The engine locks live until process exit (see Common/CriticalSection.h).

	return TheGameExitCode;	// 0 ok, 2 replay desync, 3 replay/map not found

}  // end WinMain

// CreateGameEngine ===========================================================
/** Create the Win32 game engine we're going to use */
//=============================================================================
GameEngine *CreateGameEngine( void )
{
	Win32GameEngine *engine;

	engine = NEW Win32GameEngine;
	//game engine may not have existed when app got focus so make sure it
	//knows about current focus state.
	engine->setIsActive(isWinMainActive);

	return engine;

}  // end CreateGameEngine
