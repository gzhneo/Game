/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MapOverview.cpp -- top-down view of the open map.

#include "App/Common.h"
#include "App/MapOverview.h"

#include "App/Language.h"
#include "App/MapImage.h"
#include "Core/Contours.h"
#include "Core/Slope.h"
#include "Model/MapIO.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <set>
#include <cmath>
#include <map>
#include <optional>

namespace MapEditorApp
{

namespace
{

constexpr std::int32_t ROAD_AND_BRIDGE_FLAGS = 0x36;	// the terrain draws these points
constexpr std::int32_t ROAD_POINT1 = 0x02;
constexpr std::int32_t ROAD_POINT2 = 0x04;
constexpr std::int32_t BRIDGE_POINT1 = 0x10;
constexpr std::int32_t BRIDGE_POINT2 = 0x20;
constexpr double PICK_PIXELS = 8.0;

bool pickable(const MapModel::MapObject &o, const std::set<std::string> &hidden)
{
	return (o.flags & ROAD_AND_BRIDGE_FLAGS) == 0 && (hidden.empty() || !hidden.count(o.properties.getAscii("objectLayer").value_or("")));
}

bool isStartPosition(const std::string &name)
{
	return name.rfind("Player_", 0) == 0 && name.find("_Start") != std::string::npos;
}

} // namespace

MapOverview::MapOverview(const MapModel::Map &map, const MapEditorCore::Selection &selection, const Tokens &tokens, QWidget *parent)
	: QWidget(parent), m_map(map), m_selection(selection), m_tokens(tokens)
{
	MapImageOptions options;
	options.objects = false;	// drawn live in paintEvent
	options.waterAreas = false;
	m_image = renderMapImage(map, options);
	setMouseTracking(true);
	setMinimumSize(200, 200);
	setFocusPolicy(Qt::StrongFocus);
}

void MapOverview::fit()
{
	if (m_image.isNull() || width() <= 0 || height() <= 0)
		return;
	const double margin = 24.0;
	m_scale = std::min((width() - 2 * margin) / m_image.width(), (height() - 2 * margin) / m_image.height());
	m_scale = std::max(m_scale, 0.05);
	m_offset = QPointF((width() - m_image.width() * m_scale) / 2.0, (height() - m_image.height() * m_scale) / 2.0);
	m_userView = false;
	update();
}

void MapOverview::centerOn(float x, float y)
{
	if (!m_map.heightMap)
		return;
	m_offset += QPointF(width() / 2.0, height() / 2.0) - worldToWidget(x, y);
	m_userView = true;
	update();
}

QPointF MapOverview::toImage(const QPointF &widgetPos) const
{
	return (widgetPos - m_offset) / m_scale;
}

QPointF MapOverview::worldToWidget(float x, float y) const
{
	if (!m_map.heightMap)
		return {};
	return m_offset + worldToCell(*m_map.heightMap, x, y) * m_scale;
}

QPointF MapOverview::widgetToWorld(const QPointF &pos) const
{
	if (!m_map.heightMap)
		return {};
	const auto &hm = *m_map.heightMap;
	const QPointF cell = toImage(pos);
	return QPointF((cell.x() - hm.borderSize) * CELL, (static_cast<double>(hm.height - 1) - cell.y() - hm.borderSize) * CELL);
}

int MapOverview::objectAt(const QPointF &pos) const
{
	if (!m_map.objects || !m_map.heightMap)
		return -1;
	int best = -1;
	double bestDistance = PICK_PIXELS * PICK_PIXELS;
	const auto &objects = *m_map.objects;
	for (std::size_t i = 0; i < objects.size(); ++i)
	{
		if (!pickable(objects[i], m_hiddenLayers))
			continue;
		const QPointF d = worldToWidget(objects[i].x, objects[i].y) - pos;
		const double distance = d.x() * d.x() + d.y() * d.y();
		if (distance <= bestDistance)
		{
			bestDistance = distance;
			best = static_cast<int>(i);
		}
	}
	return best;
}

void MapOverview::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.fillRect(rect(), QColor(0x0c, 0x0d, 0x0f));
	if (m_image.isNull())
	{
		p.setPen(m_tokens.tx3);
		p.drawText(rect(), Qt::AlignCenter, T("The map has no height map."));
		return;
	}
	p.setRenderHint(QPainter::SmoothPixmapTransform, m_scale < 2.0);
	const QRectF target(m_offset, QSizeF(m_image.width() * m_scale, m_image.height() * m_scale));
	p.drawImage(target, m_image);
	if (!m_tint.isNull())
		p.drawImage(target, m_tint);
	if (!m_slopes.isNull())
		p.drawImage(target, m_slopes);
	if (!m_impassable.isNull())
		p.drawImage(target, m_impassable);
	if (!m_contours.isEmpty())
	{
		p.save();
		p.translate(m_offset);
		p.scale(m_scale, m_scale);
		QPen pen(QColor(255, 255, 255, 110));
		pen.setCosmetic(true);
		p.setPen(pen);
		p.drawLines(m_contours);
		p.restore();
	}

