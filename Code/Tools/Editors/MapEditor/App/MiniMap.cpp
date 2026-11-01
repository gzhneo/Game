/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MiniMap.cpp -- small picture of the whole map beside the 3D view.

#include "App/Common.h"
#include "App/MiniMap.h"

#include "App/MapImage.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace MapEditorApp
{

namespace
{

constexpr int PICTURE_SIZE = 256;

} // namespace

MiniMap::MiniMap(const MapModel::Map &map, QWidget *parent) : QWidget(parent), m_map(map)
{
	MapImageOptions options;
	options.objects = true;
	options.maxSize = PICTURE_SIZE;
	m_image = renderMapImage(m_map, options);
	setMinimumHeight(120);
	setCursor(Qt::PointingHandCursor);
}

void MiniMap::setCamera(float x, float y, float direction)
{
	if (x == m_cameraX && y == m_cameraY && direction == m_yaw)
		return;
	m_cameraX = x;
	m_cameraY = y;
	m_yaw = direction;
	update();
}

QRectF MiniMap::pictureRect() const
{
	if (m_image.isNull())
		return {};
	const QSizeF fit = QSizeF(m_image.size()).scaled(QSizeF(width(), height()), Qt::KeepAspectRatio);
	return QRectF(QPointF((width() - fit.width()) * 0.5, (height() - fit.height()) * 0.5), fit);
}

QPointF MiniMap::worldToWidget(float x, float y) const
{
	if (!m_map.heightMap)
		return {};
	const MapModel::HeightMap &hm = *m_map.heightMap;
	const QRectF r = pictureRect();
	// Picture row 0 is the northern (highest y) row, as in the 2D overview.
	const double cx = static_cast<double>(x) / CELL + hm.borderSize;
	const double cy = static_cast<double>(hm.height - 1) - (static_cast<double>(y) / CELL + hm.borderSize);
	return QPointF(r.left() + cx / hm.width * r.width(), r.top() + cy / hm.height * r.height());
}

bool MiniMap::widgetToWorld(const QPointF &pos, float &x, float &y) const
{
	if (!m_map.heightMap)
		return false;
	const QRectF r = pictureRect();
	if (!r.contains(pos))
		return false;
	const MapModel::HeightMap &hm = *m_map.heightMap;
	const double cx = (pos.x() - r.left()) / r.width() * hm.width;
	const double cy = (pos.y() - r.top()) / r.height() * hm.height;
	x = static_cast<float>((cx - hm.borderSize) * CELL);
	y = static_cast<float>((static_cast<double>(hm.height - 1) - cy - hm.borderSize) * CELL);
	return true;
}

void MiniMap::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	if (m_image.isNull())
		return;
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	p.drawImage(pictureRect(), m_image);
	p.setRenderHint(QPainter::Antialiasing);
	// The camera: a cone in its direction; the picture's y axis points down.
	const QPointF c = worldToWidget(m_cameraX, m_cameraY);
	const double dx = std::cos(m_yaw);
	const double dy = -std::sin(m_yaw);
	const double length = 22.0;
	const double spread = 0.55;
	QPainterPath cone;
	cone.moveTo(c);
	cone.lineTo(c + QPointF(std::cos(std::atan2(dy, dx) - spread), std::sin(std::atan2(dy, dx) - spread)) * length);
	cone.lineTo(c + QPointF(std::cos(std::atan2(dy, dx) + spread), std::sin(std::atan2(dy, dx) + spread)) * length);
	cone.closeSubpath();
	p.setPen(QPen(QColor(0x45, 0xc8, 0xff), 1.5));
	p.setBrush(QColor(0x45, 0xc8, 0xff, 70));
	p.drawPath(cone);
	p.setBrush(QColor(0x45, 0xc8, 0xff));
	p.drawEllipse(c, 3.5, 3.5);
}

void MiniMap::jump(const QPointF &pos)
{
	float x = 0.0f;
	float y = 0.0f;
	if (onJump && widgetToWorld(pos, x, y))
		onJump(x, y);
}

void MiniMap::mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton)
		jump(event->position());
}

void MiniMap::mouseMoveEvent(QMouseEvent *event)
{
	if (event->buttons() & Qt::LeftButton)
		jump(event->position());
}

} // namespace MapEditorApp
