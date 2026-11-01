/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// EngineClient.cpp -- loading MapEditorEngine.dll and forwarding the calls.

#include "App/EngineClient.h"

#include "App/Language.h"

#include <QCoreApplication>
#include <QDir>

#include <algorithm>
#include <array>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#endif

namespace MapEditorApp
{

namespace
{

template <class FN>
bool resolve(void *module, const char *name, FN &fn)
{
#ifdef _WIN32
	fn = reinterpret_cast<FN>(reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(module), name)));
#else
	(void)module;
	(void)name;
	fn = nullptr;
#endif
	return fn != nullptr;
}

QString fromBuffer(const std::array<char, 1024> &buffer)
{
	return QString::fromUtf8(buffer.data());
}

} // namespace

EngineClient &EngineClient::instance()
{
	static EngineClient client;
	return client;
}

bool EngineClient::load(QString *error)
{
	if (m_loaded)
		return true;
#ifdef _WIN32
	const QString path = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("MapEditorEngine.dll"));
	HMODULE module = LoadLibraryW(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(path).utf16()));
	if (!module)
	{
		if (error)
			*error = T("The 3D view is not available: MapEditorEngine.dll could not be loaded (error %1).").arg(GetLastError());
		return false;
	}
	MeApiVersionFn version = nullptr;
	if (!resolve(module, "meApiVersion", version) || version() != ME_API_VERSION)
	{
		if (error)
			*error = T("The 3D view is not available: MapEditorEngine.dll does not match this editor.");
		FreeLibrary(module);
		return false;
	}
	const bool all = resolve(module, "meEngineStart", m_start) && resolve(module, "meEngineStop", m_stop)
		&& resolve(module, "meEngineReport", m_report) && resolve(module, "meViewAttach", m_attach)
		&& resolve(module, "meViewDetach", m_detach) && resolve(module, "meViewResize", m_resize)
		&& resolve(module, "meViewLoadMap", m_loadMap) && resolve(module, "meViewUnloadMap", m_unloadMap)
		&& resolve(module, "meViewGetCamera", m_getCamera) && resolve(module, "meViewSetCamera", m_setCamera)
		&& resolve(module, "meViewResetCamera", m_resetCamera) && resolve(module, "meViewPick", m_pick)
		&& resolve(module, "meViewRender", m_render) && resolve(module, "meViewCapture", m_capture)
		&& resolve(module, "meViewSetOverlay", m_setOverlay) && resolve(module, "meViewPickObject", m_pickObject)
		&& resolve(module, "meViewMoveObject", m_moveObject) && resolve(module, "meViewObjectInfo", m_objectInfo)
		&& resolve(module, "meViewObjectsInRect", m_objectsInRect) && resolve(module, "meViewSetMarquee", m_setMarquee)
		&& resolve(module, "meViewReloadObjects", m_reloadObjects)
		&& resolve(module, "meCatalogCount", m_catalogCount) && resolve(module, "meCatalogEntry", m_catalogEntry)
		&& resolve(module, "mePreviewTemplate", m_preview) && resolve(module, "meRoadCount", m_roadCount)
		&& resolve(module, "meRoadEntry", m_roadEntry) && resolve(module, "meSoundCount", m_soundCount)
		&& resolve(module, "meSoundEntry", m_soundEntry) && resolve(module, "meViewSetHeights", m_setHeights)
		&& resolve(module, "meTerrainCount", m_terrainCount) && resolve(module, "meTerrainEntry", m_terrainEntry)
		&& resolve(module, "meTerrainPreview", m_terrainPreview) && resolve(module, "meViewSetTiles", m_setTiles)
		&& resolve(module, "meViewSetBlendTable", m_setBlendTable) && resolve(module, "meMoldCount", m_moldCount)
		&& resolve(module, "meMoldName", m_moldName) && resolve(module, "meMoldSample", m_moldSample)
		&& resolve(module, "meViewSetCliffFlags", m_setCliffFlags) && resolve(module, "meViewSetTerrainTint", m_setTerrainTint)
		&& resolve(module, "meMapDefaults", m_mapDefaults)
		&& resolve(module, "mePlayerTemplateCount", m_factionCount) && resolve(module, "mePlayerTemplateEntry", m_factionEntry)
		&& resolve(module, "meColorCount", m_colorCount) && resolve(module, "meColorEntry", m_colorEntry)
		&& resolve(module, "meScriptTemplateCount", m_scriptTemplateCount) && resolve(module, "meScriptTemplate", m_scriptTemplate)
		&& resolve(module, "meScriptValueCount", m_scriptValueCount) && resolve(module, "meScriptValue", m_scriptValue)
		&& resolve(module, "meViewSetDisplay", m_setDisplay) && resolve(module, "meMacroTextureCount", m_macroTextureCount)
		&& resolve(module, "meMacroTextureName", m_macroTextureName) && resolve(module, "meViewReloadTextures", m_reloadTextures)
		&& resolve(module, "meParticleCount", m_particleCount) && resolve(module, "meParticleName", m_particleName)
		&& resolve(module, "meParticleIni", m_particleIni) && resolve(module, "meParticleApply", m_particleApply)
		&& resolve(module, "meParticleSaveAll", m_particleSaveAll) && resolve(module, "meParticlePreview", m_particlePreview)
		&& resolve(module, "meParticleStopAll", m_particleStopAll) && resolve(module, "meParticleChanged", m_particleChanged)
		&& resolve(module, "meParticleReset", m_particleReset) && resolve(module, "meParticleChangedIni", m_particleChangedIni)
		&& resolve(module, "meParticleSetLimit", m_particleSetLimit) && resolve(module, "meParticleOriginalIni", m_particleOriginalIni)
		&& resolve(module, "meTextureCount", m_textureCount) && resolve(module, "meTextureName", m_textureName)
		&& resolve(module, "meTextureThumbnail", m_textureThumbnail) && resolve(module, "meViewSetLighting", m_setLighting)
		&& resolve(module, "meViewFreezeTime", m_freezeTime)
		&& resolve(module, "meViewSetTile", m_setTile) && resolve(module, "meViewGetWaterLook", m_getWaterLook) && resolve(module, "meViewSetWaterLook", m_setWaterLook);
	if (!all)
	{
		if (error)
			*error = T("The 3D view is not available: MapEditorEngine.dll does not match this editor.");
		FreeLibrary(module);
		return false;
	}
	// The DLL stays loaded until the process ends: the engine cannot be started twice.
	m_loaded = true;
	return true;
