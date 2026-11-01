/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Fixed version values. EA's build servers generated this header and incremented the build
// number on every build; that tooling is not part of the source release.
#define VERSION_MAJOR 2
#define VERSION_MINOR 0
// Pre-release suffix after major.minor ("2.0-alpha-1"); empty for a final release.
#define VERSION_SUFFIX "alpha-1"
// The same version as text for the version resource of RTS.RC (the resource compiler cannot join strings).
#define VERSION_TEXT "2.0-alpha-1"
#define VERSION_BUILDNUM 0
#ifndef RC_INVOKED	// RTS.RC includes this header; the resource compiler knows no C++
constexpr auto VERSION_LOCALBUILDNUM = 0;
#endif
#define VERSION_BUILDUSER "modernization"
#define VERSION_BUILDLOC "local-build"
