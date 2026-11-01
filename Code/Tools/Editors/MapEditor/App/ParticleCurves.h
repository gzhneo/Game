/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ParticleCurves.h -- the colour and alpha keys of a particle system as curves over a particle's life.

#pragma once

#include "App/Language.h"
#include "Core/ParticleIni.h"

#include <QColorDialog>
#include <QMouseEvent>
#include <QPainter>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <vector>

namespace MapEditorApp
{

// Frames 0..last across the width, shared by the colour and alpha editors so their keys line up.
struct FrameAxis
{
	int last = 60;
	static constexpr double MARGIN = 12.0;
	double x(int frame, double width) const { return MARGIN + (width - 2.0 * MARGIN) * frame / std::max(1, last); }
	int frameAt(double px, double width) const
	{
		const double f = (px - MARGIN) / std::max(1.0, width - 2.0 * MARGIN) * last;
		return std::clamp(static_cast<int>(std::lround(f)), 0, last);
	}
};

// The colour key frames as the gradient a particle runs through. A click on a key picks its colour,
// dragging moves it in time, a double click adds a key, a right click removes one. The first key is
// the start colour and keeps its frame.
class ColorGradient : public QWidget
{
public:
	std::vector<MapEditorCore::ColorKey> keys;
	FrameAxis axis;
	std::function<void()> onChanged;

	ColorGradient()
	{
		setMinimumHeight(46);
		setCursor(Qt::PointingHandCursor);
		setToolTip(T("Click a key to pick its colour, drag it in time; double-click adds a key, right-click removes one."));
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		const double w = width();
		const int bar = height() - 16;
		for (int px = static_cast<int>(FrameAxis::MARGIN); px < static_cast<int>(w - FrameAxis::MARGIN); ++px)
		{
			const MapEditorCore::ColorKey c = MapEditorCore::colorAt(keys, axis.frameAt(px, w));
			p.setPen(QColor(std::clamp(c.r, 0, 255), std::clamp(c.g, 0, 255), std::clamp(c.b, 0, 255)));
			p.drawLine(px, 2, px, bar);
		}
		p.setPen(palette().color(QPalette::Mid));
		p.drawRect(QRectF(FrameAxis::MARGIN, 2, w - 2 * FrameAxis::MARGIN, bar - 2));
		for (std::size_t i = 0; i < keys.size(); ++i)
		{
			const double x = axis.x(keys[i].frame, w);
			const QRectF handle(x - 5, bar + 2, 10, 11);
			p.setBrush(QColor(std::clamp(keys[i].r, 0, 255), std::clamp(keys[i].g, 0, 255), std::clamp(keys[i].b, 0, 255)));
			p.setPen(static_cast<int>(i) == m_drag ? palette().color(QPalette::Highlight) : palette().color(QPalette::Text));
			p.drawRect(handle);
		}
	}
	void mousePressEvent(QMouseEvent *event) override
	{
		m_drag = keyAt(event->position().x());
		m_moved = false;
		if (event->button() == Qt::RightButton && m_drag > 0)
		{
			keys.erase(keys.begin() + m_drag);
			m_drag = -1;
			changed();
		}
		update();
	}
	void mouseMoveEvent(QMouseEvent *event) override
	{
		if (m_drag <= 0 || !(event->buttons() & Qt::LeftButton))
			return;
		keys[static_cast<std::size_t>(m_drag)].frame = std::max(1, axis.frameAt(event->position().x(), width()));
		m_moved = true;
		update();
	}
	void mouseReleaseEvent(QMouseEvent *event) override
	{
		const int key = m_drag;
		m_drag = -1;
		if (key < 0 || event->button() != Qt::LeftButton)
			return;
		if (m_moved)
		{
			changed();
			return;
		}
		MapEditorCore::ColorKey &k = keys[static_cast<std::size_t>(key)];
		const QColor chosen = QColorDialog::getColor(QColor(k.r, k.g, k.b), this, T("Colour %1").arg(key + 1));
		if (!chosen.isValid())
			return;
		k.r = chosen.red();
		k.g = chosen.green();
		k.b = chosen.blue();
		changed();
	}
	void mouseDoubleClickEvent(QMouseEvent *event) override
	{
		if (keyAt(event->position().x()) >= 0 || keys.size() >= static_cast<std::size_t>(MapEditorCore::PARTICLE_KEYS))
			return;
		const int frame = std::max(1, axis.frameAt(event->position().x(), width()));
		keys.push_back(MapEditorCore::colorAt(keys, frame));
		keys.back().frame = frame;
		changed();
	}

private:
	int m_drag = -1;
	bool m_moved = false;
	int keyAt(double px) const
	{
		for (std::size_t i = keys.size(); i-- > 0;)
			if (std::abs(axis.x(keys[i].frame, width()) - px) <= 6.0)
				return static_cast<int>(i);
		return -1;
	}
	void changed()
	{
		update();
		if (onChanged)
			onChanged();
	}
};

// The alpha key frames as a curve: each key has a range (low..high, the game picks a value in it per
// particle), drawn as a band. The upper handle sets high, the lower one low; with Shift, or where both
// are equal, the handle moves both. Dragging sideways moves a key in time; double click adds, right
// click removes.
class AlphaCurve : public QWidget
{
public:
	std::vector<MapEditorCore::AlphaKey> keys;
	FrameAxis axis;
	std::function<void()> onChanged;

