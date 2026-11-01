/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindow.h -- the editor window: top bar with menus, command search and game badge; pages for
// the start page, the map viewer and the settings; status bar.
//
// A change of theme, language or game rebuilds the pages, so every text and colour follows at once
// without per-widget update code. The open map and the settings live in AppState.

#pragma once

#include "App/AppState.h"
#include "App/EditorContext.h"
#include "App/ScriptCatalog.h"
#include "App/Windows/MapSettingsWindow.h"
#include "App/Overlay3D.h"
#include "Core/HeightShapes.h"
#include "Core/MapCheck.h"
#include "Core/Symmetry.h"
#include "Core/WaterLook.h"
#include "Core/MapSettingsCommands.h"
#include "Core/MapSize.h"
#include "Core/PlacementCommands.h"
#include "Core/ScriptCommands.h"
#include "Core/SidesCommands.h"
#include "Core/TerrainCommands.h"
#include "Model/MapIO.h"

#include <QColor>
#include <QDateTime>
#include <QDialog>
#include <QKeySequence>
#include <QMainWindow>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QTreeWidget>

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace MapEditorCore
{
struct GraphicsLevel;
struct MapDifference;
struct Reference;
}

class QApplication;
class QDoubleSpinBox;
class QLabel;
class QMenuBar;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTreeWidget;
class QVBoxLayout;

class QTimer;

namespace MapEditorApp
{

class ParticlesWindow;
class ScriptsWindow;
class SidesWindow;

struct ToolEvent;

class MainWindow : public QMainWindow, public EditorContext, public MapSettingsContext
{
public:
	enum class Page
	{
		Start,
		Map,
		Settings
	};

	MainWindow(AppState &state, QApplication &app);

	bool openMapFile(const QString &path, QString *error = nullptr);
	// For the self test: shows a map that has no file.
	void showMapModel(MapModel::Map map, const QString &name);

	void showPage(Page page);
	Page page() const { return m_page; }

	// Applies theme, language and game from the state and rebuilds the window content.
	void applySettings();

	// Engine log of this run (default: next to the settings).
	void setEngineLog(const QString &path) { m_engineLog = path; }
	// Self test: switches the map page to the 3D view and saves the next frame as BMP. False with
	// the reason shown on the page if the 3D view is not available.
	bool capture3D(const QString &bmpPath, QString *error, bool wholeMap = false);
	// Where the 3D view sits in the window (self test: a window grab does not contain its image).
	QRect viewportRect() const;
	// Self test of editing in the 3D view: picks an object, moves it (frame saved as BMP), undoes and
	// redoes the move and undoes it again, so the map ends unmodified. Adds one line per step to
	// report; false if a step fails.
	bool selfTestEdit(const QString &bmpPath, QStringList &report);
	// Part of selfTestEdit: places the template near the object in both views and undoes it.
	// Streams the C runtime can still open (the editor and the engine DLL share its table of open
	// files); the self test uses it to find files left open.
	static int freeFileStreams();
	// Part of selfTestEdit: the height brush in the overview and the 3D view, then undo.
	bool selfTestTerrain(const QString &shotDir, QStringList &report);
	// Part of selfTestEdit: the road tool in the overview and the 3D view, then undo.
	bool selfTestRoads(const QString &shotDir, QStringList &report);
	// Part of selfTestEdit: the area tools in the overview and the 3D view, then undo.
	bool selfTestAreas(const QString &shotDir, QStringList &report);
	// Part of selfTestEdit: the waypoint tool in the overview and the 3D view, then undo.
	bool selfTestWaypoints(const QString &shotDir, QStringList &report);
	bool selfTestCatalog(const std::string &templateName, int nearObject, const QString &shotDir, QStringList &report);

protected:
	void closeEvent(QCloseEvent *event) override;

private:
	void rebuild();
	QWidget *buildTopBar();
	QWidget *buildStartPage();
	QWidget *buildMapPage();
	QWidget *buildSettingsPage();
	void buildMenus(class QMenuBar *bar);
	void openDialog();
	void save(bool saveAs);
	void closeMap();
	void showCommands();
	// Entries of the command palette: menu commands, settings, and "Go to" for the map's names.
	struct PaletteEntry
	{
		QString text;
		QString detail;			// shortcut
		std::function<void()> run;
	};
	std::vector<PaletteEntry> paletteEntries();
	void goToArea(int index);
	// Moves the views to the world position (the 3D camera keeps its angles and distance).
	void centerOn(float x, float y);
	void addBookmark();
	void goToBookmark(int index);
	bool exportMapText(const QString &path, QString *error);
	bool exportMapPicture(const QString &path, int pixelsPerCell, QString *error);
	void showAbout();
	// Prefabs (MainWindowPrefabs.cpp).
	QString prefabFolder() const;
	QStringList prefabNames() const;
	bool savePrefab(const QString &name, QString *error);
	void saveSelectionAsPrefab();
	// Places the prefab with its middle at the world position, as one step.
	bool placePrefab(const QString &name, float x, float y);
	void deletePrefabDialog();
	// Comparing maps and the height map as a picture (MainWindowCompare.cpp).
	QString differenceText(const MapEditorCore::MapDifference &d) const;
	std::vector<MapEditorCore::MapDifference> compareWithFile(const QString &path, QString *error);
	void compareDialog();
	bool exportHeightmap(const QString &path, QString *error);
	// Replaces the heights from the picture as one step; false (with the reason) if nothing changed.
	bool importHeightmap(const QString &path, int low, int high, QString *error);
	void importHeightmapDialog();
	// The particle system editor (Windows/ParticlesWindow).
	void showParticles();
	// Keyboard shortcuts (MainWindowShortcuts.cpp).
	void applyShortcuts(QMenuBar *bar);
	// The command already using the keys (its menu path as shown), empty if none.
	QString shortcutConflict(const QKeySequence &keys, const QString &exceptId);
	void showShortcuts();
	// map.ini and solo.ini beside the map: the game reads them when the map loads.
	void showMapIni();
	void toast(const QString &text) override;
	void updateTitle();
	QWidget *buildViewArea(const Tokens &t);
	bool prepare3D(QString *error, bool *chooseData, bool *restart);
	// Starts the engine for the game of the map if it is not running (catalog and 3D view).
	bool ensureEngine(QString *error, bool *chooseData, bool *restart);
	void chooseDataFolder(Game game);
	void restartEditor();
	QString engineLog() const;
	// Sends the overlay of the current map to the 3D view.
	void applyOverlay();
	// The map as the engine reads it: every object carries its model index ("editorIndex").
	MapModel::WriteResult engineMapBytes() const;

