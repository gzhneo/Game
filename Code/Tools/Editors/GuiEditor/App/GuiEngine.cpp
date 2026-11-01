/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// GuiEngine.cpp -- loading MapEditorEngine.dll and forwarding the calls of the GUI editor.

#include "App/GuiEngine.h"

#include "Engine/EngineApi.h"

#include <QCoreApplication>
#include <QDir>

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <array>
#include <string>

namespace GuiEditorApp
{

namespace
{

#ifdef _WIN32
template <class FN>
bool resolve(void *module, const char *name, FN &fn)
{
	fn = reinterpret_cast<FN>(reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(module), name)));
	return fn != nullptr;
}
#endif

QString fromBuffer(const std::array<char, 1024> &buffer)
{
	return QString::fromLocal8Bit(buffer.data());
}

std::wstring nativePath(const QString &path)
{
	return QDir::toNativeSeparators(path).toStdWString();
}

} // namespace

GuiEngine &GuiEngine::instance()
{
	static GuiEngine engine;
	return engine;
}

bool GuiEngine::load(QString *error)
{
	if (m_module)
		return true;
#ifndef _WIN32
	// The engine is a Windows DLL; elsewhere the editor works on the layout model alone.
	if (error)
		*error = QStringLiteral("MapEditorEngine.dll exists only on Windows.");
	return false;
#else
	const QString path = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("MapEditorEngine.dll"));
	HMODULE module = LoadLibraryW(nativePath(path).c_str());
	if (!module)
	{
		if (error)
			*error = QStringLiteral("MapEditorEngine.dll could not be loaded (error %1).").arg(GetLastError());
		return false;
	}
	using VersionFn = std::int32_t (*)();
	VersionFn version = nullptr;
	const bool all = resolve(module, "meApiVersion", version) && version() == ME_API_VERSION
		&& resolve(module, "meEngineStart", m_start) && resolve(module, "meEngineStop", m_stop)
		&& resolve(module, "meEngineReport", m_report) && resolve(module, "meViewAttach", m_attach)
		&& resolve(module, "meViewResize", m_resize) && resolve(module, "meViewCapture", m_capture)
		&& resolve(module, "meWindowsStart", m_windowsStart) && resolve(module, "meWindowsLoad", m_windowsLoad)
		&& resolve(module, "meWindowsRender", m_windowsRender) && resolve(module, "meWindowsLayouts", m_layouts)
		&& resolve(module, "meFileRead", m_readFile) && resolve(module, "meWindowsShowHidden", m_showHidden)
		&& resolve(module, "meWindowsHit", m_hit) && resolve(module, "meWindowsHighlight", m_highlight)
		&& resolve(module, "meWindowsRect", m_rect) && resolve(module, "meWindowsOverlay", m_overlay)
		&& resolve(module, "meWindowsClear", m_clear) && resolve(module, "meWindowsImages", m_images)
		&& resolve(module, "meWindowsImagePreview", m_imagePreview) && resolve(module, "meWindowsHeaderTemplates", m_headerTemplates)
		&& resolve(module, "meWindowsCallbacks", m_callbacks) && resolve(module, "meWindowsTextKeys", m_textKeys)
		&& resolve(module, "meWindowsText", m_text) && resolve(module, "meWindowsInput", m_input)
		&& resolve(module, "meWindowsEvents", m_events) && resolve(module, "meWindowsBackgroundImage", m_backgroundImage);
	if (!all)
	{
		FreeLibrary(module);
		if (error)
			*error = QStringLiteral("MapEditorEngine.dll does not match this editor.");
		return false;
	}
	m_module = module;
	return true;
#endif
}

bool GuiEngine::start(Game game, const QString &dataFolder, const QString &logFile, QString *error)
{
	if (m_started)
		return true;
	if (!load(error))
		return false;
	const std::wstring data = nativePath(dataFolder);
	const std::wstring shaders = nativePath(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("Shaders")));
	const std::wstring log = nativePath(logFile);
	MeEngineSettings settings{};
	settings.game = game == Game::Generals ? ME_GAME_GENERALS : ME_GAME_ZERO_HOUR;
	settings.dataFolder = data.c_str();
	settings.shaderFolder = shaders.c_str();
	settings.logFile = logFile.isEmpty() ? nullptr : log.c_str();
	settings.windowsOnly = 1;
	std::array<char, 1024> buffer{};
	if (!m_start(&settings, buffer.data(), static_cast<std::int32_t>(buffer.size())))
	{
		if (error)
			*error = fromBuffer(buffer);
		return false;
	}
	m_started = true;
	return true;
}

