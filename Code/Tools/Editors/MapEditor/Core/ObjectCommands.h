/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ObjectCommands.h -- commands on map objects, and the selection.

#pragma once

#include "Core/History.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace MapEditorCore
{

// Selected map objects (indices into Map::objects).
struct Selection
{
	std::vector<std::int32_t> objects;

	bool empty() const { return objects.empty(); }
	bool contains(std::int32_t index) const { return std::find(objects.begin(), objects.end(), index) != objects.end(); }
	void clear() { objects.clear(); }
	void set(std::int32_t index) { objects.assign(1, index); }
	void add(std::int32_t index)
	{
		if (!contains(index))
			objects.push_back(index);
	}
	// Adds the object, or removes it if it is selected already.
	void toggle(std::int32_t index)
	{
		const auto it = std::find(objects.begin(), objects.end(), index);
		if (it == objects.end())
			objects.push_back(index);
		else
			objects.erase(it);
	}
};

// Moves objects in the ground plane; the height above the ground stays. One drag is one command.
class MoveObjects : public Command
{
public:
	struct Move
	{
		std::int32_t index = 0;
		float fromX = 0.0f, fromY = 0.0f;
		float toX = 0.0f, toY = 0.0f;
	};

	explicit MoveObjects(std::vector<Move> moves) : m_moves(std::move(moves)) {}

	virtual void apply(MapModel::Map &map) override;
	virtual void revert(MapModel::Map &map) override;
	virtual std::string name() const override;
	virtual Changes changes() const override;

	const std::vector<Move> &moves() const { return m_moves; }

private:
	std::vector<Move> m_moves;
};

// Replaces objects by edited copies (position, height, angle, properties); the command keeps both
// states. name is the English menu text ("Change object", "Rotate object", ...). rebuildView: the
// change alters how the objects look (properties), see Changes::rebuild.
class EditObjects : public Command
{
public:
	struct Edit
	{
		std::int32_t index = 0;
		MapModel::MapObject before;
		MapModel::MapObject after;
	};

	EditObjects(std::string name, std::vector<Edit> edits, bool rebuildView = false)
		: m_name(std::move(name)), m_edits(std::move(edits)), m_rebuildView(rebuildView) {}

	virtual void apply(MapModel::Map &map) override;
	virtual void revert(MapModel::Map &map) override;
	virtual std::string name() const override { return m_name; }
	virtual Changes changes() const override;

private:
	std::string m_name;
	std::vector<Edit> m_edits;
	bool m_rebuildView = false;
};

// Removes objects; undo puts them back at their old places. Links of removed waypoints go with them.
class DeleteObjects : public Command
{
public:
	// indices into Map::objects, in any order; invalid ones are ignored. name: menu text ("Cut"
	// for a cut); empty gives "Delete object(s)".
	DeleteObjects(const MapModel::Map &map, std::vector<std::int32_t> indices, std::string name = {});

	virtual void apply(MapModel::Map &map) override;
	virtual void revert(MapModel::Map &map) override;
	virtual std::string name() const override;
	virtual Changes changes() const override;

	std::size_t count() const { return m_removed.size(); }

private:
	struct Removed
	{
		std::int32_t index;
		MapModel::MapObject object;
	};
	std::vector<Removed> m_removed;				// ascending by index
	std::vector<MapModel::WaypointLink> m_links;	// removed links, in their old order
	std::vector<std::size_t> m_linkPositions;	// their positions in the old link list
	bool m_applied = false;
	std::string m_name;
};

// Copied objects, independent of the map they came from.
struct Clipboard
{
	std::vector<MapModel::MapObject> objects;
	std::vector<MapModel::WaypointLink> links;	// between copied waypoints, by their old IDs

	bool empty() const { return objects.empty(); }
};

[[nodiscard]] Clipboard copyObjects(const MapModel::Map &map, const std::vector<std::int32_t> &indices);

// The clipboard as bytes for the system clipboard: the chunk data of a map holding only these
// objects and links (MapIO), so copies go between editor windows and survive a restart. Reading
// gives nothing for data that is no such map or has no objects.
[[nodiscard]] std::vector<std::uint8_t> clipboardToBytes(const Clipboard &clipboard);
[[nodiscard]] std::optional<Clipboard> clipboardFromBytes(std::span<const std::uint8_t> bytes);

// Appends objects (and waypoint links) at the end of the map; undo removes them again.
class InsertObjects : public Command
{
public:
	InsertObjects(std::string name, std::vector<MapModel::MapObject> objects, std::vector<MapModel::WaypointLink> links)
		: m_name(std::move(name)), m_objects(std::move(objects)), m_links(std::move(links)) {}

	virtual void apply(MapModel::Map &map) override;
	virtual void revert(MapModel::Map &map) override;
	virtual std::string name() const override { return m_name; }
	virtual Changes changes() const override;

	const std::vector<MapModel::MapObject> &objects() const { return m_objects; }

private:
	std::string m_name;
	std::vector<MapModel::MapObject> m_objects;
	std::vector<MapModel::WaypointLink> m_links;
	std::size_t m_first = 0;
	bool m_applied = false;
	bool m_createdObjects = false;
	bool m_createdLinks = false;
};

// The objects of the clipboard made ready for this map: moved by (dx, dy), a fresh uniqueID
// ("<template> <n>" as the old WorldBuilder numbers them), no script name (objectName), and
// waypoints with new IDs, unique names ("Waypoint <id>") and their links among themselves.
std::unique_ptr<InsertObjects> pasteObjects(const MapModel::Map &map, const Clipboard &clipboard, float dx, float dy,
	std::string name = "Paste");

} // namespace MapEditorCore
