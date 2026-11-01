/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// LayoutView.cpp -- the drawing area of the GUI editor.

#include "App/LayoutView.h"

#include "App/GuiEngine.h"
#include "Engine/EngineApi.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>
#include <utility>

namespace GuiEditorApp
{

LayoutView::LayoutView(QWidget *parent) : QWidget(parent)
{
	setAttribute(Qt::WA_NativeWindow);
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setFocusPolicy(Qt::StrongFocus);
}

bool LayoutView::ensureDevice(QString *error)
{
	if (m_device)
		return true;
	GuiEngine &engine = GuiEngine::instance();
	if (!engine.isStarted())
		return false;
	const qreal ratio = devicePixelRatioF();
	m_device = engine.attach(reinterpret_cast<void *>(winId()), std::max(1, static_cast<int>(width() * ratio)),
		std::max(1, static_cast<int>(height() * ratio)), error);
	return m_device;
}

void LayoutView::renderFrame()
{
	if (m_device)
		GuiEngine::instance().render(m_background);
}

void LayoutView::paintEvent(QPaintEvent *)
{
	renderFrame();
}

void LayoutView::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	if (!m_device)
		return;
	const qreal ratio = devicePixelRatioF();
	GuiEngine::instance().resize(std::max(1, static_cast<int>(width() * ratio)), std::max(1, static_cast<int>(height() * ratio)));
}

namespace
{

QPoint pixels(const QWidget *w, const QMouseEvent *event)
{
	const qreal ratio = w->devicePixelRatioF();
	return QPoint(static_cast<int>(event->position().x() * ratio), static_cast<int>(event->position().y() * ratio));
}

} // namespace

void LayoutView::mousePressEvent(QMouseEvent *event)
{
	setFocus(Qt::MouseFocusReason);
	if (onTestInput)
	{
		const QPoint p = pixels(this, event);
		if (event->button() == Qt::LeftButton)
			onTestInput(ME_INPUT_LEFT_DOWN, p.x(), p.y(), 0);
		else if (event->button() == Qt::RightButton)
			onTestInput(ME_INPUT_RIGHT_DOWN, p.x(), p.y(), 0);
		return;
	}
	if (event->button() == Qt::RightButton && onContextMenu)
	{
		const QPoint p = pixels(this, event);
		onContextMenu(p.x(), p.y(), event->globalPosition().toPoint());
		return;
	}
	if (event->button() != Qt::LeftButton || !onMouse)
		return;
	const QPoint p = pixels(this, event);
	onMouse(MouseEvent::Press, p.x(), p.y(), event->modifiers());
}

void LayoutView::mouseMoveEvent(QMouseEvent *event)
{
	if (onTestInput)
	{
		const QPoint p = pixels(this, event);
		onTestInput(ME_INPUT_MOVE, p.x(), p.y(), 0);
		return;
	}
	if (!(event->buttons() & Qt::LeftButton) || !onMouse)
		return;
	const QPoint p = pixels(this, event);
	onMouse(MouseEvent::Move, p.x(), p.y(), event->modifiers());
}

void LayoutView::mouseReleaseEvent(QMouseEvent *event)
{
	if (onTestInput)
	{
		const QPoint p = pixels(this, event);
		if (event->button() == Qt::LeftButton)
			onTestInput(ME_INPUT_LEFT_UP, p.x(), p.y(), 0);
		else if (event->button() == Qt::RightButton)
			onTestInput(ME_INPUT_RIGHT_UP, p.x(), p.y(), 0);
		return;
	}
	if (event->button() != Qt::LeftButton || !onMouse)
		return;
	const QPoint p = pixels(this, event);
	onMouse(MouseEvent::Release, p.x(), p.y(), event->modifiers());
}

void LayoutView::keyPressEvent(QKeyEvent *event)
{
	if (onTestInput && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier)))
	{
		static const std::pair<int, int> KEYS[] = {{Qt::Key_Backspace, ME_KEY_BACKSPACE}, {Qt::Key_Delete, ME_KEY_DELETE},
			{Qt::Key_Left, ME_KEY_LEFT}, {Qt::Key_Right, ME_KEY_RIGHT}, {Qt::Key_Up, ME_KEY_UP}, {Qt::Key_Down, ME_KEY_DOWN},
			{Qt::Key_Home, ME_KEY_HOME}, {Qt::Key_End, ME_KEY_END}, {Qt::Key_Tab, ME_KEY_TAB}};
		for (const auto &[qt, key] : KEYS)
			if (event->key() == qt)
			{
				onTestInput(ME_INPUT_KEY, 0, 0, key);
				return;
			}
		if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
		{
			onTestInput(ME_INPUT_CHAR, 0, 0, 13);
			return;
		}
		const QString text = event->text();
		if (!text.isEmpty() && text.at(0).isPrint())
		{
			for (const QChar c : text)
				onTestInput(ME_INPUT_CHAR, 0, 0, c.unicode());
			return;
		}
	}
	if (!onKey || !onKey(event->key(), event->modifiers()))
		QWidget::keyPressEvent(event);
}

void LayoutView::wheelEvent(QWheelEvent *event)
{
	if (!onTestInput)
	{
		QWidget::wheelEvent(event);
		return;
	}
	const qreal ratio = devicePixelRatioF();
	onTestInput(ME_INPUT_WHEEL, static_cast<int>(event->position().x() * ratio), static_cast<int>(event->position().y() * ratio),
		event->angleDelta().y());
}

} // namespace GuiEditorApp
