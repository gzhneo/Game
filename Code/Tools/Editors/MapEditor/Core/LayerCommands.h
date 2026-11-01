/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// LayerCommands.h -- layers and groups of map objects. A layer is the name an object keeps in its
// property objectLayer and an area in its layer field, as the old WorldBuilder's LayersList stores
// them (empty: the default layer); the game ignores both. A group is a number in the editor's own
// property editorGroup: a click on one member picks them all.

#pragma once

#include "Core/History.h"
#include "Core/ObjectCommands.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace MapEditorCore
{

inline constexpr const char *LAYER_KEY = "objectLayer";
inline constexpr const char *GROUP_KEY = "editorGroup";

// The layer of an object; empty for the default layer.
[[nodiscard]] std::string objectLayer(const MapModel::MapObject &object);

struct LayerInfo
{
	std::string name;		// empty: the default layer
	int objects = 0;
	int areas = 0;
};

// The default layer first, then every layer an object or area names, sorted.
[[nodiscard]] std::vector<LayerInfo> mapLayers(const MapModel::Map &map);

// Objects and areas moved between layers, as one step.
class EditLayers : public Command
{
public:
	struct ObjectLayer
	{
		std::int32_t index = 0;
		std::optional<std::string> before;	// no value: the object had no layer property
		std::optional<std::string> after;
	};
	struct AreaLayer
	{
		std::int32_t index = 0;
		std::string before;
		std::string after;
	};

	EditLayers(std::string name, std::vector<ObjectLayer> objects, std::vector<AreaLayer> areas)
		: m_name(std::move(name)), m_objects(std::move(objects)), m_areas(std::move(areas)) {}

	virtual void apply(MapModel::Map &map) override { set(map, true); }
	virtual void revert(MapModel::Map &map) override { set(map, false); }
	virtual std::string name() const override { return m_name; }
	virtual Changes changes() const override;

private:
	void set(MapModel::Map &map, bool after) const;

	std::string m_name;
	std::vector<ObjectLayer> m_objects;
	std::vector<AreaLayer> m_areas;
};

// Moves the objects and areas into the layer (empty: default); null if none changes.
std::unique_ptr<EditLayers> moveToLayer(const MapModel::Map &map, const std::vector<std::int32_t> &objects,
	const std::vector<std::int32_t> &areas, const std::string &layer, std::string name = "Move to layer");

// Everything of layer from goes to layer to: renaming, merging, and deleting a layer (into the
// default one). Null if the layer is empty or the names are the same.
std::unique_ptr<EditLayers> mergeLayer(const MapModel::Map &map, const std::string &from, const std::string &to, std::string name);

// The objects of the layer.
[[nodiscard]] std::vector<std::int32_t> layerObjects(const MapModel::Map &map, const std::string &layer);

// The group of an object, 0 if none.
[[nodiscard]] std::int32_t objectGroup(const MapModel::MapObject &object);

// Puts the objects into a new group (or takes them out of theirs, ungroup); null if nothing changes.
std::unique_ptr<EditObjects> groupObjects(const MapModel::Map &map, const std::vector<std::int32_t> &objects);
std::unique_ptr<EditObjects> ungroupObjects(const MapModel::Map &map, const std::vector<std::int32_t> &objects);

// The objects with every other member of their groups added, in the order of the map.
[[nodiscard]] std::vector<std::int32_t> withGroups(const MapModel::Map &map, const std::vector<std::int32_t> &objects);

} // namespace MapEditorCore
