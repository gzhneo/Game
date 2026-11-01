/*
**	Command & Conquer Generals(tm) / Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**	Modified by GZH Neo, Copyright (C) 2026 GZH Neo
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// EngineWindows.cpp -- the game's window system for the GUI editor: window layouts (.wnd) loaded with
// the game's reader and drawn by the game's gadget code into the attached view. The display draws its
// 2D primitives as W3DDisplay does (the drawing code is taken from there); the singletons are the set
// the old GUIEdit created (GUIEdit::init).

#include "Engine/EngineApi.h"
#include "Engine/EngineInternal.h"

#include "Common/FileSystem.h"
#include "Common/FunctionLexicon.h"
#include "Common/GlobalData.h"
#include "Common/file.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "GameLogic/RankInfo.h"
#include "GameClient/Display.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/HeaderTemplate.h"
#include "GameClient/Image.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/Line2D.h"
#include "GameClient/Mouse.h"
#include "W3DDevice/Common/W3DFunctionLexicon.h"
#include "W3DDevice/GameClient/W3DDisplayStringManager.h"
#include "W3DDevice/GameClient/W3DGameFont.h"
#include "W3DDevice/GameClient/W3DGameWindowManager.h"
#include "render2d.h"
#include "surfaceclass.h"
#include "texture.h"
#include "ww3d.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{

// The 2D drawing of W3DDisplay without its scene, views and debug display.
class WindowsDisplay : public Display
{
public:
	WindowsDisplay() : m_render(std::make_unique<Render2DClass>())
	{
		m_render->Enable_Screen_UV_Bias(false);
	}

	void setSize(int width, int height)
	{
		setWidth(width);
		setHeight(height);
		m_render->Set_Coordinate_Range(RectClass(0, 0, static_cast<float>(width), static_cast<float>(height)));
		// The text renderers of the display strings take the screen size from here (as W3DDisplay sets it).
		Render2DClass::Set_Screen_Resolution(RectClass(0, 0, static_cast<float>(width), static_cast<float>(height)));
	}

	virtual void draw(void) override {}

	virtual void drawLine(Int startX, Int startY, Int endX, Int endY, Real lineWidth, UnsignedInt lineColor) override
	{
		m_render->Reset();
		m_render->Enable_Texturing(FALSE);
		m_render->Add_Line(Vector2(startX, startY), Vector2(endX, endY), lineWidth, lineColor);
		m_render->Render();
	}

	virtual void drawLine(Int startX, Int startY, Int endX, Int endY, Real lineWidth, UnsignedInt lineColor1, UnsignedInt lineColor2) override
	{
		m_render->Reset();
		m_render->Enable_Texturing(FALSE);
		m_render->Add_Line(Vector2(startX, startY), Vector2(endX, endY), lineWidth, lineColor1, lineColor2);
		m_render->Render();
	}

	virtual void drawOpenRect(Int startX, Int startY, Int width, Int height, Real lineWidth, UnsignedInt lineColor) override
	{
		if (m_clipped)
		{
			const auto edge = [&](Int x0, Int y0, Int x1, Int y1) {
				ICoord2D start{x0, y0}, end{x1, y1}, a, b;
				if (ClipLine2D(&start, &end, &a, &b, &m_clip))
					drawLine(a.x, a.y, b.x, b.y, lineWidth, lineColor);
			};
			edge(startX, startY, startX, startY + height);
			edge(startX, startY, startX + width, startY);
			edge(startX + width, startY, startX + width, startY + height);
			edge(startX, startY + height, startX + width, startY + height);
			return;
		}
		m_render->Reset();
		m_render->Enable_Texturing(FALSE);
		m_render->Add_Outline(RectClass(startX, startY, startX + width, startY + height), lineWidth, lineColor);
		m_render->Render();
	}

	virtual void drawFillRect(Int startX, Int startY, Int width, Int height, UnsignedInt color) override
	{
		m_render->Reset();
		m_render->Enable_Texturing(FALSE);
		m_render->Add_Rect(RectClass(startX, startY, startX + width, startY + height), 0, 0, color);
		m_render->Render();
	}

	virtual void drawRectClock(Int, Int, Int, Int, Int, UnsignedInt) override {}
	virtual void drawRemainingRectClock(Int, Int, Int, Int, Int, UnsignedInt) override {}

	virtual void drawImage(const Image *image, Int startX, Int startY, Int endX, Int endY, Color color, DrawImageMode mode) override
	{
		if (image == nullptr)
			return;
		const Region2D *uv = image->getUV();
		m_render->Reset();
		m_render->Enable_Texturing(TRUE);
		bool alphaReset = false;
		switch (mode)
		{
		case DrawImageMode::DRAW_IMAGE_GRAYSCALE:
			m_render->Enable_Grayscale(true);
			break;
		case DrawImageMode::DRAW_IMAGE_ADDITIVE:
			m_render->Enable_Additive(true);
			alphaReset = true;
			break;
		case DrawImageMode::DRAW_IMAGE_SOLID:
			m_render->Enable_Additive(false);
			m_render->Enable_Alpha(false);
			alphaReset = true;
			break;
		default:
			break;
		}
		if (BitTest(image->getStatus(), IMAGE_STATUS_RAW_TEXTURE))
			m_render->Set_Texture(const_cast<TextureClass *>(static_cast<const TextureClass *>(image->getRawTextureData())));
		else
			m_render->Set_Texture(image->getFilename().str());

		RectClass screen(startX, startY, endX, endY);
		RectClass uvRect(uv->lo.x, uv->lo.y, uv->hi.x, uv->hi.y);
		const bool rotated = BitTest(image->getStatus(), IMAGE_STATUS_ROTATED_90_CLOCKWISE);
		if (m_clipped)
		{
			if (endX <= m_clip.lo.x || endY <= m_clip.lo.y)
				return;
			RectClass clipped;
			clipped.Left = std::max<float>(screen.Left, m_clip.lo.x);
			clipped.Right = std::min<float>(screen.Right, m_clip.hi.x);
			clipped.Top = std::max<float>(screen.Top, m_clip.lo.y);
			clipped.Bottom = std::min<float>(screen.Bottom, m_clip.hi.y);
			const float left = (clipped.Left - screen.Left) / screen.Width();
			const float right = (clipped.Right - screen.Left) / screen.Width();
			const float top = (clipped.Top - screen.Top) / screen.Height();
			const float bottom = (clipped.Bottom - screen.Top) / screen.Height();
			RectClass clippedUv;
			if (rotated)
			{
				clippedUv.Top = uvRect.Top + uvRect.Height() * left;
				clippedUv.Bottom = uvRect.Top + uvRect.Height() * right;
				clippedUv.Right = uvRect.Right - uvRect.Width() * top;
				clippedUv.Left = uvRect.Right - uvRect.Width() * bottom;
			}
			else
			{
				clippedUv.Left = uvRect.Left + uvRect.Width() * left;
				clippedUv.Right = uvRect.Left + uvRect.Width() * right;
				clippedUv.Top = uvRect.Top + uvRect.Height() * top;
				clippedUv.Bottom = uvRect.Top + uvRect.Height() * bottom;
			}
			screen = clipped;
			uvRect = clippedUv;
		}
		if (rotated)
		{
			m_render->Add_Tri(Vector2(screen.Left, screen.Top), Vector2(screen.Left, screen.Bottom), Vector2(screen.Right, screen.Top),
				Vector2(uvRect.Right, uvRect.Top), Vector2(uvRect.Left, uvRect.Top), Vector2(uvRect.Right, uvRect.Bottom), color);
			m_render->Add_Tri(Vector2(screen.Right, screen.Bottom), Vector2(screen.Right, screen.Top), Vector2(screen.Left, screen.Bottom),
				Vector2(uvRect.Left, uvRect.Bottom), Vector2(uvRect.Right, uvRect.Bottom), Vector2(uvRect.Left, uvRect.Top), color);
		}
		else
		{
			m_render->Add_Quad(screen, uvRect, color);
		}
		m_render->Render();
		m_render->Enable_Grayscale(false);
		if (alphaReset)
			m_render->Enable_Alpha(true);
	}

	virtual void setClipRegion(IRegion2D *region) override
	{
		m_clip = *region;
		m_clipped = TRUE;
	}
	virtual Bool isClippingEnabled(void) override { return m_clipped; }
	virtual void enableClipping(Bool onoff) override { m_clipped = onoff; }

	virtual VideoBuffer *createVideoBuffer(void) override { return nullptr; }
	virtual void drawVideoBuffer(VideoBuffer *, Int, Int, Int, Int) override {}
	virtual void takeScreenShot(void) override {}
	virtual void setTimeOfDay(TimeOfDay) override {}
	virtual void createLightPulse(const Coord3D *, const RGBColor *, Real, Real, UnsignedInt, UnsignedInt) override {}
	virtual void setShroudLevel(Int, Int, CellShroudStatus) override {}
	virtual void setBorderShroudLevel(UnsignedByte) override {}
	virtual void clearShroud() override {}
	virtual void preloadModelAssets(AsciiString) override {}
	virtual void preloadTextureAssets(AsciiString) override {}
	virtual void toggleLetterBox(void) override {}
	virtual void enableLetterBox(Bool) override {}
#if defined(_DEBUG)
	virtual void dumpModelAssets(const char *) override {}
	virtual void dumpAssetUsage(const char *) override {}
#endif
	virtual void doSmartAssetPurgeAndPreload(const char *) override {}
	virtual Real getAverageFPS(void) override { return 0.0f; }
	virtual Int getLastFrameDrawCalls(void) override { return 0; }

private:
	std::unique_ptr<Render2DClass> m_render;
	IRegion2D m_clip{};
	Bool m_clipped = FALSE;
};

bool s_started = false;
WindowsDisplay *s_display = nullptr;
bool s_ownPlayerList = false;
bool s_showHidden = false;
std::vector<std::string> s_highlight;	// NAME of the marked windows, as written
uint32_t s_highlightColor = 0xFFFFFFFF;
struct OverlayItem
{
	int left, top, right, bottom;
	uint32_t argb;
	bool filled;
};
std::vector<OverlayItem> s_overlay;	// handles and the selection band, in view pixels

// The mouse the window system asks for tooltips and the cursor; the input itself comes from meWindowsInput.
class EditorMouse : public Mouse
{
public:
	void initCursorResources() override {}
	void setCursor(MouseCursor) override {}
	void capture() override {}
	void releaseCapture() override {}

protected:
	UnsignedByte getMouseEvent(MouseIO *, Bool) override { return MOUSE_NONE; }
};

constexpr std::size_t MAX_EVENTS = 500;
std::vector<std::string> s_events;		// gadget messages of the test mode, NAME<TAB>message<TAB>value
bool s_leftDown = false;
ICoord2D s_mouse = {0, 0};
bool s_ownMouse = false;
TextureClass *s_background = nullptr;		// picture under the windows, null for none

// Copies text with its terminating zero if it fits; returns the length without the zero.
int32_t copyOut(const std::string &text, char *buffer, int32_t size)
{
	if (buffer && size > 0)
	{
		const std::size_t n = std::min<std::size_t>(text.size(), static_cast<std::size_t>(size) - 1);
		std::memcpy(buffer, text.data(), n);
		buffer[n] = '\0';
	}
	return static_cast<int32_t>(text.size());
}

std::string windowName(GameWindow *window)
{
	const WinInstanceData *data = window->winGetInstanceData();
	return data ? std::string(data->m_decoratedNameString.str()) : std::string();
}

// The children of a window that come from the layout file: a gadget makes its parts itself (they copy
// its name), except a tab control, whose panes the file holds.
GameWindow *fileChildren(GameWindow *window)
{
	const UnsignedInt style = window->winGetStyle();
	if ((style & GWS_GADGET_WINDOW) && !(style & GWS_TAB_CONTROL))
		return nullptr;
	return window->winGetChild();
}

void showAll(GameWindow *window)
{
	for (; window; window = window->winGetNext())
	{
		window->winHide(FALSE);
		showAll(window->winGetChild());
	}
}

bool contains(GameWindow *window, int x, int y)
{
	int left = 0, top = 0, width = 0, height = 0;
	window->winGetScreenPosition(&left, &top);
	window->winGetSize(&width, &height);
	return x >= left && y >= top && x < left + width && y < top + height;
}

// The deepest window of the file at the point; the head of a list is drawn on top. The parts of a gadget
// count as the gadget.
GameWindow *hit(GameWindow *list, int x, int y)
{
	for (GameWindow *window = list; window; window = window->winGetNext())
	{
		if (window->winIsHidden() || windowName(window).empty() || !contains(window, x, y))
			continue;
		GameWindow *child = hit(fileChildren(window), x, y);
		return child ? child : window;
	}
	return nullptr;
}

void collect(GameWindow *window, const std::string &name, std::vector<GameWindow *> &found)
{
	for (; window; window = window->winGetNext())
	{
		if (windowName(window) == name)
			found.push_back(window);
		collect(fileChildren(window), name, found);
	}
}

void drawHighlight()
{
	if (s_highlight.empty() || !s_display)
		return;
	std::vector<GameWindow *> windows;
	for (const std::string &name : s_highlight)
		collect(TheWindowManager->winGetWindowList(), name, windows);
	for (GameWindow *window : windows)
	{
		int left = 0, top = 0, width = 0, height = 0;
		window->winGetScreenPosition(&left, &top);
		window->winGetSize(&width, &height);
		s_display->drawOpenRect(left - 1, top - 1, width + 2, height + 2, 2.0f, s_highlightColor);
	}
}

void drawOverlay()
{
	if (!s_display)
		return;
	for (const OverlayItem &o : s_overlay)
	{
		if (o.filled)
			s_display->drawFillRect(o.left, o.top, o.right - o.left, o.bottom - o.top, o.argb);
		else
			s_display->drawOpenRect(o.left, o.top, o.right - o.left, o.bottom - o.top, 1.0f, o.argb);
	}
}

template <typename T>
void deleteSingleton(T *&p)
{
	delete p;
	p = nullptr;
}

// Windows of the layout file.
int countWindows(GameWindow *window)
{
	int n = 0;
	for (; window; window = window->winGetNext())
		n += (windowName(window).empty() ? 0 : 1) + countWindows(fileChildren(window));
	return n;
}

// The messages a gadget sends its window when the user works it, and what they carry.
const char *gadgetMessage(UnsignedInt msg)
{
	switch (msg)
	{
	case GBM_SELECTED: return "GBM_SELECTED";
	case GBM_SELECTED_RIGHT: return "GBM_SELECTED_RIGHT";
	case GSM_SLIDER_TRACK: return "GSM_SLIDER_TRACK";
	case GSM_SLIDER_DONE: return "GSM_SLIDER_DONE";
	case GLM_SELECTED: return "GLM_SELECTED";
	case GLM_DOUBLE_CLICKED: return "GLM_DOUBLE_CLICKED";
	case GLM_RIGHT_CLICKED: return "GLM_RIGHT_CLICKED";
	case GCM_SELECTED: return "GCM_SELECTED";
	case GCM_EDIT_DONE: return "GCM_EDIT_DONE";
	case GCM_UPDATE_TEXT: return "GCM_UPDATE_TEXT";
	case GEM_EDIT_DONE: return "GEM_EDIT_DONE";
	case GEM_UPDATE_TEXT: return "GEM_UPDATE_TEXT";
	default: return nullptr;
	}
}

std::string gadgetValue(GameWindow *gadget, UnsignedInt msg, WindowMsgData mData2)
{
	const UnsignedInt style = gadget->winGetStyle();
	if (msg == GEM_EDIT_DONE || msg == GEM_UPDATE_TEXT)
		return MapEditorEngine::utf8(GadgetTextEntryGetText(gadget).str());
	if (style & GWS_CHECK_BOX)
		return GadgetCheckBoxIsChecked(gadget) ? "1" : "0";
	if (msg == GSM_SLIDER_TRACK || msg == GSM_SLIDER_DONE || msg == GLM_SELECTED || msg == GLM_DOUBLE_CLICKED
		|| msg == GLM_RIGHT_CLICKED)
		return std::to_string(static_cast<long long>(mData2));
	return std::string();
}

// The system callback of user windows in the editor: what the default does, and the gadget messages noted.
WindowMsgHandledType recordingSystem(GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2)
{
	if (const char *name = gadgetMessage(msg); name && s_events.size() < MAX_EVENTS)
	{
		GameWindow *gadget = reinterpret_cast<GameWindow *>(mData1);
		if (gadget)
			s_events.push_back(windowName(gadget) + '\t' + name + '\t' + gadgetValue(gadget, msg, mData2));
	}
	return GameWinDefaultSystem(window, msg, mData1, mData2);
}

// Layouts name the game's callbacks; outside the game those touch state the editor does not have.
// User windows get the default callbacks, gadgets keep theirs (as the old GUIEdit did); the system
// callback also notes what the gadgets report, for the test mode.
void useDefaultCallbacks(GameWindow *window)
{
	for (; window; window = window->winGetNext())
	{
		if ((window->winGetStyle() & GWS_GADGET_WINDOW) == 0)
		{
			window->winSetSystemFunc(recordingSystem);
			window->winSetInputFunc(TheWindowManager->getDefaultInput());
			window->winSetTooltipFunc(TheWindowManager->getDefaultTooltip());
			window->winSetDrawFunc(TheWindowManager->getDefaultDraw());
		}
		useDefaultCallbacks(window->winGetChild());
	}
}

void syncDisplaySize()
{
	int width = 0, height = 0;
	MapEditorEngine::viewSize(width, height);
	if (s_display && width > 0 && height > 0 && (s_display->getWidth() != static_cast<UnsignedInt>(width)
		|| s_display->getHeight() != static_cast<UnsignedInt>(height)))
		s_display->setSize(width, height);
}

} // namespace

namespace MapEditorEngine
{

void stopWindows()
{
	if (!s_started)
		return;
	if (TheWindowManager)
	{
		TheWindowManager->winDestroyAll();
		TheWindowManager->update();
	}
	REF_PTR_RELEASE(s_background);
	deleteSingleton(::Engine::context().windowManager);
	if (s_ownMouse)
		deleteSingleton(::Engine::context().mouse);
	s_ownMouse = false;
	deleteSingleton(::Engine::context().headerTemplateManager);
	deleteSingleton(::Engine::context().displayStringManager);
	deleteSingleton(::Engine::context().mappedImageCollection);
	deleteSingleton(::Engine::context().fontLibrary);
	deleteSingleton(::Engine::context().functionLexicon);
	deleteSingleton(::Engine::context().globalLanguageData);
	if (s_ownPlayerList)
		deleteSingleton(::Engine::context().playerList);
	s_ownPlayerList = false;
	::Engine::context().display = nullptr;
	delete s_display;
	s_display = nullptr;
	s_started = false;
}

} // namespace MapEditorEngine

MEAPI int32_t meWindowsStart(char *error, int32_t errorSize)
{
	using MapEditorEngine::setError;
	if (s_started)
		return 1;
	if (!MapEditorEngine::engineStarted() || !MapEditorEngine::viewReady())
	{
		setError(error, errorSize, "the window system needs the engine and an attached view");
		return 0;
	}
	if (TheDisplay)
	{
		setError(error, errorSize, "another display is active");
		return 0;
	}
	try
	{
		::Engine::context().globalLanguageData = new GlobalLanguage;
		TheGlobalLanguageData->init();
		::Engine::context().functionLexicon = new W3DFunctionLexicon;
		TheFunctionLexicon->init();
		::Engine::context().fontLibrary = new W3DFontLibrary;
		TheFontLibrary->init();
		::Engine::context().mappedImageCollection = new ImageCollection;
		TheMappedImageCollection->init();
		TheMappedImageCollection->load(512);
		::Engine::context().displayStringManager = new W3DDisplayStringManager;
		TheDisplayStringManager->init();
		::Engine::context().headerTemplateManager = new HeaderTemplateManager;
		TheHeaderTemplateManager->init();
		// Players need the rank and player stores of the full start; the gadgets draw without them.
		if (!ThePlayerList && TheRankInfoStore && ThePlayerTemplateStore)
		{
			::Engine::context().playerList = new PlayerList;
			ThePlayerList->init();
			s_ownPlayerList = true;
		}
		s_display = new WindowsDisplay;
		::Engine::context().display = s_display;
		s_started = true;
		syncDisplaySize();
		::Engine::context().windowManager = new W3DGameWindowManager;
		TheWindowManager->init();
		if (!TheMouse)
		{
			::Engine::context().mouse = new EditorMouse;
			TheMouse->init();
			s_ownMouse = true;
		}
	}
	catch (const std::exception &e)
	{
		MapEditorEngine::stopWindows();
		setError(error, errorSize, std::string("window system failed: ") + e.what());
		return 0;
	}
	catch (...)
	{
		MapEditorEngine::stopWindows();
		setError(error, errorSize, "window system failed");
		return 0;
	}
	unsigned images = 0;
	while (TheMappedImageCollection->Enum(images))
		++images;
	MapEditorEngine::report("windows: started, " + std::to_string(images) + " images");
	return 1;
}

MEAPI int32_t meWindowsLoad(const char *path, char *error, int32_t errorSize)
{
	using MapEditorEngine::setError;
	if (!s_started || !path || !path[0])
	{
		setError(error, errorSize, "window system not started");
		return -1;
	}
	syncDisplaySize();
	TheWindowManager->winDestroyAll();
	TheWindowManager->update();
	WindowLayoutInfo info;
	GameWindow *first = nullptr;
	try
	{
		first = TheWindowManager->winCreateFromScript(AsciiString(path), &info);
	}
	catch (...)
	{
		first = nullptr;
	}
	if (!first)
	{
		setError(error, errorSize, std::string("layout not read: ") + path);
		return -1;
	}
	GameWindow *list = TheWindowManager->winGetWindowList();
	useDefaultCallbacks(list);
	s_events.clear();
	s_leftDown = false;
	if (s_showHidden)
		showAll(list);
	const int count = countWindows(list);
	MapEditorEngine::report(std::string("windows: ") + path + ", " + std::to_string(count) + " windows");
	return count;
}

MEAPI void meWindowsClear(void)
{
	if (!s_started)
		return;
	TheWindowManager->winDestroyAll();
	TheWindowManager->update();
}

MEAPI void meWindowsRender(uint32_t background)
{
	if (!s_started || !MapEditorEngine::viewReady())
		return;
	syncDisplaySize();
	TheWindowManager->update();
	WW3D::Sync(GetTickCount());
	const Vector3 clear(((background >> 16) & 0xFF) / 255.0f, ((background >> 8) & 0xFF) / 255.0f, (background & 0xFF) / 255.0f);
	if (WW3D::Begin_Render(true, true, clear) != WW3D_ERROR_OK)
		return;
	if (s_background && s_display)
	{
		const RectClass screen(0.0f, 0.0f, static_cast<float>(s_display->getWidth()), static_cast<float>(s_display->getHeight()));
		Render2DClass render;
		render.Set_Coordinate_Range(screen);
		render.Enable_Texturing(TRUE);
		render.Set_Texture(s_background);
		render.Add_Quad(screen, RectClass(0.0f, 0.0f, 1.0f, 1.0f));
		render.Render();
	}
	TheWindowManager->winRepaint();
	drawHighlight();
	drawOverlay();
	WW3D::End_Render();
}

MEAPI int32_t meWindowsLayouts(char *text, int32_t size)
{
	if (!TheFileSystem)
		return copyOut(std::string(), text, size);
	FilenameList files;
	TheFileSystem->getFileListInDirectory(AsciiString("Window\\"), AsciiString("*.wnd"), files, TRUE);
	std::set<std::string> names;
	for (const AsciiString &file : files)
	{
		std::string name = file.str();
		std::replace(name.begin(), name.end(), '\\', '/');
		const std::string lower = [&] {
			std::string l = name;
			std::transform(l.begin(), l.end(), l.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return l;
		}();
		const std::size_t at = lower.find("window/");
		names.insert(at == std::string::npos ? name : name.substr(at + 7));
	}
	std::string out;
	for (const std::string &name : names)
		out += name + '\n';
	return copyOut(out, text, size);
}

MEAPI int32_t meFileRead(const char *path, char *buffer, int32_t size)
{
	if (!TheFileSystem || !path || !path[0])
		return -1;
	File *file = TheFileSystem->openFile(path, File::READ | File::BINARY);
	if (!file)
		return -1;
	const Int length = file->size();
	if (buffer && size > 0 && length > 0)
		file->read(buffer, std::min<Int>(length, size));
	file->close();
	return length;
}

MEAPI void meWindowsShowHidden(int32_t show)
{
	s_showHidden = show != 0;
}

MEAPI int32_t meWindowsHit(int32_t x, int32_t y, char *name, int32_t size)
{
	if (!s_started)
		return copyOut(std::string(), name, size);
	GameWindow *window = hit(TheWindowManager->winGetWindowList(), x, y);
	return copyOut(window ? windowName(window) : std::string(), name, size);
}

MEAPI void meWindowsHighlight(const char *names, uint32_t argb)
{
	s_highlight.clear();
	s_highlightColor = argb;
	if (!names)
		return;
	std::string all = names;
	std::size_t start = 0;
	while (start < all.size())
	{
		const std::size_t end = std::min(all.find('\n', start), all.size());
		if (end > start)
			s_highlight.push_back(all.substr(start, end - start));
		start = end + 1;
	}
}

MEAPI void meWindowsOverlay(const int32_t *items, int32_t count)
{
	s_overlay.clear();
	for (int32_t i = 0; items && i < count; ++i)
	{
		const int32_t *v = items + i * 6;
		s_overlay.push_back({v[0], v[1], v[2], v[3], static_cast<uint32_t>(v[4]), v[5] != 0});
	}
}

MEAPI int32_t meWindowsRect(const char *name, int32_t *rect)
{
	if (!s_started || !name || !rect)
		return 0;
	std::vector<GameWindow *> found;
	collect(TheWindowManager->winGetWindowList(), name, found);
	if (found.empty())
		return 0;
	int left = 0, top = 0, width = 0, height = 0;
	found.front()->winGetScreenPosition(&left, &top);
	found.front()->winGetSize(&width, &height);
	rect[0] = left;
	rect[1] = top;
	rect[2] = left + width;
	rect[3] = top + height;
	return 1;
}

MEAPI int32_t meWindowsImages(char *text, int32_t size)
{
	std::vector<std::string> lines;
	if (s_started && TheMappedImageCollection)
		for (unsigned i = 0;; ++i)
		{
			const Image *image = TheMappedImageCollection->Enum(i);
			if (!image)
				break;
			const ICoord2D *dim = image->getImageSize();
			lines.push_back(std::string(image->getName().str()) + '\t' + std::to_string(dim ? dim->x : 0) + '\t'
				+ std::to_string(dim ? dim->y : 0));
		}
	std::sort(lines.begin(), lines.end());
	std::string out;
	for (const std::string &l : lines)
		out += l + '\n';
	return copyOut(out, text, size);
}

MEAPI int32_t meWindowsImagePreview(const char *name, int32_t size, uint8_t *rgba)
{
	if (!s_started || !TheMappedImageCollection || !name || !name[0])
		return 0;
	const Image *image = TheMappedImageCollection->findImageByName(AsciiString(name));
	if (!image || BitTest(image->getStatus(), IMAGE_STATUS_RAW_TEXTURE))
		return 0;
	const Region2D *uv = image->getUV();
	return MapEditorEngine::textureCrop(image->getFilename().str(), uv->lo.x, uv->lo.y, uv->hi.x, uv->hi.y, size, rgba) ? 1 : 0;
}

MEAPI int32_t meWindowsHeaderTemplates(char *text, int32_t size)
{
	std::vector<std::string> names;
	if (s_started && TheHeaderTemplateManager)
		for (HeaderTemplate *h = TheHeaderTemplateManager->getFirstHeader(); h; h = TheHeaderTemplateManager->getNextHeader(h))
			names.push_back(h->m_name.str());
	std::sort(names.begin(), names.end());
	std::string out;
	for (const std::string &n : names)
		out += n + '\n';
	return copyOut(out, text, size);
}

MEAPI int32_t meWindowsCallbacks(const char *kind, char *text, int32_t size)
{
	std::vector<FunctionLexicon::TableIndex> tables;
	const std::string k = kind ? kind : "";
	if (k == "system")
		tables = {FunctionLexicon::TABLE_GAME_WIN_SYSTEM};
	else if (k == "input")
		tables = {FunctionLexicon::TABLE_GAME_WIN_INPUT};
	else if (k == "tooltip")
		tables = {FunctionLexicon::TABLE_GAME_WIN_TOOLTIP};
	else if (k == "draw")
		tables = {FunctionLexicon::TABLE_GAME_WIN_DRAW, FunctionLexicon::TABLE_GAME_WIN_DEVICEDRAW};
	else if (k == "init")
		tables = {FunctionLexicon::TABLE_WIN_LAYOUT_INIT, FunctionLexicon::TABLE_WIN_LAYOUT_DEVICEINIT};
	else if (k == "update")
		tables = {FunctionLexicon::TABLE_WIN_LAYOUT_UPDATE};
	else if (k == "shutdown")
		tables = {FunctionLexicon::TABLE_WIN_LAYOUT_SHUTDOWN};
	std::set<std::string> names;
	if (s_started && TheFunctionLexicon)
		for (const FunctionLexicon::TableIndex t : tables)
			for (const FunctionLexicon::TableEntry *e = TheFunctionLexicon->getTable(t); e && e->name; ++e)
				names.insert(e->name);
	std::string out;
	for (const std::string &n : names)
		out += n + '\n';
	return copyOut(out, text, size);
}

MEAPI int32_t meWindowsTextKeys(char *text, int32_t size)
{
	std::vector<std::string> keys;
	if (TheGameText)
		for (const AsciiString &label : TheGameText->getStringsWithLabelPrefix(AsciiString("")))
			keys.push_back(label.str());
	std::sort(keys.begin(), keys.end());
	keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
	std::string out;
	for (const std::string &k : keys)
		out += k + '\n';
	return copyOut(out, text, size);
}

MEAPI int32_t meWindowsText(const char *key, char *text, int32_t size)
{
	if (!TheGameText || !key || !key[0])
		return -1;
	Bool exists = FALSE;
	const UnicodeString value = TheGameText->fetch(key, &exists);
	if (!exists)
		return -1;
	return copyOut(MapEditorEngine::utf8(value.str()), text, size);
}

MEAPI int32_t meWindowsInput(int32_t kind, int32_t x, int32_t y, int32_t data)
{
	if (!s_started || !TheWindowManager)
		return 0;
	ICoord2D pos = {x, y};
	WinInputReturnCode used = WinInputReturnCode::WIN_INPUT_NOT_USED;
	switch (kind)
	{
	case ME_INPUT_MOVE:
		if (s_leftDown)
		{
			ICoord2D delta = {x - s_mouse.x, y - s_mouse.y};
			used = TheWindowManager->winProcessMouseEvent(GWM_LEFT_DRAG, &pos, &delta);
		}
		else
			used = TheWindowManager->winProcessMouseEvent(GWM_MOUSE_POS, &pos, nullptr);
		break;
	case ME_INPUT_LEFT_DOWN:
		s_leftDown = true;
		used = TheWindowManager->winProcessMouseEvent(GWM_LEFT_DOWN, &pos, nullptr);
		break;
	case ME_INPUT_LEFT_UP:
		s_leftDown = false;
		used = TheWindowManager->winProcessMouseEvent(GWM_LEFT_UP, &pos, nullptr);
		break;
	case ME_INPUT_RIGHT_DOWN:
		used = TheWindowManager->winProcessMouseEvent(GWM_RIGHT_DOWN, &pos, nullptr);
		break;
	case ME_INPUT_RIGHT_UP:
		used = TheWindowManager->winProcessMouseEvent(GWM_RIGHT_UP, &pos, nullptr);
		break;
	case ME_INPUT_WHEEL:
	{
		Int wheel = data;
		used = TheWindowManager->winProcessMouseEvent(data > 0 ? GWM_WHEEL_UP : GWM_WHEEL_DOWN, &pos, &wheel);
		break;
	}
	case ME_INPUT_CHAR:
		if (GameWindow *focus = TheWindowManager->winGetFocus())
			used = TheWindowManager->winSendInputMsg(focus, GWM_IME_CHAR, static_cast<WindowMsgData>(data), 0)
					== WindowMsgHandledType::MSG_HANDLED
				? WinInputReturnCode::WIN_INPUT_USED
				: WinInputReturnCode::WIN_INPUT_NOT_USED;
		break;
	case ME_INPUT_KEY:
	{
		static const UnsignedByte KEYS[] = {KEY_NONE, KEY_BACKSPACE, KEY_DEL, KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN,
			KEY_HOME, KEY_END, KEY_TAB};
		if (data <= 0 || data >= static_cast<int32_t>(sizeof(KEYS)))
			break;
		used = TheWindowManager->winProcessKey(KEYS[data], KEY_STATE_DOWN);
		TheWindowManager->winProcessKey(KEYS[data], KEY_STATE_UP);
		break;
	}
	default:
		break;
	}
	s_mouse = pos;
	return used == WinInputReturnCode::WIN_INPUT_USED ? 1 : 0;
}

MEAPI int32_t meWindowsEvents(char *text, int32_t size)
{
	std::string out;
	for (const std::string &e : s_events)
		out += e + '\n';
	const int32_t length = copyOut(out, text, size);
	if (text && size > length)
		s_events.clear();
	return length;
}

MEAPI int32_t meWindowsBackgroundImage(const uint8_t *rgba, int32_t width, int32_t height)
{
	REF_PTR_RELEASE(s_background);
	if (!rgba || width <= 0 || height <= 0)
		return 1;
	if (!s_started)
		return 0;
	TextureClass *texture = MSGNEW("TextureClass") TextureClass(static_cast<unsigned>(width), static_cast<unsigned>(height),
		WW3D_FORMAT_A8R8G8B8, MIP_LEVELS_1, TextureClass::POOL_MANAGED, false, false);
	SurfaceClass *surface = texture->Get_Surface_Level();
	if (!surface)
	{
		REF_PTR_RELEASE(texture);
		return 0;
	}
	int pitch = 0;
	UnsignedByte *bits = static_cast<UnsignedByte *>(surface->Lock(&pitch));
	if (bits)
	{
		for (int32_t y = 0; y < height; ++y)
		{
			UnsignedInt *row = reinterpret_cast<UnsignedInt *>(bits + y * pitch);
			const uint8_t *in = rgba + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4;
			for (int32_t x = 0; x < width; ++x, in += 4)
				row[x] = (static_cast<UnsignedInt>(in[3]) << 24) | (static_cast<UnsignedInt>(in[0]) << 16)
					| (static_cast<UnsignedInt>(in[1]) << 8) | in[2];
		}
		surface->Unlock();
	}
	REF_PTR_RELEASE(surface);
	if (!bits)
	{
		REF_PTR_RELEASE(texture);
		return 0;
	}
	s_background = texture;
	return 1;
}
