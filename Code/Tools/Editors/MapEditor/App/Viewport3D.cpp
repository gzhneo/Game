/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Viewport3D.cpp -- camera control and on-demand drawing of the engine's 3D view.

#include "App/Viewport3D.h"

#include "App/EngineClient.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace MapEditorApp
{

namespace
{

constexpr float HORIZONTAL_FOV = 50.0f * 3.14159265f / 180.0f;
constexpr float TURN_PER_PIXEL = 0.006f;

} // namespace

Viewport3D::Viewport3D(QWidget *mainWindow) : QWidget(mainWindow)
{
	setAttribute(Qt::WA_NativeWindow);
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setMouseTracking(true);
	setFocusPolicy(Qt::StrongFocus);
	hide();
}

bool Viewport3D::ensureDevice(QString *error)
{
	if (m_device)
		return true;
	EngineClient &engine = EngineClient::instance();
	if (!engine.isStarted())
		return false;
	m_device = engine.attach(reinterpret_cast<void *>(winId()), std::max(1, devicePixels(width())), std::max(1, devicePixels(height())), error);
	return m_device;
}

void Viewport3D::trackSlot(QWidget *slot)
{
	if (m_slot)
		m_slot->removeEventFilter(this);
	m_slot = slot;
	if (m_slot)
		m_slot->installEventFilter(this);
	syncToSlot();
}

void Viewport3D::syncToSlot()
{
	if (!m_slot || !m_slot->isVisible() || !m_device)
	{
		hide();
		return;
	}
	const QPoint topLeft = m_slot->mapTo(parentWidget(), QPoint(0, 0));
	setGeometry(QRect(topLeft, m_slot->size()));
	show();
	raise();
	update();
}

bool Viewport3D::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == m_slot)
	{
		switch (event->type())
		{
		case QEvent::Move:
		case QEvent::Resize:
		case QEvent::Show:
		case QEvent::Hide:
			syncToSlot();
			break;
		case QEvent::Destroy:
			m_slot = nullptr;
			hide();
			break;
		default:
			break;
		}
	}
	return QWidget::eventFilter(watched, event);
}

int Viewport3D::devicePixels(qreal logical) const
{
	return static_cast<int>(std::lround(logical * devicePixelRatioF()));
}

void Viewport3D::paintEvent(QPaintEvent *)
{
	if (m_device)
		EngineClient::instance().render();
}

void Viewport3D::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	if (m_device)
		EngineClient::instance().resize(std::max(1, devicePixels(width())), std::max(1, devicePixels(height())));
	update();
}

void Viewport3D::resetCamera()
{
	EngineClient::instance().resetCamera();
	update();
}

void Viewport3D::showWholeMap()
{
	if (m_info.worldWidth <= 0.0f || m_info.worldHeight <= 0.0f)
		return;
	MeCamera c{};
	c.targetX = m_info.worldWidth * 0.5f;
	c.targetY = m_info.worldHeight * 0.5f;
	c.targetZ = 0.0f;
	c.yaw = 0.0f;
	c.pitch = 1.35f;
	// Fit both sides into the field of view; the margin covers the perspective of the tilted camera.
	const float aspect = height() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;
	const float halfH = std::tan(HORIZONTAL_FOV * 0.5f);
	const float halfV = halfH / std::max(0.1f, aspect);
	// The border (outside the playable area, 10 world units per cell) is shown too.
	const float border = 2.0f * 10.0f * static_cast<float>(m_info.border);
	c.distance = 1.2f * std::max((m_info.worldWidth + border) * 0.5f / halfH, (m_info.worldHeight + border) * 0.5f / halfV);
	EngineClient::instance().setCamera(c);
	update();
}

bool Viewport3D::captureFrame(const QString &bmpPath)
{
	if (!m_device)
		return false;
	EngineClient &engine = EngineClient::instance();
	engine.capture(bmpPath);
	engine.render();
	return true;
}

bool Viewport3D::pickCenter(MePickResult *result)
{
	return m_device && EngineClient::instance().pickObject(devicePixels(width() * 0.5), devicePixels(height() * 0.5), result);
}