#else
	if (error)
		*error = T("The 3D view needs Windows.");
	return false;
#endif
}

bool EngineClient::start(Game game, const QString &dataFolder, const QString &logFile, QString *error)
{
	if (m_started)
		return m_game == game;
	if (!load(error))
		return false;
	const std::wstring data = QDir::toNativeSeparators(dataFolder).toStdWString();
	const std::wstring shaders = QDir::toNativeSeparators(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("Shaders"))).toStdWString();
	const std::wstring log = QDir::toNativeSeparators(logFile).toStdWString();
	MeEngineSettings settings{};
	settings.game = game == Game::Generals ? ME_GAME_GENERALS : ME_GAME_ZERO_HOUR;
	settings.dataFolder = data.c_str();
	settings.shaderFolder = shaders.c_str();
	settings.logFile = logFile.isEmpty() ? nullptr : log.c_str();
	std::array<char, 1024> buffer{};
	if (!m_start(&settings, buffer.data(), static_cast<int32_t>(buffer.size())))
	{
		if (error)
			*error = fromBuffer(buffer);
		return false;
	}
	m_started = true;
	m_game = game;
	return true;
}

QString EngineClient::report() const
{
	if (!m_loaded)
		return QString();
	std::vector<char> text(static_cast<std::size_t>(m_report(nullptr, 0)) + 1);
	m_report(text.data(), static_cast<int32_t>(text.size()));
	return QString::fromUtf8(text.data());
}

