/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowEdit.cpp -- editing in the main window: selection (click, Ctrl+click, rectangle, all),
// dragging the selection, undo and redo, the selection panel on the right, and the self test of
// all that.

#include "App/Common.h"
#include "App/MainWindow.h"
#include "App/Windows/SearchWindow.h"

#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/MapOverview.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include "Core/RoadCommands.h"
#include "Core/LayerCommands.h"
#include "Core/SelectionTools.h"
#include "Core/WaypointCommands.h"

#include <QClipboard>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGuiApplication>
#include <QMimeData>
#include <QScrollArea>
#include <QThread>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>

namespace MapEditorApp
{

namespace
{

bool isStartPosition(const std::string &name)
{
	return name.rfind("Player_", 0) == 0 && name.find("_Start") != std::string::npos;
}

QString displayName(const MapModel::MapObject &o)
{
	if (const auto waypoint = o.properties.getAscii("waypointName"))
		return utf8(*waypoint);
	return utf8(o.templateName);
}

QString kindName(const MapModel::MapObject &o)
{
	if (const auto waypoint = o.properties.getAscii("waypointName"))
		return isStartPosition(*waypoint) ? T("Start position") : T("Waypoint");
	return T("Object");
}

MapModel::MapObject *objectAt(OpenMap &open, int index)
{
	auto &objects = open.map().objects;
	if (!objects || index < 0 || static_cast<std::size_t>(index) >= objects->size())
		return nullptr;
	return &(*objects)[static_cast<std::size_t>(index)];
}

} // namespace

// ------------------------------------------------------------------------------------------------
// Selection

void MainWindow::clickObject(int object, bool toggle, bool released)
{
	if (!m_state.map)
		return;
	// An object the pick filter leaves out is not hit.
	if (object >= 0 && !pickable(object))
		object = -1;
	MapEditorCore::Selection &sel = m_state.map->selection;
	if (released)
	{
		// A plain click on one object of several selected ones selects only that one; on press the
		// selection stayed so the whole of it could be dragged.
		// A member of a group stands for the whole group.
		if (!toggle && object >= 0)
		{
			const std::vector<std::int32_t> group = MapEditorCore::withGroups(m_state.map->map(), {object});
			if (sel.objects.size() > group.size())
			{
				sel.objects = group;
				selectionChanged();
			}
		}
		return;
	}
	if (object < 0)
	{
		if (!toggle && !sel.empty())
		{
			sel.clear();
			selectionChanged();
		}
		return;
	}
	const std::vector<std::int32_t> group = MapEditorCore::withGroups(m_state.map->map(), {object});
	if (toggle)
	{
		const bool remove = sel.contains(object);
		for (std::int32_t member : group)
		{
			if (remove)
				sel.objects.erase(std::remove(sel.objects.begin(), sel.objects.end(), member), sel.objects.end());
			else
				sel.add(member);
		}
	}
	else if (!sel.contains(object))
		sel.objects = group;
	else
		return;
	selectionChanged();
}

void MainWindow::selectRect(const std::vector<int> &objects, bool add)
{
	if (!m_state.map)
		return;
	MapEditorCore::Selection &sel = m_state.map->selection;
	if (!add)
		sel.clear();
	std::vector<std::int32_t> hits;
	for (int object : objects)
	{
		if (pickable(object))
			hits.push_back(object);
	}
	for (std::int32_t object : MapEditorCore::withGroups(m_state.map->map(), hits))
		sel.add(object);
	selectionChanged();
}

namespace
{

QString pickFilterName(int sorting)
{
	switch (sorting)
	{
	case 1: return T("Buildings");
	case 2: return T("Infantry");
	case 3: return T("Vehicles");
	case 4: return T("Shrubbery");
	case 5: return T("Props");
	case 6: return T("Natural");
	case 7: return T("Debris");
	case 9: return T("Sounds");
	case 12: return T("Roads");
	case 13: return T("Waypoints and areas");
	default: return T("Anything");
	}
}

} // namespace

void MainWindow::setPickFilter(int sorting)
{
	m_pickFilter = sorting;
	toast(sorting == MapEditorCore::PICK_ANYTHING ? T("Picks anything") : T("Picks only: %1").arg(pickFilterName(sorting)));
	updateSelectionStatus();
}

bool MainWindow::pickable(int object) const
{
	if (!m_state.map || !m_state.map->map().objects || object < 0 || static_cast<std::size_t>(object) >= m_state.map->map().objects->size())
		return true;
	if (!m_hiddenLayers.empty() && m_hiddenLayers.count(MapEditorCore::objectLayer((*m_state.map->map().objects)[static_cast<std::size_t>(object)])))
		return false;
	if (m_pickFilter == MapEditorCore::PICK_ANYTHING)
		return true;
	// The lookup is built once per catalog: a rectangle may hold thousands of objects.
	if (EngineClient::instance().isStarted())
	{
		const std::vector<MeTemplateInfo> &catalog = EngineClient::instance().catalog();
		if (m_sortingsCatalog != catalog.data())
		{
			m_templateSortings.clear();
			for (const MeTemplateInfo &info : catalog)
				m_templateSortings[info.name] = info.sorting;
			m_sortingsCatalog = catalog.data();
		}
	}
	const MapEditorCore::SortingOf sortingOf = [this](const std::string &name) {
		const auto found = m_templateSortings.find(name);
		return found != m_templateSortings.end() ? found->second : 0;
	};
	const MapModel::MapObject &o = (*m_state.map->map().objects)[static_cast<std::size_t>(object)];
	return MapEditorCore::pickAllowed(MapEditorCore::objectSorting(o, sortingOf), m_pickFilter);
}

void MainWindow::selectDuplicates()
{
	if (!m_state.map)
		return;
	const std::vector<std::int32_t> found = MapEditorCore::findDuplicates(m_state.map->map());
	m_state.map->selection.objects = found;
	selectionChanged();
	toast(found.empty() ? T("No duplicate objects.") : T("%1 duplicate objects selected (Delete removes them)").arg(found.size()));
}

void MainWindow::selectSimilar()
{
	if (!m_state.map)
		return;
	const std::vector<std::int32_t> found = MapEditorCore::findSimilar(m_state.map->map(), m_state.map->selection.objects);
	if (found.empty())
	{
		toast(T("Select an object first; its like are selected with it."));
		return;
	}
	m_state.map->selection.objects = found;
	selectionChanged();
	toast(T("%1 objects of this template selected").arg(found.size()));
}

void MainWindow::selectInvalidTeams()
{
	if (!m_state.map)
		return;
	std::set<std::string> teams;
	const std::vector<std::int32_t> found = MapEditorCore::findInvalidTeams(m_state.map->map(), teams);
	m_state.map->selection.objects = found;
	selectionChanged();
	if (found.empty())
	{
		toast(T("Every object has a team of the map."));
		return;
	}
	QStringList names;
	for (const std::string &team : teams)
		names << (team.empty() ? T("(none)") : QString::fromStdString(team));
	toast(T("%1 objects with teams the map lacks: %2").arg(found.size()).arg(names.join(QStringLiteral(", "))));
}

void MainWindow::selectAll()
{
	if (!m_state.map || !m_state.map->map().objects)
		return;
	MapEditorCore::Selection &sel = m_state.map->selection;
	sel.clear();
	// Everything but road and bridge points (the terrain draws those; flags 0x36).
	const auto &objects = *m_state.map->map().objects;
	for (std::size_t i = 0; i < objects.size(); ++i)
	{
		if ((objects[i].flags & 0x36) == 0)
			sel.add(static_cast<std::int32_t>(i));
	}
	selectionChanged();
}

void MainWindow::selectNone()
{
	if (!m_state.map || m_state.map->selection.empty())
		return;
	m_state.map->selection.clear();
	selectionChanged();
}

void MainWindow::selectionChanged()
{
	m_selectionRadii.clear();
	if (m_state.map && m_viewport->hasDevice())
	{
		for (std::int32_t object : m_state.map->selection.objects)
		{
			MePickResult info{};
			if (EngineClient::instance().objectInfo(object, &info))
				m_selectionRadii[object] = info.radius;
		}
	}
	applyOverlay();
	updateSelectionStatus();
	fillSelectionPanel();
}

// ------------------------------------------------------------------------------------------------
// Moving

void MainWindow::dragSelection(float dx, float dy)
{
	if (!m_state.map)
		return;
	if (m_dragStart.empty())
	{
		for (std::int32_t object : m_state.map->selection.objects)
		{
			if (const MapModel::MapObject *o = objectAt(*m_state.map, object))
				m_dragStart.push_back(DragStart{object, o->x, o->y});
		}
	}
	// With the grid, the move goes in whole cells.
	dx = snapCoord(dx);
	dy = snapCoord(dy);
	for (const DragStart &start : m_dragStart)
	{
		MapModel::MapObject *o = objectAt(*m_state.map, start.object);
		if (!o)
			continue;
		o->x = start.x + dx;
		o->y = start.y + dy;
		EngineClient::instance().moveObject(start.object, o->x, o->y, o->z, o->angle);
	}
	applyOverlay();
	updateSelectionStatus();
	refreshSelectionValues();
}

void MainWindow::endDragSelection(bool cancel)
{
	if (!m_state.map)
		return;
	std::vector<MapEditorCore::MoveObjects::Move> moves;
	for (const DragStart &start : m_dragStart)
	{
		MapModel::MapObject *o = objectAt(*m_state.map, start.object);
		if (!o)
			continue;
		if (cancel)
		{
			o->x = start.x;
			o->y = start.y;
			EngineClient::instance().moveObject(start.object, o->x, o->y, o->z, o->angle);
		}
		else if (o->x != start.x || o->y != start.y)
			moves.push_back({start.object, start.x, start.y, o->x, o->y});
	}
	m_dragStart.clear();
	// The drag already changed the model; the command only records it.
	if (!moves.empty())
		m_state.map->history.record(std::make_unique<MapEditorCore::MoveObjects>(std::move(moves)));
	applyOverlay();
	updateSelectionStatus();
	refreshSelectionValues();
	updateTitle();
}

void MainWindow::turnSelection(float delta)
{
	if (!m_state.map)
		return;
	if (m_turnStart.empty())
	{
		float sumX = 0.0f, sumY = 0.0f;
		for (std::int32_t object : m_state.map->selection.objects)
		{
			if (const MapModel::MapObject *o = objectAt(*m_state.map, object))
			{
				m_turnStart.push_back(TurnStart{object, o->x, o->y, o->angle});
				sumX += o->x;
				sumY += o->y;
			}
		}
		if (m_turnStart.empty())
			return;
		m_turnPivotX = sumX / static_cast<float>(m_turnStart.size());
		m_turnPivotY = sumY / static_cast<float>(m_turnStart.size());
	}
	const float c = std::cos(delta);
	const float s = std::sin(delta);
	for (const TurnStart &start : m_turnStart)
	{
		MapModel::MapObject *o = objectAt(*m_state.map, start.object);
		if (!o)
			continue;
		// Positions turn around the centre (a single object stays where it is), angles turn along.
		const float rx = start.x - m_turnPivotX;
		const float ry = start.y - m_turnPivotY;
		o->x = m_turnPivotX + rx * c - ry * s;
		o->y = m_turnPivotY + rx * s + ry * c;
		o->angle = static_cast<float>(std::remainder(start.angle + delta, 6.283185307179586));
		EngineClient::instance().moveObject(start.object, o->x, o->y, o->z, o->angle);
	}
	applyOverlay();
	updateSelectionStatus();
	refreshSelectionValues();
}

void MainWindow::rotateSelection(float startX, float startY, float x, float y, bool snap)
{
	if (!m_state.map || m_state.map->selection.empty())
		return;
	if (m_turnStart.empty())
	{
		turnSelection(0.0f);
		m_turnHasReference = false;
	}
	// The reference direction: pivot to the grabbed point, or, if the object was grabbed at its
	// centre, pivot to the first point the cursor reaches a little away from it.
	if (!m_turnHasReference)
	{
		float rx = startX - m_turnPivotX;
		float ry = startY - m_turnPivotY;
		if (rx * rx + ry * ry < 4.0f)
		{
			rx = x - m_turnPivotX;
			ry = y - m_turnPivotY;
			if (rx * rx + ry * ry < 4.0f)
				return;
		}
		m_turnReference = std::atan2(ry, rx);
		m_turnHasReference = true;
	}
	float delta = static_cast<float>(std::remainder(std::atan2(y - m_turnPivotY, x - m_turnPivotX) - m_turnReference, 6.283185307179586));
	if (snap)
	{
		const float step = static_cast<float>(15.0 / RAD_TO_DEG);
		delta = std::round(delta / step) * step;
	}
	turnSelection(delta);
}

void MainWindow::endRotateSelection(bool cancel)
{
	if (!m_state.map)
		return;
	std::vector<MapEditorCore::EditObjects::Edit> edits;
	for (const TurnStart &start : m_turnStart)
	{
		MapModel::MapObject *o = objectAt(*m_state.map, start.object);
		if (!o)
			continue;
		MapModel::MapObject before = *o;
		before.x = start.x;
		before.y = start.y;
		before.angle = start.angle;
		if (cancel)
		{
			*o = before;
			EngineClient::instance().moveObject(start.object, o->x, o->y, o->z, o->angle);
		}
		else if (o->angle != start.angle || o->x != start.x || o->y != start.y)
			edits.push_back({start.object, before, *o});
	}
	m_turnStart.clear();
	m_turnHasReference = false;
	// The drag already changed the model; the command only records it.
	if (!edits.empty())
	{
		const std::string name = edits.size() == 1 ? "Rotate object" : "Rotate objects";
		m_state.map->history.record(std::make_unique<MapEditorCore::EditObjects>(name, std::move(edits)));
	}
	applyOverlay();
	updateSelectionStatus();
	refreshSelectionValues();
	updateTitle();
}

void MainWindow::editSelected(int field, double value)
{
	if (!m_state.map || m_state.map->selection.objects.size() != 1)
		return;
	const int index = m_state.map->selection.objects[0];
	MapModel::MapObject *o = objectAt(*m_state.map, index);
	if (!o)
		return;
	MapModel::MapObject after = *o;
	const float v = static_cast<float>(value);
	std::string name = "Move object";
	switch (field)
	{
	case 0: after.x = v; break;
	case 1: after.y = v; break;
	case 2: after.z = v; name = "Change height"; break;
	default: after.angle = static_cast<float>(value / RAD_TO_DEG); name = "Rotate object"; break;
	}
	// The fields show rounded values; only a real change becomes a step.
	const bool same = field == 3 ? std::abs(after.angle - o->angle) < 1.0e-4f
		: std::abs((field == 0 ? o->x : field == 1 ? o->y : o->z) - v) < 0.05f;
	if (same)
		return;
	m_state.map->history.execute(std::make_unique<MapEditorCore::EditObjects>(name,
		std::vector<MapEditorCore::EditObjects::Edit>{{index, *o, after}}), m_state.map->map());
	applyChanges(MapEditorCore::Changes{{index}});
}

void MainWindow::deleteSelection()
{
	if (deleteBoundarySelection() || deleteRoadSelection() || deleteAreaSelection())
		return;
	if (!m_state.map || m_state.map->selection.empty() || !m_dragStart.empty() || !m_turnStart.empty())
		return;
	auto command = std::make_unique<MapEditorCore::DeleteObjects>(m_state.map->map(), m_state.map->selection.objects);
	if (command->count() == 0)
		return;
	const MapEditorCore::Changes changes = command->changes();
	m_state.map->history.execute(std::move(command), m_state.map->map());
	MapEditorCore::Changes after = changes;
	after.objects.clear();	// the objects are gone
	applyChanges(after);
}

void MainWindow::copySelection()
{
	if (!m_state.map || m_state.map->selection.empty())
		return;
	m_clipboard = MapEditorCore::copyObjects(m_state.map->map(), m_state.map->selection.objects);
	if (toSystemClipboard(m_clipboard))
		toast(T("%1 objects copied").arg(m_clipboard.objects.size()));
	else
		toast(T("%1 objects copied; the system clipboard is busy, so they can only be pasted in this window").arg(m_clipboard.objects.size()));
}

namespace
{

// Windows refuses to set or read the clipboard while another program holds it open, as clipboard
// histories and remote desktop clients do for a moment after every change. A few short waits
// ride that out.
constexpr int CLIPBOARD_TRIES = 6;

void waitForClipboard()
{
	QThread::msleep(50);
	QCoreApplication::processEvents();
}

bool holdsFormat(const QMimeData *mime, const char *format)
{
	return mime && mime->hasFormat(QString::fromLatin1(format));
}

} // namespace

bool MainWindow::toSystemClipboard(const MapEditorCore::Clipboard &clipboard)
{
	const std::vector<std::uint8_t> bytes = MapEditorCore::clipboardToBytes(clipboard);
	if (bytes.empty())
		return false;
	QStringList names;
	for (const MapModel::MapObject &o : clipboard.objects)
		names << utf8(o.templateName);
	QClipboard *system = QGuiApplication::clipboard();
	for (int attempt = 0; attempt < CLIPBOARD_TRIES; ++attempt)
	{
		if (attempt > 0)
			waitForClipboard();
		// The clipboard takes ownership of the data, also when setting it fails.
		auto *mime = new QMimeData;
		mime->setData(QString::fromLatin1(CLIPBOARD_FORMAT), QByteArray(reinterpret_cast<const char *>(bytes.data()), static_cast<qsizetype>(bytes.size())));
		mime->setText(names.join(QLatin1Char('\n')));
		system->setMimeData(mime);
		// Platforms without clipboard ownership report false; there reading back decides.
		if (system->ownsClipboard() || holdsFormat(system->mimeData(), CLIPBOARD_FORMAT))
			return true;
	}
	return false;
}

std::optional<MapEditorCore::Clipboard> MainWindow::fromSystemClipboard() const
{
	QClipboard *system = QGuiApplication::clipboard();
	for (int attempt = 0; attempt < CLIPBOARD_TRIES; ++attempt)
	{
		if (attempt > 0)
			waitForClipboard();
		const QMimeData *mime = system->mimeData();
		if (holdsFormat(mime, CLIPBOARD_FORMAT))
		{
			const QByteArray bytes = mime->data(QString::fromLatin1(CLIPBOARD_FORMAT));
			return MapEditorCore::clipboardFromBytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(bytes.constData()),
				static_cast<std::size_t>(bytes.size())));
		}
		// Waiting helps only while the clipboard still holds this editor's copy but could not be read.
		if (!system->ownsClipboard())
			return std::nullopt;
	}
	return std::nullopt;
}

