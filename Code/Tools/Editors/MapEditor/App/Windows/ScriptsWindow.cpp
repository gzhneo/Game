/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "App/Windows/ScriptsWindow.h"

#include "App/EditorContext.h"
#include "App/ScriptCatalog.h"
#include "App/Common.h"
#include "App/MainWindowParts.h"
#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include "Core/RoadCommands.h"
#include "Core/ScriptCommands.h"
#include "Core/ScriptExchange.h"
#include "Core/SidesCommands.h"
#include "Core/WaypointCommands.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCompleter>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <climits>
#include <functional>
#include <map>
#include <memory>
#include <set>

namespace MapEditorApp
{

namespace
{

namespace Param = MapEditorCore::Param;

constexpr int ROLE_SIDE = Qt::UserRole + 1;
constexpr int ROLE_GROUP = Qt::UserRole + 2;
constexpr int ROLE_SCRIPT = Qt::UserRole + 3;

MapEditorCore::ScriptPath pathOf(const QTreeWidgetItem *item)
{
	if (!item)
		return {};
	return {item->data(0, ROLE_SIDE).toInt(), item->data(0, ROLE_GROUP).toInt(), item->data(0, ROLE_SCRIPT).toInt()};
}

void setItemPath(QTreeWidgetItem *item, const MapEditorCore::ScriptPath &p)
{
	item->setData(0, ROLE_SIDE, p.side);
	item->setData(0, ROLE_GROUP, p.group);
	item->setData(0, ROLE_SCRIPT, p.script);
}

// The tree hands a drop to the editor instead of moving its items itself.
class ScriptTree : public QTreeWidget
{
public:
	std::function<void(const MapEditorCore::ScriptPath &from, const MapEditorCore::ScriptPath &to)> onDrop;

protected:
	void dropEvent(QDropEvent *event) override
	{
		const MapEditorCore::ScriptPath from = pathOf(currentItem());
		const MapEditorCore::ScriptPath to = pathOf(itemAt(event->position().toPoint()));
		event->setDropAction(Qt::IgnoreAction);
		event->accept();
		if (onDrop && from.side >= 0 && to.side >= 0 && !(from == to))
		{
			auto drop = onDrop;
			QTimer::singleShot(0, this, [drop, from, to] { drop(from, to); });
		}
	}
};

QString warningText(const std::vector<std::string> &warnings, const QString &text)
{
	QString out = text;
	for (const std::string &w : warnings)
		out += QStringLiteral("\n") + utf8(w);
	return out;
}

QString trimmedPart(const QString &part)
{
	QString p = part.trimmed();
	while (p.endsWith(QLatin1Char('_')))
		p.chop(1);
	return p;
}

// Number-valued types whose names the engine lists.
bool namedIntType(std::int32_t type)
{
	switch (type)
	{
		case Param::COMPARISON:
		case Param::RELATION:
		case Param::AI_MOOD:
		case Param::RADAR_EVENT_TYPE:
		case Param::LEFT_OR_RIGHT:
		case Param::BOOLEAN:
		case Param::KIND_OF_PARAM:
		case Param::BUILDABLE:
		case Param::SURFACES_ALLOWED:
		case Param::SHAKE_INTENSITY:
		case Param::BOUNDARY:
			return true;
		default:
			return false;
	}
}

} // namespace

ScriptsWindow::ScriptsWindow(EditorContext &editor, ScriptCatalog &catalog, QWidget *parent)
	: QDialog(parent), m_editor(editor), m_catalog(catalog)
{
	setObjectName(QStringLiteral("ScriptsWindow"));
	setWindowTitle(T("Scripts"));
	resize(1180, 760);
}

const MapModel::Map *ScriptsWindow::currentMap() const
{
	OpenMap *open = m_editor.openMap();
	return open ? &open->map() : nullptr;
}

QWidget *ScriptsWindow::parameterField(const MapModel::ScriptParameter &p, const std::function<void(const MapModel::ScriptParameter &)> &changed)
{
	auto value = std::make_shared<MapModel::ScriptParameter>(p);
	const auto commit = [value, changed] { changed(*value); };
	const MapEditorCore::ValueKind kind = MapEditorCore::valueKind(p.type);
	if (kind == MapEditorCore::ValueKind::Coord)
	{
		auto *box = new QWidget;
		auto *row = new QHBoxLayout(box);
		row->setContentsMargins(0, 0, 0, 0);
		std::vector<QDoubleSpinBox *> spins;
		for (int i = 0; i < 3; ++i)
		{
			auto *spin = new QDoubleSpinBox;
			spin->setObjectName(QStringLiteral("Coord%1").arg(i));
			spin->setRange(-1.0e6, 1.0e6);
			spin->setDecimals(2);
			spin->setValue(p.coord[static_cast<std::size_t>(i)]);
			spin->setPrefix(i == 0 ? QStringLiteral("x ") : (i == 1 ? QStringLiteral("y ") : QStringLiteral("z ")));
			QObject::connect(spin, &QDoubleSpinBox::valueChanged, box, [value, commit, i](double v) {
				value->coord[static_cast<std::size_t>(i)] = static_cast<float>(v);
				commit();
			});
			row->addWidget(spin);
			spins.push_back(spin);
		}
		// The point the camera looks at, or the first selected object.
		const auto setAll = [spins](float x, float y, float z) {
			spins[0]->setValue(x);
			spins[1]->setValue(y);
			spins[2]->setValue(z);
		};
		auto *camera = new QPushButton(T("Camera"));
		camera->setObjectName(QStringLiteral("CoordCamera"));
		camera->setEnabled(EngineClient::instance().isStarted() && m_editor.hasView());
		QObject::connect(camera, &QPushButton::clicked, box, [setAll] {
			const MeCamera c = EngineClient::instance().camera();
			setAll(c.targetX, c.targetY, c.targetZ);
		});
		row->addWidget(camera);
		auto *selected = new QPushButton(T("Selection"));
		selected->setObjectName(QStringLiteral("CoordSelection"));
		QObject::connect(selected, &QPushButton::clicked, box, [this, setAll] {
			if (const MapModel::MapObject *o = m_editor.firstSelectedObject())
				setAll(o->x, o->y, o->z);
			else
				m_editor.toast(T("Nothing is selected."));
		});
		row->addWidget(selected);
		return box;
	}
	if (p.type == Param::COLOR)
	{
		auto *button = new QPushButton;
		const auto show = [button, value] {
			const QColor c = QColor::fromRgb(static_cast<QRgb>(value->intValue) & 0xFFFFFFu);
			button->setText(c.name());
			button->setStyleSheet(QStringLiteral("QPushButton { background: %1; }").arg(c.name()));
		};
		show();
		QObject::connect(button, &QPushButton::clicked, button, [button, value, commit, show] {
			const QColor c = QColorDialog::getColor(QColor::fromRgb(static_cast<QRgb>(value->intValue) & 0xFFFFFFu), button, T("Colour"));
			if (!c.isValid())
				return;
			value->intValue = static_cast<std::int32_t>(0xFF000000u | (c.rgb() & 0xFFFFFFu));
			show();
			commit();
		});
		return button;
	}
	if (namedIntType(p.type))
	{
		auto *combo = new QComboBox;
		int count = 0;
		for (const MeNamedValue &v : EngineClient::instance().scriptValues(p.type))
		{
			// The old dialog offered only the map's playable areas.
			if (p.type == Param::BOUNDARY && m_editor.openMap() && m_editor.openMap()->map().heightMap
				&& count >= std::max<int>(1, static_cast<int>(m_editor.openMap()->map().heightMap->boundaries.size())))
				break;
			combo->addItem(utf8(v.name), v.value);
			++count;
		}
		int row = combo->findData(p.intValue);
		if (row < 0)
		{
			combo->addItem(QStringLiteral("(%1)").arg(p.intValue), p.intValue);
			row = combo->count() - 1;
		}
		combo->setCurrentIndex(row);
		QObject::connect(combo, &QComboBox::activated, combo, [combo, value, commit](int r) {
			value->intValue = combo->itemData(r).toInt();
			// The game stores a kind of by its name as well.
			if (value->type == Param::KIND_OF_PARAM)
				value->text = ascii(combo->itemText(r));
			commit();
		});
		return combo;
	}
	if (kind == MapEditorCore::ValueKind::Int)
	{
		auto *spin = new QSpinBox;
		spin->setRange(INT_MIN, INT_MAX);
		spin->setValue(p.intValue);
		QObject::connect(spin, &QSpinBox::valueChanged, spin, [value, commit](int v) {
			value->intValue = v;
			commit();
		});
		return spin;
	}
	if (kind == MapEditorCore::ValueKind::Real)
	{
		// Angles in degrees and percentages as the old dialog shows them; the map keeps radians and fractions.
		const double scale = p.type == Param::ANGLE ? 180.0 / 3.14159265358979323846 : (p.type == Param::PERCENT ? 100.0 : 1.0);
		auto *spin = new QDoubleSpinBox;
		spin->setRange(-1.0e9, 1.0e9);
		spin->setDecimals(2);
		spin->setValue(p.realValue * scale);
		if (p.type == Param::ANGLE)
			spin->setSuffix(QStringLiteral(" deg"));
		else if (p.type == Param::PERCENT)
			spin->setSuffix(QStringLiteral(" %"));
		QObject::connect(spin, &QDoubleSpinBox::valueChanged, spin, [value, commit, scale](double v) {
			value->realValue = static_cast<float>(v / scale);
			commit();
		});
		return spin;
	}
	if (p.type == Param::TEXT_STRING || p.type == Param::TEAM_STATE)
	{
		auto *edit = new QLineEdit(utf8(p.text));
		QObject::connect(edit, &QLineEdit::textEdited, edit, [value, commit](const QString &text) {
			value->text = ascii(text);
			commit();
		});
		return edit;
	}
	auto *combo = new QComboBox;
	combo->setEditable(true);
	combo->setInsertPolicy(QComboBox::NoInsert);
	combo->addItems(m_catalog.parameterChoices(currentMap(), p.type));
	// Teams, units, object types and waypoints can come from the selected object.
	const char *fromSelection = nullptr;
	if (p.type == Param::TEAM)
		fromSelection = "originalOwner";
	else if (p.type == Param::UNIT)
		fromSelection = "objectName";
	else if (p.type == Param::WAYPOINT)
		fromSelection = "waypointName";
	else if (p.type == Param::OBJECT_TYPE)
		fromSelection = "";
	combo->completer()->setFilterMode(Qt::MatchContains);
	combo->completer()->setCaseSensitivity(Qt::CaseInsensitive);
	combo->setCurrentText(utf8(p.text));
	combo->setMinimumContentsLength(24);
	QObject::connect(combo, &QComboBox::currentTextChanged, combo, [value, commit](const QString &text) {
		value->text = ascii(text);
		commit();
	});
	if (!fromSelection)
		return combo;
	auto *box = new QWidget;
	auto *row = new QHBoxLayout(box);
	row->setContentsMargins(0, 0, 0, 0);
	row->addWidget(combo, 1);
	auto *take = new QPushButton(T("From selection"));
	take->setObjectName(QStringLiteral("ParamFromSelection"));
	const std::string key = fromSelection;
	QObject::connect(take, &QPushButton::clicked, box, [this, combo, key] {
		const MapModel::MapObject *o = m_editor.firstSelectedObject();
		const std::string text = !o ? std::string() : (key.empty() ? o->templateName : o->properties.getAscii(key).value_or(""));
		if (text.empty())
			m_editor.toast(o ? T("The selected object has no such name.") : T("Nothing is selected."));
		else
			combo->setCurrentText(utf8(text));
	});
	row->addWidget(take);
	return box;
}

void ScriptsWindow::showProblems()
{
	if (!m_editor.openMap() || !m_editor.openMap()->map().sides)
		return;
	const std::vector<MapEditorCore::ScriptProblem> problems =
		MapEditorCore::scriptProblems(*m_editor.openMap()->map().sides, m_catalog.templates(), m_catalog.check(currentMap()));
	auto *dialog = new QDialog(this);
	dialog->setObjectName(QStringLiteral("ScriptProblems"));
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setWindowTitle(T("Script check"));
	dialog->resize(760, 420);
	auto *col = new QVBoxLayout(dialog);
	col->addWidget(label(problems.empty() ? T("No problems found.") : T("%1 problems; a double-click shows the script.").arg(problems.size()), "Hint"));
	auto *list = new QListWidget;
	list->setObjectName(QStringLiteral("ScriptProblemList"));
	for (const MapEditorCore::ScriptProblem &p : problems)
	{
		auto *item = new QListWidgetItem(utf8(p.text), list);
		item->setData(ROLE_SIDE, p.path.side);
		item->setData(ROLE_GROUP, p.path.group);
		item->setData(ROLE_SCRIPT, p.path.script);
	}
	connect(list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
		m_path = {item->data(ROLE_SIDE).toInt(), item->data(ROLE_GROUP).toInt(), item->data(ROLE_SCRIPT).toInt()};
		refresh();
	});
	col->addWidget(list, 1);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
	col->addWidget(buttons);
	dialog->show();
}