	// Grid every 10 cells when zoomed in far enough.
	if (m_showGrid && m_scale >= 4.0)
	{
		QPen pen(QColor(255, 255, 255, 28));
		pen.setCosmetic(true);
		p.setPen(pen);
		for (int x = 0; x <= m_image.width(); x += 10)
			p.drawLine(QPointF(target.left() + x * m_scale, target.top()), QPointF(target.left() + x * m_scale, target.bottom()));
		for (int y = 0; y <= m_image.height(); y += 10)
			p.drawLine(QPointF(target.left(), target.top() + y * m_scale), QPointF(target.right(), target.top() + y * m_scale));
	}

	// Objects from the model: dots, waypoints, start positions; the selection with ring and facing.
	p.setRenderHint(QPainter::Antialiasing);
	// Areas live from the model: water filled, trigger areas as outlines; the selected area and the
	// one being drawn with their points.
	const auto areaPolygon = [this](const MapModel::PolygonArea &area) {
		QPolygonF poly;
		for (const auto &pt : area.points)
			poly << worldToWidget(static_cast<float>(pt[0]), static_cast<float>(pt[1]));
		return poly;
	};
	const auto drawPoints = [&p](const QPolygonF &poly, int selected) {
		for (int i = 0; i < poly.size(); ++i)
		{
			p.setPen(QPen(QColor(20, 20, 20), 1.0));
			p.setBrush(i == selected ? QColor(0x45, 0xc8, 0xff) : QColor(255, 255, 255));
			p.drawRect(QRectF(poly[i].x() - 3.5, poly[i].y() - 3.5, 7.0, 7.0));
		}
	};
	if (m_map.polygons)
	{
		for (std::size_t i = 0; i < m_map.polygons->size(); ++i)
		{
			const MapModel::PolygonArea &area = (*m_map.polygons)[i];
			const QPolygonF poly = areaPolygon(area);
			const bool selected = static_cast<int>(i) == m_markArea;
			QPen pen(selected ? QColor(0x45, 0xc8, 0xff) : (area.isWater ? QColor(0x4f, 0xa3, 0xd8) : QColor(0xf2, 0xc9, 0x4c)), selected ? 2.0 : 1.5);
			pen.setCosmetic(true);
			p.setPen(pen);
			p.setBrush(area.isWater ? QColor(0x2c, 0x5f, 0x7a, 150) : QColor(0xf2, 0xc9, 0x4c, selected ? 40 : 0));
			p.drawPolygon(poly);
			if (selected)
				drawPoints(poly, m_markPoint);
		}
	}
	if (m_draft)
	{
		const QPolygonF poly = areaPolygon(*m_draft);
		QPen pen(QColor(0x45, 0xc8, 0xff), 2.0, Qt::DashLine);
		pen.setCosmetic(true);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		p.drawPolyline(poly);
		drawPoints(poly, -1);
	}
	// Roads: a band between the points of each segment (the terrain's roads are about 30 units wide).
	if (m_map.objects)
	{
		QPen road(QColor(0x55, 0x52, 0x4e, 220), std::max(2.0, 3.0 * m_scale), Qt::SolidLine, Qt::RoundCap);
		p.setPen(road);
		const auto &objects = *m_map.objects;
		for (std::size_t i = 0; i + 1 < objects.size(); ++i)
		{
			if ((objects[i].flags & ROAD_POINT1) && (objects[i + 1].flags & ROAD_POINT2))
				p.drawLine(worldToWidget(objects[i].x, objects[i].y), worldToWidget(objects[i + 1].x, objects[i + 1].y));
		}
		QPen bridge(QColor(0x8a, 0x6a, 0x48, 230), std::max(3.0, 4.0 * m_scale), Qt::SolidLine, Qt::FlatCap);
		p.setPen(bridge);
		for (std::size_t i = 0; i + 1 < objects.size(); ++i)
		{
			if ((objects[i].flags & BRIDGE_POINT1) && (objects[i + 1].flags & BRIDGE_POINT2))
				p.drawLine(worldToWidget(objects[i].x, objects[i].y), worldToWidget(objects[i + 1].x, objects[i + 1].y));
		}
	}
	if (m_toolLines.size() >= 4)
	{
		QPen line(QColor(0x45, 0xc8, 0xff), 2.0);
		line.setCosmetic(true);
		for (std::size_t i = 0; i + 3 < m_toolLines.size(); i += 4)
		{
			const QPointF from = worldToWidget(m_toolLines[i], m_toolLines[i + 1]);
			const QPointF to = worldToWidget(m_toolLines[i + 2], m_toolLines[i + 3]);
			p.setPen(line);
			p.drawLine(from, to);
			p.setPen(QPen(QColor(20, 20, 20), 1.0));
			p.setBrush(QColor(0x45, 0xc8, 0xff));
			p.drawRect(QRectF(from.x() - 3.5, from.y() - 3.5, 7.0, 7.0));
			p.drawRect(QRectF(to.x() - 3.5, to.y() - 3.5, 7.0, 7.0));
		}
	}
	// Waypoint links under the dots, with an arrow halfway in their direction.
	if (m_map.objects && m_map.waypointLinks)
	{
		std::map<std::int32_t, QPointF> byId;
		for (const MapModel::MapObject &o : *m_map.objects)
		{
			if (const auto id = o.properties.getInt("waypointID"))
				byId[*id] = worldToWidget(o.x, o.y);
		}
		QPen link(QColor(0xb5, 0x8c, 0xff, 200), 1.5);
		link.setCosmetic(true);
		p.setPen(link);
		p.setBrush(Qt::NoBrush);
		for (const MapModel::WaypointLink &l : *m_map.waypointLinks)
		{
			const auto from = byId.find(l.from);
			const auto to = byId.find(l.to);
			if (from == byId.end() || to == byId.end())
				continue;
			const QPointF a = from->second;
			const QPointF b = to->second;
			p.drawLine(a, b);
			const double length = std::hypot(b.x() - a.x(), b.y() - a.y());
			if (length < 16.0)
				continue;
			const QPointF dir = (b - a) / length;
			const QPointF normal(-dir.y(), dir.x());
			const QPointF tip = a + (b - a) * 0.55;
			p.drawLine(tip, tip - dir * 6.0 + normal * 4.0);
			p.drawLine(tip, tip - dir * 6.0 - normal * 4.0);
		}
	}
	if (m_map.objects)
	{
		const double dot = std::max(2.0, 1.2 * m_scale);
		for (const MapModel::MapObject &o : *m_map.objects)
		{
			if (!pickable(o, m_hiddenLayers))
				continue;
			const QPointF c = worldToWidget(o.x, o.y);
			if (!rect().adjusted(-20, -20, 20, 20).contains(c.toPoint()))
				continue;
			p.setPen(Qt::NoPen);
			if (const auto waypoint = o.properties.getAscii("waypointName"))
			{
				const bool start = isStartPosition(*waypoint);
				p.setBrush(start ? QColor(0xff, 0xd2, 0x4a) : QColor(0xb5, 0x8c, 0xff, 220));
				const double r = start ? std::max(5.0, 3.5 * m_scale) : std::max(3.0, 1.2 * m_scale);
				p.drawEllipse(c, r, r);
			}
			else if (m_showObjects)
			{
				p.setBrush(QColor(20, 20, 20, 170));
				p.drawRect(QRectF(c.x() - dot * 0.5, c.y() - dot * 0.5, dot, dot));
			}
		}
		if (m_showLabels)
		{
			// Names beside the dots, with a dark outline so they read on any ground.
			QFont font = p.font();
			font.setPointSizeF(8.0);
			p.setFont(font);
			const auto text = [&p](const QPointF &at, const QString &s) {
				p.setPen(QColor(0, 0, 0, 200));
				p.drawText(at + QPointF(1.0, 1.0), s);
				p.setPen(QColor(255, 255, 255));
				p.drawText(at, s);
			};
			for (const MapModel::MapObject &o : *m_map.objects)
			{
				if (!pickable(o, m_hiddenLayers))
					continue;
				std::optional<std::string> name = o.properties.getAscii("waypointName");
				if (!name || name->empty())
					name = o.properties.getAscii("objectName");
				if (!name || name->empty())
					continue;
				const QPointF c = worldToWidget(o.x, o.y);
				if (rect().contains(c.toPoint()))
					text(c + QPointF(6.0, -4.0), QString::fromUtf8(name->c_str()));
			}
			if (m_map.polygons)
			{
				for (const MapModel::PolygonArea &area : *m_map.polygons)
				{
					if (area.points.empty() || area.name.empty())
						continue;
					QPointF centre;
					for (const auto &pt : area.points)
						centre += worldToWidget(static_cast<float>(pt[0]), static_cast<float>(pt[1]));
					centre /= static_cast<double>(area.points.size());
					text(centre, QString::fromUtf8(area.name.c_str()));
				}
			}
		}
		const QColor accent(0x45, 0xc8, 0xff);
		QPen ring(accent, 2.0);
		ring.setCosmetic(true);
		p.setBrush(Qt::NoBrush);
		p.setPen(ring);
		for (std::int32_t index : m_selection.objects)
		{
			if (index < 0 || static_cast<std::size_t>(index) >= m_map.objects->size())
				continue;
			const MapModel::MapObject &o = (*m_map.objects)[static_cast<std::size_t>(index)];
			const QPointF c = worldToWidget(o.x, o.y);
			const double r = std::max(7.0, 1.5 * m_scale);
			p.drawEllipse(c, r, r);
			// Facing: +x at angle 0; the picture's y axis points down.
			p.drawLine(c, c + QPointF(std::cos(o.angle), -std::sin(o.angle)) * (r + 6.0));
		}
	}

