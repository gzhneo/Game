/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// GuiEngine.h -- the engine DLL of the editors (MapEditorEngine.dll) as the GUI editor uses it: engine
// start, a view, and the game's window system that reads and draws window layouts.

#pragma once

#include "EditorCommon/Theme.h"

#include <QByteArray>
#include <QImage>
#include <QRect>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <vector>

struct MeEngineSettings;

namespace GuiEditorApp
{

using EditorCommon::Game;

class GuiEngine
{
public:
	static GuiEngine &instance();

	// Loads the DLL next to the program; false with a reason if it is missing or does not match.
	bool load(QString *error);
	bool start(Game game, const QString &dataFolder, const QString &logFile, QString *error);
	[[nodiscard]] bool isStarted() const { return m_started; }
	bool attach(void *window, int width, int height, QString *error);
	void resize(int width, int height);
	bool startWindows(QString *error);
	// Number of windows of the layout, -1 if it cannot be read.
	int loadLayout(const QString &path, QString *error);
	void render(std::uint32_t background);
	// Every layout of the game data (loose and in archives), below the folder Window with '/'.
	[[nodiscard]] QStringList layouts() const;
	// A file through the game's file system (archives included); null if there is none.
	[[nodiscard]] QByteArray readFile(const QString &path) const;
	// Shows the windows a layout starts hidden; applies from the next load.
	void setShowHidden(bool show);
	// NAME of the deepest visible window at the point of the view (pixels); empty if none.
	[[nodiscard]] QString windowAt(int x, int y) const;
	// Frames the windows with these names in the colour 0xAARRGGBB.
	void highlight(const QStringList &names, std::uint32_t argb);
	// Rectangles over the windows: frames or filled, in view pixels.
	struct OverlayItem
	{
		QRect rect;
		std::uint32_t argb = 0xFFFFFFFF;
		bool filled = false;
	};
	void overlay(const std::vector<OverlayItem> &items);
	// Catalogs of the game data.
	struct ImageInfo
	{
		QString name;
		int width = 0;
		int height = 0;
	};
	[[nodiscard]] std::vector<ImageInfo> images() const;
	// The picture of a mapped image in a square of the size; null if it cannot be read.
	[[nodiscard]] QImage imagePreview(const QString &name, int size) const;
	[[nodiscard]] QStringList headerTemplates() const;
	// "system", "input", "tooltip", "draw", "init", "update" or "shutdown".
	[[nodiscard]] QStringList callbacks(const char *kind) const;
	[[nodiscard]] QStringList textKeys() const;
	// The text of a key of the string table; exists is false if there is no such key.
	[[nodiscard]] QString text(const QString &key, bool *exists) const;
	// Removes the windows of the layout.
	void clear();
	// Test mode: input to the game's windows (kind and data as meWindowsInput); true if a window used it.
	bool input(int kind, int x, int y, int data);
	// What the gadgets reported since the last call: NAME, message, value separated by tabs.
	[[nodiscard]] QStringList events() const;
	// A picture under the windows, stretched to the view; a null image clears it.
	bool setBackgroundImage(const QImage &image);
	// Rectangle of the window in the view (pixels); empty if there is none.
	[[nodiscard]] QRect windowRect(const QString &name) const;
	// The next rendered frame goes into this BMP file.
	void capture(const QString &bmpPath);
	[[nodiscard]] QString report() const;
	void stop();

private:
	GuiEngine() = default;

	void *m_module = nullptr;
	bool m_started = false;
	bool m_attached = false;

	using StartFn = std::int32_t (*)(const MeEngineSettings *settings, char *error, std::int32_t size);
	using VoidFn = void (*)();
	using ReportFn = std::int32_t (*)(char *text, std::int32_t size);
	using AttachFn = std::int32_t (*)(void *hwnd, std::int32_t width, std::int32_t height, char *error, std::int32_t size);
	using ResizeFn = void (*)(std::int32_t width, std::int32_t height);
	using CaptureFn = void (*)(const wchar_t *path);
	using ErrorFn = std::int32_t (*)(char *error, std::int32_t size);
	using LoadFn = std::int32_t (*)(const char *path, char *error, std::int32_t size);
	using RenderFn = void (*)(std::uint32_t background);

	StartFn m_start = nullptr;
	VoidFn m_stop = nullptr;
	ReportFn m_report = nullptr;
	AttachFn m_attach = nullptr;
	ResizeFn m_resize = nullptr;
	CaptureFn m_capture = nullptr;
	ErrorFn m_windowsStart = nullptr;
	LoadFn m_windowsLoad = nullptr;
	RenderFn m_windowsRender = nullptr;

	using TextFn = std::int32_t (*)(char *text, std::int32_t size);
	using ReadFn = std::int32_t (*)(const char *path, char *buffer, std::int32_t size);
	using FlagFn = void (*)(std::int32_t flag);
	using HitFn = std::int32_t (*)(std::int32_t x, std::int32_t y, char *name, std::int32_t size);
	using HighlightFn = void (*)(const char *names, std::uint32_t argb);
	using RectFn = std::int32_t (*)(const char *name, std::int32_t *rect);

	TextFn m_layouts = nullptr;
	ReadFn m_readFile = nullptr;
	FlagFn m_showHidden = nullptr;
	HitFn m_hit = nullptr;
	HighlightFn m_highlight = nullptr;
	RectFn m_rect = nullptr;
	using OverlayFn = void (*)(const std::int32_t *items, std::int32_t count);
	OverlayFn m_overlay = nullptr;
	using PreviewFn = std::int32_t (*)(const char *name, std::int32_t size, std::uint8_t *rgba);
	using KindTextFn = std::int32_t (*)(const char *kind, char *text, std::int32_t size);
	using KeyTextFn = std::int32_t (*)(const char *key, char *text, std::int32_t size);
	TextFn m_images = nullptr;
	PreviewFn m_imagePreview = nullptr;
	TextFn m_headerTemplates = nullptr;
	KindTextFn m_callbacks = nullptr;
	TextFn m_textKeys = nullptr;
	KeyTextFn m_text = nullptr;
	using InputFn = std::int32_t (*)(std::int32_t kind, std::int32_t x, std::int32_t y, std::int32_t data);
	InputFn m_input = nullptr;
	TextFn m_events = nullptr;
	using BackgroundFn = std::int32_t (*)(const std::uint8_t *rgba, std::int32_t width, std::int32_t height);
	BackgroundFn m_backgroundImage = nullptr;
	VoidFn m_clear = nullptr;
};

} // namespace GuiEditorApp