bool ScriptsWindow::exportFile(const QString &path, const MapEditorCore::ScriptExport &options, QString *error)
{
	const std::optional<MapModel::ScriptFile> file = MapEditorCore::exportScripts(m_editor.openMap()->map(), options);
	if (!file)
	{
		*error = T("Nothing is selected to export.");
		return false;
	}
	const MapModel::WriteResult bytes = MapModel::writeScriptFile(*file);
	if (!bytes.ok())
	{
		*error = utf8(bytes.error);
		return false;
	}
	QFile out(path);
	if (!out.open(QIODevice::WriteOnly) || out.write(reinterpret_cast<const char *>(bytes.bytes.data()), static_cast<qint64>(bytes.bytes.size())) != static_cast<qint64>(bytes.bytes.size()))
	{
		*error = T("The file cannot be written: %1").arg(path);
		return false;
	}
	return true;
}

void ScriptsWindow::exportDialog()
{
	if (!m_editor.openMap())
		return;
	QDialog dialog(this);
	dialog.setWindowTitle(T("Export scripts"));
	auto *col = new QVBoxLayout(&dialog);
	auto *all = new QCheckBox(T("All scripts (otherwise the selected script, group or player)"));
	all->setChecked(true);
	auto *waypoints = new QCheckBox(T("Waypoints the scripts name, with the waypoints linked to them"));
	waypoints->setChecked(true);
	auto *areas = new QCheckBox(T("Areas the scripts name"));
	areas->setChecked(true);
	auto *units = new QCheckBox(T("Units the scripts name, and the units of the teams they name"));
	units->setChecked(true);
	auto *players = new QCheckBox(T("Players with their settings"));
	players->setChecked(true);
	for (QCheckBox *box : {all, waypoints, areas, units, players})
		col->addWidget(box);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	col->addWidget(buttons);
	if (dialog.exec() != QDialog::Accepted)
		return;
	MapEditorCore::ScriptExport options;
	options.allScripts = all->isChecked();
	options.waypoints = waypoints->isChecked();
	options.triggers = areas->isChecked();
	options.units = units->isChecked();
	options.sides = players->isChecked();
	options.selection = m_path;
	const QString start = m_editor.mapFolder();
	const QString path = QFileDialog::getSaveFileName(this, T("Export scripts"), start, T("Script files (*.scb)"));
	if (path.isEmpty())
		return;
	QString error;
	if (!exportFile(path, options, &error))
		m_editor.toast(error);
	else
		m_editor.toast(T("Scripts exported to %1").arg(QFileInfo(path).fileName()));
}

