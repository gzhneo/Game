/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ScriptCatalog.h -- what the editor knows about scripts beyond the map: the engine's conditions and
// actions, the names of number-valued parameters, the values offered for a parameter and the names
// that exist for the script check (from the map and the game data). Without the engine the lists are
// empty and nothing is checked against the game data.

#pragma once

#include "Core/ScriptCommands.h"
#include "Model/Map.h"

#include <QString>
#include <QStringList>

#include <cstdint>

namespace MapEditorApp
{

class ScriptCatalog
{
public:
	// The engine's conditions and actions, read once; empty without the engine.
	[[nodiscard]] const MapEditorCore::ScriptTemplates &templates();
	[[nodiscard]] MapEditorCore::ValueNames valueNames();
	[[nodiscard]] QString callText(const MapModel::ScriptCall &call, bool action);
	// Values offered for a parameter type: from the map (null: none), its scripts and the game data.
	[[nodiscard]] QStringList parameterChoices(const MapModel::Map *mapOrNull, std::int32_t type);
	// Names that exist, for the warnings of the scripts.
	[[nodiscard]] MapEditorCore::ScriptCheck check(const MapModel::Map *mapOrNull);

private:
	MapEditorCore::ScriptTemplates m_templates;
};

} // namespace MapEditorApp
