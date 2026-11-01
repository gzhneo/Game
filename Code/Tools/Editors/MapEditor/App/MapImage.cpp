/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MapImage.cpp -- top-down picture of a map drawn from the map model alone.

#include "App/Common.h"
#include "App/MapImage.h"

#include <QPainter>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace MapEditorApp
{

namespace
{

// World units per height map cell (MAP_XY_FACTOR) and per height step (MAP_HEIGHT_SCALE).
constexpr float HEIGHT_STEP = CELL / 16.0f;

// Ground colour guessed from the texture class name; the names follow a fixed scheme
// (GrassMediumType8, SnowLargeType1, CliffMediumType12, AsphaltType1, ...).
QColor classColor(const std::string &rawName)
{
	QString name = QString::fromStdString(rawName).toLower();
	struct Rule
	{
		const char *word;
		QColor color;
	};
	static const Rule rules[] = {
		{"snow", QColor(0xe4, 0xe8, 0xec)},
		{"ice", QColor(0xcf, 0xdd, 0xe6)},
		{"cliff", QColor(0x7b, 0x74, 0x6a)},
		{"rock", QColor(0x86, 0x80, 0x76)},
		{"mountain", QColor(0x80, 0x78, 0x6c)},
		{"asphalt", QColor(0x55, 0x57, 0x5a)},
		{"parking", QColor(0x5f, 0x60, 0x62)},
		{"concrete", QColor(0x9a, 0x98, 0x92)},
		{"cobble", QColor(0x8a, 0x82, 0x76)},
		{"road", QColor(0x6c, 0x68, 0x62)},
		{"sand", QColor(0xc4, 0xa8, 0x76)},
		{"desert", QColor(0xbf, 0xa2, 0x70)},
		{"dirt", QColor(0x8e, 0x74, 0x55)},
		{"mud", QColor(0x6e, 0x5b, 0x44)},
		{"gravel", QColor(0x94, 0x8c, 0x7e)},
		{"leaves", QColor(0x6f, 0x6a, 0x3a)},
		{"grass", QColor(0x5d, 0x6f, 0x3b)},
		{"farm", QColor(0x7d, 0x7a, 0x44)},
		{"water", QColor(0x3a, 0x6a, 0x82)},
	};
	for (const Rule &r : rules)
		if (name.contains(QLatin1String(r.word)))
			return r.color;
	return QColor(0x7c, 0x78, 0x5c);
}

} // namespace

QPointF worldToCell(const MapModel::HeightMap &hm, float x, float y)
{
	const float cx = x / CELL + static_cast<float>(hm.borderSize);
	const float cy = y / CELL + static_cast<float>(hm.borderSize);
	return QPointF(cx, static_cast<float>(hm.height - 1) - cy);
}

QImage renderMapImage(const MapModel::Map &map, const MapImageOptions &options)
{
	if (!map.heightMap || map.heightMap->width <= 1 || map.heightMap->height <= 1)
		return {};
	const MapModel::HeightMap &hm = *map.heightMap;
	const int w = hm.width;
	const int h = hm.height;
	auto heightAt = [&](int x, int y) {
		x = std::clamp(x, 0, w - 1);
		y = std::clamp(y, 0, h - 1);
		return static_cast<float>(hm.heights[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)]) * HEIGHT_STEP;
	};

	// Texture class per bitmap tile.
	std::vector<QColor> tileColor;
	if (map.blendTiles)
	{
		tileColor.assign(static_cast<std::size_t>(std::max(map.blendTiles->numBitmapTiles, 0)), QColor(0x7c, 0x78, 0x5c));
		for (const MapModel::TextureClass &tc : map.blendTiles->textureClasses)
		{
			const QColor c = classColor(tc.name);
			for (int i = tc.firstTile; i < tc.firstTile + tc.numTiles; ++i)
				if (i >= 0 && static_cast<std::size_t>(i) < tileColor.size())
					tileColor[static_cast<std::size_t>(i)] = c;
		}
	}

	const float lo = static_cast<float>(*std::min_element(hm.heights.begin(), hm.heights.end())) * HEIGHT_STEP;
	const float hi = static_cast<float>(*std::max_element(hm.heights.begin(), hm.heights.end())) * HEIGHT_STEP;
	const float range = std::max(hi - lo, 1.0f);

	QImage img(w, h, QImage::Format_RGB32);
	for (int y = 0; y < h; ++y)
	{
		QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(h - 1 - y));
		for (int x = 0; x < w; ++x)
		{
			const std::size_t cell = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
			QColor base(0x7c, 0x78, 0x5c);
			if (map.blendTiles && cell < map.blendTiles->tiles.size())
			{
				const int tile = static_cast<std::uint16_t>(map.blendTiles->tiles[cell]) >> 2;
				if (tile >= 0 && static_cast<std::size_t>(tile) < tileColor.size())
					base = tileColor[static_cast<std::size_t>(tile)];
			}
			// Hill shading, light from the north-west.
			const float dx = heightAt(x + 1, y) - heightAt(x - 1, y);
			const float dy = heightAt(x, y + 1) - heightAt(x, y - 1);
			float shade = 1.0f + (-dx + dy) * 0.035f;
			shade = std::clamp(shade, 0.55f, 1.35f);
			const float lift = 0.85f + 0.3f * (heightAt(x, y) - lo) / range;
			const float f = shade * lift;
			int r = std::clamp(static_cast<int>(static_cast<float>(base.red()) * f), 0, 255);
			int g = std::clamp(static_cast<int>(static_cast<float>(base.green()) * f), 0, 255);
			int b = std::clamp(static_cast<int>(static_cast<float>(base.blue()) * f), 0, 255);
			if (map.blendTiles && !map.blendTiles->cliffState.empty())
			{
				const std::size_t row = static_cast<std::size_t>((w + 7) / 8);
				const std::size_t idx = static_cast<std::size_t>(y) * row + static_cast<std::size_t>(x >> 3);
				if (idx < map.blendTiles->cliffState.size() && (map.blendTiles->cliffState[idx] & (1 << (x & 7))))
				{
					r = r * 3 / 4;
					g = g * 3 / 4;
					b = b * 3 / 4;
				}
			}
			line[x] = qRgb(r, g, b);
		}
	}

	QPainter p(&img);
	p.setRenderHint(QPainter::Antialiasing);

	// Water areas as translucent polygons (not clipped to the ground below the water level).
	if (options.waterAreas && map.polygons)
	{
		for (const MapModel::PolygonArea &area : *map.polygons)
		{
			if (!area.isWater || area.points.size() < 3)
				continue;
			QPolygonF poly;
			for (const auto &pt : area.points)
				poly << worldToCell(hm, static_cast<float>(pt[0]), static_cast<float>(pt[1]));
			QColor water(0x2c, 0x5f, 0x7a);
			water.setAlpha(170);
			p.setPen(Qt::NoPen);
			p.setBrush(water);
			p.drawPolygon(poly);
		}
	}

	if (options.boundary)
	{
		p.setBrush(Qt::NoBrush);
		QPen pen(QColor(255, 255, 255, 150), 1.0);
		pen.setCosmetic(true);
		p.setPen(pen);
		for (const MapModel::Point2 &b : hm.boundaries)
		{
			const QPointF a = worldToCell(hm, 0.0f, 0.0f);
			const QPointF c = worldToCell(hm, static_cast<float>(b.x) * CELL, static_cast<float>(b.y) * CELL);
			p.drawRect(QRectF(a, c).normalized());
		}
	}

	if (options.objects && map.objects)
	{
		for (const MapModel::MapObject &o : *map.objects)
		{
			const QPointF c = worldToCell(hm, o.x, o.y);
			const bool waypoint = o.properties.getInt("waypointID").has_value();
			if (waypoint)
			{
				const std::string name = o.properties.getAscii("waypointName").value_or("");
				const bool start = name.rfind("Player_", 0) == 0 && name.find("_Start") != std::string::npos;
				p.setPen(Qt::NoPen);
				p.setBrush(start ? QColor(0xff, 0xd2, 0x4a) : QColor(0xb5, 0x8c, 0xff, 200));
				const qreal r = start ? 3.5 : 1.2;
				p.drawEllipse(c, r, r);
			}
			else
			{
				p.setPen(Qt::NoPen);
				p.setBrush(QColor(20, 20, 20, 150));
				p.drawRect(QRectF(c.x() - 0.6, c.y() - 0.6, 1.2, 1.2));
			}
		}
	}
	p.end();

	if (options.maxSize > 0 && std::max(w, h) > options.maxSize)
		return img.scaled(options.maxSize, options.maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	return img;
}

} // namespace MapEditorApp