bool ScriptsWindow::importFile(const QString &path, QString *summary, QStringList *messages)
{
	QFile in(path);
	if (!in.open(QIODevice::ReadOnly))
	{
		*summary = T("The file cannot be read: %1").arg(path);
		return false;
	}
	const QByteArray content = in.readAll();
	const MapModel::ScriptFileResult read =
		MapModel::readScriptFile(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(content.constData()), static_cast<std::size_t>(content.size())));
	if (!read.ok())
	{
		*summary = T("The script file cannot be read: %1").arg(utf8(read.error));
		return false;
	}
	MapModel::Map &map = m_editor.openMap()->map();
	const MapEditorCore::EditMapParts::Parts before = MapEditorCore::mapParts(map);
	const MapEditorCore::ScriptImportReport report = MapEditorCore::importScripts(map, read.file, std::max(0, m_path.side));
	auto step = MapEditorCore::mapPartsStep(map, before, "Import scripts");
	for (const std::string &m : report.messages)
		messages->append(utf8(m));
	*summary = T("Imported: %1 scripts, %2 groups, %3 objects, %4 areas, %5 teams, %6 players")
		.arg(report.scripts).arg(report.groups).arg(report.objects).arg(report.areas).arg(report.teams).arg(report.players);
	if (!step)
		return true;
	m_editor.recordStep(std::move(step));
	return true;
}

void ScriptsWindow::importDialog()
{
	if (!m_editor.openMap())
		return;
	const QString start = m_editor.mapFolder();
	const QString path = QFileDialog::getOpenFileName(this, T("Import scripts"), start, T("Script files (*.scb)"));
	if (path.isEmpty())
		return;
	QString summary;
	QStringList messages;
	importFile(path, &summary, &messages);
	if (messages.isEmpty())
		m_editor.toast(summary);
	else
		QMessageBox::information(this, T("Import scripts"), summary + QStringLiteral("\n\n") + messages.join(QStringLiteral("\n")));
}

