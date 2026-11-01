/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "App/Windows/SidesWindow.h"

#include "App/EditorContext.h"
#include "App/Common.h"
#include "App/Windows/ScriptsWindow.h"
#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/Widgets.h"
#include "Core/SidesCommands.h"
#include "Core/WaypointCommands.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCompleter>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <set>

namespace MapEditorApp
{

QString playerLabel(const MapModel::Side &side)
{
	const std::string name = MapEditorCore::playerName(side);
	return name.empty() ? T("(neutral)") : utf8(name);
}

QString displayName(const MapModel::Side &side)
{
	if (const auto *v = side.properties.find("playerDisplayName"); v && std::holds_alternative<std::u16string>(*v))
		return QString::fromStdU16String(std::get<std::u16string>(*v));
	return QString();
}

namespace
{

constexpr int ROLE_INDEX = Qt::UserRole + 1;

QIcon swatch(std::uint32_t rgb)
{
	QPixmap p(14, 14);
	p.fill(QColor::fromRgb(rgb & 0xFFFFFFu));
	return QIcon(p);
}

// Names of the subroutine scripts and subroutine groups of every player (EditParameter::loadScripts),
// which teams may name.
QStringList subroutineScripts(const MapModel::Map &map)
{
	QStringList names;
	if (!map.sides)
		return names;
	for (const MapModel::Side &side : map.sides->sides)
	{
		for (const MapModel::Script &s : side.scripts.scripts)
			if (s.isSubroutine)
				names << utf8(s.name);
		for (const MapModel::ScriptGroup &g : side.scripts.groups)
		{
			if (g.isSubroutine)
				names << utf8(g.name);
			for (const MapModel::Script &s : g.scripts)
				if (s.isSubroutine)
					names << utf8(s.name);
		}
	}
	return names;
}

QStringList waypointNames(const MapModel::Map &map)
{
	QStringList names;
	if (!map.objects)
		return names;
	for (const MapModel::MapObject &o : *map.objects)
		if (MapEditorCore::isWaypoint(o))
			if (const auto n = o.properties.getAscii("waypointName"); n && !n->empty())
				names << utf8(*n);
	names.sort(Qt::CaseInsensitive);
	return names;
}

} // namespace

SidesWindow::SidesWindow(EditorContext &editor, QWidget *parent) : QDialog(parent), m_editor(editor)
{
	setObjectName(QStringLiteral("SidesWindow"));
	setWindowTitle(T("Players and teams"));
	resize(900, 640);
}

void SidesWindow::refresh()
{
	if (!m_editor.openMap())
		return;
	QDialog *window = this;
	// Detached at once, so lookups by name find only the new fields.
	for (QWidget *child : window->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly))
	{
		child->hide();
		child->setParent(nullptr);
		child->deleteLater();
	}
	delete window->layout();
	auto *col = new QVBoxLayout(window);
	auto *tabs = new QTabWidget(window);
	tabs->setObjectName(QStringLiteral("SidesTabs"));
	col->addWidget(tabs);

	MapModel::Map &map = m_editor.openMap()->map();
	if (!map.sides)
	{
		auto *empty = new QWidget;
		auto *v = new QVBoxLayout(empty);
		auto *add = new QPushButton(T("Add the neutral player"));
		connect(add, &QPushButton::clicked, this, [this] {
			m_editor.editSides("Add player", [](MapModel::Map &) { return true; });
		});
		v->addWidget(label(T("The map has no players."), "Hint"));
		v->addWidget(add);
		v->addStretch(1);
		tabs->addTab(empty, T("Players"));
		return;
	}
	const MapModel::Sides &sides = *map.sides;
	m_player = std::clamp(m_player, 0, static_cast<int>(sides.sides.size()) - 1);
	EngineClient &engine = EngineClient::instance();

