/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Win32Monitors.cpp -- see Win32Monitors.h.

#include "Win32Device/Common/Win32Monitors.h"

#include <vector>

namespace
{
	struct MonitorEntry
	{
		HMONITOR handle;
		MONITORINFOEXW info;
	};

	BOOL CALLBACK collectMonitor( HMONITOR monitor, HDC, LPRECT, LPARAM data )
	{
		MonitorEntry entry;
		entry.handle = monitor;
		ZeroMemory( &entry.info, sizeof( entry.info ) );
		entry.info.cbSize = sizeof( entry.info );
		if( GetMonitorInfoW( monitor, &entry.info ) )
			reinterpret_cast<std::vector<MonitorEntry> *>( data )->push_back( entry );
		return TRUE;
	}

	/// The primary monitor first, then the others in the order Windows lists them.
	std::vector<MonitorEntry> monitors( void )
	{
		std::vector<MonitorEntry> list;
		EnumDisplayMonitors( nullptr, nullptr, collectMonitor, reinterpret_cast<LPARAM>( &list ) );
		for( size_t i = 1; i < list.size(); ++i )
		{
			if( list[i].info.dwFlags & MONITORINFOF_PRIMARY )
			{
				const MonitorEntry primary = list[i];
				list.erase( list.begin() + static_cast<std::ptrdiff_t>( i ) );
				list.insert( list.begin(), primary );
				break;
			}
		}
		return list;
	}

	AsciiString deviceNameOf( const MONITORINFOEXW &info )
	{
		AsciiString name;
		for( const WCHAR *c = info.szDevice; *c != 0; ++c )
			name.concat( ( *c > 0 && *c < 128 ) ? static_cast<char>( *c ) : '?' );
		return name;
	}
}

Bool PlaceWindowOnMonitor( HWND window, Int index, const AsciiString &deviceName, Bool coverMonitor )
{
	if( window == nullptr )
		return FALSE;
	const std::vector<MonitorEntry> list = monitors();
	const MonitorEntry *target = nullptr;
	if( index >= 1 && index <= static_cast<Int>( list.size() ) )
		target = &list[static_cast<size_t>( index - 1 )];
	for( size_t i = 0; target == nullptr && deviceName.isNotEmpty() && i < list.size(); ++i )
	{
		if( deviceNameOf( list[i].info ).compareNoCase( deviceName ) == 0 )
			target = &list[i];
	}
	if( target == nullptr )
		return FALSE;
	if( MonitorFromWindow( window, MONITOR_DEFAULTTONULL ) == target->handle )
		return TRUE;
	if( coverMonitor )
	{
		const RECT &r = target->info.rcMonitor;
		return SetWindowPos( window, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE ) != 0;
	}
	RECT current;
	if( !GetWindowRect( window, &current ) )
		return FALSE;
	const RECT &work = target->info.rcWork;
	const Int width = current.right - current.left;
	const Int height = current.bottom - current.top;
	Int left = work.left + ( ( work.right - work.left ) - width ) / 2;
	Int top = work.top + ( ( work.bottom - work.top ) - height ) / 2;
	if( left < work.left )
		left = work.left;
	if( top < work.top )
		top = work.top;
	return SetWindowPos( window, nullptr, left, top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE ) != 0;
}

Bool WindowMonitorSize( HWND window, Int &width, Int &height )
{
	MONITORINFO info;
	ZeroMemory( &info, sizeof( info ) );
	info.cbSize = sizeof( info );
	HMONITOR monitor = MonitorFromWindow( window, MONITOR_DEFAULTTOPRIMARY );
	if( monitor == nullptr || !GetMonitorInfo( monitor, &info ) )
		return FALSE;
	width = info.rcMonitor.right - info.rcMonitor.left;
	height = info.rcMonitor.bottom - info.rcMonitor.top;
	return width > 0 && height > 0;
}

AsciiString WindowMonitorName( HWND window )
{
	MONITORINFOEXW info;
	ZeroMemory( &info, sizeof( info ) );
	info.cbSize = sizeof( info );
	HMONITOR monitor = MonitorFromWindow( window, MONITOR_DEFAULTTONULL );
	if( monitor == nullptr || !GetMonitorInfoW( monitor, &info ) )
		return AsciiString::TheEmptyString;
	return deviceNameOf( info );
}

UnsignedInt WindowDpi( HWND window )
{
	// GetDpiForWindow exists from Windows 10 1607 on; looked up so older headers and systems work
	typedef UINT (WINAPI *GetDpiForWindowProc)( HWND );
	static GetDpiForWindowProc s_getDpi = reinterpret_cast<GetDpiForWindowProc>(
		reinterpret_cast<void *>( GetProcAddress( GetModuleHandleW( L"user32.dll" ), "GetDpiForWindow" ) ) );
	const UINT dpi = ( s_getDpi != nullptr && window != nullptr ) ? s_getDpi( window ) : 0;
	return dpi != 0 ? dpi : 96;
}