	// Editing: selection and moves from the 3D view, undo and redo.
	void clickObject(int object, bool toggle, bool released);
	void selectRect(const std::vector<int> &objects, bool add);
	void selectAll();
	void selectNone();
	// Selection helpers of the old WorldBuilder (MainWindowEdit.cpp): the pick filter by editor sorting
	// (0 anything), duplicates, similar objects, objects whose team the map lacks.
	void setPickFilter(int sorting);
	bool pickable(int object) const;
	void selectDuplicates();
	void selectSimilar();
	void selectInvalidTeams();
	void showLayers();
	// The undo history as a list (MainWindowHistory.cpp); a click goes to that state.
	void showHistory();
	void refreshHistoryWindow();
	void goToHistory(int position);
	// Translated menu text of a step of the history.
	static QString stepName(const std::string &name);
	// Search in the whole map (MainWindowSearch.cpp): where a name is used, replace in the scripts.
	void showReference(const MapEditorCore::Reference &r);
	void showSearch(const QString &text = QString(), bool wholeValue = false);
	int replaceInScripts(const QString &text, const QString &with, bool wholeValue, bool matchCase);
	QString selectionName() const;
	void whereUsed();
	// The multiplayer fairness report (MainWindowFairness.cpp) and its territories in the views.
	void showTerritories(const std::vector<std::uint32_t> &tint);
	void showFairness();
	// Symmetry from one part of the map (MainWindowSymmetry.cpp); the lines show in the views while
	// the dialog is open.
	void showSymmetryLines(std::vector<float> lines);
	bool applySymmetry(const MapEditorCore::SymmetryOptions &options);
	void showSymmetry();
	// The built-in help (MainWindowHelp.cpp): F1 opens the topic of the window or tool in use.
	QString helpTopic() const;
	void showHelp(const QString &topic);
	void refreshLayers();
	void setLayerHidden(const std::string &layer, bool hidden);
	void setActiveLayer(const std::string &layer);
	std::string newLayer();
	void renameLayer(const std::string &from, const std::string &to);
	void deleteLayer(const std::string &layer);
	void moveSelectionToLayer(const std::string &layer);
	void selectLayer(const std::string &layer);
	void groupSelection();
	void ungroupSelection();
	// Selection changed: radii from the engine, overlay, status line, side panel.
	void selectionChanged();
	void dragSelection(float dx, float dy);
	void endDragSelection(bool cancel);
	// Turns the selection by the angle the cursor sweeps around its centre (one object: its own
	// position). snap: 15-degree steps.
	void rotateSelection(float startX, float startY, float x, float y, bool snap);
	void endRotateSelection(bool cancel);
	// Turns the selection by delta radians around its centre (drag and self test).
	void turnSelection(float delta);
	void deleteSelection();
	// Clipboard of the editor (kept across maps): copy, cut, paste at the cursor in the 3D view (else
	// 30 units away from the originals), duplicate without touching the clipboard.
	void copySelection();
	void cutSelection();
	void paste();
	void duplicateSelection();
	// Inserts the objects, moved so their centre lies at the cursor (or offset if it is elsewhere).
	void insertCopies(const MapEditorCore::Clipboard &clipboard, const std::string &name);
	// The system clipboard carries copies as map chunk data in this format (and their names as text).
	static constexpr const char *CLIPBOARD_FORMAT = "application/x-gzhneo-map-objects";
	// False if the system clipboard could not be set; the copy then stays inside the editor.
	bool toSystemClipboard(const MapEditorCore::Clipboard &clipboard);
	std::optional<MapEditorCore::Clipboard> fromSystemClipboard() const;
	void undo();
	void redo();
	// Brings the 3D view and the panel in line with the model after a command.
	void applyChanges(const MapEditorCore::Changes &changes);
	void updateSelectionStatus();
	// Side panel of the selection: rebuilt when the selection changes, values refreshed on moves.
	void fillSelectionPanel();
	void refreshSelectionValues();
	// A value typed into the panel: 0 = X, 1 = Y, 2 = height, 3 = angle in degrees.
	void editSelected(int field, double value);
	// Changes every selected object that is not a waypoint; one step. False if nothing changed.
	void addLightEditors(QVBoxLayout *box, const MapModel::MapObject &light);
	// Records a change of the cliff mapping; the 3D view loads the map again to show it.
	void recordCliffs(const MapModel::BlendTiles &before, const char *name);
	bool editSelectedObjects(const std::function<void(MapModel::MapObject &)> &change, const std::string &commandName);
	// Ambient sound of the selected objects, shown for the first (MainWindowSound.cpp).
	void addSoundEditors(QVBoxLayout *box, const MapModel::MapObject &shown);
	// Properties of the selection in the panel (MainWindowProps.cpp).
	void addPropertyEditors(QVBoxLayout *box);
	// Sets (or with no value removes) a property on the selected objects, or on the selected
	// waypoints; one step in the history. False if nothing changed.
	bool setSelectedProperty(const std::string &key, const std::optional<MapModel::PropertyValue> &value,
		const std::string &commandName, bool waypoints);
	// Objects mode (MainWindowCatalog.cpp): the catalog on the left; a chosen template is placed by a
	// click in the 3D view or the overview. Esc ends placing, then clears the selection.
	QWidget *buildCatalogPanel(const Tokens &t);
	void setPlacingTemplate(const QString &name, float placementAngle);
	// object: the object under the cursor (-1 if none); ctrl: Ctrl was held (waypoints: new path).
	void placeObject(float x, float y, float angle, bool aimed, int object = -1, bool ctrl = false);
	void escape();
	// Waypoints (MainWindowWaypoints.cpp). A click of the waypoint tool: on a waypoint it links or
	// unlinks it with the selected one (Ctrl: only selects), elsewhere it places a new waypoint
	// linked from the selected one (Ctrl: a new path).
	void placeWaypoint(float x, float y, int object, bool ctrl);
	void linkSelectedWaypoints();
	void unlinkSelectedWaypoints();
	// Runs a command of the core (null: nothing to do) and updates the views. False if null.
	bool runCommand(std::unique_ptr<MapEditorCore::Command> command);
	// Areas (MainWindowAreas.cpp): the catalog entries for trigger and water areas are tools, not
	// templates; the views report the left button to them (ToolEvent).
	static constexpr const char *AREA_TRIGGER_TOOL = "*Areas/Trigger";
	static constexpr const char *AREA_WATER_TOOL = "*Areas/Water";
	bool areaTool() const;
	void areaToolEvent(const ToolEvent &e);
	void areaToolKey(int key);
	void finishAreaDraft();
	// Del with an area tool: the area being drawn, the selected point (an area keeps three), else
	// the selected area. False if there is none.
	bool deleteAreaSelection();
	// The selected area, its point or the draft changed: overview marks and overlay.
	void areasChanged();
	void addAreaEditors(QVBoxLayout *box);
	// Terrain mode (MainWindowTerrain.cpp): the height brush in both views.
	bool terrainTool() const;
	QWidget *buildTerrainPanel(const Tokens &t);
	void terrainToolEvent(const ToolEvent &e);
	// The cursor over the ground (brush circle, height under it).
	void brushMoved(bool valid, float x, float y);
	std::vector<float> brushCircle() const;
	// Heights changed (a stroke, undo, redo): the engine's terrain, objects on it, the overview.
	void terrainChanged(const MapEditorCore::Changes &changes);
	// The texture brush of the terrain mode (MainWindowTerrain.cpp).
	enum class TerrainTool
	{
		Height,
		Texture,
		Shape,
		Pathing
	};
	// Texture brush: paint (with soft edges if chosen), blend one edge by a drag, or auto edge a region.
	enum class TextureMode
	{
		Paint,
		Fill,
		Edge,
		AutoEdge,
		Cliffs
	};
	enum class ShapeTool
	{
		Ramp,
		Mold
	};
	void textureToolEvent(const ToolEvent &e);
	void addTexturePanel(QVBoxLayout *col, const Tokens &t);
	// Ramps and mesh molds (MainWindowTerrain.cpp).
	void addShapePanel(QVBoxLayout *col);
	void shapeToolEvent(const ToolEvent &e);
	// Enter and Backspace in tool mode: to the area tool, or the mold being placed.
	void toolKey(int key);
	// Lines the terrain tools draw: the ramp, the mold's outline, the edge being dragged.
	std::vector<float> terrainLines() const;
	// Built-in molds and those of the game's mold files.
	void refreshMolds();
	// Puts the mold at its placement over the heights it started from (preview).
	void placeMold();
	// Ends the mold preview: as one step in the history, or back to the heights before.
	void finishMold(bool apply);
	// Passability brush (MainWindowTerrain.cpp): marks cells impassable or passable.
	void addPathingPanel(QVBoxLayout *col);
	void pathingToolEvent(const ToolEvent &e);
	// Impassable cells shaded in both views while the passability brush is chosen.
	void updateImpassableView();
	// Texture under a cell becomes the brush's texture (Alt+click).
	void pickTexture(int x, int y);
	// A texture step (edge, auto edge) into the history and the views.
	void recordTiles(const MapModel::BlendTiles &before, MapEditorCore::CellRect rect, const char *name);
	// Loads the map into the engine again with the camera where it is (new texture classes).
	void reloadEngineMap();
	// Roads (MainWindowRoads.cpp): a catalog entry per road type is the road tool.
	static constexpr const char *ROAD_TOOL_PREFIX = "*Road/";
	static constexpr const char *BRIDGE_TOOL_PREFIX = "*Bridge/";
	// Groves, fences and scorch marks (MainWindowPlacement.cpp): catalog entries that are tools.
	static constexpr const char *GROVE_TOOL = "*Grove";
	static constexpr const char *FENCE_TOOL_PREFIX = "*Fence/";
	static constexpr const char *PREFAB_TOOL_PREFIX = "*Prefab/";
	static constexpr const char *SCORCH_TOOL = "*Scorch";
	// Autosave and test in game (MainWindowSession.cpp).
	struct AutosaveInfo
	{
		bool pending = false;				// holds work that was never saved
		QString file;
		QString map;						// path of the map it came from, empty if unnamed
		QDateTime time;
		Game game = Game::ZeroHour;
	};
	QString autosaveFolder() const;
	void startAutosaveTimer();
	bool autosaveNow();
	void markAutosaveClean();
	AutosaveInfo autosaveInfo() const;
	bool recoverAutosave();
	// Program and arguments to start the game with the saved map; empty with error if not possible.
	QStringList gameLaunch(QString *program, QString *mapFile, QString *error);
	// Writes the map as the game's test copy into folder, with the map.ini and solo.ini beside the
	// map and the changed particle systems appended to map.ini. Returns the map file, empty on error.
	QString writeTestCopy(const QString &folder, QString *error);
	void testInGame();
	// Build list tool (MainWindowBuildList.cpp).
	static constexpr const char *BUILD_LIST_TOOL = "*BuildList";
	struct BuildDrag
	{
		bool active = false;
		bool adding = false;
		bool turning = false;
		int index = -1;
		float x0 = 0.0f, y0 = 0.0f, x = 0.0f, y = 0.0f;
		float startX = 0.0f, startY = 0.0f;
		MapEditorCore::EditSides::State before;
	};
	bool buildListTool() const;
	const MapModel::BuildListEntry *buildEntry(int side, int index) const;
	std::vector<float> buildListLines() const;
	// The entries of every player as objects of the engine's copy of the map, to be drawn.
	void appendBuildListObjects(MapModel::Map &copy) const;
	void buildListChanged();
	void buildListToolEvent(const ToolEvent &e);
	void addBuildListEditors(QVBoxLayout *box);
	bool exportBuildListIni(const QString &path, int side, QString *error);
	// Map settings and playable areas (MainWindowSettings.cpp).
	static constexpr const char *BORDER_TOOL = "*Border";
	// Camera, ruler, preview picture (MainWindowCamera.cpp).
	static constexpr const char *RULER_TOOL = "*Ruler";
	bool rulerTool() const;
	QString rulerText() const;
	std::vector<float> rulerLines() const;
	void rulerToolEvent(const ToolEvent &e);
	void addRulerEditors(QVBoxLayout *box);
	void setCameraYaw(float degrees);
	void centerOnSelection();
	void waypointAtCamera();
	void showCameraWindow();
	void refreshCameraWindow();
	std::vector<std::uint32_t> previewPixels() const;
	// The preview picture next to the map (<name>.tga), as the game shows it in the map lists.
	bool writePreview(const QString &mapPath);
	// Players and teams (MainWindowSides.cpp).
	// Changes players and teams as one step; change returns false if it did nothing. refresh false:
	// the window made the change and keeps its fields.
	bool editSides(const char *name, const std::function<bool(MapModel::Map &)> &change, bool mergeable = false, bool refresh = true) override;
	void sidesChanged();
	void fillSidesTree();
	// tab: 0 players, 1 teams, -1 as last shown.
	void showSides(int tab = -1);
	SidesWindow *sidesWindow();
	void selectTeam(int teamIndex) override;
	Game game() const override;
	void selectTeamMembers(int teamIndex);
	// Scripts (MainWindowScripts.cpp, Windows/ScriptsWindow, ScriptCatalog).
	bool editScripts(const char *name, const std::function<bool(MapModel::Sides &)> &change, bool mergeable = false, bool refresh = true) override;
	void scriptsChanged();
	// Shows the scripts window, at the given script if one is given.
	void showScripts(const MapEditorCore::ScriptPath *at = nullptr);
	ScriptsWindow *scriptsWindow();
	// EditorContext for the editor's own windows.
	OpenMap *openMap() override;
	QString mapFolder() const override;
	bool hasView() const override;
	const MapModel::MapObject *firstSelectedObject() const override;
	void recordStep(std::unique_ptr<MapEditorCore::Command> step) override;
	// What the views show (MainWindowDisplay.cpp).
	MeDisplayOptions engineDisplay() const;
	// Sends the display options to the engine and the overview.
	void applyDisplay();
	void setDisplay(bool DisplayOptions::*flag, bool on);
	// A change of lighting, time of day or shadow colour: goes to the view at once, no reload.
	void editLighting(const char *name, const std::function<void(MapModel::Map &)> &change) override;
	void previewLighting(const MapModel::GlobalLighting &lighting) override;
	void pushLighting();
	// The water look of the map (MainWindowWater.cpp): the game data with the map's map.ini over them.
	std::optional<MapEditorCore::WaterLook> mapWaterLook();
	void pushMapWaterLook();
	bool writeMapWaterLook(QString *error) override;
	std::optional<MapEditorCore::WaterLook> &waterLook() override;
	void showWaterLook() override;
	void resetWaterLook() override;
	// The 3D view as a poster, scale times its size (MainWindowPoster.cpp).
	bool renderPoster(int scale, bool withOverlay, QImage *out, QString *error);
	bool exportPoster(const QString &path, int scale, bool withOverlay, QString *error);
	void showPosterDialog();
	// The game's detail levels (GameLODOverride.ini of the project, in the resources).
	static const std::vector<MapEditorCore::GraphicsLevel> &graphicsLevels();
	void setGraphicsLevel(const QString &level);
	void setEffectOff(const QString &effect, bool off);
	void fillGraphicsMenu(QMenu *menu);
	void slopeOptions();
	// Minimum and maximum range of the ambient sound the object plays; false if none.
	bool objectSoundRange(const MapModel::MapObject &object, float &minRange, float &maxRange);
	void chooseMacroTexture();
	void contourOptions();
	// The catalog entry of a template; null without the engine or for a type the game lacks.
	const MeTemplateInfo *templateInfo(const std::string &templateName);
	// A world coordinate on the cell grid if "snap to grid" is on, else unchanged.
	float snapCoord(float v) const;
	// Reads the textures again from the files and loads the map anew in the 3D view.
	void reloadTextures();
	// Problem list (MainWindowProblems.cpp).
	// The map check with what the started game knows; without the engine, types, factions and
	// scripts are left out.
	std::vector<MapEditorCore::Problem> mapProblems();
	QString problemText(const MapEditorCore::Problem &p) const;
	// Runs fix on the map as one step; false if nothing changed.
	bool fixMap(const char *name, const std::function<int(MapModel::Map &)> &fix);
	// The fixes that need no choice, as one step; returns how many problems they fixed.
	int applySuggestions();
	void showProblem(const MapEditorCore::Problem &p);
	void scheduleProblemCheck();
	// Another map (or none): the old problems go, the new map is checked.
	void mapReplaced();
	void refreshProblems();
	void fillProblemFix(int index);
	void showProblems();
	// New map and resize (MainWindowMapSize.cpp).
	// Size-independent values of a new map from the game data: the texture (by name, or a grass),
	// lighting and water height. nullopt without the engine.
	std::optional<MapEditorCore::NewMapOptions> newMapOptions(const QString &texture, QString *error);
	void newMapDialog();
	void openNewMap(MapModel::Map map);
	void resizeMapDialog();
	bool resizeMapTo(const MapEditorCore::ResizeOptions &options);
	void mapResized();
	bool borderTool() const;
	void boundaryToolEvent(const ToolEvent &e);
	std::vector<float> boundaryLines() const;
	void addBoundaryEditors(QVBoxLayout *box);
	bool deleteBoundarySelection();
	void showMapSettings();
	// Changes the settings in the map as one step (same-named steps in a row fold together).
	void editSettings(const char *name, const std::function<void(MapModel::Map &)> &change) override;
	void settingsChanged(const MapEditorCore::Changes &changes);
	bool placementTool() const;
	bool groveTool() const;
	bool fenceTool() const;
	bool scorchTool() const;
	void placementToolEvent(const ToolEvent &e);
	void addPlacementEditors(QVBoxLayout *box);
	std::vector<float> placementLines() const;
	// New objects as one step (a paste), selected afterwards.
	void insertObjects(std::vector<MapModel::MapObject> objects, const char *name);
	// Every tool event of the views goes here and on to the tool that is chosen.
	void toolEvent(const ToolEvent &e);
	// Road or bridge tool (the same tool; bridges neither snap nor cut roads).
	bool roadTool() const;
	bool bridgeTool() const;
	std::string roadToolType() const;
	void roadToolEvent(const ToolEvent &e);
	bool deleteRoadSelection();
	// The segment being drawn and the selected one, as world lines x1, y1, x2, y2.
	std::vector<float> toolLines() const;
	void roadsChanged();
	void addRoadEditors(QVBoxLayout *box);
	// Model of the chosen template in the catalog, turned by m_previewYaw, in the colour of the team.
	void updateCatalogPreview();
	// Asks before unsaved changes would be lost; false if the user cancels.
	bool confirmUnsavedChanges();