	// Players ------------------------------------------------------------------------------------
	auto *players = new QWidget;
	auto *prow = new QHBoxLayout(players);
	auto *left = new QVBoxLayout;
	prow->addLayout(left, 2);
	auto *list = new QListWidget;
	list->setObjectName(QStringLiteral("SidesPlayers"));
	for (std::size_t i = 0; i < sides.sides.size(); ++i)
	{
		auto *item = new QListWidgetItem(playerLabel(sides.sides[i]), list);
		if (const auto c = sides.sides[i].properties.getInt("playerColor"))
			item->setIcon(swatch(static_cast<std::uint32_t>(*c)));
	}
	list->setCurrentRow(m_player);
	connect(list, &QListWidget::currentRowChanged, this, [this](int row) {
		if (row < 0 || row == m_player)
			return;
		m_player = row;
		m_team = -1;
		QTimer::singleShot(0, this, [this] { refresh(); });
	});
	left->addWidget(list, 1);
	auto *pbuttons = new QHBoxLayout;
	const auto button = [this](QHBoxLayout *row, const QString &text, const char *name, const std::function<void()> &action) {
		auto *b = new QPushButton(text);
		b->setObjectName(QString::fromLatin1(name));
		connect(b, &QPushButton::clicked, this, action);
		row->addWidget(b);
		return b;
	};
	const int playerIndex = m_player;
	const MapModel::Side &side = sides.sides[static_cast<std::size_t>(playerIndex)];
	const bool neutral = MapEditorCore::playerName(side).empty();
	button(pbuttons, T("New..."), "SidesPlayerNew", [this] {
		QStringList factions;
		for (const MePlayerTemplateInfo &f : EngineClient::instance().factions())
			factions << QString::fromLatin1(f.name);
		bool ok = false;
		const QString faction = QInputDialog::getItem(this, T("New player"), T("Faction"), factions, 0, true, &ok);
		if (!ok)
			return;
		int added = -1;
		m_editor.editSides("Add player", [&added, faction](MapModel::Map &m) {
			const std::string name = MapEditorCore::freePlayerName(*m.sides);
			added = MapEditorCore::addPlayer(*m.sides, name, QString::fromStdString(name).toStdU16String(), ascii(faction), true);
			return added >= 0;
		});
		if (added < 0)
			m_editor.toast(T("No more players fit into the map."));
		else
		{
			m_player = added;
			refresh();
		}
	});
	button(pbuttons, T("Remove"), "SidesPlayerRemove", [this, playerIndex] {
		const int count = MapEditorCore::playerObjectCount(m_editor.openMap()->map(), playerIndex);
		if (count > 0 && QMessageBox::question(this, T("Remove player"),
			T("%1 objects belong to teams of this player. Remove the player and its teams anyway?").arg(count)) != QMessageBox::Yes)
			return;
		m_editor.editSides("Remove player", [playerIndex](MapModel::Map &m) { return MapEditorCore::removePlayer(*m.sides, playerIndex); });
	})->setEnabled(!neutral);
	button(pbuttons, T("Skirmish players"), "SidesSkirmish", [this] {
		const bool zeroHour = m_editor.game() == Game::ZeroHour;
		int added = 0;
		m_editor.editSides("Add skirmish players", [zeroHour, &added](MapModel::Map &m) {
			added = MapEditorCore::addSkirmishPlayers(*m.sides, zeroHour);
			return added > 0;
		});
		m_editor.toast(added > 0 ? T("%1 skirmish players added").arg(added) : T("The skirmish players are there already."));
	});
	left->addLayout(pbuttons);

