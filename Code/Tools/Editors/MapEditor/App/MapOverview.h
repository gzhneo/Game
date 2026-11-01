/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MapOverview.h -- top-down view of the open map, with the same selection and editing as the 3D
// view: click selects an object, Ctrl+click adds or removes it, dragging a selected object moves the
// selection, Shift+drag turns it (Ctrl snaps; Esc cancels), dragging on free ground draws a
// selection rectangle (Ctrl adds). Middle or right button pans, the wheel zooms at the cursor,
// double-click fits the map. Reports the cell and the world position under the cursor.
//
// The terrain is a picture made once; the objects and waypoint links are drawn from the model on every
// paint, so moves and edits show at once.

#pragma once

#include "App/Theme.h"
#include "App/ToolEvent.h"
#include "Core/ObjectCommands.h"
#include "Model/Map.h"

#include <QImage>
#include <QLineF>
#include <QVector>
#include <QPointF>
#include <QWidget>

#include <functional>
#include <set>
#include <string>
#include <cstdint>
#include <vector>

namespace MapEditorApp
{

class MapOverview : public QWidget
{
public:
	MapOverview(const MapModel::Map &map, const MapEditorCore::Selection &selection, const Tokens &tokens, QWidget *parent = nullptr);

	// Cell x, y (-1 outside), height step, world x, y.
	std::function<void(int, int, int, float, float)> onHover;
	// The same meaning as in Viewport3D.
	std::function<void(int object, bool toggle, bool released)> onClickObject;
	std::function<void(const std::vector<int> &objects, bool add)> onSelectRect;
	std::function<void(float dx, float dy)> onDragSelection;
	std::function<void(bool cancel)> onDragSelectionEnd;
	std::function<void(float startX, float startY, float x, float y, bool snap)> onRotateSelection;
	std::function<void(bool cancel)> onRotateSelectionEnd;
	std::function<void()> onEscape;
	// Placing new objects, as in Viewport3D.
	void setPlacing(bool placing);
	// object: the object under the cursor at the press (-1 if none); ctrl: Ctrl was held.
	std::function<void(float x, float y, float angle, bool aimed, int object, bool ctrl)> onPlace;
	// Tool mode (areas): the left button reports press, drag, release and double-click on the ground
	// instead of selecting; Enter and Backspace go to onToolKey.
	void setToolMode(bool on);
	std::function<void(const ToolEvent &)> onTool;
	std::function<void(int key)> onToolKey;
	// Areas drawn over the map: the selected one with its points (point: the selected point), and
	// the one being drawn (not in the map yet). The overview keeps the pointer; null clears it.
	void setAreaMarks(int area, int point, const MapModel::PolygonArea *draft);
	// Tool lines in world units, x1, y1, x2, y2 each (roads being drawn or selected).
	void setToolLines(std::vector<float> lines);
	// Brush outline at the world position with the radius in world units; hidden if not valid.
	void setBrushCircle(bool valid, float x, float y, float radius);
	// Draws the terrain picture again from the model's heights (and the impassable cells if shown).
	void refreshImage();
	// Shades the cells the map's cliff flags mark impassable.
	void setShowImpassable(bool show);
	// The slope of every cell as colours, green on flat ground to red at limit degrees (0: none);
	// drawn again with refreshImage.
	void setSlopes(float limit);
	// Colours over the cells (0xAARRGGBB per height map cell, alpha 0 for none), such as the
	// territories of the fairness report; empty clears them.
	void setTint(const std::vector<std::uint32_t> &colors);
	// Cells of the slope layer steeper than its limit (for the self test and the status line).
	int steepCellCount() const { return m_steepCells; }
	// Contour lines every step height units from offset (0: none); drawn again with refreshImage.
	void setContours(int step, int offset);
	// Grid every 10 cells when zoomed in.
	void setShowGrid(bool show);
	// Names of waypoints, named objects and areas.
	void setShowLabels(bool show);
	// Dots of the objects (waypoints and the selection stay).
	void setShowObjects(bool show);
	int contourLineCount() const { return static_cast<int>(m_contours.size()); }
	// Objects of these layers are neither drawn nor picked.
	void setHiddenLayers(std::set<std::string> layers);

	void fit();
	// Moves the view so the world position is in the middle, at the same zoom.
	void centerOn(float x, float y);
	// Object within a few pixels of the widget position, the nearest; -1 if none.
	int objectAt(const QPointF &pos) const;
	QPointF worldToWidget(float x, float y) const;
	QPointF widgetToWorld(const QPointF &pos) const;

protected:
	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void leaveEvent(QEvent *event) override;

private:
	QPointF toImage(const QPointF &widgetPos) const;
	void endObjectDrag(bool cancel);
	void report(const QPointF &pos);
	void emitTool(ToolEvent::Kind kind, const QPointF &pos, Qt::KeyboardModifiers mods);
	void buildImpassable();
	void buildSlopes();
	void buildContours();

	const MapModel::Map &m_map;
	const MapEditorCore::Selection &m_selection;
	Tokens m_tokens;
	QImage m_image;
	QImage m_impassable;		// red cells, one pixel per cell like m_image; null if not shown
	bool m_showImpassable = false;
	QImage m_slopes;			// slope colours, one pixel per cell like m_image; null if not shown
	QImage m_tint;				// colours set with setTint, one pixel per cell; null if none
	float m_slopeLimit = 0.0f;
	int m_steepCells = 0;
	int m_contourStep = 0;
	int m_contourOffset = 0;
	QVector<QLineF> m_contours;	// in image pixels
	bool m_showGrid = true;
	bool m_showLabels = false;
	bool m_showObjects = true;
	std::set<std::string> m_hiddenLayers;
	double m_scale = 1.0;
	QPointF m_offset;			// widget position of image pixel (0, 0)
	QPointF m_dragStart;
	QPointF m_offsetAtDrag;
	bool m_panning = false;
	bool m_userView = false;	// zoomed or panned by the user; resizing keeps the view

	// Left button: on an object (drag moves or turns the selection) or on free ground (rectangle).
	Qt::MouseButton m_button = Qt::NoButton;
	QPointF m_pressPos;
	bool m_pressCtrl = false;
	bool m_rotateMode = false;
	int m_pressObject = -1;
	bool m_draggingObject = false;
	bool m_marquee = false;
	QPointF m_marqueeEnd;
	float m_pressWorldX = 0.0f, m_pressWorldY = 0.0f;
	bool m_placing = false;
	bool m_placePress = false;
	bool m_toolMode = false;
	bool m_toolDown = false;
	int m_markArea = -1;
	int m_markPoint = -1;
	const MapModel::PolygonArea *m_draft = nullptr;
	std::vector<float> m_toolLines;
	bool m_brushValid = false;
	float m_brushX = 0.0f, m_brushY = 0.0f, m_brushRadius = 0.0f;
	int m_placeObject = -1;
	bool m_placeCtrl = false;
};

} // namespace MapEditorApp