void ScriptsWindow::refresh()
{
	if (!m_editor.openMap())
		return;
	QDialog *window = this;
	QList<int> sizes;
	if (auto *oldSplit = window->findChild<QSplitter *>(QStringLiteral("ScriptsSplit")))
		sizes = oldSplit->sizes();
	// Detached at once, so lookups by name find only the new fields.
	for (QWidget *child : window->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly))
	{
		child->hide();
		child->setParent(nullptr);
		child->deleteLater();
	}
	delete window->layout();
	auto *col = new QVBoxLayout(window);
	MapModel::Map &map = m_editor.openMap()->map();
	if (!map.sides)
		return;
	const MapModel::Sides &sides = *map.sides;
	auto *split = new QSplitter(Qt::Horizontal);
	split->setObjectName(QStringLiteral("ScriptsSplit"));
	col->addWidget(split, 1);
	if (m_catalog.templates().conditions.empty())
		col->addWidget(label(T("Without the game data the conditions and actions show only their names; they cannot be edited."), "Hint"));

	// Tree ---------------------------------------------------------------------------------------
	auto *left = new QWidget;
	auto *lcol = new QVBoxLayout(left);
	lcol->setContentsMargins(0, 0, 0, 0);
	auto *tree = new ScriptTree;
	tree->setObjectName(QStringLiteral("ScriptsTree"));
	tree->setHeaderHidden(true);
	tree->setDragDropMode(QAbstractItemView::InternalMove);
	tree->setDefaultDropAction(Qt::MoveAction);
	const MapEditorCore::ScriptTemplates &templates = m_catalog.templates();
	const bool haveTemplates = !templates.conditions.empty();
	const MapEditorCore::ScriptCheck check = m_catalog.check(currentMap());
	// A script with a problem (a value that does not exist, an entry the game does not know) is marked,
	// as the old dialog did with its warning icon.
	const auto scriptLabel = [&templates, &check](const MapModel::Script &s) {
		QString text = utf8(s.name);
		if (s.isSubroutine)
			text += QStringLiteral(" ") + T("(subroutine)");
		if (!s.isActive)
			text += QStringLiteral(" ") + T("(inactive)");
		return MapEditorCore::scriptHasProblems(s, templates, check) ? QStringLiteral("[!] ") + text : text;
	};
	QTreeWidgetItem *current = nullptr;
	const auto styled = [](QTreeWidgetItem *item, bool active) {
		if (!active)
		{
			QFont f = item->font(0);
			f.setItalic(true);
			item->setFont(0, f);
			item->setForeground(0, QApplication::palette().color(QPalette::Disabled, QPalette::Text));
		}
	};
	for (std::size_t s = 0; s < sides.sides.size(); ++s)
	{
		const MapModel::Side &side = sides.sides[s];
		const std::string player = MapEditorCore::playerName(side);
		auto *playerItem = new QTreeWidgetItem(tree, {player.empty() ? T("(neutral)") : utf8(player)});
		setItemPath(playerItem, {static_cast<int>(s), -1, -1});
		playerItem->setFlags(playerItem->flags() & ~Qt::ItemIsDragEnabled);
		if (m_path == MapEditorCore::ScriptPath{static_cast<int>(s), -1, -1})
			current = playerItem;
		for (std::size_t i = 0; i < side.scripts.scripts.size(); ++i)
		{
			const MapModel::Script &script = side.scripts.scripts[i];
			auto *item = new QTreeWidgetItem(playerItem, {scriptLabel(script)});
			setItemPath(item, {static_cast<int>(s), -1, static_cast<int>(i)});
			item->setToolTip(0, utf8(script.comment));
			styled(item, script.isActive);
			if (m_path == pathOf(item))
				current = item;
		}
		for (std::size_t g = 0; g < side.scripts.groups.size(); ++g)
		{
			const MapModel::ScriptGroup &group = side.scripts.groups[g];
			QString text = QStringLiteral("[") + utf8(group.name) + QStringLiteral("]");
			if (std::any_of(group.scripts.begin(), group.scripts.end(), [&templates, &check](const MapModel::Script &gs) { return MapEditorCore::scriptHasProblems(gs, templates, check); }))
				text = QStringLiteral("[!] ") + text;
			if (group.isSubroutine)
				text += QStringLiteral(" ") + T("(subroutine)");
			if (!group.isActive)
				text += QStringLiteral(" ") + T("(inactive)");
			auto *groupItem = new QTreeWidgetItem(playerItem, {text});
			setItemPath(groupItem, {static_cast<int>(s), static_cast<int>(g), -1});
			styled(groupItem, group.isActive);
			if (m_path == pathOf(groupItem))
				current = groupItem;
			for (std::size_t i = 0; i < group.scripts.size(); ++i)
			{
				const MapModel::Script &script = group.scripts[i];
				auto *item = new QTreeWidgetItem(groupItem, {scriptLabel(script)});
				setItemPath(item, {static_cast<int>(s), static_cast<int>(g), static_cast<int>(i)});
				item->setToolTip(0, utf8(script.comment));
				styled(item, script.isActive);
				if (m_path == pathOf(item))
					current = item;
			}
		}
	}
	tree->expandAll();
	if (current)
	{
		tree->setCurrentItem(current);
		tree->scrollToItem(current);
	}
	else
		m_path = {};
	connect(tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
		const MapEditorCore::ScriptPath p = pathOf(item);
		if (!item || p == m_path)
			return;
		m_path = p;
		QTimer::singleShot(0, this, [this] { refresh(); });
	});
	tree->onDrop = [this](const MapEditorCore::ScriptPath &from, const MapEditorCore::ScriptPath &to) {
		MapEditorCore::ScriptPath moved;
		m_editor.editScripts("Move script", [&moved, from, to](MapModel::Sides &s) {
			moved = MapEditorCore::moveScriptOrGroup(s, from, to);
			return moved.side >= 0;
		}, false, false);
		if (moved.side >= 0)
			m_path = moved;
		refresh();
	};
	lcol->addWidget(tree, 1);
	const auto button = [this](QLayout *row, const QString &text, const char *name, const std::function<void()> &action) {
		auto *b = new QPushButton(text);
		b->setObjectName(QString::fromLatin1(name));
		connect(b, &QPushButton::clicked, this, action);
		row->addWidget(b);
		return b;
	};
	const MapEditorCore::ScriptPath at = m_path;
	auto *row1 = new QHBoxLayout;
	button(row1, T("New script"), "ScriptNew", [this, at] {
		const MapEditorCore::ScriptPath where = at.side >= 0 ? at : MapEditorCore::ScriptPath{0, -1, -1};
		MapEditorCore::ScriptPath added;
		m_editor.editScripts("Add script", [this, &added, where](MapModel::Sides &s) {
			added = MapEditorCore::insertScript(s, where, MapEditorCore::newScript(m_catalog.templates(), MapEditorCore::freeScriptName(s, "Script")));
			return added.side >= 0;
		}, false, false);
		if (added.side >= 0)
			m_path = added;
		refresh();
	})->setEnabled(haveTemplates);
	button(row1, T("New group"), "ScriptNewGroup", [this, at] {
		const MapEditorCore::ScriptPath where = at.side >= 0 ? at : MapEditorCore::ScriptPath{0, -1, -1};
		MapEditorCore::ScriptPath added;
		m_editor.editScripts("Add script group", [&added, where](MapModel::Sides &s) {
			MapModel::ScriptGroup g;
			g.name = MapEditorCore::freeScriptName(s, "Group");
			added = MapEditorCore::insertGroup(s, where, g);
			return added.side >= 0;
		}, false, false);
		if (added.side >= 0)
			m_path = added;
		refresh();
	});
	lcol->addLayout(row1);
	auto *row2 = new QHBoxLayout;
	button(row2, T("Copy"), "ScriptCopy", [this, at] {
		MapEditorCore::ScriptPath added;
		m_editor.editScripts("Copy script", [&added, at](MapModel::Sides &s) {
			added = MapEditorCore::copyScript(s, at);
			return added.side >= 0;
		}, false, false);
		if (added.side >= 0)
			m_path = added;
		refresh();
	})->setEnabled(at.script >= 0);
	button(row2, T("Delete"), "ScriptDelete", [this, at] {
		m_path = {at.side, at.script >= 0 ? at.group : -1, -1};
		m_editor.editScripts("Delete script", [at](MapModel::Sides &s) { return MapEditorCore::removeScriptOrGroup(s, at); });
	})->setEnabled(at.script >= 0 || at.group >= 0);
	button(row2, T("Active on/off"), "ScriptToggle", [this, at] {
		m_editor.editScripts("Switch script", [at](MapModel::Sides &s) {
			if (MapModel::Script *script = MapEditorCore::scriptAt(s, at))
				script->isActive = !script->isActive;
			else if (MapModel::ScriptGroup *group = MapEditorCore::groupAt(s, at))
				group->isActive = !group->isActive;
			else
				return false;
			return true;
		});
	})->setEnabled(at.script >= 0 || at.group >= 0);
	lcol->addLayout(row2);
	auto *row3 = new QHBoxLayout;
	button(row3, T("Check"), "ScriptCheck", [this] { showProblems(); });
	button(row3, T("Patch GC"), "ScriptPatchGc", [this] {
		int changed = 0;
		const MapEditorCore::ScriptCheck gc = m_catalog.check(currentMap());
		m_editor.editScripts("Patch GC", [&changed, &gc](MapModel::Sides &s) {
			changed = MapEditorCore::patchGcObjectTypes(s, gc);
			return changed > 0;
		});
		m_editor.toast(T("%1 object types without GC_ prefix").arg(changed));
	})->setToolTip(T("Object types GC_<name> that do not exist become <name>, where that exists."));
	button(row3, T("Export..."), "ScriptExport", [this] { exportDialog(); });
	button(row3, T("Import..."), "ScriptImport", [this] { importDialog(); });
	lcol->addLayout(row3);
	auto *hint = label(T("Drag a script onto another place to move it."), "Hint");
	hint->setWordWrap(true);
	lcol->addWidget(hint);
	split->addWidget(left);

	// Editor -------------------------------------------------------------------------------------
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	auto *editor = new QWidget;
	auto *ecol = new QVBoxLayout(editor);
	scroll->setWidget(editor);
	split->addWidget(scroll);
	split->setStretchFactor(1, 2);
	if (sizes.size() == 2)
		split->setSizes(sizes);
	else
		split->setSizes({380, 800});

	const MapModel::Script *script = MapEditorCore::scriptAt(sides, at);
	const MapModel::ScriptGroup *group = at.script < 0 ? MapEditorCore::groupAt(sides, at) : nullptr;
	if (group)
	{
		auto *form = new QFormLayout;
		ecol->addLayout(form);
		auto *name = new QLineEdit(utf8(group->name));
		name->setObjectName(QStringLiteral("ScriptGroupName"));
		connect(name, &QLineEdit::editingFinished, this, [this, name, at] {
			const std::string text = ascii(name->text().trimmed());
			m_editor.editScripts("Rename script group", [at, text](MapModel::Sides &s) {
				MapModel::ScriptGroup *g = MapEditorCore::groupAt(s, at);
				if (!g || text.empty() || g->name == text)
					return false;
				g->name = text;
				return true;
			});
		});
		form->addRow(T("Name"), name);
		auto *active = new QCheckBox(T("Active"));
		active->setChecked(group->isActive);
		connect(active, &QCheckBox::toggled, this, [this, at](bool on) {
			m_editor.editScripts("Switch script", [at, on](MapModel::Sides &s) {
				MapModel::ScriptGroup *g = MapEditorCore::groupAt(s, at);
				return g && ((g->isActive = on), true);
			});
		});
		form->addRow(QString(), active);
		auto *sub = new QCheckBox(T("Subroutine (runs only when a script calls it)"));
		sub->setChecked(group->isSubroutine);
		connect(sub, &QCheckBox::toggled, this, [this, at](bool on) {
			m_editor.editScripts("Change script group", [at, on](MapModel::Sides &s) {
				MapModel::ScriptGroup *g = MapEditorCore::groupAt(s, at);
				return g && ((g->isSubroutine = on), true);
			});
		});
		form->addRow(QString(), sub);
		ecol->addStretch(1);
		return;
	}
	if (!script)
	{
		auto *h = label(T("Choose a script or group. New script adds one after the selection."), "Hint");
		h->setWordWrap(true);
		ecol->addWidget(h);
		ecol->addStretch(1);
		return;
	}

	// A change of the selected script as a step.
	const auto changeScript = [this, at](const char *step, const std::function<bool(MapModel::Script &)> &change, bool mergeable, bool refresh) {
		return m_editor.editScripts(step, [at, change](MapModel::Sides &s) {
			MapModel::Script *sc = MapEditorCore::scriptAt(s, at);
			return sc && change(*sc);
		}, mergeable, refresh);
	};
	auto *form = new QFormLayout;
	ecol->addLayout(form);
	auto *name = new QLineEdit(utf8(script->name));
	name->setObjectName(QStringLiteral("ScriptName"));
	connect(name, &QLineEdit::editingFinished, this, [name, changeScript] {
		const std::string text = ascii(name->text().trimmed());
		changeScript("Rename script", [text](MapModel::Script &s) {
			if (text.empty() || s.name == text)
				return false;
			s.name = text;
			return true;
		}, false, true);
	});
	form->addRow(T("Name"), name);
	auto *comment = new QPlainTextEdit(utf8(script->comment));
	comment->setObjectName(QStringLiteral("ScriptComment"));
	comment->setFixedHeight(60);
	connect(comment, &QPlainTextEdit::textChanged, this, [comment, changeScript] {
		const std::string text = ascii(comment->toPlainText());
		changeScript("Change comment", [text](MapModel::Script &s) {
			if (s.comment == text)
				return false;
			s.comment = text;
			return true;
		}, true, false);
	});
	form->addRow(T("Comment"), comment);
	const auto checkBox = [&changeScript](const QString &text, bool value, const char *objectName, std::function<void(MapModel::Script &, bool)> set) {
		auto *box = new QCheckBox(text);
		box->setObjectName(QString::fromLatin1(objectName));
		box->setChecked(value);
		QObject::connect(box, &QCheckBox::toggled, box, [changeScript, set](bool on) {
			changeScript("Change script", [set, on](MapModel::Script &s) {
				set(s, on);
				return true;
			}, false, true);
		});
		return box;
	};
	auto *flags = new QHBoxLayout;
	flags->addWidget(checkBox(T("Active"), script->isActive, "ScriptActive", [](MapModel::Script &s, bool on) { s.isActive = on; }));
	flags->addWidget(checkBox(T("Only once (deactivate when it ran)"), script->isOneShot, "ScriptOneShot", [](MapModel::Script &s, bool on) { s.isOneShot = on; }));
	flags->addWidget(checkBox(T("Subroutine"), script->isSubroutine, "ScriptSubroutine", [](MapModel::Script &s, bool on) { s.isSubroutine = on; }));
	flags->addStretch(1);
	form->addRow(QString(), flags);
	auto *levels = new QHBoxLayout;
	levels->addWidget(checkBox(T("Easy"), script->easy, "ScriptEasy", [](MapModel::Script &s, bool on) { s.easy = on; }));
	levels->addWidget(checkBox(T("Normal"), script->normal, "ScriptNormal", [](MapModel::Script &s, bool on) { s.normal = on; }));
	levels->addWidget(checkBox(T("Hard"), script->hard, "ScriptHard", [](MapModel::Script &s, bool on) { s.hard = on; }));
	levels->addStretch(1);
	form->addRow(T("Difficulty"), levels);
	auto *delay = new QSpinBox;
	delay->setObjectName(QStringLiteral("ScriptDelay"));
	delay->setRange(0, 100000);
	delay->setSuffix(QStringLiteral(" s"));
	delay->setSpecialValueText(T("every frame"));
	delay->setValue(script->delayEvaluationSeconds);
	connect(delay, &QSpinBox::valueChanged, this, [changeScript](int v) {
		changeScript("Change script", [v](MapModel::Script &s) {
			s.delayEvaluationSeconds = v;
			return true;
		}, true, false);
	});
	form->addRow(T("Check every"), delay);

	// Conditions: or-groups of conditions that must all hold.
	ecol->addWidget(label(T("IF"), "Section"));
	auto *conditions = new QTreeWidget;
	conditions->setObjectName(QStringLiteral("ScriptConditions"));
	conditions->setHeaderHidden(true);
	conditions->setMinimumHeight(140);
	for (std::size_t o = 0; o < script->conditions.size(); ++o)
	{
		auto *orItem = new QTreeWidgetItem(conditions, {o == 0 ? T("All of these:") : T("OR all of these:")});
		orItem->setData(0, ROLE_GROUP, static_cast<int>(o));
		orItem->setData(0, ROLE_SCRIPT, -1);
		QFont f = orItem->font(0);
		f.setBold(true);
		orItem->setFont(0, f);
		for (std::size_t c = 0; c < script->conditions[o].conditions.size(); ++c)
		{
			const MapModel::ScriptCall &call = script->conditions[o].conditions[c];
			const std::vector<std::string> warnings = MapEditorCore::callWarnings(call, false, templates, check);
			auto *item = new QTreeWidgetItem(orItem, {(warnings.empty() ? QString() : QStringLiteral("[???] ")) + m_catalog.callText(call, false)});
			item->setData(0, ROLE_GROUP, static_cast<int>(o));
			item->setData(0, ROLE_SCRIPT, static_cast<int>(c));
			item->setToolTip(0, warningText(warnings, item->text(0)));
			if (!warnings.empty())
				item->setForeground(0, QColor(0xE0, 0x6C, 0x5C));
		}
	}
	conditions->expandAll();
	ecol->addWidget(conditions);
	const auto selectedCondition = [conditions]() -> std::pair<int, int> {
		QTreeWidgetItem *item = conditions->currentItem();
		if (!item)
			return {-1, -1};
		return {item->data(0, ROLE_GROUP).toInt(), item->data(0, ROLE_SCRIPT).toInt()};
	};
	const auto editCondition = [this, changeScript, at](int o, int c) {
		const MapModel::Script *sc = MapEditorCore::scriptAt(*m_editor.openMap()->map().sides, at);
		if (!sc || o < 0 || c < 0 || o >= static_cast<int>(sc->conditions.size()) || c >= static_cast<int>(sc->conditions[static_cast<std::size_t>(o)].conditions.size()))
			return;
		editCall(false, sc->conditions[static_cast<std::size_t>(o)].conditions[static_cast<std::size_t>(c)], [changeScript, o, c](const MapModel::ScriptCall &call) {
			changeScript("Change condition", [o, c, call](MapModel::Script &s) {
				s.conditions[static_cast<std::size_t>(o)].conditions[static_cast<std::size_t>(c)] = call;
				return true;
			}, false, true);
		});
	};
	connect(conditions, &QTreeWidget::itemDoubleClicked, this, [editCondition](QTreeWidgetItem *item) {
		editCondition(item->data(0, ROLE_GROUP).toInt(), item->data(0, ROLE_SCRIPT).toInt());
	});
	auto *crow = new QHBoxLayout;
	button(crow, T("New condition"), "ConditionNew", [this, changeScript, selectedCondition] {
		const auto [o, c] = selectedCondition();
		const int trueIndex = std::max(0, m_catalog.templates().find(false, "CONDITION_TRUE"));
		editCall(false, MapEditorCore::makeCall(m_catalog.templates(), false, trueIndex), [changeScript, o, c](const MapModel::ScriptCall &call) {
			changeScript("Add condition", [o, c, call](MapModel::Script &s) {
				if (s.conditions.empty())
					s.conditions.emplace_back();
				const std::size_t orIndex = o < 0 ? s.conditions.size() - 1 : static_cast<std::size_t>(o);
				std::vector<MapModel::ScriptCall> &list = s.conditions[orIndex].conditions;
				list.insert(list.begin() + std::clamp(c < 0 ? static_cast<int>(list.size()) : c + 1, 0, static_cast<int>(list.size())), call);
				return true;
			}, false, true);
		});
	})->setEnabled(haveTemplates);
	button(crow, T("New OR"), "ConditionNewOr", [this, changeScript, selectedCondition] {
		const int o = selectedCondition().first;
		const int trueIndex = std::max(0, m_catalog.templates().find(false, "CONDITION_TRUE"));
		const MapModel::ScriptCall call = MapEditorCore::makeCall(m_catalog.templates(), false, trueIndex);
		changeScript("Add condition", [o, call](MapModel::Script &s) {
			MapModel::OrCondition alternative;
			alternative.conditions.push_back(call);
			s.conditions.insert(s.conditions.begin() + std::clamp(o < 0 ? static_cast<int>(s.conditions.size()) : o + 1, 0, static_cast<int>(s.conditions.size())), alternative);
			return true;
		}, false, true);
	})->setEnabled(haveTemplates);
	button(crow, T("Edit"), "ConditionEdit", [editCondition, selectedCondition] {
		const auto [o, c] = selectedCondition();
		editCondition(o, c);
	});
	button(crow, T("Delete"), "ConditionDelete", [changeScript, selectedCondition] {
		const auto [o, c] = selectedCondition();
		changeScript("Delete condition", [o, c](MapModel::Script &s) {
			if (o < 0 || o >= static_cast<int>(s.conditions.size()))
				return false;
			std::vector<MapModel::ScriptCall> &list = s.conditions[static_cast<std::size_t>(o)].conditions;
			if (c < 0 || list.size() <= 1)
				s.conditions.erase(s.conditions.begin() + o);
			else if (c < static_cast<int>(list.size()))
				list.erase(list.begin() + c);
			return true;
		}, false, true);
	});
	button(crow, T("Up"), "ConditionUp", [changeScript, selectedCondition] {
		const auto [o, c] = selectedCondition();
		changeScript("Move condition", [o, c](MapModel::Script &s) {
			if (o < 0 || o >= static_cast<int>(s.conditions.size()))
				return false;
			std::vector<MapModel::ScriptCall> &list = s.conditions[static_cast<std::size_t>(o)].conditions;
			if (c <= 0 || c >= static_cast<int>(list.size()))
				return false;
			std::swap(list[static_cast<std::size_t>(c)], list[static_cast<std::size_t>(c - 1)]);
			return true;
		}, false, true);
	});
	button(crow, T("Down"), "ConditionDown", [changeScript, selectedCondition] {
		const auto [o, c] = selectedCondition();
		changeScript("Move condition", [o, c](MapModel::Script &s) {
			if (o < 0 || o >= static_cast<int>(s.conditions.size()))
				return false;
			std::vector<MapModel::ScriptCall> &list = s.conditions[static_cast<std::size_t>(o)].conditions;
			if (c < 0 || c + 1 >= static_cast<int>(list.size()))
				return false;
			std::swap(list[static_cast<std::size_t>(c)], list[static_cast<std::size_t>(c + 1)]);
			return true;
		}, false, true);
	});
	crow->addStretch(1);
	ecol->addLayout(crow);
	auto *conditionComment = new QLineEdit(utf8(script->conditionComment));
	conditionComment->setObjectName(QStringLiteral("ScriptConditionComment"));
	conditionComment->setPlaceholderText(T("Comment on the conditions"));
	connect(conditionComment, &QLineEdit::textEdited, this, [changeScript](const QString &text) {
		const std::string t = ascii(text);
		changeScript("Change comment", [t](MapModel::Script &s) {
			s.conditionComment = t;
			return true;
		}, true, false);
	});
	ecol->addWidget(conditionComment);

	// Actions if true and if false.
	const auto actionList = [&](const QString &title, bool whenFalse, const char *objectName) {
		ecol->addWidget(label(title, "Section"));
		auto *list = new QListWidget;
		list->setObjectName(QString::fromLatin1(objectName));
		list->setMinimumHeight(110);
		const std::vector<MapModel::ScriptCall> &calls = whenFalse ? script->falseActions : script->actions;
		for (const MapModel::ScriptCall &call : calls)
		{
			const std::vector<std::string> warnings = MapEditorCore::callWarnings(call, true, templates, check);
			auto *item = new QListWidgetItem((warnings.empty() ? QString() : QStringLiteral("[???] ")) + m_catalog.callText(call, true), list);
			item->setToolTip(warningText(warnings, item->text()));
			if (!warnings.empty())
				item->setForeground(QColor(0xE0, 0x6C, 0x5C));
		}
		ecol->addWidget(list);
		const auto listOf = [whenFalse](MapModel::Script &s) -> std::vector<MapModel::ScriptCall> & { return whenFalse ? s.falseActions : s.actions; };
		const auto editAction = [this, changeScript, at, whenFalse, listOf](int row) {
			const MapModel::Script *sc = MapEditorCore::scriptAt(*m_editor.openMap()->map().sides, at);
			const std::vector<MapModel::ScriptCall> *existing = sc ? (whenFalse ? &sc->falseActions : &sc->actions) : nullptr;
			if (!existing || row < 0 || row >= static_cast<int>(existing->size()))
				return;
			editCall(true, (*existing)[static_cast<std::size_t>(row)], [changeScript, row, listOf](const MapModel::ScriptCall &call) {
				changeScript("Change action", [row, call, listOf](MapModel::Script &s) {
					listOf(s)[static_cast<std::size_t>(row)] = call;
					return true;
				}, false, true);
			});
		};
		connect(list, &QListWidget::itemDoubleClicked, this, [list, editAction](QListWidgetItem *item) { editAction(list->row(item)); });
		auto *row = new QHBoxLayout;
		const QString prefix = QString::fromLatin1(objectName);
		const auto named = [&prefix](const char *suffix) { return (prefix + QString::fromLatin1(suffix)).toLatin1(); };
		const QByteArray newName = named("New"), editName = named("Edit"), copyName = named("Copy"), deleteName = named("Delete"),
			upName = named("Up"), downName = named("Down");
		button(row, T("New action"), newName.constData(), [this, list, changeScript, listOf] {
			const int selected = list->currentRow();
			const int noop = std::max(0, m_catalog.templates().find(true, "NO_OP"));
			editCall(true, MapEditorCore::makeCall(m_catalog.templates(), true, noop), [changeScript, selected, listOf](const MapModel::ScriptCall &call) {
				changeScript("Add action", [selected, call, listOf](MapModel::Script &s) {
					std::vector<MapModel::ScriptCall> &l = listOf(s);
					l.insert(l.begin() + std::clamp(selected < 0 ? static_cast<int>(l.size()) : selected + 1, 0, static_cast<int>(l.size())), call);
					return true;
				}, false, true);
			});
		})->setEnabled(haveTemplates);
		button(row, T("Edit"), editName.constData(), [list, editAction] { editAction(list->currentRow()); });
		button(row, T("Copy"), copyName.constData(), [list, changeScript, listOf] {
			const int selected = list->currentRow();
			changeScript("Copy action", [selected, listOf](MapModel::Script &s) {
				std::vector<MapModel::ScriptCall> &l = listOf(s);
				if (selected < 0 || selected >= static_cast<int>(l.size()))
					return false;
				const MapModel::ScriptCall copy = l[static_cast<std::size_t>(selected)];
				l.insert(l.begin() + selected + 1, copy);
				return true;
			}, false, true);
		});
		button(row, T("Delete"), deleteName.constData(), [list, changeScript, listOf] {
			const int selected = list->currentRow();
			changeScript("Delete action", [selected, listOf](MapModel::Script &s) {
				std::vector<MapModel::ScriptCall> &l = listOf(s);
				if (selected < 0 || selected >= static_cast<int>(l.size()))
					return false;
				l.erase(l.begin() + selected);
				return true;
			}, false, true);
		});
		button(row, T("Up"), upName.constData(), [list, changeScript, listOf] {
			const int selected = list->currentRow();
			changeScript("Move action", [selected, listOf](MapModel::Script &s) {
				std::vector<MapModel::ScriptCall> &l = listOf(s);
				if (selected <= 0 || selected >= static_cast<int>(l.size()))
					return false;
				std::swap(l[static_cast<std::size_t>(selected)], l[static_cast<std::size_t>(selected - 1)]);
				return true;
			}, false, true);
		});
		button(row, T("Down"), downName.constData(), [list, changeScript, listOf] {
			const int selected = list->currentRow();
			changeScript("Move action", [selected, listOf](MapModel::Script &s) {
				std::vector<MapModel::ScriptCall> &l = listOf(s);
				if (selected < 0 || selected + 1 >= static_cast<int>(l.size()))
					return false;
				std::swap(l[static_cast<std::size_t>(selected)], l[static_cast<std::size_t>(selected + 1)]);
				return true;
			}, false, true);
		});
		row->addStretch(1);
		ecol->addLayout(row);
	};
	actionList(T("THEN"), false, "ScriptActions");
	auto *actionComment = new QLineEdit(utf8(script->actionComment));
	actionComment->setObjectName(QStringLiteral("ScriptActionComment"));
	actionComment->setPlaceholderText(T("Comment on the actions"));
	connect(actionComment, &QLineEdit::textEdited, this, [changeScript](const QString &text) {
		const std::string t = ascii(text);
		changeScript("Change comment", [t](MapModel::Script &s) {
			s.actionComment = t;
			return true;
		}, true, false);
	});
	ecol->addWidget(actionComment);
	actionList(T("ELSE (when the conditions do not hold)"), true, "ScriptFalseActions");
	ecol->addStretch(1);
}