	auto *pform = new QFormLayout;
	prow->addLayout(pform, 3);
	auto *name = new QLineEdit(utf8(MapEditorCore::playerName(side)));
	name->setObjectName(QStringLiteral("SidesPlayerName"));
	name->setEnabled(!neutral);
	connect(name, &QLineEdit::editingFinished, this, [this, name, playerIndex] {
		const std::string text = ascii(name->text().trimmed());
		const MapModel::Map &m = m_editor.openMap()->map();
		if (!m.sides || playerIndex >= static_cast<int>(m.sides->sides.size()) || text == MapEditorCore::playerName(m.sides->sides[static_cast<std::size_t>(playerIndex)]))
			return;
		if (!m_editor.editSides("Rename player", [playerIndex, text](MapModel::Map &mm) { return MapEditorCore::renamePlayer(mm, playerIndex, text); }))
		{
			m_editor.toast(T("The name is empty or taken."));
			QTimer::singleShot(0, this, [this] { refresh(); });
		}
	});
	pform->addRow(T("Name"), name);
	auto *display = new QLineEdit(displayName(side));
	display->setObjectName(QStringLiteral("SidesDisplayName"));
	connect(display, &QLineEdit::textEdited, this, [this, playerIndex](const QString &text) {
		m_editor.editSides("Change display name", [playerIndex, text](MapModel::Map &m) {
			m.sides->sides[static_cast<std::size_t>(playerIndex)].properties.set("playerDisplayName", text.toStdU16String());
			return true;
		}, true, false);
	});
	pform->addRow(T("Display name"), display);
	auto *faction = new QComboBox;
	faction->setObjectName(QStringLiteral("SidesFaction"));
	const QString currentFaction = utf8(side.properties.getAscii("playerFaction").value_or(""));
	for (const MePlayerTemplateInfo &f : engine.factions())
		faction->addItem(swatch(f.color), QString::fromLatin1(f.name));
	if (faction->findText(currentFaction) < 0)
		faction->addItem(currentFaction);
	faction->setCurrentText(currentFaction);
	faction->setEnabled(!neutral);
	connect(faction, &QComboBox::activated, this, [this, faction, playerIndex](int) {
		const std::string value = ascii(faction->currentText());
		m_editor.editSides("Change faction", [playerIndex, value](MapModel::Map &m) {
			m.sides->sides[static_cast<std::size_t>(playerIndex)].properties.set("playerFaction", value);
			return true;
		});
	});
	pform->addRow(T("Faction"), faction);
	auto *computer = new QCheckBox(T("Computer player"));
	computer->setObjectName(QStringLiteral("SidesComputer"));
	computer->setChecked(!side.properties.getBool("playerIsHuman").value_or(false));
	computer->setEnabled(!neutral);
	connect(computer, &QCheckBox::toggled, this, [this, playerIndex](bool on) {
		m_editor.editSides("Change controller", [playerIndex, on](MapModel::Map &m) {
			m.sides->sides[static_cast<std::size_t>(playerIndex)].properties.set("playerIsHuman", !on);
			return true;
		});
	});
	pform->addRow(QString(), computer);
	auto *color = new QComboBox;
	color->setObjectName(QStringLiteral("SidesColor"));
	color->addItem(T("Colour of the faction"), QVariant());
	const std::optional<std::int32_t> ownColor = side.properties.getInt("playerColor");
	int colorRow = 0;
	for (const MeColorInfo &c : engine.colors())
	{
		color->addItem(swatch(c.color), utf8(c.name), QVariant(static_cast<qlonglong>(c.color)));
		if (ownColor && (static_cast<std::uint32_t>(*ownColor) & 0xFFFFFFu) == (c.color & 0xFFFFFFu))
			colorRow = color->count() - 1;
	}
	if (ownColor && colorRow == 0)
	{
		const std::uint32_t rgb = static_cast<std::uint32_t>(*ownColor) & 0xFFFFFFu;
		color->addItem(swatch(rgb), QStringLiteral("#%1").arg(rgb, 6, 16, QLatin1Char('0')), QVariant(static_cast<qlonglong>(rgb | 0xFF000000u)));
		colorRow = color->count() - 1;
	}
	color->addItem(T("Choose..."), QVariant(QStringLiteral("choose")));
	color->setCurrentIndex(colorRow);
	color->setEnabled(!neutral);
	connect(color, &QComboBox::activated, this, [this, color, playerIndex](int row) {
		QVariant value = color->itemData(row);
		if (value.typeId() == QMetaType::QString)
		{
			const QColor chosen = QColorDialog::getColor(Qt::white, this, T("Player colour"));
			if (!chosen.isValid())
			{
				QTimer::singleShot(0, this, [this] { refresh(); });
				return;
			}
			value = QVariant(static_cast<qlonglong>(0xFF000000u | (chosen.rgb() & 0xFFFFFFu)));
		}
		m_editor.editSides("Change colour", [playerIndex, value](MapModel::Map &m) {
			MapModel::PropertyMap &p = m.sides->sides[static_cast<std::size_t>(playerIndex)].properties;
			if (value.isValid())
				p.set("playerColor", static_cast<std::int32_t>(static_cast<std::uint32_t>(value.toLongLong())));
			else
				p.erase("playerColor");
			return true;
		});
	});
	pform->addRow(T("Colour"), color);

