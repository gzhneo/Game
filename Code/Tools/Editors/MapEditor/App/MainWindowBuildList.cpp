/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowBuildList.cpp -- the build list tool (the old WorldBuilder's BuildListTool with its panel
// BuildList and the building dialog BaseBuildProps): the structures a player's AI builds, in order.
// A click places the chosen structure for the player (a drag sets its direction), dragging an entry
// moves it, dragging the ring around the selected one turns it. The panel lists the entries and
// edits the selected one; the power bar shows what the player's objects and the list up to the
// entry produce and use. The 3D view draws every player's entries in the player's colour.

#include "App/Common.h"
#include "App/MainWindow.h"

#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/MapOverview.h"
#include "App/ToolEvent.h"
#include "App/Widgets.h"
#include "Core/BuildListCommands.h"
#include "Core/SidesCommands.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace MapEditorApp
{

namespace
{

// The ring around a selected entry that turns it (BuildListTool: half a cell to one and a half).
constexpr float TURN_INNER = 0.5f * CELL;
constexpr float TURN_OUTER = 1.5f * CELL;
constexpr float HALF_SIZE = 1.5f * CELL;

} // namespace

bool MainWindow::buildListTool() const
{
	return m_placeTemplate == QLatin1String(BUILD_LIST_TOOL);
}

const MapModel::BuildListEntry *MainWindow::buildEntry(int side, int index) const
{
	if (!m_state.map || !m_state.map->map().sides)
		return nullptr;
	const MapModel::Sides &sides = *m_state.map->map().sides;
	if (side < 0 || side >= static_cast<int>(sides.sides.size()))
		return nullptr;
	const std::vector<MapModel::BuildListEntry> &list = sides.sides[static_cast<std::size_t>(side)].buildList;
	return index >= 0 && index < static_cast<int>(list.size()) ? &list[static_cast<std::size_t>(index)] : nullptr;
}

std::vector<float> MainWindow::buildListLines() const
{
	std::vector<float> lines;
	if (!m_state.map || !m_state.map->map().sides || m_buildSide < 0 || m_buildSide >= static_cast<int>(m_state.map->map().sides->sides.size()))
		return lines;
	const std::vector<MapModel::BuildListEntry> &list = m_state.map->map().sides->sides[static_cast<std::size_t>(m_buildSide)].buildList;
	for (std::size_t i = 0; i < list.size(); ++i)
	{
		const MapModel::BuildListEntry &e = list[i];
		// A turned square with a line to the front, and the number of the entry as ticks is left out:
		// the panel shows the order.
		const float c = std::cos(e.angle);
		const float s = std::sin(e.angle);
		const auto corner = [&](float u, float v) { return std::pair<float, float>{e.x + u * c - v * s, e.y + u * s + v * c}; };
		const std::pair<float, float> p[4] = {corner(-HALF_SIZE, -HALF_SIZE), corner(HALF_SIZE, -HALF_SIZE), corner(HALF_SIZE, HALF_SIZE), corner(-HALF_SIZE, HALF_SIZE)};
		for (int k = 0; k < 4; ++k)
			lines.insert(lines.end(), {p[k].first, p[k].second, p[(k + 1) % 4].first, p[(k + 1) % 4].second});
		const auto front = corner(HALF_SIZE * 1.6f, 0.0f);
		lines.insert(lines.end(), {e.x, e.y, front.first, front.second});
		if (static_cast<int>(i) == m_buildEntry)
		{
			constexpr int SEGMENTS = 24;
			for (int k = 0; k < SEGMENTS; ++k)
			{
				const float a = static_cast<float>(k) * 2.0f * PI / SEGMENTS;
				const float b = static_cast<float>(k + 1) * 2.0f * PI / SEGMENTS;
				lines.insert(lines.end(), {e.x + std::cos(a) * TURN_OUTER, e.y + std::sin(a) * TURN_OUTER, e.x + std::cos(b) * TURN_OUTER, e.y + std::sin(b) * TURN_OUTER});
			}
		}
	}
	if (m_buildDrag.active && m_buildDrag.adding)
		lines.insert(lines.end(), {m_buildDrag.x0, m_buildDrag.y0, m_buildDrag.x, m_buildDrag.y});
	return lines;
}

void MainWindow::appendBuildListObjects(MapModel::Map &copy) const
{
	// The engine draws the entries like objects of the player's default team (its colour). Without an
	// editor index they cannot be picked as objects.
	if (!copy.sides)
		return;
	if (!copy.objects)
		copy.objects = std::vector<MapModel::MapObject>{};
	for (const MapModel::Side &side : copy.sides->sides)
	{
		const std::string team = "team" + MapEditorCore::playerName(side);
		for (const MapModel::BuildListEntry &e : side.buildList)
		{
			MapModel::MapObject o;
			o.x = e.x;
			o.y = e.y;
			o.z = e.z;
			o.angle = e.angle;
			o.templateName = e.templateName;
			o.properties.set("originalOwner", team);
			copy.objects->push_back(std::move(o));
		}
	}
}

void MainWindow::buildListChanged()
{
	if (m_overview)
		m_overview->setToolLines(toolLines());
	applyOverlay();
}

void MainWindow::buildListToolEvent(const ToolEvent &e)
{
	MapModel::Map &map = m_state.map->map();
	if (!map.sides || m_buildSide < 0 || m_buildSide >= static_cast<int>(map.sides->sides.size()))
		return;
	const bool neutral = MapEditorCore::playerName(map.sides->sides[static_cast<std::size_t>(m_buildSide)]).empty();
	switch (e.kind)
	{
	case ToolEvent::Kind::Press:
	{
		m_buildDrag = BuildDrag{};
		m_buildDrag.before = MapEditorCore::sidesState(map);
		m_buildDrag.x0 = m_buildDrag.x = snapCoord(e.x);
		m_buildDrag.y0 = m_buildDrag.y = snapCoord(e.y);
		// The ring of the selected entry turns it, the entry itself moves it.
		if (const MapModel::BuildListEntry *selected = buildEntry(m_buildSide, m_buildEntry))
		{
			const float d = std::hypot(selected->x - e.x, selected->y - e.y);
			if (d > TURN_INNER && d < TURN_OUTER + e.tolerance)
			{
				m_buildDrag.active = true;
				m_buildDrag.turning = true;
				m_buildDrag.index = m_buildEntry;
				return;
			}
		}
		const int hit = MapEditorCore::buildEntryAt(*map.sides, m_buildSide, e.x, e.y, std::max(e.tolerance, HALF_SIZE));
		if (hit >= 0)
		{
			m_buildEntry = hit;
			m_buildDrag.active = true;
			m_buildDrag.index = hit;
			m_buildDrag.startX = buildEntry(m_buildSide, hit)->x;
			m_buildDrag.startY = buildEntry(m_buildSide, hit)->y;
			buildListChanged();
			fillSelectionPanel();
			return;
		}
		if (neutral)
		{
			toast(T("The neutral player has no build list."));
			return;
		}
		if (m_buildTemplate.isEmpty())
		{
			toast(T("Choose a structure for the build list first."));
			return;
		}
		m_buildDrag.active = true;
		m_buildDrag.adding = true;
		buildListChanged();
		return;
	}
	case ToolEvent::Kind::Move:
	{
		if (!m_buildDrag.active)
			return;
		m_buildDrag.x = e.x;
		m_buildDrag.y = e.y;
		if (!m_buildDrag.adding)
		{
			MapModel::BuildListEntry &entry = map.sides->sides[static_cast<std::size_t>(m_buildSide)].buildList[static_cast<std::size_t>(m_buildDrag.index)];
			if (m_buildDrag.turning)
				entry.angle = std::atan2(e.y - entry.y, e.x - entry.x);
			else
			{
				entry.x = m_buildDrag.startX + (e.x - m_buildDrag.x0);
				entry.y = m_buildDrag.startY + (e.y - m_buildDrag.y0);
			}
		}
		buildListChanged();
		return;
	}
	case ToolEvent::Kind::Release:
	{
		if (!m_buildDrag.active)
			return;
		BuildDrag drag = m_buildDrag;
		m_buildDrag = BuildDrag{};
		const char *name = drag.turning ? "Turn build list entry" : "Move build list entry";
		if (drag.adding)
		{
			const bool dragged = std::hypot(e.x - drag.x0, e.y - drag.y0) > std::max(e.tolerance, CELL);
			const float angle = dragged ? std::atan2(e.y - drag.y0, e.x - drag.x0) : 0.0f;
			m_buildEntry = MapEditorCore::addBuildEntry(*map.sides, m_buildSide, MapEditorCore::newBuildEntry(ascii(m_buildTemplate), drag.x0, drag.y0, angle));
			name = "Add to build list";
		}
		auto step = MapEditorCore::sidesStep(map, std::move(drag.before), name);
		if (step)
		{
			const MapEditorCore::Changes changes = step->changes();
			m_state.map->history.record(std::move(step));
			applyChanges(changes);
		}
		buildListChanged();
		fillSelectionPanel();
		return;
	}
	case ToolEvent::Kind::DoubleClick:
		return;
	}
}

void MainWindow::addBuildListEditors(QVBoxLayout *box)
{
	box->addWidget(label(T("BUILD LIST"), "Section"));
	if (!m_state.map || !m_state.map->map().sides)
	{
		box->addWidget(label(T("The map has no players."), "Hint"));
		return;
	}
	const MapModel::Sides &sides = *m_state.map->map().sides;
	m_buildSide = std::clamp(m_buildSide, 0, static_cast<int>(sides.sides.size()) - 1);
	const int side = m_buildSide;
	const MapModel::Side &player = sides.sides[static_cast<std::size_t>(side)];
	const bool neutral = MapEditorCore::playerName(player).empty();
	auto *form = new QFormLayout;
	box->addLayout(form);
	auto *players = new QComboBox;
	players->setObjectName(QStringLiteral("BuildListPlayer"));
	for (const MapModel::Side &s : sides.sides)
		players->addItem(MapEditorCore::playerName(s).empty() ? T("(neutral)") : utf8(MapEditorCore::playerName(s)));
	players->setCurrentIndex(side);
	connect(players, &QComboBox::activated, this, [this](int i) {
		m_buildSide = i;
		m_buildEntry = -1;
		buildListChanged();
		QTimer::singleShot(0, this, [this] { fillSelectionPanel(); });
	});
	form->addRow(T("Player"), players);
	auto *structure = new QComboBox;
	structure->setObjectName(QStringLiteral("BuildListTemplate"));
	structure->setEditable(true);
	structure->setInsertPolicy(QComboBox::NoInsert);
	QStringList structures;
	for (const MeTemplateInfo &info : EngineClient::instance().catalog())
		if (info.sorting == 1)
			structures << QString::fromLatin1(info.name);
	structures.sort(Qt::CaseInsensitive);
	structure->addItems(structures);
	structure->completer()->setFilterMode(Qt::MatchContains);
	structure->completer()->setCaseSensitivity(Qt::CaseInsensitive);
	structure->setCurrentText(m_buildTemplate);
	connect(structure, &QComboBox::currentTextChanged, this, [this](const QString &text) { m_buildTemplate = text.trimmed(); });
	structure->setEnabled(!neutral);
	form->addRow(T("Structure"), structure);
	auto *hint = label(neutral ? T("The neutral player has no build list.")
		: T("A click adds the structure (drag to turn it), dragging an entry moves it, the ring around the selected one turns it."), "Hint");
	hint->setWordWrap(true);
	box->addWidget(hint);

	auto *list = new QListWidget;
	list->setObjectName(QStringLiteral("BuildListEntries"));
	list->setMinimumHeight(140);
	for (std::size_t i = 0; i < player.buildList.size(); ++i)
	{
		const MapModel::BuildListEntry &e = player.buildList[i];
		QString text = QStringLiteral("%1. %2").arg(i + 1).arg(utf8(e.templateName));
		if (!e.buildingName.empty())
			text += QStringLiteral(" (") + utf8(e.buildingName) + QStringLiteral(")");
		if (e.initiallyBuilt)
			text += QStringLiteral(" *");
		list->addItem(text);
	}
	if (m_buildEntry >= list->count())
		m_buildEntry = -1;
	list->setCurrentRow(m_buildEntry);
	connect(list, &QListWidget::currentRowChanged, this, [this](int row) {
		if (row == m_buildEntry)
			return;
		m_buildEntry = row;
		buildListChanged();
		QTimer::singleShot(0, this, [this] { fillSelectionPanel(); });
	});
	box->addWidget(list);

	// A change of the list or the selected entry as a step of the players.
	const auto change = [this, side](const char *step, const std::function<bool(MapModel::Sides &)> &edit, bool mergeable, bool refresh) {
		const bool done = editSides(step, [edit](MapModel::Map &m) { return edit(*m.sides); }, mergeable, !mergeable);
		buildListChanged();
		if (done && refresh)
			QTimer::singleShot(0, this, [this] { fillSelectionPanel(); });
		(void)side;
		return done;
	};
	const int entry = m_buildEntry;
	auto *row = new QHBoxLayout;
	const auto button = [this, row](const QString &text, const char *name, const std::function<void()> &action) {
		auto *b = new QPushButton(text);
		b->setObjectName(QString::fromLatin1(name));
		connect(b, &QPushButton::clicked, this, action);
		row->addWidget(b);
		return b;
	};
	button(T("Up"), "BuildListUp", [this, change, side, entry] {
		if (change("Move build list entry", [side, entry](MapModel::Sides &s) { return MapEditorCore::moveBuildEntry(s, side, entry, -1); }, false, true))
			m_buildEntry = entry - 1;
	})->setEnabled(entry > 0);
	button(T("Down"), "BuildListDown", [this, change, side, entry] {
		if (change("Move build list entry", [side, entry](MapModel::Sides &s) { return MapEditorCore::moveBuildEntry(s, side, entry, 1); }, false, true))
			m_buildEntry = entry + 1;
	})->setEnabled(entry >= 0 && entry + 1 < static_cast<int>(player.buildList.size()));
	button(T("Delete"), "BuildListDelete", [this, change, side, entry] {
		if (change("Delete build list entry", [side, entry](MapModel::Sides &s) { return MapEditorCore::removeBuildEntry(s, side, entry); }, false, true))
			m_buildEntry = -1;
	})->setEnabled(entry >= 0);
	button(T("Export..."), "BuildListExport", [this, side] {
		const QString start = QDir(m_state.map->path.isEmpty() ? m_state.userMapFolder(m_state.game) : QFileInfo(m_state.map->path).dir().path())
			.filePath(utf8(MapEditorCore::playerName(m_state.map->map().sides->sides[static_cast<std::size_t>(side)])) + QStringLiteral("_BuildList.ini"));
		const QString path = QFileDialog::getSaveFileName(this, T("Export build list"), start, T("INI files (*.ini)"));
		if (path.isEmpty())
			return;
		QString error;
		toast(exportBuildListIni(path, side, &error) ? T("Build list exported to %1").arg(QFileInfo(path).fileName()) : error);
	})->setEnabled(!player.buildList.empty());
	box->addLayout(row);

	// Power of the player up to the selected entry (or the whole list).
	const MapEditorCore::BuildPower power = MapEditorCore::buildListPower(m_state.map->map(), side,
		entry >= 0 ? entry : static_cast<int>(player.buildList.size()) - 1, [](const std::string &name) {
			for (const MeTemplateInfo &info : EngineClient::instance().catalog())
				if (name == info.name)
					return static_cast<int>(info.energy);
			return 0;
		});
	auto *bar = new QProgressBar;
	bar->setObjectName(QStringLiteral("BuildListPower"));
	bar->setRange(0, 100);
	bar->setValue(static_cast<int>(std::lround((1.0f - power.used()) * 100.0f)));
	bar->setFormat(T("Power %1 / %2").arg(power.production - power.consumption).arg(power.production));
	form = new QFormLayout;
	box->addLayout(form);
	form->addRow(T("Power"), bar);

	const MapModel::BuildListEntry *e = buildEntry(side, entry);
	if (!e)
		return;
	const auto entryChange = [change, side, entry](const char *step, const std::function<void(MapModel::BuildListEntry &)> &set, bool mergeable) {
		change(step, [side, entry, set](MapModel::Sides &s) {
			if (side >= static_cast<int>(s.sides.size()) || entry >= static_cast<int>(s.sides[static_cast<std::size_t>(side)].buildList.size()))
				return false;
			set(s.sides[static_cast<std::size_t>(side)].buildList[static_cast<std::size_t>(entry)]);
			return true;
		}, mergeable, !mergeable);
	};
	auto *name = new QLineEdit(utf8(e->buildingName));
	name->setObjectName(QStringLiteral("BuildListName"));
	connect(name, &QLineEdit::editingFinished, this, [name, entryChange] {
		const std::string text = ascii(name->text().trimmed());
		entryChange("Change build list entry", [text](MapModel::BuildListEntry &x) { x.buildingName = text; }, false);
	});
	form->addRow(T("Name"), name);
	auto *script = new QComboBox;
	script->setObjectName(QStringLiteral("BuildListScript"));
	script->addItem(T("(none)"));
	for (const std::string &s : MapEditorCore::scriptNames(sides, true))
		script->addItem(utf8(s));
	if (!e->script.empty())
	{
		if (script->findText(utf8(e->script)) < 0)
			script->addItem(utf8(e->script));
		script->setCurrentText(utf8(e->script));
	}
	connect(script, &QComboBox::activated, this, [script, entryChange](int chosen) {
		const std::string text = chosen == 0 ? std::string() : ascii(script->itemText(chosen));
		entryChange("Change build list entry", [text](MapModel::BuildListEntry &x) { x.script = text; }, false);
	});
	form->addRow(T("Script"), script);
	auto *health = new QSpinBox;
	health->setObjectName(QStringLiteral("BuildListHealth"));
	health->setRange(0, 1000);
	health->setSuffix(QStringLiteral(" %"));
	health->setValue(e->health);
	connect(health, &QSpinBox::valueChanged, this, [entryChange](int v) {
		entryChange("Change build list entry", [v](MapModel::BuildListEntry &x) { x.health = v; }, true);
	});
	form->addRow(T("Health"), health);
	auto *rebuilds = new QComboBox;
	rebuilds->setObjectName(QStringLiteral("BuildListRebuilds"));
	for (int i = 0; i <= 5; ++i)
		rebuilds->addItem(QString::number(i), i);
	rebuilds->addItem(T("Unlimited"), MapEditorCore::UNLIMITED_REBUILDS);
	if (rebuilds->findData(e->numRebuilds) < 0)
		rebuilds->addItem(QString::number(e->numRebuilds), e->numRebuilds);
	rebuilds->setCurrentIndex(rebuilds->findData(e->numRebuilds));
	connect(rebuilds, &QComboBox::activated, this, [rebuilds, entryChange](int chosen) {
		const int value = rebuilds->itemData(chosen).toInt();
		entryChange("Change build list entry", [value](MapModel::BuildListEntry &x) { x.numRebuilds = value; }, false);
	});
	form->addRow(T("Rebuilds"), rebuilds);
	auto *built = new QCheckBox(T("Built at the start"));
	built->setObjectName(QStringLiteral("BuildListBuilt"));
	built->setChecked(e->initiallyBuilt);
	connect(built, &QCheckBox::toggled, this, [entryChange](bool on) {
		entryChange("Change build list entry", [on](MapModel::BuildListEntry &x) { x.initiallyBuilt = on; }, false);
	});
	form->addRow(QString(), built);
	auto *unsellable = new QCheckBox(T("Cannot be sold"));
	unsellable->setObjectName(QStringLiteral("BuildListUnsellable"));
	unsellable->setChecked(e->unsellable);
	connect(unsellable, &QCheckBox::toggled, this, [entryChange](bool on) {
		entryChange("Change build list entry", [on](MapModel::BuildListEntry &x) { x.unsellable = on; }, false);
	});
	form->addRow(QString(), unsellable);
	auto *angle = new QDoubleSpinBox;
	angle->setObjectName(QStringLiteral("BuildListAngle"));
	angle->setRange(-360.0, 360.0);
	angle->setDecimals(1);
	angle->setSuffix(QStringLiteral(" deg"));
	angle->setValue(static_cast<double>(e->angle) * 180.0 / PI);
	connect(angle, &QDoubleSpinBox::valueChanged, this, [entryChange](double v) {
		const float a = static_cast<float>(v) * PI / 180.0f;
		entryChange("Change build list entry", [a](MapModel::BuildListEntry &x) { x.angle = a; }, true);
	});
	form->addRow(T("Angle"), angle);
	auto *height = new QDoubleSpinBox;
	height->setObjectName(QStringLiteral("BuildListHeight"));
	height->setRange(-50.0, 50.0);
	height->setDecimals(1);
	height->setValue(e->z);
	connect(height, &QDoubleSpinBox::valueChanged, this, [entryChange](double v) {
		const float z = static_cast<float>(v);
		entryChange("Change build list entry", [z](MapModel::BuildListEntry &x) { x.z = z; }, true);
	});
	form->addRow(T("Height offset"), height);
	auto *where = label(QStringLiteral("x %1, y %2").arg(static_cast<double>(e->x), 0, 'f', 1).arg(static_cast<double>(e->y), 0, 'f', 1), "Meta");
	form->addRow(T("Position"), where);
}

bool MainWindow::exportBuildListIni(const QString &path, int side, QString *error)
{
	const MapModel::Map &map = m_state.map->map();
	if (!map.sides || side < 0 || side >= static_cast<int>(map.sides->sides.size()))
		return false;
	const MapModel::Side &player = map.sides->sides[static_cast<std::size_t>(side)];
	// The block names the faction's side ("America"), as the old export took it from the player template.
	const std::string faction = player.properties.getAscii("playerFaction").value_or("");
	std::string factionSide = faction;
	for (const MePlayerTemplateInfo &f : EngineClient::instance().factions())
		if (faction == f.name)
			factionSide = f.side;
	const std::string text = MapEditorCore::skirmishBuildListIni(player, factionSide);
	QFile out(path);
	if (!out.open(QIODevice::WriteOnly) || out.write(text.data(), static_cast<qint64>(text.size())) != static_cast<qint64>(text.size()))
	{
		*error = T("The file cannot be written: %1").arg(path);
		return false;
	}
	return true;
}

} // namespace MapEditorApp
