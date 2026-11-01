/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// OwnedSubsystem.h -- an owner (GameClient, GameLogic) holds the subsystems it creates in
// std::unique_ptr; the global names (TheDisplay, TheScriptEngine, ...) point to them.

#pragma once

#include <memory>

/// Takes `created` into `owner`; the result goes into the global name.
template <typename T, typename U>
T *adoptSubsystem( std::unique_ptr<T> &owner, U *created )
{
	owner.reset(created);
	return owner.get();
}

/// Frees an owned subsystem and then clears the global name, if it points to it: an owner that was
/// never initialised (a test's stand-in) leaves the subsystems of others alone. The name stays valid
/// while the subsystem is destroyed, as its destructor may still use it.
template <typename T, typename V>
void destroySubsystem( std::unique_ptr<T> &owner, V *&global )
{
	const bool named = owner && global == owner.get();
	owner.reset();
	if (named)
		global = nullptr;
}
