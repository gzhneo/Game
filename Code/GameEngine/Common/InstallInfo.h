/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FILE: InstallInfo.h ///////////////////////////////////////////////////////////////////////////
// Installation data. The game does not read the registry; language and version come from
// Options.ini, the archive names or fixed values (see InstallInfo.cpp).
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __INSTALLINFO_H_
#define __INSTALLINFO_H_

#include <Common/AsciiString.h>

/// Language of the installed game data, e.g. "english", "german", "german2" (selects Data\<language>\...).
AsciiString GetGameLanguage( void );

/// Game version in the format of the registry value "Version": upper 16 bits major, lower 16 bits minor.
UnsignedInt GetGameVersion( void );

#endif // __INSTALLINFO_H_