	if (m_toolMode && m_brushValid && m_brushRadius > 0.0f)
	{
		const QPointF c = worldToWidget(m_brushX, m_brushY);
		const double r = static_cast<double>(m_brushRadius) / CELL * m_scale;
		QPen pen(QColor(0x45, 0xc8, 0xff), 1.5);
		pen.setCosmetic(true);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(c, r, r);
	}
	if (m_marquee)
	{
		const QRectF r = QRectF(m_pressPos, m_marqueeEnd).normalized();
		p.setPen(QPen(QColor(0x45, 0xc8, 0xff), 1.0));
		p.setBrush(QColor(0x45, 0xc8, 0xff, 0x30));
		p.drawRect(r);
	}
}

void MapOverview::resizeEvent(QResizeEvent *)
{
	if (!m_userView)
		fit();
}

void MapOverview::wheelEvent(QWheelEvent *event)
{
	const QPointF at = event->position();
	const QPointF before = toImage(at);
	const double factor = std::pow(1.0015, event->angleDelta().y());
	m_scale = std::clamp(m_scale * factor, 0.05, 40.0);
	m_offset = at - before * m_scale;
	m_userView = true;
	update();
}

void MapOverview::setToolMode(bool on)
{
	m_toolMode = on;
	m_toolDown = false;
	if (on || m_placing)
		setCursor(Qt::CrossCursor);
	else
		unsetCursor();
}