	// Relations to the other players (not the neutral one): how this one regards them, and back.
	auto *relations = new QTableWidget;
	relations->setObjectName(QStringLiteral("SidesRelations"));
	relations->setColumnCount(3);
	relations->setHorizontalHeaderLabels({T("Player"), T("Regarded as"), T("Regards this player as")});
	relations->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	relations->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	relations->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	relations->verticalHeader()->hide();
	relations->setEnabled(!neutral);
	const QStringList relationNames = {T("Neutral"), T("Ally"), T("Enemy")};
	for (std::size_t i = 0; i < sides.sides.size(); ++i)
	{
		const int other = static_cast<int>(i);
		if (other == playerIndex || MapEditorCore::playerName(sides.sides[i]).empty())
			continue;
		const int row = relations->rowCount();
		relations->insertRow(row);
		relations->setItem(row, 0, new QTableWidgetItem(playerLabel(sides.sides[i])));
		auto *combo = new QComboBox;
		combo->setObjectName(QStringLiteral("SidesRelation%1").arg(other));
		combo->addItems(relationNames);
		combo->setCurrentIndex(static_cast<int>(MapEditorCore::relation(sides, playerIndex, other)));
		connect(combo, &QComboBox::activated, this, [this, playerIndex, other](int r) {
			m_editor.editSides("Change relation", [playerIndex, other, r](MapModel::Map &m) {
				MapEditorCore::setRelation(*m.sides, playerIndex, other, static_cast<MapEditorCore::Relation>(r));
				return true;
			});
		});
		relations->setCellWidget(row, 1, combo);
		relations->setItem(row, 2, new QTableWidgetItem(relationNames[static_cast<int>(MapEditorCore::relation(sides, other, playerIndex))]));
	}
	pform->addRow(T("Relations"), relations);
	tabs->addTab(players, T("Players"));