void EngineClient::stop()
{
	if (!m_loaded)
		return;
	m_stop();
	m_attached = false;
	m_started = false;
	m_catalog.clear();
	m_roads.clear();
	m_factions.clear();
	m_colors.clear();
	m_conditionTemplates.clear();
	m_actionTemplates.clear();
	m_scriptValues.clear();
	m_sounds.clear();
	m_terrains.clear();
	m_molds.clear();
	m_moldsRead = false;
	m_macroTextures.clear();
	m_macroTexturesRead = false;
	m_particles.clear();
	m_particlesRead = false;
	m_textures.clear();
	m_texturesRead = false;
	m_thumbnails.clear();
}

bool EngineClient::attach(void *hwnd, int width, int height, QString *error)
{
	if (!m_started)
		return false;
	std::array<char, 1024> buffer{};
	if (!m_attach(hwnd, width, height, buffer.data(), static_cast<int32_t>(buffer.size())))
	{
		if (error)
			*error = fromBuffer(buffer);
		return false;
	}
	m_attached = true;
	return true;
}

void EngineClient::detach()
{
	if (m_attached)
		m_detach();
	m_attached = false;
}

void EngineClient::resize(int width, int height)
{
	if (m_attached)
		m_resize(width, height);
}

bool EngineClient::loadMap(const std::vector<std::uint8_t> &chunks, MeMapInfo *info, QString *error)
{
	if (!m_attached)
		return false;
	std::array<char, 1024> buffer{};
	if (!m_loadMap(chunks.data(), static_cast<int32_t>(chunks.size()), info, buffer.data(), static_cast<int32_t>(buffer.size())))
	{
		if (error)
			*error = fromBuffer(buffer);
		return false;
	}
	return true;
}

void EngineClient::unloadMap()
{
	if (m_attached)
		m_unloadMap();
}

MeCamera EngineClient::camera() const
{
	MeCamera c{};
	if (m_attached)
		m_getCamera(&c);
	return c;
}

void EngineClient::setCamera(const MeCamera &camera)
{
	if (m_attached)
		m_setCamera(&camera);
}

void EngineClient::resetCamera()
{
	if (m_attached)
		m_resetCamera();
}

bool EngineClient::pick(int x, int y, float world[3])
{
	return m_attached && m_pick(x, y, world) != 0;
}

void EngineClient::render()
{
	if (m_attached)
		m_render();
}

void EngineClient::capture(const QString &bmpPath)
{
	if (m_attached)
		m_capture(QDir::toNativeSeparators(bmpPath).toStdWString().c_str());
}

bool EngineClient::pickObject(int x, int y, MePickResult *result)
{
	return m_attached && m_pickObject(x, y, result) != 0;
}

void EngineClient::moveObject(int object, float x, float y, float z, float angle)
{
	if (m_attached)
		m_moveObject(object, x, y, z, angle);
}

bool EngineClient::objectInfo(int object, MePickResult *result)
{
	return m_attached && m_objectInfo(object, result) != 0;
}

std::vector<int> EngineClient::objectsInRect(int x0, int y0, int x1, int y1)
{
	if (!m_attached)
		return {};
	// First the count, then the indices.
	const int32_t count = m_objectsInRect(x0, y0, x1, y1, nullptr, 0);
	std::vector<int32_t> found(static_cast<std::size_t>(std::max(0, count)));
	if (count > 0)
		m_objectsInRect(x0, y0, x1, y1, found.data(), count);
	return std::vector<int>(found.begin(), found.end());
}

bool EngineClient::reloadObjects(const std::vector<std::uint8_t> &chunks)
{
	return m_attached && m_reloadObjects(chunks.data(), static_cast<int32_t>(chunks.size())) != 0;
}

void EngineClient::setMarquee(int x0, int y0, int x1, int y1)
{
	if (m_attached)
		m_setMarquee(x0, y0, x1, y1);
}

void EngineClient::setHeights(int x0, int y0, int x1, int y1, const std::vector<std::uint8_t> &heights, int width)
{
	if (m_attached && !heights.empty())
		m_setHeights(x0, y0, x1, y1, heights.data(), width);
}