void MapOverview::setAreaMarks(int area, int point, const MapModel::PolygonArea *draft)
{
	m_markArea = area;
	m_markPoint = point;
	m_draft = draft;
	update();
}

void MapOverview::setBrushCircle(bool valid, float x, float y, float radius)
{
	m_brushValid = valid;
	m_brushX = x;
	m_brushY = y;
	m_brushRadius = radius;
	update();
}

void MapOverview::refreshImage()
{
	MapImageOptions options;
	options.objects = false;
	options.waterAreas = false;
	m_image = renderMapImage(m_map, options);
	buildImpassable();
	buildSlopes();
	buildContours();
	update();
}

void MapOverview::setContours(int step, int offset)
{
	if (step == m_contourStep && offset == m_contourOffset)
		return;
	m_contourStep = step;
	m_contourOffset = offset;
	buildContours();
	update();
}

void MapOverview::setShowGrid(bool show)
{
	m_showGrid = show;
	update();
}

void MapOverview::setShowLabels(bool show)
{
	m_showLabels = show;
	update();
}

void MapOverview::setShowObjects(bool show)
{
	m_showObjects = show;
	update();
}

void MapOverview::buildContours()
{
	m_contours.clear();
	if (m_contourStep <= 0 || !m_map.heightMap)
		return;
	const std::vector<float> lines = MapEditorCore::contourLines(*m_map.heightMap, m_contourStep, m_contourOffset);
	// Sample (x, y) lies at image pixel (x, height - 1 - y), as widgetToWorld has it.
	const double top = static_cast<double>(m_map.heightMap->height - 1);
	m_contours.reserve(static_cast<qsizetype>(lines.size() / 4));
	for (std::size_t i = 0; i + 3 < lines.size(); i += 4)
		m_contours.push_back(QLineF(lines[i], top - lines[i + 1], lines[i + 2], top - lines[i + 3]));
}