void MainWindow::cutSelection()
{
	if (!m_state.map || m_state.map->selection.empty() || !m_dragStart.empty() || !m_turnStart.empty())
		return;
	m_clipboard = MapEditorCore::copyObjects(m_state.map->map(), m_state.map->selection.objects);
	if (!toSystemClipboard(m_clipboard))
		toast(T("The system clipboard is busy; the cut objects can only be pasted in this window"));
	auto command = std::make_unique<MapEditorCore::DeleteObjects>(m_state.map->map(), m_state.map->selection.objects, "Cut");
	if (command->count() == 0)
		return;
	MapEditorCore::Changes changes = command->changes();
	m_state.map->history.execute(std::move(command), m_state.map->map());
	changes.objects.clear();	// the objects are gone
	applyChanges(changes);
}

void MainWindow::paste()
{
	// The system clipboard first (copies from another editor window or an earlier session).
	if (const std::optional<MapEditorCore::Clipboard> shared = fromSystemClipboard())
	{
		insertCopies(*shared, "Paste");
		return;
	}
	if (m_clipboard.empty())
		return;
	insertCopies(m_clipboard, "Paste");
}

void MainWindow::duplicateSelection()
{
	if (!m_state.map || m_state.map->selection.empty())
		return;
	const bool hover = m_hoverValid;
	m_hoverValid = false;	// a duplicate lies next to its original, not at the cursor
	insertCopies(MapEditorCore::copyObjects(m_state.map->map(), m_state.map->selection.objects), "Duplicate");
	m_hoverValid = hover;
}