	AppState &m_state;
	QApplication &m_app;
	Page m_page = Page::Start;
	QStackedWidget *m_pages = nullptr;
	QLabel *m_statusLeft = nullptr;
	QLabel *m_statusRight = nullptr;
	QString m_recentFilter = QStringLiteral("all");
	QString m_recentQuery;
	class Viewport3D *m_viewport = nullptr;
	bool m_show3D = false;
	enum class WorkMode
	{
		View,
		Terrain,
		Objects
	};
	WorkMode m_workMode = WorkMode::View;
	QString m_placeTemplate;			// template placed by a click; empty: selecting
	float m_placeAngle = 0.0f;
	QString m_placeTeam = QStringLiteral("team");
	QString m_catalogFilter;
	QPointer<QTreeWidget> m_catalogTree;
	QPointer<QLabel> m_catalogPreview;
	QPointer<QLabel> m_catalogPreviewName;
	QColor m_previewBackground;
	float m_previewYaw = 0.6f;			// three-quarter view from the front left
	int m_area = -1;					// area selected with an area tool, index into Map::polygons
	int m_areaPoint = -1;
	std::optional<MapModel::PolygonArea> m_areaDraft;	// being drawn, not in the map yet
	// Dragging a point or the whole area: the map shows it live, the release records one step.
	struct AreaDrag
	{
		bool active = false;
		bool whole = false;
		bool inserted = false;
		float startX = 0.0f, startY = 0.0f;
		std::optional<std::vector<MapModel::PolygonArea>> before;
	};
	AreaDrag m_areaDrag;
	int m_road = -1;					// selected road segment (its first point) with the road tool
	std::int32_t m_roadCorner = 0;		// corner flags of new roads
	bool m_roadJoin = false;			// new roads join a road of another type (ROAD_JOIN)
	struct RoadDrag
	{
		bool active = false;
		bool move = false;				// Shift+drag of a road end
		float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
		float pressX = 0.0f, pressY = 0.0f;
		std::int32_t split = -1;		// segment cut where the new one starts
		std::int32_t splitEnd = -1;		// segment cut where it ends (a junction)
		std::vector<std::int32_t> points;
		std::vector<MapModel::MapObject> before;
	};
	RoadDrag m_roadDrag;
	MapEditorCore::Brush m_brush;
	// A stroke of the brush: all heights from its start and the cells it changed.
	struct Stroke
	{
		bool active = false;
		std::vector<std::uint8_t> before;
		MapEditorCore::CellRect rect;
		float lastX = 0.0f, lastY = 0.0f;	// cells
	};
	Stroke m_stroke;
	bool m_brushValid = false;
	float m_brushX = 0.0f, m_brushY = 0.0f;
	QPointer<QSpinBox> m_brushHeightField;
	QPointer<QLabel> m_brushInfo;
	TerrainTool m_terrainTool = TerrainTool::Height;
	QString m_paintTexture;				// texture of the texture brush (Terrain.ini name)
	QString m_textureFilter;
	MapModel::BlendTiles m_tilesBefore;	// tiles at the start of a texture stroke
	int m_strokeClass = -1;				// texture class a texture stroke paints
	TextureMode m_textureMode = TextureMode::Paint;
	bool m_softEdges = true;				// the texture brush blends the edges of what it paints
	bool m_markImpassable = true;			// the passability brush marks cells impassable, else passable
	std::vector<std::uint8_t> m_cliffsBefore;	// cliff flags at the start of a passability stroke
	ShapeTool m_shapeTool = ShapeTool::Ramp;
	float m_rampWidth = 4.0f;				// cells
	// A drag of the ramp or the edge tool, world positions.
	struct ShapeDrag
	{
		bool active = false;
		float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
		int fenceCount = 0;			// with Shift held: pieces kept, spacing stretched
	};
	ShapeDrag m_shapeDrag;
	// A mesh mold being placed: the heights from before, where it is and the cells it touched.
	struct MoldState
	{
		bool active = false;
		bool dragging = false;
		int mapSerial = -1;
		std::vector<std::uint8_t> before;
		MapEditorCore::CellRect rect;		// cells the mold covers now
		MapEditorCore::CellRect touched;	// cells it covered since it was placed
		float offsetX = 0.0f, offsetY = 0.0f;	// mold position minus the grab point
	};
	MoldState m_mold;
	MapEditorCore::MoldPlacement m_moldPlace;
	std::vector<MapEditorCore::Mold> m_molds;
	int m_builtInMolds = 0;
	int m_moldIndex = 0;
	std::map<QString, MapEditorCore::Mold> m_moldFiles;	// sampled once per engine start
	QPointer<QSpinBox> m_moldHeightField;
	MapEditorCore::GroveOptions m_grove;
	std::mt19937 m_placeRandom{std::random_device{}()};
	float m_fenceSpacing = 0.0f;			// world units; the template's fence width when chosen
	float m_fenceOffset = 0.0f;
	int m_pickFilter = 0;
	QPointer<MapSettingsWindow> m_settingsWindow;
	QPointer<QDialog> m_cameraWindow;
	struct Ruler
	{
		bool valid = false;
		bool dragging = false;
		bool circle = false;
		float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
	};
	Ruler m_ruler;
	QTimer *m_reloadTimer = nullptr;
	bool m_settingsEditing = false;			// the settings window made the change (keep its fields)
	int m_boundary = -1;					// chosen playable area
	struct BoundaryDrag
	{
		bool active = false;
		int index = -1;
		MapEditorCore::BoundaryHit::Part part = MapEditorCore::BoundaryHit::Part::None;
		MapEditorCore::MapSettings before;
	};
	BoundaryDrag m_boundaryDrag;
	// Layers (MainWindowLayers.cpp): hidden ones, the one new objects go into, and new ones no object
	// uses yet.
	std::set<std::string> m_hiddenLayers;
	std::string m_activeLayer;
	std::set<std::string> m_newLayers;
	QPointer<QDialog> m_layersWindow;
	QPointer<class HistoryWindow> m_historyWindow;
	QPointer<class SearchWindow> m_searchWindow;
	QPointer<class FairnessWindow> m_fairnessWindow;
	std::vector<std::uint32_t> m_territoryTint;
	std::vector<float> m_symmetryLines;
	MapEditorCore::SymmetryMode m_symmetryMode = MapEditorCore::SymmetryMode::MirrorLeftRight;
	QPointer<class HelpWindow> m_helpWindow;
	QPointer<class SidesWindow> m_sidesWindow;
	QPointer<QTreeWidget> m_sidesTree;		// players and teams on the map page
	bool m_sidesEditing = false;			// the window made the change (keep its fields)
	QPointer<QTimer> m_autosaveTimer;
	QString m_autosaveFolder;				// set by the self test
	int m_editSerial = 0;					// counts applied changes
	int m_autosavedSerial = 0;
	int m_buildSide = 0;
	int m_buildEntry = -1;
	QString m_buildTemplate;
	BuildDrag m_buildDrag;
	QPointer<class ScriptsWindow> m_scriptsWindow;
	QPointer<class ParticlesWindow> m_particlesWindow;
	std::optional<MapEditorCore::WaterLook> m_waterLook;	// the water being edited, in the view
	bool m_lightingOnly = false;			// the settings step being applied changes only the lighting
	QString m_prefabFolder;					// the self test's own folder, empty for the user's
	QPointer<QDialog> m_problemsWindow;
	QPointer<class TextListModel> m_problemsModel;
	QPointer<QPushButton> m_problemsButton;	// status bar: how many problems the map has
	QPointer<QTimer> m_problemsTimer;
	std::vector<MapEditorCore::Problem> m_problems;
	bool m_scriptsEditing = false;
	ScriptCatalog m_scriptCatalog;
	// Editor sorting per template, from the catalog it was built from (see pickable).
	mutable std::map<std::string, int> m_templateSortings;
	mutable const void *m_sortingsCatalog = nullptr;
	int m_scorchType = 0;
	float m_scorchRadius = 20.0f;
	OverlayOptions m_overlay;
	std::map<int, float> m_selectionRadii;
	MapEditorCore::Clipboard m_clipboard;
	bool m_hoverValid = false;
	float m_hoverX = 0.0f, m_hoverY = 0.0f;
	// Start positions of the selection while it is dragged.
	struct DragStart
	{
		int object;
		float x, y;
	};
	std::vector<DragStart> m_dragStart;
	// State of the selection while it is turned.
	struct TurnStart
	{
		int object;
		float x, y, angle;
	};
	std::vector<TurnStart> m_turnStart;
	float m_turnPivotX = 0.0f, m_turnPivotY = 0.0f;
	float m_turnReference = 0.0f;
	bool m_turnHasReference = false;
	QPointer<QWidget> m_selectionBox;
	QPointer<class MapOverview> m_overview;
	std::array<QPointer<QDoubleSpinBox>, 4> m_selectionFields;
	int m_mapSerial = 0;
	int m_viewMapSerial = -1;
	QString m_last3DError;
	QString m_engineLog;

