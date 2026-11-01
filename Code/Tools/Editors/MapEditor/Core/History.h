/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// History.h -- editing commands and the undo history of an open map.
//
// Every change to the map model is a Command that can apply and revert itself. The history holds
// the commands in order; undo reverts the last one, redo applies it again. A command reports which
// parts of the map it touched (Changes), so the 3D view and the panels update only those. The core
// has no Qt and no engine and is tested on its own (Code/Tests/MapEditorTests).

#pragma once

#include "Model/Map.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace MapEditorCore
{

// What a command changed: indices into Map::objects. reindexed: objects were removed or inserted,
// so every index after the first change moved; views rebuild their objects. The indices then name
// the objects that are there again after the step (restored ones), empty if they are gone.
// rebuild: the objects keep their places, but what draws them changed (team colour, damage state,
// model), so views rebuild them. links: waypoint links changed (the objects stay where they are).
// areas: trigger or water areas changed. terrain: heights changed within terrainRect (cells x0, y0,
// x1, y1, border included). textures: tiles changed there; textureClasses: the map's texture classes too
// (views reload their terrain textures).
struct Changes
{
	std::vector<std::int32_t> objects;
	bool reindexed = false;
	bool rebuild = false;
	bool links = false;
	bool areas = false;
	bool terrain = false;
	bool textures = false;
	bool textureClasses = false;
	bool passability = false;			// cliff flags (which cells units cannot cross)
	bool settings = false;				// world info, lighting (the view loads the map again)
	bool boundaries = false;			// playable areas
	bool size = false;					// the map's size: everything is laid out anew
	bool sides = false;					// players and teams
	bool scripts = false;				// the players' scripts
	std::array<int, 4> terrainRect{};
};

class Command
{
public:
	virtual ~Command() = default;
	virtual void apply(MapModel::Map &map) = 0;
	virtual void revert(MapModel::Map &map) = 0;
	// English text for the menu ("Undo: <name>"); the editor translates it.
	virtual std::string name() const = 0;
	virtual Changes changes() const = 0;
	// Folds a following command of the same kind into this one (one undo step); false keeps both.
	virtual bool mergeWith(const Command &) { return false; }
};

// Several commands as one step: applied in order, reverted backwards. The changes are those of all
// parts; the objects are those of the last part (after an insert: the new objects).
class Batch : public Command
{
public:
	Batch(std::string name, std::vector<std::unique_ptr<Command>> parts) : m_name(std::move(name)), m_parts(std::move(parts)) {}

	virtual void apply(MapModel::Map &map) override;
	virtual void revert(MapModel::Map &map) override;
	virtual std::string name() const override { return m_name; }
	virtual Changes changes() const override;

private:
	std::string m_name;
	std::vector<std::unique_ptr<Command>> m_parts;
};

class History
{
public:
	// Applies the command and records it; drops everything that could be redone.
	void execute(std::unique_ptr<Command> command, MapModel::Map &map);
	// Records a command whose effect is already in the map (the result of a drag, say).
	void record(std::unique_ptr<Command> command);

	// Revert or apply again; return the changes, empty if there was nothing to do.
	Changes undo(MapModel::Map &map);
	Changes redo(MapModel::Map &map);

	bool canUndo() const { return m_position > 0; }
	// Steps that can be undone.
	std::size_t undoCount() const { return m_position; }
	// Names of all recorded steps, oldest first; the first undoCount() of them are done.
	std::vector<std::string> names() const
	{
		std::vector<std::string> out;
		out.reserve(m_commands.size());
		for (const std::unique_ptr<Command> &c : m_commands)
			out.push_back(c->name());
		return out;
	}
	bool canRedo() const { return m_position < m_commands.size(); }
	std::string undoName() const { return canUndo() ? m_commands[m_position - 1]->name() : std::string(); }
	std::string redoName() const { return canRedo() ? m_commands[m_position]->name() : std::string(); }

	// Modified since the last save (undoing back to the saved state counts as unmodified).
	bool isModified() const { return m_position != m_savedPosition; }
	// Number of done steps at the last save; SIZE_MAX when that state can no longer be reached.
	std::size_t savedPosition() const { return m_savedPosition; }
	void markSaved() { m_savedPosition = m_position; }
	// The map differs from its file although nothing was done yet (a recovered autosave).
	void markUnsaved() { m_savedPosition = SIZE_MAX; }
	void clear();

private:
	void push(std::unique_ptr<Command> command);

	std::vector<std::unique_ptr<Command>> m_commands;
	std::size_t m_position = 0;
	// Position of the saved state; SIZE_MAX when that state can no longer be reached.
	std::size_t m_savedPosition = 0;
};

} // namespace MapEditorCore