void MapOverview::setHiddenLayers(std::set<std::string> layers)
{
	m_hiddenLayers = std::move(layers);
	update();
}

void MapOverview::setShowImpassable(bool show)
{
	if (show == m_showImpassable)
		return;
	m_showImpassable = show;
	buildImpassable();
	update();
}

void MapOverview::setSlopes(float limit)
{
	if (limit == m_slopeLimit)
		return;
	m_slopeLimit = limit;
	buildSlopes();
	update();
}

void MapOverview::setTint(const std::vector<std::uint32_t> &colors)
{
	m_tint = QImage();
	if (m_map.heightMap && !m_image.isNull() && colors.size() == static_cast<std::size_t>(m_map.heightMap->width) * static_cast<std::size_t>(m_map.heightMap->height))
	{
		const MapModel::HeightMap &hm = *m_map.heightMap;
		m_tint = QImage(m_image.size(), QImage::Format_ARGB32_Premultiplied);
		m_tint.fill(Qt::transparent);
		for (int y = 0; y + 1 < hm.height && y + 1 < m_tint.height(); ++y)
		{
			QRgb *row = reinterpret_cast<QRgb *>(m_tint.scanLine(hm.height - 2 - y));
			for (int x = 0; x + 1 < hm.width && x < m_tint.width(); ++x)
				row[x] = qPremultiply(colors[static_cast<std::size_t>(y) * static_cast<std::size_t>(hm.width) + static_cast<std::size_t>(x)]);
		}
	}
	update();
}

