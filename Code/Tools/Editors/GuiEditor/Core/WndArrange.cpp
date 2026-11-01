/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// WndArrange.cpp -- arranging several windows.

#include "Core/WndArrange.h"

#include <algorithm>
#include <cstdlib>

namespace WndModel
{

namespace
{

struct Placed
{
	Window *window = nullptr;
	Rect rect;
};

std::vector<Placed> placed(Layout &layout, const std::vector<WindowPath> &paths)
{
	std::vector<Placed> out;
	for (const WindowPath &p : paths)
		if (Window *w = windowAt(layout, p))
			if (const std::optional<Rect> r = w->rect())
				out.push_back({w, *r});
	return out;
}

bool moveTo(Placed &p, int left, int top)
{
	const int dx = left - p.rect.left;
	const int dy = top - p.rect.top;
	if (dx == 0 && dy == 0)
		return false;
	offsetWindow(*p.window, dx, dy);
	return true;
}

// The three positions of an axis: start, centre, end.
void positions(int a, int b, int out[3])
{
	out[0] = a;
	out[1] = (a + b) / 2;
	out[2] = b;
}

} // namespace

int alignWindows(Layout &layout, const std::vector<WindowPath> &paths, Align align)
{
	std::vector<Placed> windows = placed(layout, paths);
	if (windows.size() < 2)
		return 0;
	const Rect key = windows.back().rect;
	int moved = 0;
	for (std::size_t i = 0; i + 1 < windows.size(); ++i)
	{
		Placed &p = windows[i];
		const int width = p.rect.right - p.rect.left;
		const int height = p.rect.bottom - p.rect.top;
		int left = p.rect.left;
		int top = p.rect.top;
		switch (align)
		{
		case Align::Left: left = key.left; break;
		case Align::Right: left = key.right - width; break;
		case Align::Top: top = key.top; break;
		case Align::Bottom: top = key.bottom - height; break;
		case Align::CenterX: left = (key.left + key.right) / 2 - width / 2; break;
		case Align::CenterY: top = (key.top + key.bottom) / 2 - height / 2; break;
		}
		moved += moveTo(p, left, top) ? 1 : 0;
	}
	return moved;
}

int distributeWindows(Layout &layout, const std::vector<WindowPath> &paths, bool horizontal)
{
	std::vector<Placed> windows = placed(layout, paths);
	if (windows.size() < 3)
		return 0;
	const auto start = [horizontal](const Placed &p) { return horizontal ? p.rect.left : p.rect.top; };
	const auto size = [horizontal](const Placed &p) {
		return horizontal ? p.rect.right - p.rect.left : p.rect.bottom - p.rect.top;
	};
	std::stable_sort(windows.begin(), windows.end(), [&](const Placed &a, const Placed &b) { return start(a) < start(b); });
	int total = 0;
	for (const Placed &p : windows)
		total += size(p);
	const int first = start(windows.front());
	const int last = start(windows.back()) + size(windows.back());
	const int gaps = static_cast<int>(windows.size()) - 1;
	const int free = last - first - total;
	int at = first;
	int moved = 0;
	for (int i = 0; i <= gaps; ++i)
	{
		Placed &p = windows[static_cast<std::size_t>(i)];
		// The gaps share the free space; the remainder goes to the first ones, the last window stays.
		const int pos = i == gaps ? start(p) : at;
		moved += moveTo(p, horizontal ? pos : p.rect.left, horizontal ? p.rect.top : pos) ? 1 : 0;
		at = pos + size(p) + free / gaps + (i < free % gaps ? 1 : 0);
	}
	return moved;
}

int sameSize(Layout &layout, const std::vector<WindowPath> &paths, bool width, bool height)
{
	std::vector<Placed> windows = placed(layout, paths);
	if (windows.size() < 2)
		return 0;
	const Rect key = windows.back().rect;
	int changed = 0;
	for (std::size_t i = 0; i + 1 < windows.size(); ++i)
	{
		Rect r = windows[i].rect;
		if (width)
			r.right = r.left + (key.right - key.left);
		if (height)
			r.bottom = r.top + (key.bottom - key.top);
		if (r.right != windows[i].rect.right || r.bottom != windows[i].rect.bottom)
		{
			windows[i].window->setRect(r);
			++changed;
		}
	}
	return changed;
}

GuideSnap snapToGuides(const Rect &moving, const std::vector<Rect> &others, int tolerance)
{
	GuideSnap snap;
	int best[2] = {tolerance + 1, tolerance + 1};
	int mine[2][3];
	positions(moving.left, moving.right, mine[0]);
	positions(moving.top, moving.bottom, mine[1]);
	for (const Rect &o : others)
	{
		int theirs[2][3];
		positions(o.left, o.right, theirs[0]);
		positions(o.top, o.bottom, theirs[1]);
		for (int axis = 0; axis < 2; ++axis)
			for (int a = 0; a < 3; ++a)
				for (int b = 0; b < 3; ++b)
				{
					const int d = theirs[axis][b] - mine[axis][a];
					if (std::abs(d) < best[axis])
					{
						best[axis] = std::abs(d);
						if (axis == 0)
						{
							snap.dx = d;
							snap.lineX = theirs[axis][b];
						}
						else
						{
							snap.dy = d;
							snap.lineY = theirs[axis][b];
						}
					}
				}
	}
	return snap;
}

} // namespace WndModel