void Viewport3D::pan(float dxPixels, float dyPixels)
{
	EngineClient &engine = EngineClient::instance();
	MeCamera c = engine.camera();
	// World units per pixel at the target distance.
	const float perPixel = 2.0f * c.distance * std::tan(HORIZONTAL_FOV * 0.5f) / static_cast<float>(std::max(1, width()));
	const float right = -dxPixels * perPixel;
	const float forward = dyPixels * perPixel / std::max(0.2f, std::sin(c.pitch));
	c.targetX += right * std::cos(c.yaw) + forward * std::sin(c.yaw);
	c.targetY += -right * std::sin(c.yaw) + forward * std::cos(c.yaw);
	engine.setCamera(c);
}

void Viewport3D::hover(const QPointF &pos)
{
	if (!onHover)
		return;
	float world[3] = {};
	if (EngineClient::instance().pick(devicePixels(pos.x()), devicePixels(pos.y()), world))
		onHover(true, world[0], world[1], world[2]);
	else
		onHover(false, 0, 0, 0);
}

bool Viewport3D::groundAt(const QPointF &pos, float &x, float &y)
{
	float world[3] = {};
	if (!EngineClient::instance().pick(devicePixels(pos.x()), devicePixels(pos.y()), world))
		return false;
	x = world[0];
	y = world[1];
	return true;
}

void Viewport3D::endObjectDrag(bool cancel)
{
	if (m_draggingObject)
	{
		if (m_rotating)
		{
			if (onRotateSelectionEnd)
				onRotateSelectionEnd(cancel);
		}
		else if (onDragSelectionEnd)
			onDragSelectionEnd(cancel);
		update();
	}
	m_pressObject = -1;
	m_draggingObject = false;
	m_rotating = false;
	m_rotateMode = false;
}

void Viewport3D::updateMarquee(const QPointF &to)
{
	EngineClient::instance().setMarquee(devicePixels(m_pressPos.x()), devicePixels(m_pressPos.y()), devicePixels(to.x()), devicePixels(to.y()));
	update();
}

void Viewport3D::setPlacing(bool placing)
{
	m_placing = placing;
	m_placePress = false;
	if (placing || m_toolMode)
		setCursor(Qt::CrossCursor);
	else
		unsetCursor();
}

void Viewport3D::setToolMode(bool on)
{
	m_toolMode = on;
	m_toolDown = false;
	if (on || m_placing)
		setCursor(Qt::CrossCursor);
	else
		unsetCursor();
}

void Viewport3D::emitTool(ToolEvent::Kind kind, const QPointF &pos, Qt::KeyboardModifiers mods)
{
	ToolEvent e;
	e.kind = kind;
	if (!onTool || !groundAt(pos, e.x, e.y))
		return;
	// About eight pixels at the distance of the camera target (horizontal field of view 50 degrees).
	const MeCamera c = EngineClient::instance().camera();
	e.tolerance = 8.0f * 2.0f * std::tan(0.4363f) * c.distance / static_cast<float>(std::max(1, width()));
	e.ctrl = (mods & Qt::ControlModifier) != 0;
	e.shift = (mods & Qt::ShiftModifier) != 0;
	e.alt = (mods & Qt::AltModifier) != 0;
	onTool(e);
}

