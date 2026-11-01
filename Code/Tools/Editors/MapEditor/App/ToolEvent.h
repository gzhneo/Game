/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ToolEvent.h -- what the 3D view and the overview report to a tool (areas) while it is active: the
// left button on the ground in world units, and how far a few pixels reach there, so the tool can
// hit points and edges in either view.

#pragma once

namespace MapEditorApp
{

struct ToolEvent
{
	enum class Kind
	{
		Press,
		Move,
		Release,
		DoubleClick
	};
	Kind kind = Kind::Press;
	float x = 0.0f;
	float y = 0.0f;
	float tolerance = 0.0f;		// world units of about eight pixels at this place
	bool ctrl = false;
	bool shift = false;
	bool alt = false;
};

} // namespace MapEditorApp
