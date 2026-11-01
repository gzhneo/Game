/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// History.cpp -- the undo history.

#include "Core/History.h"

#include <cstdint>

namespace MapEditorCore
{

void Batch::apply(MapModel::Map &map)
{
	for (const std::unique_ptr<Command> &part : m_parts)
		part->apply(map);
}

void Batch::revert(MapModel::Map &map)
{
	for (auto it = m_parts.rbegin(); it != m_parts.rend(); ++it)
		(*it)->revert(map);
}

Changes Batch::changes() const
{
	Changes all;
	for (const std::unique_ptr<Command> &part : m_parts)
	{
		const Changes c = part->changes();
		all.reindexed = all.reindexed || c.reindexed;
		all.rebuild = all.rebuild || c.rebuild;
		all.links = all.links || c.links;
		all.areas = all.areas || c.areas;
		if (c.terrain || c.textures || c.passability)
			all.terrainRect = c.terrainRect;
		all.terrain = all.terrain || c.terrain;
		all.textures = all.textures || c.textures;
		all.textureClasses = all.textureClasses || c.textureClasses;
		all.passability = all.passability || c.passability;
		all.settings = all.settings || c.settings;
		all.boundaries = all.boundaries || c.boundaries;
		all.size = all.size || c.size;
		all.sides = all.sides || c.sides;
		all.scripts = all.scripts || c.scripts;
		all.objects = c.objects;
	}
	return all;
}

void History::execute(std::unique_ptr<Command> command, MapModel::Map &map)
{
	command->apply(map);
	push(std::move(command));
}

void History::record(std::unique_ptr<Command> command)
{
	push(std::move(command));
}

void History::push(std::unique_ptr<Command> command)
{
	if (m_savedPosition > m_position)
		m_savedPosition = SIZE_MAX;	// the saved state was undone and is now overwritten
	m_commands.resize(m_position);
	// Merging into the last command is only allowed while that one is not the saved state.
	if (!m_commands.empty() && m_savedPosition != m_position && m_commands.back()->mergeWith(*command))
		return;
	m_commands.push_back(std::move(command));
	m_position = m_commands.size();
}

Changes History::undo(MapModel::Map &map)
{
	if (!canUndo())
		return {};
	Command &command = *m_commands[--m_position];
	command.revert(map);
	return command.changes();
}

Changes History::redo(MapModel::Map &map)
{
	if (!canRedo())
		return {};
	Command &command = *m_commands[m_position++];
	command.apply(map);
	return command.changes();
}

void History::clear()
{
	m_commands.clear();
	m_position = 0;
	m_savedPosition = 0;
}

} // namespace MapEditorCore
