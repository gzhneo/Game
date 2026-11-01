/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// AppState.h -- editor settings and the open map.
//
// Settings live in an INI file in the user profile (%APPDATA%\GZH Neo\MapEditor.ini), never in the
// registry and never in the game's Options.ini. The self test passes its own file so it leaves the
// user's settings alone.

#pragma once

#include "App/Language.h"
#include "App/Theme.h"
#include "Core/History.h"
#include "Core/ObjectCommands.h"
#include "Model/MapFile.h"

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <map>
#include <memory>
#include <vector>

namespace MapEditorApp
{

struct RecentMap
{
	QString path;
	Game game = Game::ZeroHour;
	QDateTime opened;
};

struct OpenMap
{
	QString path;
	MapModel::LoadedMap loaded;
	MapEditorCore::History history;
	MapEditorCore::Selection selection;

	MapModel::Map &map() { return loaded.result.map; }
};

// What the views show (view menu); kept in the settings.
struct DisplayOptions
{
	bool models = true;
	bool shadows = true;
	bool clouds = true;
	bool macroTexture = true;
	QString macroTextureName;		// empty: the game's default
	bool softWater = true;
	bool wireframe = false;
	bool extraBlends = false;		// cells with a third texture drawn black
	bool sounds = false;			// ambient sounds with their ranges
	bool contours = false;			// contour lines in the 2D overview
	int contourStep = 8;			// height map units
	int contourOffset = 0;
	bool grid = true;				// grid in the 2D overview when zoomed in
	bool boundingBoxes = false;		// footprint of each object
	bool sightRanges = false;
	bool weaponRanges = false;
	bool testArt = false;			// objects whose type the game lacks
	bool letterbox = false;			// the game's 16:9 cinematic bars
	bool garrisoned = false;		// buildings in their garrisoned state
	bool labels = false;			// names in the 2D overview
	bool objectIcons = true;		// object dots in the 2D overview
	bool snapToGrid = false;		// placing and moving snap to the cell grid
	bool brushOutline = true;		// outline of the terrain brush
	// The game's detail level the 3D view draws with (StaticGameLOD name in GameLODOverride.ini);
	// empty: the classic look without Direct3D 11 effects. Medium is the level of most players.
	QString graphicsLevel = QStringLiteral("Medium");
	QStringList effectsOff;			// effects of the level switched off ("water" for the realistic water)
	bool slopes = false;			// slope map in 2D, cells steeper than slopeLimit red in 3D
	int slopeLimit = 45;			// degrees; the game's default for visible cliffs
};

// A saved camera position of a map.
struct CameraBookmark
{
	QString name;
	float x = 0.0f, y = 0.0f, z = 0.0f;
	float yaw = 0.0f, pitch = 0.0f, distance = 0.0f;
};

class AppState
{
public:
	// iniPath empty: the user's settings file.
	explicit AppState(const QString &iniPath = QString());

	ThemeKind theme = ThemeKind::Dark;
	Language language = Language::English;
	Game game = Game::ZeroHour;
	std::vector<RecentMap> recent;
	std::unique_ptr<OpenMap> map;

	void save() const;
	void addRecent(const QString &path);

	// Maps in the user map folder of the selected game (Documents\...\Maps).
	QString userMapFolder(Game game) const;

	// Game data for the 3D view: the folder with the game's *.big and Data\ (the asset copy of
	// tools/assets/setup_assets.py or an installation). Empty if not set and no build default exists.
	QString dataFolder(Game game) const;
	void setDataFolder(Game game, const QString &folder);
	// The game program "Test in game" starts; the build's own game if not set.
	QString gameProgram(Game game) const;
	void setGameProgram(Game game, const QString &program);
	// Autosave of the open map while it has unsaved changes.
	bool autosave = true;
	int autosaveSeconds = 120;
	DisplayOptions display;
	// Own shortcuts by command (English menu path, e.g. "Edit/Check map..."), portable key text; empty: none.
	std::map<QString, QString> shortcuts;
	// Camera bookmarks per map file (maps without a file keep theirs until the editor closes).
	std::vector<CameraBookmark> bookmarks(const QString &mapPath) const;
	void setBookmarks(const QString &mapPath, const std::vector<CameraBookmark> &list);
	// Whether the folder looks like game data (archives or a Data\INI folder).
	static bool looksLikeGameData(const QString &folder);

private:
	QString m_iniPath;
	std::vector<CameraBookmark> m_unsavedBookmarks;
	QString m_dataGenerals;
	QString m_dataZeroHour;
	QString m_gameGenerals;
	QString m_gameZeroHour;
};

} // namespace MapEditorApp