void ScriptsWindow::editCall(bool action, MapModel::ScriptCall call, std::function<void(const MapModel::ScriptCall &)> done)
{
	const MapEditorCore::ScriptTemplates &templates = m_catalog.templates();
	auto state = std::make_shared<MapModel::ScriptCall>(std::move(call));
	auto *dialog = new QDialog(this);
	dialog->setObjectName(QStringLiteral("ScriptCallDialog"));
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setWindowTitle(action ? T("Action") : T("Condition"));
	dialog->resize(1000, 620);
	auto *col = new QVBoxLayout(dialog);
	auto *split = new QSplitter(Qt::Horizontal);
	col->addWidget(split, 1);

	// Menu tree of the templates, as the engine names them ("Team_/Move/Move to a waypoint.").
	auto *left = new QWidget;
	auto *lcol = new QVBoxLayout(left);
	lcol->setContentsMargins(0, 0, 0, 0);
	auto *search = new QLineEdit;
	search->setObjectName(QStringLiteral("ScriptCallSearch"));
	search->setPlaceholderText(T("Search"));
	search->setClearButtonEnabled(true);
	lcol->addWidget(search);
	auto *tree = new QTreeWidget;
	tree->setObjectName(QStringLiteral("ScriptCallTemplates"));
	tree->setHeaderHidden(true);
	lcol->addWidget(tree, 1);
	split->addWidget(left);
	const std::vector<MapEditorCore::ScriptTemplate> &list = action ? templates.actions : templates.conditions;
	std::map<QString, QTreeWidgetItem *> folders;
	QTreeWidgetItem *currentLeaf = nullptr;
	const MapEditorCore::ScriptTemplate *currentTemplate = MapEditorCore::callTemplate(templates, action, *state);
	for (std::size_t i = 0; i < list.size(); ++i)
	{
		if (!list[i].valid())
			continue;
		for (const std::string &path : {list[i].uiName, list[i].uiName2})
		{
			if (path.empty())
				continue;
			const QStringList parts = utf8(path).split(QLatin1Char('/'));
			QTreeWidgetItem *parent = nullptr;
			QString key;
			for (int p = 0; p + 1 < parts.size(); ++p)
			{
				key += QLatin1Char('/') + trimmedPart(parts[p]);
				auto it = folders.find(key);
				if (it == folders.end())
				{
					auto *folder = parent ? new QTreeWidgetItem(parent, {trimmedPart(parts[p])}) : new QTreeWidgetItem(tree, {trimmedPart(parts[p])});
					folder->setData(0, ROLE_TEMPLATE, -1);
					folder->setFlags(folder->flags() & ~Qt::ItemIsSelectable);
					it = folders.emplace(key, folder).first;
				}
				parent = it->second;
			}
			// The script engine appends the template's number ("[308]") to every menu name.
			QString leafText = parts.isEmpty() ? QString() : parts.last().trimmed();
			leafText.remove(QRegularExpression(QStringLiteral("\\[\\d+\\]$")));
			auto *leaf = parent ? new QTreeWidgetItem(parent, {leafText}) : new QTreeWidgetItem(tree, {leafText});
			leaf->setData(0, ROLE_TEMPLATE, static_cast<int>(i));
			leaf->setToolTip(0, utf8(list[i].internalName));
			if (&list[i] == currentTemplate && !currentLeaf)
				currentLeaf = leaf;
		}
	}
	tree->sortItems(0, Qt::AscendingOrder);
	connect(search, &QLineEdit::textChanged, dialog, [tree](const QString &text) {
		// An entry shows when it, a folder above it or an entry below it matches.
		std::function<bool(QTreeWidgetItem *, bool)> filter = [&filter, &text](QTreeWidgetItem *item, bool above) {
			const bool self = text.isEmpty() || item->text(0).contains(text, Qt::CaseInsensitive) || item->toolTip(0).contains(text, Qt::CaseInsensitive);
			bool below = false;
			for (int i = 0; i < item->childCount(); ++i)
				below = filter(item->child(i), above || (self && !text.isEmpty())) || below;
			const bool show = self || above || below;
			item->setHidden(!show);
			if (item->childCount() > 0)
				item->setExpanded(!text.isEmpty() && below);
			return show;
		};
		for (int i = 0; i < tree->topLevelItemCount(); ++i)
			filter(tree->topLevelItem(i), false);
	});

	// The sentence, the help and one field per parameter.
	auto *right = new QWidget;
	auto *rcol = new QVBoxLayout(right);
	auto *sentence = new QLabel;
	sentence->setObjectName(QStringLiteral("ScriptCallSentence"));
	sentence->setWordWrap(true);
	sentence->setTextInteractionFlags(Qt::TextSelectableByMouse);
	QFont big = sentence->font();
	big.setPointSizeF(big.pointSizeF() * 1.15);
	sentence->setFont(big);
	rcol->addWidget(sentence);
	auto *help = label(QString(), "Hint");
	help->setWordWrap(true);
	rcol->addWidget(help);
	auto *fields = new QWidget;
	rcol->addWidget(fields);
	rcol->addStretch(1);
	split->addWidget(right);
	split->setSizes({420, 580});
	const auto updateSentence = [this, sentence, state, action] { sentence->setText(m_catalog.callText(*state, action)); };
	std::function<void()> buildFields;
	buildFields = [this, fields, help, state, action, updateSentence, &templates]() {
		delete fields->layout();
		for (QWidget *child : fields->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly))
		{
			child->hide();
			child->setParent(nullptr);
			child->deleteLater();
		}
		auto *form = new QFormLayout(fields);
		const MapEditorCore::ScriptTemplate *t = MapEditorCore::callTemplate(templates, action, *state);
		help->setText(t ? (t->help.empty() ? utf8(t->internalName) : utf8(t->help) + QStringLiteral(" (") + utf8(t->internalName) + QStringLiteral(")"))
			: T("The game does not know this entry; it stays as it is."));
		for (std::size_t i = 0; i < state->parameters.size(); ++i)
		{
			QString caption = t && i < t->uiStrings.size() ? utf8(t->uiStrings[i]).trimmed() : QString();
			if (caption.isEmpty())
				caption = T("Value %1").arg(i + 1);
			QWidget *field = parameterField(state->parameters[i], [state, i, updateSentence](const MapModel::ScriptParameter &p) {
				if (i < state->parameters.size())
					state->parameters[i] = p;
				updateSentence();
			});
			field->setObjectName(QStringLiteral("ScriptParam%1").arg(i));
			form->addRow(caption, field);
		}
		updateSentence();
	};
	buildFields();
	if (currentLeaf)
	{
		tree->setCurrentItem(currentLeaf);
		tree->scrollToItem(currentLeaf);
	}
	connect(tree, &QTreeWidget::currentItemChanged, dialog, [this, state, action, buildFields](QTreeWidgetItem *item) {
		if (!item || item->data(0, ROLE_TEMPLATE).toInt() < 0)
			return;
		const int index = item->data(0, ROLE_TEMPLATE).toInt();
		const MapEditorCore::ScriptTemplates &tt = m_catalog.templates();
		if (MapEditorCore::callTemplate(tt, action, *state) == &(action ? tt.actions : tt.conditions)[static_cast<std::size_t>(index)])
			return;
		MapEditorCore::setCallTemplate(*state, tt, action, index);
		buildFields();
	});
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
	col->addWidget(buttons);
	connect(dialog, &QDialog::accepted, this, [state, done] { done(*state); });
	dialog->open();
}

} // namespace MapEditorApp