void MainWindow::insertCopies(const MapEditorCore::Clipboard &clipboard, const std::string &name)
{
	if (!m_state.map || clipboard.empty() || !m_dragStart.empty() || !m_turnStart.empty())
		return;
	float dx = 30.0f;
	float dy = 30.0f;
	if (m_hoverValid && m_page == Page::Map)
	{
		float cx = 0.0f, cy = 0.0f;
		for (const MapModel::MapObject &o : clipboard.objects)
		{
			cx += o.x;
			cy += o.y;
		}
		const float n = static_cast<float>(clipboard.objects.size());
		dx = m_hoverX - cx / n;
		dy = m_hoverY - cy / n;
	}
	auto command = MapEditorCore::pasteObjects(m_state.map->map(), clipboard, dx, dy, name);
	MapEditorCore::InsertObjects *inserted = command.get();
	m_state.map->history.execute(std::move(command), m_state.map->map());
	applyChanges(inserted->changes());
}

// ------------------------------------------------------------------------------------------------
// Undo and redo

void MainWindow::undo()
{
	// Undo while a mold is shown only takes the mold away.
	if (m_mold.active)
	{
		finishMold(false);
		return;
	}
	if (m_state.map && m_state.map->history.canUndo() && m_dragStart.empty() && m_turnStart.empty())
		applyChanges(m_state.map->history.undo(m_state.map->map()));
}

