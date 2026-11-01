/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MiniMap.h -- small picture of the whole map beside the 3D view: where the camera looks (a cone in
// its direction), and a click or drag moves the camera there.

#pragma once

#include "Model/Map.h"

#include <QImage>
#include <QWidget>

#include <functional>

namespace MapEditorApp
{

class MiniMap : public QWidget
{
public:
	MiniMap(const MapModel::Map &map, QWidget *parent = nullptr);

	// The camera looks at the world position, in the direction (radians from +x).
	void setCamera(float x, float y, float direction);
	// World position the user clicked or dragged to.
	std::function<void(float x, float y)> onJump;
	// World position of a widget position; false outside the picture.
	bool widgetToWorld(const QPointF &pos, float &x, float &y) const;

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;

private:
	QRectF pictureRect() const;
	QPointF worldToWidget(float x, float y) const;
	void jump(const QPointF &pos);

	const MapModel::Map &m_map;
	QImage m_image;
	float m_cameraX = 0.0f;
	float m_cameraY = 0.0f;
	float m_yaw = 0.0f;
};

} // namespace MapEditorApp