	// Self tests of the parts, called from selfTestEdit; they live in App/SelfTests/ (one file per
	// part of the window) and add one report line per step.
	bool selfTestBuildList(const QString &shotDir, QStringList &report);
	bool selfTestCamera(const QString &shotDir, QStringList &report);
	bool selfTestCompare(const QString &shotDir, QStringList &report);
	bool selfTestDisplay(const QString &shotDir, QStringList &report);
	bool selfTestFairness(QStringList &report);
	bool selfTestGraphics(const QString &shotDir, QStringList &report);
	bool selfTestHelp(QStringList &report);
	bool selfTestHistory(QStringList &report);
	bool selfTestLayers(QStringList &report);
	bool selfTestLighting(const QString &shotDir, QStringList &report);
	bool selfTestMapSettings(QStringList &report);
	bool selfTestMapSize(QStringList &report);
	bool selfTestParticles(const QString &shotDir, QStringList &report);
	bool selfTestPlacement(const QString &shotDir, QStringList &report);
	bool selfTestPoster(const QString &shotDir, QStringList &report);
	bool selfTestPrefabs(const QString &shotDir, QStringList &report);
	bool selfTestProblems(QStringList &report);
	bool selfTestScripts(const QString &shotDir, QStringList &report);
	bool selfTestSearch(QStringList &report);
	bool selfTestSelection(QStringList &report);
	bool selfTestSession(const QString &shotDir, QStringList &report);
	bool selfTestShortcuts(QStringList &report);
	bool selfTestSides(const QString &shotDir, QStringList &report);
	bool selfTestSlopes(const QString &shotDir, QStringList &report);
	bool selfTestSound(int object, QStringList &report);
	bool selfTestSymmetry(QStringList &report);
	bool selfTestTools(const QString &shotDir, QStringList &report);
	bool selfTestWater(const QString &shotDir, QStringList &report);
};

} // namespace MapEditorApp
