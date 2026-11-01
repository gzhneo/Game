/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Viewport3D.h -- the engine's 3D view as a native child window of the main window.
//
// The Direct3D device belongs to this window for the whole run, so the widget is a direct child of
// the main window and survives the page rebuilds (theme, language, game); it follows the geometry of
// a slot widget in the current map page. Drawing happens on demand: on changes and while the camera
// moves, otherwise the GPU rests.
//
// Mouse: left click selects an object, Ctrl+click adds or removes it, dragging a selected object
// moves the whole selection, Shift+drag turns it (Ctrl snaps; Esc cancels either), left drag on the
// ground draws a selection rectangle (Ctrl adds). Middle drag moves the camera, right button turns, wheel zooms to the cursor,
// double-click returns to the start camera. Keys: arrows move, Page Up/Down turn, Home start
// camera, Esc clears the selection.

#pragma once

#include "App/ToolEvent.h"
#include "Engine/EngineApi.h"

#include <QPointer>
#include <QWidget>

#include <functional>
#include <vector>

namespace MapEditorApp
{

class Viewport3D : public QWidget
{
public:
	explicit Viewport3D(QWidget *mainWindow);

	// Creates the device on first use; false with a reason if that fails.
	bool ensureDevice(QString *error);
	bool hasDevice() const { return m_device; }

	// Follows the geometry and visibility of the slot; null hides the view.
	void trackSlot(QWidget *slot);

	void resetCamera();
	// Nearly top-down view of the whole map (needs the map size, setMapInfo).
	void showWholeMap();
	void setMapInfo(const MeMapInfo &info) { m_info = info; }
	// Draws one frame and saves it (self test); returns false if there is no device.
	bool captureFrame(const QString &bmpPath);
	// Object under the centre of the view (self test).
	bool pickCenter(MePickResult *result);

	// World position under the cursor, or nothing (valid = false).
	std::function<void(bool valid, float x, float y, float z)> onHover;
	// Left button on an object (index into the map's objects) or on the ground (-1); toggle: Ctrl
	// held. Called on press, and with released = true when the button goes up without a drag.
	std::function<void(int object, bool toggle, bool released)> onClickObject;
	// Selection rectangle finished: the objects inside it; add: Ctrl held.
	std::function<void(const std::vector<int> &objects, bool add)> onSelectRect;
	// Dragging the selection: offset of the grabbed object since the press, then commit or cancel.
	std::function<void(float dx, float dy)> onDragSelection;
	std::function<void(bool cancel)> onDragSelectionEnd;
	// Turning the selection (Shift+drag): ground points under the cursor at the press and now;
	// snap: Ctrl held. Then commit or cancel.
	std::function<void(float startX, float startY, float x, float y, bool snap)> onRotateSelection;
	std::function<void(bool cancel)> onRotateSelectionEnd;
	// Esc while nothing is dragged.
	std::function<void()> onEscape;
	// Placing new objects: the left button marks the place, dragging aims the facing; the release
	// reports it (aimed: the button was dragged, angle then points from the place to the cursor).
	void setPlacing(bool placing);
	// object: the object under the cursor at the press (-1 if none); ctrl: Ctrl was held.
	std::function<void(float x, float y, float angle, bool aimed, int object, bool ctrl)> onPlace;
	// Tool mode (areas): the left button reports press, drag, release and double-click on the ground
	// instead of selecting; Enter and Backspace go to onToolKey.
	void setToolMode(bool on);
	std::function<void(const ToolEvent &)> onTool;
	std::function<void(int key)> onToolKey;

	virtual QPaintEngine *paintEngine() const override { return nullptr; }

protected:
	virtual bool eventFilter(QObject *watched, QEvent *event) override;
	virtual void paintEvent(QPaintEvent *event) override;
	virtual void resizeEvent(QResizeEvent *event) override;
	virtual void mousePressEvent(QMouseEvent *event) override;
	virtual void mouseMoveEvent(QMouseEvent *event) override;
	virtual void mouseReleaseEvent(QMouseEvent *event) override;
	virtual void mouseDoubleClickEvent(QMouseEvent *event) override;
	virtual void wheelEvent(QWheelEvent *event) override;
	virtual void keyPressEvent(QKeyEvent *event) override;
	virtual void leaveEvent(QEvent *event) override;

private:
	void syncToSlot();
	void pan(float dxPixels, float dyPixels);
	void hover(const QPointF &pos);
	bool groundAt(const QPointF &pos, float &x, float &y);
	void endObjectDrag(bool cancel);
	void updateMarquee(const QPointF &to);
	int devicePixels(qreal logical) const;
	void emitTool(ToolEvent::Kind kind, const QPointF &pos, Qt::KeyboardModifiers mods);

	QPointer<QWidget> m_slot;
	MeMapInfo m_info{};
	bool m_device = false;
	Qt::MouseButton m_dragButton = Qt::NoButton;
	QPointF m_last;
	QPointF m_pressPos;
	bool m_pressCtrl = false;
	// Object under the left button: index, start position, offset from the grabbed ground point.
	int m_pressObject = -1;
	bool m_draggingObject = false;
	float m_objectStartX = 0.0f, m_objectStartY = 0.0f;
	float m_grabX = 0.0f, m_grabY = 0.0f;
	// Left button on the ground: selection rectangle.
	bool m_marquee = false;
	// Shift on the press: the drag turns instead of moving; ground point at the press.
	bool m_rotateMode = false;
	bool m_placing = false;
	bool m_placePress = false;
	float m_placeX = 0.0f, m_placeY = 0.0f;
	int m_placeObject = -1;
	bool m_placeCtrl = false;
	bool m_rotating = false;
	bool m_toolMode = false;
	bool m_toolDown = false;
	float m_rotateStartX = 0.0f, m_rotateStartY = 0.0f;
};

} // namespace MapEditorApp