void MapOverview::buildSlopes()
{
	m_slopes = QImage();
	m_steepCells = 0;
	if (m_slopeLimit <= 0.0f || !m_map.heightMap || m_image.isNull())
		return;
	const MapModel::HeightMap &hm = *m_map.heightMap;
	const std::vector<float> slopes = MapEditorCore::slopeMap(hm);
	m_slopes = QImage(m_image.size(), QImage::Format_ARGB32_Premultiplied);
	m_slopes.fill(Qt::transparent);
	// Same rows as the impassable cells: a cell spans its corner samples y .. y + 1.
	for (int y = 0; y + 1 < hm.height && y + 1 < m_slopes.height(); ++y)
	{
		QRgb *row = reinterpret_cast<QRgb *>(m_slopes.scanLine(hm.height - 2 - y));
		for (int x = 0; x + 1 < hm.width && x < m_slopes.width(); ++x)
		{
			const float degrees = slopes[static_cast<std::size_t>(y) * static_cast<std::size_t>(hm.width) + static_cast<std::size_t>(x)];
			const MapEditorCore::SlopeColor c = MapEditorCore::slopeColor(degrees, m_slopeLimit);
			// Flat ground stays see-through, steep ground covers the picture more.
			const int alpha = degrees >= m_slopeLimit ? 0xd0 : 0x40 + static_cast<int>(0x70 * std::min(1.0f, degrees / m_slopeLimit));
			row[x] = qPremultiply(qRgba(c.r, c.g, c.b, alpha));
			m_steepCells += degrees >= m_slopeLimit ? 1 : 0;
		}
	}
}

void MapOverview::buildImpassable()
{
	m_impassable = QImage();
	if (!m_showImpassable || !m_map.heightMap || !m_map.blendTiles || m_image.isNull())
		return;
	const MapModel::HeightMap &hm = *m_map.heightMap;
	const std::vector<std::uint8_t> &flags = m_map.blendTiles->cliffState;
	m_impassable = QImage(m_image.size(), QImage::Format_ARGB32_Premultiplied);
	m_impassable.fill(Qt::transparent);
	const QRgb red = qPremultiply(qRgba(0xe0, 0x30, 0x30, 0x90));
	// A cell spans its corner samples y .. y + 1: image row height - 2 - y.
	for (int y = 0; y + 1 < hm.height && y + 1 < m_impassable.height(); ++y)
	{
		QRgb *row = reinterpret_cast<QRgb *>(m_impassable.scanLine(hm.height - 2 - y));
		for (int x = 0; x < hm.width && x < m_impassable.width(); ++x)
		{
			if (MapModel::cliffFlag(flags, hm.width, x, y))
				row[x] = red;
		}
	}
}

void MapOverview::setToolLines(std::vector<float> lines)
{
	m_toolLines = std::move(lines);
	update();
}

void MapOverview::emitTool(ToolEvent::Kind kind, const QPointF &pos, Qt::KeyboardModifiers mods)
{
	if (!onTool || !m_map.heightMap)
		return;
	ToolEvent e;
	e.kind = kind;
	const QPointF world = widgetToWorld(pos);
	e.x = static_cast<float>(world.x());
	e.y = static_cast<float>(world.y());
	e.tolerance = static_cast<float>(8.0 * CELL / std::max(m_scale, 0.01));
	e.ctrl = (mods & Qt::ControlModifier) != 0;
	e.shift = (mods & Qt::ShiftModifier) != 0;
	e.alt = (mods & Qt::AltModifier) != 0;
	onTool(e);
}