void EngineClient::setOverlay(const std::vector<float> &points, const std::vector<MeOverlayShape> &shapes)
{
	if (m_attached)
		m_setOverlay(points.data(), static_cast<int32_t>(points.size() / 3), shapes.data(), static_cast<int32_t>(shapes.size()));
}

const std::vector<MeTemplateInfo> &EngineClient::catalog()
{
	if (m_catalog.empty() && m_started)
	{
		const int32_t count = m_catalogCount();
		m_catalog.reserve(static_cast<std::size_t>(std::max(0, count)));
		for (int32_t i = 0; i < count; ++i)
		{
			MeTemplateInfo info{};
			if (m_catalogEntry(i, &info))
				m_catalog.push_back(info);
		}
	}
	return m_catalog;
}

bool EngineClient::mapDefaults(MeMapDefaults &defaults)
{
	return m_started && m_mapDefaults(&defaults) != 0;
}

const std::vector<MePlayerTemplateInfo> &EngineClient::factions()
{
	if (m_factions.empty() && m_started)
	{
		const int32_t count = m_factionCount();
		for (int32_t i = 0; i < count; ++i)
		{
			MePlayerTemplateInfo info{};
			if (m_factionEntry(i, &info))
				m_factions.push_back(info);
		}
	}
	return m_factions;
}

const std::vector<MeColorInfo> &EngineClient::colors()
{
	if (m_colors.empty() && m_started)
	{
		const int32_t count = m_colorCount();
		for (int32_t i = 0; i < count; ++i)
		{
			MeColorInfo info{};
			if (m_colorEntry(i, &info))
				m_colors.push_back(info);
		}
	}
	return m_colors;
}

const std::vector<MeScriptTemplate> &EngineClient::scriptTemplates(bool actions)
{
	std::vector<MeScriptTemplate> &list = actions ? m_actionTemplates : m_conditionTemplates;
	if (list.empty() && m_started)
	{
		// The index is the number the map stores: keep every entry, filled or not.
		const int32_t count = m_scriptTemplateCount(actions ? 1 : 0);
		list.resize(static_cast<std::size_t>(std::max(0, count)));
		for (int32_t i = 0; i < count; ++i)
			if (!m_scriptTemplate(actions ? 1 : 0, i, &list[static_cast<std::size_t>(i)]))
				list[static_cast<std::size_t>(i)] = MeScriptTemplate{};
	}
	return list;
}

const std::vector<MeNamedValue> &EngineClient::scriptValues(int parameterType)
{
	auto it = m_scriptValues.find(parameterType);
	if (it != m_scriptValues.end())
		return it->second;
	std::vector<MeNamedValue> list;
	if (m_started)
	{
		const int32_t count = m_scriptValueCount(parameterType);
		for (int32_t i = 0; i < count; ++i)
		{
			MeNamedValue v{};
			if (m_scriptValue(parameterType, i, &v))
				list.push_back(v);
		}
	}
	if (!m_started)
		return m_scriptValues[-1];
	return m_scriptValues.emplace(parameterType, std::move(list)).first->second;
}

const std::vector<MeRoadInfo> &EngineClient::roads()
{
	if (m_roads.empty() && m_started)
	{
		const int32_t count = m_roadCount();
		for (int32_t i = 0; i < count; ++i)
		{
			MeRoadInfo info{};
			if (m_roadEntry(i, &info))
				m_roads.push_back(info);
		}
	}
	return m_roads;
}

const std::vector<MeSoundInfo> &EngineClient::sounds()
{
	if (m_sounds.empty() && m_started)
	{
		const int32_t count = m_soundCount();
		for (int32_t i = 0; i < count; ++i)
		{
			MeSoundInfo info{};
			if (m_soundEntry(i, &info))
				m_sounds.push_back(info);
		}
	}
	return m_sounds;
}

const std::vector<MeTerrainInfo> &EngineClient::terrains()
{
	if (m_terrains.empty() && m_started)
	{
		const int32_t count = m_terrainCount();
		for (int32_t i = 0; i < count; ++i)
		{
			MeTerrainInfo info{};
			if (m_terrainEntry(i, &info))
				m_terrains.push_back(info);
		}
	}
	return m_terrains;
}

