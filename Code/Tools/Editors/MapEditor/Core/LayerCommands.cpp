/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// LayerCommands.cpp -- see LayerCommands.h.

#include "Core/LayerCommands.h"

#include <algorithm>
#include <map>
#include <set>

namespace MapEditorCore
{

namespace
{

bool validObject(const MapModel::Map &map, std::int32_t index)
{
	return map.objects && index >= 0 && static_cast<std::size_t>(index) < map.objects->size();
}

bool validArea(const MapModel::Map &map, std::int32_t index)
{
	return map.polygons && index >= 0 && static_cast<std::size_t>(index) < map.polygons->size();
}

// The property value an object in the layer gets: "" for the default layer only if it had the
// property already, as the old editor writes it.
std::optional<std::string> layerValue(const MapModel::MapObject &object, const std::string &layer)
{
	if (layer.empty() && !object.properties.find(LAYER_KEY))
		return std::nullopt;
	return layer;
}

} // namespace

std::string objectLayer(const MapModel::MapObject &object)
{
	return object.properties.getAscii(LAYER_KEY).value_or("");
}

std::vector<LayerInfo> mapLayers(const MapModel::Map &map)
{
	std::map<std::string, LayerInfo> found;
	found[""] = LayerInfo{};
	if (map.objects)
	{
		for (const MapModel::MapObject &o : *map.objects)
		{
			LayerInfo &info = found[objectLayer(o)];
			++info.objects;
		}
	}
	if (map.polygons)
	{
		for (const MapModel::PolygonArea &a : *map.polygons)
			++found[a.layer].areas;
	}
	std::vector<LayerInfo> layers;
	for (auto &[name, info] : found)
	{
		info.name = name;
		layers.push_back(info);
	}
	return layers;
}

Changes EditLayers::changes() const
{
	Changes c;
	for (const ObjectLayer &o : m_objects)
		c.objects.push_back(o.index);
	c.areas = !m_areas.empty();
	// Hidden layers leave their objects out of the view, so it is built anew.
	c.rebuild = true;
	return c;
}

void EditLayers::set(MapModel::Map &map, bool after) const
{
	for (const ObjectLayer &o : m_objects)
	{
		if (!validObject(map, o.index))
			continue;
		MapModel::PropertyMap &props = (*map.objects)[static_cast<std::size_t>(o.index)].properties;
		const std::optional<std::string> &value = after ? o.after : o.before;
		if (value)
			props.set(LAYER_KEY, MapModel::PropertyValue(*value));
		else
			props.erase(LAYER_KEY);
	}
	for (const AreaLayer &a : m_areas)
	{
		if (validArea(map, a.index))
			(*map.polygons)[static_cast<std::size_t>(a.index)].layer = after ? a.after : a.before;
	}
}

std::unique_ptr<EditLayers> moveToLayer(const MapModel::Map &map, const std::vector<std::int32_t> &objects,
	const std::vector<std::int32_t> &areas, const std::string &layer, std::string name)
{
	std::vector<EditLayers::ObjectLayer> objectEdits;
	std::set<std::int32_t> seen;
	for (std::int32_t index : objects)
	{
		if (!validObject(map, index) || !seen.insert(index).second)
			continue;
		const MapModel::MapObject &o = (*map.objects)[static_cast<std::size_t>(index)];
		if (objectLayer(o) == layer)
			continue;
		std::optional<std::string> before;
		if (o.properties.find(LAYER_KEY))
			before = objectLayer(o);
		objectEdits.push_back({index, before, layerValue(o, layer)});
	}
	std::vector<EditLayers::AreaLayer> areaEdits;
	for (std::int32_t index : areas)
	{
		if (validArea(map, index) && (*map.polygons)[static_cast<std::size_t>(index)].layer != layer)
			areaEdits.push_back({index, (*map.polygons)[static_cast<std::size_t>(index)].layer, layer});
	}
	if (objectEdits.empty() && areaEdits.empty())
		return nullptr;
	return std::make_unique<EditLayers>(std::move(name), std::move(objectEdits), std::move(areaEdits));
}

std::unique_ptr<EditLayers> mergeLayer(const MapModel::Map &map, const std::string &from, const std::string &to, std::string name)
{
	if (from == to)
		return nullptr;
	std::vector<std::int32_t> areas;
	for (std::size_t i = 0; map.polygons && i < map.polygons->size(); ++i)
	{
		if ((*map.polygons)[i].layer == from)
			areas.push_back(static_cast<std::int32_t>(i));
	}
	return moveToLayer(map, layerObjects(map, from), areas, to, std::move(name));
}

std::vector<std::int32_t> layerObjects(const MapModel::Map &map, const std::string &layer)
{
	std::vector<std::int32_t> found;
	for (std::size_t i = 0; map.objects && i < map.objects->size(); ++i)
	{
		if (objectLayer((*map.objects)[i]) == layer)
			found.push_back(static_cast<std::int32_t>(i));
	}
	return found;
}

std::int32_t objectGroup(const MapModel::MapObject &object)
{
	return std::max<std::int32_t>(0, object.properties.getInt(GROUP_KEY).value_or(0));
}

std::unique_ptr<EditObjects> groupObjects(const MapModel::Map &map, const std::vector<std::int32_t> &objects)
{
	if (!map.objects)
		return nullptr;
	std::int32_t next = 1;
	for (const MapModel::MapObject &o : *map.objects)
		next = std::max(next, objectGroup(o) + 1);
	std::vector<EditObjects::Edit> edits;
	std::set<std::int32_t> seen;
	for (std::int32_t index : objects)
	{
		if (!validObject(map, index) || !seen.insert(index).second)
			continue;
		EditObjects::Edit edit{index, (*map.objects)[static_cast<std::size_t>(index)], (*map.objects)[static_cast<std::size_t>(index)]};
		edit.after.properties.set(GROUP_KEY, MapModel::PropertyValue(next));
		edits.push_back(std::move(edit));
	}
	if (edits.size() < 2)
		return nullptr;
	return std::make_unique<EditObjects>("Group objects", std::move(edits));
}

std::unique_ptr<EditObjects> ungroupObjects(const MapModel::Map &map, const std::vector<std::int32_t> &objects)
{
	std::vector<EditObjects::Edit> edits;
	std::set<std::int32_t> seen;
	for (std::int32_t index : objects)
	{
		if (!validObject(map, index) || !seen.insert(index).second)
			continue;
		const MapModel::MapObject &o = (*map.objects)[static_cast<std::size_t>(index)];
		if (!o.properties.find(GROUP_KEY))
			continue;
		EditObjects::Edit edit{index, o, o};
		edit.after.properties.erase(GROUP_KEY);
		edits.push_back(std::move(edit));
	}
	if (edits.empty())
		return nullptr;
	return std::make_unique<EditObjects>("Ungroup objects", std::move(edits));
}

std::vector<std::int32_t> withGroups(const MapModel::Map &map, const std::vector<std::int32_t> &objects)
{
	if (!map.objects)
		return objects;
	std::set<std::int32_t> groups;
	std::set<std::int32_t> wanted;
	for (std::int32_t index : objects)
	{
		if (!validObject(map, index))
			continue;
		wanted.insert(index);
		if (const std::int32_t g = objectGroup((*map.objects)[static_cast<std::size_t>(index)]))
			groups.insert(g);
	}
	std::vector<std::int32_t> result;
	for (std::size_t i = 0; i < map.objects->size(); ++i)
	{
		const std::int32_t index = static_cast<std::int32_t>(i);
		if (wanted.count(index) || groups.count(objectGroup((*map.objects)[i])))
			result.push_back(index);
	}
	return result;
}

} // namespace MapEditorCore