void MainWindow::redo()
{
	finishMold(false);
	if (m_state.map && m_state.map->history.canRedo() && m_dragStart.empty() && m_turnStart.empty())
		applyChanges(m_state.map->history.redo(m_state.map->map()));
}

void MainWindow::applyChanges(const MapEditorCore::Changes &changes)
{
	++m_editSerial;
	refreshHistoryWindow();
	if (m_searchWindow)
		m_searchWindow->mapChanged();
	scheduleProblemCheck();
	if (changes.size)
	{
		mapResized();
		return;
	}
	refreshLayers();
	if (changes.sides)
		sidesChanged();
	if (changes.scripts)
	{
		scriptsChanged();
		// Scripts alone do not show in the views.
		if (!changes.reindexed && !changes.areas && !changes.links)
			return;
	}
	if (changes.settings || changes.boundaries)
	{
		settingsChanged(changes);
		return;
	}
	if (changes.terrain || changes.textures || changes.passability)
	{
		terrainChanged(changes);
		return;
	}
	if (changes.areas)
	{
		// Water areas are drawn by the engine from its copy of the map.
		if (m_viewport->hasDevice() && m_viewMapSerial == m_mapSerial)
		{
			const MapModel::WriteResult bytes = engineMapBytes();
			if (!bytes.ok() || !EngineClient::instance().reloadObjects(bytes.bytes))
				m_viewMapSerial = -1;
		}
		areasChanged();
		fillSelectionPanel();
		updateSelectionStatus();
		updateTitle();
		return;
	}
	if (changes.links)
	{
		// Links and path labels: only the overlay and the panel show them.
		applyOverlay();
		fillSelectionPanel();
		updateSelectionStatus();
		updateTitle();
		return;
	}
	if (changes.rebuild && !changes.reindexed)
	{
		// Same objects in the same places, drawn differently (team colour, damage state, model).
		if (m_viewport->hasDevice() && m_viewMapSerial == m_mapSerial)
		{
			const MapModel::WriteResult bytes = engineMapBytes();
			if (!bytes.ok() || !EngineClient::instance().reloadObjects(bytes.bytes))
				m_viewMapSerial = -1;
		}
		selectionChanged();
		updateTitle();
		return;
	}
	if (changes.reindexed)
	{
		// Objects were removed or put back: every index behind them moved. The view rebuilds its
		// objects from the model (if it shows this map), and the selection becomes what came back.
		if (m_viewport->hasDevice() && m_viewMapSerial == m_mapSerial)
		{
			const MapModel::WriteResult bytes = engineMapBytes();
			if (!bytes.ok() || !EngineClient::instance().reloadObjects(bytes.bytes))
				m_viewMapSerial = -1;	// load the whole map again next time
		}
		// Road and bridge points are never selected objects; the road tool handles them.
		m_state.map->selection.objects.clear();
		for (std::int32_t index : changes.objects)
		{
			const MapModel::MapObject *o = objectAt(*m_state.map, index);
			if (o && !MapEditorCore::isRoadOrBridgePoint(*o))
				m_state.map->selection.objects.push_back(index);
		}
		selectionChanged();
		updateTitle();
		return;
	}
	for (std::int32_t index : changes.objects)
	{
		if (const MapModel::MapObject *o = objectAt(*m_state.map, index))
			EngineClient::instance().moveObject(index, o->x, o->y, o->z, o->angle);
	}
	applyOverlay();
	updateSelectionStatus();
	refreshSelectionValues();
	updateTitle();
}