QImage EngineClient::terrainPreview(const QString &name, int size)
{
	if (!m_started)
		return {};
	QImage image(size, size, QImage::Format_RGBA8888);
	const QByteArray utf8Name = name.toUtf8();
	if (!m_terrainPreview(utf8Name.constData(), size, image.bits()))
		return {};
	return image;
}

void EngineClient::setTiles(int x0, int y0, int x1, int y1, const std::vector<std::int16_t> &tiles, const std::vector<std::int16_t> &blends,
	const std::vector<std::int16_t> &extraBlends, const std::vector<std::int16_t> &cliffs, int width)
{
	if (m_attached && !tiles.empty() && tiles.size() == blends.size() && tiles.size() == extraBlends.size() && tiles.size() == cliffs.size())
		m_setTiles(x0, y0, x1, y1, tiles.data(), blends.data(), extraBlends.data(), cliffs.data(), width);
}

void EngineClient::setBlendTable(const std::vector<MapModel::BlendedTile> &table)
{
	if (!m_attached)
		return;
	std::vector<MeBlendTile> entries;
	entries.reserve(table.size());
	for (const MapModel::BlendedTile &t : table)
	{
		MeBlendTile e{};
		e.blendIndex = t.blendIndex;
		e.horizontal = t.horizontal;
		e.vertical = t.vertical;
		e.rightDiagonal = t.rightDiagonal;
		e.leftDiagonal = t.leftDiagonal;
		e.inverted = t.inverted;
		e.longDiagonal = t.longDiagonal;
		e.customBlendEdgeClass = t.customBlendEdgeClass;
		entries.push_back(e);
	}
	m_setBlendTable(entries.data(), static_cast<int32_t>(entries.size()));
}

void EngineClient::setCliffFlags(int x0, int y0, int x1, int y1, const std::vector<std::uint8_t> &flags, int width)
{
	if (m_attached && !flags.empty())
		m_setCliffFlags(x0, y0, x1, y1, flags.data(), width);
}

std::optional<MapEditorCore::WaterLook> EngineClient::waterLook(bool gameData)
{
	MeWaterLook l{};
	if (!m_started || !m_getWaterLook(&l, gameData ? 1 : 0))
		return std::nullopt;
	const auto channel = [](float v) { return std::clamp(static_cast<int>(std::lround(v * 255.0f)), 0, 255); };
	MapEditorCore::WaterLook look;
	look.transparentDepth = l.transparentDepth;
	look.minOpacity = l.minOpacity;
	look.standingColor = {channel(l.standingColor[0]), channel(l.standingColor[1]), channel(l.standingColor[2])};
	look.additive = l.additive != 0;
	look.turbidityDepth = l.turbidityDepth;
	look.turbidityColor = {channel(l.turbidityColor[0]), channel(l.turbidityColor[1]), channel(l.turbidityColor[2])};
	look.refraction = l.refraction;
	look.reflection = l.reflection;
	return look;
}

bool EngineClient::setWaterLook(const MapEditorCore::WaterLook &look)
{
	if (!m_started)
		return false;
	MeWaterLook l{};
	l.transparentDepth = look.transparentDepth;
	l.minOpacity = look.minOpacity;
	l.additive = look.additive ? 1 : 0;
	l.turbidityDepth = look.turbidityDepth;
	l.refraction = look.refraction;
	l.reflection = look.reflection;
	for (std::size_t c = 0; c < 3; ++c)
	{
		l.standingColor[c] = static_cast<float>(look.standingColor[c]) / 255.0f;
		l.turbidityColor[c] = static_cast<float>(look.turbidityColor[c]) / 255.0f;
	}
	return m_setWaterLook(&l) != 0;
}

void EngineClient::setTile(float left, float top, float right, float bottom)
{
	if (m_started)
		m_setTile(left, top, right, bottom);
}

