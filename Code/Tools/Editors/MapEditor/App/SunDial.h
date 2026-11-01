/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// SunDial.h -- the sun of the map on a dial seen from above: direction round the dial, height from the rim to the middle.

#pragma once

#include "Model/Map.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>

namespace MapEditorApp
{

// The sun of the map on a dial seen from above, north up as in the 2D view: its direction round the
// dial, its height from the rim (on the horizon) to the middle (straight above). The light's position
// is the direction it shines in, so the sun stands opposite it.
class SunDial : public QWidget
{
public:
	float azimuth = 0.0f;		// direction the light shines in, degrees (lightAngles)
	float elevation = 45.0f;	// 90 straight down
	std::function<void(float, float)> onMove;
	std::function<void(float, float)> onDone;

	SunDial()
	{
		setFixedSize(150, 150);
		setCursor(Qt::PointingHandCursor);
	}

protected:
	QPointF centre() const { return QPointF(width() / 2.0, height() / 2.0); }
	double radius() const { return std::min(width(), height()) / 2.0 - 14.0; }
	QPointF sunPoint() const
	{
		const double towardsSun = (static_cast<double>(azimuth) + 180.0) * PI_D / 180.0;
		const double r = radius() * (1.0 - std::clamp(static_cast<double>(elevation), 0.0, 90.0) / 90.0);
		return centre() + QPointF(r * std::cos(towardsSun), -r * std::sin(towardsSun));
	}
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(QPen(palette().color(QPalette::Mid), 1.0));
		p.setBrush(palette().color(QPalette::Base));
		p.drawEllipse(centre(), radius(), radius());
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(centre(), radius() / 2.0, radius() / 2.0);
		p.setPen(palette().color(QPalette::Text));
		const QPointF c = centre();
		const double r = radius() + 8.0;
		p.drawText(QRectF(c.x() - 8, c.y() - r - 7, 16, 14), Qt::AlignCenter, QStringLiteral("N"));
		p.drawText(QRectF(c.x() + r - 8, c.y() - 7, 16, 14), Qt::AlignCenter, QStringLiteral("E"));
		p.drawText(QRectF(c.x() - 8, c.y() + r - 7, 16, 14), Qt::AlignCenter, QStringLiteral("S"));
		p.drawText(QRectF(c.x() - r - 8, c.y() - 7, 16, 14), Qt::AlignCenter, QStringLiteral("W"));
		// The light falls from the sun through the middle.
		p.setPen(QPen(QColor(255, 190, 40), 1.5, Qt::DashLine));
		p.drawLine(sunPoint(), centre());
		p.setPen(QPen(QColor(160, 100, 0), 1.5));
		p.setBrush(QColor(255, 200, 40));
		p.drawEllipse(sunPoint(), 8.0, 8.0);
	}
	void mousePressEvent(QMouseEvent *event) override { move(event->position(), false); }
	void mouseMoveEvent(QMouseEvent *event) override
	{
		if (event->buttons() & Qt::LeftButton)
			move(event->position(), false);
	}
	void mouseReleaseEvent(QMouseEvent *event) override { move(event->position(), true); }

private:
	static constexpr double PI_D = 3.14159265358979323846;
	void move(const QPointF &at, bool done)
	{
		const QPointF d = at - centre();
		const double distance = std::hypot(d.x(), d.y());
		double towardsSun = std::atan2(-d.y(), d.x()) * 180.0 / PI_D;
		double shines = towardsSun - 180.0;
		if (shines < -180.0)
			shines += 360.0;
		azimuth = static_cast<float>(std::lround(shines));
		elevation = static_cast<float>(std::lround(90.0 * (1.0 - std::clamp(distance / radius(), 0.0, 1.0))));
		update();
		if (done ? static_cast<bool>(onDone) : static_cast<bool>(onMove))
			(done ? onDone : onMove)(azimuth, elevation);
	}
};

} // namespace MapEditorApp
