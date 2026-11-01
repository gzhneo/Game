/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// LayoutView.h -- the drawing area of the GUI editor: a native window the engine draws the layout into
// with Direct3D 11, as the game would show it.

#pragma once

#include <QWidget>

#include <cstdint>
#include <functional>

namespace GuiEditorApp
{

class LayoutView : public QWidget
{
public:
	explicit LayoutView(QWidget *parent = nullptr);

	// Attaches the engine to this window; false with a reason.
	bool ensureDevice(QString *error);
	[[nodiscard]] bool hasDevice() const { return m_device; }
	void setBackground(std::uint32_t rgb) { m_background = rgb; }
	void renderFrame();

	QPaintEngine *paintEngine() const override { return nullptr; }

	// The mouse in the view, in pixels of the drawing.
	enum class MouseEvent
	{
		Press,
		Move,
		Release,
	};
	std::function<void(MouseEvent event, int x, int y, Qt::KeyboardModifiers modifiers)> onMouse;
	// The right mouse button: pixels of the drawing and the place on the screen for a menu.
	std::function<void(int x, int y, QPoint global)> onContextMenu;
	// A key the view has the focus for; true if it was used.
	std::function<bool(int key, Qt::KeyboardModifiers modifiers)> onKey;
	// Set in the test mode: every mouse and key event goes here instead (kind and data as meWindowsInput).
	std::function<void(int kind, int x, int y, int data)> onTestInput;

protected:
	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;

private:
	bool m_device = false;
	std::uint32_t m_background = 0x202428;
};

} // namespace GuiEditorApp