void EngineClient::freezeTime(bool freeze)
{
	if (m_started)
		m_freezeTime(freeze ? 1 : 0);
}

void EngineClient::setTerrainTint(const std::vector<std::uint32_t> &colors, int width, int height, int border)
{
	if (m_attached)
		m_setTerrainTint(colors.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) ? colors.data() : nullptr, width, height, border);
}

bool EngineClient::setDisplay(const MeDisplayOptions &options)
{
	if (!m_started)
		return false;
	return m_setDisplay(&options) != 0;
}

const QStringList &EngineClient::particleNames()
{
	if (!m_particlesRead && m_started)
	{
		m_particlesRead = true;
		const int32_t count = m_particleCount();
		for (int32_t i = 0; i < count; ++i)
		{
			char name[256] = {};
			if (m_particleName(i, name, static_cast<int32_t>(sizeof(name))))
				m_particles.push_back(QString::fromUtf8(name));
		}
	}
	return m_particles;
}

QString EngineClient::particleIni(const QString &name)
{
	if (!m_started)
		return {};
	const QByteArray utf8Name = name.toUtf8();
	const int32_t length = m_particleIni(utf8Name.constData(), nullptr, 0);
	if (length <= 0)
		return {};
	QByteArray text(length + 1, '\0');
	m_particleIni(utf8Name.constData(), text.data(), length + 1);
	text.resize(length);
	return QString::fromLatin1(text);
}

bool EngineClient::applyParticleIni(const QString &text, QString *error)
{
	if (!m_started)
		return false;
	char message[512] = {};
	const QByteArray latin = text.toLatin1();
	const bool ok = m_particleApply(latin.constData(), message, static_cast<int32_t>(sizeof(message))) != 0;
	if (!ok && error)
		*error = QString::fromUtf8(message);
	// New templates join the list.
	m_particles.clear();
	m_particlesRead = false;
	return ok;
}

bool EngineClient::saveAllParticles(const QString &path)
{
	return m_started && m_particleSaveAll(QDir::toNativeSeparators(path).toStdWString().c_str()) != 0;
}

bool EngineClient::previewParticle(const QString &name, float x, float y, float z)
{
	return m_attached && m_particlePreview(name.toUtf8().constData(), x, y, z) != 0;
}

void EngineClient::stopParticles()
{
	if (m_started)
		m_particleStopAll();
}

int EngineClient::particleChange(const QString &name)
{
	return m_started ? m_particleChanged(name.toUtf8().constData()) : 0;
}

int EngineClient::resetParticles(const QString &name)
{
	if (!m_started)
		return 0;
	const int count = m_particleReset(name.toUtf8().constData());
	// Dropped new systems leave the list.
	m_particles.clear();
	m_particlesRead = false;
	return count;
}

QString EngineClient::changedParticlesIni()
{
	if (!m_started)
		return {};
	const int32_t length = m_particleChangedIni(nullptr, 0);
	if (length <= 0)
		return {};
	QByteArray text(length + 1, '\0');
	m_particleChangedIni(text.data(), length + 1);
	text.resize(length);
	return QString::fromLatin1(text);
}

void EngineClient::setParticleLimit(int count)
{
	if (m_started)
		m_particleSetLimit(count);
}

QString EngineClient::particleOriginalIni(const QString &name)
{
	if (!m_started)
		return {};
	const QByteArray utf8Name = name.toUtf8();
	const int32_t length = m_particleOriginalIni(utf8Name.constData(), nullptr, 0);
	if (length <= 0)
		return {};
	QByteArray text(length + 1, '\0');
	m_particleOriginalIni(utf8Name.constData(), text.data(), length + 1);
	text.resize(length);
	return QString::fromLatin1(text);
}

const QStringList &EngineClient::textureNames()
{
	if (!m_texturesRead && m_started)
	{
		m_texturesRead = true;
		const int32_t count = m_textureCount();
		for (int32_t i = 0; i < count; ++i)
		{
			char name[260] = {};
			if (m_textureName(i, name, static_cast<int32_t>(sizeof(name))))
				m_textures.push_back(QString::fromUtf8(name));
		}
	}
	return m_textures;
}

