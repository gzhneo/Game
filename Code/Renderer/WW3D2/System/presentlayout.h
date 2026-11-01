/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// presentlayout.h --------------------------------------------------------------------------------
//
// Where the rendered image lands in the window. The scene is rendered at the display resolution;
// the window can be larger or have another aspect ratio (fullscreen is a borderless window
// covering the monitor). The swap chain buffer then gets the aspect ratio of the window, the scene
// is copied 1:1 into its centre, the margins stay black, and DXGI stretches the buffer uniformly
// onto the window. If window and scene have the same aspect ratio (always in windowed mode), the
// buffer is exactly the scene.
// ------------------------------------------------------------------------------------------------

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef PRESENTLAYOUT_H
#define PRESENTLAYOUT_H

struct PresentLayout
{
	int	renderWidth;		///< scene size (display resolution)
	int	renderHeight;
	int	bufferWidth;		///< swap chain buffer size
	int	bufferHeight;
	int	offsetX;			///< scene position inside the buffer
	int	offsetY;
};

/// Buffer size and scene offset for a scene of render_width x render_height shown in a window
/// with a client area of output_width x output_height.
inline PresentLayout Compute_Present_Layout(int render_width, int render_height, int output_width, int output_height)
{
	PresentLayout layout;
	layout.renderWidth  = layout.bufferWidth  = render_width;
	layout.renderHeight = layout.bufferHeight = render_height;
	layout.offsetX = layout.offsetY = 0;
	if (render_width <= 0 || render_height <= 0 || output_width <= 0 || output_height <= 0)
		return layout;

	// Cross products compare the aspect ratios without rounding.
	const long long wide = static_cast<long long>(output_width) * render_height;
	const long long tall = static_cast<long long>(output_height) * render_width;
	const long long max_size = 16384;		// largest Direct3D 11 texture
	if (wide > tall) {
		// Window is wider: bars left and right.
		const long long width = (2 * wide + output_height) / (2LL * output_height);
		if (width > render_width && width <= max_size) {
			layout.bufferWidth = static_cast<int>(width);
			layout.offsetX = (layout.bufferWidth - render_width) / 2;
		}
	}
	else if (tall > wide) {
		// Window is taller: bars top and bottom.
		const long long height = (2 * tall + output_width) / (2LL * output_width);
		if (height > render_height && height <= max_size) {
			layout.bufferHeight = static_cast<int>(height);
			layout.offsetY = (layout.bufferHeight - render_height) / 2;
		}
	}
	return layout;
}

/// Converts a point in window client coordinates to scene coordinates. Inside the bars the point
/// is clamped to the scene edge. If the client area is exactly the scene, the point is unchanged.
inline void Present_Window_To_Render(const PresentLayout& layout, int client_width, int client_height, int& x, int& y)
{
	if (client_width <= 0 || client_height <= 0 || layout.renderWidth <= 0 || layout.renderHeight <= 0)
		return;
	if (client_width == layout.bufferWidth && client_height == layout.bufferHeight &&
		layout.offsetX == 0 && layout.offsetY == 0)
		return;

	// Pixel centre of the client pixel, scaled onto the buffer, then relative to the scene.
	long long bx = ((2LL * x + 1) * layout.bufferWidth)  / (2LL * client_width)  - layout.offsetX;
	long long by = ((2LL * y + 1) * layout.bufferHeight) / (2LL * client_height) - layout.offsetY;
	if (bx < 0) bx = 0;
	if (by < 0) by = 0;
	if (bx >= layout.renderWidth)  bx = layout.renderWidth  - 1;
	if (by >= layout.renderHeight) by = layout.renderHeight - 1;
	x = static_cast<int>(bx);
	y = static_cast<int>(by);
}

#endif // PRESENTLAYOUT_H