void MapOverview::setPlacing(bool placing)
{
	m_placing = placing;
	m_placePress = false;
	if (placing || m_toolMode)
		setCursor(Qt::CrossCursor);
	else
		unsetCursor();
}

void MapOverview::mousePressEvent(QMouseEvent *event)
{
	setFocus();
	m_button = event->button();
	m_pressPos = event->position();
	if (m_button == Qt::MiddleButton || m_button == Qt::RightButton)
	{
		m_panning = true;
		m_dragStart = event->position();
		m_offsetAtDrag = m_offset;
		setCursor(Qt::ClosedHandCursor);
		return;
	}
	if (m_button != Qt::LeftButton)
		return;
	if (m_toolMode)
	{
		m_toolDown = true;
		emitTool(ToolEvent::Kind::Press, m_pressPos, event->modifiers());
		return;
	}
	if (m_placing)
	{
		const QPointF world = widgetToWorld(m_pressPos);
		m_pressWorldX = static_cast<float>(world.x());
		m_pressWorldY = static_cast<float>(world.y());
		m_placePress = true;
		m_placeCtrl = (event->modifiers() & Qt::ControlModifier) != 0;
		m_placeObject = objectAt(m_pressPos);
		return;
	}
	m_pressCtrl = (event->modifiers() & Qt::ControlModifier) != 0;
	m_rotateMode = (event->modifiers() & Qt::ShiftModifier) != 0;
	const QPointF world = widgetToWorld(m_pressPos);
	m_pressWorldX = static_cast<float>(world.x());
	m_pressWorldY = static_cast<float>(world.y());
	m_pressObject = objectAt(m_pressPos);
	if (onClickObject)
		onClickObject(m_pressObject, m_pressCtrl && !m_rotateMode, false);
	if (m_pressObject < 0)
	{
		m_marquee = true;
		m_marqueeEnd = m_pressPos;
	}
	update();
}

void MapOverview::mouseMoveEvent(QMouseEvent *event)
{
	if (m_toolDown)
	{
		emitTool(ToolEvent::Kind::Move, event->position(), event->modifiers());
		report(event->position());
		return;
	}
	if (m_panning)
	{
		m_offset = m_offsetAtDrag + (event->position() - m_dragStart);
		m_userView = true;
		update();
	}
	else if (m_button == Qt::LeftButton && m_marquee)
	{
		m_marqueeEnd = event->position();
		update();
	}
	else if (m_button == Qt::LeftButton && m_pressObject >= 0 && (!m_pressCtrl || m_rotateMode))
	{
		const QPointF moved = event->position() - m_pressPos;
		if (!m_draggingObject && std::abs(moved.x()) + std::abs(moved.y()) > 3.0)
		{
			m_draggingObject = true;
			setCursor(m_rotateMode ? Qt::CrossCursor : Qt::SizeAllCursor);
		}
		if (m_draggingObject)
		{
			const QPointF world = widgetToWorld(event->position());
			const float x = static_cast<float>(world.x());
			const float y = static_cast<float>(world.y());
			if (m_rotateMode)
			{
				if (onRotateSelection)
					onRotateSelection(m_pressWorldX, m_pressWorldY, x, y, (event->modifiers() & Qt::ControlModifier) != 0);
			}
			else if (onDragSelection)
				onDragSelection(x - m_pressWorldX, y - m_pressWorldY);
			update();
		}
	}
	report(event->position());
}

void MapOverview::report(const QPointF &pos)
{
	if (!onHover || !m_map.heightMap)
		return;
	const QPointF img = toImage(pos);
	const auto &hm = *m_map.heightMap;
	const int cx = static_cast<int>(std::floor(img.x()));
	const int row = static_cast<int>(std::floor(img.y()));
	const int cy = hm.height - 1 - row;
	if (cx < 0 || cy < 0 || cx >= hm.width || cy >= hm.height || m_image.isNull() || m_image.width() != hm.width)
	{
		onHover(-1, -1, 0, 0.0f, 0.0f);
		return;
	}
	const int h = hm.heights[static_cast<std::size_t>(cy) * static_cast<std::size_t>(hm.width) + static_cast<std::size_t>(cx)];
	const QPointF world = widgetToWorld(pos);
	onHover(cx, cy, h, static_cast<float>(world.x()), static_cast<float>(world.y()));
}