bool GuiEngine::attach(void *window, int width, int height, QString *error)
{
	if (m_attached)
		return true;
	if (!m_started)
		return false;
	std::array<char, 1024> buffer{};
	if (!m_attach(window, width, height, buffer.data(), static_cast<std::int32_t>(buffer.size())))
	{
		if (error)
			*error = fromBuffer(buffer);
		return false;
	}
	m_attached = true;
	return true;
}

void GuiEngine::resize(int width, int height)
{
	if (m_attached)
		m_resize(width, height);
}

bool GuiEngine::startWindows(QString *error)
{
	if (!m_attached)
		return false;
	std::array<char, 1024> buffer{};
	if (!m_windowsStart(buffer.data(), static_cast<std::int32_t>(buffer.size())))
	{
		if (error)
			*error = fromBuffer(buffer);
		return false;
	}
	return true;
}

int GuiEngine::loadLayout(const QString &path, QString *error)
{
	if (!m_attached)
		return -1;
	std::array<char, 1024> buffer{};
	// Relative paths keep their slashes: the game looks them up in the folder Window only without a backslash.
	const QByteArray ansi = (QDir::isAbsolutePath(path) ? QDir::toNativeSeparators(path) : path).toLocal8Bit();
	const int count = m_windowsLoad(ansi.constData(), buffer.data(), static_cast<std::int32_t>(buffer.size()));
	if (count < 0 && error)
		*error = fromBuffer(buffer);
	return count;
}

void GuiEngine::render(std::uint32_t background)
{
	if (m_attached)
		m_windowsRender(background);
}