// ------------------------------------------------------------------------------------------------
// Status line and side panel

void MainWindow::updateSelectionStatus()
{
	if (!m_statusRight)
		return;
	if (!m_state.map || m_page != Page::Map)
	{
		m_statusRight->setText(QString());
		return;
	}
	if (terrainTool() && m_terrainTool == TerrainTool::Texture)
	{
		if (m_textureMode == TextureMode::Fill)
			m_statusRight->setText(T("Textures: click fills a region with %1, Shift+click replaces its texture everywhere").arg(m_paintTexture));
		else if (m_textureMode == TextureMode::Edge)
			m_statusRight->setText(T("Textures: drag from a cell onto its neighbour to blend the edge, with Shift the chosen texture"));
		else if (m_textureMode == TextureMode::AutoEdge)
			m_statusRight->setText(T("Textures: click a region to blend its edges into the textures around it"));
		else if (m_textureMode == TextureMode::Cliffs)
			m_statusRight->setText(T("Textures: click a cliff to lay its texture along the slope"));
		else
			m_statusRight->setText(m_paintTexture.isEmpty() ? T("Textures: choose a texture on the left, then drag to paint it")
				: T("Textures: drag paints %1").arg(m_paintTexture));
		return;
	}
	if (placementTool() && (!scorchTool() || m_state.map->selection.empty()))
	{
		m_statusRight->setText(rulerTool() ? (m_ruler.valid ? rulerText() : T("Ruler: drag to measure"))
			: borderTool() ? T("Playable areas: drag for a new one, drag a corner or edge to resize")
			: groveTool() ? T("Grove: a click grows a grove, a drag scatters trees in a rectangle")
			: fenceTool() ? T("Fence: drag from one end to the other") : T("Scorch marks: a click sets one or picks the one there"));
		return;
	}
	if (terrainTool() && m_terrainTool == TerrainTool::Pathing)
	{
		m_statusRight->setText(m_markImpassable ? T("Passability: drag marks cells impassable") : T("Passability: drag marks cells passable"));
		return;
	}
	if (terrainTool() && m_terrainTool == TerrainTool::Shape)
	{
		m_statusRight->setText(m_shapeTool == ShapeTool::Ramp ? T("Ramp: drag from one end to the other")
			: T("Mesh mold: click to place, drag to move, Enter applies, Esc discards"));
		return;
	}
	if (terrainTool())
	{
		m_statusRight->setText(T("Terrain: drag paints with the height brush, Alt+click takes the height under the cursor"));
		return;
	}
	if (bridgeTool())
	{
		m_statusRight->setText(T("Bridges: drag from bank to bank; a click selects a bridge, Del deletes it"));
		return;
	}
	if (roadTool())
	{
		m_statusRight->setText(T("Roads: drag draws a segment (ends snap to roads, a start on a road branches off it); a click selects, Shift+drag moves a road end, Del deletes"));
		return;
	}
	if (areaTool())
	{
		m_statusRight->setText(T("Areas: clicks set points, a click on the first point or Enter closes; drag a point, click an edge to insert one, drag inside to move; Del removes; Ctrl+click starts a new area"));
		return;
	}
	if (m_placeTemplate == QLatin1String(MapEditorCore::WAYPOINT_TEMPLATE))
	{
		m_statusRight->setText(T("Waypoints: a click places one linked to the selected waypoint, Ctrl+click starts a new path, a click on a waypoint links or unlinks it, Esc ends"));
		return;
	}
	if (!m_placeTemplate.isEmpty())
	{
		m_statusRight->setText(T("Placing %1: click places, drag sets the direction, Esc ends").arg(m_placeTemplate));
		return;
	}
	const MapEditorCore::Selection &sel = m_state.map->selection;
	if (sel.empty())
	{
		m_statusRight->setText(T("Click selects, Ctrl+click adds, drag moves, Shift+drag turns, drag on the ground selects a rectangle"));
		return;
	}
	if (sel.objects.size() > 1)
	{
		m_statusRight->setText(T("Selected: %1 objects").arg(sel.objects.size()));
		return;
	}
	const MapModel::MapObject *o = objectAt(*m_state.map, sel.objects[0]);
	if (!o)
		return;
	m_statusRight->setText(T("Selected: %1   X %2   Y %3").arg(displayName(*o)).arg(static_cast<double>(o->x), 0, 'f', 0).arg(static_cast<double>(o->y), 0, 'f', 0));
}