void MapOverview::endObjectDrag(bool cancel)
{
	if (m_draggingObject)
	{
		if (m_rotateMode)
		{
			if (onRotateSelectionEnd)
				onRotateSelectionEnd(cancel);
		}
		else if (onDragSelectionEnd)
			onDragSelectionEnd(cancel);
	}
	m_pressObject = -1;
	m_draggingObject = false;
	m_rotateMode = false;
	update();
}

void MapOverview::mouseReleaseEvent(QMouseEvent *event)
{
	if (m_toolDown && event->button() == Qt::LeftButton)
	{
		m_toolDown = false;
		m_button = Qt::NoButton;
		emitTool(ToolEvent::Kind::Release, event->position(), event->modifiers());
		return;
	}
	if (m_placePress && m_button == Qt::LeftButton)
	{
		m_placePress = false;
		m_button = Qt::NoButton;
		const QPointF moved = event->position() - m_pressPos;
		const QPointF world = widgetToWorld(event->position());
		const bool aimed = std::abs(moved.x()) + std::abs(moved.y()) > 6.0;
		if (onPlace)
			onPlace(m_pressWorldX, m_pressWorldY,
				static_cast<float>(std::atan2(world.y() - m_pressWorldY, world.x() - m_pressWorldX)), aimed, m_placeObject, m_placeCtrl);
		return;
	}
	if (m_button == Qt::LeftButton)
	{
		if (m_marquee)
		{
			m_marquee = false;
			const QRectF r = QRectF(m_pressPos, event->position()).normalized();
			if (r.width() >= 4.0 && r.height() >= 4.0 && onSelectRect && m_map.objects)
			{
				std::vector<int> inside;
				const auto &objects = *m_map.objects;
				for (std::size_t i = 0; i < objects.size(); ++i)
				{
					if (pickable(objects[i], m_hiddenLayers) && r.contains(worldToWidget(objects[i].x, objects[i].y)))
						inside.push_back(static_cast<int>(i));
				}
				onSelectRect(inside, m_pressCtrl);
			}
			update();
		}
		else if (m_pressObject >= 0 && !m_draggingObject && onClickObject)
			onClickObject(m_pressObject, m_pressCtrl && !m_rotateMode, true);
		endObjectDrag(false);
	}
	m_panning = false;
	m_button = Qt::NoButton;
	if (m_placing)
		setCursor(Qt::CrossCursor);
	else
		unsetCursor();
}

void MapOverview::mouseDoubleClickEvent(QMouseEvent *event)
{
	if (m_toolMode && event->button() == Qt::LeftButton)
	{
		emitTool(ToolEvent::Kind::DoubleClick, event->position(), event->modifiers());
		return;
	}
	fit();
}

void MapOverview::keyPressEvent(QKeyEvent *event)
{
	if (m_toolMode && onToolKey && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Backspace))
	{
		onToolKey(event->key());
		return;
	}
	if (event->key() != Qt::Key_Escape)
	{
		QWidget::keyPressEvent(event);
		return;
	}
	if (m_draggingObject)
	{
		endObjectDrag(true);
		m_button = Qt::NoButton;
		unsetCursor();
	}
	else if (m_marquee)
	{
		m_marquee = false;
		m_button = Qt::NoButton;
		update();
	}
	else if (onEscape)
		onEscape();
}

void MapOverview::leaveEvent(QEvent *)
{
	if (onHover)
		onHover(-1, -1, 0, 0.0f, 0.0f);
}

} // namespace MapEditorApp
