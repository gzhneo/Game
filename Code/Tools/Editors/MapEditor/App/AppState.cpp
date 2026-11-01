/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// AppState.cpp -- editor settings and the open map.

#include "App/AppState.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace MapEditorApp
{

namespace
{

constexpr int MAX_RECENT = 24;

// Asset folders of the build (CMake GENERALS_ASSETS_DIR, GENERALSZH_ASSETS_DIR) as defaults.
#ifndef MAPEDITOR_DEFAULT_DATA_GENERALS
#define MAPEDITOR_DEFAULT_DATA_GENERALS ""
#endif
#ifndef MAPEDITOR_DEFAULT_DATA_ZERO_HOUR
#define MAPEDITOR_DEFAULT_DATA_ZERO_HOUR ""
#endif
// Game programs of the build (targets GZHNeo and GZHNeoZH).
#ifndef MAPEDITOR_DEFAULT_GAME_GENERALS
#define MAPEDITOR_DEFAULT_GAME_GENERALS ""
#endif
#ifndef MAPEDITOR_DEFAULT_GAME_ZERO_HOUR
#define MAPEDITOR_DEFAULT_GAME_ZERO_HOUR ""
#endif

std::unique_ptr<QSettings> openSettings(const QString &iniPath)
{
	if (!iniPath.isEmpty())
		return std::make_unique<QSettings>(iniPath, QSettings::IniFormat);
	return std::make_unique<QSettings>(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("GZH Neo"), QStringLiteral("MapEditor"));
}

QString gameKey(Game game)
{
	return game == Game::Generals ? QStringLiteral("generals") : QStringLiteral("zh");
}

Game gameFromKey(const QString &key)
{
	return key == QStringLiteral("generals") ? Game::Generals : Game::ZeroHour;
}

} // namespace

AppState::AppState(const QString &iniPath) : m_iniPath(iniPath)
{
	const auto s = openSettings(m_iniPath);
	theme = s->value(QStringLiteral("appearance/theme"), QStringLiteral("dark")).toString() == QStringLiteral("light") ? ThemeKind::Light : ThemeKind::Dark;
	const QString lang = s->value(QStringLiteral("appearance/language")).toString();
	language = lang == QStringLiteral("de") ? Language::German : lang == QStringLiteral("en") ? Language::English : systemLanguage();
	game = gameFromKey(s->value(QStringLiteral("game/selected"), QStringLiteral("zh")).toString());
	m_dataGenerals = s->value(QStringLiteral("data/generals")).toString();
	m_dataZeroHour = s->value(QStringLiteral("data/zh")).toString();
	m_gameGenerals = s->value(QStringLiteral("game/programGenerals")).toString();
	m_gameZeroHour = s->value(QStringLiteral("game/programZh")).toString();
	autosave = s->value(QStringLiteral("autosave/enabled"), true).toBool();
	autosaveSeconds = std::clamp(s->value(QStringLiteral("autosave/seconds"), 120).toInt(), 30, 3600);
	const DisplayOptions d;
	const auto flag = [&s](const char *key, bool byDefault) { return s->value(QStringLiteral("display/") + QLatin1String(key), byDefault).toBool(); };
	display.models = flag("models", d.models);
	display.shadows = flag("shadows", d.shadows);
	display.clouds = flag("clouds", d.clouds);
	display.macroTexture = flag("macroTexture", d.macroTexture);
	display.macroTextureName = s->value(QStringLiteral("display/macroTextureName")).toString();
	display.softWater = flag("softWater", d.softWater);
	display.wireframe = flag("wireframe", d.wireframe);
	display.extraBlends = flag("extraBlends", d.extraBlends);
	display.sounds = flag("sounds", d.sounds);
	display.contours = flag("contours", d.contours);
	display.contourStep = std::clamp(s->value(QStringLiteral("display/contourStep"), d.contourStep).toInt(), 1, 128);
	display.contourOffset = std::clamp(s->value(QStringLiteral("display/contourOffset"), d.contourOffset).toInt(), 0, 127);
	display.grid = flag("grid", d.grid);
	display.boundingBoxes = flag("boundingBoxes", d.boundingBoxes);
	display.sightRanges = flag("sightRanges", d.sightRanges);
	display.weaponRanges = flag("weaponRanges", d.weaponRanges);
	display.testArt = flag("testArt", d.testArt);
	display.letterbox = flag("letterbox", d.letterbox);
	display.garrisoned = flag("garrisoned", d.garrisoned);
	display.labels = flag("labels", d.labels);
	display.objectIcons = flag("objectIcons", d.objectIcons);
	display.snapToGrid = flag("snapToGrid", d.snapToGrid);
	display.brushOutline = flag("brushOutline", d.brushOutline);
	display.graphicsLevel = s->value(QStringLiteral("display/graphicsLevel"), d.graphicsLevel).toString();
	display.effectsOff = s->value(QStringLiteral("display/effectsOff")).toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
	display.slopes = flag("slopes", d.slopes);
	display.slopeLimit = std::clamp(s->value(QStringLiteral("display/slopeLimit"), d.slopeLimit).toInt(), 1, 89);
	s->beginGroup(QStringLiteral("shortcuts"));
	for (const QString &key : s->childKeys())
		shortcuts[QString(key).replace(QLatin1Char('|'), QLatin1Char('/'))] = s->value(key).toString();
	s->endGroup();
	const int n = s->beginReadArray(QStringLiteral("recent"));
	for (int i = 0; i < n; ++i)
	{
		s->setArrayIndex(i);
		RecentMap r;
		r.path = s->value(QStringLiteral("path")).toString();
		r.game = gameFromKey(s->value(QStringLiteral("game")).toString());
		r.opened = s->value(QStringLiteral("opened")).toDateTime();
		if (!r.path.isEmpty())
			recent.push_back(r);
	}
	s->endArray();
}

void AppState::save() const
{
	const auto s = openSettings(m_iniPath);
	s->setValue(QStringLiteral("appearance/theme"), theme == ThemeKind::Light ? QStringLiteral("light") : QStringLiteral("dark"));
	s->setValue(QStringLiteral("appearance/language"), language == Language::German ? QStringLiteral("de") : QStringLiteral("en"));
	s->setValue(QStringLiteral("game/selected"), gameKey(game));
	s->setValue(QStringLiteral("data/generals"), m_dataGenerals);
	s->setValue(QStringLiteral("data/zh"), m_dataZeroHour);
	s->setValue(QStringLiteral("game/programGenerals"), m_gameGenerals);
	s->setValue(QStringLiteral("game/programZh"), m_gameZeroHour);
	s->setValue(QStringLiteral("autosave/enabled"), autosave);
	s->setValue(QStringLiteral("autosave/seconds"), autosaveSeconds);
	s->setValue(QStringLiteral("display/models"), display.models);
	s->setValue(QStringLiteral("display/shadows"), display.shadows);
	s->setValue(QStringLiteral("display/clouds"), display.clouds);
	s->setValue(QStringLiteral("display/macroTexture"), display.macroTexture);
	s->setValue(QStringLiteral("display/macroTextureName"), display.macroTextureName);
	s->setValue(QStringLiteral("display/softWater"), display.softWater);
	s->setValue(QStringLiteral("display/wireframe"), display.wireframe);
	s->setValue(QStringLiteral("display/extraBlends"), display.extraBlends);
	s->setValue(QStringLiteral("display/sounds"), display.sounds);
	s->setValue(QStringLiteral("display/contours"), display.contours);
	s->setValue(QStringLiteral("display/contourStep"), display.contourStep);
	s->setValue(QStringLiteral("display/contourOffset"), display.contourOffset);
	s->setValue(QStringLiteral("display/grid"), display.grid);
	s->setValue(QStringLiteral("display/boundingBoxes"), display.boundingBoxes);
	s->setValue(QStringLiteral("display/sightRanges"), display.sightRanges);
	s->setValue(QStringLiteral("display/weaponRanges"), display.weaponRanges);
	s->setValue(QStringLiteral("display/testArt"), display.testArt);
	s->setValue(QStringLiteral("display/letterbox"), display.letterbox);
	s->setValue(QStringLiteral("display/garrisoned"), display.garrisoned);
	s->setValue(QStringLiteral("display/labels"), display.labels);
	s->setValue(QStringLiteral("display/objectIcons"), display.objectIcons);
	s->setValue(QStringLiteral("display/snapToGrid"), display.snapToGrid);
	s->setValue(QStringLiteral("display/brushOutline"), display.brushOutline);
	s->setValue(QStringLiteral("display/graphicsLevel"), display.graphicsLevel);
	s->setValue(QStringLiteral("display/effectsOff"), display.effectsOff.join(QLatin1Char(',')));
	s->setValue(QStringLiteral("display/slopes"), display.slopes);
	s->setValue(QStringLiteral("display/slopeLimit"), display.slopeLimit);
	// '/' separates groups in the settings; the command paths store it as '|'.
	s->remove(QStringLiteral("shortcuts"));
	s->beginGroup(QStringLiteral("shortcuts"));
	for (const auto &[command, keys] : shortcuts)
		s->setValue(QString(command).replace(QLatin1Char('/'), QLatin1Char('|')), keys);
	s->endGroup();
	s->beginWriteArray(QStringLiteral("recent"), static_cast<int>(recent.size()));
	for (std::size_t i = 0; i < recent.size(); ++i)
	{
		s->setArrayIndex(static_cast<int>(i));
		s->setValue(QStringLiteral("path"), recent[i].path);
		s->setValue(QStringLiteral("game"), gameKey(recent[i].game));
		s->setValue(QStringLiteral("opened"), recent[i].opened);
	}
	s->endArray();
}

void AppState::addRecent(const QString &path)
{
	const QString clean = QDir::cleanPath(path);
	recent.erase(std::remove_if(recent.begin(), recent.end(), [&](const RecentMap &r) { return QDir::cleanPath(r.path) == clean; }), recent.end());
	recent.insert(recent.begin(), RecentMap{clean, game, QDateTime::currentDateTime()});
	if (recent.size() > MAX_RECENT)
		recent.resize(MAX_RECENT);
}

QString AppState::userMapFolder(Game forGame) const
{
	const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
	const QString leaf = forGame == Game::Generals ? QStringLiteral("GZH Neo/Generals")
		: QStringLiteral("GZH Neo/Zero Hour");
	return QDir(docs).filePath(leaf + QStringLiteral("/Maps"));
}

QString AppState::dataFolder(Game forGame) const
{
	const QString &set = forGame == Game::Generals ? m_dataGenerals : m_dataZeroHour;
	if (!set.isEmpty())
		return set;
	const QString fallback = QString::fromUtf8(forGame == Game::Generals ? MAPEDITOR_DEFAULT_DATA_GENERALS : MAPEDITOR_DEFAULT_DATA_ZERO_HOUR);
	return looksLikeGameData(fallback) ? QDir::toNativeSeparators(fallback) : QString();
}

void AppState::setDataFolder(Game forGame, const QString &folder)
{
	(forGame == Game::Generals ? m_dataGenerals : m_dataZeroHour) = folder;
}

QString AppState::gameProgram(Game forGame) const
{
	const QString &set = forGame == Game::Generals ? m_gameGenerals : m_gameZeroHour;
	if (!set.isEmpty())
		return set;
	const QString fallback = QString::fromUtf8(forGame == Game::Generals ? MAPEDITOR_DEFAULT_GAME_GENERALS : MAPEDITOR_DEFAULT_GAME_ZERO_HOUR);
	return QFileInfo::exists(fallback) ? QDir::toNativeSeparators(fallback) : QString();
}

void AppState::setGameProgram(Game forGame, const QString &program)
{
	(forGame == Game::Generals ? m_gameGenerals : m_gameZeroHour) = program;
}

bool AppState::looksLikeGameData(const QString &folder)
{
	if (folder.isEmpty())
		return false;
	const QDir dir(folder);
	return dir.exists(QStringLiteral("Data/INI")) || !dir.entryList({QStringLiteral("*.big")}, QDir::Files).isEmpty();
}

namespace
{

// One settings group per map file; the path itself is no valid key.
QString bookmarkGroup(const QString &mapPath)
{
	return QStringLiteral("bookmarks/") + QString::number(qHash(QDir::cleanPath(mapPath).toLower()), 16);
}

} // namespace

std::vector<CameraBookmark> AppState::bookmarks(const QString &mapPath) const
{
	if (mapPath.isEmpty())
		return m_unsavedBookmarks;
	std::vector<CameraBookmark> list;
	const auto s = openSettings(m_iniPath);
	const int n = s->beginReadArray(bookmarkGroup(mapPath));
	for (int i = 0; i < n; ++i)
	{
		s->setArrayIndex(i);
		CameraBookmark b;
		b.name = s->value(QStringLiteral("name")).toString();
		b.x = s->value(QStringLiteral("x")).toFloat();
		b.y = s->value(QStringLiteral("y")).toFloat();
		b.z = s->value(QStringLiteral("z")).toFloat();
		b.yaw = s->value(QStringLiteral("yaw")).toFloat();
		b.pitch = s->value(QStringLiteral("pitch")).toFloat();
		b.distance = s->value(QStringLiteral("distance")).toFloat();
		list.push_back(b);
	}
	s->endArray();
	return list;
}

void AppState::setBookmarks(const QString &mapPath, const std::vector<CameraBookmark> &list)
{
	if (mapPath.isEmpty())
	{
		m_unsavedBookmarks = list;
		return;
	}
	const auto s = openSettings(m_iniPath);
	s->remove(bookmarkGroup(mapPath));
	s->beginWriteArray(bookmarkGroup(mapPath), static_cast<int>(list.size()));
	for (std::size_t i = 0; i < list.size(); ++i)
	{
		s->setArrayIndex(static_cast<int>(i));
		const CameraBookmark &b = list[i];
		s->setValue(QStringLiteral("name"), b.name);
		s->setValue(QStringLiteral("x"), b.x);
		s->setValue(QStringLiteral("y"), b.y);
		s->setValue(QStringLiteral("z"), b.z);
		s->setValue(QStringLiteral("yaw"), b.yaw);
		s->setValue(QStringLiteral("pitch"), b.pitch);
		s->setValue(QStringLiteral("distance"), b.distance);
	}
	s->endArray();
}

} // namespace MapEditorApp