void MainWindow::fillSelectionPanel()
{
	if (!m_selectionBox)
		return;
	// The panel is refilled from the signals of its own fields, so the old ones go later.
	for (QWidget *child : m_selectionBox->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly))
	{
		child->hide();
		child->setParent(nullptr);
		child->deleteLater();
	}
	delete m_selectionBox->layout();
	m_selectionFields = {};
	auto *box = new QVBoxLayout(m_selectionBox);
	box->setContentsMargins(0, 0, 0, 10);
	box->setSpacing(6);
	const MapEditorCore::Selection *sel = m_state.map ? &m_state.map->selection : nullptr;
	// The box sits in a scroll area (viewport, then the area itself).
	QWidget *area = m_selectionBox->parentWidget() ? m_selectionBox->parentWidget()->parentWidget() : nullptr;
	QWidget *shown = qobject_cast<QScrollArea *>(area) ? area : m_selectionBox.data();
	if (roadTool() && m_state.map)
	{
		shown->show();
		m_selectionBox->show();
		addRoadEditors(box);
		return;
	}
	// The grove, fence and scorch tools show their settings; a picked scorch mark shows its values.
	if (placementTool() && m_state.map && (!scorchTool() || !sel || sel->empty()))
	{
		shown->show();
		m_selectionBox->show();
		addPlacementEditors(box);
		return;
	}
	// With an area tool the panel belongs to the selected area.
	if (areaTool() && m_area >= 0 && m_state.map && m_state.map->map().polygons
		&& static_cast<std::size_t>(m_area) < m_state.map->map().polygons->size())
	{
		shown->show();
		m_selectionBox->show();
		addAreaEditors(box);
		return;
	}
	if (!sel || sel->empty())
	{
		shown->hide();
		return;
	}
	shown->show();
	m_selectionBox->show();
	box->addWidget(label(T("SELECTION"), "Section"));

	if (sel->objects.size() > 1)
	{
		box->addWidget(label(T("%1 objects").arg(sel->objects.size())));
		// Grouped by name, most frequent first.
		std::map<QString, int> counts;
		for (std::int32_t index : sel->objects)
		{
			if (const MapModel::MapObject *o = objectAt(*m_state.map, index))
				++counts[displayName(*o)];
		}
		std::vector<std::pair<QString, int>> sorted(counts.begin(), counts.end());
		std::stable_sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
		auto *list = new QTreeWidget;
		list->setHeaderHidden(true);
		list->setRootIsDecorated(false);
		list->setColumnCount(2);
		for (const auto &[name, count] : sorted)
			new QTreeWidgetItem(list, {name, QStringLiteral("\u00d7 %1").arg(count)});
		list->resizeColumnToContents(1);
		list->setMaximumHeight(200);
		box->addWidget(list);
		addPropertyEditors(box);
		return;
	}

	const int index = sel->objects[0];
	const MapModel::MapObject *o = objectAt(*m_state.map, index);
	if (!o)
		return;
	auto *name = label(displayName(*o));
	name->setStyleSheet(QStringLiteral("font-weight: 600;"));
	name->setWordWrap(true);
	box->addWidget(name);
	QString meta = kindName(*o);
	if (const auto owner = o->properties.getAscii("originalOwner"); owner && !owner->empty())
		meta += QStringLiteral(" \u00b7 ") + T("Team %1").arg(utf8(*owner));
	box->addWidget(label(meta, "Meta"));

	auto *grid = new QGridLayout;
	grid->setHorizontalSpacing(10);
	grid->setVerticalSpacing(6);
	const QString names[4] = {QStringLiteral("X"), QStringLiteral("Y"), T("Height"), T("Angle")};
	for (int field = 0; field < 4; ++field)
	{
		auto *spin = new QDoubleSpinBox;
		spin->setKeyboardTracking(false);
		spin->setDecimals(1);
		if (field == 3)
		{
			spin->setRange(0.0, 360.0);
			spin->setWrapping(true);
			spin->setSuffix(QStringLiteral("\u00b0"));
		}
		else
			spin->setRange(-100000.0, 100000.0);
		connect(spin, &QDoubleSpinBox::editingFinished, this, [this, field, spin] { editSelected(field, spin->value()); });
		m_selectionFields[static_cast<std::size_t>(field)] = spin;
		grid->addWidget(label(names[field], "Meta"), field, 0);
		grid->addWidget(spin, field, 1);
	}
	box->addLayout(grid);
	refreshSelectionValues();
	addPropertyEditors(box);
}

void MainWindow::refreshSelectionValues()
{
	if (!m_state.map || m_state.map->selection.objects.size() != 1 || !m_selectionFields[0])
		return;
	const MapModel::MapObject *o = objectAt(*m_state.map, m_state.map->selection.objects[0]);
	if (!o)
		return;
	double angle = std::fmod(static_cast<double>(o->angle) * RAD_TO_DEG, 360.0);
	if (angle < 0.0)
		angle += 360.0;
	const double values[4] = {o->x, o->y, o->z, angle};
	for (std::size_t i = 0; i < 4; ++i)
	{
		QDoubleSpinBox *spin = m_selectionFields[i];
		if (!spin || spin->hasFocus())
			continue;
		const QSignalBlocker block(spin);
		spin->setValue(values[i]);
	}
}

} // namespace MapEditorApp