QStringList GuiEngine::layouts() const
{
	if (!m_started)
		return {};
	const std::int32_t size = m_layouts(nullptr, 0);
	std::string text(static_cast<std::size_t>(std::max<std::int32_t>(size, 0)) + 1, '\0');
	m_layouts(text.data(), static_cast<std::int32_t>(text.size()));
	return QString::fromLatin1(text.c_str()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

QByteArray GuiEngine::readFile(const QString &path) const
{
	if (!m_started)
		return {};
	const QByteArray name = path.toLocal8Bit();
	const std::int32_t size = m_readFile(name.constData(), nullptr, 0);
	if (size < 0)
		return {};
	QByteArray data(size, '\0');
	if (size > 0)
		m_readFile(name.constData(), data.data(), size);
	return data.isNull() ? QByteArray("") : data;
}

void GuiEngine::setShowHidden(bool show)
{
	if (m_module)
		m_showHidden(show ? 1 : 0);
}

QString GuiEngine::windowAt(int x, int y) const
{
	if (!m_attached)
		return {};
	std::array<char, 256> name{};
	m_hit(x, y, name.data(), static_cast<std::int32_t>(name.size()));
	return QString::fromLatin1(name.data());
}

void GuiEngine::highlight(const QStringList &names, std::uint32_t argb)
{
	if (m_module)
		m_highlight(names.join(QLatin1Char('\n')).toLatin1().constData(), argb);
}

void GuiEngine::overlay(const std::vector<OverlayItem> &items)
{
	if (!m_module)
		return;
	std::vector<std::int32_t> values;
	values.reserve(items.size() * 6);
	for (const OverlayItem &o : items)
	{
		values.insert(values.end(), {o.rect.left(), o.rect.top(), o.rect.right() + 1, o.rect.bottom() + 1,
			static_cast<std::int32_t>(o.argb), o.filled ? 1 : 0});
	}
	m_overlay(values.data(), static_cast<std::int32_t>(items.size()));
}

namespace
{

// The text of a call that returns its length and fills the buffer when it is big enough.
template <class Call>
std::string fetchText(const Call &call)
{
	const std::int32_t size = call(nullptr, 0);
	if (size <= 0)
		return {};
	std::string text(static_cast<std::size_t>(size) + 1, '\0');
	call(text.data(), static_cast<std::int32_t>(text.size()));
	text.resize(static_cast<std::size_t>(size));
	return text;
}

QStringList lines(const std::string &text)
{
	return QString::fromLatin1(text.c_str()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

} // namespace

std::vector<GuiEngine::ImageInfo> GuiEngine::images() const
{
	std::vector<ImageInfo> out;
	if (!m_attached)
		return out;
	for (const QString &line : lines(fetchText([this](char *t, std::int32_t n) { return m_images(t, n); })))
	{
		const QStringList parts = line.split(QLatin1Char('\t'));
		out.push_back({parts.value(0), parts.value(1).toInt(), parts.value(2).toInt()});
	}
	return out;
}

QImage GuiEngine::imagePreview(const QString &name, int size) const
{
	if (!m_attached || size <= 0)
		return {};
	QImage image(size, size, QImage::Format_RGBA8888);
	if (!m_imagePreview(name.toLatin1().constData(), size, image.bits()))
		return {};
	return image;
}

QStringList GuiEngine::headerTemplates() const
{
	return m_attached ? lines(fetchText([this](char *t, std::int32_t n) { return m_headerTemplates(t, n); })) : QStringList();
}

QStringList GuiEngine::callbacks(const char *kind) const
{
	return m_attached ? lines(fetchText([this, kind](char *t, std::int32_t n) { return m_callbacks(kind, t, n); })) : QStringList();
}

bool GuiEngine::input(int kind, int x, int y, int data)
{
	return m_attached && m_input(kind, x, y, data) != 0;
}

bool GuiEngine::setBackgroundImage(const QImage &image)
{
	if (!m_attached)
		return false;
	if (image.isNull())
		return m_backgroundImage(nullptr, 0, 0) != 0;
	const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
	// Rows are packed for the engine; QImage pads them to 4 bytes, which RGBA rows already are.
	return m_backgroundImage(rgba.constBits(), rgba.width(), rgba.height()) != 0;
}

QStringList GuiEngine::events() const
{
	return m_attached ? lines(fetchText([this](char *t, std::int32_t n) { return m_events(t, n); })) : QStringList();
}

QStringList GuiEngine::textKeys() const
{
	return m_attached ? lines(fetchText([this](char *t, std::int32_t n) { return m_textKeys(t, n); })) : QStringList();
}

QString GuiEngine::text(const QString &key, bool *exists) const
{
	*exists = false;
	if (!m_attached)
		return {};
	const QByteArray k = key.toLatin1();
	const std::int32_t size = m_text(k.constData(), nullptr, 0);
	if (size < 0)
		return {};
	*exists = true;
	std::string text(static_cast<std::size_t>(size) + 1, '\0');
	m_text(k.constData(), text.data(), static_cast<std::int32_t>(text.size()));
	text.resize(static_cast<std::size_t>(size));
	return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

void GuiEngine::clear()
{
	if (m_attached)
		m_clear();
}

QRect GuiEngine::windowRect(const QString &name) const
{
	std::array<std::int32_t, 4> r{};
	if (!m_attached || !m_rect(name.toLatin1().constData(), r.data()))
		return {};
	return QRect(QPoint(r[0], r[1]), QPoint(r[2] - 1, r[3] - 1));
}

void GuiEngine::capture(const QString &bmpPath)
{
	if (m_attached)
		m_capture(nativePath(bmpPath).c_str());
}

QString GuiEngine::report() const
{
	if (!m_module)
		return {};
	const std::int32_t size = m_report(nullptr, 0);
	std::string text(static_cast<std::size_t>(std::max<std::int32_t>(size, 0)) + 1, '\0');
	m_report(text.data(), static_cast<std::int32_t>(text.size()));
	return QString::fromLocal8Bit(text.c_str());
}

void GuiEngine::stop()
{
	if (m_started)
		m_stop();
	m_started = false;
	m_attached = false;
}

} // namespace GuiEditorApp
