/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowProps.cpp -- the properties of the selection in the side panel: the fields of the old
// WorldBuilder's object dialog (team, health, veterancy, behaviour, weather and time model, the
// switches), script and waypoint names, and every property of a single object as an editable list.
// With several objects selected, a field shows "(mixed)" where they differ and an edit applies to
// all of them. Every edit is one step in the history.

#include "App/Common.h"
#include "App/MainWindow.h"

#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/Widgets.h"
#include "Core/WaypointCommands.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCompleter>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>

namespace MapEditorApp
{

namespace
{

// The team of neutral objects, as the game and the old WorldBuilder name it.

std::string toUtf8(const QString &s)
{
	const QByteArray bytes = s.toUtf8();
	return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

bool isWaypoint(const MapModel::MapObject &o)
{
	return o.properties.getInt("waypointID").has_value();
}

// The value of a key over several objects: the same everywhere (value, or none if absent
// everywhere), or mixed.
struct Shared
{
	bool mixed = false;
	std::optional<MapModel::PropertyValue> value;
};

Shared sharedValue(const std::vector<const MapModel::MapObject *> &objects, const std::string &key)
{
	Shared s;
	bool first = true;
	for (const MapModel::MapObject *o : objects)
	{
		const MapModel::PropertyValue *v = o->properties.find(key);
		std::optional<MapModel::PropertyValue> value = v ? std::optional<MapModel::PropertyValue>(*v) : std::nullopt;
		if (first)
			s.value = value;
		else if (value != s.value)
		{
			s.mixed = true;
			s.value.reset();
			return s;
		}
		first = false;
	}
	return s;
}

int intOr(const Shared &s, int fallback)
{
	if (s.value && std::holds_alternative<std::int32_t>(*s.value))
		return std::get<std::int32_t>(*s.value);
	return fallback;
}

bool boolOr(const Shared &s, bool fallback)
{
	if (s.value && std::holds_alternative<bool>(*s.value))
		return std::get<bool>(*s.value);
	return fallback;
}

QString valueText(const MapModel::PropertyValue &value)
{
	return std::visit([](const auto &v) -> QString {
		using V = std::decay_t<decltype(v)>;
		if constexpr (std::is_same_v<V, bool>)
			return v ? QStringLiteral("true") : QStringLiteral("false");
		else if constexpr (std::is_same_v<V, std::int32_t>)
			return QString::number(v);
		else if constexpr (std::is_same_v<V, float>)
			return QString::number(static_cast<double>(v), 'g', 7);
		else if constexpr (std::is_same_v<V, std::string>)
			return utf8(v);
		else
			return QString::fromStdU16String(v);
	}, value);
}

// Text typed into the list, read as the type the property already has; nothing if it does not fit.
std::optional<MapModel::PropertyValue> parseAs(const MapModel::PropertyValue &type, const QString &text)
{
	const QString t = text.trimmed();
	bool ok = false;
	switch (type.index())
	{
	case 0:	// bool
		if (t.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0 || t == QStringLiteral("1"))
			return MapModel::PropertyValue(true);
		if (t.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0 || t == QStringLiteral("0"))
			return MapModel::PropertyValue(false);
		return std::nullopt;
	case 1:	// int
	{
		const int v = t.toInt(&ok);
		return ok ? std::optional<MapModel::PropertyValue>(static_cast<std::int32_t>(v)) : std::nullopt;
	}
	case 2:	// real
	{
		const float v = t.toFloat(&ok);
		return ok && std::isfinite(v) ? std::optional<MapModel::PropertyValue>(v) : std::nullopt;
	}
	case 3:
		return MapModel::PropertyValue(toUtf8(text));
	default:
		return MapModel::PropertyValue(text.toStdU16String());
	}
}

} // namespace

// ------------------------------------------------------------------------------------------------
// Applying an edit

bool MainWindow::setSelectedProperty(const std::string &key, const std::optional<MapModel::PropertyValue> &value,
	const std::string &commandName, bool waypoints)
{
	if (!m_state.map || !m_state.map->map().objects)
		return false;
	auto &objects = *m_state.map->map().objects;
	std::vector<MapEditorCore::EditObjects::Edit> edits;
	for (std::int32_t index : m_state.map->selection.objects)
	{
		if (index < 0 || static_cast<std::size_t>(index) >= objects.size())
			continue;
		const MapModel::MapObject &o = objects[static_cast<std::size_t>(index)];
		if (isWaypoint(o) != waypoints)
			continue;
		MapModel::MapObject after = o;
		if (value)
			after.properties.set(key, *value);
		else
			after.properties.erase(key);
		if (after != o)
			edits.push_back({index, o, after});
	}
	if (edits.empty())
		return false;
	auto command = std::make_unique<MapEditorCore::EditObjects>(commandName, std::move(edits), true);
	const MapEditorCore::Changes changes = command->changes();
	m_state.map->history.execute(std::move(command), m_state.map->map());
	applyChanges(changes);
	return true;
}

// ------------------------------------------------------------------------------------------------
// The editors

void MainWindow::addPropertyEditors(QVBoxLayout *box)
{
	if (!m_state.map || !m_state.map->map().objects)
		return;
	const auto &all = *m_state.map->map().objects;
	std::vector<const MapModel::MapObject *> objects;
	std::vector<const MapModel::MapObject *> waypoints;
	for (std::int32_t index : m_state.map->selection.objects)
	{
		if (index < 0 || static_cast<std::size_t>(index) >= all.size())
			continue;
		const MapModel::MapObject &o = all[static_cast<std::size_t>(index)];
		(isWaypoint(o) ? waypoints : objects).push_back(&o);
	}
	const bool single = objects.size() + waypoints.size() == 1;
	const QString mixed = T("(mixed)");

	auto *grid = new QGridLayout;
	grid->setHorizontalSpacing(10);
	grid->setVerticalSpacing(6);
	int row = 0;
	const auto addRow = [&grid, &row](const QString &name, QWidget *editor) {
		grid->addWidget(label(name, "Meta"), row, 0);
		grid->addWidget(editor, row, 1);
		++row;
	};

	// A combo box of fixed choices for an integer property.
	const auto intChoice = [&](const QString &name, const std::string &key, const std::string &command,
		const std::vector<std::pair<int, QString>> &choices, int fallback) {
		const Shared s = sharedValue(objects, key);
		auto *combo = new QComboBox;
		if (s.mixed)
			combo->addItem(mixed, QVariant());
		for (const auto &[value, text] : choices)
			combo->addItem(text, value);
		if (!s.mixed)
			combo->setCurrentIndex(std::max(0, combo->findData(intOr(s, fallback))));
		connect(combo, &QComboBox::activated, this, [this, combo, key, command](int i) {
			const QVariant choice = combo->itemData(i);
			if (choice.isValid())
				setSelectedProperty(key, MapModel::PropertyValue(static_cast<std::int32_t>(choice.toInt())), command, false);
		});
		addRow(name, combo);
	};

	if (!objects.empty())
	{
		box->addWidget(label(T("PROPERTIES"), "Section"));

		if (single)
		{
			auto *name = new QLineEdit(utf8(objects[0]->properties.getAscii("objectName").value_or("")));
			name->setPlaceholderText(T("none"));
			connect(name, &QLineEdit::editingFinished, this, [this, name] {
				const std::string text = toUtf8(name->text().trimmed());
				// Scripts find objects by this name, so it must be unique.
				const auto &list = *m_state.map->map().objects;
				const std::int32_t self = m_state.map->selection.objects[0];
				for (std::size_t i = 0; !text.empty() && i < list.size(); ++i)
				{
					if (static_cast<std::int32_t>(i) != self && list[i].properties.getAscii("objectName") == text)
					{
						toast(T("The name %1 is already used.").arg(utf8(text)));
						name->setText(utf8(list[static_cast<std::size_t>(self)].properties.getAscii("objectName").value_or("")));
						return;
					}
				}
				setSelectedProperty("objectName", text.empty() ? std::nullopt : std::optional<MapModel::PropertyValue>(text), "Rename object", false);
			});
			addRow(T("Script name"), name);
		}

		// Team: the teams of the map; "team" is the neutral one.
		{
			const Shared s = sharedValue(objects, "originalOwner");
			auto *combo = new QComboBox;
			if (s.mixed)
				combo->addItem(mixed, QVariant());
			combo->addItem(T("(neutral)"), QString::fromLatin1(NEUTRAL_TEAM));
			if (m_state.map->map().sides)
			{
				for (const MapModel::PropertyMap &team : m_state.map->map().sides->teams)
				{
					const std::string teamName = team.getAscii("teamName").value_or("");
					if (!teamName.empty() && teamName != NEUTRAL_TEAM)
						combo->addItem(utf8(teamName), utf8(teamName));
				}
			}
			if (!s.mixed)
			{
				const QString current = s.value && std::holds_alternative<std::string>(*s.value)
					? utf8(std::get<std::string>(*s.value)) : QString::fromLatin1(NEUTRAL_TEAM);
				int i = combo->findData(current);
				if (i < 0)
				{
					combo->addItem(current, current);	// a team the map does not list (kept as it is)
					i = combo->count() - 1;
				}
				combo->setCurrentIndex(i);
			}
			connect(combo, &QComboBox::activated, this, [this, combo](int i) {
				const QVariant choice = combo->itemData(i);
				if (choice.isValid())
					setSelectedProperty("originalOwner", MapModel::PropertyValue(toUtf8(choice.toString())), "Change team", false);
			});
			addRow(T("Team"), combo);
		}

		// Health in percent of the maximum.
		{
			const Shared s = sharedValue(objects, "objectInitialHealth");
			auto *spin = new QSpinBox;
			spin->setKeyboardTracking(false);
			spin->setRange(s.mixed ? -1 : 0, 100);
			spin->setSuffix(QStringLiteral(" %"));
			if (s.mixed)
			{
				spin->setSpecialValueText(mixed);
				spin->setValue(-1);
			}
			else
				spin->setValue(intOr(s, 100));
			connect(spin, &QSpinBox::editingFinished, this, [this, spin] {
				if (spin->value() >= 0)
					setSelectedProperty("objectInitialHealth", MapModel::PropertyValue(static_cast<std::int32_t>(spin->value())), "Change health", false);
			});
			addRow(T("Health"), spin);
		}

		intChoice(T("Veterancy"), "objectVeterancy", "Change veterancy",
			{{0, T("Regular")}, {1, T("Veteran")}, {2, T("Elite")}, {3, T("Heroic")}}, 0);
		intChoice(T("Behaviour"), "objectAggressiveness", "Change behaviour",
			{{-2, T("Sleep")}, {-1, T("Passive")}, {0, T("Normal")}, {1, T("Alert")}, {2, T("Aggressive")}}, 0);
		intChoice(T("Weather model"), "objectWeather", "Change model",
			{{0, T("As the map")}, {1, T("Normal")}, {2, T("Snow")}}, 0);
		intChoice(T("Time model"), "objectTime", "Change model",
			{{0, T("As the map")}, {1, T("Day")}, {2, T("Night")}}, 0);
		box->addLayout(grid);

		// Switches, with the game's defaults where the map leaves them out.
		struct Switch
		{
			QString text;
			const char *key;
			bool fallback;
		};
		const Switch switches[] = {
			{T("Enabled"), "objectEnabled", true},
			{T("Indestructible"), "objectIndestructible", false},
			{T("Unsellable"), "objectUnsellable", false},
			{T("Powered"), "objectPowered", true},
			{T("Targetable"), "objectTargetable", false},
			{T("Recruitable by the AI"), "objectRecruitableAI", true},
			{T("Selectable"), "objectSelectable", true},
		};
		for (const Switch &sw : switches)
		{
			const Shared s = sharedValue(objects, sw.key);
			auto *check = new QCheckBox(sw.text);
			if (s.mixed)
			{
				check->setTristate(true);
				check->setCheckState(Qt::PartiallyChecked);
			}
			else
				check->setChecked(boolOr(s, sw.fallback));
			const std::string key = sw.key;
			connect(check, &QCheckBox::clicked, this, [this, check, key](bool on) {
				check->setTristate(false);
				setSelectedProperty(key, MapModel::PropertyValue(on), "Change property", false);
			});
			box->addWidget(check);
		}
		// A flag of the object, not a property: drawn in the reflection of water.
		constexpr std::int32_t DRAWS_IN_MIRROR = 0x1;
		int reflecting = 0;
		for (const MapModel::MapObject *o : objects)
			reflecting += (o->flags & DRAWS_IN_MIRROR) ? 1 : 0;
		auto *mirror = new QCheckBox(T("Reflects in water"));
		mirror->setObjectName(QStringLiteral("ReflectsInWater"));
		if (reflecting > 0 && reflecting < static_cast<int>(objects.size()))
		{
			mirror->setTristate(true);
			mirror->setCheckState(Qt::PartiallyChecked);
		}
		else
			mirror->setChecked(reflecting > 0);
		connect(mirror, &QCheckBox::clicked, this, [this, mirror](bool on) {
			mirror->setTristate(false);
			editSelectedObjects([on](MapModel::MapObject &o) {
				o.flags = on ? (o.flags | DRAWS_IN_MIRROR) : (o.flags & ~DRAWS_IN_MIRROR);
			}, "Change property");
		});
		box->addWidget(mirror);
	}

	if (single && objects.size() == 1 && objects[0]->properties.getReal("lightHeightAboveTerrain"))
		addLightEditors(box, *objects[0]);

	if (!objects.empty())
		addSoundEditors(box, *objects[0]);

	// A single waypoint: its name (unique among the waypoints) and its path labels.
	if (single && waypoints.size() == 1)
	{
		box->addWidget(label(T("WAYPOINT"), "Section"));
		auto *wgrid = new QGridLayout;
		wgrid->setHorizontalSpacing(10);
		wgrid->setVerticalSpacing(6);
		const MapModel::MapObject &w = *waypoints[0];
		const auto textField = [&](int r, const QString &name, const std::string &key, bool unique) {
			auto *edit = new QLineEdit(utf8(w.properties.getAscii(key).value_or("")));
			connect(edit, &QLineEdit::editingFinished, this, [this, edit, key, unique] {
				const std::string text = toUtf8(edit->text().trimmed());
				const auto &list = *m_state.map->map().objects;
				const std::int32_t self = m_state.map->selection.objects[0];
				if (unique)
				{
					bool taken = text.empty();
					for (std::size_t i = 0; !taken && i < list.size(); ++i)
						taken = static_cast<std::int32_t>(i) != self && isWaypoint(list[i]) && list[i].properties.getAscii(key) == text;
					if (taken)
					{
						toast(text.empty() ? T("A waypoint needs a name.") : T("The name %1 is already used.").arg(utf8(text)));
						edit->setText(utf8(list[static_cast<std::size_t>(self)].properties.getAscii(key).value_or("")));
						return;
					}
				}
				setSelectedProperty(key, text.empty() ? std::nullopt : std::optional<MapModel::PropertyValue>(text), "Rename waypoint", true);
			});
			wgrid->addWidget(label(name, "Meta"), r, 0);
			wgrid->addWidget(edit, r, 1);
		};
		textField(0, T("Name"), "waypointName", true);
		// Path labels and the direction belong to the whole connected path, as in the game's scripts.
		const std::int32_t self = m_state.map->selection.objects[0];
		const auto pathField = [&](int r, const QString &name, const std::string &key) {
			auto *edit = new QLineEdit(utf8(w.properties.getAscii(key).value_or("")));
			edit->setObjectName(QStringLiteral("PathLabel%1").arg(r));
			connect(edit, &QLineEdit::editingFinished, this, [this, edit, key, self] {
				const std::string text = toUtf8(edit->text().trimmed());
				runCommand(MapEditorCore::setPathProperty(m_state.map->map(), self, key,
					text.empty() ? std::nullopt : std::optional<MapModel::PropertyValue>(text)));
			});
			wgrid->addWidget(label(name, "Meta"), r, 0);
			wgrid->addWidget(edit, r, 1);
		};
		pathField(1, T("Path label 1"), "waypointPathLabel1");
		pathField(2, T("Path label 2"), "waypointPathLabel2");
		pathField(3, T("Path label 3"), "waypointPathLabel3");
		box->addLayout(wgrid);
		auto *both = new QCheckBox(T("Path in both directions"));
		both->setChecked(w.properties.getBool("waypointPathBiDirectional").value_or(false));
		connect(both, &QCheckBox::clicked, this, [this, self](bool on) {
			runCommand(MapEditorCore::setPathProperty(m_state.map->map(), self, "waypointPathBiDirectional", MapModel::PropertyValue(on)));
		});
		box->addWidget(both);

		// Links of this waypoint; the arrow shows the direction, the button removes the link.
		box->addWidget(label(T("LINKS"), "Section"));
		const std::vector<std::int32_t> linked = MapEditorCore::linkedWaypoints(m_state.map->map(), self);
		if (linked.empty())
			box->addWidget(label(T("No links. In the waypoint tool a click on another waypoint links it; Ctrl+L links the selected ones."), "Hint"));
		const auto &list = *m_state.map->map().objects;
		const std::int32_t selfId = w.properties.getInt("waypointID").value_or(-1);
		for (std::int32_t other : linked)
		{
			const MapModel::MapObject &o = list[static_cast<std::size_t>(other)];
			const std::int32_t otherId = o.properties.getInt("waypointID").value_or(-1);
			bool out = false, in = false;
			for (const MapModel::WaypointLink &l : *m_state.map->map().waypointLinks)
			{
				out = out || (l.from == selfId && l.to == otherId);
				in = in || (l.from == otherId && l.to == selfId);
			}
			const QChar arrow(out && in ? 0x2194 : (out ? 0x2192 : 0x2190));
			auto *linkRow = new QHBoxLayout;
			linkRow->addWidget(label(QString(arrow) + QLatin1Char(' ') + utf8(o.properties.getAscii("waypointName").value_or("")), "Secondary"), 1);
			auto *remove = new QPushButton(QStringLiteral("\u00d7"));
			remove->setFixedWidth(28);
			remove->setToolTip(T("Remove link"));
			connect(remove, &QPushButton::clicked, this, [this, self, other] {
				runCommand(MapEditorCore::toggleWaypointLink(m_state.map->map(), self, other));
			});
			linkRow->addWidget(remove);
			box->addLayout(linkRow);
		}
	}

	// Every property of a single object; double-click a value to change it.
	if (single)
	{
		const MapModel::MapObject &o = objects.empty() ? *waypoints[0] : *objects[0];
		box->addWidget(label(T("All properties"), "Meta"));
		auto *props = new QTreeWidget;
		props->setHeaderHidden(true);
		props->setRootIsDecorated(false);
		props->setColumnCount(2);
		for (const MapModel::PropertyMap::Pair &p : o.properties.pairs())
		{
			if (p.key == "editorIndex")
				continue;
			auto *item = new QTreeWidgetItem(props, {utf8(p.key), valueText(p.value)});
			item->setFlags(item->flags() | Qt::ItemIsEditable);
		}
		props->resizeColumnToContents(0);
		props->setMaximumHeight(220);
		props->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
		connect(props, &QTreeWidget::itemDoubleClicked, props, [props](QTreeWidgetItem *item, int column) {
			if (column == 1)
				props->editItem(item, 1);
		});
		const bool waypoint = isWaypoint(o);
		connect(props, &QTreeWidget::itemChanged, this, [this, waypoint](QTreeWidgetItem *item, int column) {
			if (column != 1 || !m_state.map || m_state.map->selection.objects.size() != 1)
				return;
			const std::string key = toUtf8(item->text(0));
			const auto &list = *m_state.map->map().objects;
			const MapModel::MapObject &current = list[static_cast<std::size_t>(m_state.map->selection.objects[0])];
			const MapModel::PropertyValue *old = current.properties.find(key);
			if (!old)
				return;
			const std::optional<MapModel::PropertyValue> value = parseAs(*old, item->text(1));
			if (!value)
			{
				toast(T("%1 is not a valid value for %2.").arg(item->text(1), item->text(0)));
				const QSignalBlocker block(item->treeWidget());
				item->setText(1, valueText(*old));
				return;
			}
			setSelectedProperty(key, value, "Change property", waypoint);
		});
		box->addWidget(props);

		// Removing: the button or the context menu of the list. The waypoint ID makes the waypoint.
		const auto removeCurrent = [this, props, waypoint] {
			const QTreeWidgetItem *item = props->currentItem();
			if (!item)
				return;
			const std::string key = toUtf8(item->text(0));
			if (key == "waypointID")
			{
				toast(T("The waypoint ID cannot be removed."));
				return;
			}
			setSelectedProperty(key, std::nullopt, "Remove property", waypoint);
		};
		props->setContextMenuPolicy(Qt::CustomContextMenu);
		connect(props, &QWidget::customContextMenuRequested, this, [props, removeCurrent](const QPoint &at) {
			if (!props->itemAt(at))
				return;
			QMenu menu;
			menu.addAction(T("Remove property"), removeCurrent);
			menu.exec(props->viewport()->mapToGlobal(at));
		});

		// Adding: name (suggested from the keys of the map), type, value.
		std::map<std::string, std::size_t> knownTypes;
		for (const MapModel::MapObject &other : *m_state.map->map().objects)
		{
			for (const MapModel::PropertyMap::Pair &p : other.properties.pairs())
			{
				if (p.key != "editorIndex")
					knownTypes.emplace(p.key, p.value.index());
			}
		}
		QStringList names;
		for (const auto &[key, type] : knownTypes)
			names << utf8(key);
		auto *addGrid = new QGridLayout;
		addGrid->setHorizontalSpacing(6);
		addGrid->setVerticalSpacing(6);
		auto *keyEdit = new QLineEdit;
		keyEdit->setObjectName(QStringLiteral("NewPropertyName"));
		keyEdit->setPlaceholderText(T("New property"));
		auto *completer = new QCompleter(names, keyEdit);
		completer->setCaseSensitivity(Qt::CaseInsensitive);
		completer->setFilterMode(Qt::MatchContains);
		keyEdit->setCompleter(completer);
		auto *typeBox = new QComboBox;
		typeBox->setObjectName(QStringLiteral("NewPropertyType"));
		typeBox->addItems({T("Yes/no"), T("Whole number"), T("Number"), T("Text"), T("Unicode text")});
		auto *valueEdit = new QLineEdit;
		valueEdit->setObjectName(QStringLiteral("NewPropertyValue"));
		valueEdit->setPlaceholderText(T("Value"));
		auto *add = new QPushButton(T("Add"));
		add->setObjectName(QStringLiteral("NewPropertyAdd"));
		auto *remove = new QPushButton(T("Remove"));
		remove->setObjectName(QStringLiteral("RemoveProperty"));
		remove->setEnabled(false);
		connect(props, &QTreeWidget::currentItemChanged, remove, [remove](QTreeWidgetItem *current, QTreeWidgetItem *) { remove->setEnabled(current != nullptr); });
		connect(remove, &QPushButton::clicked, this, removeCurrent);
		connect(keyEdit, &QLineEdit::textChanged, typeBox, [typeBox, knownTypes](const QString &text) {
			const auto known = knownTypes.find(toUtf8(text.trimmed()));
			if (known != knownTypes.end())
				typeBox->setCurrentIndex(static_cast<int>(known->second));
		});
		connect(add, &QPushButton::clicked, this, [this, keyEdit, typeBox, valueEdit, waypoint] {
			const std::string key = toUtf8(keyEdit->text().trimmed());
			const auto &list = *m_state.map->map().objects;
			const MapModel::MapObject &current = list[static_cast<std::size_t>(m_state.map->selection.objects[0])];
			if (key.empty() || key == "editorIndex")
			{
				toast(T("The property needs a name."));
				return;
			}
			if (current.properties.find(key))
			{
				toast(T("%1 is there already; double-click its value to change it.").arg(utf8(key)));
				return;
			}
			// The chosen type as an example value, for parseAs.
			static const MapModel::PropertyValue examples[] = {MapModel::PropertyValue(false), MapModel::PropertyValue(std::int32_t(0)),
				MapModel::PropertyValue(0.0f), MapModel::PropertyValue(std::string()), MapModel::PropertyValue(std::u16string())};
			const std::optional<MapModel::PropertyValue> value = parseAs(examples[std::clamp(typeBox->currentIndex(), 0, 4)], valueEdit->text());
			if (!value)
			{
				toast(T("%1 is not a valid value for %2.").arg(valueEdit->text(), utf8(key)));
				return;
			}
			setSelectedProperty(key, value, "Add property", waypoint);
		});
		addGrid->addWidget(keyEdit, 0, 0, 1, 2);
		addGrid->addWidget(typeBox, 1, 0);
		addGrid->addWidget(valueEdit, 1, 1);
		auto *buttons = new QHBoxLayout;
		buttons->addWidget(remove);
		buttons->addStretch(1);
		buttons->addWidget(add);
		box->addLayout(addGrid);
		box->addLayout(buttons);
	}
}

void MainWindow::addLightEditors(QVBoxLayout *box, const MapModel::MapObject &light)
{
	// A point light the game adds to the scene (W3DTerrainVisual): colours as 0xRRGGBB, radii in world units.
	box->addWidget(label(T("LIGHT"), "Section"));
	auto *grid = new QGridLayout;
	grid->setHorizontalSpacing(10);
	grid->setVerticalSpacing(6);
	int row = 0;
	const MapModel::PropertyMap &p = light.properties;
	const auto real = [&](const QString &text, const char *key, double maximum) {
		auto *field = new QDoubleSpinBox;
		field->setRange(0.0, maximum);
		field->setDecimals(1);
		field->setValue(static_cast<double>(p.getReal(key).value_or(0.0f)));
		const std::string k = key;
		connect(field, &QDoubleSpinBox::editingFinished, this, [this, field, k] {
			setSelectedProperty(k, MapModel::PropertyValue(static_cast<float>(field->value())), "Change light", false);
		});
		grid->addWidget(label(text, "Secondary"), row, 0);
		grid->addWidget(field, row++, 1);
	};
	const auto colour = [&](const QString &text, const char *key) {
		const QColor current = QColor::fromRgb(static_cast<QRgb>(p.getInt(key).value_or(0)) & 0xFFFFFFu);
		auto *button = new QPushButton;
		button->setObjectName(QStringLiteral("ColourSwatch"));
		button->setStyleSheet(QStringLiteral("background: %1; min-height: 20px;").arg(current.name()));
		const std::string k = key;
		connect(button, &QPushButton::clicked, this, [this, button, current, k, text] {
			const QColor c = QColorDialog::getColor(current, button, text);
			if (c.isValid())
				setSelectedProperty(k, MapModel::PropertyValue(static_cast<std::int32_t>(c.rgb() & 0xFFFFFFu)), "Change light", false);
		});
		grid->addWidget(label(text, "Secondary"), row, 0);
		grid->addWidget(button, row++, 1);
	};
	real(T("Height above ground"), "lightHeightAboveTerrain", 1000.0);
	colour(T("Ambient colour"), "lightAmbientColor");
	colour(T("Diffuse colour"), "lightDiffuseColor");
	real(T("Full brightness up to"), "lightInnerRadius", 10000.0);
	real(T("Dark from"), "lightOuterRadius", 10000.0);
	box->addLayout(grid);
}

} // namespace MapEditorApp