void Viewport3D::mousePressEvent(QMouseEvent *event)
{
	setFocus();
	m_dragButton = event->button();
	m_last = event->position();
	m_pressPos = event->position();
	if (m_toolMode && m_dragButton == Qt::LeftButton)
	{
		m_toolDown = true;
		emitTool(ToolEvent::Kind::Press, m_pressPos, event->modifiers());
		return;
	}
	if (m_placing && m_dragButton == Qt::LeftButton)
	{
		m_placePress = groundAt(m_pressPos, m_placeX, m_placeY);
		m_placeCtrl = (event->modifiers() & Qt::ControlModifier) != 0;
		MePickResult hit{};
		m_placeObject = EngineClient::instance().pickObject(devicePixels(m_pressPos.x()), devicePixels(m_pressPos.y()), &hit) ? hit.object : -1;
		return;
	}
	m_pressCtrl = (event->modifiers() & Qt::ControlModifier) != 0;
	m_rotateMode = (event->modifiers() & Qt::ShiftModifier) != 0;
	if (m_dragButton == Qt::LeftButton)
	{
		MePickResult hit{};
		if (EngineClient::instance().pickObject(devicePixels(m_last.x()), devicePixels(m_last.y()), &hit))
		{
			m_pressObject = hit.object;
			m_objectStartX = hit.x;
			m_objectStartY = hit.y;
			// The object keeps its offset to the point under the cursor, so it does not jump.
			float gx = 0.0f, gy = 0.0f;
			m_grabX = m_grabY = 0.0f;
			if (groundAt(m_last, gx, gy))
			{
				m_grabX = hit.x - gx;
				m_grabY = hit.y - gy;
			}
			m_rotateStartX = gx;
			m_rotateStartY = gy;
			// Shift+Ctrl turns with snapping; it does not change the selection.
			if (onClickObject)
				onClickObject(hit.object, m_pressCtrl && !m_rotateMode, false);
			update();
			return;
		}
		if (onClickObject)
			onClickObject(-1, m_pressCtrl, false);
		m_marquee = true;
		update();
		return;
	}
	setCursor(m_dragButton == Qt::RightButton ? Qt::SizeAllCursor : Qt::ClosedHandCursor);
}

void Viewport3D::mouseMoveEvent(QMouseEvent *event)
{
	const QPointF d = event->position() - m_last;
	m_last = event->position();
	if (m_toolDown)
	{
		emitTool(ToolEvent::Kind::Move, event->position(), event->modifiers());
		hover(event->position());
		return;
	}
	if (m_dragButton == Qt::LeftButton && m_marquee)
	{
		updateMarquee(event->position());
		hover(event->position());
		return;
	}
	// A Ctrl+click only changes the selection; it never drags (Shift+Ctrl turns with snapping).
	if (m_dragButton == Qt::LeftButton && m_pressObject >= 0 && (!m_pressCtrl || m_rotateMode))
	{
		const QPointF moved = event->position() - m_pressPos;
		if (!m_draggingObject && std::abs(moved.x()) + std::abs(moved.y()) > 3.0)
		{
			m_draggingObject = true;
			m_rotating = m_rotateMode;
			setCursor(m_rotating ? Qt::CrossCursor : Qt::SizeAllCursor);
		}
		float gx = 0.0f, gy = 0.0f;
		if (m_draggingObject && groundAt(event->position(), gx, gy))
		{
			if (m_rotating)
			{
				if (onRotateSelection)
					onRotateSelection(m_rotateStartX, m_rotateStartY, gx, gy, (event->modifiers() & Qt::ControlModifier) != 0);
			}
			else if (onDragSelection)
				onDragSelection(gx + m_grabX - m_objectStartX, gy + m_grabY - m_objectStartY);
			update();
		}
		hover(event->position());
		return;
	}
	if (m_dragButton == Qt::RightButton)
	{
		EngineClient &engine = EngineClient::instance();
		MeCamera c = engine.camera();
		c.yaw += static_cast<float>(d.x()) * TURN_PER_PIXEL;
		c.pitch += static_cast<float>(d.y()) * TURN_PER_PIXEL;
		engine.setCamera(c);
		update();
	}
	else if (m_dragButton == Qt::MiddleButton)
	{
		pan(static_cast<float>(d.x()), static_cast<float>(d.y()));
		update();
	}
	hover(event->position());
}