	AlphaCurve()
	{
		setMinimumHeight(120);
		setCursor(Qt::PointingHandCursor);
		setToolTip(T("Drag a point up or down to change the alpha, sideways to move it in time; with Shift both ends of the range move. Double-click adds a key, right-click removes one."));
	}

protected:
	double yOf(float alpha) const { return 6.0 + (height() - 12.0) * (1.0 - std::clamp(static_cast<double>(alpha), 0.0, 1.0)); }
	float alphaAt(double py) const { return static_cast<float>(std::clamp(1.0 - (py - 6.0) / std::max(1.0, height() - 12.0), 0.0, 1.0)); }

	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const double w = width();
		p.setPen(QPen(palette().color(QPalette::Mid), 1.0, Qt::DotLine));
		for (const float level : {0.0f, 0.5f, 1.0f})
			p.drawLine(QPointF(FrameAxis::MARGIN, yOf(level)), QPointF(w - FrameAxis::MARGIN, yOf(level)));
		if (keys.empty())
			return;
		// The band from the last key on holds its value to the end of the axis.
		QPolygonF band;
		for (const MapEditorCore::AlphaKey &k : keys)
			band << QPointF(axis.x(k.frame, w), yOf(k.high));
		band << QPointF(w - FrameAxis::MARGIN, yOf(keys.back().high)) << QPointF(w - FrameAxis::MARGIN, yOf(keys.back().low));
		for (std::size_t i = keys.size(); i-- > 0;)
			band << QPointF(axis.x(keys[i].frame, w), yOf(keys[i].low));
		QColor fill = palette().color(QPalette::Highlight);
		fill.setAlpha(70);
		p.setBrush(fill);
		p.setPen(QPen(palette().color(QPalette::Highlight), 1.5));
		p.drawPolygon(band);
		for (std::size_t i = 0; i < keys.size(); ++i)
		{
			const double x = axis.x(keys[i].frame, w);
			p.setBrush(palette().color(QPalette::Base));
			p.setPen(QPen(static_cast<int>(i) == m_drag ? palette().color(QPalette::Highlight) : palette().color(QPalette::Text), 1.5));
			p.drawEllipse(QPointF(x, yOf(keys[i].high)), 4.5, 4.5);
			if (keys[i].low != keys[i].high)
				p.drawRect(QRectF(x - 4, yOf(keys[i].low) - 4, 8, 8));
		}
	}
	void mousePressEvent(QMouseEvent *event) override
	{
		m_drag = -1;
		m_moved = false;
		for (std::size_t i = keys.size(); i-- > 0 && m_drag < 0;)
		{
			const double x = axis.x(keys[i].frame, width());
			if (std::abs(x - event->position().x()) > 7.0)
				continue;
			if (std::abs(yOf(keys[i].high) - event->position().y()) <= 7.0)
			{
				m_drag = static_cast<int>(i);
				m_end = keys[i].low == keys[i].high ? End::Both : End::High;
			}
			else if (std::abs(yOf(keys[i].low) - event->position().y()) <= 7.0)
			{
				m_drag = static_cast<int>(i);
				m_end = End::Low;
			}
		}
		if (event->button() == Qt::RightButton && m_drag > 0)
		{
			keys.erase(keys.begin() + m_drag);
			m_drag = -1;
			changed();
		}
		update();
	}
	void mouseMoveEvent(QMouseEvent *event) override
	{
		if (m_drag < 0 || !(event->buttons() & Qt::LeftButton))
			return;
		MapEditorCore::AlphaKey &k = keys[static_cast<std::size_t>(m_drag)];
		const float value = alphaAt(event->position().y());
		const bool both = m_end == End::Both || (event->modifiers() & Qt::ShiftModifier);
		if (both)
		{
			// The range keeps its width.
			const float spread = k.high - k.low;
			k.high = std::clamp(value, spread, 1.0f);
			k.low = k.high - spread;
		}
		else if (m_end == End::High)
		{
			k.high = std::max(value, k.low);
		}
		else
		{
			k.low = std::min(value, k.high);
		}
		if (m_drag > 0)
			k.frame = std::max(1, axis.frameAt(event->position().x(), width()));
		m_moved = true;
		update();
	}
	void mouseReleaseEvent(QMouseEvent *) override
	{
		const bool moved = m_drag >= 0 && m_moved;
		m_drag = -1;
		if (moved)
			changed();
		update();
	}
	void mouseDoubleClickEvent(QMouseEvent *event) override
	{
		if (keys.size() >= static_cast<std::size_t>(MapEditorCore::PARTICLE_KEYS))
			return;
		const float value = alphaAt(event->position().y());
		keys.push_back(MapEditorCore::AlphaKey{value, value, std::max(1, axis.frameAt(event->position().x(), width()))});
		changed();
	}

private:
	enum class End
	{
		High,
		Low,
		Both
	};
	int m_drag = -1;
	End m_end = End::Both;
	bool m_moved = false;
	void changed()
	{
		update();
		if (onChanged)
			onChanged();
	}
};

} // namespace MapEditorApp