QImage EngineClient::textureThumbnail(const QString &name, int size)
{
	if (!m_started || size <= 0)
		return {};
	const QString key = name.toLower() + QLatin1Char('@') + QString::number(size);
	const auto cached = m_thumbnails.find(key);
	if (cached != m_thumbnails.end())
		return cached->second;
	QImage image(size, size, QImage::Format_RGBA8888);
	if (!m_textureThumbnail(name.toUtf8().constData(), size, image.bits()))
		image = QImage();
	m_thumbnails[key] = image;
	return image;
}

bool EngineClient::setLighting(const MapModel::GlobalLighting &lighting)
{
	if (!m_attached || lighting.timeOfDay < 1 || lighting.timeOfDay > 4)
		return false;
	MeLighting l{};
	l.timeOfDay = lighting.timeOfDay;
	const MapModel::TimeOfDayLighting &t = lighting.times[static_cast<std::size_t>(lighting.timeOfDay - 1)];
	const auto copy = [](MeLight &to, const MapModel::Light &from) {
		for (int c = 0; c < 3; ++c)
		{
			to.ambient[c] = from.ambient[static_cast<std::size_t>(c)];
			to.diffuse[c] = from.diffuse[static_cast<std::size_t>(c)];
			to.position[c] = from.position[static_cast<std::size_t>(c)];
		}
	};
	for (std::size_t i = 0; i < 3; ++i)
	{
		copy(l.terrain[i], t.terrain[i]);
		copy(l.objects[i], t.objects[i]);
	}
	l.shadowColor = lighting.shadowColor.value_or(0u);
	return m_setLighting(&l) != 0;
}

void EngineClient::reloadTextures()
{
	if (m_attached)
		m_reloadTextures();
}

const QStringList &EngineClient::macroTextures()
{
	if (!m_macroTexturesRead && m_started)
	{
		m_macroTexturesRead = true;
		const int32_t count = m_macroTextureCount();
		for (int32_t i = 0; i < count; ++i)
		{
			char name[96] = {};
			if (m_macroTextureName(i, name, static_cast<int32_t>(sizeof(name))))
				m_macroTextures.push_back(QString::fromUtf8(name));
		}
	}
	return m_macroTextures;
}

const QStringList &EngineClient::molds()
{
	if (!m_moldsRead && m_started)
	{
		m_moldsRead = true;
		const int32_t count = m_moldCount();
		for (int32_t i = 0; i < count; ++i)
		{
			char name[64] = {};
			if (m_moldName(i, name, static_cast<int32_t>(sizeof(name))))
				m_molds.push_back(QString::fromUtf8(name));
		}
	}
	return m_molds;
}

bool EngineClient::moldSample(const QString &name, float &x0, float &y0, float &step, int &cols, int &rows, std::vector<float> &z)
{
	if (!m_attached)
		return false;
	const QByteArray utf8Name = name.toUtf8();
	MeMoldGrid grid{};
	const int32_t samples = m_moldSample(utf8Name.constData(), &grid, nullptr, 0);
	if (samples <= 0)
		return false;
	z.assign(static_cast<std::size_t>(samples), 0.0f);
	if (m_moldSample(utf8Name.constData(), &grid, z.data(), samples) != samples)
		return false;
	x0 = grid.x0;
	y0 = grid.y0;
	step = grid.step;
	cols = grid.cols;
	rows = grid.rows;
	return true;
}

QImage EngineClient::preview(const QString &templateName, int size, float yaw, const QString &team, const QColor &background)
{
	if (!m_attached)
		return {};
	QImage image(size, size, QImage::Format_RGBA8888);
	const QByteArray name = templateName.toUtf8();
	const QByteArray teamName = team.toUtf8();
	if (!m_preview(name.constData(), size, yaw, team.isEmpty() ? nullptr : teamName.constData(), background.rgb() & 0xFFFFFFu, image.bits()))
		return {};
	return image;
}

} // namespace MapEditorApp
