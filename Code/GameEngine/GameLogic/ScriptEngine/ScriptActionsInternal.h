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

// ScriptActionsInternal.h -- what the ScriptActions*.cpp files share and nobody else needs.

#pragma once

class Object;

// Kind of hacky, but we need to dance on the guts of the terrain.
extern void oversizeTheTerrain(Int amount);
extern void doSkyBoxSet(Bool startDraw);	//hack to avoid including globaldata here.

/// After a change of controlling player or team: upgrades and indicator color of the object.
void updateTeamAndPlayerStuff( Object *obj, void *userData );

#define REALLY_FAR	(100000 * MAP_XY_FACTOR)
