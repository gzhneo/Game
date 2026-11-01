/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MapSettingsCommands.cpp -- see MapSettingsCommands.h.

#include "Core/MapSettingsCommands.h"

#include <algorithm>
#include <cmath>

namespace MapEditorCore
{

namespace
{

constexpr float PI = 3.14159265358979f;
constexpr float CELL = 10.0f;

} // namespace

MapSettings mapSettings(const MapModel::Map &map)
{
	MapSettings s;
	s.worldInfo = map.worldInfo;
	s.lighting = map.lighting;
	if (map.heightMap)
		s.boundaries = map.heightMap->boundaries;
	return s;
}

Changes EditMapSettings::changes() const
{
	Changes c;
	c.settings = m_before.worldInfo != m_after.worldInfo || m_before.lighting != m_after.lighting;
	c.boundaries = m_before.boundaries != m_after.boundaries;
	return c;
}

bool EditMapSettings::mergeWith(const Command &next)
{
	const auto *other = dynamic_cast<const EditMapSettings *>(&next);
	if (!other || other->m_name != m_name)
		return false;
	m_after = other->m_after;
	return true;
}

void EditMapSettings::set(MapModel::Map &map, const MapSettings &settings) const
{
	map.worldInfo = settings.worldInfo;
	map.lighting = settings.lighting;
	if (map.heightMap)
		map.heightMap->boundaries = settings.boundaries;
}

std::unique_ptr<EditMapSettings> settingsStep(const MapModel::Map &map, const MapSettings &before, std::string name)
{
	MapSettings now = mapSettings(map);
	if (now == before)
		return nullptr;
	return std::make_unique<EditMapSettings>(std::move(name), before, std::move(now));
}

std::array<float, 3> lightDirection(float azimuth, float elevation)
{
	const float a = azimuth / 180.0f * PI;
	const float e = PI / 2.0f + elevation / 180.0f * PI;
	return {std::sin(e) * std::cos(a), std::sin(e) * std::sin(a), std::cos(e)};
}

void lightAngles(const std::array<float, 3> &direction, float &azimuth, float &elevation)
{
	const float length = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
	const float z = length > 0.0f ? std::clamp(direction[2] / length, -1.0f, 1.0f) : -1.0f;
	azimuth = std::atan2(direction[1], direction[0]) * 180.0f / PI;
	elevation = (std::acos(z) - PI / 2.0f) * 180.0f / PI;
}

BoundaryHit boundaryAt(const std::vector<MapModel::Point2> &boundaries, float x, float y, float tolerance)
{
	BoundaryHit best;
	float bestDistance = tolerance;
	for (std::size_t i = 0; i < boundaries.size(); ++i)
	{
		const float bx = static_cast<float>(boundaries[i].x) * CELL;
		const float by = static_cast<float>(boundaries[i].y) * CELL;
		const float corner = std::hypot(x - bx, y - by);
		const float top = y >= -tolerance && x >= 0.0f && x <= bx ? std::abs(y - by) : 1e9f;
		const float right = x >= -tolerance && y >= 0.0f && y <= by ? std::abs(x - bx) : 1e9f;
		// The corner wins over the edges that meet there.
		if (corner <= bestDistance)
		{
			bestDistance = corner;
			best = {static_cast<int>(i), BoundaryHit::Part::Corner};
			continue;
		}
		if (top < bestDistance)
		{
			bestDistance = top;
			best = {static_cast<int>(i), BoundaryHit::Part::Top};
		}
		if (right < bestDistance)
		{
			bestDistance = right;
			best = {static_cast<int>(i), BoundaryHit::Part::Right};
		}
	}
	return best;
}

} // namespace MapEditorCore