	// Teams --------------------------------------------------------------------------------------
	auto *teams = new QWidget;
	auto *tcol = new QVBoxLayout(teams);
	auto *filterRow = new QHBoxLayout;
	filterRow->addWidget(label(T("Player"), nullptr));
	auto *owner = new QComboBox;
	owner->setObjectName(QStringLiteral("SidesTeamPlayer"));
	for (const MapModel::Side &s : sides.sides)
		owner->addItem(playerLabel(s));
	owner->setCurrentIndex(playerIndex);
	connect(owner, &QComboBox::activated, this, [this](int row) {
		m_player = row;
		m_team = -1;
		QTimer::singleShot(0, this, [this] { refresh(); });
	});
	filterRow->addWidget(owner, 1);
	tcol->addLayout(filterRow);
	auto *teamList = new QTreeWidget;
	teamList->setObjectName(QStringLiteral("SidesTeams"));
	teamList->setColumnCount(5);
	teamList->setHeaderLabels({T("Team"), T("Home"), T("Priority"), T("On create"), T("Condition")});
	teamList->setRootIsDecorated(false);
	teamList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	const std::string ownerName = MapEditorCore::playerName(side);
	const auto fillRow = [&sides](QTreeWidgetItem *item, const MapModel::PropertyMap &team) {
		const bool isDefault = MapEditorCore::isDefaultTeam(sides, team);
		item->setText(0, isDefault ? T("(default team)") : utf8(MapEditorCore::teamName(team)));
		item->setText(1, utf8(team.getAscii("teamHome").value_or("")));
		item->setText(2, QString::number(team.getInt("teamProductionPriority").value_or(0)));
		item->setText(3, utf8(team.getAscii("teamOnCreateScript").value_or("")));
		item->setText(4, utf8(team.getAscii("teamProductionCondition").value_or("")));
	};
	QTreeWidgetItem *currentItem = nullptr;
	for (std::size_t t = 0; t < sides.teams.size(); ++t)
	{
		if (MapEditorCore::teamOwner(sides.teams[t]) != ownerName)
			continue;
		auto *item = new QTreeWidgetItem(teamList);
		fillRow(item, sides.teams[t]);
		item->setData(0, ROLE_INDEX, static_cast<int>(t));
		if (static_cast<int>(t) == m_team || (m_team < 0 && !currentItem))
			currentItem = item;
	}
	if (!currentItem && teamList->topLevelItemCount() > 0)
		currentItem = teamList->topLevelItem(0);
	m_team = currentItem ? currentItem->data(0, ROLE_INDEX).toInt() : -1;
	if (currentItem)
		teamList->setCurrentItem(currentItem);
	connect(teamList, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
		if (!item || item->data(0, ROLE_INDEX).toInt() == m_team)
			return;
		m_team = item->data(0, ROLE_INDEX).toInt();
		QTimer::singleShot(0, this, [this] { refresh(); });
	});
	tcol->addWidget(teamList, 2);
	const int teamIndex = m_team;
	const bool defaultTeam = teamIndex < 0 || MapEditorCore::isDefaultTeam(sides, sides.teams[static_cast<std::size_t>(teamIndex)]);
	auto *tbuttons = new QHBoxLayout;
	button(tbuttons, T("New"), "SidesTeamNew", [this, ownerName] {
		int added = -1;
		m_editor.editSides("Add team", [&added, ownerName](MapModel::Map &m) {
			added = MapEditorCore::addTeam(*m.sides, ownerName);
			return added >= 0;
		}, false, false);
		m_team = added;
		refresh();
	});
	button(tbuttons, T("Copy"), "SidesTeamCopy", [this, teamIndex] {
		int added = -1;
		m_editor.editSides("Copy team", [&added, teamIndex](MapModel::Map &m) {
			added = MapEditorCore::copyTeam(*m.sides, teamIndex);
			return added >= 0;
		}, false, false);
		m_team = added;
		refresh();
	})->setEnabled(!defaultTeam);
	button(tbuttons, T("Delete"), "SidesTeamDelete", [this, teamIndex] {
		const MapModel::Map &m = m_editor.openMap()->map();
		const std::size_t count = MapEditorCore::teamObjects(m, MapEditorCore::teamName(m.sides->teams[static_cast<std::size_t>(teamIndex)])).size();
		if (count > 0 && QMessageBox::question(this, T("Delete team"),
			T("%1 objects belong to this team. Delete it anyway?").arg(count)) != QMessageBox::Yes)
			return;
		m_team = -1;
		m_editor.editSides("Delete team", [teamIndex](MapModel::Map &mm) { return MapEditorCore::removeTeam(*mm.sides, teamIndex); });
	})->setEnabled(!defaultTeam);
	button(tbuttons, T("Up"), "SidesTeamUp", [this, teamIndex] {
		if (m_editor.editSides("Move team", [teamIndex](MapModel::Map &m) { return MapEditorCore::moveTeam(*m.sides, teamIndex, -1); }, false, false))
			m_team = teamIndex - 1;
		refresh();
	})->setEnabled(!defaultTeam);
	button(tbuttons, T("Down"), "SidesTeamDown", [this, teamIndex] {
		if (m_editor.editSides("Move team", [teamIndex](MapModel::Map &m) { return MapEditorCore::moveTeam(*m.sides, teamIndex, 1); }, false, false))
			m_team = teamIndex + 1;
		refresh();
	})->setEnabled(!defaultTeam);
	button(tbuttons, T("Select members"), "SidesTeamSelect", [this, teamIndex] { m_editor.selectTeam(teamIndex); })->setEnabled(teamIndex >= 0);
	tcol->addLayout(tbuttons);

	if (teamIndex >= 0)
	{
		const MapModel::PropertyMap &team = sides.teams[static_cast<std::size_t>(teamIndex)];
		auto *teamTabs = new QTabWidget;
		teamTabs->setObjectName(QStringLiteral("SidesTeamTabs"));
		teamTabs->setEnabled(!defaultTeam);
		// A change of one value of the team; typing folds into one step.
		const auto setValue = [this, teamIndex, teamList](const char *step, std::string key, MapModel::PropertyValue value, bool erase, bool mergeable) {
			m_editor.editSides(step, [teamIndex, key, value, erase](MapModel::Map &m) {
				MapModel::PropertyMap &t = m.sides->teams[static_cast<std::size_t>(teamIndex)];
				if (erase)
					return t.erase(key);
				t.set(key, value);
				return true;
			}, mergeable, false);
			// The list shows some of the values.
			if (QTreeWidgetItem *item = teamList->currentItem(); item && m_editor.openMap()->map().sides)
			{
				const MapModel::PropertyMap &t = m_editor.openMap()->map().sides->teams[static_cast<std::size_t>(teamIndex)];
				item->setText(1, utf8(t.getAscii("teamHome").value_or("")));
				item->setText(2, QString::number(t.getInt("teamProductionPriority").value_or(0)));
				item->setText(3, utf8(t.getAscii("teamOnCreateScript").value_or("")));
				item->setText(4, utf8(t.getAscii("teamProductionCondition").value_or("")));
			}
		};
		const auto intField = [&team, setValue](const char *key, int lo, int hi) {
			auto *spin = new QSpinBox;
			spin->setObjectName(QStringLiteral("Team_%1").arg(QString::fromLatin1(key)));
			spin->setRange(lo, hi);
			spin->setValue(team.getInt(key).value_or(0));
			QObject::connect(spin, &QSpinBox::valueChanged, spin, [setValue, key](int v) {
				setValue("Change team", key, MapModel::PropertyValue(static_cast<std::int32_t>(v)), false, true);
			});
			return spin;
		};
		const auto boolField = [&team, setValue](const char *key, const QString &text) {
			auto *check = new QCheckBox(text);
			check->setObjectName(QStringLiteral("Team_%1").arg(QString::fromLatin1(key)));
			check->setChecked(team.getBool(key).value_or(false));
			QObject::connect(check, &QCheckBox::toggled, check, [setValue, key](bool on) {
				setValue("Change team", key, MapModel::PropertyValue(on), false, false);
			});
			return check;
		};
		const auto textField = [&team, setValue](const char *key) {
			auto *edit = new QLineEdit(utf8(team.getAscii(key).value_or("")));
			edit->setObjectName(QStringLiteral("Team_%1").arg(QString::fromLatin1(key)));
			QObject::connect(edit, &QLineEdit::textEdited, edit, [setValue, key](const QString &text) {
				setValue("Change team", key, MapModel::PropertyValue(ascii(text)), false, true);
			});
			return edit;
		};
		// A choice from a list; "(none)" removes the value. A value not in the list stays, marked with *.
		const auto choiceField = [&team, setValue](const std::string &key, const QStringList &items, bool editable) {
			auto *combo = new QComboBox;
			combo->setObjectName(QStringLiteral("Team_%1").arg(QString::fromStdString(key)));
			combo->addItem(T("(none)"));
			combo->addItems(items);
			combo->setEditable(editable);
			if (editable)
				combo->completer()->setFilterMode(Qt::MatchContains);
			const QString current = utf8(team.getAscii(key).value_or(""));
			if (!current.isEmpty())
			{
				int row = combo->findText(current);
				if (row < 0)
				{
					combo->addItem(QStringLiteral("*") + current, current);
					row = combo->count() - 1;
				}
				combo->setCurrentIndex(row);
			}
			const auto commit = [combo, setValue, key] {
				const int row = combo->currentIndex();
				QString value = combo->itemData(row).isValid() ? combo->itemData(row).toString() : combo->currentText();
				if (row == 0 && combo->currentText() == T("(none)"))
					value.clear();
				setValue("Change team", key, MapModel::PropertyValue(ascii(value)), value.isEmpty(), false);
			};
			QObject::connect(combo, &QComboBox::activated, combo, commit);
			return combo;
		};
		const QStringList scripts = subroutineScripts(map);
		const QStringList waypoints = waypointNames(map);
		QStringList units;
		QStringList transports;
		for (const MeTemplateInfo &info : engine.catalog())
		{
			if (info.sorting == 2 || info.sorting == 3)
				units << QString::fromLatin1(info.name);
			if (info.sorting == 3)
				transports << QString::fromLatin1(info.name);
		}
		units.sort(Qt::CaseInsensitive);
		transports.sort(Qt::CaseInsensitive);
		const auto page = [teamTabs](const QString &title) {
			auto *scroll = new QScrollArea;
			scroll->setWidgetResizable(true);
			auto *inner = new QWidget;
			auto *form = new QFormLayout(inner);
			scroll->setWidget(inner);
			teamTabs->addTab(scroll, title);
			return form;
		};

		QFormLayout *identity = page(T("Identity"));
		auto *teamNameEdit = new QLineEdit(utf8(MapEditorCore::teamName(team)));
		teamNameEdit->setObjectName(QStringLiteral("SidesTeamName"));
		connect(teamNameEdit, &QLineEdit::editingFinished, this, [this, teamNameEdit, teamIndex] {
			const std::string text = ascii(teamNameEdit->text().trimmed());
			const MapModel::Map &m = m_editor.openMap()->map();
			if (text == MapEditorCore::teamName(m.sides->teams[static_cast<std::size_t>(teamIndex)]))
				return;
			if (!m_editor.editSides("Rename team", [teamIndex, text](MapModel::Map &mm) { return MapEditorCore::renameTeam(mm, teamIndex, text); }))
				m_editor.toast(T("The name is empty, taken or has a space."));
			QTimer::singleShot(0, this, [this] { refresh(); });
		});
		identity->addRow(T("Name"), teamNameEdit);
		auto *teamOwnerBox = new QComboBox;
		teamOwnerBox->setObjectName(QStringLiteral("SidesTeamOwner"));
		for (const MapModel::Side &s : sides.sides)
			teamOwnerBox->addItem(playerLabel(s), utf8(MapEditorCore::playerName(s)));
		teamOwnerBox->setCurrentIndex(playerIndex);
		connect(teamOwnerBox, &QComboBox::activated, this, [this, teamOwnerBox, teamIndex](int row) {
			const std::string newOwner = ascii(teamOwnerBox->itemData(row).toString());
			m_editor.editSides("Change team owner", [teamIndex, newOwner](MapModel::Map &m) {
				m.sides->teams[static_cast<std::size_t>(teamIndex)].set("teamOwner", newOwner);
				return true;
			});
			m_player = row;
			refresh();
		});
		identity->addRow(T("Owner"), teamOwnerBox);
		identity->addRow(T("Description"), textField("teamDescription"));
		identity->addRow(T("Max instances"), intField("teamMaxInstances", 0, 1000));
		identity->addRow(QString(), boolField("teamIsSingleton", T("Only one instance (singleton)")));
		identity->addRow(T("Home waypoint"), choiceField("teamHome", waypoints, false));
		for (int u = 1; u <= 7; ++u)
		{
			const std::string n = std::to_string(u);
			auto *row = new QHBoxLayout;
			row->addWidget(choiceField("teamUnitType" + n, units, true), 1);
			static const char *const MINS[] = {"teamUnitMinCount1", "teamUnitMinCount2", "teamUnitMinCount3", "teamUnitMinCount4",
				"teamUnitMinCount5", "teamUnitMinCount6", "teamUnitMinCount7"};
			static const char *const MAXS[] = {"teamUnitMaxCount1", "teamUnitMaxCount2", "teamUnitMaxCount3", "teamUnitMaxCount4",
				"teamUnitMaxCount5", "teamUnitMaxCount6", "teamUnitMaxCount7"};
			row->addWidget(label(T("min"), "Meta"));
			row->addWidget(intField(MINS[u - 1], 0, 100));
			row->addWidget(label(T("max"), "Meta"));
			row->addWidget(intField(MAXS[u - 1], 0, 100));
			identity->addRow(T("Unit %1").arg(u), row);
		}
		identity->addRow(T("Production priority"), intField("teamProductionPriority", -10000, 10000));
		identity->addRow(T("Priority on success"), intField("teamProductionPrioritySuccessIncrease", -10000, 10000));
		identity->addRow(T("Priority on failure"), intField("teamProductionPriorityFailureDecrease", -10000, 10000));
		identity->addRow(T("Production condition"), choiceField("teamProductionCondition", scripts, false));
		identity->addRow(QString(), boolField("teamExecutesActionsOnCreate", T("Run the condition's actions when the team is built")));
		identity->addRow(T("Initial idle frames"), intField("teamInitialIdleFrames", 0, 100000));
		identity->addRow(QString(), boolField("teamIsAIRecruitable", T("The AI may recruit its units")));
		identity->addRow(QString(), boolField("teamAutoReinforce", T("Reinforce automatically")));

		QFormLayout *reinforce = page(T("Reinforcements"));
		reinforce->addRow(T("Transport"), choiceField("teamTransport", transports, true));
		reinforce->addRow(T("Origin"), choiceField("teamReinforcementOrigin", waypoints, false));
		auto *veterancy = new QComboBox;
		veterancy->setObjectName(QStringLiteral("Team_teamVeterancy"));
		veterancy->addItems({T("Normal"), T("Veteran"), T("Elite"), T("Heroic")});
		veterancy->setCurrentIndex(std::clamp(team.getInt("teamVeterancy").value_or(0), 0, 3));
		connect(veterancy, &QComboBox::activated, this, [setValue](int v) {
			setValue("Change team", "teamVeterancy", MapModel::PropertyValue(static_cast<std::int32_t>(v)), false, false);
		});
		reinforce->addRow(T("Veterancy"), veterancy);
		reinforce->addRow(QString(), boolField("teamTransportsExit", T("Transports leave after unloading")));
		reinforce->addRow(QString(), boolField("teamStartsFull", T("Transports start full")));

		QFormLayout *behavior = page(T("Behaviour"));
		behavior->addRow(T("On create"), choiceField("teamOnCreateScript", scripts, false));
		behavior->addRow(T("On enemy sighted"), choiceField("teamEnemySightedScript", scripts, false));
		behavior->addRow(T("On all clear"), choiceField("teamAllClearScript", scripts, false));
		behavior->addRow(T("On idle"), choiceField("teamOnIdleScript", scripts, false));
		behavior->addRow(T("On unit destroyed"), choiceField("teamOnUnitDestroyedScript", scripts, false));
		behavior->addRow(T("On destroyed"), choiceField("teamOnDestroyedScript", scripts, false));
		auto *threshold = new QSpinBox;
		threshold->setObjectName(QStringLiteral("Team_teamDestroyedThreshold"));
		threshold->setRange(0, 100);
		threshold->setSuffix(QStringLiteral(" %"));
		threshold->setValue(static_cast<int>(team.getReal("teamDestroyedThreshold").value_or(0.0f) * 100.0f + 0.5f));
		connect(threshold, &QSpinBox::valueChanged, this, [setValue](int v) {
			setValue("Change team", "teamDestroyedThreshold", MapModel::PropertyValue(static_cast<float>(v) / 100.0f), false, true);
		});
		behavior->addRow(T("Destroyed at"), threshold);
		auto *attitude = new QComboBox;
		attitude->setObjectName(QStringLiteral("Team_teamAggressiveness"));
		attitude->addItems({T("Sleep"), T("Passive"), T("Normal"), T("Alert"), T("Aggressive")});
		attitude->setCurrentIndex(std::clamp(team.getInt("teamAggressiveness").value_or(0) + 2, 0, 4));
		connect(attitude, &QComboBox::activated, this, [setValue](int v) {
			setValue("Change team", "teamAggressiveness", MapModel::PropertyValue(static_cast<std::int32_t>(v - 2)), false, false);
		});
		behavior->addRow(T("Attitude"), attitude);
		behavior->addRow(QString(), boolField("teamAvoidThreats", T("Avoid threats")));
		behavior->addRow(QString(), boolField("teamAttackCommonTarget", T("Attack a common target")));
		behavior->addRow(QString(), boolField("teamTransportsReturn", T("Transports return")));
		behavior->addRow(QString(), boolField("teamIsBaseDefense", T("Base defence")));
		behavior->addRow(QString(), boolField("teamIsPerimeterDefense", T("Perimeter defence")));

		QFormLayout *generic = page(T("Generic scripts"));
		for (int g = 0; g < 16; ++g)
			generic->addRow(T("Script %1").arg(g + 1), choiceField("teamGenericScriptHook" + std::to_string(g), scripts, false));
		teamTabs->setCurrentIndex(std::clamp(m_teamTab, 0, teamTabs->count() - 1));
		connect(teamTabs, &QTabWidget::currentChanged, this, [this](int i) { m_teamTab = i; });
		tcol->addWidget(teamTabs, 3);
		if (defaultTeam)
			tcol->addWidget(label(T("The default team of a player holds its units; the game sets its values."), "Hint"));
	}
	tabs->addTab(teams, T("Teams"));
	tabs->setCurrentIndex(std::clamp(m_tab, 0, tabs->count() - 1));
	connect(tabs, &QTabWidget::currentChanged, this, [this](int i) { m_tab = i; });
}

} // namespace MapEditorApp