void Viewport3D::mouseReleaseEvent(QMouseEvent *event)
{
	if (m_toolDown && event->button() == Qt::LeftButton)
	{
		m_toolDown = false;
		m_dragButton = Qt::NoButton;
		emitTool(ToolEvent::Kind::Release, event->position(), event->modifiers());
		update();
		return;
	}
	if (m_placePress && m_dragButton == Qt::LeftButton)
	{
		m_placePress = false;
		m_dragButton = Qt::NoButton;
		const QPointF moved = event->position() - m_pressPos;
		float gx = m_placeX, gy = m_placeY;
		const bool aimed = std::abs(moved.x()) + std::abs(moved.y()) > 6.0 && groundAt(event->position(), gx, gy);
		if (onPlace)
			onPlace(m_placeX, m_placeY, std::atan2(gy - m_placeY, gx - m_placeX), aimed, m_placeObject, m_placeCtrl);
		update();
		return;
	}
	if (m_dragButton == Qt::LeftButton)
	{
		if (m_marquee)
		{
			m_marquee = false;
			EngineClient &engine = EngineClient::instance();
			engine.setMarquee(0, 0, 0, 0);
			const QPointF size = event->position() - m_pressPos;
			if (std::abs(size.x()) >= 4.0 && std::abs(size.y()) >= 4.0 && onSelectRect)
			{
				onSelectRect(engine.objectsInRect(devicePixels(m_pressPos.x()), devicePixels(m_pressPos.y()),
					devicePixels(event->position().x()), devicePixels(event->position().y())), m_pressCtrl);
			}
			update();
		}
		else if (m_pressObject >= 0 && !m_draggingObject && onClickObject)
			onClickObject(m_pressObject, m_pressCtrl && !m_rotateMode, true);
		endObjectDrag(false);
	}
	m_dragButton = Qt::NoButton;
	if (m_placing || m_toolMode)
		setCursor(Qt::CrossCursor);
	else
		unsetCursor();
}

void Viewport3D::mouseDoubleClickEvent(QMouseEvent *event)
{
	if (m_toolMode && event->button() == Qt::LeftButton)
	{
		emitTool(ToolEvent::Kind::DoubleClick, event->position(), event->modifiers());
		return;
	}
	resetCamera();
}

void Viewport3D::wheelEvent(QWheelEvent *event)
{
	EngineClient &engine = EngineClient::instance();
	MeCamera c = engine.camera();
	const float factor = std::pow(0.999f, static_cast<float>(event->angleDelta().y()));
	// Zoom toward the point under the cursor: move the target part of the way there.
	float world[3] = {};
	if (engine.pick(devicePixels(event->position().x()), devicePixels(event->position().y()), world))
	{
		const float pull = 1.0f - factor;
		c.targetX += (world[0] - c.targetX) * pull;
		c.targetY += (world[1] - c.targetY) * pull;
		c.targetZ += (world[2] - c.targetZ) * pull;
	}
	c.distance *= factor;
	engine.setCamera(c);
	update();
	hover(event->position());
}

void Viewport3D::keyPressEvent(QKeyEvent *event)
{
	const float step = 40.0f;
	if (m_toolMode && onToolKey && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Backspace))
	{
		onToolKey(event->key());
		return;
	}
	switch (event->key())
	{
	case Qt::Key_Left: pan(step, 0); break;
	case Qt::Key_Right: pan(-step, 0); break;
	case Qt::Key_Up: pan(0, step); break;
	case Qt::Key_Down: pan(0, -step); break;
	case Qt::Key_PageUp:
	case Qt::Key_PageDown:
	{
		EngineClient &engine = EngineClient::instance();
		MeCamera c = engine.camera();
		c.yaw += event->key() == Qt::Key_PageUp ? 0.1f : -0.1f;
		engine.setCamera(c);
		break;
	}
	case Qt::Key_Home: resetCamera(); return;
	case Qt::Key_Escape:
		if (m_draggingObject)
		{
			endObjectDrag(true);
			m_dragButton = Qt::NoButton;
			unsetCursor();
		}
		else if (m_marquee)
		{
			m_marquee = false;
			m_dragButton = Qt::NoButton;
			EngineClient::instance().setMarquee(0, 0, 0, 0);
			update();
		}
		else if (onEscape)
			onEscape();
		return;
	default: QWidget::keyPressEvent(event); return;
	}
	update();
}

void Viewport3D::leaveEvent(QEvent *)
{
	if (onHover)
		onHover(false, 0, 0, 0);
}

} // namespace MapEditorApp
