/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MapSettingsCommands.h -- the map's settings as the old WorldBuilder edits them: world info (name,
// weather, compression; MapSettings), global lighting of each time of day (GlobalLightOptions), the
// shadow colour (ShadowOptions), and the playable areas (BorderTool).

#pragma once

#include "Core/History.h"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace MapEditorCore
{

struct MapSettings
{
	std::optional<MapModel::PropertyMap> worldInfo;
	std::optional<MapModel::GlobalLighting> lighting;
	std::vector<MapModel::Point2> boundaries;

	bool operator==(const MapSettings &) const = default;
};

[[nodiscard]] MapSettings mapSettings(const MapModel::Map &map);

// The settings before and after, as one step. Steps of the same name that follow each other fold into
// one (dragging a value in a field).
class EditMapSettings : public Command
{
public:
	EditMapSettings(std::string name, MapSettings before, MapSettings after)
		: m_name(std::move(name)), m_before(std::move(before)), m_after(std::move(after)) {}

	virtual void apply(MapModel::Map &map) override { set(map, m_after); }
	virtual void revert(MapModel::Map &map) override { set(map, m_before); }
	virtual std::string name() const override { return m_name; }
	virtual Changes changes() const override;
	virtual bool mergeWith(const Command &next) override;

private:
	void set(MapModel::Map &map, const MapSettings &settings) const;

	std::string m_name;
	MapSettings m_before;
	MapSettings m_after;
};

// The step from the settings before to those of the map now; null if they are the same.
std::unique_ptr<EditMapSettings> settingsStep(const MapModel::Map &map, const MapSettings &before, std::string name);

// Direction of a light (x, y, z; pointing from the light) from its azimuth and elevation in degrees,
// as the old light dialog computes it, and back.
[[nodiscard]] std::array<float, 3> lightDirection(float azimuth, float elevation);
void lightAngles(const std::array<float, 3> &direction, float &azimuth, float &elevation);

// A playable area near a world position: its top right corner, its top edge or its right edge
// (every area starts at the map's origin). index -1 if none is within tolerance.
struct BoundaryHit
{
	enum class Part
	{
		None,
		Corner,
		Top,
		Right
	};
	int index = -1;
	Part part = Part::None;
};
[[nodiscard]] BoundaryHit boundaryAt(const std::vector<MapModel::Point2> &boundaries, float x, float y, float tolerance);

} // namespace MapEditorCore
